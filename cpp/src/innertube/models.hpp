//! Models for the InnerTube `player` and `browse` responses.
//!
//! Only the fields the downloader consumes are modelled; everything else is
//! ignored. YouTube is inconsistent about numeric types, so every number is
//! read through a helper that accepts both `12` and `"12"`.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ytgrab::models {

using json = nlohmann::json;

/// Read a number that may arrive as a JSON number or as a string.
std::optional<long long> flex_int(const json& value);

struct AudioTrack {
    std::optional<std::string> id;
};

struct Format {
    long long itag = 0;
    std::optional<std::string> url;
    /// Present when YouTube withholds the direct URL (the WEB client does it).
    std::optional<std::string> signature_cipher;
    std::optional<std::string> mime_type;
    std::optional<long long> bitrate;
    std::optional<long long> avg_bitrate;
    std::optional<long long> content_length;
    std::optional<long long> approx_duration_ms;
    std::optional<std::string> quality_label;
    std::optional<std::string> quality;
    std::optional<long long> width;
    std::optional<long long> height;
    std::optional<long long> fps;
    std::optional<long long> audio_channels;
    std::optional<long long> audio_sample_rate;
    std::optional<std::string> audio_quality;
    std::optional<AudioTrack> audio_track;
    std::optional<std::string> language_code;
    std::optional<std::string> container;
    std::optional<std::string> codecs;
    std::optional<long long> filesize;
    bool is_drc = false;

    bool has_video() const;
    bool has_audio() const;
    bool is_video_only() const { return has_video() && !has_audio(); }
    bool is_audio_only() const { return has_audio() && !has_video(); }
    bool is_combined() const { return has_video() && has_audio(); }

    /// The raw `codecs="..."` value.
    std::optional<std::string> codecs_raw() const;
    std::vector<std::string> codecs_list() const;
    bool is_audio_mime() const;

    std::optional<std::string> vcodec() const;
    std::optional<std::string> acodec() const;

    /// Container extension: `mp4`, `webm`, ...
    std::string ext() const;
    bool has_rate_bypass() const;
    long long tbr() const;
    long long height_val() const { return height.value_or(0); }
    long long fps_val() const { return fps.value_or(0); }
    std::string size_str() const;
    std::optional<uint64_t> size_bytes() const;
};

Format parse_format(const json& value);

struct Thumbnail {
    std::string url;
    std::optional<long long> width;
    std::optional<long long> height;
};

struct Thumbnails {
    std::vector<Thumbnail> thumbnails;
    const Thumbnail* best() const;
};

struct VideoDetails {
    std::string video_id;
    std::string title;
    std::optional<std::string> length_seconds;
    std::optional<std::string> channel_id;
    std::optional<std::string> author;
    std::optional<std::string> short_description;
    std::optional<std::string> view_count;
    std::optional<bool> is_live_content;
    std::optional<Thumbnails> thumbnail;

    std::optional<uint64_t> duration_secs() const;
};

struct StreamingData {
    std::optional<std::string> expires_in_seconds;
    std::vector<Format> formats;
    std::vector<Format> adaptive_formats;
    std::optional<std::string> hls_manifest_url;
    std::optional<std::string> dash_manifest_url;
};

struct TextRun {
    std::string text;
};

struct CaptionTrack {
    std::string base_url;
    std::optional<TextRun> name;
    std::optional<std::string> vss_id;
    std::optional<std::string> language_code;
    std::optional<std::string> kind;
    std::optional<bool> is_translatable;

    std::string display_name() const;
};

struct TrackList {
    std::vector<CaptionTrack> caption_tracks;
};

struct Captions {
    std::optional<TrackList> player_captions_tracklist_renderer;
};

struct PlayerErrorMessage {
    std::vector<TextRun> subreason_runs;
};

struct PlayabilityStatus {
    std::string status;
    std::optional<std::string> reason;
    std::vector<TextRun> error_runs;

    bool is_ok() const;
    std::string message() const;
};

struct PlayerResponse {
    PlayabilityStatus playability_status;
    std::optional<StreamingData> streaming_data;
    std::optional<VideoDetails> video_details;
    std::optional<Captions> captions;
};

PlayerResponse parse_player_response(const json& value);

/// All formats (progressive + adaptive) flattened into one list.
std::vector<Format> all_formats(const StreamingData& streaming);

}  // namespace ytgrab::models
