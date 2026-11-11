# yt-dlp-rs

A YouTube downloader written in Rust. Talks to YouTube's internal
**InnerTube** JSON API directly — no browser, no HTML scraping, no JS
interpreter.

```
yt-dlp-rs "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

## Why it's fast

* No Chromium. The binary is ~10 MB and starts instantly.
* The **ANDROID** InnerTube client returns *unciphered* `googlevideo.com`
  URLs, so there is no signature deciphering and no PO token to obtain.
  (The WEB client requires both, which is why browser-based downloaders
  need a whole JS engine.)
* Parallel byte-range downloads. YouTube throttles a single connection to
  roughly 250 kB/s but serves parallel `Range` requests at full speed, so
  large files are split into 8 ranges automatically.

Measured on the same machine, same video, back to back:

| | yt-dlp | yt-dlp-rs |
|---|---|---|
| 720p (26 MB) | 8.0 s | **2.0 s** |
| 1080p (77 MB) | 12.0 s | **2.9 s** |
| 4K (342 MB) | 25.1 s | **15.9 s** |

The parallel path was verified to produce byte-identical output to the
sequential one (`sha256` match).

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
| `137` | a format id (itag), falling back to "at most 137p" if no such id exists |
| `1080` | best video at or below 1080p |
| `bestvideo[height<=720]` | filters: `height`, `fps`, `tbr`, `ext`, `vcodec`, `acodec`, `filesize` |
| `bv[ext=mp4][height<=1080]+ba` | parts are joined with `+` |

Audio codec follows the video container: H.264 gets AAC (`.mp4`), VP9 gets
Opus (`.webm`). `--prefer-opus` forces Opus everywhere.

## Cookies

```sh
yt-dlp-rs --cookies cookies.txt URL              # Netscape export
yt-dlp-rs --cookies-from-browser brave URL       # chrome, brave, chromium, edge, firefox
```

Browser cookies on Linux live in the OS keyring, so a running D-Bus session
and an unlocked keyring are required (`secret-tool` is used under the hood).

## Current state

Working: format listing and selection, segmented/resumable downloads,
ffmpeg muxing, audio extraction, subtitles (SRT/VTT), thumbnails, info JSON,
playlists, output templates.

Not yet: cookie support (the file is parsed, but browser cookie extraction is
missing), format-specific rate limiting, DASH manifest support, DRM.

See `PROJECT_NOTES.md` for the InnerTube details and the remaining work.
