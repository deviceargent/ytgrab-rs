#include "runner.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "browser_cookies.hpp"
#include "cookies.hpp"
#include "download.hpp"
#include "error.hpp"
#include "formats.hpp"
#include "innertube/browse.hpp"
#include "innertube/client.hpp"
#include "innertube/models.hpp"
#include "innertube/player.hpp"
#include "mux.hpp"
#include "output.hpp"
#include "subtitles.hpp"
#include "url.hpp"
#include "util.hpp"

namespace ytgrab::runner {
namespace {

using json = nlohmann::json;
using models::CaptionTrack;
using models::Format;
using models::PlayerResponse;
using models::VideoDetails;

/// How many alternative formats to try when googlevideo refuses one.
constexpr size_t MAX_FORMAT_FALLBACKS = 6;

/// Context shared across every video in a playlist download.
struct PlaylistContext {
    std::string id;
    std::string title;
    std::optional<uint64_t> index;
};

bool move_file(const fs::path& from, const fs::path& to) {
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
}

void write_text_file(const fs::path& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error::io("cannot write to " + util::utf8(path));
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!out) throw Error::io("cannot write to " + util::utf8(path));
}

void note(const cli::Cli& cli, const std::string& message) {
    if (!cli.quiet) std::cerr << message << "\n";
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// How many byte-ranges to fetch in parallel for a stream of this size.
///
/// YouTube throttles a single connection to roughly 250 kB/s but serves
/// parallel range requests at full speed.
size_t fragment_count(const cli::Cli& cli, std::optional<uint64_t> size) {
    if (cli.fragments > 0) return cli.fragments;
    if (size && *size >= 8ull * 1024 * 1024) return 8;
    if (size && *size >= 2ull * 1024 * 1024) return 4;
    return 1;
}

std::string resolution_of(const Format& format) {
    if (format.width && format.height) {
        return std::to_string(*format.width) + "x" + std::to_string(*format.height);
    }
    if (format.height) return std::to_string(*format.height) + "p";
    return "";
}

void print_formats(const std::vector<Format>& formats) {
    std::printf("%-6s %-5s %-12s %4s  %10s  %10s  %-14s %-12s\n", "ID", "EXT", "RESOLUTION",
                "FPS", "SIZE", "TBR", "VCODEC", "ACODEC");

    std::vector<const Format*> sorted;
    sorted.reserve(formats.size());
    for (const auto& f : formats) sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(), [](const Format* a, const Format* b) {
        if (a->height_val() != b->height_val()) return a->height_val() > b->height_val();
        if (a->tbr() != b->tbr()) return a->tbr() > b->tbr();
        return a->itag < b->itag;
    });

    for (const Format* f : sorted) {
        const std::string vcodec = f->vcodec().value_or("-");
        const std::string acodec = (f->has_video() && !f->has_audio())
                                       ? "video only"
                                       : (!f->has_video() ? "audio only"
                                                          : f->acodec().value_or("-"));
        const std::string resolution =
            f->height_val() > 0 ? resolution_of(*f) : "audio only";
        const std::string size = f->size_str();
        const std::string tbr = std::to_string(f->tbr()) + "kbps";
        const std::string ext = f->ext();
        std::printf("%-6lld %-5s %-12s %4lld  %10s  %10s  %-14s %-12s\n", f->itag, ext.c_str(),
                    resolution.c_str(), f->fps_val(), size.c_str(), tbr.c_str(), vcodec.c_str(),
                    acodec.c_str());
    }
}

std::string build_info_json(const PlayerResponse& player, const formats::Selection& selection,
                            const std::string& video_id) {
    const VideoDetails details = player.video_details.value_or(VideoDetails{});

    json selected = json::array();
    auto add_format = [&](const Format& f) {
        json item = json::object();
        item["format_id"] = std::to_string(f.itag);
        item["ext"] = f.ext();
        item["height"] = f.height_val();
        item["fps"] = f.fps_val();
        item["vcodec"] = f.vcodec() ? json(*f.vcodec()) : json(nullptr);
        item["acodec"] = f.acodec() ? json(*f.acodec()) : json(nullptr);
        item["tbr"] = f.tbr();
        item["filesize"] = f.size_bytes() ? json(*f.size_bytes()) : json(nullptr);
        selected.push_back(std::move(item));
    };
    for (const auto& f : selection.videos) add_format(f);
    for (const auto& f : selection.audios) add_format(f);

    uint64_t view_count = 0;
    bool has_views = false;
    if (details.view_count) {
        try {
            view_count = std::stoull(*details.view_count);
            has_views = true;
        } catch (...) {
        }
    }

    json doc = json::object();
    doc["id"] = video_id;
    doc["title"] = details.title;
    doc["uploader"] = details.author ? json(*details.author) : json(nullptr);
    doc["channel_id"] = details.channel_id ? json(*details.channel_id) : json(nullptr);
    doc["description"] = details.short_description ? json(*details.short_description)
                                                   : json(nullptr);
    doc["duration"] = details.duration_secs() ? json(*details.duration_secs()) : json(nullptr);
    doc["view_count"] = has_views ? json(view_count) : json(nullptr);
    doc["webpage_url"] = "https://www.youtube.com/watch?v=" + video_id;
    doc["extractor"] = "innertube:android";
    doc["selected_formats"] = std::move(selected);
    return doc.dump(2);
}

/// Machine-readable metadata plus *every* available format.
std::string build_formats_json(const PlayerResponse& player, const std::vector<Format>& formats,
                               const std::string& video_id) {
    const VideoDetails details = player.video_details.value_or(VideoDetails{});

    json list = json::array();
    for (const auto& f : formats) {
        json item = json::object();
        item["format_id"] = std::to_string(f.itag);
        item["ext"] = f.ext();
        item["height"] = f.height_val();
        item["fps"] = f.fps_val();
        item["vcodec"] = f.vcodec() ? json(*f.vcodec()) : json(nullptr);
        item["acodec"] = f.acodec() ? json(*f.acodec()) : json(nullptr);
        item["tbr"] = f.tbr();
        item["filesize"] = f.size_bytes() ? json(*f.size_bytes()) : json(nullptr);
        item["has_video"] = f.has_video();
        item["has_audio"] = f.has_audio();
        item["url"] = f.url ? json(*f.url) : json(nullptr);
        list.push_back(std::move(item));
    }

    uint64_t view_count = 0;
    bool has_views = false;
    if (details.view_count) {
        try {
            view_count = std::stoull(*details.view_count);
            has_views = true;
        } catch (...) {
        }
    }

    json doc = json::object();
    doc["id"] = video_id;
    doc["title"] = details.title;
    doc["uploader"] = details.author ? json(*details.author) : json(nullptr);
    doc["channel_id"] = details.channel_id ? json(*details.channel_id) : json(nullptr);
    doc["description"] = details.short_description ? json(*details.short_description)
                                                   : json(nullptr);
    doc["duration"] = details.duration_secs() ? json(*details.duration_secs()) : json(nullptr);
    doc["view_count"] = has_views ? json(view_count) : json(nullptr);
    doc["webpage_url"] = "https://www.youtube.com/watch?v=" + video_id;
    doc["thumbnail"] = "https://i.ytimg.com/vi/" + video_id + "/hqdefault.jpg";
    doc["extractor"] = "innertube:android";
    doc["formats"] = std::move(list);
    return doc.dump(2);
}

enum class TrackKind { Manual, Auto, Unknown };

/// Classify a subtitle track as manual, automatic, or unknown.
TrackKind track_kind(const CaptionTrack& track) {
    const bool asr = track.kind && *track.kind == "asr";
    if (asr || track.base_url.find("caps=asr") != std::string::npos) return TrackKind::Auto;
    if (asr || (track.vss_id && *track.vss_id == "manual")) return TrackKind::Manual;
    return TrackKind::Unknown;
}

/// Swap the timedtext format parameter for `json3`.
std::optional<std::string> json3_url(const std::string& base) {
    auto parsed = url::parse_url(base);
    if (!parsed) return std::nullopt;
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& entry : parsed->query) {
        if (entry.first != "fmt") pairs.push_back(entry);
    }
    pairs.emplace_back("fmt", "json3");
    return parsed->rebuild(pairs);
}

/// Delete the partial files of formats that could not be downloaded.
void remove_stale_parts(const VideoDetails& details, const PlaylistContext& pctx,
                        const cli::Cli& cli) {
    output::TemplateVars vars;
    vars.title = details.title;
    vars.ext = "mp4";
    vars.uploader = details.author.value_or("");
    vars.duration_secs = details.duration_secs();
    vars.playlist_index = pctx.index;
    vars.playlist_title = pctx.title;
    vars.playlist_id = pctx.id;
    vars.epoch = 0;

    const fs::path out_path = util::fspath(output::sanitize_path(output::render(cli.output, vars)));
    const std::string stem = util::utf8(out_path.stem());
    if (stem.empty()) return;
    const fs::path parent = out_path.parent_path().empty() ? fs::path(".") : out_path.parent_path();

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        const std::string name = util::utf8(entry.path().filename());
        if (util::starts_with(name, stem) &&
            (name.find(".f") != std::string::npos || util::ends_with(name, ".part"))) {
            std::error_code remove_ec;
            fs::remove(entry.path(), remove_ec);
        }
    }
}

// ---------------------------------------------------------------------------
// subtitles
// ---------------------------------------------------------------------------

void write_subtitles(Downloader& dl, const cli::Cli& cli, const PlayerResponse& player,
                     const fs::path& parent, const std::string& stem) {
    if (!player.captions || !player.captions->player_captions_tracklist_renderer) {
        note(cli, "no subtitles available");
        return;
    }
    const auto& tracks = player.captions->player_captions_tracklist_renderer->caption_tracks;

    std::vector<std::string> wanted_langs;
    for (const auto& piece : util::split(cli.sub_lang, ',')) {
        wanted_langs.push_back(util::trim(piece));
    }

    // One file per language: a manual track wins over an automatic one.
    std::vector<std::pair<std::string, const CaptionTrack*>> chosen;
    for (const auto& track : tracks) {
        const std::string lang = track.language_code.value_or("");
        const TrackKind kind = track_kind(track);

        bool wanted = false;
        switch (kind) {
            case TrackKind::Manual:
                wanted = cli.write_subs;
                break;
            case TrackKind::Auto:
                wanted = cli.write_auto_subs;
                break;
            case TrackKind::Unknown:
                // The ANDROID client does not say whether a track was uploaded
                // or generated, so accept it for either flag.
                wanted = cli.write_subs || cli.write_auto_subs;
                break;
        }
        if (!wanted) continue;
        const bool language_wanted =
            std::any_of(wanted_langs.begin(), wanted_langs.end(),
                        [&](const std::string& w) { return w == lang || w == "all"; });
        if (!language_wanted) continue;

        auto existing = std::find_if(chosen.begin(), chosen.end(),
                                     [&](const auto& entry) { return entry.first == lang; });
        if (existing != chosen.end()) {
            if (track_kind(*existing->second) != TrackKind::Manual &&
                kind == TrackKind::Manual) {
                existing->second = &track;
            }
        } else {
            chosen.emplace_back(lang, &track);
        }
    }

    for (const auto& [lang, track] : chosen) {
        auto url = json3_url(track->base_url);
        if (!url) continue;
        std::string body = dl.get_bytes(*url);
        auto cues = subtitles::parse_json3(body);
        if (cues.empty()) continue;
        const fs::path path = parent / (stem + "." + lang + "." + cli.sub_format);
        subtitles::write_cues(cues, path);
        note(cli, "Saved " + util::utf8(path));
    }
}

// ---------------------------------------------------------------------------
// the actual download
// ---------------------------------------------------------------------------

void extract_audio_only(Downloader& dl, const cli::Cli& cli, const VideoDetails& details,
                        const Format& audio, const PlaylistContext& pctx,
                        const std::string& video_id) {
    output::TemplateVars vars;
    vars.title = details.title;
    vars.id = video_id;
    vars.ext = cli.audio_format;
    vars.uploader = details.author.value_or("");
    vars.duration_secs = details.duration_secs();
    vars.format_id = std::to_string(audio.itag);
    vars.playlist_index = pctx.index;
    vars.playlist_title = pctx.title;
    vars.playlist_id = pctx.id;
    vars.epoch = util::now_unix();

    const fs::path out_path = util::fspath(output::sanitize_path(output::render(cli.output, vars)));
    if (!out_path.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::string stem = util::utf8(out_path.stem());
    if (stem.empty()) stem = "audio";
    const fs::path parent =
        out_path.parent_path().empty() ? fs::path(".") : out_path.parent_path();

    const std::string source_ext = audio.ext();
    const fs::path tmp = parent / (stem + ".f" + std::to_string(audio.itag) + "." + source_ext);
    if (!audio.url) throw Error::other("audio stream has no url");

    ProgressBar bar(audio.size_bytes(), "audio");
    dl.download(*audio.url, tmp, bar, audio.size_bytes(),
                fragment_count(cli, audio.size_bytes()), audio.itag);
    bar.finish_and_clear();

    // Already in the requested container? Then no conversion is needed.
    if (source_ext == cli.audio_format) {
        if (!move_file(tmp, out_path)) throw Error::io("cannot move the audio stream");
    } else {
        const std::string codec = [&]() -> std::string {
            if (cli.audio_format == "mp3") return "libmp3lame";
            if (cli.audio_format == "m4a" || cli.audio_format == "aac") return "aac";
            if (cli.audio_format == "opus") return "libopus";
            if (cli.audio_format == "flac") return "flac";
            if (cli.audio_format == "wav") return "pcm_s16le";
            return cli.audio_format;
        }();
        mux::extract_audio(tmp, out_path, codec, std::string("192k"));
        std::error_code ec;
        fs::remove(tmp, ec);
    }

    note(cli, "Saved " + util::utf8(out_path));
}

void download_streams(Downloader& dl, const cli::Cli& cli, const PlayerResponse& player,
                      const VideoDetails& details, const formats::Selection& selection,
                      const PlaylistContext& pctx, const std::string& video_id) {
    if (selection.videos.empty()) throw Error::other("no video stream selected");
    const Format& video = selection.videos.front();
    const Format* audio = selection.audios.empty() ? nullptr : &selection.audios.front();

    const std::string video_ext = video.ext();
    const std::string audio_ext = audio ? audio->ext() : "";
    std::string final_ext;
    if (selection.needs_mux()) {
        // Keep the container aligned with the codecs for stream copy. Opus in
        // an MP4 works but many players refuse it, so fall back to mkv.
        const bool audio_ok = audio_ext == "m4a" || audio_ext == "mp4" || audio_ext.empty();
        if (video_ext == "mp4" && audio_ok) {
            final_ext = "mp4";
        } else if (video_ext == "webm" && (audio_ext == "webm" || audio_ext.empty())) {
            final_ext = "webm";
        } else {
            final_ext = "mkv";
        }
    } else {
        final_ext = video_ext;
    }

    std::string format_id;
    for (const auto& f : selection.videos) {
        if (!format_id.empty()) format_id += "+";
        format_id += std::to_string(f.itag);
    }
    for (const auto& f : selection.audios) {
        if (!format_id.empty()) format_id += "+";
        format_id += std::to_string(f.itag);
    }

    output::TemplateVars vars;
    vars.title = details.title;
    vars.id = video_id;
    vars.ext = final_ext;
    vars.uploader = details.author.value_or("");
    vars.duration_secs = details.duration_secs();
    vars.resolution = resolution_of(video);
    vars.format_id = format_id;
    vars.playlist_index = pctx.index;
    vars.playlist_title = pctx.title;
    vars.playlist_id = pctx.id;
    vars.epoch = util::now_unix();

    const fs::path out_path = util::fspath(output::sanitize_path(output::render(cli.output, vars)));
    if (!out_path.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }

    std::string stem = util::utf8(out_path.stem());
    if (stem.empty()) stem = "video";
    const fs::path parent =
        out_path.parent_path().empty() ? fs::path(".") : out_path.parent_path();

    // ---- video -----------------------------------------------------------
    const fs::path video_tmp =
        parent / (stem + ".f" + std::to_string(video.itag) + "." + video_ext);
    if (!video.url) {
        throw Error::other("video stream has no url (signature cipher unsupported)");
    }
    {
        ProgressBar bar(video.size_bytes(), "video");
        dl.download(*video.url, video_tmp, bar, video.size_bytes(),
                    fragment_count(cli, video.size_bytes()), video.itag);
        bar.finish_and_clear();
    }

    // ---- audio -----------------------------------------------------------
    std::optional<fs::path> audio_tmp;
    if (audio) {
        const std::string aext = audio->ext();
        const fs::path tmp = parent / (stem + ".f" + std::to_string(audio->itag) + "." + aext);
        if (!audio->url) throw Error::other("audio stream has no url");
        ProgressBar bar(audio->size_bytes(), "audio");
        dl.download(*audio->url, tmp, bar, audio->size_bytes(),
                    fragment_count(cli, audio->size_bytes()), audio->itag);
        bar.finish_and_clear();
        audio_tmp = tmp;
    }

    // ---- combine ---------------------------------------------------------
    mux::Meta meta;
    meta.title = details.title;
    meta.uploader = details.author;
    meta.artist = details.author;
    meta.description = details.short_description;
    meta.webpage_url = "https://www.youtube.com/watch?v=" + video_id;
    meta.duration = details.duration_secs();

    if (audio_tmp) {
        if (cli.no_mux) {
            // Keep the two streams separate, like yt-dlp's --no-mux.
            note(cli, "Saved " + util::utf8(video_tmp));
            note(cli, "Saved " + util::utf8(*audio_tmp));
        } else {
            mux::merge(video_tmp, *audio_tmp, out_path, meta);
            std::error_code ec;
            fs::remove(video_tmp, ec);
            if (!cli.keep_files) fs::remove(*audio_tmp, ec);
            note(cli, "Saved " + util::utf8(out_path));
        }
    } else {
        if (!move_file(video_tmp, out_path)) throw Error::io("cannot move the video stream");
        note(cli, "Saved " + util::utf8(out_path));
    }

    // ---- sidecar files ---------------------------------------------------
    if (cli.write_info_json) {
        const fs::path path = parent / (stem + ".info.json");
        write_text_file(path, build_info_json(player, selection, video_id));
    }

    if (cli.write_thumbnail) {
        if (details.thumbnail) {
            if (const models::Thumbnail* best = details.thumbnail->best()) {
                try {
                    std::string bytes = dl.get_bytes(best->url);
                    const std::string ext =
                        best->url.find("webp") != std::string::npos ? "webp" : "jpg";
                    const fs::path path = parent / (stem + "." + ext);
                    write_text_file(path, bytes);
                } catch (const Error&) {
                    // Thumbnails are best-effort.
                }
            }
        }
    }

    if (cli.write_subs || cli.write_auto_subs) {
        write_subtitles(dl, cli, player, parent, stem);
    }
}

void download_video(innertube::InnerTube& it, Downloader& dl, const cli::Cli& cli,
                    const std::string& video_id, const PlaylistContext& pctx) {
    // googlevideo serves some formats only in small windows and refuses larger
    // ranges with a 403. When that happens, mint a fresh URL and fall back to
    // the next best format.
    std::vector<long long> failed_formats;

    while (true) {
        PlayerResponse player = innertube::player::fetch(it, video_id);
        if (!player.streaming_data) throw Error::unavailable("no streaming data");
        std::vector<Format> formats = models::all_formats(*player.streaming_data);

        if (cli.list_formats) {
            print_formats(formats);
            return;
        }
        if (cli.formats_json) {
            std::cout << build_formats_json(player, formats, video_id) << "\n";
            return;
        }

        const formats::Selector selector = formats::Selector::parse(cli.format);
        const formats::Selection selection = formats::select_excluding_with(
            selector, formats, cli.prefer_opus, failed_formats, !failed_formats.empty());

        if (cli.get_url) {
            for (const auto& f : selection.videos) {
                if (f.url) std::cout << *f.url << "\n";
            }
            for (const auto& f : selection.audios) {
                if (f.url) std::cout << *f.url << "\n";
            }
            return;
        }

        const VideoDetails details = player.video_details.value_or(VideoDetails{});
        if (cli.dump_json) {
            std::cout << build_info_json(player, selection, video_id) << "\n";
            return;
        }

        // Extracting audio never needs the video stream at all.
        if (cli.extract_audio) {
            Format audio;
            if (!selection.audios.empty()) {
                audio = selection.audios.front();
            } else {
                audio = formats::best_audio_only(formats, cli.prefer_opus);
            }
            extract_audio_only(dl, cli, details, audio, pctx, video_id);
            return;
        }

        try {
            download_streams(dl, cli, player, details, selection, pctx, video_id);
            return;
        } catch (const Error& e) {
            if (e.kind != ErrKind::SignedUrl) throw;
            failed_formats.push_back(e.itag);
            if (failed_formats.size() > MAX_FORMAT_FALLBACKS) {
                throw Error::other("googlevideo refused every format it offered (HTTP " +
                                   std::to_string(e.status) + ")");
            }
            std::cerr << "googlevideo refused format " << e.itag << " (HTTP " << e.status
                      << "); minting a new URL and trying another format\n";
            // The half downloaded bytes belong to the refused format.
            remove_stale_parts(details, pctx, cli);
            continue;
        }
    }
}

void download_playlist(innertube::InnerTube& it, Downloader& dl, const cli::Cli& cli,
                       const std::string& playlist_id) {
    note(cli, "Fetching playlist " + playlist_id + " ...");
    std::vector<innertube::browse::PlaylistItem> items =
        innertube::browse::playlist(it, playlist_id);
    if (items.empty()) throw Error::other("playlist is empty");
    note(cli, "Playlist has " + std::to_string(items.size()) + " videos");

    PlaylistContext ctx;
    ctx.id = playlist_id;

    size_t failures = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        PlaylistContext item_ctx = ctx;
        item_ctx.index = i + 1;
        try {
            download_video(it, dl, cli, items[i].video_id, item_ctx);
        } catch (const Error& e) {
            ++failures;
            std::cerr << "error: " << items[i].video_id << " failed: " << e.what() << "\n";
        }
    }

    if (failures > 0) {
        throw Error::other(std::to_string(failures) + " of " + std::to_string(items.size()) +
                           " videos failed");
    }
}

}  // namespace

void run(const cli::Cli& cli) {
    const url::Target target = url::parse(cli.url);

    auto context = innertube::ClientContext::by_name(cli.client);
    if (!context) throw Error::other("unknown client `" + cli.client + "`");

    // Cookies are shared between the API client and the media downloader:
    // age-restricted videos need them on both.
    std::optional<CookieJar> jar;
    if (cli.cookies) {
        jar = CookieJar::from_netscape_file(*cli.cookies);
    } else if (cli.cookies_from_browser) {
        auto cookies = extract_browser_cookies(*cli.cookies_from_browser);
        note(cli, "Read " + std::to_string(cookies.size()) + " cookies from " +
                      *cli.cookies_from_browser);
        jar = to_jar(cookies);
    }

    innertube::InnerTube it(*context, jar ? &*jar : nullptr);
    Downloader dl(it.user_agent(), jar ? &*jar : nullptr);
    dl.retries = cli.retries;
    dl.fragments = std::max<size_t>(cli.fragments, 1);

    if (target.kind == url::Target::Kind::Video) {
        const bool want_playlist = cli.playlist && !cli.no_playlist;
        if (want_playlist && target.playlist) {
            download_playlist(it, dl, cli, *target.playlist);
        } else {
            download_video(it, dl, cli, target.id, PlaylistContext{});
        }
    } else {
        download_playlist(it, dl, cli, target.id);
    }
}

}  // namespace ytgrab::runner
