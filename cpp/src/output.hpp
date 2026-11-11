//! Output filename templating (yt-dlp style) and path sanitising.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ytgrab::output {

struct TemplateVars {
    std::string title;
    std::string id;
    std::string ext;
    std::string uploader;
    std::optional<uint64_t> duration_secs;
    std::string resolution;
    std::string format_id;
    std::optional<uint64_t> playlist_index;
    std::string playlist_title;
    std::string playlist_id;
    uint64_t epoch = 0;
};

/// `3661` -> `1:01:01`, `213` -> `3:33`
std::string format_duration(uint64_t secs);

/// Render a `%(field)s` template.
std::string render(const std::string& template_text, const TemplateVars& vars);

/// Make a string safe to use as a filename component.
std::string sanitize(const std::string& input);

/// Sanitise a whole path template, keeping directory separators intact.
std::string sanitize_path(const std::string& path);

}  // namespace ytgrab::output
