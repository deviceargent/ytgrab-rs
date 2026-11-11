//! `player` endpoint: everything we know about a single video.

#pragma once

#include <string>

#include "client.hpp"
#include "models.hpp"

namespace ytgrab::innertube::player {

/// Fetch the player payload for a video id.
models::PlayerResponse fetch(InnerTube& it, const std::string& video_id);

}  // namespace ytgrab::innertube::player
