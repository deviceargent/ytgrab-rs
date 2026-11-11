//! Format selection — the part of yt-dlp people actually interact with.
//!
//! Supported selectors:
//!
//!     best                            best video + best audio (default)
//!     worst                           lowest quality combined
//!     bestvideo / bv                  best video-only stream
//!     bestaudio / ba                  best audio-only stream
//!     1080                            best video at (or below) 1080p
//!     bestvideo[height<=720]+bestaudio
//!     best[ext=mp4]

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "innertube/models.hpp"

namespace ytgrab::formats {

using models::Format;

enum class Kind { Video, Audio, Combined };
enum class Op { Eq, Le, Ge, Lt, Gt };

struct Filter {
    std::string key;
    Op op = Op::Eq;
    std::string value;
};

struct Part {
    Kind kind = Kind::Combined;
    bool worst = false;
    std::vector<Filter> filters;
    /// A bare number: a format id if such a format exists, otherwise a height cap.
    std::optional<long long> itag;
    std::optional<long long> height_cap;
};

struct Selector {
    std::vector<Part> parts;

    static Selector parse(const std::string& input);
    bool is_single_stream() const;
};

/// The result of resolving a selector against a format list.
struct Selection {
    std::vector<Format> videos;
    std::vector<Format> audios;

    bool needs_mux() const { return !audios.empty(); }
    uint64_t total_size() const;
};

Selection select(const Selector& selector, const std::vector<Format>& formats);
Selection select_with(const Selector& selector, const std::vector<Format>& formats,
                      bool prefer_opus);
Selection select_excluding(const Selector& selector, const std::vector<Format>& formats,
                           bool prefer_opus, const std::vector<long long>& excluded);
Selection select_excluding_with(const Selector& selector, const std::vector<Format>& formats,
                                bool prefer_opus, const std::vector<long long>& excluded,
                                bool prefer_servable);

/// The best audio-only stream, ignoring the selector.
Format best_audio_only(const std::vector<Format>& formats, bool prefer_opus);

}  // namespace ytgrab::formats
