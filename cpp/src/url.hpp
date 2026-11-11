//! Parsing YouTube URLs into video / playlist identifiers.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ytgrab::url {

struct Target {
    enum class Kind { Video, Playlist };
    Kind kind = Kind::Video;
    std::string id;
    /// Only set for videos: the `list=` parameter of a watch url.
    std::optional<std::string> playlist;
};

/// A bare 11-character YouTube id.
bool looks_like_video_id(const std::string& s);

Target parse(const std::string& input);

/// A very small absolute-url splitter (scheme, host, path, query pairs).
struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;

    /// The value of the first `key` in the query, if any.
    std::optional<std::string> query_value(const std::string& key) const;
    /// Rebuild the url with a replaced query string.
    std::string rebuild(const std::vector<std::pair<std::string, std::string>>& pairs) const;
};

std::optional<ParsedUrl> parse_url(const std::string& input);

}  // namespace ytgrab::url
