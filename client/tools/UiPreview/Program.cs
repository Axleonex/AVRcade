using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Threading;
using Avalonia.VisualTree;
using VrClient.App.Services;
using VrClient.App.ViewModels;
using VrClient.App.Views;

// Usage: UiPreview <output-dir> [--dark|--light] [--no-covers] [--width N] [--height N] [slug ...]
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
var viewModel = args.Contains("--no-covers")
    ? new MainWindowViewModel(_ => null)
    : new MainWindowViewModel(); // disposed by MainWindow.Closed
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
