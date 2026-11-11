namespace YtGrabGui;

/// <summary>
/// Central palette, lifted from the Regedit Reactor skin: deep navy surfaces,
/// a blue → purple → amber accent ramp, and one hue per download/format kind.
/// </summary>
internal static class Palette
{
    // ── Surfaces ─────────────────────────────────────────────────────
    public const string Background = "#080D19";
    public const string BackgroundAlt = "#0B1220";
    public const string Panel = "#0C1424";
    public const string Surface = "#101A2E";
    public const string SurfaceAlt = "#0E1728";
    public const string Row = "#0C1424";
    public const string RowAlt = "#0F1930";
    public const string RowHover = "#172541";
    public const string RowSelected = "#1B2A49";

    // ── Strokes ──────────────────────────────────────────────────────
    public const string Border = "#223252";
    public const string BorderSoft = "#182440";

    // ── Text ─────────────────────────────────────────────────────────
    public const string TextPrimary = "#E8EEF9";
    public const string TextSecondary = "#9DAFCB";
    public const string TextMuted = "#6B7F9E";

    // ── Accents ──────────────────────────────────────────────────────
    public const string Blue = "#4C8DFF";
    public const string Purple = "#B565F5";
    public const string Pink = "#E879F9";
    public const string Amber = "#F5A623";
    public const string Mint = "#7EE7C7";
    public const string Green = "#6EE7A8";
    public const string Teal = "#4FD1C5";
    public const string Red = "#FF6B81";

    public const string MonoFont = "Cascadia Mono";
    public const string IconFont = "Segoe Fluent Icons";

    /// <summary>Adds an alpha channel to a #RRGGBB color (e.g. for tinted chips).</summary>
    public static string Tint(string hex, byte alpha) =>
        $"#{alpha:X2}{hex.TrimStart('#')}";
}
