# ytgrab GUI

WinUI 3 front-end for the `ytgrab` CLI, built with
[Microsoft.UI.Reactor](https://github.com/microsoft/microsoft-ui-reactor).

The visual language is lifted from the Reactor Regedit sample: deep navy
surfaces, a blue → purple → amber accent ramp, and one hue per object kind
(here: video / audio / muxed formats and download states).

## Why a GUI at all

The CLI is the product; this shell adds no download logic. Every network call
is delegated to `ytgrab`:

| GUI feature | CLI surface |
|---|---|
| Video metadata + full format list | `ytgrab --formats-json URL` |
| Metadata fallback for older builds | `ytgrab --dump-json URL` |
| Downloads | `ytgrab -f <id> -o <template> --no-playlist URL` |
| Audio extraction | `ytgrab -x --audio-format mp3 …` |
| Queue progress | the indicatif lines on stderr |

`--formats-json` was added alongside this app. `--dump-json` only describes the
streams the selector picked, which is not enough for a picker.

## Requirements

- .NET 10 SDK + Windows App SDK (the project targets
  `net10.0-windows10.0.22621.0`).
- A Reactor checkout. Reactor is not published to NuGet; the project references
  its `.csproj` from a sibling folder by default.

## Build

```sh
# Reactor is expected at ../../reactor next to the repo root.
dotnet build ui/YtGrabGui -p:Platform=x64

# …or point at any checkout.
dotnet build ui/YtGrabGui -p:Platform=x64 `
  -p:ReactorProject=C:\src\reactor\src\Reactor\Reactor.csproj
```

The project fails with a readable error when `ReactorProject` does not exist.

## Run

```sh
cargo build --release
ui/YtGrabGui/bin/x64/Debug/net10.0-windows10.0.22621.0/YtGrabGui.exe
```

Binary discovery order:

1. `YTGRAB_PATH`
2. next to `YtGrabGui.exe`
3. `target/{release,debug}/ytgrab.exe` in any ancestor folder
4. the folder containing `ytgrab.exe`
5. `PATH`

`Help → Settings…` overrides the path and the download folder at runtime.

## Demo mode

When no binary is found the app loads a bundled dataset (one video, eight
formats, thumbnail from ytimg). Downloading from it animates the queue with a
simulated byte counter, so the skin is reviewable without Rust installed. The
amber banner at the top makes the state explicit.

## Layout

```
┌ header: dots, wordmark, REACTOR/RUST chips ──────────────┐
│ gradient accent strip                                    │
│ menu bar: File · Download · View · Help                  │
│ url bar: 📺 [url] [Fetch] [Paste] [ ] Audio only  ◌      │
│ setup banner (only when ytgrab.exe is missing)           │
├ format pane (400) ─────────┬ download queue ─────────────┤
│ video card                 │ rows: state, progress       │
│ format rows + legends      ├ activity log ───────────────┤
│                            │ stderr · indicatif          │
├ status bar: status · binary path · queue/log counters ───┤
```

## Context menus

Right-clicking targets a specific object and only offers relevant actions:

- **Format rows** — download, download as MP3, copy id / command / size.
- **Video card** — copy URL / title, open in browser, refetch.
- **Queue rows** — open folder, copy command, cancel, remove.
- **Queue pane background** — open download folder, clear finished / all.
- **Format pane background** — fetch, paste, download best, load demo, open URL.
- **Log pane** — copy log, clear log.

## Files

```
App.cs                    component tree, state, reducers, chrome
Models/UiModels.cs        FormatEntry/VideoInfo/DownloadItem, actions
Services/YtGrabCli.cs     process spawn, JSON parsing, progress parsing
Services/DemoData.cs      bundled dataset for demo mode
Components/FormatPane.cs  metadata card + colour-coded format list
Components/QueuePane.cs   download queue rows
Components/LogPane.cs     activity stream
Theme/Palette.cs          surfaces, strokes, accents
```
