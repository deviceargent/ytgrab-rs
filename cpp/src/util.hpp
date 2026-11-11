//! Small string / time / filesystem helpers.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ytgrab::util {

namespace fs = std::filesystem;

/// UTF-8 form of a path (std::filesystem::path::u8string returns char8_t).
std::string utf8(const fs::path& path);
/// Build a path from a UTF-8 string.
fs::path fspath(const std::string& utf8);

std::string trim(const std::string& s);
std::string to_lower(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
bool eq_ignore_ascii_case(const std::string& a, const std::string& b);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
std::string replace_all(std::string s, const std::string& from, const std::string& to);

/// `3661` -> `1:01:01`, `213` -> `3:33`
std::string format_duration(uint64_t secs);

/// yt-dlp-ish byte formatting: `1234567` -> `1.18MiB`.
std::string human_bytes(double bytes);

/// Seconds since the unix epoch.
uint64_t now_unix();

/// Wide/UTF-8 conversion for the Win32 APIs.
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);

/// Process id of the current process.
uint32_t process_id();

/// Is a process with this pid still alive?
bool process_alive(uint32_t pid);

std::string base64_encode(const std::string& in);
std::string base64_decode(const std::string& in);

/// Decode `%XX` escapes (used for query values).
std::string percent_decode(const std::string& in);

}  // namespace ytgrab::util
