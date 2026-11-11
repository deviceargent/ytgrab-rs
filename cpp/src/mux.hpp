//! Muxing and metadata handling via ffmpeg.

#pragma once

#include <optional>
#include <string>

#include "download.hpp"  // fs

namespace ytgrab::mux {

struct Meta {
    std::string title;
    std::optional<std::string> uploader;
    std::optional<std::string> artist;
    std::optional<std::string> description;
    std::optional<std::string> webpage_url;
    std::optional<uint64_t> duration;
};

bool ffmpeg_available();

/// Merge a video-only and an audio-only file into `out` (stream copy).
void merge(const fs::path& video, const fs::path& audio, const fs::path& out, const Meta& meta);

/// Re-encode the audio track into a standalone file (e.g. mp3).
void extract_audio(const fs::path& input, const fs::path& out, const std::string& codec,
                   const std::optional<std::string>& bitrate);

/// Remux a single file into a container (e.g. webm -> mp4) without re-encoding.
void remux(const fs::path& input, const fs::path& out, const Meta& meta);

}  // namespace ytgrab::mux
