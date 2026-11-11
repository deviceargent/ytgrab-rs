//! Subtitle handling.
//!
//! YouTube serves captions in the `json3` format (a JSON variant of the
//! timedtext XML). We fetch that and emit SubRip or WebVTT.

use std::path::Path;

use serde::Deserialize;

use crate::error::Result;

#[derive(Debug, Deserialize)]
struct Json3 {
    #[serde(default)]
    events: Vec<Event>,
}

#[derive(Debug, Deserialize)]
struct Event {
    #[serde(default)]
    #[serde(rename = "tStartMs")]
    start_ms: i64,
    #[serde(default)]
    #[serde(rename = "dDurationMs")]
    duration_ms: i64,
    #[serde(default)]
    segs: Vec<Seg>,
}

#[derive(Debug, Deserialize)]
struct Seg {
    #[serde(default)]
    utf8: Option<String>,
}

/// One caption cue.
#[derive(Debug, Clone)]
pub struct Cue {
    pub start_ms: i64,
    pub duration_ms: i64,
    pub text: String,
}

/// Parse the `json3` caption payload into cues.
pub fn parse_json3(body: &str) -> Result<Vec<Cue>> {
    let parsed: Json3 = serde_json::from_str(body)?;
    let mut cues = Vec::new();

    for ev in parsed.events {
        let text = ev
            .segs
            .iter()
            .filter_map(|s| s.utf8.as_deref())
            .collect::<String>()
            .replace('\n', " ")
            .trim()
            .to_string();
        if text.is_empty() {
            continue;
        }
        cues.push(Cue {
            start_ms: ev.start_ms,
            duration_ms: if ev.duration_ms > 0 {
                ev.duration_ms
            } else {
                2000
            },
            text,
        });
    }
    Ok(cues)
}

fn timestamp(ms: i64, comma: bool) -> String {
    let (sep, ms_part) = if comma {
        (',', ms % 1000)
    } else {
        ('.', ms % 1000)
    };
    let total_secs = ms.div_euclid(1000);
    format!(
        "{:02}:{:02}:{:02}{sep}{:03}",
        total_secs / 3600,
        (total_secs / 60) % 60,
        total_secs % 60,
        ms_part
    )
}

/// Render cues as SubRip (.srt).
pub fn to_srt(cues: &[Cue]) -> String {
    let mut out = String::new();
    for (i, cue) in cues.iter().enumerate() {
        let end = cue.start_ms + cue.duration_ms;
        out.push_str(&format!(
            "{}\n{} --> {}\n{}\n\n",
            i + 1,
            timestamp(cue.start_ms, true),
            timestamp(end, true),
            cue.text
        ));
    }
    out
}

/// Render cues as WebVTT (.vtt).
pub fn to_vtt(cues: &[Cue]) -> String {
    let mut out = String::from("WEBVTT\n\n");
    for cue in cues {
        let end = cue.start_ms + cue.duration_ms;
        out.push_str(&format!(
            "{} --> {}\n{}\n\n",
            timestamp(cue.start_ms, false),
            timestamp(end, false),
            cue.text
        ));
    }
    out
}

/// Write cues to `path` with the format implied by the extension.
pub fn write_cues(cues: &[Cue], path: &Path) -> Result<()> {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("srt")
        .to_ascii_lowercase();
    let body = if ext == "vtt" {
        to_vtt(cues)
    } else {
        to_srt(cues)
    };
    std::fs::write(path, body)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_and_renders() {
        let body = r#"{"events":[
            {"tStartMs":0,"dDurationMs":1500,"segs":[{"utf8":"Hello"},{"utf8":" world"}]},
            {"tStartMs":2000,"dDurationMs":1000,"segs":[{"utf8":"Second line"}]}
        ]}"#;
        let cues = parse_json3(body).unwrap();
        assert_eq!(cues.len(), 2);
        assert_eq!(cues[0].text, "Hello world");

        let srt = to_srt(&cues);
        assert!(srt.starts_with("1\n00:00:00,000 --> 00:00:01,500\n"));
        assert!(srt.contains("Second line"));

        let vtt = to_vtt(&cues);
        assert!(vtt.starts_with("WEBVTT"));
        assert!(vtt.contains("00:00:02.000 --> 00:00:03.000"));
    }

    #[test]
    fn formats_hours() {
        assert_eq!(timestamp(3_723_456, true), "01:02:03,456");
    }
}
