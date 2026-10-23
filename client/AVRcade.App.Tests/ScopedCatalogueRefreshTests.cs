using Avalonia.Threading;
using VrClient.App.ViewModels;
using VrClient.Core.App;
using Xunit;

namespace AVRcade.App.Tests;

public sealed class ScopedCatalogueRefreshTests : IDisposable, IClassFixture<CatalogueUiLoop>
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-scoped-ui-" + Guid.NewGuid().ToString("N"));
    private readonly List<string> _calls = [];
    private readonly Dictionary<string, string> _installs = [];
    private readonly AppController _controller;
    public static TheoryData<string, string> Games => new()
    {
        { "big-walk", "1478500" }, { "content-warning", "2881650" }, { "cyberpunk-2077", "1091500" },
        { "escape-the-backrooms", "1943950" }, { "gta-san-andreas", "12120" },
        { "lethal-company", "1966720" }, { "peak", "3527290" }, { "repo", "3241660" },
        { "rv-there-yet", "3949040" }, { "yapyap", "3834090" }
    };

    private readonly CatalogueUiLoop _ui;
    public ScopedCatalogueRefreshTests(CatalogueUiLoop ui)
    {
        _ui = ui;
        var source = new DirectoryInfo(AppContext.BaseDirectory);
        while (source is not null && !File.Exists(Path.Combine(source.FullName, "client", "VrClient.sln")))
            source = source.Parent;
        Assert.NotNull(source);
        var config = Path.Combine(source.FullName, "config");
        foreach (var file in Directory.EnumerateFiles(config, "*.json", SearchOption.AllDirectories))
        {
            var target = Path.Combine(_root, "config", Path.GetRelativePath(config, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target);
        }
        _controller = new(_root, (app, _) => { _calls.Add(app); return _installs.GetValueOrDefault(app); },
            headsetConnected: () => false, gtaSanAndreasLocator: () => { _calls.Add("12120"); return null; },
            installationStore: new VrClient.Core.Compatibility.GameInstallationStore(Path.Combine(_root, "preferences", "installations.json")));
    }

    public void Dispose() => Directory.Delete(_root, true);
    private MainWindowViewModel Window() => new(_ => null, controller: _controller);

    [Fact]
    public void Switching_games_does_not_carry_the_previous_troubleshooting_step()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            window.SelectedGame = window.Games.Single(g => g.Slug == "repo");
            window.TroubleshootingFindings = [new("VR components", "Attention", "missing package", "open setup", VrClient.Core.Launch.TroubleshootingAction.VrSetup)];
            Assert.Equal("Finish VR setup", window.TroubleshootingGuide.Next.Title);
            window.SelectedGame = window.Games.Single(g => g.Slug == "peak");
            Assert.Empty(window.TroubleshootingFindings);
            Assert.Equal(HelpAction.CheckGame, window.TroubleshootingGuide.Next.Action);
        });
    }

    [Fact]
    public void GameVersionMismatchOffersRecoveryThroughBothVrButtonsWhileMonitorPlayRemainsAvailable()
    {
        _ui.Run(() =>
        {
            var root = Path.Combine(_root, "older-copy");
            Directory.CreateDirectory(root);
            File.WriteAllText(Path.Combine(root, "REPO.exe"), "fixture");
            var game = new GameView("repo", "R.E.P.O.", "3241660", root, true, "Allow", "test",
                GameEngine.Unity, GameReadiness.ProfileUnverified, "Experimental");
            var config = Path.Combine(_root, "config", "games", "repo.json");
            File.WriteAllText(config, """{"executable_names":["REPO.exe"],"game_compatibility":{"supported_versions":["2.31"],"package_version":"1.0","source_url":"https://example.com/reviewed"}}""");
            using var row = new GameItemViewModel(game, null, null,
                VrClient.Core.Legacy.GtaSanAndreasTurnSettings.Default,
                VrClient.Core.Redengine.CyberpunkTurnSettings.Default, null);
            row.SetCompatibility(_controller.Compatibility, _ => { });
            row.SetPlayModeSource(_ => Enum.GetValues<PlayMode>().Select(mode =>
                new PlayModeState(mode, PlayModeAvailability.Ready, "ready", "Play")).ToArray(),
                false, new("", []));
            Assert.True(row.CanRecoverGameVersion);
            Assert.Equal("2.31", row.VersionRecovery!.RequiredBuild);
            foreach (var mode in new[] { PlayMode.VrOnly, PlayMode.VrWithMods })
            {
                Assert.True(row.PlayModeFor(mode)!.CanAct);
                Assert.False(row.PlayModeFor(mode)!.IsLaunch);
                Assert.Equal(PlayModeSetup.RecoverGameVersion, row.PlayModeFor(mode)!.State.Setup);
            }
            Assert.True(row.PlayModeFor(PlayMode.Vanilla)!.IsLaunch);
        });
    }

    [Theory, MemberData(nameof(Games))]
    public void RefreshOnlyReplacesTheRequestedGameAndPreservesFourModes(string slug, string appId)
    {
        _ui.Run(() =>
        {
            using var window = Window();
            var before = window.Games.ToDictionary(g => g.Slug);
            window.SelectedGame = before[slug]; window.ShowGameDetail = true;
            window.DetailTabIndex = 2; window.StatusText = "Keep operation result";
            _calls.Clear();
            window.RedetectQuietly();
            Assert.Equal(new[] { appId }, _calls);
            Assert.NotSame(before[slug], window.SelectedGame);
            Assert.Equal(slug, window.SelectedGame!.Slug);
            Assert.Equal(Enum.GetValues<PlayMode>(), window.SelectedGame.PlayModes.Select(m => m.Mode));
            Assert.Equal(2, window.DetailTabIndex);
            Assert.Equal("Keep operation result", window.StatusText);
            foreach (var game in window.Games.Where(g => g.Slug != slug)) Assert.Same(before[game.Slug], game);
        });
    }

    [Fact]
    public void CompletingAnotherGamesOperationDoesNotSwitchTheCurrentPage()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            var selected = window.Games.Single(g => g.Slug == "peak");
            var oldRepo = window.Games.Single(g => g.Slug == "repo");
            window.SelectedGame = selected; window.ShowGameDetail = true;
            _calls.Clear();
            window.RefreshGame("repo");
            Assert.Equal(new[] { "3241660" }, _calls);
            Assert.Same(selected, window.SelectedGame);
            Assert.NotSame(oldRepo, window.Games.Single(g => g.Slug == "repo"));
        });
    }

    [Fact]
    public void ChangingTheSelectedInstallationRefreshesItsNewRootOnly()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            window.SelectedGame = window.Games.Single(g => g.Slug == "repo");
            var others = window.Games.Where(g => g.Slug != "repo").ToArray();
            var root = Path.Combine(_root, "disk B", "steamapps", "common", "REPO");
            Directory.CreateDirectory(root);
            File.WriteAllText(Path.Combine(root, "REPO.exe"), "fixture");
            _installs["3241660"] = root;
            _calls.Clear(); window.RefreshGame("repo");
            Assert.Equal(new[] { "3241660" }, _calls);
            Assert.Equal(root, window.SelectedGame!.View.InstallDir);
            foreach (var game in others) Assert.Same(game, window.Games.Single(g => g.Slug == game.Slug));
        });
    }

    [Fact]
    public void QuietLibraryRefreshDoesNotInspectAnyGame()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            var before = window.Games.ToArray();
            window.ShowGameDetail = false;
            _calls.Clear(); window.RedetectQuietly();
            Assert.Empty(_calls);
            foreach (var game in before) Assert.Same(game, window.Games.Single(g => g.Slug == game.Slug));
        });
    }

    [Fact]
    public void ExplicitRefreshStillInspectsTheWholeCatalogue()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            _calls.Clear(); window.RefreshCommand.Execute(null);
            Assert.Equal(10, _calls.Count);
            Assert.Equal(10, _calls.Distinct().Count());
            Assert.Equal(10, window.Games.Count);
        });
    }

    [Fact]
    public void BrokenUnrelatedConfigCannotInterruptTheSelectedPage()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            window.SelectedGame = window.Games.Single(g => g.Slug == "repo");
            window.ShowGameDetail = true;
            File.WriteAllText(Path.Combine(_root, "config", "redengine", "native", "cyberpunk-2077.json"), "{");
            _calls.Clear(); window.RedetectQuietly();
            Assert.Equal(new[] { "3241660" }, _calls);
            Assert.Equal("repo", window.SelectedGame!.Slug);
        });
    }

    [Fact]
    public void QuietRefreshDoesNotRaceAnActiveInstallation()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            window.ShowGameDetail = true;
            _calls.Clear(); window.Busy = true; window.RedetectQuietly();
            Assert.Empty(_calls);
            window.Busy = false; window.IsInstallingCyberpunkRequirements = true; window.RedetectQuietly();
            Assert.Empty(_calls);
        });
    }

    [Fact]
    public void RemovingAnEntryDoesNotDisposeOrReplaceOtherGamePages()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            var others = window.Games.Where(g => g.Slug != "repo").ToArray();
            window.SelectedGame = window.Games.Single(g => g.Slug == "repo");
            var path = Path.Combine(_root, "config", "modpacks", "repo.modpack.json");
            File.Move(path, path + ".disabled");
            _calls.Clear(); window.RefreshGame("repo");
            Assert.Null(window.SelectedGame);
            Assert.Equal(9, window.Games.Count);
            foreach (var game in others) Assert.Same(game, window.Games.Single(g => g.Slug == game.Slug));
            Assert.Empty(_calls);
        });
    }

    [Fact]
    public void RecheckSetupPreflightOnlyRefreshesItsGameWhenDiskIsMissing()
    {
        _ui.Run(() =>
        {
            using var window = Window();
            window.SelectedGame = window.Games.Single(g => g.Slug == "repo");
            _calls.Clear();
            var recheck = window.RecheckGameSetupCommand.ExecuteAsync(null);
            Assert.True(recheck.IsCompletedSuccessfully);
            Assert.Equal(new[] { "3241660" }, _calls);
            Assert.Contains("Reconnect", window.StatusText);
        });
    }
}

/// Avalonia timers require a pumped dispatcher, including in headless view-model tests.
public sealed class CatalogueUiLoop : IDisposable
{
    private readonly CancellationTokenSource _stop = new();
    private readonly ManualResetEventSlim _ready = new();
    private readonly Thread _thread;
    private Dispatcher? _dispatcher;
    public CatalogueUiLoop()
    {
        _thread = new Thread(() =>
        {
            _dispatcher = Dispatcher.UIThread;
            _ready.Set();
            while (!_stop.IsCancellationRequested)
            {
                _dispatcher.RunJobs();
                _stop.Token.WaitHandle.WaitOne(5);
            }
        }) { IsBackground = true, Name = "Catalogue test UI" };
        _thread.Start();
        if (!_ready.Wait(TimeSpan.FromSeconds(5))) throw new TimeoutException("Test dispatcher did not start.");
    }
    public void Run(Action action) => _dispatcher!.Invoke(action);
    public void Dispose()
    {
        _stop.Cancel();
        _dispatcher!.Post(() => { });
        if (!_thread.Join(TimeSpan.FromSeconds(5))) throw new TimeoutException("Test dispatcher did not stop.");
        _stop.Dispose();
        _ready.Dispose();
    }
}
