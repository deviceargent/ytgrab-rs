//! Reading cookies out of a Chromium-based browser (Chrome, Brave, Chromium,
//! Edge) and out of Firefox.
//!
//! Chromium encrypts cookie values on disk. Two schemes matter:
//!
//! * **v10** — the AES-128-CBC key is `PBKDF2-HMAC-SHA1(password, "saltysalt",
//!   1 iteration, 16 bytes)`, where the password comes from the OS keyring
//!   ("Chrome Safe Storage" / "Brave Safe Storage" / ...).
//! * **v11** — `Local State` carries `os_crypt.encrypted_key`; the keyring
//!   password is used directly as the AES key to decrypt it.
//!
//! On Linux the keyring is reached through `secret-tool` (libsecret), which
//! needs a running D-Bus session. When it is not reachable we say so rather
//! than failing cryptically.

use std::path::{Path, PathBuf};
use std::process::Command;

use aes::cipher::{block_padding::Pkcs7, BlockDecryptMut, KeyIvInit};
use rusqlite::Connection;

use crate::error::{Error, Result};

type Aes128CbcDec = cbc::Decryptor<aes::Aes128>;

const SALT: &[u8] = b"saltysalt";
const IV: &[u8; 16] = b"                ";

#[derive(Debug, Clone)]
pub struct BrowserCookie {
    pub domain: String,
    pub path: String,
    pub secure: bool,
    pub name: String,
    pub value: String,
    pub expires: i64,
}

/// Where a browser keeps its profile data.
struct BrowserProfile {
    /// Directory holding `Local State` and the profile folders.
    root: PathBuf,
    /// Name of the keyring entry that holds the encryption password.
    keyring_app: String,
}

impl BrowserProfile {
    fn detect(browser: &str) -> Result<BrowserProfile> {
        let home = dirs::home_dir().ok_or_else(|| Error::other("no home directory"))?;
        let (rel, keyring_app) = match browser.to_ascii_lowercase().as_str() {
            "chrome" => (
                ".config/google-chrome",
                "Chrome Safe Storage",
            ),
            "brave" => (
                ".config/BraveSoftware/Brave-Browser",
                "Brave Safe Storage",
            ),
            "chromium" => (".config/chromium", "Chromium Safe Storage"),
            "edge" => (
                ".config/microsoft-edge",
                "Edge Safe Storage",
            ),
            other => return Err(Error::other(format!("unknown browser `{other}`"))),
        };

        let root = home.join(rel);
        if !root.exists() {
            return Err(Error::other(format!(
                "no {browser} profile found at {}",
                root.display()
            )));
        }
        Ok(BrowserProfile {
            root,
            keyring_app: keyring_app.to_string(),
        })
    }

    /// The `Cookies` database of the default profile.
    fn cookies_db(&self) -> Option<PathBuf> {
        let candidates = [
            self.root.join("Default").join("Network").join("Cookies"),
            self.root.join("Default").join("Cookies"),
        ];
        candidates.into_iter().find(|p| p.exists())
    }

    fn local_state(&self) -> Option<PathBuf> {
        let p = self.root.join("Local State");
        if p.exists() {
            Some(p)
        } else {
            None
        }
    }
}

/// Ask the OS keyring for the browser's encryption password.
fn keyring_password(app: &str) -> Result<Vec<u8>> {
    let output = Command::new("secret-tool")
        .args(["lookup", "application", app])
        .output()
        .map_err(|e| {
            Error::other(format!(
                "cannot run secret-tool to reach the keyring ({e}); \
                 is a D-Bus session running?"
            ))
        })?;

    if !output.status.success() {
        return Err(Error::other(format!(
            "the keyring has no entry for `{app}` (is the browser unlocked?)"
        )));
    }
    Ok(output.stdout)
}

/// PBKDF2-HMAC-SHA1, 1 iteration — Chromium's (deliberately weak) KDF.
fn pbkdf2_sha1(password: &[u8], salt: &[u8]) -> [u8; 16] {
    let mut key = [0u8; 16];
    // One iteration, so this is cheap; Chromium uses it to stretch the
    // keyring password into a 16 byte AES key.
    pbkdf2::pbkdf2_hmac::<sha1::Sha1>(password, salt, 1, &mut key);
    key
}

fn decrypt_v11(encrypted_key_b64: &str, password: &[u8]) -> Result<[u8; 16]> {
    use base64::Engine;
    let raw = base64::engine::general_purpose::STANDARD
        .decode(encrypted_key_b64)
        .map_err(|e| Error::other(format!("bad encrypted_key in Local State: {e}")))?;
    let raw = raw.strip_prefix(b"DPAPI").unwrap_or(&raw);
    if raw.len() < 16 {
        return Err(Error::other("encrypted_key is too short"));
    }
    let mut buf = raw.to_vec();
    let mut key = [0u8; 16];
    // On Linux the keyring password is used directly as the AES key.
    let mut pwd = [0u8; 16];
    let take = password.len().min(16);
    pwd[..take].copy_from_slice(&password[..take]);
    let dec = Aes128CbcDec::new_from_slices(&pwd, IV)
        .map_err(|_| Error::other("bad key length"))?;
    let plain = dec
        .decrypt_padded_mut::<Pkcs7>(&mut buf)
        .map_err(|_| Error::other("could not decrypt the browser key"))?;
    key.copy_from_slice(&plain[..16]);
    Ok(key)
}

/// The AES key for `v10` cookies.
fn v10_key(password: &[u8]) -> [u8; 16] {
    pbkdf2_sha1(password, SALT)
}

/// Decrypt a single cookie value, given both possible keys.
fn decrypt_value(
    encrypted: &[u8],
    v10: Option<&[u8; 16]>,
    v11: Option<&[u8; 16]>,
) -> Result<Vec<u8>> {
    if encrypted.is_empty() {
        return Ok(Vec::new());
    }
    if encrypted.starts_with(b"v10") {
        let key = v10.ok_or_else(|| Error::other("no v10 key available"))?;
        let body = &encrypted[3..];
        let mut buf = body.to_vec();
        let dec = Aes128CbcDec::new_from_slices(key, IV)
            .map_err(|_| Error::other("bad key length"))?;
        let plain = dec
            .decrypt_padded_mut::<Pkcs7>(&mut buf)
            .map_err(|_| Error::other("cookie decryption failed (wrong key)"))?;
        return Ok(plain[3..].to_vec());
    }
    if encrypted.starts_with(b"v11") {
        let key = v11.ok_or_else(|| Error::other("no v11 key available"))?;
        let body = &encrypted[3..];
        let mut buf = body.to_vec();
        let dec = Aes128CbcDec::new_from_slices(key, IV)
            .map_err(|_| Error::other("bad key length"))?;
        let plain = dec
            .decrypt_padded_mut::<Pkcs7>(&mut buf)
            .map_err(|_| Error::other("cookie decryption failed (wrong key)"))?;
        return Ok(plain[3..].to_vec());
    }
    if encrypted.starts_with(b"v20") {
        return Err(Error::other(
            "v20 (app-bound) cookies are not supported — this is a Windows-only scheme",
        ));
    }
    Err(Error::other("unknown cookie encryption version"))
}

/// Read and decrypt every cookie of a Chromium browser.
pub fn chromium_cookies(browser: &str) -> Result<Vec<BrowserCookie>> {
    let profile = BrowserProfile::detect(browser)?;
    let db = profile
        .cookies_db()
        .ok_or_else(|| Error::other("no Cookies database found (browser never ran?)"))?;

    // The keyring password is needed for both schemes.
    let password = keyring_password(&profile.keyring_app)?;

    // v11 key, if the browser stores one.
    let v11 = match profile.local_state() {
        Some(path) => read_local_state_key(&path)
            .and_then(|b64| decrypt_v11(&b64, &password).ok()),
        None => None,
    };
    let v10 = v10_key(&password);

    // Chromium keeps the database locked while the browser runs; copy it.
    let tmp = tempdir_path();
    std::fs::copy(&db, &tmp)?;

    let conn = Connection::open(&tmp)?;
    let mut stmt = conn.prepare(
        "SELECT host_key, path, is_secure, name, value, encrypted_value, expires_utc
         FROM cookies",
    )?;

    let mut out = Vec::new();
    let mut rows = stmt.query([])?;
    while let Some(row) = rows.next()? {
        let plain_value: String = row.get(4)?;
        let encrypted: Vec<u8> = row.get(5)?;
        let value = if plain_value.is_empty() && !encrypted.is_empty() {
            match decrypt_value(&encrypted, Some(&v10), v11.as_ref()) {
                Ok(v) => String::from_utf8_lossy(&v).to_string(),
                Err(_) => continue,
            }
        } else {
            plain_value
        };

        let expires_raw: i64 = row.get(6)?;
        out.push(BrowserCookie {
            domain: row.get(0)?,
            path: row.get(1)?,
            secure: row.get::<_, i64>(2)? != 0,
            name: row.get(3)?,
            value,
            expires: chromium_timestamp(expires_raw),
        });
    }

    let _ = std::fs::remove_file(&tmp);
    Ok(out)
}

/// Chromium stores microseconds since 1601-01-01. Convert to unix seconds.
fn chromium_timestamp(raw: i64) -> i64 {
    if raw == 0 {
        return 0;
    }
    // Microseconds since 1601-01-01 -> seconds since the unix epoch.
    let unix_micros = raw - 11_644_473_600_000_000;
    unix_micros / 1_000_000
}

fn read_local_state_key(path: &Path) -> Option<String> {
    let content = std::fs::read_to_string(path).ok()?;
    let doc: serde_json::Value = serde_json::from_str(&content).ok()?;
    doc.get("os_crypt")?
        .get("encrypted_key")?
        .as_str()
        .map(str::to_string)
}

/// Firefox stores cookies in `cookies.sqlite` **unencrypted**.
pub fn firefox_cookies() -> Result<Vec<BrowserCookie>> {
    let home = dirs::home_dir().ok_or_else(|| Error::other("no home directory"))?;
    let root = home.join(".mozilla/firefox");
    if !root.exists() {
        return Err(Error::other("no Firefox profile found"));
    }

    let mut profiles: Vec<PathBuf> = Vec::new();
    for entry in std::fs::read_dir(&root)? {
        let entry = entry?;
        let path = entry.path();
        if path.join("cookies.sqlite").exists() {
            profiles.push(path);
        }
    }
    let profile = profiles
        .into_iter()
        .next()
        .ok_or_else(|| Error::other("no Firefox profile with cookies.sqlite"))?;

    let tmp = tempdir_path();
    std::fs::copy(profile.join("cookies.sqlite"), &tmp)?;

    let conn = Connection::open(&tmp)?;
    let mut stmt = conn.prepare(
        "SELECT host, path, isSecure, name, value, expiry FROM moz_cookies",
    )?;
    let mut out = Vec::new();
    let mut rows = stmt.query([])?;
    while let Some(row) = rows.next()? {
        out.push(BrowserCookie {
            domain: row.get(0)?,
            path: row.get(1)?,
            secure: row.get::<_, i64>(2)? != 0,
            name: row.get(3)?,
            value: row.get(4)?,
            expires: row.get(5)?,
        });
    }
    let _ = std::fs::remove_file(&tmp);
    Ok(out)
}

/// Read cookies from any supported browser.
pub fn extract(browser: &str) -> Result<Vec<BrowserCookie>> {
    if browser.eq_ignore_ascii_case("firefox") {
        firefox_cookies()
    } else {
        chromium_cookies(browser)
    }
}

/// Build a cookie jar from browser cookies.
pub fn to_jar(cookies: &[BrowserCookie]) -> reqwest::cookie::Jar {
    let jar = reqwest::cookie::Jar::default();
    for c in cookies {
        let host = c.domain.trim_start_matches('.');
        if host.is_empty() {
            continue;
        }
        let url = match reqwest::Url::parse(&format!("https://{host}{}", c.path)) {
            Ok(u) => u,
            Err(_) => continue,
        };
        let mut parts = vec![format!("{}={}", c.name, c.value)];
        parts.push(format!("Domain={}", c.domain));
        parts.push(format!("Path={}", c.path));
        if c.secure {
            parts.push("Secure".into());
        }
        if c.expires > 0 {
            parts.push(format!("Expires={}", c.expires));
        }
        jar.add_cookie_str(&parts.join("; "), &url);
    }
    jar
}

fn tempdir_path() -> PathBuf {
    let dir = std::env::temp_dir();
    let name = format!(
        "ytgrab-cookies-{}-{}.db",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0)
    );
    dir.join(name)
}

/// Turn browser cookies into a Netscape cookie file.
pub fn to_netscape(cookies: &[BrowserCookie]) -> String {
    let mut out = String::from("# Netscape HTTP Cookie File\n");
    for c in cookies {
        out.push_str(&format!(
            "{}\t{}\t{}\t{}\t{}\t{}\t{}\n",
            c.domain,
            if c.secure { "TRUE" } else { "FALSE" },
            c.path,
            if c.secure { "TRUE" } else { "FALSE" },
            c.expires,
            c.name,
            c.value
        ));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chromium_timestamp_conversion() {
        // 1601-01-01 -> 1970-01-01
        assert_eq!(chromium_timestamp(11_644_473_600_000_000), 0);
        // 2024-01-01T00:00:00Z
        assert_eq!(chromium_timestamp(13_348_540_800_000_000), 1_704_067_200);
    }

    #[test]
    fn netscape_output_has_the_right_shape() {
        let cookies = vec![BrowserCookie {
            domain: ".youtube.com".into(),
            path: "/".into(),
            secure: true,
            name: "SID".into(),
            value: "abc".into(),
            expires: 1_700_000_000,
        }];
        let text = to_netscape(&cookies);
        assert!(text.contains(".youtube.com\tTRUE\t/\tTRUE\t1700000000\tSID\tabc"));
    }

    #[test]
    fn v10_round_trip() {
        use aes::cipher::{block_padding::Pkcs7, BlockEncryptMut, KeyIvInit};
        type Aes128CbcEnc = cbc::Encryptor<aes::Aes128>;

        let password = b"hunter2";
        let key = v10_key(password);
        let plaintext = b"cookie-value-1234";

        // Chromium encrypts "v10" + value with PKCS#7 padding.
        let mut buf = vec![0u8; 32];
        buf[..3].copy_from_slice(b"v10");
        buf[3..3 + plaintext.len()].copy_from_slice(plaintext);

        let enc = Aes128CbcEnc::new_from_slices(&key, IV).unwrap();
        let len = 3 + plaintext.len();
        let cipher = enc.encrypt_padded_mut::<Pkcs7>(&mut buf, len).unwrap();

        let mut with_prefix = b"v10".to_vec();
        with_prefix.extend_from_slice(cipher);

        let out = decrypt_value(&with_prefix, Some(&key), None).unwrap();
        assert_eq!(out, plaintext);
    }
}
