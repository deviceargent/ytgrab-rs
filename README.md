# yt-dlp-rs

A YouTube downloader written in Rust. Talks to YouTube's internal
**InnerTube** JSON API directly — no browser, no HTML scraping, no JS
interpreter.

```
yt-dlp-rs "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

## Why it's fast

* No Chromium. The binary is ~8 MB and starts instantly.
* The **ANDROID** InnerTube client returns *unciphered* `googlevideo.com`
  URLs, so there is no signature deciphering and no PO token to obtain.
  (The WEB client requires both, which is why browser-based downloaders
  need a whole JS engine.)
* Segmented parallel downloads (`--fragments N`) saturate a fast link
  instead of crawling on one throttled connection.

## Build

```sh
cargo build --release
./target/release/yt-dlp-rs --help
```

Requires `ffmpeg` on `PATH` for muxing and audio extraction.

## Usage

```sh
# best available (video + audio, muxed to mp4)
yt-dlp-rs "https://youtu.be/dQw4w9WgXcQ"

# pick a resolution
yt-dlp-rs -f "bestvideo[height<=1080]+bestaudio" URL

# audio only
yt-dlp-rs -x --audio-format mp3 URL

# inspect what is available
yt-dlp-rs --list-formats URL
yt-dlp-rs --get-url URL
yt-dlp-rs --dump-json URL

# sidecar files
yt-dlp-rs --write-subs --write-auto-subs --write-thumbnail --write-info-json URL

# output template (yt-dlp syntax)
yt-dlp-rs -o "%(playlist_index)s - %(title)s.%(ext)s" URL

# whole playlist
yt-dlp-rs --playlist "https://www.youtube.com/playlist?list=PL..."

# 8 parallel segments per stream
yt-dlp-rs --fragments 8 URL
```

### Format selectors

| Selector | Meaning |
|---|---|
| `best` | best video + best audio (default) |
| `worst` | lowest quality |
| `bestvideo` / `bv` | best video-only stream |
| `bestaudio` / `ba` | best audio-only stream |
| `1080` | best video at or below 1080p |
| `bestvideo[height<=720]` | filters: `height`, `fps`, `tbr`, `ext`, `vcodec`, `acodec`, `filesize` |
| `bv[ext=mp4]+ba` | parts are joined with `+` |

## Current state

Working: format listing and selection, segmented/resumable downloads,
ffmpeg muxing, audio extraction, subtitles (SRT/VTT), thumbnails, info JSON,
playlists, output templates.

Not yet: cookie support (the file is parsed, but browser cookie extraction is
missing), format-specific rate limiting, DASH manifest support, DRM.

See `PROJECT_NOTES.md` for the InnerTube details and the remaining work.
