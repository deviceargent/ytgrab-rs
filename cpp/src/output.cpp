#include "output.hpp"

#include <cctype>
#include <map>

#include "util.hpp"

namespace ytgrab::output {

std::string format_duration(uint64_t secs) { return util::format_duration(secs); }

namespace {

std::map<std::string, std::string> to_map(const TemplateVars& vars) {
    std::map<std::string, std::string> m;
    m["title"] = vars.title;
    m["id"] = vars.id;
    m["ext"] = vars.ext;
    m["uploader"] = vars.uploader;
    m["artist"] = vars.uploader;
    m["duration"] = vars.duration_secs ? format_duration(*vars.duration_secs) : "";
    m["duration_string"] = m["duration"];
    m["resolution"] = vars.resolution;

    std::string height = vars.resolution;
    while (!height.empty() && height.back() == 'p') height.pop_back();
    m["height"] = height;
    m["format_id"] = vars.format_id;
    m["playlist_index"] =
        vars.playlist_index ? std::to_string(*vars.playlist_index) : std::string();
    m["playlist"] = vars.playlist_title;
    m["playlist_id"] = vars.playlist_id;
    m["epoch"] = std::to_string(vars.epoch);
    return m;
}

}  // namespace

std::string render(const std::string& template_text, const TemplateVars& vars) {
    const auto map = to_map(vars);
    std::string out;
    out.reserve(template_text.size());
    const size_t n = template_text.size();
    size_t i = 0;

    while (i < n) {
        if (template_text[i] == '%' && i + 1 < n && template_text[i + 1] == '(') {
            size_t close = template_text.find(')', i + 2);
            if (close != std::string::npos) {
                const std::string key = template_text.substr(i + 2, close - (i + 2));
                auto it = map.find(key);
                const std::string value = it == map.end() ? "" : it->second;

                // Optional format spec, e.g. %(height)06d
                size_t j = close + 1;
                std::string spec;
                if (j < n && template_text[j] == '0') {
                    spec += template_text[j++];
                    while (j < n && std::isdigit(static_cast<unsigned char>(template_text[j]))) {
                        spec += template_text[j++];
                    }
                }
                if (j < n) {
                    char conv = template_text[j++];
                    if (conv == 'd') {
                        size_t width = 0;
                        bool digits = false;
                        for (char c : spec) {
                            if (c >= '0' && c <= '9') {
                                width = width * 10 + static_cast<size_t>(c - '0');
                                digits = true;
                            }
                        }
                        bool numeric = !value.empty();
                        for (size_t k = 0; k < value.size(); ++k) {
                            char c = value[k];
                            if (std::isdigit(static_cast<unsigned char>(c))) continue;
                            if (k == 0 && c == '-' && value.size() > 1) continue;
                            numeric = false;
                            break;
                        }
                        if (numeric && digits) {
                            std::string padded = value;
                            while (padded.size() < width) padded.insert(padded.begin(), '0');
                            out += padded;
                        } else {
                            out += value;
                        }
                    } else {
                        out += value;
                    }
                } else {
                    out += value;
                }
                i = j;
                continue;
            }
        }
        out += template_text[i++];
    }
    return out;
}

std::string sanitize(const std::string& input) {
    std::string cleaned;
    cleaned.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '/':
            case '\\':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
            case '\0':
                cleaned += '_';
                break;
            default:
                cleaned += (static_cast<unsigned char>(c) < 0x20) ? '_' : c;
        }
    }
    std::string trimmed = util::trim(cleaned);
    while (!trimmed.empty() && trimmed.front() == '.') trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && trimmed.back() == '.') trimmed.pop_back();
    trimmed = util::trim(trimmed);
    if (trimmed.empty()) return "video";
    return trimmed;
}

std::string sanitize_path(const std::string& path) {
    // Windows paths use '\\' but the template uses '/'; keep both working.
    std::string normalized = path;
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    const bool absolute = !normalized.empty() && normalized.front() == '/';
    const bool drive_absolute = normalized.size() > 1 &&
                                std::isalpha(static_cast<unsigned char>(normalized[0])) &&
                                normalized[1] == ':';

    std::vector<std::string> parts;
    for (const auto& piece : util::split(normalized, '/')) {
        if (piece.empty()) continue;
        parts.push_back(sanitize(piece));
    }
    std::string joined = util::join(parts, "/");
    if (drive_absolute) {
        // Keep "C:/..." and turn sanitize's "C_/" back into "C:/".
        if (joined.size() >= 2 && joined[0] == normalized[0] && joined[1] == '_') joined[1] = ':';
        return joined;
    }
    if (absolute) return "/" + joined;
    return joined;
}

}  // namespace ytgrab::output
