//! Cookie handling (Netscape cookie files).

use reqwest::cookie::Jar;
use reqwest::Url;
use std::path::Path;

use crate::error::Result;

/// Load a Netscape-format cookie file into a cookie jar.
pub fn load_jar(path: &Path) -> Result<Jar> {
    let jar = Jar::default();
    let content = std::fs::read_to_string(path)?;
    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        // Comments and the HttpOnly prefix.
        let line = line.strip_prefix("#HttpOnly_").unwrap_or(line);
        if line.starts_with('#') {
            continue;
        }
        let mut fields = line.split('\t');
        let (Some(domain), Some(_flag), Some(path), Some(secure), Some(expires), Some(name)) =
            (fields.next(), fields.next(), fields.next(), fields.next(), fields.next(), fields.next())
        else {
            continue;
        };
        let value = fields.next().unwrap_or("");

        let host = domain.trim_start_matches('.');
        let url = if secure.eq_ignore_ascii_case("true") {
            format!("https://{host}{path}")
        } else {
            format!("http://{host}{path}")
        };
        let Ok(url) = Url::parse(&url) else {
            continue;
        };
        let expires_i = expires.parse::<i64>().unwrap_or(0);
        jar.add_cookie_str(
            &format!(
                "{}={}; Domain={}; Path={}{}",
                name,
                value,
                domain,
                path,
                if expires_i > 0 {
                    format!("; Expires={}", expires_i)
                } else {
                    String::new()
                }
            ),
            &url,
        );
    }
    Ok(jar)
}

/// Apply the jar to an HTTP client builder.
pub fn apply(builder: reqwest::ClientBuilder, jar: Jar) -> reqwest::ClientBuilder {
    builder.cookie_provider(std::sync::Arc::new(jar))
}
