//! Format selection — the part of yt-dlp people actually interact with.
//!
//! Supports the common selector syntax:
//!
//! ```text
//! best                          best video + best audio (default)
//! worst                         lowest quality combined
//! bestvideo / bv                best video-only stream
//! bestaudio / ba                best audio-only stream
//! 1080                          best video at (or below) 1080p
//! bestvideo[height<=720]+bestaudio
//! best[ext=mp4]
//! ```

use std::cmp::Ordering;

use crate::error::{Error, Result};
use crate::innertube::models::Format;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Video,
    Audio,
    Combined,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Op {
    Eq,
    Le,
    Ge,
    Lt,
    Gt,
}

#[derive(Debug, Clone)]
pub struct Filter {
    pub key: String,
    pub op: Op,
    pub value: String,
}

#[derive(Debug, Clone)]
pub struct Part {
    pub kind: Kind,
    pub worst: bool,
    pub filters: Vec<Filter>,
    /// A bare number in the selector: a format id if such a format exists,
    /// otherwise a height cap (this is how yt-dlp reads `-f 137`).
    pub itag: Option<i64>,
    /// The same number read as a height cap.
    pub height_cap: Option<i64>,
}

/// A parsed `-f` expression: parts joined by `+`.
#[derive(Debug, Clone)]
pub struct Selector {
    pub parts: Vec<Part>,
}

impl Default for Selector {
    fn default() -> Self {
        Selector::parse("best").expect("'best' is a valid selector")
    }
}

impl Selector {
    pub fn parse(input: &str) -> Result<Self> {
        let input = input.trim();
        if input.is_empty() {
            return Err(Error::other("empty format selector"));
        }

        let mut parts = Vec::new();
        for raw in input.split('+') {
            let raw = raw.trim();
            if raw.is_empty() {
                continue;
            }
            parts.push(parse_part(raw)?);
        }
        if parts.is_empty() {
            return Err(Error::other(format!("could not parse format selector `{input}`")));
        }
        Ok(Selector { parts })
    }

    /// True when the selector asks for exactly one stream (no mux needed).
    pub fn is_single_stream(&self) -> bool {
        self.parts.len() == 1 && self.parts[0].kind == Kind::Combined
    }
}

/// Split `bv[height<=720][ext=mp4]` into the head and its filter groups.
fn split_head_and_filters(raw: &str) -> Result<(String, Vec<Filter>)> {
    let mut head = String::new();
    let mut filters = Vec::new();
    let mut rest = raw;
    while let Some(open) = rest.find('[') {
        head.push_str(&rest[..open]);
        let after = &rest[open + 1..];
        let close = after
            .find(']')
            .ok_or_else(|| Error::other(format!("unterminated filter in `{raw}`")))?;
        filters.extend(parse_filters(&after[..close])?);
        rest = &after[close + 1..];
    }
    head.push_str(rest);
    Ok((head, filters))
}

fn parse_part(raw: &str) -> Result<Part> {
    let (head, filters) = split_head_and_filters(raw)?;
    let head = head.trim();
    let lower = head.to_ascii_lowercase();

    // Bare height, e.g. "1080" or "1080p".
    if let Some(num) = lower.strip_suffix('p').and_then(|s| s.parse::<i64>().ok()) {
        return Ok(Part {
            kind: Kind::Video,
            worst: false,
            filters: Vec::new(),
            itag: Some(num),
            height_cap: Some(num),
        });
    }
    if let Ok(num) = lower.parse::<i64>() {
        return Ok(Part {
            kind: Kind::Video,
            worst: false,
            filters: Vec::new(),
            itag: Some(num),
            height_cap: Some(num),
        });
    }

    let (kind, worst) = match lower.as_str() {
        "best" => (Kind::Combined, false),
        "worst" => (Kind::Combined, true),
        "bestvideo" | "bv" => (Kind::Video, false),
        "worstvideo" | "wv" => (Kind::Video, true),
        "bestaudio" | "ba" => (Kind::Audio, false),
        "worstaudio" | "wa" => (Kind::Audio, true),
        other => {
            return Err(Error::other(format!(
                "unknown format selector `{other}` (try best, bestvideo+bestaudio, 1080, ...)"
            )))
        }
    };

    Ok(Part {
        kind,
        worst,
        filters,
        itag: None,
        height_cap: None,
    })
}

fn parse_filters(inner: &str) -> Result<Vec<Filter>> {
    let mut out = Vec::new();
    for chunk in inner.split(',') {
        let chunk = chunk.trim();
        if chunk.is_empty() {
            continue;
        }
        let (key, op, value) = if let Some((k, v)) = chunk.split_once("<=") {
            (k, Op::Le, v)
        } else if let Some((k, v)) = chunk.split_once(">=") {
            (k, Op::Ge, v)
        } else if let Some((k, v)) = chunk.split_once('=') {
            (k, Op::Eq, v)
        } else if let Some((k, v)) = chunk.split_once('<') {
            (k, Op::Lt, v)
        } else if let Some((k, v)) = chunk.split_once('>') {
            (k, Op::Gt, v)
        } else {
            return Err(Error::other(format!("bad filter `{chunk}`")));
        };
        out.push(Filter {
            key: key.trim().to_ascii_lowercase(),
            op,
            value: value.trim().to_string(),
        });
    }
    Ok(out)
}

// ---------------------------------------------------------------------------
// matching & ranking
// ---------------------------------------------------------------------------

fn filter_value(format: &Format, key: &str) -> Option<String> {
    match key {
        "height" => Some(format.height().to_string()),
        "fps" => Some(format.fps().to_string()),
        "tbr" | "vbr" => Some(format.tbr().to_string()),
        "filesize" => format.size_bytes().map(|b| b.to_string()),
        "ext" => Some(format.ext().to_ascii_lowercase()),
        "vcodec" => format.vcodec().map(str::to_string),
        "acodec" => format.acodec().map(str::to_string),
        "abr" => {
            // Approximate audio bitrate for audio-only formats.
            if format.is_audio_only() {
                Some(format.tbr().to_string())
            } else {
                None
            }
        }
        _ => None,
    }
}

fn matches(format: &Format, filter: &Filter) -> bool {
    let Some(actual) = filter_value(format, &filter.key) else {
        return false;
    };
    let numeric = actual.parse::<f64>().ok();
    let expected = filter.value.parse::<f64>().ok();

    match (numeric, expected) {
        (Some(a), Some(e)) => match filter.op {
            Op::Eq => (a - e).abs() < f64::EPSILON,
            Op::Le => a <= e,
            Op::Ge => a >= e,
            Op::Lt => a < e,
            Op::Gt => a > e,
        },
        _ => match filter.op {
            Op::Eq => actual.eq_ignore_ascii_case(&filter.value),
            // For strings only equality makes sense; treat <=/>= as equality.
            _ => actual.eq_ignore_ascii_case(&filter.value),
        },
    }
}

/// Codec preference for video: H.264 first (best device compatibility),
/// then VP9, then AV1.
fn video_codec_rank(format: &Format) -> u8 {
    match format.vcodec() {
        Some(c) if c.starts_with("avc1") => 3,
        Some(c) if c.starts_with("vp9") || c.starts_with("vp09") => 2,
        Some(c) if c.starts_with("av01") => 1,
        _ => 0,
    }
}

/// Audio container preference: m4a/AAC first for maximum player support
/// (AAC plays everywhere, Opus-in-MP4 does not).
fn audio_codec_rank(format: &Format, prefer_opus: bool) -> u8 {
    let aac = matches!(format.ext().as_str(), "m4a" | "mp4");
    let opus = matches!(format.ext().as_str(), "webm");
    match (aac, opus) {
        (true, false) => {
            if prefer_opus {
                2
            } else {
                3
            }
        }
        (false, true) => {
            if prefer_opus {
                3
            } else {
                2
            }
        }
        _ => 1,
    }
}

/// Compare two video formats; `Ordering::Greater` means `a` is better.
///
/// `prefer_servable` puts formats whose URL carries `ratebypass=yes` first:
/// googlevideo serves those without the range window, so they are the ones
/// that can be downloaded at all. It is only used when falling back after
/// the preferred format was refused.
fn cmp_video(a: &Format, b: &Format, prefer_servable: bool) -> Ordering {
    let servable = if prefer_servable {
        a.has_rate_bypass().cmp(&b.has_rate_bypass())
    } else {
        Ordering::Equal
    };
    servable
        .then_with(|| a.height().cmp(&b.height()))
        .then_with(|| a.fps().cmp(&b.fps()))
        .then_with(|| video_codec_rank(a).cmp(&video_codec_rank(b)))
        .then_with(|| a.tbr().cmp(&b.tbr()))
        .then_with(|| b.itag.cmp(&a.itag))
}

/// Compare two audio formats; `Ordering::Greater` means `a` is better.
fn cmp_audio(a: &Format, b: &Format, prefer_opus: bool, prefer_servable: bool) -> Ordering {
    let servable = if prefer_servable {
        a.has_rate_bypass().cmp(&b.has_rate_bypass())
    } else {
        Ordering::Equal
    };
    servable
        .then_with(|| audio_codec_rank(a, prefer_opus).cmp(&audio_codec_rank(b, prefer_opus)))
        .then_with(|| a.tbr().cmp(&b.tbr()))
        .then_with(|| b.itag.cmp(&a.itag))
}

/// The result of resolving a selector against a format list.
#[derive(Debug, Clone)]
pub struct Selection {
    /// Video streams (one for a combined format, one for video-only).
    pub videos: Vec<Format>,
    /// Audio streams; empty when the video stream already carries audio.
    pub audios: Vec<Format>,
}

impl Selection {
    pub fn needs_mux(&self) -> bool {
        !self.audios.is_empty()
    }

    pub fn total_size(&self) -> u64 {
        self.videos
            .iter()
            .chain(self.audios.iter())
            .filter_map(|f| f.size_bytes())
            .sum()
    }
}

/// Resolve a selector against the available formats.
pub fn select(selector: &Selector, formats: &[Format]) -> Result<Selection> {
    select_with(selector, formats, false)
}

/// Like [`select`], but can prefer Opus audio over AAC.
pub fn select_with(
    selector: &Selector,
    formats: &[Format],
    prefer_opus: bool,
) -> Result<Selection> {
    select_excluding(selector, formats, prefer_opus, &[])
}

/// Like [`select_with`], but skips the given format ids.
///
/// Used to fall back to a different format when googlevideo refuses to serve
/// the one that was picked.
pub fn select_excluding(
    selector: &Selector,
    formats: &[Format],
    prefer_opus: bool,
    excluded: &[i64],
) -> Result<Selection> {
    select_excluding_with(selector, formats, prefer_opus, excluded, false)
}

/// Like [`select_excluding`], but can prefer formats that googlevideo is
/// known to serve without a range window.
pub fn select_excluding_with(
    selector: &Selector,
    formats: &[Format],
    prefer_opus: bool,
    excluded: &[i64],
    prefer_servable: bool,
) -> Result<Selection> {
    let formats: Vec<Format> = formats
        .iter()
        .filter(|f| !excluded.contains(&f.itag))
        .cloned()
        .collect();
    let formats = &formats[..];
    let mut videos: Vec<Format> = Vec::new();
    let mut audios: Vec<Format> = Vec::new();

    for part in &selector.parts {
        match part.kind {
            Kind::Combined => {
                // "best" means best video plus best audio, muxed together.
                // A progressive (combined) stream is only used when there is
                // no separate video/audio pair available.
                let video = pick_video(formats, part, prefer_servable)?;
                if video.has_audio() {
                    // A progressive stream already carries the audio.
                    videos.push(video);
                    continue;
                }
                // Match the audio codec to the video container: vp9 wants
                // opus so the result can stay a .webm, h.264 wants AAC so it
                // can stay a .mp4.
                let prefer_opus_for_video = prefer_opus || video.ext() == "webm";
                let audio = pick_audio(formats, part, prefer_opus_for_video, prefer_servable);
                match audio {
                    Ok(a) if a.is_audio_only() => {
                        videos.push(video);
                        audios.push(a);
                    }
                    _ => {
                        // No separate audio stream: use a progressive format.
                        let combined = pick_combined(formats, part)?;
                        videos.push(combined);
                    }
                }
            }
            Kind::Video => {
                // An explicit video selector means video only.
                videos.push(pick_video(formats, part, prefer_servable)?);
            }
            Kind::Audio => {
                // Keep the audio codec compatible with the video container
                // that was already chosen (vp9 -> opus -> .webm).
                let want_opus = prefer_opus
                    || videos
                        .first()
                        .map(|v| v.ext() == "webm")
                        .unwrap_or(false);
                audios.push(pick_audio(formats, part, want_opus, prefer_servable)?);
            }
        }
    }

    if videos.is_empty() && audios.is_empty() {
        return Err(Error::NoFormat(
            selector
                .parts
                .iter()
                .map(|p| format!("{p:?}"))
                .collect::<Vec<_>>()
                .join("+"),
        ));
    }

    Ok(Selection { videos, audios })
}

/// The best audio-only stream, ignoring the selector.
pub fn best_audio_only(formats: &[Format], prefer_opus: bool) -> Result<Format> {
    let part = Part {
        kind: Kind::Audio,
        worst: false,
        filters: Vec::new(),
        itag: None,
        height_cap: None,
    };
    pick_audio(formats, &part, prefer_opus, false)
}

/// The best progressive format (video and audio in one file).
fn pick_combined(formats: &[Format], part: &Part) -> Result<Format> {
    let mut candidates: Vec<&Format> = formats
        .iter()
        .filter(|f| f.is_combined())
        .filter(|f| part.filters.iter().all(|fl| matches(f, fl)))
        .collect();
    if candidates.is_empty() {
        return Err(Error::NoFormat("best combined".into()));
    }
    candidates.sort_by(|a, b| cmp_video(a, b, false));
    Ok(if part.worst {
        candidates[0].clone()
    } else {
        candidates[candidates.len() - 1].clone()
    })
}

fn pick_video(formats: &[Format], part: &Part, prefer_servable: bool) -> Result<Format> {
    // An explicit format id wins over everything else.
    if let Some(itag) = part.itag {
        if let Some(f) = formats.iter().find(|f| f.itag == itag) {
            return Ok(f.clone());
        }
        // No such format id: treat the number as a height cap.
        let height_part = Part {
            kind: part.kind,
            worst: part.worst,
            filters: vec![Filter {
                key: "height".into(),
                op: Op::Le,
                value: itag.to_string(),
            }],
            itag: None,
            height_cap: None,
        };
        // Only use the height reading when something actually matches it:
        // `-f 137` means itag 137, and there is no 137p video.
        if formats
            .iter()
            .any(|f| f.has_video() && matches(f, &height_part.filters[0]))
        {
            return pick_video(formats, &height_part, prefer_servable);
        }
    }

    let mut candidates: Vec<&Format> = formats
        .iter()
        .filter(|f| f.has_video())
        .filter(|f| part.filters.iter().all(|fl| matches(f, fl)))
        .collect();

    if prefer_servable {
        // Googlevideo told us which URLs it will serve in full. Use those.
        let servable: Vec<&Format> = candidates
            .iter()
            .copied()
            .filter(|f| f.has_rate_bypass())
            .collect();
        if !servable.is_empty() {
            candidates = servable;
        }
    } else {
        // Prefer video-only streams, but fall back to combined ones.
        let video_only: Vec<&Format> = candidates
            .iter()
            .copied()
            .filter(|f| f.is_video_only())
            .collect();
        if !video_only.is_empty() {
            candidates = video_only;
        }
    }

    if candidates.is_empty() {
        return Err(Error::NoFormat(match part.itag {
            Some(id) => format!("format id {id}"),
            None => "any video stream".to_string(),
        }));
    }
    candidates.sort_by(|a, b| cmp_video(a, b, prefer_servable));
    Ok(if part.worst {
        candidates[0].clone()
    } else {
        candidates[candidates.len() - 1].clone()
    })
}

fn pick_audio(
    formats: &[Format],
    part: &Part,
    prefer_opus: bool,
    prefer_servable: bool,
) -> Result<Format> {
    let mut candidates: Vec<&Format> = formats
        .iter()
        .filter(|f| f.has_audio())
        .filter(|f| part.filters.iter().all(|fl| matches(f, fl)))
        .collect();

    let audio_only: Vec<&Format> = candidates
        .iter()
        .copied()
        .filter(|f| f.is_audio_only())
        .collect();
    if !audio_only.is_empty() {
        candidates = audio_only;
    }

    if candidates.is_empty() {
        return Err(Error::NoFormat("any audio stream".into()));
    }
    candidates.sort_by(|a, b| cmp_audio(a, b, prefer_opus, prefer_servable));
    Ok(if part.worst {
        candidates[0].clone()
    } else {
        candidates[candidates.len() - 1].clone()
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fmt(itag: i64, mime: &str, height: i64, codecs: &str, tbr: i64) -> Format {
        Format {
            itag,
            url: Some("http://example".into()),
            mime_type: Some(mime.into()),
            height: Some(height),
            codecs: Some(codecs.into()),
            bitrate: Some(tbr * 1000),
            ..Default::default()
        }
    }

    fn sample() -> Vec<Format> {
        vec![
            fmt(18, "video/mp4", 360, "avc1.42001E, mp4a.40.2", 700),
            fmt(137, "video/mp4", 1080, "avc1.640028", 4200),
            fmt(248, "video/webm", 1080, "vp9", 2800),
            fmt(313, "video/webm", 2160, "vp09.00.40.08", 12000),
            fmt(140, "audio/mp4", 0, "mp4a.40.2", 128),
            fmt(251, "audio/webm", 0, "opus", 160),
        ]
    }

    #[test]
    fn parses_selectors() {
        assert_eq!(Selector::parse("best").unwrap().parts[0].kind, Kind::Combined);
        assert_eq!(Selector::parse("bv").unwrap().parts[0].kind, Kind::Video);
        assert_eq!(Selector::parse("ba").unwrap().parts[0].kind, Kind::Audio);
        assert_eq!(Selector::parse("1080").unwrap().parts[0].itag, Some(1080));
        assert!(Selector::parse("nonsense").is_err());
    }

    #[test]
    fn picks_highest_video() {
        let sel = Selector::parse("bestvideo").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].itag, 313, "highest resolution wins");
    }

    #[test]
    fn prefers_h264_at_equal_height() {
        let sel = Selector::parse("bestvideo[height=1080]").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].itag, 137, "avc1 beats vp9 at 1080p");
    }

    #[test]
    fn bare_number_is_a_format_id_when_it_exists() {
        let sel = Selector::parse("137").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].itag, 137);
    }

    #[test]
    fn bare_number_falls_back_to_a_height_cap() {
        // No format with itag 720, so it means "at most 720p".
        let sel = Selector::parse("720").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert!(picked.videos[0].height() <= 720);
    }

    #[test]
    fn height_filter_caps_resolution() {
        let sel = Selector::parse("bestvideo[height<=720]").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].height(), 360);
    }

    #[test]
    fn best_pairs_video_with_audio() {
        let sel = Selector::parse("best").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.audios.len(), 1);
        assert!(picked.needs_mux());
    }

    #[test]
    fn best_prefers_separate_streams_over_progressive() {
        let sel = Selector::parse("best").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].itag, 313, "highest resolution");
        assert_eq!(picked.audios.len(), 1);
        assert!(picked.needs_mux());
    }

    #[test]
    fn explicit_video_selector_does_not_add_audio() {
        let sel = Selector::parse("bestvideo").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert!(picked.audios.is_empty());
    }

    #[test]
    fn combined_only_when_no_separate_audio_exists() {
        // Only format 18 carries both streams here.
        let only_combined = vec![fmt(18, "video/mp4", 360, "avc1.42001E, mp4a.40.2", 700)];
        let sel = Selector::parse("best").unwrap();
        let picked = select(&sel, &only_combined).unwrap();
        assert_eq!(picked.videos[0].itag, 18);
        assert!(!picked.needs_mux());
    }

    #[test]
    fn height_and_ext_filters_apply() {
        let sel = Selector::parse("best[ext=mp4][height<=360]").unwrap();
        let picked = select(&sel, &sample()).unwrap();
        assert_eq!(picked.videos[0].height(), 360);
        assert_eq!(picked.videos[0].ext(), "mp4");
    }
}
