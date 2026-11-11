namespace YtGrabGui.Models;

/// <summary>Broad classification used for colour-coding and icons.</summary>
public enum FormatKind
{
    Video,
    Audio,
    Muxed,
}

/// <summary>A single downloadable stream returned by <c>ytgrab --formats-json</c>.</summary>
public sealed record FormatEntry(
    string FormatId,
    string Ext,
    int Height,
    int Fps,
    string VCodec,
    string ACodec,
    long Tbr,
    long Filesize,
    bool HasVideo,
    bool HasAudio,
    string Url)
{
    public FormatKind Kind =>
        HasVideo && HasAudio ? FormatKind.Muxed
        : HasVideo ? FormatKind.Video
        : FormatKind.Audio;

    public string KindLabel => Kind switch
    {
        FormatKind.Muxed => "MUXED",
        FormatKind.Video => "VIDEO",
        _ => "AUDIO",
    };

    public string Resolution => Height > 0
        ? (Fps > 30 ? $"{Height}p{Fps}" : $"{Height}p")
        : (Tbr > 0 ? $"{Tbr} kbps" : "audio");

    public string SizeText => Filesize > 0 ? FormatEntry.HumanBytes(Filesize) : "—";

    public string CodecText
    {
        get
        {
            var v = string.IsNullOrWhiteSpace(VCodec) || VCodec == "none" ? null : VCodec;
            var a = string.IsNullOrWhiteSpace(ACodec) || ACodec == "none" ? null : ACodec;
            return (v, a) switch
            {
                (not null, not null) => $"{v} · {a}",
                (not null, null) => v,
                (null, not null) => a,
                _ => "—",
            };
        }
    }

    /// <summary>Arguments (after the binary) needed to download exactly this stream.</summary>
    public string CommandArgs => $"-f {FormatId}";

    public static string HumanBytes(long bytes)
    {
        string[] units = ["B", "KiB", "MiB", "GiB", "TiB"];
        double value = bytes;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        return unit == 0 ? $"{bytes} B" : $"{value:0.0} {units[unit]}";
    }
}

/// <summary>Video metadata plus every format the InnerTube client returned.</summary>
public sealed record VideoInfo(
    string Id,
    string Title,
    string Uploader,
    string Description,
    long DurationSeconds,
    long? ViewCount,
    string WebpageUrl,
    string Thumbnail,
    FormatEntry[] Formats)
{
    public string DurationText
    {
        get
        {
            if (DurationSeconds <= 0) return "—";
            var t = TimeSpan.FromSeconds(DurationSeconds);
            return t.TotalHours >= 1
                ? $"{(int)t.TotalHours}:{t.Minutes:00}:{t.Seconds:00}"
                : $"{t.Minutes}:{t.Seconds:00}";
        }
    }

    public string ViewsText => ViewCount is > 0
        ? ViewCount.Value >= 1_000_000
            ? $"{ViewCount.Value / 1_000_000.0:0.0}M views"
            : ViewCount.Value >= 1_000
                ? $"{ViewCount.Value / 1_000.0:0.0}K views"
                : $"{ViewCount.Value} views"
        : "—";
}

public enum DownloadState
{
    Queued,
    Downloading,
    Done,
    Failed,
    Cancelled,
}

/// <summary>A row in the download queue.</summary>
public sealed record DownloadItem(
    string Id,
    string Title,
    string FormatId,
    string FormatLabel,
    string Command,
    string OutputPath,
    DownloadState State,
    double Progress,
    string Message);

/// <summary>Actions accepted by the queue reducer.</summary>
public enum QueueActionKind
{
    Add,
    Progress,
    State,
    Remove,
    ClearFinished,
    ClearAll,
}

public sealed record QueueAction(
    QueueActionKind Kind,
    DownloadItem? Item = null,
    string? Id = null,
    double Progress = 0,
    DownloadState State = DownloadState.Queued,
    string? Message = null);

/// <summary>Actions accepted by the activity-log reducer.</summary>
public enum LogActionKind
{
    Add,
    Clear,
}

public sealed record LogAction(LogActionKind Kind, string? Line = null);

/// <summary>Which modal dialog, if any, is open.</summary>
public enum UiDialog
{
    None,
    About,
    Settings,
}
