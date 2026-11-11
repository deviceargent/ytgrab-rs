//! Orchestration: glue InnerTube, format selection, downloading and muxing
//! together into the commands the user actually types.

use std::path::{Path, PathBuf};

use indicatif::MultiProgress;
use serde_json::json;

use crate::cli::Cli;
use crate::download::Downloader;
use crate::error::{Error, Result};
use crate::formats::{self, Selector};
use crate::innertube::browse;
use crate::innertube::client::{ClientContext, InnerTube};
use crate::innertube::models::{CaptionTrack, Format, PlayerResponse, VideoDetails};
use crate::innertube::player;
use crate::mux::{self, Meta};
use crate::output::{self, TemplateVars};
use crate::subtitles;
use crate::url::{self, Target};

/// Context shared across every video in a playlist download.
#[derive(Debug, Clone, Default)]
struct PlaylistContext {
    id: String,
    title: String,
    index: Option<u64>,
}

pub async fn run(cli: Cli) -> Result<()> {
    let target = url::parse(&cli.url)?;

    let ctx = ClientContext::by_name(&cli.client)
        .ok_or_else(|| Error::other(format!("unknown client `{}`", cli.client)))?;

    // Cookies are shared between the API client and the media downloader:
    // age-restricted videos need them on both.
    let jar = match (&cli.cookies, &cli.cookies_from_browser) {
        (Some(path), _) => {
            let jar = crate::cookies::load_jar(path)?;
            Some(std::sync::Arc::new(jar))
        }
        (None, Some(browser)) => {
            let cookies = crate::browser_cookies::extract(browser)?;
            if !cli.quiet {
                eprintln!(
                    "Read {} cookies from {browser}",
                    cookies.len()
                );
            }
            Some(std::sync::Arc::new(crate::browser_cookies::to_jar(&cookies)))
        }
        (None, None) => None,
    };

    let it = InnerTube::with_cookie_jar(ctx, jar.clone())?;

    let mut dl = Downloader::with_cookie_jar(it.user_agent(), jar)?;
    dl.retries = cli.retries;
    dl.fragments = cli.fragments.max(1);

    match target {
        Target::Video { id, playlist } => {
            let want_playlist = cli.playlist && !cli.no_playlist;
            match (want_playlist, playlist) {
                (true, Some(list)) => download_playlist(&it, &dl, &cli, &list).await,
                _ => {
                    download_video(
                        &it,
                        &dl,
                        &cli,
                        &id,
                        &PlaylistContext::default(),
                    )
                    .await
                }
            }
        }
        Target::Playlist { id } => download_playlist(&it, &dl, &cli, &id).await,
    }
}

async fn download_playlist(
    it: &InnerTube,
    dl: &Downloader,
    cli: &Cli,
    playlist_id: &str,
) -> Result<()> {
    if !cli.quiet {
        eprintln!("Fetching playlist {playlist_id} ...");
    }
    let items = browse::playlist(it, playlist_id).await?;
    if items.is_empty() {
        return Err(Error::other("playlist is empty"));
    }
    if !cli.quiet {
        eprintln!("Playlist has {} videos", items.len());
    }

    let ctx = PlaylistContext {
        id: playlist_id.to_string(),
        title: String::new(),
        index: None,
    };

    let mut failures = 0;
    for (i, item) in items.iter().enumerate() {
        let mut item_ctx = ctx.clone();
        item_ctx.index = Some(i as u64 + 1);
        if let Err(e) = download_video(it, dl, cli, &item.video_id, &item_ctx).await {
            failures += 1;
            eprintln!("error: {} failed: {e}", item.video_id);
        }
    }

    if failures > 0 {
        return Err(Error::other(format!(
            "{failures} of {} videos failed",
            items.len()
        )));
    }
    Ok(())
}

async fn download_video(
    it: &InnerTube,
    dl: &Downloader,
    cli: &Cli,
    video_id: &str,
    pctx: &PlaylistContext,
) -> Result<()> {
    let player = player::fetch(it, video_id).await?;
    let streaming = player
        .streaming_data
        .as_ref()
        .ok_or_else(|| Error::Unavailable("no streaming data".into()))?;
    let formats = player::all_formats(streaming);

    if cli.list_formats {
        print_formats(&formats);
        return Ok(());
    }

    let selector = Selector::parse(&cli.format)?;
    let selection = formats::select_with(&selector, &formats, cli.prefer_opus)?;

    if cli.get_url {
        for f in selection.videos.iter().chain(selection.audios.iter()) {
            if let Some(url) = &f.url {
                println!("{url}");
            }
        }
        return Ok(());
    }

    let details = player.video_details.clone().unwrap_or_default();
    if cli.dump_json {
        println!("{}", build_info_json(&player, &selection, video_id));
        return Ok(());
    }

    download_streams(dl, cli, &player, &details, &selection, pctx, video_id).await
}

// ---------------------------------------------------------------------------
// the actual download
// ---------------------------------------------------------------------------

async fn download_streams(
    dl: &Downloader,
    cli: &Cli,
    player: &PlayerResponse,
    details: &VideoDetails,
    selection: &formats::Selection,
    pctx: &PlaylistContext,
    video_id: &str,
) -> Result<()> {
    let video = selection
        .videos
        .first()
        .ok_or_else(|| Error::other("no video stream selected"))?;

    let audio = selection.audios.first();

    let video_ext = video.ext();
    let audio_ext = audio.map(|a| a.ext()).unwrap_or_default();
    let final_ext = if cli.extract_audio {
        cli.audio_format.clone()
    } else if selection.needs_mux() {
        // Keep the container aligned with the codecs for stream copy. Opus in
        // an MP4 works but many players refuse it, so fall back to mkv.
        let audio_ok = audio_ext == "m4a" || audio_ext == "mp4" || audio_ext.is_empty();
        if video_ext == "mp4" && audio_ok {
            "mp4".into()
        } else if video_ext == "webm" && (audio_ext == "webm" || audio_ext.is_empty()) {
            "webm".into()
        } else {
            "mkv".into()
        }
    } else {
        video_ext.clone()
    };

    let vars = TemplateVars {
        title: details.title.clone(),
        id: video_id.to_string(),
        ext: final_ext.clone(),
        uploader: details.author.clone().unwrap_or_default(),
        duration_secs: details.duration_secs(),
        resolution: resolution_of(video),
        format_id: selection
            .videos
            .iter()
            .chain(selection.audios.iter())
            .map(|f| f.itag.to_string())
            .collect::<Vec<_>>()
            .join("+"),
        playlist_index: pctx.index,
        playlist_title: pctx.title.clone(),
        epoch: chrono::Utc::now().timestamp() as u64,
    };

    let out_path = PathBuf::from(output::sanitize_path(&output::render(&cli.output, &vars)));
    if let Some(parent) = out_path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)?;
        }
    }

    let stem = out_path
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| "video".into());
    let parent = out_path
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));

    let mp = MultiProgress::new();

    // ---- video -----------------------------------------------------------
    let video_tmp = parent.join(format!("{stem}.f{}.{video_ext}", video.itag));
    let video_url = video
        .url
        .clone()
        .ok_or_else(|| Error::other("video stream has no url (signature cipher unsupported)"))?;

    let video_bar = mp.add(mux_progress_bar(video.size_bytes(), "video"));
    dl.download(
        &video_url,
        &video_tmp,
        &video_bar,
        video.size_bytes(),
        fragment_count(cli, video.size_bytes()),
    )
    .await?;
    video_bar.finish_and_clear();

    // ---- audio -----------------------------------------------------------
    let mut audio_tmp: Option<PathBuf> = None;
    if let Some(a) = audio {
        let aext = a.ext();
        let tmp = parent.join(format!("{stem}.f{}.{aext}", a.itag));
        let url = a
            .url
            .clone()
            .ok_or_else(|| Error::other("audio stream has no url"))?;
        let bar = mp.add(mux_progress_bar(a.size_bytes(), "audio"));
        dl.download(
            &url,
            &tmp,
            &bar,
            a.size_bytes(),
            fragment_count(cli, a.size_bytes()),
        )
        .await?;
        bar.finish_and_clear();
        audio_tmp = Some(tmp);
    }

    // ---- combine ---------------------------------------------------------
    let meta = Meta {
        title: details.title.clone(),
        uploader: details.author.clone(),
        artist: details.author.clone(),
        description: details.short_description.clone(),
        webpage_url: Some(format!("https://www.youtube.com/watch?v={video_id}")),
        duration: details.duration_secs(),
    };

    if cli.extract_audio {
        let target = parent.join(format!("{stem}.{}", cli.audio_format));
        mux::extract_audio(
            &video_tmp,
            &target,
            &cli.audio_format,
            Some(&format!("{}k", 192)),
        )
        .await?;
        let _ = std::fs::remove_file(&video_tmp);
        if !cli.quiet {
            eprintln!("Saved {}", target.display());
        }
    } else if let Some(audio_path) = &audio_tmp {
        if cli.no_mux {
            // Keep the two streams separate, like yt-dlp's --no-mux.
            if !cli.quiet {
                eprintln!("Saved {}", video_tmp.display());
                eprintln!("Saved {}", audio_path.display());
            }
        } else {
            mux::merge(&video_tmp, audio_path, &out_path, &meta).await?;
            let _ = std::fs::remove_file(&video_tmp);
            if !cli.keep_files {
                let _ = std::fs::remove_file(audio_path);
            }
            if !cli.quiet {
                eprintln!("Saved {}", out_path.display());
            }
        }
    } else {
        std::fs::rename(&video_tmp, &out_path)?;
        if !cli.quiet {
            eprintln!("Saved {}", out_path.display());
        }
    }

    // ---- sidecar files ---------------------------------------------------
    if cli.write_info_json {
        let path = parent.join(format!("{stem}.info.json"));
        std::fs::write(&path, build_info_json(player, selection, video_id))?;
    }

    if cli.write_thumbnail {
        if let Some(thumbs) = &details.thumbnail {
            if let Some(best) = thumbs.best() {
                if let Ok(bytes) = dl.get_bytes(&best.url).await {
                    let ext = if best.url.contains("webp") {
                        "webp"
                    } else {
                        "jpg"
                    };
                    let path = parent.join(format!("{stem}.{ext}"));
                    let _ = std::fs::write(&path, bytes);
                }
            }
        }
    }

    if cli.write_subs || cli.write_auto_subs {
        write_subtitles(dl, cli, player, &parent, &stem).await?;
    }

    Ok(())
}

async fn write_subtitles(
    dl: &Downloader,
    cli: &Cli,
    player: &PlayerResponse,
    parent: &Path,
    stem: &str,
) -> Result<()> {
    let Some(tracklist) = player
        .captions
        .as_ref()
        .and_then(|c| c.player_captions_tracklist_renderer.as_ref())
    else {
        if !cli.quiet {
            eprintln!("no subtitles available");
        }
        return Ok(());
    };

    let wanted: Vec<&str> = cli.sub_lang.split(',').map(|s| s.trim()).collect();

    for track in &tracklist.caption_tracks {
        let lang = track.language_code.clone().unwrap_or_default();
        let is_auto = track.kind.as_deref() == Some("asr");

        if is_auto && !cli.write_auto_subs {
            continue;
        }
        if !is_auto && !cli.write_subs {
            continue;
        }
        if !wanted.iter().any(|w| w == &lang || *w == "all") {
            continue;
        }

        let Some(url) = json3_url(&track.base_url) else {
            continue;
        };
        let body = dl.get_bytes(&url).await?;
        let text = String::from_utf8_lossy(&body);
        let cues = subtitles::parse_json3(&text)?;
        if cues.is_empty() {
            continue;
        }
        let path = parent.join(format!("{stem}.{lang}.{}", cli.sub_format));
        subtitles::write_cues(&cues, &path)?;
        if !cli.quiet {
            eprintln!("Saved {}", path.display());
        }
    }
    Ok(())
}

/// Swap the timedtext format parameter for `json3`.
fn json3_url(base: &str) -> Option<String> {
    let mut url = ::url::Url::parse(base).ok()?;
    // Rebuild the query string, replacing any existing `fmt` with json3.
    let pairs: Vec<(String, String)> = url
        .query_pairs()
        .map(|(k, v)| (k.into_owned(), v.into_owned()))
        .filter(|(k, _)| k != "fmt")
        .collect();
    url.set_query(None);
    {
        let mut q = url.query_pairs_mut();
        for (k, v) in &pairs {
            q.append_pair(k, v);
        }
        q.append_pair("fmt", "json3");
    }
    Some(url.to_string())
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// How many byte-ranges to fetch in parallel for a stream of this size.
///
/// YouTube throttles a *single* connection to roughly 250 kB/s, but serves
/// parallel range requests at full speed. Splitting a large file into eight
/// ranges took 2 s where one connection took 213 s for the same bytes.
fn fragment_count(cli: &Cli, size: Option<u64>) -> usize {
    if cli.fragments > 0 {
        return cli.fragments;
    }
    // Auto: only bother with parallel ranges when there is enough to gain.
    match size {
        Some(s) if s >= 8 * 1024 * 1024 => 8,
        Some(s) if s >= 2 * 1024 * 1024 => 4,
        _ => 1,
    }
}

fn resolution_of(format: &Format) -> String {
    match (format.width, format.height) {
        (Some(w), Some(h)) => format!("{w}x{h}"),
        (_, Some(h)) => format!("{h}p"),
        _ => String::new(),
    }
}

fn mux_progress_bar(total: Option<u64>, label: &str) -> indicatif::ProgressBar {
    let bar = indicatif::ProgressBar::new(total.unwrap_or(0));
    bar.set_style(crate::download::progress_style());
    bar.set_message(label.to_string());
    bar
}

fn print_formats(formats: &[Format]) {
    println!(
        "{:<6} {:<5} {:<12} {:>4}  {:>10}  {:>10}  {:<14} {:<12}",
        "ID", "EXT", "RESOLUTION", "FPS", "SIZE", "TBR", "VCODEC", "ACODEC"
    );
    let mut sorted: Vec<&Format> = formats.iter().collect();
    sorted.sort_by(|a, b| {
        b.height()
            .cmp(&a.height())
            .then_with(|| b.tbr().cmp(&a.tbr()))
            .then_with(|| a.itag.cmp(&b.itag))
    });
    for f in sorted {
        let vcodec = f.vcodec().unwrap_or("-");
        let acodec = if f.has_video() && !f.has_audio() {
            "video only".to_string()
        } else if !f.has_video() {
            "audio only".to_string()
        } else {
            f.acodec().unwrap_or("-").to_string()
        };
        println!(
            "{:<6} {:<5} {:<12} {:>4}  {:>10}  {:>10}  {:<14} {:<12}",
            f.itag,
            f.ext(),
            if f.height() > 0 {
                resolution_of(f)
            } else {
                "audio only".to_string()
            },
            if f.fps() > 0 { f.fps() } else { 0 },
            f.size_str(),
            format!("{}kbps", f.tbr()),
            vcodec,
            acodec
        );
    }
}

fn build_info_json(
    player: &PlayerResponse,
    selection: &formats::Selection,
    video_id: &str,
) -> String {
    let details = player.video_details.clone().unwrap_or_default();
    let formats_json: Vec<serde_json::Value> = selection
        .videos
        .iter()
        .chain(selection.audios.iter())
        .map(|f| {
            json!({
                "format_id": f.itag.to_string(),
                "ext": f.ext(),
                "height": f.height(),
                "fps": f.fps(),
                "vcodec": f.vcodec(),
                "acodec": f.acodec(),
                "tbr": f.tbr(),
                "filesize": f.size_bytes(),
            })
        })
        .collect();

    let doc = json!({
        "id": video_id,
        "title": details.title,
        "uploader": details.author,
        "channel_id": details.channel_id,
        "description": details.short_description,
        "duration": details.duration_secs(),
        "view_count": details.view_count.as_ref().and_then(|v| v.parse::<u64>().ok()),
        "webpage_url": format!("https://www.youtube.com/watch?v={video_id}"),
        "extractor": "innertube:android",
        "selected_formats": formats_json,
    });
    serde_json::to_string_pretty(&doc).unwrap_or_else(|_| "{}".into())
}

/// Helper for the caption track language list (used by --dump-json).
#[allow(dead_code)]
pub fn caption_languages(player: &PlayerResponse) -> Vec<String> {
    player
        .captions
        .as_ref()
        .and_then(|c| c.player_captions_tracklist_renderer.as_ref())
        .map(|t| {
            t.caption_tracks
                .iter()
                .map(|c: &CaptionTrack| c.language_code.clone().unwrap_or_default())
                .collect()
        })
        .unwrap_or_default()
}
