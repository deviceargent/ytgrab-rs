//! `browse` endpoint: playlist listing.
//!
//! YouTube reshuffles the playlist JSON layout every few months, so instead
//! of modelling every nesting level we walk the response tree and pick out
//! the renderer objects we care about. It is far more resilient than a strict
//! struct hierarchy.

use serde_json::{json, Value};

use crate::error::Result;
use crate::innertube::client::InnerTube;

#[derive(Debug, Clone)]
pub struct PlaylistItem {
    pub video_id: String,
    pub title: String,
    pub index: Option<u64>,
    pub duration_secs: Option<u64>,
}

/// Recursively collect the value of every `key` found in the tree.
fn find_all_by_key<'a>(value: &'a Value, key: &str, out: &mut Vec<&'a Value>) {
    match value {
        Value::Object(map) => {
            if let Some(inner) = map.get(key) {
                out.push(inner);
            }
            for v in map.values() {
                find_all_by_key(v, key, out);
            }
        }
        Value::Array(items) => {
            for v in items {
                find_all_by_key(v, key, out);
            }
        }
        _ => {}
    }
}

fn find_continuation(value: &Value) -> Option<String> {
    let mut found = Vec::new();
    find_all_by_key(value, "continuationCommand", &mut found);
    for c in found {
        if let Some(token) = c.get("token").and_then(|t| t.as_str()) {
            if !token.is_empty() {
                return Some(token.to_string());
            }
        }
    }
    // `nextContinuationData` is used on some playlist pages.
    let mut next = Vec::new();
    find_all_by_key(value, "nextContinuationData", &mut next);
    for n in next {
        if let Some(token) = n.get("continuation").and_then(|t| t.as_str()) {
            if !token.is_empty() {
                return Some(token.to_string());
            }
        }
    }
    None
}

/// List every video in a playlist. Follows continuations until exhausted.
pub async fn playlist(it: &InnerTube, playlist_id: &str) -> Result<Vec<PlaylistItem>> {
    let mut items: Vec<PlaylistItem> = Vec::new();
    let mut continuation: Option<String> = None;
    let mut seen_tokens: Vec<String> = Vec::new();

    loop {
        let mut body = it.context_object().await;
        if let Some(token) = &continuation {
            body["continuation"] = json!(token);
        } else {
            body["browseId"] = json!(format!("VL{playlist_id}"));
        }

        let raw = it.post("browse", body).await?;
        items.extend(collect_items(&raw));

        match find_continuation(&raw) {
            Some(token) if !seen_tokens.contains(&token) => {
                seen_tokens.push(token.clone());
                continuation = Some(token);
            }
            _ => break,
        }
        if items.len() > 10_000 {
            break;
        }
    }

    Ok(items)
}

fn collect_items(raw: &Value) -> Vec<PlaylistItem> {
    let mut renderers = Vec::new();
    find_all_by_key(raw, "playlistVideoRenderer", &mut renderers);

    renderers
        .into_iter()
        .filter_map(|r| {
            let video_id = r.get("videoId")?.as_str()?.to_string();
            let title = r
                .get("title")
                .and_then(|t| {
                    t.get("runs")
                        .and_then(|runs| runs.as_array())
                        .map(|runs| {
                            runs.iter()
                                .filter_map(|x| x.get("text").and_then(|t| t.as_str()))
                                .collect::<String>()
                        })
                })
                .or_else(|| {
                    r.get("title")
                        .and_then(|t| t.get("simpleText"))
                        .and_then(|t| t.as_str())
                        .map(str::to_string)
                })
                .unwrap_or_default();
            let index = r
                .get("index")
                .and_then(|i| i.get("simpleText"))
                .and_then(|t| t.as_str())
                .and_then(|t| t.parse::<u64>().ok());
            let duration_secs = r
                .get("lengthSeconds")
                .and_then(|d| d.as_str())
                .and_then(|d| d.parse::<u64>().ok());
            Some(PlaylistItem {
                video_id,
                title,
                index,
                duration_secs,
            })
        })
        .collect()
}
