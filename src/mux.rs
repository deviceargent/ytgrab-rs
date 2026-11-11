//! Muxing and metadata handling via ffmpeg.

use std::path::Path;
use std::process::Stdio;

use tokio::process::Command;

use crate::error::{Error, Result};

#[derive(Debug, Clone, Default)]
pub struct Meta {
    pub title: String,
    pub uploader: Option<String>,
    pub artist: Option<String>,
    pub description: Option<String>,
    pub webpage_url: Option<String>,
    pub duration: Option<u64>,
}

pub async fn ffmpeg_available() -> bool {
    Command::new("ffmpeg")
        .arg("-version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .await
        .map(|s| s.success())
        .unwrap_or(false)
}

/// Merge a video-only and an audio-only file into `out` (stream copy).
pub async fn merge(video: &Path, audio: &Path, out: &Path, meta: &Meta) -> Result<()> {
    let mut cmd = Command::new("ffmpeg");
    cmd.arg("-hide_banner")
        .arg("-loglevel")
        .arg("error")
        .arg("-y")
        .arg("-i")
        .arg(video)
        .arg("-i")
        .arg(audio)
        .arg("-map")
        .arg("0:v:0")
        .arg("-map")
        .arg("1:a:0")
        .arg("-c")
        .arg("copy");

    if !meta.title.is_empty() {
        cmd.arg("-metadata").arg(format!("title={}", meta.title));
    }
    if let Some(artist) = &meta.artist {
        cmd.arg("-metadata").arg(format!("artist={artist}"));
    }
    if let Some(desc) = &meta.description {
        cmd.arg("-metadata").arg(format!("description={desc}"));
    }
    if let Some(url) = &meta.webpage_url {
        cmd.arg("-metadata").arg(format!("comment={url}"));
    }

    cmd.arg(out);

    run_ffmpeg(&mut cmd).await
}

/// Re-encode the audio track into a standalone file (e.g. mp3).
pub async fn extract_audio(input: &Path, out: &Path, codec: &str, bitrate: Option<&str>) -> Result<()> {
    let mut cmd = Command::new("ffmpeg");
    cmd.arg("-hide_banner")
        .arg("-loglevel")
        .arg("error")
        .arg("-y")
        .arg("-i")
        .arg(input)
        .arg("-vn")
        .arg("-c:a")
        .arg(codec);
    if let Some(b) = bitrate {
        cmd.arg("-b:a").arg(b);
    }
    cmd.arg(out);
    run_ffmpeg(&mut cmd).await
}

/// Remux a single file into a container (e.g. webm -> mp4) without re-encoding.
pub async fn remux(input: &Path, out: &Path, meta: &Meta) -> Result<()> {
    let mut cmd = Command::new("ffmpeg");
    cmd.arg("-hide_banner")
        .arg("-loglevel")
        .arg("error")
        .arg("-y")
        .arg("-i")
        .arg(input)
        .arg("-c")
        .arg("copy");
    if !meta.title.is_empty() {
        cmd.arg("-metadata").arg(format!("title={}", meta.title));
    }
    cmd.arg(out);
    run_ffmpeg(&mut cmd).await
}

async fn run_ffmpeg(cmd: &mut Command) -> Result<()> {
    let output = cmd.output().await?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(Error::Ffmpeg(stderr.trim().to_string()));
    }
    Ok(())
}
