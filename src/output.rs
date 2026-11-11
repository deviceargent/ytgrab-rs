//! Output filename templating (yt-dlp style) and path sanitising.

use std::collections::HashMap;

#[derive(Debug, Clone, Default)]
pub struct TemplateVars {
    pub title: String,
    pub id: String,
    pub ext: String,
    pub uploader: String,
    pub duration_secs: Option<u64>,
    pub resolution: String,
    pub format_id: String,
    pub playlist_index: Option<u64>,
    pub playlist_title: String,
    pub playlist_id: String,
    pub epoch: u64,
}

impl TemplateVars {
    pub fn to_map(&self) -> HashMap<String, String> {
        let mut m = HashMap::new();
        m.insert("title".into(), self.title.clone());
        m.insert("id".into(), self.id.clone());
        m.insert("ext".into(), self.ext.clone());
        m.insert("uploader".into(), self.uploader.clone());
        m.insert("artist".into(), self.uploader.clone());
        m.insert(
            "duration".into(),
            self.duration_secs.map(format_duration).unwrap_or_default(),
        );
        m.insert("duration_string".into(), self.duration_secs.map(format_duration).unwrap_or_default());
        m.insert("resolution".into(), self.resolution.clone());
        m.insert("format_id".into(), self.format_id.clone());
        m.insert("height".into(), self.resolution.trim_end_matches('p').to_string());
        m.insert(
            "playlist_index".into(),
            self.playlist_index.map(|i| i.to_string()).unwrap_or_default(),
        );
        m.insert("playlist".into(), self.playlist_title.clone());
        m.insert("playlist_id".into(), self.playlist_id.clone());
        m.insert("epoch".into(), self.epoch.to_string());
        m
    }
}

/// `3661` -> `1:01:01`, `213` -> `3:33`
pub fn format_duration(secs: u64) -> String {
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    if h > 0 {
        format!("{h}:{m:02}:{s:02}")
    } else {
        format!("{m}:{s:02}")
    }
}

/// Render a `%(field)s` template.
pub fn render(template: &str, vars: &TemplateVars) -> String {
    let map = vars.to_map();
    let mut out = String::with_capacity(template.len());
    let bytes: Vec<char> = template.chars().collect();
    let mut i = 0;

    while i < bytes.len() {
        if bytes[i] == '%' && i + 1 < bytes.len() && bytes[i + 1] == '(' {
            // find the closing ')'
            if let Some(close) = bytes[i + 2..].iter().position(|c| *c == ')') {
                let close = i + 2 + close;
                let key: String = bytes[i + 2..close].iter().collect();
                let value = map.get(&key).cloned().unwrap_or_default();
                // optional format spec, e.g. %(height)06d
                let mut j = close + 1;
                let mut spec = String::new();
                if j < bytes.len() && bytes[j] == '0' {
                    spec.push(bytes[j]);
                    j += 1;
                    while j < bytes.len() && bytes[j].is_ascii_digit() {
                        spec.push(bytes[j]);
                        j += 1;
                    }
                }
                // The conversion character (s, d, ...) always follows.
                if j < bytes.len() {
                    let conv = bytes[j];
                    j += 1;
                    if conv == 'd' {
                        let width: usize = spec.trim_start_matches('0').parse().unwrap_or(0);
                        match value.parse::<i64>() {
                            Ok(n) => out.push_str(&format!("{n:0width$}")),
                            Err(_) => out.push_str(&value),
                        }
                    } else {
                        out.push_str(&value);
                    }
                } else {
                    out.push_str(&value);
                }
                i = j;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }

    out
}

/// Make a string safe to use as a filename component.
pub fn sanitize(input: &str) -> String {
    let cleaned: String = input
        .chars()
        .map(|c| match c {
            '/' | '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|' | '\0' => '_',
            c if (c as u32) < 0x20 => '_',
            c => c,
        })
        .collect();
    let trimmed = cleaned.trim().trim_matches('.').to_string();
    if trimmed.is_empty() {
        "video".to_string()
    } else {
        trimmed
    }
}

/// Sanitise a whole path template, keeping directory separators intact.
///
/// An absolute path stays absolute: a leading `/` must not be dropped, or
/// `/tmp/out.mp4` would end up in `./tmp/out.mp4`. Windows separators and
/// drive prefixes are understood too, so `C:\out\%(title)s.mp4` keeps its
/// directories instead of collapsing into one long filename.
pub fn sanitize_path(path: &str) -> String {
    let normalized = path.replace('\\', "/");
    let absolute = normalized.starts_with('/');
    let drive_prefix = {
        let bytes = normalized.as_bytes();
        bytes.len() > 1 && bytes[0].is_ascii_alphabetic() && bytes[1] == b':'
    };
    let parts: Vec<String> = normalized
        .split('/')
        .filter(|p| !p.is_empty())
        .map(sanitize)
        .collect();
    let mut joined = parts.join("/");
    if drive_prefix {
        // `C:` was sanitised to `C_`; put the colon back.
        if joined.len() > 1 && joined.as_bytes()[1] == b'_' {
            joined.replace_range(1..2, ":");
        }
        return joined;
    }
    if absolute {
        format!("/{joined}")
    } else {
        joined
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vars() -> TemplateVars {
        TemplateVars {
            title: "Never Gonna Give You Up".into(),
            id: "dQw4w9WgXcQ".into(),
            ext: "mp4".into(),
            uploader: "Rick Astley".into(),
            duration_secs: Some(213),
            resolution: "1920x1080".into(),
            format_id: "137+140".into(),
            playlist_index: None,
            playlist_title: String::new(),
            playlist_id: String::new(),
            epoch: 0,
        }
    }

    #[test]
    fn renders_template() {
        let out = render("%(title)s.%(ext)s", &vars());
        assert_eq!(out, "Never Gonna Give You Up.mp4");
    }

    #[test]
    fn renders_playlist_index() {
        let mut v = vars();
        v.playlist_index = Some(3);
        assert_eq!(render("%(playlist_index)s - %(title)s", &v), "3 - Never Gonna Give You Up");
    }

    #[test]
    fn strips_path_separators() {
        assert_eq!(sanitize("a/b:c"), "a_b_c");
    }

    #[test]
    fn keeps_absolute_paths_absolute() {
        assert_eq!(sanitize_path("/tmp/out.mp4"), "/tmp/out.mp4");
        assert_eq!(sanitize_path("out.mp4"), "out.mp4");
        assert_eq!(sanitize_path("a/b/out.mp4"), "a/b/out.mp4");
        // ".." is neutralised, it cannot escape the current directory.
        assert_eq!(sanitize_path("../etc/passwd"), "video/etc/passwd");
        // Windows absolute paths keep their directories and drive.
        assert_eq!(
            sanitize_path(r"C:\Users\me\Downloads\%(title)s.%(ext)s"),
            "C:/Users/me/Downloads/%(title)s.%(ext)s"
        );
        assert_eq!(sanitize_path(r"sub\out.mp4"), "sub/out.mp4");
    }

    #[test]
    fn formats_duration() {
        assert_eq!(format_duration(213), "3:33");
        assert_eq!(format_duration(3661), "1:01:01");
    }
}
