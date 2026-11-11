#include "browser_cookies.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "error.hpp"
#include "util.hpp"

#ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#endif

namespace ytgrab {
namespace {

using json = nlohmann::json;

struct ChromiumProfile {
    fs::path root;
};

const char* kChromiumIv = "                ";  // 16 spaces, the legacy CBC iv

// ---------------------------------------------------------------------------
// crypto
// ---------------------------------------------------------------------------

struct BCryptAlg {
    BCRYPT_ALG_HANDLE handle = nullptr;
    explicit BCryptAlg(LPCWSTR algorithm) {
        if (BCryptOpenAlgorithmProvider(&handle, algorithm, nullptr, 0) < 0) handle = nullptr;
    }
    ~BCryptAlg() {
        if (handle) BCryptCloseAlgorithmProvider(handle, 0);
    }
    BCryptAlg(const BCryptAlg&) = delete;
    BCryptAlg& operator=(const BCryptAlg&) = delete;
};

std::string sha256(const std::string& data) {
    BCryptAlg alg(BCRYPT_SHA256_ALGORITHM);
    if (!alg.handle) return {};
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg.handle, &hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
    std::string digest(32, '\0');
    bool ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                             static_cast<ULONG>(data.size()), 0) >= 0 &&
              BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), 32, 0) >= 0;
    BCryptDestroyHash(hash);
    if (!ok) return {};
    return digest;
}

/// Unwrap the DPAPI-protected AES key from `Local State`.
std::string unwrap_dpapi(const std::string& wrapped) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(wrapped.data()));
    in.cbData = static_cast<DWORD>(wrapped.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        throw Error::other("DPAPI could not unwrap the browser key (are you the same user?)");
    }
    std::string key(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return key;
}

std::string aes_gcm_decrypt(const std::string& key, const std::string& body) {
    if (body.size() < 12 + 16) throw Error::other("cookie value is too short");
    const char* nonce = body.data();
    const size_t cipher_len = body.size() - 12 - 16;
    const char* cipher = body.data() + 12;
    const char* tag = body.data() + body.size() - 16;

    BCryptAlg alg(BCRYPT_AES_ALGORITHM);
    if (!alg.handle) throw Error::other("AES is not available");

    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (BCryptGenerateSymmetricKey(alg.handle, &key_handle, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                                   static_cast<ULONG>(key.size()), 0) < 0) {
        throw Error::other("could not import the browser key");
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce));
    info.cbNonce = 12;
    info.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(tag));
    info.cbTag = 16;

    std::string plain(cipher_len, '\0');
    ULONG produced = 0;
    NTSTATUS status = BCryptDecrypt(
        key_handle, reinterpret_cast<PUCHAR>(const_cast<char*>(cipher)),
        static_cast<ULONG>(cipher_len), &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()), &produced, 0);
    BCryptDestroyKey(key_handle);
    if (status < 0) throw Error::other("cookie decryption failed (wrong key)");
    plain.resize(produced);
    return plain;
}

std::string aes_cbc_decrypt(const std::string& key, const std::string& body) {
    BCryptAlg alg(BCRYPT_AES_ALGORITHM);
    if (!alg.handle) throw Error::other("AES is not available");
    BCryptSetProperty(alg.handle, BCRYPT_CHAINING_MODE,
                      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);

    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (BCryptGenerateSymmetricKey(alg.handle, &key_handle, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                                   static_cast<ULONG>(key.size()), 0) < 0) {
        throw Error::other("could not import the browser key");
    }

    std::string iv(kChromiumIv, 16);
    std::string plain(body.size() + 32, '\0');
    ULONG produced = 0;
    NTSTATUS status = BCryptDecrypt(
        key_handle, reinterpret_cast<PUCHAR>(const_cast<char*>(body.data())),
        static_cast<ULONG>(body.size()), nullptr, reinterpret_cast<PUCHAR>(iv.data()), 16,
        reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()), &produced, 0);
    BCryptDestroyKey(key_handle);
    if (status < 0) throw Error::other("cookie decryption failed (wrong key)");
    plain.resize(produced);

    // PKCS#7 padding.
    if (plain.empty()) return plain;
    const unsigned char pad = static_cast<unsigned char>(plain.back());
    if (pad == 0 || pad > 16 || pad > plain.size()) {
        throw Error::other("cookie decryption produced bad padding");
    }
    plain.resize(plain.size() - pad);
    return plain;
}

/// Decrypt one `encrypted_value` blob with the current key.
std::string decrypt_cookie_value(const std::string& key, const std::string& host_key,
                                 const std::string& encrypted) {
    if (encrypted.empty()) return "";
    if (util::starts_with(encrypted, "v20")) {
        throw Error::other("v20 (app-bound) cookies are not supported yet");
    }
    if (!util::starts_with(encrypted, "v10") && !util::starts_with(encrypted, "v11")) {
        throw Error::other("unknown cookie encryption version");
    }
    const std::string body = encrypted.substr(3);

    std::string plain;
    try {
        plain = aes_gcm_decrypt(key, body);
    } catch (const Error&) {
        // Older profiles still use AES-256-CBC with a fixed IV.
        plain = aes_cbc_decrypt(key, body);
    }

    // Recent Chromium versions prefix the value with SHA256(host_key).
    const std::string prefix = sha256(host_key);
    if (!prefix.empty() && plain.size() > 32 && plain.compare(0, 32, prefix) == 0) {
        plain = plain.substr(32);
    }
    return plain;
}

// ---------------------------------------------------------------------------
// profile discovery
// ---------------------------------------------------------------------------

std::string env_var(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return util::narrow(value);
}

fs::path chromium_root(const std::string& browser) {
    const std::string local = env_var(L"LOCALAPPDATA");
    if (local.empty()) throw Error::other("LOCALAPPDATA is not set");
    const fs::path base = util::fspath(local);
    if (browser == "chrome") return base / "Google" / "Chrome" / "User Data";
    if (browser == "brave") return base / "BraveSoftware" / "Brave-Browser" / "User Data";
    if (browser == "chromium") return base / "Chromium" / "User Data";
    if (browser == "edge") return base / "Microsoft" / "Edge" / "User Data";
    throw Error::other("unknown browser `" + browser + "`");
}

std::optional<fs::path> cookies_db(const fs::path& root) {
    const fs::path candidates[] = {root / "Default" / "Network" / "Cookies",
                                   root / "Default" / "Cookies"};
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec)) return candidate;
    }
    return std::nullopt;
}

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error::io("cannot read " + util::utf8(path));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

fs::path temp_copy(const fs::path& source) {
    fs::path dir = fs::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path target = dir / ("ytgrab-cookies-" + std::to_string(util::process_id()) + "-" +
                             std::to_string(stamp) + ".db");
    std::error_code ec;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) throw Error::io("cannot copy the cookie database: " + ec.message());
    return target;
}

// ---------------------------------------------------------------------------
// sqlite
// ---------------------------------------------------------------------------

struct SqliteDb {
    sqlite3* db = nullptr;
    explicit SqliteDb(const fs::path& path) {
        if (sqlite3_open_v2(util::utf8(path).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
            SQLITE_OK) {
            std::string message = db ? sqlite3_errmsg(db) : "cannot open the database";
            if (db) sqlite3_close(db);
            db = nullptr;
            throw Error::db(message);
        }
    }
    ~SqliteDb() {
        if (db) sqlite3_close(db);
    }
    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
};

std::string column_text(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    int bytes = sqlite3_column_bytes(stmt, index);
    if (!text) return "";
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
}

long long chromium_timestamp(long long raw) {
    if (raw == 0) return 0;
    // Microseconds since 1601-01-01 -> seconds since the unix epoch.
    return (raw - 11644473600000000LL) / 1000000LL;
}

std::vector<BrowserCookie> read_chromium_cookies(const fs::path& db_path,
                                                 const std::string& key) {
    fs::path copy = temp_copy(db_path);
    std::vector<BrowserCookie> cookies;

    try {
        SqliteDb database(copy);
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT host_key, path, is_secure, name, value, encrypted_value, expires_utc "
            "FROM cookies";
        if (sqlite3_prepare_v2(database.db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw Error::db(sqlite3_errmsg(database.db));
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BrowserCookie cookie;
            cookie.domain = column_text(stmt, 0);
            cookie.path = column_text(stmt, 1);
            cookie.secure = sqlite3_column_int64(stmt, 2) != 0;
            cookie.name = column_text(stmt, 3);
            cookie.value = column_text(stmt, 4);
            const void* blob = sqlite3_column_blob(stmt, 5);
            const int blob_len = sqlite3_column_bytes(stmt, 5);
            std::string encrypted;
            if (blob && blob_len > 0) {
                encrypted.assign(static_cast<const char*>(blob), static_cast<size_t>(blob_len));
            }
            if (cookie.value.empty() && !encrypted.empty()) {
                try {
                    cookie.value = decrypt_cookie_value(key, cookie.domain, encrypted);
                } catch (const Error&) {
                    continue;
                }
            }
            cookie.expires = chromium_timestamp(sqlite3_column_int64(stmt, 6));
            cookies.push_back(std::move(cookie));
        }
        sqlite3_finalize(stmt);
    } catch (...) {
        std::error_code ec;
        fs::remove(copy, ec);
        throw;
    }
    std::error_code ec;
    fs::remove(copy, ec);
    return cookies;
}

std::vector<BrowserCookie> chromium_cookies(const std::string& browser) {
    const fs::path root = chromium_root(browser);
    if (!fs::exists(root)) {
        throw Error::other("no " + browser + " profile found at " + util::utf8(root));
    }
    auto db = cookies_db(root);
    if (!db) throw Error::other("no Cookies database found (browser never ran?)");

    const fs::path local_state = root / "Local State";
    if (!fs::exists(local_state)) throw Error::other("no Local State file in the profile");

    json state = json::parse(read_text_file(local_state));
    std::string encrypted_key;
    if (state.contains("os_crypt") && state["os_crypt"].is_object()) {
        encrypted_key = state["os_crypt"].value("encrypted_key", std::string());
    }
    if (encrypted_key.empty()) {
        throw Error::other("Local State has no os_crypt.encrypted_key");
    }
    std::string raw = util::base64_decode(encrypted_key);
    if (util::starts_with(raw, "DPAPI")) raw = raw.substr(5);
    std::string key = unwrap_dpapi(raw);
    if (key.size() != 32 && key.size() != 16) {
        throw Error::other("unexpected browser key length");
    }
    return read_chromium_cookies(*db, key);
}

std::vector<BrowserCookie> firefox_cookies() {
    const std::string appdata = env_var(L"APPDATA");
    if (appdata.empty()) throw Error::other("APPDATA is not set");
    const fs::path root = util::fspath(appdata) / "Mozilla" / "Firefox" / "Profiles";
    if (!fs::exists(root)) throw Error::other("no Firefox profile found");

    fs::path profile;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (fs::exists(entry.path() / "cookies.sqlite")) {
            profile = entry.path();
            break;
        }
    }
    if (profile.empty()) throw Error::other("no Firefox profile with cookies.sqlite");

    fs::path copy = temp_copy(profile / "cookies.sqlite");
    std::vector<BrowserCookie> cookies;
    try {
        SqliteDb database(copy);
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT host, path, isSecure, name, value, expiry FROM moz_cookies";
        if (sqlite3_prepare_v2(database.db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw Error::db(sqlite3_errmsg(database.db));
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BrowserCookie cookie;
            cookie.domain = column_text(stmt, 0);
            cookie.path = column_text(stmt, 1);
            cookie.secure = sqlite3_column_int64(stmt, 2) != 0;
            cookie.name = column_text(stmt, 3);
            cookie.value = column_text(stmt, 4);
            cookie.expires = sqlite3_column_int64(stmt, 5);
            cookies.push_back(std::move(cookie));
        }
        sqlite3_finalize(stmt);
    } catch (...) {
        fs::remove(copy, ec);
        throw;
    }
    fs::remove(copy, ec);
    return cookies;
}

}  // namespace

std::vector<BrowserCookie> extract_browser_cookies(const std::string& browser) {
    const std::string name = util::to_lower(browser);
    if (name == "firefox") return firefox_cookies();
    return chromium_cookies(name);
}

CookieJar to_jar(const std::vector<BrowserCookie>& cookies) {
    CookieJar jar;
    for (const auto& c : cookies) {
        std::string host = c.domain;
        while (!host.empty() && host.front() == '.') host.erase(host.begin());
        if (host.empty()) continue;
        Cookie cookie;
        cookie.domain = c.domain;
        cookie.path = c.path;
        cookie.name = c.name;
        cookie.value = c.value;
        cookie.secure = c.secure;
        cookie.expires = c.expires;
        jar.add(std::move(cookie));
    }
    return jar;
}

std::string to_netscape(const std::vector<BrowserCookie>& cookies) {
    std::string out = "# Netscape HTTP Cookie File\n";
    for (const auto& c : cookies) {
        out += c.domain + "\t" + (c.secure ? "TRUE" : "FALSE") + "\t" + c.path + "\t" +
               (c.secure ? "TRUE" : "FALSE") + "\t" + std::to_string(c.expires) + "\t" +
               c.name + "\t" + c.value + "\n";
    }
    return out;
}

}  // namespace ytgrab
