using Microsoft.UI.Reactor;
using Microsoft.UI.Reactor.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using YtGrabGui.Models;
using static Microsoft.UI.Reactor.Factories;

namespace YtGrabGui.Components;

internal sealed record QueuePaneProps(
    DownloadItem[] Items,
    Action<DownloadItem> OnCancel,
    Action<DownloadItem> OnRemove,
    Action OnClearFinished,
    Action OnClearAll,
    Action<DownloadItem> OnOpenFolder,
    Action<string> OnCopy,
    Action OnOpenOutputFolder
);

/// <summary>Right pane, top half: live download rows with progress and state.</summary>
internal sealed class QueuePane : Component<QueuePaneProps>
{
    public override Element Render()
    {
        var items = Props.Items;
        var active = items.Count(i => i.State is DownloadState.Downloading or DownloadState.Queued);

        var pane = Grid(
            [GridSize.Star()],
            [GridSize.Auto, GridSize.Star()],
            Header(items.Length, active).Grid(row: 0, column: 0),
            (items.Length == 0
                ? EmptyState()
                : LazyVStack<DownloadItem>(
                    items,
                    i => i.Id,
                    (item, _) => Row(item)) with
                  {
                      EstimatedItemSize = 66,
                      Spacing = 0,
                  })
                .Grid(row: 1, column: 0)
        )
        .Background(Palette.BackgroundAlt);

        return pane.WithContextFlyout(MenuItems(
            MenuItem("Open download folder", Props.OnOpenOutputFolder, "\uE838"),
            MenuItem("Clear finished", Props.OnClearFinished, "\uE74D"),
            MenuItem("Clear all", Props.OnClearAll, "\uE894")));
    }

    private static Element Header(int total, int active) =>
        Grid(
            [GridSize.Star(), GridSize.Auto],
            [GridSize.Px(34)],
            HStack(8,
                Dot(Palette.Blue),
                TextBlock("DOWNLOAD QUEUE")
                    .FontSize(9)
                    .SemiBold()
                    .Foreground(Palette.TextSecondary)
                    .Set(tb => tb.CharacterSpacing = 120)
                    .VAlign(VerticalAlignment.Center),
                Chip($"{active} active", active > 0 ? Palette.Blue : Palette.TextMuted),
                Chip($"{total} total", Palette.TextMuted)
            ).Padding(12, 0, 8, 0).Grid(row: 0, column: 0),
            TextBlock("Double-click a format to download")
                .FontSize(9.5)
                .Foreground(Palette.TextMuted)
                .VAlign(VerticalAlignment.Center)
                .Margin(0, 0, 12, 0)
                .Grid(row: 0, column: 1)
        )
        .Background(Palette.SurfaceAlt)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1);

    private Element EmptyState() =>
        VStack(10,
            TextBlock("\uE896")
                .FontFamily(Palette.IconFont)
                .FontSize(26)
                .Foreground(Palette.TextMuted)
                .HAlign(HorizontalAlignment.Center),
            TextBlock("Nothing in the queue")
                .FontSize(12)
                .SemiBold()
                .Foreground(Palette.TextSecondary)
                .HAlign(HorizontalAlignment.Center),
            TextBlock("Pick a format on the left, or use Download → Best quality.")
                .FontSize(10.5)
                .Foreground(Palette.TextMuted)
                .TextWrapping(TextWrapping.Wrap)
                .HAlign(HorizontalAlignment.Center)
                .MaxWidth(320)
        )
        .Padding(24)
        .VAlign(VerticalAlignment.Center);

    private Element Row(DownloadItem item)
    {
        var accent = AccentFor(item.State);
        var showProgress = item.State == DownloadState.Downloading;

        var row = Grid(
            [GridSize.Px(3), GridSize.Star(), GridSize.Auto],
            [GridSize.Auto],
            Border(Empty())
                .Background(accent)
                .Grid(row: 0, column: 0),
            VStack(6,
                HStack(8,
                    TextBlock(IconFor(item.State))
                        .FontFamily(Palette.IconFont)
                        .FontSize(13)
                        .Foreground(accent)
                        .VAlign(VerticalAlignment.Center),
                    TextBlock(item.Title)
                        .FontSize(11.5)
                        .SemiBold()
                        .Foreground(Palette.TextPrimary)
                        .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                        .TextWrapping(TextWrapping.NoWrap)
                        .VAlign(VerticalAlignment.Center),
                    StateBadge(item, accent)
                ),
                TextBlock(item.FormatLabel)
                    .FontSize(10)
                    .Foreground(Palette.TextMuted)
                    .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                    .TextWrapping(TextWrapping.NoWrap),
                When(showProgress,
                    () => Progress(item.Progress * 100)
                        .Set(pb =>
                        {
                            pb.Maximum = 100;
                            pb.Minimum = 0;
                        })
                        .Foreground(accent)
                        .Height(3)
                        .HAlign(HorizontalAlignment.Stretch)),
                TextBlock(item.Message)
                    .FontSize(9.5)
                    .FontFamily(Palette.MonoFont)
                    .Foreground(Palette.TextMuted)
                    .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                    .TextWrapping(TextWrapping.NoWrap)
            ).Padding(13, 8, 10, 8).Grid(row: 0, column: 1),
            VStack(4,
                TextBlock(item.State is DownloadState.Done ? "\uE73E" : "\uE711")
                    .FontFamily(Palette.IconFont)
                    .FontSize(12)
                    .Foreground(accent)
                    .HAlign(HorizontalAlignment.Center),
                TextBlock(item.State is DownloadState.Done ? "Done" : "Cancel")
                    .FontSize(8.5)
                    .Foreground(Palette.TextMuted)
                    .HAlign(HorizontalAlignment.Center)
            )
            .Padding(8, 4, 12, 4)
            .VAlign(VerticalAlignment.Center)
            .OnTapped((_, _) =>
            {
                if (item.State is DownloadState.Done) Props.OnOpenFolder(item);
                else Props.OnCancel(item);
            })
            .Grid(row: 0, column: 2)
        )
        .Background(Palette.Row)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1);

        return row.WithContextFlyout(MenuItems(
            MenuItem("Open download folder", () => Props.OnOpenFolder(item), "\uE838"),
            MenuItem("Copy command", () => Props.OnCopy(item.Command), "\uE8C8"),
            MenuSeparator(),
            MenuItem("Cancel download", () => Props.OnCancel(item), "\uE711"),
            MenuItem("Remove from queue", () => Props.OnRemove(item), "\uE74D")));
    }

    private static string AccentFor(DownloadState state) => state switch
    {
        DownloadState.Downloading => Palette.Blue,
        DownloadState.Queued => Palette.Purple,
        DownloadState.Done => Palette.Mint,
        DownloadState.Failed => Palette.Red,
        _ => Palette.Amber,
    };

    private static string IconFor(DownloadState state) => state switch
    {
        DownloadState.Downloading => "\uE896",
        DownloadState.Queued => "\uE823",
        DownloadState.Done => "\uE73E",
        DownloadState.Failed => "\uEA39",
        _ => "\uE711",
    };

    private static string LabelFor(DownloadState state) => state switch
    {
        DownloadState.Downloading => "DOWNLOADING",
        DownloadState.Queued => "QUEUED",
        DownloadState.Done => "DONE",
        DownloadState.Failed => "FAILED",
        _ => "CANCELLED",
    };

    private static Element StateBadge(DownloadItem item, string color) =>
        Border(
            TextBlock(LabelFor(item.State))
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

    private static Element Dot(string color) =>
        Border(Empty())
            .Width(6)
            .Height(6)
            .CornerRadius(3)
            .Background(color)
            .VAlign(VerticalAlignment.Center);

    private static Element Chip(string text, string color) =>
        Border(
            TextBlock(text)
                .FontSize(9)
                .SemiBold()
                .Foreground(color)
        )
        .Background(Palette.Tint(color, 0x20))
        .BorderBrush(Palette.Tint(color, 0x55))
        .BorderThickness(1)
        .CornerRadius(9)
        .Padding(6, 1, 6, 1)
        .VAlign(VerticalAlignment.Center);
}
