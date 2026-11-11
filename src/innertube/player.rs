//! `player` endpoint: everything we know about a single video.

use serde_json::json;

use crate::error::{Error, Result};
use crate::innertube::client::InnerTube;
use crate::innertube::models::{PlayerResponse, StreamingData};

/// Fetch the player payload for a video id.
pub async fn fetch(it: &InnerTube, video_id: &str) -> Result<PlayerResponse> {
    let mut body = it.context_object().await;
    body["videoId"] = json!(video_id);
    body["contentCheckOk"] = json!(true);
    body["racyCheckOk"] = json!(true);
    // `playbackContext` is only accepted by the web-flavoured clients; the
    // ANDROID client rejects the whole request with a 400 when it is present.
    if matches!(it.context().name, "WEB" | "MWEB") {
        body["playbackContext"] = json!({
            "contentPlaybackContext": {
                "html5Preference": "HTML5_PREF_WANTS"
            }
        });
    }

    let raw = it.post("player", body).await?;
    let parsed: PlayerResponse = serde_json::from_value(raw)?;

    if !parsed.playability_status.is_ok() {
        return Err(Error::Unavailable(parsed.playability_status.message()));
    }
    if parsed
        .streaming_data
        .as_ref()
        .map(|s| s.formats.is_empty() && s.adaptive_formats.is_empty())
        .unwrap_or(true)
    {
        return Err(Error::Unavailable(
            "no playable streams in the player response".into(),
        ));
    }
    Ok(parsed)
}

/// All formats (progressive + adaptive) flattened into one list.
pub fn all_formats(streaming: &StreamingData) -> Vec<crate::innertube::models::Format> {
    let mut v = streaming.formats.clone();
    v.extend(streaming.adaptive_formats.iter().cloned());
    v
}
