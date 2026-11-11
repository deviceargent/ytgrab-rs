//! Error types shared across the crate.

use std::fmt;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("network error: {0}")]
    Http(#[from] reqwest::Error),

    #[error("malformed response: {0}")]
    Json(#[from] serde_json::Error),

    #[error("io error: {0}")]
    Io(#[from] std::io::Error),

    #[error("this video is unavailable: {0}")]
    Unavailable(String),

    #[error("unsupported url: {0}")]
    UnsupportedUrl(String),

    #[error("no matching format for selector `{0}`")]
    NoFormat(String),

    #[error("ffmpeg failed: {0}")]
    Ffmpeg(String),

    #[error("database error: {0}")]
    Db(#[from] rusqlite::Error),

    #[error("{0}")]
    Other(String),
}

impl Error {
    pub fn other(msg: impl fmt::Display) -> Self {
        Error::Other(msg.to_string())
    }
}
