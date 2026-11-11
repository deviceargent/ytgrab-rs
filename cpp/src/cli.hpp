//! Command line interface definition, mirroring the clap surface.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ytgrab::cli {

struct Cli {
    /// Video URL, playlist URL, or bare video id.
    std::string url;

    std::string format = "best";
    std::string output = "%(title)s.%(ext)s";

    bool list_formats = false;
    bool get_url = false;
    bool dump_json = false;
    bool formats_json = false;
    bool write_info_json = false;
    bool write_thumbnail = false;
    bool write_subs = false;
    bool write_auto_subs = false;

    std::string sub_lang = "en";
    std::string sub_format = "srt";

    bool extract_audio = false;
    std::string audio_format = "mp3";

    bool keep_files = false;
    bool no_mux = false;

    std::optional<std::string> cookies;
    std::optional<std::string> cookies_from_browser;

    std::string client = "android";
    size_t fragments = 0;
    uint32_t retries = 5;

    bool playlist = false;
    bool no_playlist = false;
    bool prefer_opus = false;

    bool verbose = false;
    bool quiet = false;

    bool help = false;
    bool version = false;
};

/// Parse argv (without the program name). Throws Error::other on bad input.
Cli parse(const std::vector<std::string>& args);

std::string help_text();
std::string version_text();

}  // namespace ytgrab::cli
