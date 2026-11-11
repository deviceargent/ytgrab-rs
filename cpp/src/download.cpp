#include "download.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab {
namespace {

/// The largest range googlevideo will serve in one response. A bigger range
/// (or a plain GET of the whole file) is answered with 403.
constexpr uint64_t CHUNK_SIZE = 15ull * 1024 * 1024;
constexpr uint64_t MIN_CHUNK_SIZE = 256ull * 1024;

struct RetryLater {};
struct ShrinkChunk {};
struct StopSegment {};

std::chrono::milliseconds backoff(uint32_t attempt) {
    const uint32_t shift = std::min<uint32_t>(attempt, 5);
    return std::chrono::milliseconds(500ull * (1ull << shift));
}

std::string with_suffix(const fs::path& path, const std::string& suffix) {
    return util::utf8(path) + suffix;
}

fs::path part_path(const fs::path& dest) { return util::fspath(with_suffix(dest, ".part")); }

fs::path seg_path(const fs::path& part, size_t index) {
    return util::fspath(with_suffix(part, ".seg" + std::to_string(index)));
}

std::optional<uint64_t> existing_size(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return std::nullopt;
    uint64_t size = fs::file_size(path, ec);
    if (ec) return std::nullopt;
    return size;
}

void create_parents(const fs::path& path) {
    std::error_code ec;
    fs::path parent = path.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
}

bool replace_file(const fs::path& from, const fs::path& to) {
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) !=
           0;
}

void finalize(const fs::path& part, const fs::path& dest) {
    if (!replace_file(part, dest)) {
        throw Error::io("cannot move " + util::utf8(part) + " to " + util::utf8(dest));
    }
    // Clean up any leftover segment files.
    for (size_t i = 0;; ++i) {
        fs::path seg = seg_path(part, i);
        std::error_code ec;
        if (!fs::exists(seg, ec)) break;
        fs::remove(seg, ec);
    }
}

/// Guards a destination file against two downloaders writing at once.
class LockFile {
public:
    static LockFile acquire(const fs::path& dest) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            fs::path path = util::fspath(with_suffix(dest, ".lock"));
            HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                std::string pid = std::to_string(util::process_id()) + "\n";
                DWORD written = 0;
                WriteFile(handle, pid.data(), static_cast<DWORD>(pid.size()), &written, nullptr);
                CloseHandle(handle);
                return LockFile(path);
            }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
                throw Error::io("cannot create the lock file for " + util::utf8(dest));
            }
            // A crashed process leaves the lock behind: take it over when the
            // owner is gone.
            auto owner = lock_owner(path);
            if (owner && !util::process_alive(*owner)) {
                std::error_code ec;
                fs::remove(path, ec);
                continue;
            }
            if (owner) {
                throw Error::other("another download (pid " + std::to_string(*owner) +
                                   ") is already writing to " + util::utf8(dest));
            }
            throw Error::other("another download is already writing to " + util::utf8(dest));
        }
        throw Error::other("could not acquire the download lock for " + util::utf8(dest));
    }

    LockFile(const LockFile&) = delete;
    LockFile& operator=(const LockFile&) = delete;
    LockFile(LockFile&& other) noexcept : path_(std::move(other.path_)) {
        other.path_.clear();
    }
    ~LockFile() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove(path_, ec);
        }
    }

private:
    explicit LockFile(fs::path path) : path_(std::move(path)) {}

    static std::optional<uint32_t> lock_owner(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::string text;
        std::getline(in, text);
        try {
            return static_cast<uint32_t>(std::stoul(util::trim(text)));
        } catch (...) {
            return std::nullopt;
        }
    }

    fs::path path_;
};

bool success_status(int status) { return status >= 200 && status < 300; }

}  // namespace

// ---------------------------------------------------------------------------
// progress bar
// ---------------------------------------------------------------------------

ProgressBar::ProgressBar(std::optional<uint64_t> total, std::string label)
    : total_(total.value_or(0)),
      label_(std::move(label)),
      start_(std::chrono::steady_clock::now()),
      last_draw_(start_) {}

ProgressBar::~ProgressBar() { finish_and_clear(); }

void ProgressBar::set_length(uint64_t total) {
    std::lock_guard<std::mutex> lock(mu_);
    total_ = total;
}

void ProgressBar::set_position(uint64_t position) {
    std::lock_guard<std::mutex> lock(mu_);
    position_ = position;
    last_position_ = position;
    maybe_draw_locked();
}

void ProgressBar::inc(uint64_t delta) {
    std::lock_guard<std::mutex> lock(mu_);
    position_ += delta;
    maybe_draw_locked();
}

std::string ProgressBar::eta_locked() const {
    if (total_ == 0 || speed_ <= 0.1 || position_ >= total_) return "--:--";
    const uint64_t remaining = total_ - position_;
    const uint64_t seconds = static_cast<uint64_t>(remaining / speed_);
    char buf[32];
    if (seconds >= 3600) {
        std::snprintf(buf, sizeof(buf), "%llu:%02llu:%02llu",
                      static_cast<unsigned long long>(seconds / 3600),
                      static_cast<unsigned long long>((seconds / 60) % 60),
                      static_cast<unsigned long long>(seconds % 60));
    } else {
        std::snprintf(buf, sizeof(buf), "%02llu:%02llu",
                      static_cast<unsigned long long>(seconds / 60),
                      static_cast<unsigned long long>(seconds % 60));
    }
    return buf;
}

void ProgressBar::maybe_draw_locked() {
    const auto now = std::chrono::steady_clock::now();
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw_);
    if (drawn_ && since.count() < 100) return;
    last_draw_ = now;

    const double seconds =
        std::chrono::duration<double>(now - start_).count();
    if (seconds > 0.2) speed_ = static_cast<double>(position_) / seconds;
    draw_locked(false);
}

void ProgressBar::draw_locked(bool final_draw) {
    const uint64_t shown_total = total_ > 0 ? total_ : position_;
    std::string speed = final_draw ? "" : util::human_bytes(speed_) + "/s";
    std::string line = "\r" + (label_.empty() ? "" : label_ + ": ") + "- [" +
                       util::human_bytes(static_cast<double>(position_)) + "/" +
                       util::human_bytes(static_cast<double>(shown_total)) + "] (" +
                       eta_locked() + ") " + speed;
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
    drawn_ = true;
    last_position_ = position_;
}

void ProgressBar::finish_and_clear() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!drawn_) return;
    std::fputs("\r\033[2K", stderr);
    std::fflush(stderr);
    drawn_ = false;
}

// ---------------------------------------------------------------------------
// downloader
// ---------------------------------------------------------------------------

Downloader::Downloader(std::string user_agent, const CookieJar* jar)
    : user_agent_(std::move(user_agent)), jar_(jar) {}

http::Client Downloader::make_client() const { return http::Client(user_agent_); }

std::string Downloader::get_bytes(const std::string& url) {
    http::Client client = make_client();
    if (jar_ && !jar_->empty()) client.set_cookies(jar_->header_for(url));

    http::Response response =
        client.get(url, {{"Accept-Language", "en-US,en;q=0.9"}});
    if (!success_status(response.status)) {
        throw Error::other("request failed with HTTP " + std::to_string(response.status));
    }
    return response.body;
}

std::optional<uint64_t> Downloader::probe_size(const std::string& url) {
    http::Client client = make_client();
    if (jar_ && !jar_->empty()) client.set_cookies(jar_->header_for(url));

    client.get_stream(url, {{"Accept-Encoding", "identity"}, {"Range", "bytes=0-0"}},
                      [](const char*, size_t) { return false; });
    const std::string& content_range = client.last_content_range;
    size_t slash = content_range.rfind('/');
    if (slash == std::string::npos) return std::nullopt;
    std::string total = util::trim(content_range.substr(slash + 1));
    try {
        return static_cast<uint64_t>(std::stoull(total));
    } catch (...) {
        return std::nullopt;
    }
}

uint64_t Downloader::download(const std::string& url, const fs::path& dest, ProgressBar& bar,
                              std::optional<uint64_t> total_hint, size_t fragments, long long itag) {
    LockFile lock = LockFile::acquire(dest);
    if (fragments > 1 && total_hint && *total_hint > 1024 * 1024) {
        return download_segmented(url, dest, bar, *total_hint, fragments, itag);
    }
    return download_single(url, dest, bar, total_hint, itag);
}

uint64_t Downloader::download_single(const std::string& url, const fs::path& dest,
                                     ProgressBar& bar, std::optional<uint64_t> total_hint,
                                     long long itag) {
    const fs::path part = part_path(dest);
    create_parents(part);

    std::optional<uint64_t> existing = existing_size(part);
    std::optional<uint64_t> total = total_hint;
    if (!total || *total == 0) total = probe_size(url);

    // Already complete?
    if (existing && total && *existing >= *total) {
        finalize(part, dest);
        bar.set_position(*total);
        return 0;
    }
    if (total) bar.set_length(*total);

    http::Client client = make_client();
    if (jar_ && !jar_->empty()) client.set_cookies(jar_->header_for(url));

    uint32_t attempt = 0;
    uint64_t chunk = CHUNK_SIZE;

    while (true) {
        const uint64_t offset = existing.value_or(0);
        if (total && offset >= *total) break;

        const uint64_t end =
            total ? std::min(offset + chunk - 1, *total - 1) : offset + chunk - 1;

        std::ofstream file;
        uint64_t written = 0;

        try {
            // The Range header is always sent: without it googlevideo 403s.
            client.get_stream(
                url,
                {{"Accept-Encoding", "identity"},
                 {"Range", "bytes=" + std::to_string(offset) + "-" + std::to_string(end)}},
                [&](const char* data, size_t len) {
                    if (!file.is_open()) return true;
                    file.write(data, static_cast<std::streamsize>(len));
                    written += len;
                    bar.inc(len);
                    return static_cast<bool>(file);
                },
                [&](int status, const std::string&) {
                    if (status == 403 || status == 410) {
                        // 403 from googlevideo usually means the range was too
                        // large. Ask for less before giving up.
                        if (status == 403 && chunk > MIN_CHUNK_SIZE) {
                            chunk = std::max(chunk / 2, MIN_CHUNK_SIZE);
                            throw ShrinkChunk{};
                        }
                        throw Error::signed_url(status, itag);
                    }
                    if (!success_status(status)) {
                        ++attempt;
                        if (attempt > retries) {
                            throw Error::other("download failed with HTTP " +
                                               std::to_string(status));
                        }
                        throw RetryLater{};
                    }
                    const bool partial = status == 206;
                    if (!partial && offset > 0) {
                        // The server ignored our range header: start over.
                        existing = 0;
                        ++attempt;
                        if (attempt > retries) {
                            throw Error::other("server ignored the range header");
                        }
                        throw RetryLater{};
                    }
                    file.open(part, std::ios::binary | ((partial && offset > 0)
                                                            ? std::ios::app
                                                            : std::ios::trunc));
                    if (!file) throw Error::io("cannot write to " + util::utf8(part));
                });
        } catch (const ShrinkChunk&) {
            continue;
        } catch (const RetryLater&) {
            std::this_thread::sleep_for(backoff(attempt));
            continue;
        }

        if (file.is_open()) file.close();
        existing = offset + written;

        if (total) {
            if (*existing >= *total) break;
        } else if (written < (end - offset + 1)) {
            // Short read: the server has nothing more to give.
            break;
        }
    }

    finalize(part, dest);
    return existing.value_or(0);
}

namespace {

/// Fetch one byte range of a segmented download, resuming a partial segment.
void download_range(const std::string& user_agent, const CookieJar* jar, const std::string& url,
                    const fs::path& dest, uint64_t start, uint64_t end, ProgressBar& bar,
                    uint32_t retries, long long itag) {
    const uint64_t expected = end - start + 1;
    uint64_t have = existing_size(dest).value_or(0);
    if (have >= expected) return;

    http::Client client(user_agent);
    if (jar && !jar->empty()) client.set_cookies(jar->header_for(url));

    uint32_t attempt = 0;
    while (have < expected) {
        const uint64_t from = start + have;
        const uint64_t to = std::min(from + CHUNK_SIZE - 1, end);
        uint64_t written = 0;
        std::ofstream file;

        try {
            client.get_stream(
                url,
                {{"Accept-Encoding", "identity"},
                 {"Range", "bytes=" + std::to_string(from) + "-" + std::to_string(to)}},
                [&](const char* data, size_t len) {
                    if (!file.is_open()) return true;
                    file.write(data, static_cast<std::streamsize>(len));
                    written += len;
                    bar.inc(len);
                    return static_cast<bool>(file);
                },
                [&](int status, const std::string&) {
                    if (success_status(status)) {
                        file.open(dest, std::ios::binary | std::ios::app);
                        if (!file) throw Error::io("cannot write to " + util::utf8(dest));
                        return;
                    }
                    if (status == 416) throw StopSegment{};
                    if (status == 403 || status == 410) throw Error::signed_url(status, itag);
                    ++attempt;
                    if (attempt > retries) {
                        throw Error::other("segment failed: HTTP " + std::to_string(status));
                    }
                    throw RetryLater{};
                });
        } catch (const StopSegment&) {
            break;
        } catch (const RetryLater&) {
            std::this_thread::sleep_for(backoff(attempt));
            continue;
        }

        if (file.is_open()) file.close();
        have += written;

        if (written == 0) {
            ++attempt;
            if (attempt > retries) throw Error::other("segment made no progress");
            std::this_thread::sleep_for(backoff(attempt));
        } else if (written < (to - from + 1)) {
            ++attempt;
            if (attempt > retries) throw Error::other("segment kept breaking mid-transfer");
        }
    }
}

}  // namespace

uint64_t Downloader::download_segmented(const std::string& url, const fs::path& dest,
                                        ProgressBar& bar, uint64_t total, size_t fragments,
                                        long long itag) {
    const size_t n = std::max<size_t>(fragments, 1);
    const uint64_t chunk = (total + n - 1) / n;
    const fs::path part = part_path(dest);
    create_parents(part);

    std::vector<std::thread> workers;
    std::vector<std::exception_ptr> errors(n);
    workers.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        const uint64_t start = i * chunk;
        if (start >= total) break;
        const uint64_t end = std::min((i + 1) * chunk, total) - 1;
        const fs::path segment = seg_path(part, i);
        workers.emplace_back([&, i, start, end, segment]() {
            try {
                download_range(user_agent_, jar_, url, segment, start, end, bar, retries, itag);
            } catch (...) {
                errors[i] = std::current_exception();
            }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors) {
        if (error) std::rethrow_exception(error);
    }

    // Concatenate the segments into the final file.
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) throw Error::io("cannot write to " + util::utf8(part));
    std::vector<char> buffer(1 << 20);
    for (size_t i = 0;; ++i) {
        fs::path segment = seg_path(part, i);
        std::error_code ec;
        if (!fs::exists(segment, ec)) break;
        std::ifstream in(segment, std::ios::binary);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            std::streamsize got = in.gcount();
            if (got <= 0) break;
            out.write(buffer.data(), got);
        }
    }
    out.close();

    finalize(part, dest);
    return total;
}

}  // namespace ytgrab
