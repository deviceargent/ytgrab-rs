//! `browse` endpoint: playlist listing.
//!
//! The JSON layout changes every few months, so instead of modelling every
//! nesting level the tree is walked and the renderers we care about are
//! picked out. That is far more resilient than a strict hierarchy.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "client.hpp"

namespace ytgrab::innertube::browse {

struct PlaylistItem {
    std::string video_id;
    std::string title;
    std::optional<uint64_t> index;
    std::optional<uint64_t> duration_secs;
};

/// List every video in a playlist, following continuations until exhausted.
std::vector<PlaylistItem> playlist(InnerTube& it, const std::string& playlist_id);

}  // namespace ytgrab::innertube::browse
