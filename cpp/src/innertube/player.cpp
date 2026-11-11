#include "player.hpp"

#include "error.hpp"

namespace ytgrab::innertube::player {

models::PlayerResponse fetch(InnerTube& it, const std::string& video_id) {
    json body = it.context_object();
    body["videoId"] = video_id;
    body["contentCheckOk"] = true;
    body["racyCheckOk"] = true;
    // `playbackContext` is only accepted by the web-flavoured clients; the
    // ANDROID client rejects the whole request with a 400 when it is present.
    const std::string& name = it.context().name;
    if (name == "WEB" || name == "MWEB") {
        body["playbackContext"] = {
            {"contentPlaybackContext", {{"html5Preference", "HTML5_PREF_WANTS"}}}};
    }

    json raw = it.post("player", body);
    models::PlayerResponse parsed = models::parse_player_response(raw);

    if (!parsed.playability_status.is_ok()) {
        throw Error::unavailable(parsed.playability_status.message());
    }
    bool no_streams = true;
    if (parsed.streaming_data) {
        no_streams = parsed.streaming_data->formats.empty() &&
                     parsed.streaming_data->adaptive_formats.empty();
    }
    if (no_streams) {
        throw Error::unavailable("no playable streams in the player response");
    }
    return parsed;
}

}  // namespace ytgrab::innertube::player
