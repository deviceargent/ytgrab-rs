//! Parsing YouTube URLs into video / playlist identifiers.

use crate::error::{Error, Result};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Target {
    Video {
        id: String,
        playlist: Option<String>,
    },
    Playlist {
        id: String,
    },
}

/// A bare 11-character YouTube id.
pub fn looks_like_video_id(s: &str) -> bool {
    s.len() == 11
        && s.chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

pub fn parse(input: &str) -> Result<Target> {
    let input = input.trim();

    if looks_like_video_id(input) {
        return Ok(Target::Video {
            id: input.to_string(),
            playlist: None,
        });
    }

    // Accept URLs with or without a scheme.
    let with_scheme = if input.starts_with("http://") || input.starts_with("https://") {
        input.to_string()
    } else {
        format!("https://{input}")
    };

    let url = url::Url::parse(&with_scheme).map_err(|_| Error::UnsupportedUrl(input.to_string()))?;
    let host = url
        .host_str()
        .unwrap_or("")
        .trim_start_matches("www.")
        .trim_start_matches("m.");
    let path = url.path().trim_end_matches('/').to_string();

    let mut video_id: Option<String> = None;
    let mut playlist_id: Option<String> = None;

    for (k, v) in url.query_pairs() {
        match k.as_ref() {
            "v" => video_id = Some(v.to_string()),
            "list" => playlist_id = Some(v.to_string()),
            _ => {}
        }
    }

    match host {
        "youtu.be" => {
            let id = path.trim_start_matches('/').to_string();
            if id.is_empty() {
                return Err(Error::UnsupportedUrl(input.to_string()));
            }
            return Ok(Target::Video {
                id,
                playlist: playlist_id,
            });
        }
        "youtube.com" | "youtube-nocookie.com" | "m.youtube.com" => {
            let segments: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
            match segments.first().copied() {
                Some("watch") => {
                    if let Some(id) = video_id {
                        return Ok(Target::Video {
                            id,
                            playlist: playlist_id,
                        });
                    }
                }
                Some("shorts") | Some("embed") | Some("live") | Some("v") => {
                    if let Some(id) = segments.get(1) {
                        return Ok(Target::Video {
                            id: (*id).to_string(),
                            playlist: playlist_id,
                        });
                    }
                }
                Some("playlist") => {
                    if let Some(list) = playlist_id {
                        return Ok(Target::Playlist { id: list });
                    }
                }
                _ => {}
            }
            if let Some(id) = video_id {
                return Ok(Target::Video {
                    id,
                    playlist: playlist_id,
                });
            }
            if let Some(list) = playlist_id {
                return Ok(Target::Playlist { id: list });
            }
        }
        _ => {}
    }

    Err(Error::UnsupportedUrl(input.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_watch_urls() {
        let t = parse("https://www.youtube.com/watch?v=dQw4w9WgXcQ").unwrap();
        assert_eq!(
            t,
            Target::Video {
                id: "dQw4w9WgXcQ".into(),
                playlist: None
            }
        );
    }

    #[test]
    fn parses_short_urls() {
        let t = parse("https://youtu.be/dQw4w9WgXcQ?t=42").unwrap();
        assert_eq!(
            t,
            Target::Video {
                id: "dQw4w9WgXcQ".into(),
                playlist: None
            }
        );
    }

    #[test]
    fn parses_shorts_and_playlist() {
        assert!(matches!(parse("https://youtube.com/shorts/abc12345678").unwrap(), Target::Video { .. }));
        assert_eq!(
            parse("https://youtube.com/playlist?list=PL1234567890").unwrap(),
            Target::Playlist {
                id: "PL1234567890".into()
            }
        );
    }

    #[test]
    fn parses_bare_id() {
        assert!(matches!(parse("dQw4w9WgXcQ").unwrap(), Target::Video { .. }));
    }

    #[test]
    fn rejects_non_youtube() {
        assert!(parse("https://vimeo.com/12345").is_err());
    }
}
