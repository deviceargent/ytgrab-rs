using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using YtGrabGui.Models;

namespace YtGrabGui.Services;

/// <summary>
/// Thin wrapper around the Rust <c>ytgrab</c> binary. The GUI never
/// re-implements InnerTube: it shells out and renders the JSON/progress the
/// CLI already produces.
/// </summary>
internal static partial class YtGrabCli
{
    public const string DefaultTemplate = "%(title)s.%(ext)s";

    // ── Binary discovery ────────────────────────────────────────────

    /// <summary>
    /// Looks for ytgrab in (in order): YTGRAB_PATH, the app folder, any
    /// ancestor's target/{release,debug}, and finally PATH.
    /// </summary>
    public static string? ResolveBinary()
    {
        var env = Environment.GetEnvironmentVariable("YTGRAB_PATH");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env)) return env;

        foreach (var candidate in Candidates())
        {
            if (File.Exists(candidate)) return candidate;
        }

        var pathVar = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in pathVar.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            try
            {
                var candidate = Path.Combine(dir.Trim(), "ytgrab.exe");
                if (File.Exists(candidate)) return candidate;
            }
            catch
            {
                // A malformed PATH entry is not worth failing startup over.
            }
        }

        return null;
    }

    private static IEnumerable<string> Candidates()
    {
        yield return Path.Combine(AppContext.BaseDirectory, "ytgrab.exe");

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (var depth = 0; depth < 10 && dir is not null; depth++, dir = dir.Parent)
        {
            yield return Path.Combine(dir.FullName, "target", "release", "ytgrab.exe");
            yield return Path.Combine(dir.FullName, "target", "debug", "ytgrab.exe");
            yield return Path.Combine(dir.FullName, "ytgrab.exe");
        }
    }

    // ── Metadata ────────────────────────────────────────────────────

    public static async Task<(VideoInfo? Video, string? Error)> FetchAsync(
        string binary, string url, CancellationToken ct)
    {
        // --formats-json is the machine-readable surface added alongside the
        // GUI. Older builds only expose --dump-json, so fall back to it.
        var first = await RunCaptureAsync(binary, ["--formats-json", url], ct);
        if (first.ExitCode == 0 && TryParse(first.StdOut, out var video))
            return (video, null);

        var second = await RunCaptureAsync(binary, ["--dump-json", url], ct);
        if (second.ExitCode == 0 && TryParse(second.StdOut, out video))
            return (video, null);

        var error = FirstNonEmpty(second.StdErr, first.StdErr, first.StdOut, second.StdOut);
        if (string.IsNullOrWhiteSpace(error))
            error = $"ytgrab exited with code {second.ExitCode}.";

        return (null, CleanAnsi(error).Trim());
    }

    private static async Task<CaptureResult> RunCaptureAsync(
        string binary, string[] args, CancellationToken ct)
    {
        var psi = new ProcessStartInfo(binary)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var arg in args) psi.ArgumentList.Add(arg);

        using var process = new Process { StartInfo = psi };
        if (!process.Start())
            return new CaptureResult(-1, "", "Could not start ytgrab.");

        var stdout = new StringBuilder();
        var stderr = new StringBuilder();

        var outTask = PumpAsync(process.StandardOutput, line => stdout.AppendLine(line), ct);
        var errTask = PumpAsync(process.StandardError, line => stderr.AppendLine(line), ct);

        try
        {
            await process.WaitForExitAsync(ct);
        }
        catch (OperationCanceledException)
        {
            TryKill(process);
            throw;
        }

        await Task.WhenAll(outTask, errTask);
        return new CaptureResult(process.ExitCode, stdout.ToString(), stderr.ToString());
    }

    private readonly record struct CaptureResult(int ExitCode, string StdOut, string StdErr);

    // ── JSON parsing ────────────────────────────────────────────────

    public static bool TryParse(string json, out VideoInfo video)
    {
        video = null!;
        if (string.IsNullOrWhiteSpace(json)) return false;

        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return false;

            var id = GetString(root, "id") ?? "";
            var webpage = GetString(root, "webpage_url")
                ?? (id.Length > 0 ? $"https://www.youtube.com/watch?v={id}" : "");
            var thumbnail = GetString(root, "thumbnail");
            if (string.IsNullOrWhiteSpace(thumbnail) && id.Length > 0)
                thumbnail = $"https://i.ytimg.com/vi/{id}/hqdefault.jpg";

            JsonElement formatsElement;
            var hasFormats = root.TryGetProperty("formats", out formatsElement)
                && formatsElement.ValueKind == JsonValueKind.Array;
            if (!hasFormats)
            {
                hasFormats = root.TryGetProperty("selected_formats", out formatsElement)
                    && formatsElement.ValueKind == JsonValueKind.Array;
            }

            var formats = new List<FormatEntry>();
            if (hasFormats)
            {
                foreach (var item in formatsElement.EnumerateArray())
                {
                    if (item.ValueKind != JsonValueKind.Object) continue;

                    var height = (int)GetLong(item, "height");
                    var vcodec = GetString(item, "vcodec") ?? "none";
                    var acodec = GetString(item, "acodec") ?? "none";
                    var hasVideo = GetBool(item, "has_video")
                        ?? (height > 0 || (vcodec != "none" && !string.IsNullOrEmpty(vcodec)));
                    var hasAudio = GetBool(item, "has_audio")
                        ?? (acodec != "none" && !string.IsNullOrEmpty(acodec));

                    formats.Add(new FormatEntry(
                        FormatId: GetString(item, "format_id") ?? "?",
                        Ext: GetString(item, "ext") ?? "?",
                        Height: height,
                        Fps: (int)GetLong(item, "fps"),
                        VCodec: vcodec,
                        ACodec: acodec,
                        Tbr: GetLong(item, "tbr"),
                        Filesize: GetLong(item, "filesize"),
                        HasVideo: hasVideo,
                        HasAudio: hasAudio,
                        Url: GetString(item, "url") ?? ""));
                }
            }

            formats.Sort(static (a, b) =>
            {
                var kind = a.Kind.CompareTo(b.Kind);
                if (kind != 0) return kind;
                var height = b.Height.CompareTo(a.Height);
                return height != 0 ? height : b.Tbr.CompareTo(a.Tbr);
            });

            video = new VideoInfo(
                Id: id,
                Title: GetString(root, "title") ?? "(untitled)",
                Uploader: GetString(root, "uploader") ?? "unknown",
                Description: GetString(root, "description") ?? "",
                DurationSeconds: GetLong(root, "duration"),
                ViewCount: GetLongOrNull(root, "view_count"),
                WebpageUrl: webpage,
                Thumbnail: thumbnail,
                Formats: formats.ToArray());
            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static string? GetString(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static long GetLong(JsonElement element, string name) =>
        GetLongOrNull(element, name) ?? 0;

    private static long? GetLongOrNull(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value)) return null;
        return value.ValueKind switch
        {
            JsonValueKind.Number when value.TryGetInt64(out var n) => n,
            JsonValueKind.String when long.TryParse(value.GetString(), out var n) => n,
            _ => null,
        };
    }

    private static bool? GetBool(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value)) return null;
        return value.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => null,
        };
    }

    // ── Download process ────────────────────────────────────────────

    public static Process StartDownload(
        string binary,
        string url,
        string? formatId,
        bool audioOnly,
        string outputDirectory,
        string template)
    {
        var psi = new ProcessStartInfo(binary)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = outputDirectory,
        };

        if (audioOnly)
        {
            psi.ArgumentList.Add("-x");
            psi.ArgumentList.Add("--audio-format");
            psi.ArgumentList.Add("mp3");
        }

        if (!string.IsNullOrWhiteSpace(formatId))
        {
            psi.ArgumentList.Add("-f");
            psi.ArgumentList.Add(formatId);
        }

        psi.ArgumentList.Add("-o");
        psi.ArgumentList.Add(Path.Combine(outputDirectory, template));
        psi.ArgumentList.Add("--no-playlist");
        psi.ArgumentList.Add(url);

        var process = new Process { StartInfo = psi };
        process.Start();
        return process;
    }

    public static string BuildCommandDisplay(string? formatId, bool audioOnly, string template, string url)
    {
        var parts = new List<string> { "ytgrab" };
        if (audioOnly) parts.Add("-x --audio-format mp3");
        if (!string.IsNullOrWhiteSpace(formatId)) parts.Add($"-f {formatId}");
        parts.Add($"-o \"{template}\"");
        parts.Add("--no-playlist");
        parts.Add($"\"{url}\"");
        return string.Join(' ', parts);
    }

    public static void TryKill(Process process)
    {
        try
        {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
        }
        catch
        {
            // The child may have exited between the check and the kill.
        }
    }

    // ── Output pumping ──────────────────────────────────────────────

    /// <summary>
    /// Reads a redirected stream and raises complete lines. Splits on both CR
    /// and LF because indicatif repaints its progress bar with carriage
    /// returns, so ReadLineAsync would never emit anything until exit.
    /// </summary>
    public static async Task PumpAsync(StreamReader reader, Action<string> onLine, CancellationToken ct)
    {
        var buffer = new char[4096];
        var pending = new StringBuilder();

        try
        {
            while (true)
            {
                var read = await reader.ReadAsync(buffer.AsMemory(), ct);
                if (read == 0) break;

                for (var i = 0; i < read; i++)
                {
                    var c = buffer[i];
                    if (c is '\r' or '\n')
                    {
                        if (pending.Length > 0)
                        {
                            onLine(CleanAnsi(pending.ToString()).Trim());
                            pending.Clear();
                        }
                    }
                    else
                    {
                        pending.Append(c);
                    }
                }
            }
        }
        catch (OperationCanceledException)
        {
            return;
        }

        if (pending.Length > 0)
            onLine(CleanAnsi(pending.ToString()).Trim());
    }

    // ── Progress parsing ────────────────────────────────────────────

    [GeneratedRegex(@"-\s*\[(?<cur>[\d.]+)\s*(?<cu>[KMGT]?i?B)\s*/\s*(?<tot>[\d.]+)\s*(?<tu>[KMGT]?i?B)\]?")]
    private static partial Regex RatioRegex();

    [GeneratedRegex(@"\((?<eta>[^)]*)\)\s*(?<speed>[\d.]+\s*[KMGT]?i?B/s)?")]
    private static partial Regex SpeedRegex();

    [GeneratedRegex(@"\x1B\[[0-9;?]*[ -/]*[@-~]")]
    private static partial Regex AnsiRegex();

    public static string CleanAnsi(string text) => AnsiRegex().Replace(text, "");

    /// <summary>Parses an indicatif progress line into a 0..1 fraction plus a label.</summary>
    public static bool TryParseProgress(string line, out double fraction, out string label)
    {
        fraction = 0;
        label = "";

        var ratio = RatioRegex().Match(line);
        if (!ratio.Success) return false;

        var current = double.Parse(ratio.Groups["cur"].Value, CultureInfo.InvariantCulture);
        var total = double.Parse(ratio.Groups["tot"].Value, CultureInfo.InvariantCulture);
        if (total <= 0) return false;

        var currentBytes = ToBytes(current, ratio.Groups["cu"].Value);
        var totalBytes = ToBytes(total, ratio.Groups["tu"].Value);

        fraction = Math.Clamp(currentBytes / totalBytes, 0, 1);

        var speed = SpeedRegex().Match(line);
        label = speed.Success && speed.Groups["speed"].Success
            ? $"{ratio.Groups["cur"].Value} {ratio.Groups["cu"].Value} / {ratio.Groups["tot"].Value} {ratio.Groups["tu"].Value} · {speed.Groups["speed"].Value}"
            : $"{ratio.Groups["cur"].Value} {ratio.Groups["cu"].Value} / {ratio.Groups["tot"].Value} {ratio.Groups["tu"].Value}";

        return true;
    }

    private static double ToBytes(double value, string unit) => unit switch
    {
        "B" => value,
        "KiB" => value * 1024,
        "MiB" => value * 1024 * 1024,
        "GiB" => value * 1024 * 1024 * 1024,
        "TiB" => value * 1024d * 1024 * 1024 * 1024,
        "KB" => value * 1000,
        "MB" => value * 1000 * 1000,
        "GB" => value * 1000 * 1000 * 1000,
        _ => value,
    };

    private static string FirstNonEmpty(params string[] values) =>
        values.FirstOrDefault(v => !string.IsNullOrWhiteSpace(v)) ?? "";
}
