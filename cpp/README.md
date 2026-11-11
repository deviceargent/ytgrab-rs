# ytgrab — C++ port

A from-scratch C++20 reimplementation of the Rust `ytgrab` CLI in `../src`. The
command line surface, the JSON documents and the progress lines are kept
byte-compatible with the Rust version, because the WinUI GUI
(`../ui/YtGrabGui`) drives this binary and parses its output.

The GUI stays in C#: Reactor is C#-only, so only the CLI is ported.

```
cpp/
├─ CMakeLists.txt
├─ build.ps1                  # configure + build (+ test) with the portable toolchain
├─ src/
│  ├─ main.cpp                # argv -> cli -> runner
│  ├─ cli.{hpp,cpp}           # argument parsing, help/version text
│  ├─ error.{hpp}             # Error + ErrKind, messages mirroring thiserror
│  ├─ util.{hpp,cpp}          # strings, durations, sizes, UTF-8 <-> wide, base64
│  ├─ http.{hpp,cpp}          # WinHTTP client (GET/POST/streaming)
│  ├─ url.{hpp,cpp}           # YouTube URL / id parsing
│  ├─ cookies.{hpp,cpp}       # Netscape cookie files + in-memory jar
│  ├─ browser_cookies.{hpp,cpp} # Chrome/Brave/Chromium/Edge + Firefox cookies
│  ├─ formats.{hpp,cpp}       # selector parser and ranking
│  ├─ innertube/              # InnerTube clients, player + browse endpoints, models
│  ├─ download.{hpp,cpp}      # single + segmented range downloads, progress bar
│  ├─ mux.{hpp,cpp}           # ffmpeg mux / extract audio
│  ├─ subtitles.{hpp,cpp}     # json3 -> SRT / WebVTT
│  ├─ output.{hpp,cpp}        # yt-dlp style output templates
│  └─ runner.{hpp,cpp}        # orchestration: the commands users type
├─ tests/test_main.cpp        # plain assertion tests, no framework
└─ third_party/
   ├─ nlohmann/json.hpp       # 3.11.3, single header
   └─ sqlite/                 # SQLite amalgamation (cookies databases)
```

## Building

Requirements:

* CMake ≥ 3.20 and Ninja (or any CMake generator)
* A C++20 compiler: MSVC 2022, or MinGW-w64 GCC 13+
* Windows 10/11 SDK headers (WinHTTP, BCrypt, DPAPI)
* `ffmpeg` on `PATH` only if you mux or extract audio

`build.ps1` prepends a portable toolchain if one is present
(`%LOCALAPPDATA%\Temp\opencode\toolchain`, with `mingw64`, `cmake-*`, `ninja`),
then configures and builds:

```powershell
cd cpp
.\build.ps1            # configure + build
.\build.ps1 -Test      # ... and run the unit tests
.\build.ps1 -Clean     # wipe ./build first
```

The binary lands in `cpp\build\ytgrab.exe`. A MinGW build links the runtime
statically (`-static -static-libgcc -static-libstdc++`), so the exe can be
copied next to `YtGrabGui.exe` with no extra DLLs.

Manual build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\ytgrab_tests.exe
```

## Using it

```
ytgrab [OPTIONS] <URL>

Arguments:
  <URL>  Video URL, playlist URL, or bare video id

  -f, --format <FORMAT>              best, worst, bestvideo+bestaudio, 1080,
                                     bv[height<=720]+ba            [default: best]
  -o, --output <OUTPUT>              yt-dlp style template [default: %(title)s.%(ext)s]
      --list-formats                 Table of every format and exit
      --formats-json                 Metadata + every format as JSON and exit
      --dump-json                    Metadata + selected formats as JSON and exit
      --get-url                      Print the direct URL(s) and exit
      --write-info-json              Write <name>.info.json next to the download
      --write-thumbnail              Write the thumbnail next to the download
      --write-subs                   Download manual subtitles
      --write-auto-subs              Download automatic captions
      --sub-lang <LANGS>             Comma separated, `all` works   [default: en]
      --sub-format <FORMAT>          srt or vtt                     [default: srt]
  -x, --extract-audio                Extract audio only
      --audio-format <FORMAT>        mp3, m4a, opus, flac, wav      [default: mp3]
      --keep-files                   Keep the separate streams after muxing
      --no-mux                       Never mux video and audio
      --cookies <FILE>               Netscape cookie file
      --cookies-from-browser <NAME>  chrome, brave, chromium, edge, firefox
      --client <CLIENT>              android, ios, mweb, web, tv    [default: android]
      --fragments <N>                Parallel ranges per stream (0 = auto)
      --retries <N>                  Retries per request             [default: 5]
      --playlist / --no-playlist     Playlist handling
      --prefer-opus                  Prefer Opus over AAC
  -v, --verbose / -q, --quiet
```

## Contract kept for the GUI

* `--formats-json` prints one object: `id`, `title`, `uploader`, `channel_id`,
  `description`, `duration`, `view_count`, `webpage_url`, `thumbnail`,
  `extractor`, and `formats[]` with `format_id`, `ext`, `height`, `fps`,
  `vcodec`, `acodec`, `tbr`, `filesize`, `has_video`, `has_audio`, `url`.
* `--dump-json` uses the same metadata keys but `selected_formats[]`.
* `--list-formats` keeps the Rust column layout (`ID EXT RESOLUTION FPS SIZE
  TBR VCODEC ACODEC`).
* Progress repaints with `\r` on stderr as
  `video: - [12.3MiB/45.6MiB] (00:12) 1.23MiB/s`, which the GUI regex matches.
  The bar is erased when the download finishes.
* `Saved <path>` goes to stderr, one line per file.
* Exit codes: 0 success, 1 runtime error, 2 bad arguments.

## Behaviour notes

* **Selection ranking** matches Rust: H.264 > VP9 > AV1 for video, AAC > Opus
  for audio unless `--prefer-opus`, then resolution, fps, bitrate. `best`
  prefers a video-only stream plus a separate audio track and falls back to a
  progressive format when nothing else exists. When `--fragments > 0` or a
  format is retried after a 403, progressive/`ratebypass` formats are preferred.
* **Downloads** are always ranged: googlevideo answers a plain GET with 403.
  Ranges are capped at 15 MiB; a 403 halves the chunk down to 256 KiB before
  giving up. `--fragments 0` picks 8 ranges above 8 MiB, 4 above 2 MiB.
  Partial `.part` and `.part.segN` files resume. A `.lock` file with the pid
  stops two processes writing the same destination; stale locks from dead
  processes are taken over.
* **Muxing** uses `ffmpeg ... -c copy`. The container follows the codecs:
  H.264 + AAC → `mp4`, VP9 + Opus → `webm`, anything else → `mkv`.
* **Cookies from browsers** is the one deliberate divergence from the Rust
  build, which implements the Linux keyring path. On Windows the master key is
  in `Local State` as `os_crypt.encrypted_key` (base64, `DPAPI`-prefixed),
  unwrapped with `CryptUnprotectData`; values are AES-256-GCM (12 byte nonce,
  16 byte tag). When GCM fails, the older AES-256-CBC + fixed-IV scheme is
  tried. The `SHA256(host_key)` value prefix is stripped. `v20` app-bound
  cookies are rejected with a clear message instead of silently skipped.
  Firefox reads `cookies.sqlite` directly. Databases are copied to `%TEMP%`
  before opening so a running browser's lock is never an issue.
* **Errors** print `error: <message>` with the Rust wording (`network error:`,
  `malformed response:`, `this video is unavailable:`, `unsupported url:`,
  `no matching format for selector `...``, `ffmpeg failed:`, ...).

## Tests

`ytgrab_tests.exe` covers URL parsing, output templating and sanitising, the
selector grammar/ranking/filters, subtitle conversion and the cookie jar:

```
$ .\build\ytgrab_tests.exe
all tests passed
```

The network paths (`player`, `browse`, downloads, muxing) are exercised
manually against the Rust binary; both were compared on `--list-formats`,
`--formats-json`, `--dump-json` and a real download of the same video.
