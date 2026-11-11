#include "subtitles.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <nlohmann/json.hpp>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::subtitles {
namespace {

std::string timestamp(int64_t ms, bool comma) {
    const char sep = comma ? ',' : '.';
    int64_t ms_part = ms % 1000;
    if (ms_part < 0) ms_part += 1000;
    const int64_t total_secs = (ms - ms_part) / 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld%c%03lld",
                  static_cast<long long>(total_secs / 3600),
                  static_cast<long long>((total_secs / 60) % 60),
                  static_cast<long long>(total_secs % 60), sep,
                  static_cast<long long>(ms_part));
    return buf;
}

}  // namespace

std::vector<Cue> parse_json3(const std::string& body) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception& e) {
        throw Error::json(e.what());
    }

    std::vector<Cue> cues;
    if (!doc.contains("events") || !doc["events"].is_array()) return cues;

    for (const auto& event : doc["events"]) {
        std::string text;
        if (event.contains("segs") && event["segs"].is_array()) {
            for (const auto& seg : event["segs"]) {
                if (seg.contains("utf8") && seg["utf8"].is_string()) {
                    text += seg["utf8"].get<std::string>();
                }
            }
        }
        std::replace(text.begin(), text.end(), '\n', ' ');
        text = util::trim(text);
        if (text.empty()) continue;

        Cue cue;
        cue.start_ms = event.value("tStartMs", static_cast<int64_t>(0));
        cue.duration_ms = event.value("dDurationMs", static_cast<int64_t>(0));
        if (cue.duration_ms <= 0) cue.duration_ms = 2000;
        cue.text = std::move(text);
        cues.push_back(std::move(cue));
    }
    return cues;
}

std::string to_srt(const std::vector<Cue>& cues) {
    std::string out;
    for (size_t i = 0; i < cues.size(); ++i) {
        const Cue& cue = cues[i];
        const int64_t end = cue.start_ms + cue.duration_ms;
        out += std::to_string(i + 1) + "\n" + timestamp(cue.start_ms, true) + " --> " +
               timestamp(end, true) + "\n" + cue.text + "\n\n";
    }
    return out;
}

std::string to_vtt(const std::vector<Cue>& cues) {
    std::string out = "WEBVTT\n\n";
    for (const auto& cue : cues) {
        const int64_t end = cue.start_ms + cue.duration_ms;
        out += timestamp(cue.start_ms, false) + " --> " + timestamp(end, false) + "\n" +
               cue.text + "\n\n";
    }
    return out;
}

void write_cues(const std::vector<Cue>& cues, const fs::path& path) {
    std::string ext = util::utf8(path.extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    const std::string body = ext == "vtt" ? to_vtt(cues) : to_srt(cues);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error::io("cannot write to " + util::utf8(path));
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

}  // namespace ytgrab::subtitles
