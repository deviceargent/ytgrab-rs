# PROJECT NOTES

Scratchpad for `yt-dlp-rs`. Written for future sessions — everything here was
learned by poking at the live API, so do not "fix" it back.

## Layout

```
src/
  main.rs         binary entry point (clap + tokio runtime)
  cli.rs          clap definitions
  runner.rs       orchestration: fetch -> select -> download -> mux
  formats.rs      format selector parsing + ranking
  download.rs     resumable + segmented downloader
  mux.rs          ffmpeg wrappers
  subtitles.rs    json3 -> srt/vtt
  output.rs       output templates + filename sanitising
  url.rs          URL -> video id / playlist id
  cookies.rs      Netscape cookie files
  innertube/
    client.rs     client contexts (android/ios/mweb/web/tv) + POST helper
    player.rs     /youtubei/v1/player
    browse.rs     /youtubei/v1/browse (playlists)
    models.rs     serde models
```

## InnerTube facts (verified 2026-11-10)

**Endpoint**

```
POST https://www.youtube.com/youtubei/v1/player?key=<API_KEY>&prettyPrint=false
Content-Type: application/json
User-Agent: <client user agent>
X-Youtube-Client-Name: <numeric id>
X-Youtube-Client-Version: <version>
```

Body:

```json
{"context": {"client": {"clientName": "ANDROID", "clientVersion": "20.10.38",
 "androidSdkVersion": 34, "hl": "en", "gl": "US"}},
 "videoId": "...", "contentCheckOk": true, "racyCheckOk": true}
```

**Working clients**

| client | key | notes |
|---|---|---|
| ANDROID | `AIzaSyA8eiZmM1FaDVjRy-df2KTyQ_vz_yYM39w` | **use this one.** Returns unciphered URLs, no PO token, no bot check. 30 formats incl. 2160p. |
| IOS | `AIzaSyB-63vPrdThhKuerbB2N_l7Kwwcxj6yUAc` | plausible fallback, untested end to end |
| MWEB / WEB | `AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8` | returns `UNPLAYABLE / Video unavailable` without a visitor data + PO token |
| TVHTML5 | `AIzaSyDCU8hByM-4DrUqRUYnGn-3llEO78bcxq8` | `ERROR: YouTube is no longer supported in this application or device` |

**Gotchas that cost real time**

1. The body must nest the client under `context`. A flat `{"client": {...}}`
   returns `400 Precondition check failed`.
2. `playbackContext` (with `html5Preference`) is only accepted by WEB/MWEB.
   Sending it with ANDROID gives a 400.
3. Codecs are **not** a separate field in the player response — they live
   inside the mime type: `video/mp4; codecs="avc1.640028"`. A video-only
   format has one codec, an audio-only format also has exactly one (the
   audio codec!). Use the mime prefix (`video/` vs `audio/`) to tell them
   apart, never the codec count.
4. Numbers arrive as both JSON numbers and strings (`contentLength`,
   `approxDurationMs`, `fps`, `bitrate`…). `models::flex` handles both.
5. `googlevideo.com` rejects the download if the User-Agent differs from the
   one that fetched the player response. The downloader therefore reuses the
   InnerTube client UA.
6. `/browse` with `browseId: "VL<playlistId>"` works on the ANDROID client
   (WEB returns 404 for the same call). The response is ~3 MB for a 19-video
   playlist.
7. The recursive walker in `browse.rs` must collect the **inner** value of
   `playlistVideoRenderer`, not its parent. Collecting the parent silently
   yields an empty playlist.

**Signature cipher / PO token**

Not needed with the ANDROID client. If a future client returns
`signatureCipher` instead of `url`, the `s=` and `n=` parameters have to be
computed by running the player JS — that needs a JS engine (`boa_engine` or
`rquickjs`) and is the main reason to switch clients.

## The throttling discovery (important)

Measured on the same 720p video (26 MB), same machine, same minute:

| | time |
|---|---|
| yt-dlp (single connection) | 8 s |
| yt-dlp-rs, one connection | **213 s** (≈125 kB/s) |
| yt-dlp-rs, 8 parallel byte-ranges | **2 s** |
| curl on yt-dlp's own URL | throttled to 247 kB/s |

YouTube throttles a *single* HTTP connection to roughly 250 kB/s, but serves
parallel `Range` requests at full speed. The client you use barely matters:
both the ANDROID and the TVHTML5 URLs were throttled the same way by curl.

So the default is now **auto-fragmentation**: streams of 8 MiB or more are
fetched as 8 parallel byte-ranges, 2-8 MiB as 4, smaller ones sequentially.
`--fragments N` overrides it; `--fragments 1` forces the slow single
connection.

The parallel path was verified to produce **byte-identical** output to the
sequential path (`sha256` match on both the video and the audio stream).

## Subtitle classification

The ANDROID client does not return the `kind` field for caption tracks, so a
track cannot be reliably labelled "manual" or "automatic" the way yt-dlp
does (yt-dlp gets it from the WEB client, which needs a PO token). The
timedtext `type=list` endpoint is dead.

Workaround: a track with no `kind` is accepted by **either** `--write-subs`
or `--write-auto-subs`, and a manual track wins over an automatic one for
the same language. `caps=asr` in the track URL is used as a hint when
present.

## Bugs found and fixed during the build

- Template renderer emitted the conversion character (`%(title)s` -> `title` + `s`).
- `has_video()`/`has_audio()` misclassified audio-only formats as video.
- Multiple bracket filters (`[ext=mp4][height<=360]`) were not parsed.
- Playlist walker returned parent objects -> empty playlist.

## TODO

- [ ] Browser cookie extraction (Chrome/Brave on Linux: `Local State` ->
      base64 key -> PBKDF2-HMAC-SHA1 (`saltysalt`, 1000 iters) -> AES-128-CBC
      with a 16-byte space IV). The cookie *file* path already works.
- [ ] `--cookies-from-browser`
- [ ] Verify IOS client as a fallback when ANDROID gets throttled.
- [ ] Archive support (`--download-archive`).
- [ ] Rate limiting (`--sleep-requests`).
- [ ] `--write-auto-subs` translation (`tlang`).
- [ ] Integration test that hits the network, marked `#[ignore]`.
- [ ] Compare speed against yt-dlp on a large video.
