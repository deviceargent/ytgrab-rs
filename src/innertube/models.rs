//! Serde models for the InnerTube `player` and `browse` responses.
//!
//! Only the fields we actually consume are modelled; everything else is ignored.
//! YouTube returns a lot of noise in these payloads and the shape drifts, so
//! every optional field is defaulted.

use serde::Deserialize;

/// YouTube is inconsistent about numeric types: the same field arrives as a
/// JSON number in one response and as a string in the next. These helpers
/// accept either.
mod flex {
    use serde::{Deserialize, Deserializer};

    pub fn i64<'de, D>(d: D) -> Result<Option<i64>, D::Error>
    where
        D: Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(untagged)]
        enum Num {
            Int(i64),
            Float(f64),
            Str(String),
        }
        match Option::<Num>::deserialize(d)? {
            None => Ok(None),
            Some(Num::Int(v)) => Ok(Some(v)),
            Some(Num::Float(f)) => Ok(Some(f as i64)),
            Some(Num::Str(s)) => Ok(s.trim().parse::<i64>().ok()),
        }
    }

}

// ---------------------------------------------------------------------------
// player response
// ---------------------------------------------------------------------------

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlayerResponse {
    #[serde(default)]
    pub playability_status: PlayabilityStatus,
    #[serde(default)]
    pub streaming_data: Option<StreamingData>,
    #[serde(default)]
    pub video_details: Option<VideoDetails>,
    #[serde(default)]
    pub captions: Option<Captions>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlayabilityStatus {
    #[serde(default)]
    pub status: String,
    #[serde(default)]
    pub reason: Option<String>,
    #[serde(default)]
    pub error_screen: Option<ErrorScreen>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct ErrorScreen {
    #[serde(default)]
    pub player_error_message_renderer: Option<PlayerErrorMessage>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlayerErrorMessage {
    #[serde(default)]
    pub subreason: Option<Subreason>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct Subreason {
    #[serde(default)]
    pub runs: Vec<TextRun>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct TextRun {
    #[serde(default)]
    pub text: String,
}

impl PlayabilityStatus {
    /// `OK` means we can go ahead; anything else carries a reason we should
    /// surface to the user rather than failing with a cryptic error.
    pub fn is_ok(&self) -> bool {
        self.status.eq_ignore_ascii_case("ok")
    }

    pub fn message(&self) -> String {
        self.reason
            .clone()
            .or_else(|| {
                self.error_screen
                    .as_ref()
                    .and_then(|e| e.player_error_message_renderer.as_ref())
                    .and_then(|m| m.subreason.as_ref())
                    .map(|s| s.runs.iter().map(|r| r.text.as_str()).collect::<String>())
            })
            .unwrap_or_else(|| self.status.clone())
    }
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct StreamingData {
    #[serde(default)]
    pub expires_in_seconds: Option<String>,
    #[serde(default)]
    pub formats: Vec<Format>,
    #[serde(default)]
    pub adaptive_formats: Vec<Format>,
    #[serde(default)]
    pub hls_manifest_url: Option<String>,
    #[serde(default)]
    pub dash_manifest_url: Option<String>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Format {
    #[serde(default)]
    pub itag: i64,
    #[serde(default)]
    pub url: Option<String>,
    /// Present when YouTube withholds the direct URL. The ANDROID client
    /// never uses it, but the WEB client does.
    #[serde(default)]
    pub signature_cipher: Option<String>,
    #[serde(default)]
    pub mime_type: Option<String>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub bitrate: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub avg_bitrate: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub content_length: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub approx_duration_ms: Option<i64>,
    #[serde(default)]
    pub quality_label: Option<String>,
    #[serde(default)]
    pub quality: Option<String>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub width: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub height: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub fps: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub audio_channels: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub audio_sample_rate: Option<i64>,
    #[serde(default)]
    pub audio_quality: Option<String>,
    #[serde(default)]
    pub audio_track: Option<AudioTrack>,
    #[serde(default)]
    pub language_code: Option<String>,
    #[serde(default)]
    pub container: Option<String>,
    #[serde(default)]
    pub codecs: Option<String>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub filesize: Option<i64>,
    #[serde(default)]
    pub is_drc: bool,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct AudioTrack {
    #[serde(default)]
    pub id: Option<String>,
}

impl Format {
    /// Video-only or video+audio.
    pub fn has_video(&self) -> bool {
        if let Some(m) = &self.mime_type {
            if m.starts_with("audio/") {
                return false;
            }
            if m.starts_with("video/") {
                return true;
            }
        }
        self.height.unwrap_or(0) > 0 || self.codecs_list().len() == 2
    }

    /// Audio-only or video+audio.
    pub fn has_audio(&self) -> bool {
        if let Some(m) = &self.mime_type {
            if m.starts_with("audio/") {
                return true;
            }
            if m.starts_with("video/") {
                return self.codecs_list().len() > 1;
            }
        }
        self.codecs_list().len() > 1
    }

    pub fn is_video_only(&self) -> bool {
        self.has_video() && !self.has_audio()
    }

    pub fn is_audio_only(&self) -> bool {
        self.has_audio() && !self.has_video()
    }

    pub fn is_combined(&self) -> bool {
        self.has_video() && self.has_audio()
    }

    /// The raw `codecs="..."` value. In InnerTube player responses the codecs
    /// live inside the mime type (`video/mp4; codecs="avc1.640028, mp4a.40.2"`)
    /// rather than in their own field.
    fn codecs_raw(&self) -> Option<&str> {
        if let Some(c) = self.codecs.as_deref() {
            if !c.is_empty() {
                return Some(c);
            }
        }
        let mime = self.mime_type.as_deref()?;
        let start = mime.find("codecs=")? + "codecs=".len();
        let rest = &mime[start..];
        let rest = rest.strip_prefix('"').unwrap_or(rest);
        let end = rest.find('"').unwrap_or(rest.len());
        Some(&rest[..end])
    }

    /// The individual codecs, in order. Audio-only formats list a single
    /// (audio) codec; video formats list video first, audio second.
    fn codecs_list(&self) -> Vec<&str> {
        self.codecs_raw()
            .map(|c| {
                c.split(',')
                    .map(str::trim)
                    .filter(|s| !s.is_empty() && *s != "none")
                    .collect()
            })
            .unwrap_or_default()
    }

    /// True when the mime type says this is a pure audio stream.
    fn is_audio_mime(&self) -> bool {
        self.mime_type
            .as_deref()
            .map(|m| m.starts_with("audio/"))
            .unwrap_or(false)
    }

    pub fn vcodec(&self) -> Option<&str> {
        if self.is_audio_mime() {
            return None;
        }
        self.codecs_list().first().copied()
    }

    pub fn acodec(&self) -> Option<&str> {
        let list = self.codecs_list();
        if self.is_audio_mime() {
            return list.first().copied();
        }
        list.get(1).copied()
    }

    /// Container extension: `mp4`, `webm`, ...
    pub fn ext(&self) -> String {
        if let Some(c) = &self.container {
            if !c.is_empty() {
                return c.clone();
            }
        }
        if let Some(m) = &self.mime_type {
            if let Some(sub) = m.split('/').nth(1) {
                let sub = sub.split(';').next().unwrap_or(sub);
                return sub.to_string();
            }
        }
        "bin".to_string()
    }

    pub fn size_bytes(&self) -> Option<u64> {
        self.content_length
            .or(self.filesize)
            .and_then(|v| if v > 0 { Some(v as u64) } else { None })
    }

    /// Total bitrate in kbps, falling back to the average bitrate.
    pub fn tbr(&self) -> i64 {
        self.bitrate.or(self.avg_bitrate).unwrap_or(0) / 1000
    }

    pub fn height(&self) -> i64 {
        self.height.unwrap_or(0)
    }

    pub fn fps(&self) -> i64 {
        self.fps.unwrap_or(0)
    }

    /// Human readable size, yt-dlp style.
    pub fn size_str(&self) -> String {
        match self.size_bytes() {
            Some(b) => format!("{:.2}MiB", b as f64 / 1048576.0),
            None => "unknown".to_string(),
        }
    }
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct VideoDetails {
    #[serde(default)]
    pub video_id: String,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub length_seconds: Option<String>,
    #[serde(default)]
    pub channel_id: Option<String>,
    #[serde(default)]
    pub author: Option<String>,
    #[serde(default)]
    pub short_description: Option<String>,
    #[serde(default)]
    pub view_count: Option<String>,
    #[serde(default)]
    pub is_live_content: Option<bool>,
    #[serde(default)]
    pub thumbnail: Option<Thumbnails>,
}

impl VideoDetails {
    pub fn duration_secs(&self) -> Option<u64> {
        self.length_seconds.as_ref().and_then(|s| s.parse().ok())
    }
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Thumbnails {
    #[serde(default)]
    pub thumbnails: Vec<Thumbnail>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Thumbnail {
    #[serde(default)]
    pub url: String,
    #[serde(default, deserialize_with = "flex::i64")]
    pub width: Option<i64>,
    #[serde(default, deserialize_with = "flex::i64")]
    pub height: Option<i64>,
}

impl Thumbnails {
    pub fn best(&self) -> Option<&Thumbnail> {
        self.thumbnails
            .iter()
            .max_by_key(|t| t.width.unwrap_or(0) * t.height.unwrap_or(0))
    }
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct Captions {
    #[serde(default)]
    pub player_captions_tracklist_renderer: Option<TrackList>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TrackList {
    #[serde(default)]
    pub caption_tracks: Vec<CaptionTrack>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct CaptionTrack {
    #[serde(default)]
    pub base_url: String,
    #[serde(default)]
    pub name: Option<TextRun>,
    #[serde(default)]
    pub vss_id: Option<String>,
    #[serde(default)]
    pub language_code: Option<String>,
    #[serde(default)]
    pub kind: Option<String>,
    #[serde(default)]
    pub is_translatable: Option<bool>,
}

impl CaptionTrack {
    pub fn display_name(&self) -> String {
        self.name
            .as_ref()
            .map(|n| n.text.clone())
            .unwrap_or_else(|| self.language_code.clone().unwrap_or_default())
    }
}

// ---------------------------------------------------------------------------
// browse response (playlists / channels)
// ---------------------------------------------------------------------------

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct BrowseResponse {
    #[serde(default)]
    pub contents: Option<BrowseContents>,
    #[serde(default)]
    pub on_response_received_actions: Option<Vec<serde_json::Value>>,
    #[serde(default)]
    pub header: Option<serde_json::Value>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct BrowseContents {
    #[serde(default)]
    pub single_column_browse_tab_renderer: Option<SingleColumn>,
    #[serde(default)]
    pub two_column_browse_results_renderer: Option<TwoColumn>,
    #[serde(default)]
    pub section_list_renderer: Option<SectionList>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SingleColumn {
    #[serde(default)]
    pub tabs: Vec<Tab>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct Tab {
    #[serde(default)]
    pub tab_renderer: Option<TabRenderer>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TabRenderer {
    #[serde(default)]
    pub content: Option<TabContent>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TabContent {
    #[serde(default)]
    pub section_list_renderer: Option<SectionList>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SectionList {
    #[serde(default)]
    pub contents: Vec<SectionContent>,
    #[serde(default)]
    pub continuation: Option<Continuation>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SectionContent {
    #[serde(default)]
    pub item_section_renderer: Option<ItemSection>,
    #[serde(default)]
    pub playlist_video_list_renderer: Option<PlaylistVideoList>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct ItemSection {
    #[serde(default)]
    pub contents: Vec<SectionContent>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlaylistVideoList {
    #[serde(default)]
    pub contents: Vec<PlaylistEntry>,
    #[serde(default)]
    pub continuation: Option<Continuation>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlaylistEntry {
    #[serde(default)]
    pub playlist_video_renderer: Option<PlaylistVideo>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PlaylistVideo {
    #[serde(default)]
    pub video_id: String,
    #[serde(default)]
    pub title: Option<VideoTitle>,
    #[serde(default)]
    pub index: Option<SimpleText>,
    #[serde(default)]
    pub length_seconds: Option<String>,
    #[serde(default)]
    pub short_byline_text: Option<Byline>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct VideoTitle {
    #[serde(default)]
    pub runs: Vec<TextRun>,
    #[serde(default)]
    pub simple_text: Option<String>,
}

impl VideoTitle {
    pub fn text(&self) -> String {
        if !self.runs.is_empty() {
            return self.runs.iter().map(|r| r.text.as_str()).collect();
        }
        self.simple_text.clone().unwrap_or_default()
    }
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct SimpleText {
    #[serde(default)]
    pub simple_text: Option<String>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Byline {
    #[serde(default)]
    pub runs: Vec<TextRun>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Continuation {
    #[serde(default)]
    pub continuation: Option<String>,
    #[serde(default)]
    pub next_continuation_data: Option<NextContinuationData>,
}

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct NextContinuationData {
    #[serde(default)]
    pub continuation: Option<String>,
}

#[derive(Debug, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TwoColumn {
    #[serde(default)]
    pub tabs: Vec<Tab>,
}
