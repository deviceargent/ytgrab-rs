//! Reading cookies out of a Chromium-based browser (Chrome, Brave, Chromium,
//! Edge) and out of Firefox.
//!
//! Chromium encrypts cookie values on disk. On Windows the master key lives in
//! `Local State` as `os_crypt.encrypted_key`, base64-encoded and wrapped in
//! DPAPI; cookie values are AES-256-GCM with a 12 byte nonce and a 16 byte
//! tag. Firefox stores cookies unencrypted in `cookies.sqlite`.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cookies.hpp"

namespace ytgrab {

namespace fs = std::filesystem;

struct BrowserCookie {
    std::string domain;
    std::string path;
    std::string name;
    std::string value;
    bool secure = false;
    long long expires = 0;
};

/// Read cookies from any supported browser.
std::vector<BrowserCookie> extract_browser_cookies(const std::string& browser);

/// Build a cookie jar from browser cookies.
CookieJar to_jar(const std::vector<BrowserCookie>& cookies);

/// Turn browser cookies into a Netscape cookie file.
std::string to_netscape(const std::vector<BrowserCookie>& cookies);

}  // namespace ytgrab
