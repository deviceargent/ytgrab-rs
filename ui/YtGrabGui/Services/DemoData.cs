using YtGrabGui.Models;

namespace YtGrabGui.Services;

/// <summary>
/// A small, realistic dataset used when the Rust binary has not been built
/// yet. It lets the window render with content instead of an empty shell, and
/// makes the skin reviewable without a network round-trip.
/// </summary>
internal static class DemoData
{
    public const string SampleUrl = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";

    public static VideoInfo Create() => new(
        Id: "dQw4w9WgXcQ",
        Title: "Reactor + WinUI 3 — declarative native Windows apps",
        Uploader: "Microsoft.UI.Reactor",
        Description: "Demo dataset shown while the ytgrab binary is not available.",
        DurationSeconds: 213,
        ViewCount: 1_482_311,
        WebpageUrl: SampleUrl,
        Thumbnail: "https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg",
        Formats:
        [
            new("137", "mp4", 1080, 30, "avc1.640028", "none", 4300, 76_500_000, true, false, ""),
            new("136", "mp4", 720, 30, "avc1.4d401f", "none", 2100, 42_300_000, true, false, ""),
            new("135", "mp4", 480, 30, "avc1.4d401e", "none", 1100, 22_800_000, true, false, ""),
            new("134", "mp4", 360, 30, "avc1.4d401e", "none", 700, 14_100_000, true, false, ""),
            new("18", "mp4", 360, 30, "avc1.42001E", "mp4a.40.2", 640, 13_400_000, true, true, ""),
            new("140", "m4a", 0, 0, "none", "mp4a.40.2", 129, 3_400_000, false, true, ""),
            new("251", "webm", 0, 0, "none", "opus", 148, 3_900_000, false, true, ""),
            new("250", "webm", 0, 0, "none", "opus", 70, 1_900_000, false, true, ""),
        ]);
}
