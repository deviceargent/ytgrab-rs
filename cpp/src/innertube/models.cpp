#include "models.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "util.hpp"

namespace ytgrab::models {
namespace {

std::optional<std::string> opt_str(const json& obj, const char* key) {
    if (!obj.is_object()) return std::nullopt;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    return std::nullopt;
}

std::optional<long long> opt_int(const json& obj, const char* key) {
    if (!obj.is_object()) return std::nullopt;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    return flex_int(*it);
}

std::vector<TextRun> parse_runs(const json& value) {
    std::vector<TextRun> out;
    if (!value.is_array()) return out;
    for (const auto& item : value) {
        out.push_back(TextRun{item.value("text", std::string())});
    }
    return out;
}

}  // namespace

std::optional<long long> flex_int(const json& value) {
    if (value.is_number_integer()) return value.get<long long>();
    if (value.is_number_unsigned())
        return static_cast<long long>(value.get<unsigned long long>());
    if (value.is_number_float()) return static_cast<long long>(value.get<double>());
    if (value.is_string()) {
        std::string s = util::trim(value.get<std::string>());
        if (s.empty()) return std::nullopt;
        long long parsed = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), parsed);
        if (res.ec == std::errc() && res.ptr == s.data() + s.size()) return parsed;
    }
    return std::nullopt;
}

std::optional<std::string> Format::codecs_raw() const {
    if (codecs && !codecs->empty()) return codecs;
    if (!mime_type) return std::nullopt;
    const std::string& mime = *mime_type;
    size_t start = mime.find("codecs=");
    if (start == std::string::npos) return std::nullopt;
    std::string rest = mime.substr(start + 7);
    if (!rest.empty() && rest.front() == '"') rest.erase(rest.begin());
    size_t end = rest.find('"');
    if (end != std::string::npos) rest = rest.substr(0, end);
    return rest;
}

std::vector<std::string> Format::codecs_list() const {
    std::vector<std::string> out;
    auto raw = codecs_raw();
    if (!raw) return out;
    for (const auto& piece : util::split(*raw, ',')) {
        std::string c = util::trim(piece);
        if (!c.empty() && c != "none") out.push_back(c);
    }
    return out;
}

bool Format::is_audio_mime() const {
    return mime_type && util::starts_with(*mime_type, "audio/");
}

bool Format::has_video() const {
    if (mime_type) {
        if (util::starts_with(*mime_type, "audio/")) return false;
        if (util::starts_with(*mime_type, "video/")) return true;
    }
    return height.value_or(0) > 0 || codecs_list().size() == 2;
}

bool Format::has_audio() const {
    if (mime_type) {
        if (util::starts_with(*mime_type, "audio/")) return true;
        if (util::starts_with(*mime_type, "video/")) return codecs_list().size() > 1;
    }
    return codecs_list().size() > 1;
}

std::optional<std::string> Format::vcodec() const {
    if (is_audio_mime()) return std::nullopt;
    auto list = codecs_list();
    if (list.empty()) return std::nullopt;
    return list.front();
}

std::optional<std::string> Format::acodec() const {
    auto list = codecs_list();
    if (is_audio_mime()) {
        if (list.empty()) return std::nullopt;
        return list.front();
    }
    if (list.size() < 2) return std::nullopt;
    return list[1];
}

std::string Format::ext() const {
    if (container && !container->empty()) return *container;
    if (mime_type) {
        size_t slash = mime_type->find('/');
        if (slash != std::string::npos) {
            std::string sub = mime_type->substr(slash + 1);
            size_t semi = sub.find(';');
            if (semi != std::string::npos) sub = sub.substr(0, semi);
            return sub;
        }
    }
    return "bin";
}

bool Format::has_rate_bypass() const {
    return url && url->find("ratebypass=yes") != std::string::npos;
}

long long Format::tbr() const {
    long long v = 0;
    if (bitrate) {
        v = *bitrate;
    } else if (avg_bitrate) {
        v = *avg_bitrate;
    }
    return v / 1000;
}

std::optional<uint64_t> Format::size_bytes() const {
    std::optional<long long> v = content_length ? content_length : filesize;
    if (!v || *v <= 0) return std::nullopt;
    return static_cast<uint64_t>(*v);
}

std::string Format::size_str() const {
    auto bytes = size_bytes();
    if (!bytes) return "unknown";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2fMiB", static_cast<double>(*bytes) / 1048576.0);
    return buf;
}

Format parse_format(const json& value) {
    Format f;
    f.itag = flex_int(value.contains("itag") ? value["itag"] : json()).value_or(0);
    f.url = opt_str(value, "url");
    f.signature_cipher = opt_str(value, "signatureCipher");
    f.mime_type = opt_str(value, "mimeType");
    f.bitrate = opt_int(value, "bitrate");
    f.avg_bitrate = opt_int(value, "averageBitrate");
    f.content_length = opt_int(value, "contentLength");
    f.approx_duration_ms = opt_int(value, "approxDurationMs");
    f.quality_label = opt_str(value, "qualityLabel");
    f.quality = opt_str(value, "quality");
    f.width = opt_int(value, "width");
    f.height = opt_int(value, "height");
    f.fps = opt_int(value, "fps");
    f.audio_channels = opt_int(value, "audioChannels");
    f.audio_sample_rate = opt_int(value, "audioSampleRate");
    f.audio_quality = opt_str(value, "audioQuality");
    if (value.contains("audioTrack") && value["audioTrack"].is_object()) {
        f.audio_track = AudioTrack{opt_str(value["audioTrack"], "id")};
    }
    f.language_code = opt_str(value, "language");
    f.container = opt_str(value, "container");
    f.codecs = opt_str(value, "codecs");
    f.filesize = opt_int(value, "filesize");
    if (value.contains("isDrc")) f.is_drc = value["isDrc"].is_boolean() && value["isDrc"].get<bool>();
    return f;
}

const Thumbnail* Thumbnails::best() const {
    const Thumbnail* best = nullptr;
    long long best_area = -1;
    for (const auto& t : thumbnails) {
        long long area = t.width.value_or(0) * t.height.value_or(0);
        if (area > best_area) {
            best_area = area;
            best = &t;
        }
    }
    return best;
}

std::optional<uint64_t> VideoDetails::duration_secs() const {
    if (!length_seconds) return std::nullopt;
    std::string s = util::trim(*length_seconds);
    uint64_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

std::string CaptionTrack::display_name() const {
    if (name) return name->text;
    return language_code.value_or("");
}

bool PlayabilityStatus::is_ok() const { return util::eq_ignore_ascii_case(status, "ok"); }

std::string PlayabilityStatus::message() const {
    if (reason) return *reason;
    if (!error_runs.empty()) {
        std::string joined;
        for (const auto& r : error_runs) joined += r.text;
        return joined;
    }
    return status;
}

PlayerResponse parse_player_response(const json& value) {
    PlayerResponse out;
    if (value.contains("playabilityStatus")) {
        const json& ps = value["playabilityStatus"];
        out.playability_status.status = opt_str(ps, "status").value_or("");
        out.playability_status.reason = opt_str(ps, "reason");
        if (ps.contains("errorScreen") && ps["errorScreen"].is_object()) {
            const json& screen = ps["errorScreen"];
            if (screen.contains("playerErrorMessageRenderer") &&
                screen["playerErrorMessageRenderer"].is_object()) {
                const json& msg = screen["playerErrorMessageRenderer"];
                if (msg.contains("subreason") && msg["subreason"].is_object()) {
                    out.playability_status.error_runs =
                        parse_runs(msg["subreason"].value("runs", json::array()));
                }
            }
        }
    }
    if (value.contains("streamingData") && value["streamingData"].is_object()) {
        const json& sd = value["streamingData"];
        StreamingData streaming;
        streaming.expires_in_seconds = opt_str(sd, "expiresInSeconds");
        if (sd.contains("formats") && sd["formats"].is_array()) {
            for (const auto& f : sd["formats"]) streaming.formats.push_back(parse_format(f));
        }
        if (sd.contains("adaptiveFormats") && sd["adaptiveFormats"].is_array()) {
            for (const auto& f : sd["adaptiveFormats"])
                streaming.adaptive_formats.push_back(parse_format(f));
        }
        streaming.hls_manifest_url = opt_str(sd, "hlsManifestUrl");
        streaming.dash_manifest_url = opt_str(sd, "dashManifestUrl");
        out.streaming_data = std::move(streaming);
    }
    if (value.contains("videoDetails") && value["videoDetails"].is_object()) {
        const json& vd = value["videoDetails"];
        VideoDetails details;
        details.video_id = opt_str(vd, "videoId").value_or("");
        details.title = opt_str(vd, "title").value_or("");
        details.length_seconds = opt_str(vd, "lengthSeconds");
        details.channel_id = opt_str(vd, "channelId");
        details.author = opt_str(vd, "author");
        details.short_description = opt_str(vd, "shortDescription");
        details.view_count = opt_str(vd, "viewCount");
        if (vd.contains("isLiveContent") && vd["isLiveContent"].is_boolean())
            details.is_live_content = vd["isLiveContent"].get<bool>();
        if (vd.contains("thumbnail") && vd["thumbnail"].is_object() &&
            vd["thumbnail"].contains("thumbnails") && vd["thumbnail"]["thumbnails"].is_array()) {
            Thumbnails thumbs;
            for (const auto& t : vd["thumbnail"]["thumbnails"]) {
                Thumbnail thumb;
                thumb.url = opt_str(t, "url").value_or("");
                thumb.width = opt_int(t, "width");
                thumb.height = opt_int(t, "height");
                thumbs.thumbnails.push_back(std::move(thumb));
            }
            details.thumbnail = std::move(thumbs);
        }
        out.video_details = std::move(details);
    }
    if (value.contains("captions") && value["captions"].is_object()) {
        const json& caps = value["captions"];
        if (caps.contains("playerCaptionsTracklistRenderer") &&
            caps["playerCaptionsTracklistRenderer"].is_object()) {
            const json& tracklist = caps["playerCaptionsTracklistRenderer"];
            TrackList list;
            if (tracklist.contains("captionTracks") && tracklist["captionTracks"].is_array()) {
                for (const auto& ct : tracklist["captionTracks"]) {
                    CaptionTrack track;
                    track.base_url = opt_str(ct, "baseUrl").value_or("");
                    if (ct.contains("name") && ct["name"].is_object()) {
                        track.name = TextRun{opt_str(ct["name"], "simpleText").value_or("")};
                    }
                    track.vss_id = opt_str(ct, "vssId");
                    track.language_code = opt_str(ct, "languageCode");
                    track.kind = opt_str(ct, "kind");
                    if (ct.contains("isTranslatable") && ct["isTranslatable"].is_boolean())
                        track.is_translatable = ct["isTranslatable"].get<bool>();
                    list.caption_tracks.push_back(std::move(track));
                }
            }
            Captions captions;
            captions.player_captions_tracklist_renderer = std::move(list);
            out.captions = std::move(captions);
        }
    }
    return out;
}

std::vector<Format> all_formats(const StreamingData& streaming) {
    std::vector<Format> out = streaming.formats;
    out.insert(out.end(), streaming.adaptive_formats.begin(), streaming.adaptive_formats.end());
    return out;
}

}  // namespace ytgrab::models
