// YtGrabGui — a WinUI 3 + Reactor front-end for the Rust `ytgrab` binary.
//
// The GUI deliberately owns no download logic: every network operation is
// delegated to `ytgrab`, and this shell renders the JSON metadata plus the
// progress lines the CLI already emits. The visual language mirrors the
// Regedit Reactor skin: deep navy, blue → purple → amber accents, and one
// hue per format kind.

using System.Diagnostics;
using Microsoft.UI.Reactor;
using Microsoft.UI.Reactor.Core;
using Microsoft.UI.Xaml;
using YtGrabGui;
using YtGrabGui.Components;
using YtGrabGui.Models;
using YtGrabGui.Services;
using static Microsoft.UI.Reactor.Factories;

ReactorApp.Run<YtGrabApp>("ytgrab — Reactor", width: 1120, height: 720);

internal sealed class YtGrabApp : Component
{
    public override Element Render()
    {
        // ── State ────────────────────────────────────────────────────
        var (urlText, setUrlText) = UseState(DemoData.SampleUrl);
        var (video, setVideo) = UseState<VideoInfo?>(null);
        var (selectedFormatIndex, setSelectedFormatIndex) = UseState(0);
        var (isFetching, setIsFetching) = UseState(false);
        var (audioOnly, setAudioOnly) = UseState(false);
        var (outputDir, setOutputDir) = UseState(DefaultOutputDirectory());
        var (status, setStatus) = UseState("Ready — paste a URL and press Fetch.");
        var (binaryPath, setBinaryPath) = UseState(YtGrabCli.ResolveBinary() ?? "");
        var (activeDialog, setActiveDialog) = UseState(UiDialog.None);
        var (settingsBinary, setSettingsBinary) = UseState(binaryPath);
        var (settingsOutput, setSettingsOutput) = UseState(outputDir);
        var (showLog, setShowLog) = UseState(true);
        var (demoMode, setDemoMode) = UseState(false);

        var (queue, dispatchQueue) = UseReducer<DownloadItem[], QueueAction>(QueueReducer, []);
        var (logLines, dispatchLog) = UseReducer<string[], LogAction>(LogReducer, []);

        // Process handles live outside render state; they are not UI values.
        var processes = UseRef(new Dictionary<string, Process>());
        var cancels = UseRef(new Dictionary<string, CancellationTokenSource>());
        var fetchCts = UseRef<CancellationTokenSource?>(null);

        var sync = SynchronizationContext.Current;
        var binaryExists = !string.IsNullOrWhiteSpace(binaryPath) && File.Exists(binaryPath);

        // ── Helpers ──────────────────────────────────────────────────

        void Post(Action action)
        {
            if (sync is null) action();
            else sync.Post(_ => action(), null);
        }

        void Log(string line)
        {
            if (string.IsNullOrWhiteSpace(line)) return;
            Post(() => dispatchLog(new LogAction(LogActionKind.Add, line)));
        }

        void SetStatus(string text) => Post(() => setStatus(text));

        static void CopyToClipboard(string text)
        {
            if (string.IsNullOrEmpty(text)) return;
            var package = new global::Windows.ApplicationModel.DataTransfer.DataPackage();
            package.SetText(text);
            global::Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
        }

        void Copy(string text)
        {
            CopyToClipboard(text);
            setStatus($"Copied {Truncate(text, 64)}");
        }

        // ── Commands ─────────────────────────────────────────────────

        void LoadDemo()
        {
            setVideo(DemoData.Create());
            setSelectedFormatIndex(0);
            setDemoMode(true);
            setUrlText(DemoData.SampleUrl);
            setStatus("Demo dataset loaded. Build the Rust binary to fetch real videos.");
            Log("Loaded the bundled demo dataset.");
        }

        void Fetch()
        {
            var url = urlText.Trim();
            if (string.IsNullOrWhiteSpace(url))
            {
                setStatus("Paste a YouTube URL first.");
                return;
            }

            if (!binaryExists)
            {
                LoadDemo();
                return;
            }

            fetchCts.Current?.Cancel();
            var cts = new CancellationTokenSource();
            fetchCts.Current = cts;

            setIsFetching(true);
            setDemoMode(false);
            setStatus("Fetching metadata…");
            Log($"Fetching {url}");

            var path = binaryPath;
            _ = Task.Run(async () =>
            {
                try
                {
                    var (info, error) = await YtGrabCli.FetchAsync(path, url, cts.Token);
                    Post(() =>
                    {
                        if (!ReferenceEquals(fetchCts.Current, cts)) return;
                        setIsFetching(false);

                        if (info is null)
                        {
                            setStatus(error ?? "Could not fetch this video.");
                            Log($"error: {error}");
                            return;
                        }

                        setVideo(info);
                        setSelectedFormatIndex(0);
                        setStatus($"Loaded {info.Formats.Length} formats · {info.Title}");
                        Log($"Fetched {info.Formats.Length} formats for {info.Id}");
                    });
                }
                catch (OperationCanceledException)
                {
                    Post(() => setIsFetching(false));
                }
            });
        }

        void Download(FormatEntry? format, bool extractAudio)
        {
            var info = video;
            if (info is null)
            {
                setStatus("Fetch a video before downloading.");
                return;
            }

            if (!binaryExists && !demoMode)
            {
                setStatus("ytgrab.exe was not found. Build it with `cargo build --release` or set its path in Settings.");
                return;
            }

            var useAudio = extractAudio || audioOnly;
            var formatLabel = useAudio
                ? "MP3 · best audio"
                : format is null
                    ? "best quality"
                    : $"{format.Resolution} · {format.Ext}";

            var command = YtGrabCli.BuildCommandDisplay(
                format?.FormatId, useAudio, YtGrabCli.DefaultTemplate, info.WebpageUrl);

            var id = Guid.NewGuid().ToString("N");
            var item = new DownloadItem(
                Id: id,
                Title: info.Title,
                FormatId: format?.FormatId ?? "best",
                FormatLabel: formatLabel,
                Command: command,
                OutputPath: outputDir,
                State: DownloadState.Downloading,
                Progress: 0,
                Message: "Starting…");

            dispatchQueue(new QueueAction(QueueActionKind.Add, item));
            setStatus($"Downloading {formatLabel}…");
            Log($"$ {command}");

            // Nothing to fetch without the Rust binary: animate the queue row so
            // the demo dataset still exercises the full visual state machine.
            if (!binaryExists)
            {
                var total = format?.Filesize ?? 12_800_000;
                _ = Task.Run(async () =>
                {
                    for (var step = 1; step <= 24; step++)
                    {
                        await Task.Delay(110);
                        var fraction = step / 24.0;
                        var message =
                            $"demo · {FormatEntry.HumanBytes((long)(total * fraction))} / {FormatEntry.HumanBytes(total)}";
                        Post(() => dispatchQueue(new QueueAction(
                            QueueActionKind.Progress,
                            Id: id,
                            Progress: fraction,
                            Message: message)));
                    }

                    Post(() =>
                    {
                        dispatchQueue(new QueueAction(
                            QueueActionKind.State,
                            Id: id,
                            State: DownloadState.Done,
                            Message: $"Saved to {outputDir} (demo)"));
                        setStatus($"Finished: {formatLabel} (demo)");
                    });
                    Log("Demo download finished — no bytes were written.");
                });
                return;
            }

            var path = binaryPath;
            var directory = outputDir;
            var url = info.WebpageUrl;
            var formatId = format?.FormatId;
            _ = Task.Run(async () =>
            {
                var cts = new CancellationTokenSource();
                cancels.Current[id] = cts;
                Process? process = null;

                try
                {
                    process = YtGrabCli.StartDownload(
                        path, url, formatId, useAudio, directory, YtGrabCli.DefaultTemplate);
                    processes.Current[id] = process;

                    var lastProgress = DateTime.MinValue;

                    void OnLine(string line)
                    {
                        if (string.IsNullOrWhiteSpace(line)) return;

                        if (YtGrabCli.TryParseProgress(line, out var fraction, out var message))
                        {
                            // indicatif repaints several times a second; throttle
                            // so the reconciler is not flooded with full re-renders.
                            if ((DateTime.UtcNow - lastProgress).TotalMilliseconds < 120) return;
                            lastProgress = DateTime.UtcNow;
                            Post(() => dispatchQueue(new QueueAction(
                                QueueActionKind.Progress,
                                Id: id,
                                Progress: fraction,
                                Message: message)));
                            return;
                        }

                        Log(line);
                    }

                    await Task.WhenAll(
                        YtGrabCli.PumpAsync(process.StandardOutput, OnLine, cts.Token),
                        YtGrabCli.PumpAsync(process.StandardError, OnLine, cts.Token),
                        process.WaitForExitAsync(cts.Token));

                    var code = process.ExitCode;
                    Post(() =>
                    {
                        dispatchQueue(new QueueAction(
                            QueueActionKind.State,
                            Id: id,
                            State: code == 0 ? DownloadState.Done : DownloadState.Failed,
                            Message: code == 0 ? $"Saved to {directory}" : $"ytgrab exited with code {code}."));
                        setStatus(code == 0
                            ? $"Finished: {formatLabel}"
                            : $"Download failed (exit {code}).");
                    });
                }
                catch (OperationCanceledException)
                {
                    if (process is not null) YtGrabCli.TryKill(process);
                    Post(() =>
                    {
                        dispatchQueue(new QueueAction(
                            QueueActionKind.State,
                            Id: id,
                            State: DownloadState.Cancelled,
                            Message: "Cancelled."));
                        setStatus("Download cancelled.");
                    });
                }
                catch (Exception ex)
                {
                    Post(() =>
                    {
                        dispatchQueue(new QueueAction(
                            QueueActionKind.State,
                            Id: id,
                            State: DownloadState.Failed,
                            Message: ex.Message));
                        setStatus($"Download failed: {ex.Message}");
                    });
                    Log($"error: {ex.Message}");
                }
                finally
                {
                    processes.Current.Remove(id);
                    cancels.Current.Remove(id);
                    cts.Dispose();
                }
            });
        }

        void Cancel(DownloadItem item)
        {
            if (cancels.Current.TryGetValue(item.Id, out var cts))
            {
                cts.Cancel();
            }
            else if (processes.Current.TryGetValue(item.Id, out var process))
            {
                YtGrabCli.TryKill(process);
            }

            dispatchQueue(new QueueAction(
                QueueActionKind.State,
                Id: item.Id,
                State: DownloadState.Cancelled,
                Message: "Cancelled."));
        }

        void Paste()
        {
            _ = PasteAsync();
            async Task PasteAsync()
            {
                try
                {
                    var view = global::Windows.ApplicationModel.DataTransfer.Clipboard.GetContent();
                    var text = await view.GetTextAsync();
                    if (!string.IsNullOrWhiteSpace(text))
                    {
                        setUrlText(text.Trim());
                        setStatus("URL pasted from the clipboard.");
                    }
                }
                catch
                {
                    setStatus("Could not read the clipboard.");
                }
            }
        }

        void OpenUrl()
        {
            var target = video?.WebpageUrl;
            if (string.IsNullOrWhiteSpace(target)) target = urlText;
            if (string.IsNullOrWhiteSpace(target)) return;

            try
            {
                Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                setStatus($"Could not open the browser: {ex.Message}");
            }
        }

        void OpenOutputFolder()
        {
            try
            {
                Directory.CreateDirectory(outputDir);
                Process.Start(new ProcessStartInfo("explorer.exe", $"\"{outputDir}\"")
                {
                    UseShellExecute = true,
                });
            }
            catch (Exception ex)
            {
                setStatus($"Could not open the folder: {ex.Message}");
            }
        }

        void SaveSettings()
        {
            var newBinary = settingsBinary.Trim();
            var newOutput = SettingsOutputOrDefault(settingsOutput);

            setBinaryPath(newBinary);
            setOutputDir(newOutput);
            setActiveDialog(UiDialog.None);

            if (!string.IsNullOrEmpty(newBinary) && File.Exists(newBinary))
            {
                setStatus($"ytgrab configured: {newBinary}");
                Log($"ytgrab path set to {newBinary}");
            }
            else
            {
                setStatus("Binary not found at that path — demo mode will be used.");
            }
        }

        // ── Chrome ───────────────────────────────────────────────────

        var menuBar = MenuBar([
            Menu("File", [
                MenuItem("Paste URL", Paste) with
                    { KeyboardAccelerators = [Accelerator(global::Windows.System.VirtualKey.V, global::Windows.System.VirtualKeyModifiers.Control)] },
                MenuItem("Fetch metadata", Fetch) with
                    { KeyboardAccelerators = [Accelerator(global::Windows.System.VirtualKey.Enter)] },
                MenuSeparator(),
                MenuItem("Open download folder", OpenOutputFolder),
                MenuItem("Load demo data", LoadDemo),
                MenuSeparator(),
                MenuItem("Exit", () => Application.Current.Exit()),
            ]),

            Menu("Download", [
                MenuItem("Best quality", () => Download(null, false)),
                MenuItem("Best audio (MP3)", () => Download(null, true)),
                MenuItem("Selected format", () =>
                {
                    if (video is { } info && info.Formats.Length > 0)
                        Download(info.Formats[Math.Clamp(selectedFormatIndex, 0, info.Formats.Length - 1)], false);
                }),
                MenuSeparator(),
                MenuItem("Cancel active downloads", () =>
                {
                    foreach (var item in queue.Where(i => i.State is DownloadState.Downloading))
                        Cancel(item);
                }),
                MenuItem("Clear finished", () => dispatchQueue(new QueueAction(QueueActionKind.ClearFinished))),
            ]),

            Menu("View", [
                ToggleMenuItem("Activity log", showLog, on => setShowLog(on)),
                MenuItem("Refresh binary path", () =>
                {
                    var resolved = YtGrabCli.ResolveBinary() ?? "";
                    setBinaryPath(resolved);
                    setStatus(resolved.Length > 0
                        ? $"Found ytgrab at {resolved}"
                        : "ytgrab.exe was not found.");
                }),
            ]),

            Menu("Help", [
                MenuItem("Settings…", () =>
                {
                    setSettingsBinary(binaryPath);
                    setSettingsOutput(outputDir);
                    setActiveDialog(UiDialog.Settings);
                }),
                MenuItem("About", () => setActiveDialog(UiDialog.About)),
            ]),
        ]);

        var header = BuildHeader();
        var accentStrip = BuildAccentStrip();

        var urlBar = Border(
            HStack(10,
                TextBlock("\uE774")
                    .FontFamily(Palette.IconFont)
                    .FontSize(14)
                    .Foreground(Palette.Blue)
                    .VAlign(VerticalAlignment.Center),
                TextBox(urlText, setUrlText, placeholderText: "https://www.youtube.com/watch?v=…")
                    .HAlign(HorizontalAlignment.Stretch),
                Button("Fetch", Fetch)
                    .Background(Palette.Tint(Palette.Blue, 0x33))
                    .BorderBrush(Palette.Tint(Palette.Blue, 0x88))
                    .BorderThickness(1)
                    .Foreground(Palette.Blue)
                    .CornerRadius(6)
                    .Padding(16, 6, 16, 6),
                Button("Paste", Paste)
                    .Background(Palette.SurfaceAlt)
                    .BorderBrush(Palette.Border)
                    .BorderThickness(1)
                    .Foreground(Palette.TextSecondary)
                    .CornerRadius(6)
                    .Padding(12, 6, 12, 6),
                CheckBox(audioOnly, setAudioOnly, "Audio only (MP3)")
                    .Foreground(Palette.TextSecondary)
                    .VAlign(VerticalAlignment.Center),
                When(isFetching, () => ProgressRing().Width(16).Height(16).IsActive(true))
            ).Padding(14, 8, 14, 8)
        )
        .Background(Palette.BackgroundAlt)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 0, 0, 1);

        var setupBanner = When(!binaryExists, () =>
            Border(
                HStack(10,
                    TextBlock("\uE7BA")
                        .FontFamily(Palette.IconFont)
                        .FontSize(15)
                        .Foreground(Palette.Amber)
                        .VAlign(VerticalAlignment.Center),
                    VStack(1,
                        TextBlock("ytgrab.exe was not found")
                            .FontSize(11.5)
                            .SemiBold()
                            .Foreground(Palette.Amber),
                        TextBlock("Build the Rust binary (cargo build --release) or point the app at an existing build. Demo data stays available.")
                            .FontSize(10)
                            .Foreground(Palette.TextMuted)
                            .TextWrapping(TextWrapping.Wrap)
                    ).VAlign(VerticalAlignment.Center),
                    Button("Locate binary", () =>
                    {
                        setSettingsBinary(binaryPath);
                        setSettingsOutput(outputDir);
                        setActiveDialog(UiDialog.Settings);
                    })
                    .Background(Palette.Tint(Palette.Amber, 0x22))
                    .BorderBrush(Palette.Tint(Palette.Amber, 0x66))
                    .BorderThickness(1)
                    .Foreground(Palette.Amber)
                    .CornerRadius(6)
                    .Padding(12, 4, 12, 4)
                ).Padding(14, 8, 14, 8)
            )
            .Background(Palette.Tint(Palette.Amber, 0x12))
            .BorderBrush(Palette.Tint(Palette.Amber, 0x44))
            .BorderThickness(0, 0, 0, 1)
        );

        var formatPane = Component<FormatPane, FormatPaneProps>(new FormatPaneProps(
            Video: video,
            SelectedIndex: selectedFormatIndex,
            HasBinary: binaryExists,
            IsFetching: isFetching,
            OnSelect: setSelectedFormatIndex,
            OnDownload: f => Download(f, false),
            OnDownloadAudio: _ => Download(null, true),
            OnFetch: Fetch,
            OnPaste: Paste,
            OnDownloadBest: () => Download(null, false),
            OnLoadDemo: LoadDemo,
            OnOpenUrl: OpenUrl,
            OnCopy: Copy
        ));

        var queuePane = Component<QueuePane, QueuePaneProps>(new QueuePaneProps(
            Items: queue,
            OnCancel: Cancel,
            OnRemove: item => dispatchQueue(new QueueAction(QueueActionKind.Remove, Id: item.Id)),
            OnClearFinished: () => dispatchQueue(new QueueAction(QueueActionKind.ClearFinished)),
            OnClearAll: () => dispatchQueue(new QueueAction(QueueActionKind.ClearAll)),
            OnOpenFolder: _ => OpenOutputFolder(),
            OnCopy: Copy,
            OnOpenOutputFolder: OpenOutputFolder
        ));

        var logPane = Component<LogPane, LogPaneProps>(new LogPaneProps(
            Lines: logLines,
            OnClear: () => dispatchLog(new LogAction(LogActionKind.Clear)),
            OnCopy: () => Copy(string.Join(Environment.NewLine, logLines))
        ));

        var mainArea = Grid(
            [GridSize.Px(400), GridSize.Star()],
            [GridSize.Star()],
            formatPane.Grid(row: 0, column: 0),
            Grid(
                [GridSize.Star()],
                [GridSize.Star(), GridSize.Auto, GridSize.Auto],
                queuePane.Grid(row: 0, column: 0),
                Border(Empty())
                    .Height(1)
                    .Background(Palette.Border)
                    .Grid(row: 1, column: 0),
                When(showLog, () => logPane.Height(196)).Grid(row: 2, column: 0)
            ).Grid(row: 0, column: 1)
        );

        var statusBar = BuildStatusBar(status, binaryExists, binaryPath, queue.Length, logLines.Length);

        var aboutDialog = ContentDialog(
            "About ytgrab — Reactor",
            VStack(10,
                TextBlock("ytgrab")
                    .FontSize(22)
                    .SemiBold()
                    .Foreground(Palette.TextPrimary),
                TextBlock("A fast YouTube downloader written in Rust. Speaks the InnerTube API directly — no browser, no HTML scraping, no JS engine.")
                    .FontSize(11.5)
                    .Foreground(Palette.TextSecondary)
                    .TextWrapping(TextWrapping.Wrap),
                TextBlock("This front-end is built with Microsoft.UI.Reactor on WinUI 3 and mirrors the Regedit Reactor skin: navy surfaces, blue → purple → amber accents, one hue per format kind.")
                    .FontSize(11)
                    .Foreground(Palette.TextMuted)
                    .TextWrapping(TextWrapping.Wrap),
                TextBlock("github.com/fitzypopper/ytgrab-rs")
                    .FontFamily(Palette.MonoFont)
                    .FontSize(10.5)
                    .Foreground(Palette.Blue)
            ).Width(440),
            "Close"
        ) with
        {
            IsOpen = activeDialog == UiDialog.About,
            OnClosed = _ => setActiveDialog(UiDialog.None),
        };
        aboutDialog = aboutDialog.Set(d => d.RequestedTheme = ElementTheme.Dark);

        var settingsDialog = ContentDialog(
            "Settings",
            VStack(12,
                VStack(4,
                    TextBlock("ytgrab.exe path")
                        .FontSize(11)
                        .SemiBold()
                        .Foreground(Palette.TextSecondary),
                    TextBox(settingsBinary, setSettingsBinary, placeholderText: @"C:\path\to\ytgrab.exe")
                        .FontFamily(Palette.MonoFont),
                    TextBlock(binaryExists
                            ? "Binary found."
                            : "Not found — the app will fall back to demo data.")
                        .FontSize(10)
                        .Foreground(binaryExists ? Palette.Mint : Palette.Amber)
                ),
                VStack(4,
                    TextBlock("Download folder")
                        .FontSize(11)
                        .SemiBold()
                        .Foreground(Palette.TextSecondary),
                    TextBox(settingsOutput, setSettingsOutput)
                        .FontFamily(Palette.MonoFont)
                )
            ).Width(480),
            "Save"
        ) with
        {
            IsOpen = activeDialog == UiDialog.Settings,
            SecondaryButtonText = "Cancel",
            DefaultButton = Microsoft.UI.Xaml.Controls.ContentDialogButton.Primary,
            OnClosed = result =>
            {
                if (result == Microsoft.UI.Xaml.Controls.ContentDialogResult.Primary)
                    SaveSettings();
                else
                    setActiveDialog(UiDialog.None);
            },
        };
        settingsDialog = settingsDialog.Set(d => d.RequestedTheme = ElementTheme.Dark);

        // ── Layout ───────────────────────────────────────────────────
        return Grid(
            [GridSize.Star()],
            [
                GridSize.Auto, // brand header
                GridSize.Auto, // gradient strip
                GridSize.Auto, // menu bar
                GridSize.Auto, // url bar
                GridSize.Auto, // setup banner
                GridSize.Star(), // panes
                GridSize.Auto, // status bar
            ],
            header.Grid(row: 0, column: 0),
            accentStrip.Grid(row: 1, column: 0),
            menuBar.Set(mb =>
            {
                mb.Background = BrushHelper.Parse(Palette.BackgroundAlt);
                mb.Padding = new Thickness(6, 0, 6, 0);
            }).Grid(row: 2, column: 0),
            urlBar.Grid(row: 3, column: 0),
            setupBanner.Grid(row: 4, column: 0),
            mainArea.Grid(row: 5, column: 0),
            statusBar.Grid(row: 6, column: 0),
            aboutDialog,
            settingsDialog
        )
        .Background(Palette.Background)
        .RequestedTheme(ElementTheme.Dark);
    }

    // ── Reducers ─────────────────────────────────────────────────────

    private static DownloadItem[] QueueReducer(DownloadItem[] state, QueueAction action) =>
        action.Kind switch
        {
            QueueActionKind.Add when action.Item is not null => [action.Item, .. state],

            QueueActionKind.Progress => state
                .Select(i => i.Id == action.Id
                    ? i with
                    {
                        Progress = action.Progress,
                        Message = action.Message ?? i.Message,
                    }
                    : i)
                .ToArray(),

            QueueActionKind.State => state
                .Select(i => i.Id == action.Id
                    ? i with
                    {
                        State = action.State,
                        Progress = action.State == DownloadState.Done ? 1 : i.Progress,
                        Message = action.Message ?? i.Message,
                    }
                    : i)
                .ToArray(),

            QueueActionKind.Remove => state.Where(i => i.Id != action.Id).ToArray(),

            QueueActionKind.ClearFinished => state
                .Where(i => i.State is DownloadState.Downloading or DownloadState.Queued)
                .ToArray(),

            QueueActionKind.ClearAll => state
                .Where(i => i.State is DownloadState.Downloading or DownloadState.Queued)
                .ToArray(),

            _ => state,
        };

    private static string[] LogReducer(string[] state, LogAction action) =>
        action.Kind switch
        {
            LogActionKind.Add when !string.IsNullOrWhiteSpace(action.Line) =>
                state.Append(action.Line!).TakeLast(400).ToArray(),
            LogActionKind.Clear => [],
            _ => state,
        };

    // ── Shell chrome helpers ─────────────────────────────────────────

    private static Element BuildHeader() =>
        Grid(
            [GridSize.Star(), GridSize.Auto],
            [GridSize.Auto],
            HStack(12,
                HStack(4,
                    Dot(Palette.Blue),
                    Dot(Palette.Purple),
                    Dot(Palette.Amber)
                ).VAlign(VerticalAlignment.Center),
                VStack(1,
                    TextBlock("ytgrab")
                        .FontSize(17)
                        .SemiBold()
                        .Foreground(Palette.TextPrimary),
                    TextBlock("Rust  ·  InnerTube  ·  Reactor on WinUI 3")
                        .FontSize(10)
                        .Foreground(Palette.TextMuted)
                )
            ).Grid(row: 0, column: 0).VAlign(VerticalAlignment.Center),
            HStack(6,
                Chip("REACTOR", Palette.Purple),
                Chip("RUST", Palette.Amber)
            ).Grid(row: 0, column: 1).VAlign(VerticalAlignment.Center)
        )
        .Background(Palette.BackgroundAlt)
        .Padding(16, 10, 16, 10);

    private static Element BuildAccentStrip()
    {
        var brush = new Microsoft.UI.Xaml.Media.LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0, 0.5),
            EndPoint = new Windows.Foundation.Point(1, 0.5),
        };

        foreach (var (offset, color) in new[]
        {
            (0.00, Palette.Blue),
            (0.35, Palette.Purple),
            (0.70, Palette.Pink),
            (1.00, Palette.Amber),
        })
        {
            brush.GradientStops.Add(new Microsoft.UI.Xaml.Media.GradientStop
            {
                Offset = offset,
                Color = BrushHelper.Parse(color).Color,
            });
        }

        return Border(Empty())
            .Height(2)
            .Background(brush)
            .HAlign(HorizontalAlignment.Stretch);
    }

    private static Element BuildStatusBar(
        string status, bool binaryExists, string binaryPath, int queueCount, int logCount) =>
        Grid(
            [GridSize.Auto, GridSize.Star(), GridSize.Auto],
            [GridSize.Px(28)],
            HStack(8,
                Dot(binaryExists ? Palette.Mint : Palette.Amber),
                TextBlock(status)
                    .FontSize(11)
                    .Foreground(Palette.TextSecondary)
                    .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                    .TextWrapping(TextWrapping.NoWrap)
                    .VAlign(VerticalAlignment.Center)
            ).Padding(12, 0, 8, 0).Grid(row: 0, column: 0),
            TextBlock(string.IsNullOrEmpty(binaryPath) ? "ytgrab: not configured" : binaryPath)
                .FontFamily(Palette.MonoFont)
                .FontSize(10)
                .Foreground(Palette.TextMuted)
                .TextTrimming(Microsoft.UI.Xaml.TextTrimming.CharacterEllipsis)
                .TextWrapping(TextWrapping.NoWrap)
                .HAlign(HorizontalAlignment.Right)
                .VAlign(VerticalAlignment.Center)
                .Margin(8, 0, 12, 0)
                .Grid(row: 0, column: 1),
            HStack(6,
                Chip($"{queueCount} queue", Palette.Blue),
                Chip($"{logCount} log", Palette.Amber)
            ).Margin(0, 0, 12, 0).Grid(row: 0, column: 2)
        )
        .Background(Palette.BackgroundAlt)
        .BorderBrush(Palette.BorderSoft)
        .BorderThickness(0, 1, 0, 0);

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
                .Set(tb => tb.CharacterSpacing = 100)
        )
        .Background(Palette.Tint(color, 0x22))
        .BorderBrush(Palette.Tint(color, 0x55))
        .BorderThickness(1)
        .CornerRadius(9)
        .Padding(7, 1, 7, 1)
        .VAlign(VerticalAlignment.Center);

    // ── Misc ─────────────────────────────────────────────────────────

    private static string DefaultOutputDirectory()
    {
        var downloads = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
        return Directory.Exists(downloads) ? downloads : Environment.CurrentDirectory;
    }

    private static string SettingsOutputOrDefault(string value) =>
        string.IsNullOrWhiteSpace(value) ? DefaultOutputDirectory() : value.Trim();

    private static string Truncate(string text, int max) =>
        text.Length <= max ? text : text[..max] + "…";
}
