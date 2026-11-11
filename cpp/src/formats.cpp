#include "formats.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::formats {
namespace {

int cmp(long long a, long long b) { return a < b ? -1 : (a > b ? 1 : 0); }

std::optional<long long> parse_i64(const std::string& s) {
    if (s.empty()) return std::nullopt;
    long long v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

std::optional<double> parse_f64(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t used = 0;
        double v = std::stod(s, &used);
        if (used != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

struct HeadAndFilters {
    std::string head;
    std::vector<Filter> filters;
};

HeadAndFilters split_head_and_filters(const std::string& raw, bool* ok, std::string* error) {
    HeadAndFilters out;
    std::string rest = raw;
    size_t open = 0;
    while ((open = rest.find('[')) != std::string::npos) {
        out.head += rest.substr(0, open);
        std::string after = rest.substr(open + 1);
        size_t close = after.find(']');
        if (close == std::string::npos) {
            *ok = false;
            *error = "unterminated filter in `" + raw + "`";
            return out;
        }
        // parse_filters
        for (const auto& raw_chunk : util::split(after.substr(0, close), ',')) {
            std::string chunk = util::trim(raw_chunk);
            if (chunk.empty()) continue;
            std::string key, value;
            Op op;
            size_t pos;
            if ((pos = chunk.find("<=")) != std::string::npos) {
                key = chunk.substr(0, pos);
                value = chunk.substr(pos + 2);
                op = Op::Le;
            } else if ((pos = chunk.find(">=")) != std::string::npos) {
                key = chunk.substr(0, pos);
                value = chunk.substr(pos + 2);
                op = Op::Ge;
            } else if ((pos = chunk.find('=')) != std::string::npos) {
                key = chunk.substr(0, pos);
                value = chunk.substr(pos + 1);
                op = Op::Eq;
            } else if ((pos = chunk.find('<')) != std::string::npos) {
                key = chunk.substr(0, pos);
                value = chunk.substr(pos + 1);
                op = Op::Lt;
            } else if ((pos = chunk.find('>')) != std::string::npos) {
                key = chunk.substr(0, pos);
                value = chunk.substr(pos + 1);
                op = Op::Gt;
            } else {
                *ok = false;
                *error = "bad filter `" + chunk + "`";
                return out;
            }
            out.filters.push_back(
                Filter{util::to_lower(util::trim(key)), op, util::trim(value)});
        }
        rest = after.substr(close + 1);
    }
    out.head += rest;
    *ok = true;
    return out;
}

Part parse_part(const std::string& raw) {
    bool ok = false;
    std::string error;
    HeadAndFilters split = split_head_and_filters(raw, &ok, &error);
    if (!ok) throw Error::other(error);

    std::string head = util::to_lower(util::trim(split.head));

    // Bare height, e.g. "1080" or "1080p".
    if (!head.empty() && head.back() == 'p') {
        if (auto num = parse_i64(head.substr(0, head.size() - 1))) {
            Part p;
            p.kind = Kind::Video;
            p.itag = *num;
            p.height_cap = *num;
            return p;
        }
    }
    if (auto num = parse_i64(head)) {
        Part p;
        p.kind = Kind::Video;
        p.itag = *num;
        p.height_cap = *num;
        return p;
    }

    Kind kind;
    bool worst = false;
    if (head == "best") {
        kind = Kind::Combined;
    } else if (head == "worst") {
        kind = Kind::Combined;
        worst = true;
    } else if (head == "bestvideo" || head == "bv") {
        kind = Kind::Video;
    } else if (head == "worstvideo" || head == "wv") {
        kind = Kind::Video;
        worst = true;
    } else if (head == "bestaudio" || head == "ba") {
        kind = Kind::Audio;
    } else if (head == "worstaudio" || head == "wa") {
        kind = Kind::Audio;
        worst = true;
    } else {
        throw Error::other("unknown format selector `" + head +
                           "` (try best, bestvideo+bestaudio, 1080, ...)");
    }

    Part p;
    p.kind = kind;
    p.worst = worst;
    p.filters = std::move(split.filters);
    return p;
}

std::optional<std::string> filter_value(const Format& format, const std::string& key) {
    if (key == "height") return std::to_string(format.height_val());
    if (key == "fps") return std::to_string(format.fps_val());
    if (key == "tbr" || key == "vbr") return std::to_string(format.tbr());
    if (key == "filesize") {
        if (auto b = format.size_bytes()) return std::to_string(*b);
        return std::nullopt;
    }
    if (key == "ext") return util::to_lower(format.ext());
    if (key == "vcodec") return format.vcodec();
    if (key == "acodec") return format.acodec();
    if (key == "abr") {
        if (format.is_audio_only()) return std::to_string(format.tbr());
        return std::nullopt;
    }
    return std::nullopt;
}

bool matches(const Format& format, const Filter& filter) {
    auto actual = filter_value(format, filter.key);
    if (!actual) return false;
    auto numeric = parse_f64(*actual);
    auto expected = parse_f64(filter.value);

    if (numeric && expected) {
        const double a = *numeric;
        const double e = *expected;
        switch (filter.op) {
            case Op::Eq:
                return std::fabs(a - e) < 1e-9;
            case Op::Le:
                return a <= e;
            case Op::Ge:
                return a >= e;
            case Op::Lt:
                return a < e;
            case Op::Gt:
                return a > e;
        }
        return false;
    }
    // Strings: only equality makes sense; <=/>= are treated as equality.
    return util::eq_ignore_ascii_case(*actual, filter.value);
}

/// H.264 first (device compatibility), then VP9, then AV1.
int video_codec_rank(const Format& format) {
    auto codec = format.vcodec();
    if (!codec) return 0;
    if (util::starts_with(*codec, "avc1")) return 3;
    if (util::starts_with(*codec, "vp9") || util::starts_with(*codec, "vp09")) return 2;
    if (util::starts_with(*codec, "av01")) return 1;
    return 0;
}

/// m4a/AAC first for maximum player support.
int audio_codec_rank(const Format& format, bool prefer_opus) {
    const std::string ext = format.ext();
    const bool aac = ext == "m4a" || ext == "mp4";
    const bool opus = ext == "webm";
    if (aac && !opus) return prefer_opus ? 2 : 3;
    if (!aac && opus) return prefer_opus ? 3 : 2;
    return 1;
}

/// Negative means `a` is worse than `b`; positive means better.
int cmp_video(const Format& a, const Format& b, bool prefer_servable) {
    if (prefer_servable) {
        int s = cmp(a.has_rate_bypass() ? 1 : 0, b.has_rate_bypass() ? 1 : 0);
        if (s != 0) return s;
    }
    if (int c = cmp(a.height_val(), b.height_val())) return c;
    if (int c = cmp(a.fps_val(), b.fps_val())) return c;
    if (int c = cmp(video_codec_rank(a), video_codec_rank(b))) return c;
    if (int c = cmp(a.tbr(), b.tbr())) return c;
    return cmp(b.itag, a.itag);
}

int cmp_audio(const Format& a, const Format& b, bool prefer_opus, bool prefer_servable) {
    if (prefer_servable) {
        int s = cmp(a.has_rate_bypass() ? 1 : 0, b.has_rate_bypass() ? 1 : 0);
        if (s != 0) return s;
    }
    if (int c = cmp(audio_codec_rank(a, prefer_opus), audio_codec_rank(b, prefer_opus))) return c;
    if (int c = cmp(a.tbr(), b.tbr())) return c;
    return cmp(b.itag, a.itag);
}

template <typename Cmp>
Format pick_best(std::vector<const Format*>& candidates, bool worst, Cmp compare) {
    std::sort(candidates.begin(), candidates.end(),
              [&](const Format* a, const Format* b) { return compare(*a, *b) < 0; });
    return worst ? *candidates.front() : *candidates.back();
}

Format pick_video(const std::vector<Format>& formats, const Part& part, bool prefer_servable) {
    // An explicit format id wins over everything else.
    if (part.itag) {
        for (const auto& f : formats) {
            if (f.itag == *part.itag) return f;
        }
        // No such format id: treat the number as a height cap.
        Part height_part;
        height_part.kind = part.kind;
        height_part.worst = part.worst;
        height_part.filters = {Filter{"height", Op::Le, std::to_string(*part.itag)}};
        bool any = false;
        for (const auto& f : formats) {
            if (f.has_video() && matches(f, height_part.filters[0])) {
                any = true;
                break;
            }
        }
        if (any) return pick_video(formats, height_part, prefer_servable);
    }

    std::vector<const Format*> candidates;
    for (const auto& f : formats) {
        if (!f.has_video()) continue;
        bool all = true;
        for (const auto& filter : part.filters) {
            if (!matches(f, filter)) {
                all = false;
                break;
            }
        }
        if (all) candidates.push_back(&f);
    }

    if (prefer_servable) {
        std::vector<const Format*> servable;
        for (const Format* f : candidates) {
            if (f->has_rate_bypass()) servable.push_back(f);
        }
        if (!servable.empty()) candidates = std::move(servable);
    } else {
        std::vector<const Format*> video_only;
        for (const Format* f : candidates) {
            if (f->is_video_only()) video_only.push_back(f);
        }
        if (!video_only.empty()) candidates = std::move(video_only);
    }

    if (candidates.empty()) {
        if (part.itag) throw Error::no_format("format id " + std::to_string(*part.itag));
        throw Error::no_format("any video stream");
    }
    return pick_best(candidates, part.worst,
                     [&](const Format& a, const Format& b) {
                         return cmp_video(a, b, prefer_servable);
                     });
}

Format pick_audio(const std::vector<Format>& formats, const Part& part, bool prefer_opus,
                  bool prefer_servable) {
    std::vector<const Format*> candidates;
    for (const auto& f : formats) {
        if (!f.has_audio()) continue;
        bool all = true;
        for (const auto& filter : part.filters) {
            if (!matches(f, filter)) {
                all = false;
                break;
            }
        }
        if (all) candidates.push_back(&f);
    }

    std::vector<const Format*> audio_only;
    for (const Format* f : candidates) {
        if (f->is_audio_only()) audio_only.push_back(f);
    }
    if (!audio_only.empty()) candidates = std::move(audio_only);

    if (candidates.empty()) throw Error::no_format("any audio stream");
    return pick_best(candidates, part.worst, [&](const Format& a, const Format& b) {
        return cmp_audio(a, b, prefer_opus, prefer_servable);
    });
}

Format pick_combined(const std::vector<Format>& formats, const Part& part) {
    std::vector<const Format*> candidates;
    for (const auto& f : formats) {
        if (!f.is_combined()) continue;
        bool all = true;
        for (const auto& filter : part.filters) {
            if (!matches(f, filter)) {
                all = false;
                break;
            }
        }
        if (all) candidates.push_back(&f);
    }
    if (candidates.empty()) throw Error::no_format("best combined");
    return pick_best(candidates, part.worst,
                     [](const Format& a, const Format& b) { return cmp_video(a, b, false); });
}

}  // namespace

Selector Selector::parse(const std::string& input) {
    std::string trimmed = util::trim(input);
    if (trimmed.empty()) throw Error::other("empty format selector");

    Selector selector;
    for (const auto& raw : util::split(trimmed, '+')) {
        std::string part = util::trim(raw);
        if (part.empty()) continue;
        selector.parts.push_back(parse_part(part));
    }
    if (selector.parts.empty()) {
        throw Error::other("could not parse format selector `" + trimmed + "`");
    }
    return selector;
}

bool Selector::is_single_stream() const {
    return parts.size() == 1 && parts[0].kind == Kind::Combined;
}

uint64_t Selection::total_size() const {
    uint64_t total = 0;
    for (const auto& f : videos) {
        if (auto b = f.size_bytes()) total += *b;
    }
    for (const auto& f : audios) {
        if (auto b = f.size_bytes()) total += *b;
    }
    return total;
}

Selection select(const Selector& selector, const std::vector<Format>& formats) {
    return select_with(selector, formats, false);
}

Selection select_with(const Selector& selector, const std::vector<Format>& formats,
                      bool prefer_opus) {
    return select_excluding(selector, formats, prefer_opus, {});
}

Selection select_excluding(const Selector& selector, const std::vector<Format>& formats,
                           bool prefer_opus, const std::vector<long long>& excluded) {
    return select_excluding_with(selector, formats, prefer_opus, excluded, false);
}

Selection select_excluding_with(const Selector& selector, const std::vector<Format>& formats,
                                bool prefer_opus, const std::vector<long long>& excluded,
                                bool prefer_servable) {
    std::vector<Format> available;
    for (const auto& f : formats) {
        if (std::find(excluded.begin(), excluded.end(), f.itag) == excluded.end()) {
            available.push_back(f);
        }
    }

    Selection selection;
    for (const auto& part : selector.parts) {
        switch (part.kind) {
            case Kind::Combined: {
                Format video = pick_video(available, part, prefer_servable);
                if (video.has_audio()) {
                    selection.videos.push_back(std::move(video));
                    continue;
                }
                // Match the audio codec to the video container: vp9 wants
                // opus so the result stays a .webm, h.264 wants AAC so it
                // stays a .mp4.
                const bool prefer_opus_for_video = prefer_opus || video.ext() == "webm";
                try {
                    Format audio =
                        pick_audio(available, part, prefer_opus_for_video, prefer_servable);
                    if (audio.is_audio_only()) {
                        selection.videos.push_back(std::move(video));
                        selection.audios.push_back(std::move(audio));
                        break;
                    }
                } catch (const Error&) {
                    // fall through to a progressive format
                }
                selection.videos.push_back(pick_combined(available, part));
                break;
            }
            case Kind::Video:
                selection.videos.push_back(pick_video(available, part, prefer_servable));
                break;
            case Kind::Audio: {
                const bool want_opus =
                    prefer_opus ||
                    (!selection.videos.empty() && selection.videos.front().ext() == "webm");
                selection.audios.push_back(
                    pick_audio(available, part, want_opus, prefer_servable));
                break;
            }
        }
    }

    if (selection.videos.empty() && selection.audios.empty()) {
        throw Error::no_format("the selector matched nothing");
    }
    return selection;
}

Format best_audio_only(const std::vector<Format>& formats, bool prefer_opus) {
    Part part;
    part.kind = Kind::Audio;
    return pick_audio(formats, part, prefer_opus, false);
}

}  // namespace ytgrab::formats
