//! Stream downloading.
//!
//! Two modes:
//!   * single stream with HTTP range resume (restart-friendly)
//!   * segmented parallel download: N byte-ranges fetched concurrently, then
//!     concatenated. This is what saturates a fast connection when YouTube
//!     throttles a single connection.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include "cookies.hpp"
#include "http.hpp"

namespace ytgrab {

namespace fs = std::filesystem;

/// A yt-dlp-ish progress bar that repaints with a carriage return.
///
/// The GUI parses `- [12.3MiB/45.6MiB] (00:12) 1.23MiB/s`, so the shape must
/// not change.
class ProgressBar {
public:
    ProgressBar(std::optional<uint64_t> total, std::string label);
    ~ProgressBar();

    void set_length(uint64_t total);
    void set_position(uint64_t position);
    void inc(uint64_t delta);
    void finish_and_clear();

private:
    void draw_locked(bool final_draw);
    void maybe_draw_locked();
    std::string eta_locked() const;

    std::mutex mu_;
    uint64_t position_ = 0;
    uint64_t total_ = 0;
    std::string label_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_draw_;
    uint64_t last_position_ = 0;
    double speed_ = 0.0;
    bool drawn_ = false;
};

class Downloader {
public:
    Downloader(std::string user_agent, const CookieJar* jar);

    uint32_t retries = 5;
    /// Number of parallel byte-range segments (1 = plain sequential).
    size_t fragments = 1;

    const std::string& user_agent() const { return user_agent_; }

    /// Plain GET of a small resource (thumbnails, subtitle tracks).
    std::string get_bytes(const std::string& url);

    /// Download `url` into `dest`, resuming a partial `.part` file if present.
    uint64_t download(const std::string& url, const fs::path& dest, ProgressBar& bar,
                      std::optional<uint64_t> total_hint, size_t fragments, long long itag);

private:
    http::Client make_client() const;
    uint64_t download_single(const std::string& url, const fs::path& dest, ProgressBar& bar,
                             std::optional<uint64_t> total_hint, long long itag);
    uint64_t download_segmented(const std::string& url, const fs::path& dest, ProgressBar& bar,
                                uint64_t total, size_t fragments, long long itag);
    std::optional<uint64_t> probe_size(const std::string& url);

    std::string user_agent_;
    const CookieJar* jar_;
};

}  // namespace ytgrab
