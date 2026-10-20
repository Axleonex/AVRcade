using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Threading;
using Avalonia.VisualTree;
using System.Text.Json;
using VrClient.Core.Streaming;
using VrClient.Streaming;
using VrClient.App.Services;
using VrClient.App.ViewModels;
using VrClient.App.Views;

// Usage: UiPreview <output-dir> [--dark|--light] [--no-covers] [--width N] [--height N] [--streaming|--streaming-states] [slug ...]
// Renders the library, settings, and one detail page per slug (default: every game).
// --no-covers uses the typographic covers, for screenshots that go into the repository.
// Without --dark or --light the pages render in the player's saved theme.
var outputDir = args.FirstOrDefault(a => !a.StartsWith("--", StringComparison.Ordinal))
    ?? Path.Combine(Path.GetTempPath(), "avrcade-ui-preview");
int Option(string name, int fallback)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length && int.TryParse(args[index + 1], out var value)
        ? value : fallback;
}
var width = Option("--width", 1240);
var height = Option("--height", 820);
var optionValues = new[] { "--width", "--height" }
    .Select(name => Array.IndexOf(args, name)).Where(i => i >= 0).Select(i => i + 1).ToHashSet();
var slugs = args.Where((a, i) => !a.StartsWith("--", StringComparison.Ordinal) &&
        !optionValues.Contains(i)).Skip(1).ToArray();
Directory.CreateDirectory(outputDir);

AppBuilder.Configure<VrClient.App.App>()
    .UseSkia()
    .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
    .WithInterFont()
    .SetupWithoutStarting();

// The preview never changes the player's saved theme.
if (args.Contains("--dark") || args.Contains("--light"))
    LauncherThemeService.Current.SetMode(args.Contains("--dark"), persist: false);
var suffix = LauncherThemeService.Current.IsDark ? "-dark" : string.Empty;
var modularChecks = args.Contains("--streaming-modular-check");
var spectatorFixtureDirectory = modularChecks
    ? Path.Combine(Path.GetTempPath(), "avrcade-ui-spectators-" + Guid.NewGuid().ToString("N")) : null;
var fixtureBridge = spectatorFixtureDirectory is null ? null : new SpectatorBridge(spectatorFixtureDirectory);
var fixtureStreaming = fixtureBridge is null ? null : new StreamingViewModel(fixtureBridge,
    new StreamingSettings(Path.Combine(spectatorFixtureDirectory!, "obs-settings.json")));
var viewModel = args.Contains("--no-covers")
    ? new MainWindowViewModel(_ => null, fixtureStreaming)
    : new MainWindowViewModel(streaming: fixtureStreaming); // disposed by MainWindow.Closed
var window = new MainWindow { DataContext = viewModel, Width = width, Height = height };
window.Show();

// --time-refresh: how long one library re-detection takes on this PC.
if (args.Contains("--time-refresh"))
    for (var run = 1; run <= 3; run++)
    {
        var watch = System.Diagnostics.Stopwatch.StartNew();
        viewModel.RefreshCommand.Execute(null);
        Console.WriteLine($"refresh {run}: {watch.ElapsedMilliseconds} ms");
    }

void Capture(string name)
{
    for (var pass = 0; pass < 3; pass++)
    {
        Dispatcher.UIThread.RunJobs();
        AvaloniaHeadlessPlatform.ForceRenderTimerTick();
    }
    var frame = window.CaptureRenderedFrame()
        ?? throw new InvalidOperationException("No frame was rendered.");
    var path = Path.Combine(outputDir, name + suffix + ".png");
    frame.Save(path);
    Console.WriteLine(path);
}

Capture("library");

if (fixtureBridge is not null)
{
    // Controlled UI integration check. The publisher represents arbitrary game
    // backends; it never loads/injects a game, connects OBS or starts an output.
    Directory.CreateDirectory(fixtureBridge.DirectoryPath);
    using var process = System.Diagnostics.Process.GetCurrentProcess();
    var json = new JsonSerializerOptions { IncludeFields = true };
    var model = viewModel.Streaming;
    model.Connected = true;
    foreach (var game in viewModel.Games)
    {
        viewModel.SelectedGame = game;
        viewModel.ShowGameDetail = true;
        Dispatcher.UIThread.RunJobs();
        var streamButton = window.FindControl<Button>("GameStreamButton")
            ?? throw new InvalidOperationException("Game page has no Stream button.");
        if (!streamButton.IsEffectivelyVisible || !Equals(streamButton.Content, game.StreamButtonLabel))
            throw new InvalidOperationException("Game page Stream button binding is incorrect for " + game.Slug);
        model.Recording = true;
        Dispatcher.UIThread.RunJobs();
        if (!Equals(streamButton.Content, game.StreamButtonLabel) || !model.HasActiveOutput)
            throw new InvalidOperationException("Game page Stream button did not update recording status for " + game.Slug);
        model.Recording = false;
        if (!model.CanControl || model.SpectatorContextText != game.DisplayName || model.CanToggleSteadycam)
            throw new InvalidOperationException("Catalogue streaming context is incorrect for " + game.Slug);
        var session = new SpectatorStatus
        {
            ProtocolVersion = SpectatorProtocol.Version, GameSlug = game.Slug, AdapterId = "preview.synthetic",
            ProcessId = process.Id, ProcessName = process.ProcessName, SessionId = Guid.NewGuid().ToString("N"),
            UpdatedUtcTicks = DateTime.UtcNow.Ticks, SupportsSteadycam = true, CameraAvailable = true,
            CaptureHint = "Capture this game's spectator window."
        };
        File.WriteAllText(fixtureBridge.StatusPath(session), JsonSerializer.Serialize(session, json));
        // Catalogue updates use the same runtime discovery as timer ticks.
        model.SetCatalogue(viewModel.Games.Select(g => new KeyValuePair<string, string>(g.Slug, g.DisplayName)));
        if (!model.CanToggleSteadycam) throw new InvalidOperationException("Adapter not discovered for " + game.Slug);
        model.ToggleSteadycamCommand.Execute(null);
        if (model.SteadycamEnabled) throw new InvalidOperationException("An unacknowledged camera must not appear enabled.");
        var request = JsonSerializer.Deserialize<SpectatorRequest>(File.ReadAllText(fixtureBridge.RequestPath(session)), json)!;
        if (!SpectatorProtocol.IsForSession(request, session)) throw new InvalidOperationException("Camera command addressed the wrong session.");
        session.Enabled = true;
        session.RequestId = request.RequestId;
        session.UpdatedUtcTicks = DateTime.UtcNow.Ticks;
        File.WriteAllText(fixtureBridge.StatusPath(session), JsonSerializer.Serialize(session, json));
        model.SetCatalogue(viewModel.Games.Select(g => new KeyValuePair<string, string>(g.Slug, g.DisplayName)));
        if (!model.SteadycamEnabled || !model.CaptureText.Contains(session.CaptureHint))
            throw new InvalidOperationException("Camera acknowledgement or capture guidance not applied for " + game.Slug);
        Console.WriteLine("PASS catalogue streaming + synthetic adapter: " + game.Slug);
        File.Delete(fixtureBridge.StatusPath(session));
        File.Delete(fixtureBridge.RequestPath(session));
        model.SetCatalogue(viewModel.Games.Select(g => new KeyValuePair<string, string>(g.Slug, g.DisplayName)));
        if (!model.CanControl || model.CanToggleSteadycam) throw new InvalidOperationException("Adapter loss must preserve OBS controls.");
        viewModel.BackToLibraryCommand.Execute(null);
    }
    model.DisconnectCommand.Execute(null);
}

if (args.Contains("--streaming") || args.Contains("--streaming-states"))
{
    var streaming = new StreamingWindow { DataContext = viewModel.Streaming };
    streaming.Show(window);
    void CaptureStreaming(string name)
    {
        for (var pass = 0; pass < 3; pass++)
        {
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        }
        using var frame = streaming.CaptureRenderedFrame()
            ?? throw new InvalidOperationException("No streaming frame was rendered.");
        var path = Path.Combine(outputDir, name + suffix + ".png");
        frame.Save(path);
        Console.WriteLine(path);
    }
    CaptureStreaming("streaming");
    if (args.Contains("--streaming-states"))
    {
        // Visual fixtures only. Never connect to real OBS or start any output.
        // Protocol behavior is tested with an isolated WebSocket server in Core.Tests.
        var model = viewModel.Streaming;
        model.Scenes.Add("Gameplay");
        model.Scenes.Add("Break");
        model.CurrentScene = model.SelectedScene = "Gameplay";
        model.ConnectionText = "Connected to OBS";
        model.Connected = true;
        model.SetupExpanded = false;
        model.Message = "Check your capture and audio in OBS before recording or going live.";
        CaptureStreaming("streaming-connected-fixture");
        model.Streaming = model.Recording = true;
        model.SteadycamEnabled = model.CanToggleSteadycam = true;
        model.SpectatorContextText = "Example game";
        model.SteadycamText = "On · Your headset view stays responsive. Capture this game's spectator window in OBS.";
        model.CaptureText = "Capture this game's spectator window in OBS. Check the preview and audio. Closing this panel leaves your outputs running.";
        CaptureStreaming("streaming-live-fixture");
        Capture("library-live-fixture");
        if (!model.CanControl || model.ToolbarText != "LIVE + REC") throw new InvalidOperationException("Streaming state did not reach the Stream controls.");
        model.Busy = true;
        if (model.CanControl) throw new InvalidOperationException("Busy output controls must be disabled.");
        model.Busy = false;
        model.DisconnectCommand.Execute(null);
        if (model.CanControl) throw new InvalidOperationException("Disconnected output controls must be disabled.");
        var savedPort = model.Port;
        model.Port = "0";
        model.ConnectCommand.ExecuteAsync(null).GetAwaiter().GetResult();
        if (!model.Message.Contains("65535")) throw new InvalidOperationException("Invalid port needs a recovery message.");
        CaptureStreaming("streaming-error-fixture");
        model.Port = savedPort;
        model.SteadycamEnabled = false;
        model.CanToggleSteadycam = false;
    }
    streaming.Close();
}

viewModel.ShowSettingsPageCommand.Execute(null);
Capture("settings");
viewModel.BackToLibraryCommand.Execute(null);

foreach (var game in viewModel.Games.Where(g => slugs.Length == 0 || slugs.Contains(g.Slug)).ToArray())
{
    viewModel.OpenGameCommand.Execute(game);
    Capture($"detail-{game.Slug}");
    // Taller frames show each tab of the game page without scrolling.
    window.Height = height * 2;
    foreach (var (tab, name) in new[] { (1, "mods"), (2, "controls"), (3, "about") })
    {
        viewModel.DetailTabIndex = tab;
        foreach (var expander in window.GetVisualDescendants().OfType<Expander>()
                     .Where(e => e.IsEffectivelyVisible).ToArray())
            expander.IsExpanded = true;
        Capture($"detail-{game.Slug}-{name}");
    }
    window.Height = height;
    viewModel.BackToLibraryCommand.Execute(null);
}

window.Close();
if (spectatorFixtureDirectory is not null) Directory.Delete(spectatorFixtureDirectory, recursive: true);
