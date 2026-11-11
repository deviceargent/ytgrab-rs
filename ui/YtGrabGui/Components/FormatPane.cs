using Microsoft.UI.Reactor;
using Microsoft.UI.Reactor.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using YtGrabGui.Models;
using static Microsoft.UI.Reactor.Factories;

namespace YtGrabGui.Components;

internal sealed record FormatPaneProps(
    VideoInfo? Video,
    int SelectedIndex,
    bool HasBinary,
    bool IsFetching,
    Action<int> OnSelect,
    Action<FormatEntry> OnDownload,
    Action<FormatEntry> OnDownloadAudio,
    Action OnFetch,
    Action OnPaste,
    Action OnDownloadBest,
    Action OnLoadDemo,
    Action OnOpenUrl,
    Action<string> OnCopy
);

/// <summary>
/// Left pane: the current video's metadata card followed by the list of
/// available streams, colour-coded by kind (video / audio / muxed).
/// </summary>
internal sealed class FormatPane : Component<FormatPaneProps>
{
    public override Element Render()
    {
        var content = Props.Video is null ? EmptyState() : LoadedState(Props.Video);

        var pane = Grid(
            [GridSize.Star()],
            [GridSize.Star()],
            content.Grid(row: 0, column: 0)
        )
        .Background(Palette.Panel);

        return pane.WithContextFlyout(MenuItems(
            MenuItem("Fetch", Props.OnFetch, "\uE896"),
            MenuItem("Paste URL", Props.OnPaste, "\uE77F"),
            MenuItem("Download best", Props.OnDownloadBest, "\uE896"),
            MenuSeparator(),
            MenuItem("Load demo data", Props.OnLoadDemo, "\uE7C3"),
            MenuItem("Open in browser", Props.OnOpenUrl, "\uE774")));
    }

    private Element EmptyState() =>
        VStack(14,
            Border(
                TextBlock("\uE896")
                    .FontFamily(Palette.IconFont)
                    .FontSize(26)
                    .Foreground(Palette.Blue)
            )
            .Width(64)
            .Height(64)
            .CornerRadius(16)
            .Background(Palette.Tint(Palette.Blue, 0x22))
            .BorderBrush(Palette.Tint(Palette.Blue, 0x55))
            .BorderThickness(1)
            .HAlign(HorizontalAlignment.Center),
            TextBlock("No video loaded")
                .FontSize(15)
                .SemiBold()
                .Foreground(Palette.TextPrimary)
                .HAlign(HorizontalAlignment.Center),
            TextBlock("Paste a YouTube URL above and press Fetch.\nyt-grab speaks the InnerTube API directly.")
                .FontSize(11)
                .Foreground(Palette.TextMuted)
                .TextWrapping(TextWrapping.Wrap)
                .HAlign(HorizontalAlignment.Center)
                .MaxWidth(280),
            HStack(8,
                AccentButton("Load demo", Palette.Purple, Props.OnLoadDemo),
                GhostButton("Paste URL", Props.OnPaste)
            ).HAlign(HorizontalAlignment.Center)
        )
        .Padding(24)
        .VAlign(VerticalAlignment.Center)
        .HAlign(HorizontalAlignment.Stretch);

    private Element LoadedState(VideoInfo video) =>
        Grid(
            [GridSize.Star()],
            [GridSize.Auto, GridSize.Auto, GridSize.Star()],
            VideoCard(video).Grid(row: 0, column: 0),
            FormatHeader(video).Grid(row: 1, column: 0),
            (video.Formats.Length == 0
                ? EmptyFormats()
                : LazyVStack<FormatEntry>(
                    video.Formats,
                    f => f.FormatId + f.Ext,
                    (format, index) => FormatRow(format, index)) with
                  {
                      EstimatedItemSize = 44,
                      Spacing = 0,
                  })
                .Grid(row: 2, column: 0)
        );

    // ── Video card ───────────────────────────────────────────────────

    private Element VideoCard(VideoInfo video)
    {
        var card = Border(
            VStack(10,
                HStack(10,
                    Border(
                        Image(video.Thumbnail)
                            .Width(112)
                            .Height(63)
                            .Set(img => img.Stretch = Microsoft.UI.Xaml.Media.Stretch.UniformToFill)
                    )
                    .Width(112)
                    .Height(63)
                    .CornerRadius(8)
                    .BorderBrush(Palette.Border)
                    .BorderThickness(1)
                    .Background(Palette.SurfaceAlt),
                    VStack(4,
                        TextBlock(video.Title)
                            .FontSize(13)
                            .SemiBold()
                            .Foreground(Palette.TextPrimary)
                            .TextWrapping(TextWrapping.Wrap)
                            .MaxWidth(232)
                            .Set(tb => tb.MaxLines = 2),
                        TextBlock(video.Uploader)
                            .FontSize(10.5)
                            .Foreground(Palette.Purple)
                            .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                            .TextWrapping(TextWrapping.NoWrap)
                    ).VAlign(VerticalAlignment.Center)
                ),
                HStack(6,
                    Chip(video.DurationText, Palette.Amber),
                    Chip(video.ViewsText, Palette.Mint),
                    Chip(video.Id, Palette.TextMuted)
                )
            )
            .Padding(12)
        )
        .Background(Palette.Surface)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1);

        return card.WithContextFlyout(MenuItems(
            MenuItem("Copy video URL", () => Props.OnCopy(video.WebpageUrl), "\uE8C8"),
            MenuItem("Copy title", () => Props.OnCopy(video.Title), "\uE8C8"),
            MenuItem("Open in browser", Props.OnOpenUrl, "\uE774"),
            MenuSeparator(),
            MenuItem("Fetch again", Props.OnFetch, "\uE72C")));
    }

    private static Element FormatHeader(VideoInfo video)
    {
        var videos = video.Formats.Count(f => f.Kind != FormatKind.Audio);
        var audios = video.Formats.Count(f => f.Kind == FormatKind.Audio);

        return Grid(
            [GridSize.Star(), GridSize.Auto],
            [GridSize.Px(30)],
            HStack(8,
                TextBlock("FORMATS")
                    .FontSize(9)
                    .SemiBold()
                    .Foreground(Palette.TextSecondary)
                    .Set(tb => tb.CharacterSpacing = 120)
                    .VAlign(VerticalAlignment.Center),
                LegendDot(Palette.Blue, videos.ToString()),
                LegendDot(Palette.Mint, audios.ToString())
            ).Padding(12, 0, 8, 0).Grid(row: 0, column: 0),
            Chip("RIGHT-CLICK", Palette.TextMuted).Grid(row: 0, column: 1).VAlign(VerticalAlignment.Center).Margin(0, 0, 12, 0)
        )
        .Background(Palette.SurfaceAlt)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1);
    }

    private Element EmptyFormats() =>
        TextBlock("No formats were returned for this video.")
            .FontSize(11)
            .Foreground(Palette.TextMuted)
            .Margin(16)
            .VAlign(VerticalAlignment.Top);

    // ── Format rows ──────────────────────────────────────────────────

    private Element FormatRow(FormatEntry format, int index)
    {
        var accent = AccentFor(format.Kind);
        var selected = index == Props.SelectedIndex;
        var baseBackground = index % 2 == 0 ? Palette.Row : Palette.RowAlt;
        var background = selected ? Palette.RowSelected : baseBackground;

        var row = Grid(
            [GridSize.Px(3), GridSize.Star(), GridSize.Auto, GridSize.Px(34)],
            [GridSize.Px(44)],
            Border(Empty())
                .Background(selected ? accent : "transparent")
                .Grid(row: 0, column: 0),
            HStack(10,
                TextBlock(IconFor(format.Kind))
                    .FontFamily(Palette.IconFont)
                    .FontSize(15)
                    .Foreground(accent)
                    .VAlign(VerticalAlignment.Center),
                VStack(2,
                    HStack(8,
                        TextBlock(format.Resolution)
                            .FontSize(12)
                            .SemiBold()
                            .Foreground(Palette.TextPrimary),
                        KindBadge(format, accent)
                    ),
                    TextBlock($"{format.Ext} · {format.CodecText}")
                        .FontSize(10)
                        .Foreground(Palette.TextMuted)
                        .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                        .TextWrapping(TextWrapping.NoWrap)
                ).VAlign(VerticalAlignment.Center)
            ).Padding(13, 0, 0, 0).Grid(row: 0, column: 1),
            TextBlock(format.SizeText)
                .FontFamily(Palette.MonoFont)
                .FontSize(11)
                .Foreground(accent)
                .VAlign(VerticalAlignment.Center)
                .HAlign(HorizontalAlignment.Right)
                .Margin(8, 0, 10, 0)
                .Grid(row: 0, column: 2),
            Border(
                TextBlock("\uE896")
                    .FontFamily(Palette.IconFont)
                    .FontSize(12)
                    .Foreground(accent)
            )
            .Width(24)
            .Height(24)
            .CornerRadius(5)
            .Background(Palette.Tint(accent, 0x18))
            .HAlign(HorizontalAlignment.Center)
            .VAlign(VerticalAlignment.Center)
            .ToolTip($"Download {format.Resolution}")
            .OnTapped((_, e) =>
            {
                e.Handled = true;
                Props.OnDownload(format);
            })
            .Grid(row: 0, column: 3)
        )
        .Padding(0)
        .Background(background)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1)
        .OnPointerEntered((s, _) => ((Grid)s).Background = BrushHelper.Parse(Palette.RowHover))
        .OnPointerExited((s, _) => ((Grid)s).Background = BrushHelper.Parse(selected ? Palette.RowSelected : baseBackground))
        .OnTapped((_, _) => Props.OnSelect(index))
        .OnDoubleTapped((_, _) => Props.OnDownload(format));

        return row.WithContextFlyout(MenuItems(
            MenuItem($"Download {format.Resolution}", () => Props.OnDownload(format), "\uE896"),
            MenuItem("Download as MP3", () => Props.OnDownloadAudio(format), "\uE8D6"),
            MenuSeparator(),
            MenuItem("Copy format id", () => Props.OnCopy(format.FormatId), "\uE8C8"),
            MenuItem("Copy command", () => Props.OnCopy($"ytgrab {format.CommandArgs} URL"), "\uE8C8"),
            MenuItem("Copy size", () => Props.OnCopy(format.SizeText), "\uE8C8")));
    }

    // ── Small visual helpers ─────────────────────────────────────────

    private static string AccentFor(FormatKind kind) => kind switch
    {
        FormatKind.Video => Palette.Blue,
        FormatKind.Audio => Palette.Mint,
        _ => Palette.Amber,
    };

    private static string IconFor(FormatKind kind) => kind switch
    {
        FormatKind.Video => "\uE714",
        FormatKind.Audio => "\uE8D6",
        _ => "\uE786",
    };

    private static Element KindBadge(FormatEntry format, string color) =>
        Border(
            TextBlock(format.KindLabel)
                .FontSize(8)
                .SemiBold()
                .Foreground(color)
                .Set(tb => tb.CharacterSpacing = 100)
        )
        .Background(Palette.Tint(color, 0x22))
        .BorderBrush(Palette.Tint(color, 0x55))
        .BorderThickness(1)
        .CornerRadius(3)
        .Padding(5, 0, 5, 0)
        .VAlign(VerticalAlignment.Center);

    private static Element Chip(string text, string color) =>
        Border(
            TextBlock(text)
                .FontSize(9.5)
                .SemiBold()
                .Foreground(color)
                .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                .TextWrapping(TextWrapping.NoWrap)
        )
        .Background(Palette.Tint(color, 0x20))
        .BorderBrush(Palette.Tint(color, 0x55))
        .BorderThickness(1)
        .CornerRadius(9)
        .Padding(7, 1, 7, 1)
        .HAlign(HorizontalAlignment.Left);

    private static Element LegendDot(string color, string label) =>
        HStack(4,
            Border(Empty())
                .Width(5)
                .Height(5)
                .CornerRadius(3)
                .Background(color)
                .VAlign(VerticalAlignment.Center),
            TextBlock(label)
                .FontSize(9)
                .Foreground(Palette.TextMuted)
                .VAlign(VerticalAlignment.Center)
        ).VAlign(VerticalAlignment.Center);

    private Element AccentButton(string label, string color, Action onClick) =>
        Button(label, onClick)
            .Background(Palette.Tint(color, 0x33))
            .BorderBrush(Palette.Tint(color, 0x88))
            .BorderThickness(1)
            .Foreground(color)
            .CornerRadius(6)
            .Padding(14, 6, 14, 6);

    private Element GhostButton(string label, Action onClick) =>
        Button(label, onClick)
            .Background(Palette.SurfaceAlt)
            .BorderBrush(Palette.Border)
            .BorderThickness(1)
            .Foreground(Palette.TextSecondary)
            .CornerRadius(6)
            .Padding(14, 6, 14, 6);
}
