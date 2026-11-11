//! Command line interface definition.

use std::path::PathBuf;

use clap::Parser;

#[derive(Parser, Debug)]
#[command(
    name = "ytgrab",
    version,
    about = "A fast YouTube downloader written in Rust",
    long_about = None
)]
pub struct Cli {
    /// Video URL, playlist URL, or bare video id
    pub url: String,

    /// Format selector: best, worst, bestvideo+bestaudio, 1080, bv[height<=720]+ba
    #[arg(short = 'f', long, default_value = "best")]
    pub format: String,

    /// Output template, yt-dlp style
    #[arg(short = 'o', long, default_value = "%(title)s.%(ext)s")]
    pub output: String,

    /// List available formats and exit
    #[arg(long)]
    pub list_formats: bool,

    /// Print the direct URL(s) instead of downloading
    #[arg(long)]
    pub get_url: bool,

    /// Print the full metadata as JSON and exit
    #[arg(long)]
    pub dump_json: bool,

    /// Print metadata plus every available format as JSON and exit
    #[arg(long)]
    pub formats_json: bool,

    /// Write the metadata alongside the download
    #[arg(long)]
    pub write_info_json: bool,

    /// Write the thumbnail alongside the download
    #[arg(long)]
    pub write_thumbnail: bool,

    /// Download manually written subtitles
    #[arg(long)]
    pub write_subs: bool,

    /// Download automatically generated subtitles
    #[arg(long)]
    pub write_auto_subs: bool,

    /// Subtitle languages (comma separated)
    #[arg(long, default_value = "en")]
    pub sub_lang: String,

    /// Subtitle format: srt or vtt
    #[arg(long, default_value = "srt")]
    pub sub_format: String,

    /// Extract audio only
    #[arg(short = 'x', long)]
    pub extract_audio: bool,

    /// Audio format when extracting audio
    #[arg(long, default_value = "mp3")]
    pub audio_format: String,

    /// Keep the separate video/audio files after muxing
    #[arg(long)]
    pub keep_files: bool,

    /// Do not mux video and audio (keep separate streams)
    #[arg(long)]
    pub no_mux: bool,

    /// Cookies file in Netscape format
    #[arg(long)]
    pub cookies: Option<PathBuf>,

    /// Read cookies from a browser: chrome, brave, chromium, edge, firefox
    #[arg(long, value_name = "BROWSER")]
    pub cookies_from_browser: Option<String>,

    /// InnerTube client to use: android, ios, mweb, web, tv
    #[arg(long, default_value = "android")]
    pub client: String,

    /// Parallel byte-range segments per stream (0 = auto, 1 = sequential)
    #[arg(long, default_value_t = 0)]
    pub fragments: usize,

    /// Retry attempts per request
    #[arg(long, default_value_t = 5)]
    pub retries: u32,

    /// Download every video in a playlist
    #[arg(long)]
    pub playlist: bool,

    /// Never touch playlists (default when the url carries a list= param)
    #[arg(long)]
    pub no_playlist: bool,

    /// Prefer Opus audio over AAC (better quality, fewer players support it)
    #[arg(long)]
    pub prefer_opus: bool,

    /// Verbose logging
    #[arg(short = 'v', long)]
    pub verbose: bool,

    /// Only print errors
    #[arg(short = 'q', long)]
    pub quiet: bool,
}
