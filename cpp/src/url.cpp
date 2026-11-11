#include "url.hpp"

#include <cctype>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::url {
namespace {

bool is_id_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

}  // namespace

bool looks_like_video_id(const std::string& s) {
    if (s.size() != 11) return false;
    for (char c : s) {
        if (!is_id_char(c)) return false;
    }
    return true;
}

std::optional<ParsedUrl> parse_url(const std::string& input) {
    ParsedUrl out;
    std::string rest = util::trim(input);

    size_t scheme_end = rest.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;
    out.scheme = util::to_lower(rest.substr(0, scheme_end));
    rest = rest.substr(scheme_end + 3);

    size_t authority_end = rest.find_first_of("/?#");
    std::string authority = rest.substr(0, authority_end);
    if (authority.empty()) return std::nullopt;

    size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        // Keep IPv6 literals intact; ports are not needed by the parser.
        out.host = authority.substr(0, colon);
    } else {
        out.host = authority;
    }
    out.host = util::to_lower(out.host);

    rest = authority_end == std::string::npos ? "" : rest.substr(authority_end);
    size_t fragment = rest.find('#');
    if (fragment != std::string::npos) rest = rest.substr(0, fragment);
    size_t question = rest.find('?');
    std::string query;
    if (question != std::string::npos) {
        query = rest.substr(question + 1);
        rest = rest.substr(0, question);
    }
    out.path = rest.empty() ? "/" : rest;

    if (!query.empty()) {
        for (const auto& piece : util::split(query, '&')) {
            if (piece.empty()) continue;
            size_t eq = piece.find('=');
            std::string key =
                util::percent_decode(eq == std::string::npos ? piece : piece.substr(0, eq));
            std::string value =
                eq == std::string::npos ? "" : util::percent_decode(piece.substr(eq + 1));
            out.query.emplace_back(std::move(key), std::move(value));
        }
    }
    return out;
}

std::optional<std::string> ParsedUrl::query_value(const std::string& key) const {
    for (const auto& [k, v] : query) {
        if (k == key) return v;
    }
    return std::nullopt;
}

std::string ParsedUrl::rebuild(
    const std::vector<std::pair<std::string, std::string>>& pairs) const {
    std::string out = scheme + "://" + host + path;
    if (!pairs.empty()) {
        out += "?";
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (i) out += "&";
            out += pairs[i].first + "=" + pairs[i].second;
        }
    }
    return out;
}

Target parse(const std::string& input) {
    const std::string trimmed = util::trim(input);

    if (looks_like_video_id(trimmed)) {
        Target t;
        t.kind = Target::Kind::Video;
        t.id = trimmed;
        return t;
    }

    // Accept URLs with or without a scheme.
    const std::string with_scheme =
        (util::starts_with(trimmed, "http://") || util::starts_with(trimmed, "https://"))
            ? trimmed
            : "https://" + trimmed;

    auto parsed = parse_url(with_scheme);
    if (!parsed) throw Error::unsupported_url(trimmed);

    std::string host = parsed->host;
    if (util::starts_with(host, "www.")) host = host.substr(4);
    if (util::starts_with(host, "m.")) host = host.substr(2);
    std::string path = parsed->path;
    while (!path.empty() && path.back() == '/') path.pop_back();

    std::optional<std::string> video_id = parsed->query_value("v");
    std::optional<std::string> playlist_id = parsed->query_value("list");

    if (host == "youtu.be") {
        std::string id = path;
        if (!id.empty() && id.front() == '/') id.erase(id.begin());
        if (id.empty()) throw Error::unsupported_url(trimmed);
        Target t;
        t.kind = Target::Kind::Video;
        t.id = id;
        t.playlist = playlist_id;
        return t;
    }

    if (host == "youtube.com" || host == "youtube-nocookie.com" || host == "m.youtube.com") {
        std::vector<std::string> segments;
        for (const auto& seg : util::split(path, '/')) {
            if (!seg.empty()) segments.push_back(seg);
        }
        std::string first = segments.empty() ? "" : segments[0];
        if (first == "watch") {
            if (video_id) {
                Target t;
                t.kind = Target::Kind::Video;
                t.id = *video_id;
                t.playlist = playlist_id;
                return t;
            }
        } else if (first == "shorts" || first == "embed" || first == "live" || first == "v") {
            if (segments.size() > 1) {
                Target t;
                t.kind = Target::Kind::Video;
                t.id = segments[1];
                t.playlist = playlist_id;
                return t;
            }
        } else if (first == "playlist") {
            if (playlist_id) {
                Target t;
                t.kind = Target::Kind::Playlist;
                t.id = *playlist_id;
                return t;
            }
        }
        if (video_id) {
            Target t;
            t.kind = Target::Kind::Video;
            t.id = *video_id;
            t.playlist = playlist_id;
            return t;
        }
        if (playlist_id) {
            Target t;
            t.kind = Target::Kind::Playlist;
            t.id = *playlist_id;
            return t;
        }
    }

    throw Error::unsupported_url(trimmed);
}

}  // namespace ytgrab::url
