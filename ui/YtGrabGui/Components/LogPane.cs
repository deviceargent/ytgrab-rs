using Microsoft.UI.Reactor;
using Microsoft.UI.Reactor.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using static Microsoft.UI.Reactor.Factories;

namespace YtGrabGui.Components;

internal sealed record LogPaneProps(
    string[] Lines,
    Action OnClear,
    Action OnCopy
);

/// <summary>Right pane, bottom half: the raw activity stream from ytgrab.</summary>
internal sealed class LogPane : Component<LogPaneProps>
{
    public override Element Render()
    {
        var lines = Props.Lines;

        var body = lines.Length == 0
            ? (Element)TextBlock("ytgrab output will appear here.")
                .FontFamily(Palette.MonoFont)
                .FontSize(10.5)
                .Foreground(Palette.TextMuted)
                .Margin(14)
            : ScrollViewer(
                VStack(1,
                    lines.Select(LineElement).ToArray())
                .Padding(12, 8, 12, 10)
                .HAlign(HorizontalAlignment.Stretch)
            );

        var pane = Grid(
            [GridSize.Star()],
            [GridSize.Auto, GridSize.Star()],
            Header(lines.Length).Grid(row: 0, column: 0),
            body.Grid(row: 1, column: 0)
        )
        .Background(Palette.Panel);

        return pane.WithContextFlyout(MenuItems(
            MenuItem("Copy log", Props.OnCopy, "\uE8C8"),
            MenuItem("Clear log", Props.OnClear, "\uE74D")));
    }

    private static Element Header(int count) =>
        Grid(
            [GridSize.Star(), GridSize.Auto],
            [GridSize.Px(30)],
            HStack(8,
                Border(Empty())
                    .Width(6)
                    .Height(6)
                    .CornerRadius(3)
                    .Background(Palette.Amber)
                    .VAlign(VerticalAlignment.Center),
                TextBlock("ACTIVITY")
                    .FontSize(9)
                    .SemiBold()
                    .Foreground(Palette.TextSecondary)
                    .Set(tb => tb.CharacterSpacing = 120)
                    .VAlign(VerticalAlignment.Center),
                TextBlock(count == 0 ? "idle" : $"{count} lines")
                    .FontSize(9)
                    .Foreground(Palette.TextMuted)
                    .VAlign(VerticalAlignment.Center)
            ).Padding(12, 0, 8, 0).Grid(row: 0, column: 0),
            TextBlock("stderr · indicatif")
                .FontSize(9)
                .FontFamily(Palette.MonoFont)
                .Foreground(Palette.TextMuted)
                .VAlign(VerticalAlignment.Center)
                .Margin(0, 0, 12, 0)
                .Grid(row: 0, column: 1)
        )
        .Background(Palette.SurfaceAlt)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 1, 0, 1);

    private static Element LineElement(string line)
    {
        var color = line.StartsWith("Saved", StringComparison.OrdinalIgnoreCase)
            ? Palette.Mint
            : line.StartsWith("error", StringComparison.OrdinalIgnoreCase)
                || line.Contains("failed", StringComparison.OrdinalIgnoreCase)
                ? Palette.Red
                : line.StartsWith("Fetching", StringComparison.OrdinalIgnoreCase)
                    ? Palette.Blue
                    : Palette.TextMuted;

        return TextBlock(line)
            .FontFamily(Palette.MonoFont)
            .FontSize(10)
            .Foreground(color)
            .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
            .TextWrapping(TextWrapping.NoWrap)
            .HAlign(HorizontalAlignment.Stretch);
    }
}
