//! Subtitle handling: YouTube `json3` captions into SubRip or WebVTT.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "download.hpp"  // fs

namespace ytgrab::subtitles {

struct Cue {
    int64_t start_ms = 0;
    int64_t duration_ms = 0;
    std::string text;
};

/// Parse the `json3` caption payload into cues.
std::vector<Cue> parse_json3(const std::string& body);

std::string to_srt(const std::vector<Cue>& cues);
std::string to_vtt(const std::vector<Cue>& cues);

/// Write cues to `path` with the format implied by the extension.
void write_cues(const std::vector<Cue>& cues, const fs::path& path);

}  // namespace ytgrab::subtitles
