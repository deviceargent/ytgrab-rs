#include "cli.hpp"

#include <cstdlib>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::cli {
namespace {

[[noreturn]] void unknown_argument(const std::string& arg) {
    throw Error::other("unexpected argument `" + arg + "`; run `ytgrab --help` for the full list");
}

[[noreturn]] void missing_value(const std::string& arg) {
    throw Error::other("the argument `" + arg + "` needs a value");
}

unsigned long parse_uint(const std::string& arg, const std::string& value) {
    try {
        size_t used = 0;
        unsigned long parsed = std::stoul(value, &used);
        if (used != value.size()) throw std::invalid_argument("trailing");
        return parsed;
    } catch (...) {
        throw Error::other("invalid value `" + value + "` for `" + arg + "`");
    }
}

}  // namespace

Cli parse(const std::vector<std::string>& args) {
    Cli cli;
    bool positional_only = false;
    bool has_url = false;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string arg = args[i];

        auto take_value = [&](const std::string& name) -> std::string {
            size_t eq = arg.find('=');
            if (eq != std::string::npos) return arg.substr(eq + 1);
            if (i + 1 >= args.size()) missing_value(name);
            return args[++i];
        };

        if (positional_only || arg.empty() || arg[0] != '-') {
            if (has_url) throw Error::other("unexpected argument `" + arg + "`");
            cli.url = arg;
            has_url = true;
            continue;
        }
        if (arg == "--") {
            positional_only = true;
            continue;
        }

        // Long options, with or without `=value`.
        std::string name = arg;
        size_t eq = name.find('=');
        if (eq != std::string::npos) name = name.substr(0, eq);

        if (name == "--help" || name == "-h") {
            cli.help = true;
        } else if (name == "--version" || name == "-V") {
            cli.version = true;
        } else if (name == "-f" || name == "--format") {
            cli.format = take_value(name);
        } else if (name == "-o" || name == "--output") {
            cli.output = take_value(name);
        } else if (name == "--list-formats") {
            cli.list_formats = true;
        } else if (name == "--get-url") {
            cli.get_url = true;
        } else if (name == "--dump-json") {
            cli.dump_json = true;
        } else if (name == "--formats-json") {
            cli.formats_json = true;
        } else if (name == "--write-info-json") {
            cli.write_info_json = true;
        } else if (name == "--write-thumbnail") {
            cli.write_thumbnail = true;
        } else if (name == "--write-subs") {
            cli.write_subs = true;
        } else if (name == "--write-auto-subs") {
            cli.write_auto_subs = true;
        } else if (name == "--sub-lang") {
            cli.sub_lang = take_value(name);
        } else if (name == "--sub-format") {
            cli.sub_format = take_value(name);
        } else if (name == "-x" || name == "--extract-audio") {
            cli.extract_audio = true;
        } else if (name == "--audio-format") {
            cli.audio_format = take_value(name);
        } else if (name == "--keep-files") {
            cli.keep_files = true;
        } else if (name == "--no-mux") {
            cli.no_mux = true;
        } else if (name == "--cookies") {
            cli.cookies = take_value(name);
        } else if (name == "--cookies-from-browser") {
            cli.cookies_from_browser = take_value(name);
        } else if (name == "--client") {
            cli.client = take_value(name);
        } else if (name == "--fragments") {
            cli.fragments = parse_uint(name, take_value(name));
        } else if (name == "--retries") {
            cli.retries = static_cast<uint32_t>(parse_uint(name, take_value(name)));
        } else if (name == "--playlist") {
            cli.playlist = true;
        } else if (name == "--no-playlist") {
            cli.no_playlist = true;
        } else if (name == "--prefer-opus") {
            cli.prefer_opus = true;
        } else if (name == "-v" || name == "--verbose") {
            cli.verbose = true;
        } else if (name == "-q" || name == "--quiet") {
            cli.quiet = true;
        } else {
            unknown_argument(arg);
        }
    }

    if (!cli.help && !cli.version && !has_url) {
        throw Error::other("the following required argument was not provided: <URL>");
    }
    return cli;
}

std::string version_text() { return "ytgrab 0.1.0"; }

std::string help_text() {
    return R"(A fast YouTube downloader written in C++

Usage: ytgrab [OPTIONS] <URL>

Arguments:
  <URL>  Video URL, playlist URL, or bare video id

Options:
  -f, --format <FORMAT>              Format selector: best, worst, bestvideo+bestaudio,
                                     1080, bv[height<=720]+ba [default: best]
  -o, --output <OUTPUT>              Output template, yt-dlp style [default: %(title)s.%(ext)s]
      --list-formats                 List available formats and exit
      --get-url                      Print the direct URL(s) instead of downloading
      --dump-json                    Print the full metadata as JSON and exit
      --formats-json                 Print metadata plus every available format as JSON and exit
      --write-info-json              Write the metadata alongside the download
      --write-thumbnail              Write the thumbnail alongside the download
      --write-subs                   Download manually written subtitles
      --write-auto-subs              Download automatically generated subtitles
      --sub-lang <LANGS>             Subtitle languages (comma separated) [default: en]
      --sub-format <FORMAT>          Subtitle format: srt or vtt [default: srt]
  -x, --extract-audio                Extract audio only
      --audio-format <FORMAT>        Audio format when extracting audio [default: mp3]
      --keep-files                   Keep the separate video/audio files after muxing
      --no-mux                       Do not mux video and audio (keep separate streams)
      --cookies <FILE>               Cookies file in Netscape format
      --cookies-from-browser <NAME>  Read cookies from a browser: chrome, brave, chromium,
                                     edge, firefox
      --client <CLIENT>              InnerTube client: android, ios, mweb, web, tv
                                     [default: android]
      --fragments <N>                Parallel byte-range segments per stream
                                     (0 = auto, 1 = sequential) [default: 0]
      --retries <N>                  Retry attempts per request [default: 5]
      --playlist                     Download every video in a playlist
      --no-playlist                  Never touch playlists
      --prefer-opus                  Prefer Opus audio over AAC
  -v, --verbose                      Verbose logging
  -q, --quiet                        Only print errors
  -h, --help                         Print help
  -V, --version                      Print version
)";
}

}  // namespace ytgrab::cli
