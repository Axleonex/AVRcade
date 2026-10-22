using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Modpack;

namespace VrClient.Core.Tests;

public sealed class GameUpdateIsolationTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-update-isolation-" + Guid.NewGuid().ToString("N"));
    public GameUpdateIsolationTests() => Directory.CreateDirectory(Path.Combine(_root, "config"));
    public void Dispose() => Directory.Delete(_root, true);
    private void Routes(object games) => File.WriteAllText(Path.Combine(_root, "config", "mod-update-routes.json"),
        JsonSerializer.Serialize(new { schema = "mod-update-routes/1", games }));
    private static object Route(string adapter) => new { adapter, process_names = new[] { "fixture-not-running" }, sources = Array.Empty<object>() };

    [Fact]
    public async Task InvalidUnrelatedGameDoesNotBlockSelectedGame()
    {
        Routes(new Dictionary<string, object> { ["repo"] = Route("thunderstore"), ["gta-iv"] = "invalid unrelated data" });
        using var http = new HttpClient();
        var game = new GameView("repo", "R.E.P.O.", "3241660", null, false, "Allow", "fixture", GameEngine.Unity,
            GameReadiness.NeedsGame, "Install the selected game.");
        var outcome = await new GameModUpdater(_root, http).UpdateAsync(game, new AppController(_root, (_, _) => null));
        Assert.False(outcome.Ok);
        Assert.Contains("locate the game", outcome.Message);
        Assert.DoesNotContain("gta", outcome.Message);
    }

    [Fact]
    public async Task AnotherGamesAdapterCannotInstallIntoTheSelectedFolder()
    {
        Routes(new Dictionary<string, object> { ["repo"] = Route("cyberpunk") });
        var gameDir = Path.Combine(_root, "disk-b", "REPO");
        Directory.CreateDirectory(gameDir);
        File.WriteAllText(Path.Combine(gameDir, "REPO.exe"), "fixture");
        using var http = new HttpClient();
        var game = new GameView("repo", "R.E.P.O.", "3241660", gameDir, false, "Allow", "fixture", GameEngine.Unity,
            GameReadiness.NeedsVrMod, "Install VR.");
        var outcome = await new GameModUpdater(_root, http).UpdateAsync(game, new AppController(_root, (_, _) => null));
        Assert.False(outcome.Ok);
        Assert.Contains("adapter", outcome.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(Directory.Exists(Path.Combine(gameDir, ".avrcade")));
    }

    [Fact]
    public async Task BlockedGameStillChecksUpstreamWithoutWritingOrRecoveringAnyGameFiles()
    {
        Routes(new Dictionary<string, object> { ["repo"] = new {
            adapter = "thunderstore", process_names = new[] { "fixture-not-running" },
            sources = new[] { new { name = "VR mod", repository = "author/vr", kind = "release", reviewed_reference = "1.0" } } },
            ["other-game"] = "unrelated broken route" });
        var root = Path.Combine(_root, "selected"); Directory.CreateDirectory(root);
        var other = Path.Combine(_root, "other"); Directory.CreateDirectory(other);
        File.WriteAllText(Path.Combine(root, "keep.txt"), "selected original");
        File.WriteAllText(Path.Combine(other, "keep.txt"), "other original");
        var handler = new ReleaseHandler(); using var http = new HttpClient(handler);
        var game = new GameView("repo", "REPO", "3241660", root, true, "Block", "BuildMismatch",
            GameEngine.Unity, GameReadiness.Blocked, "This VR package does not support the installed game version.");
        var result = await new GameModUpdater(_root, http).UpdateAsync(game, new AppController(_root, (_, _) => null));
        Assert.False(result.Ok); Assert.Equal(1, handler.Calls);
        Assert.True(Assert.Single(result.Releases).NewerAvailable);
        Assert.Contains("2.0", result.Message); Assert.Contains("No game files were changed", result.Message);
        Assert.False(Directory.Exists(Path.Combine(root, ".avrcade")));
        Assert.Equal("selected original", File.ReadAllText(Path.Combine(root, "keep.txt")));
        Assert.Equal("other original", File.ReadAllText(Path.Combine(other, "keep.txt")));
    }

    private sealed class ReleaseHandler : HttpMessageHandler
    {
        public int Calls { get; private set; }
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            Calls++;
            Assert.Equal("https://api.github.com/repos/author/vr/releases?per_page=30", request.RequestUri!.AbsoluteUri);
            return Task.FromResult(new HttpResponseMessage(System.Net.HttpStatusCode.OK) { Content = new StringContent("""
                [{"draft":false,"tag_name":"2.0","published_at":"2026-10-22T00:00:00Z","html_url":"https://github.com/author/vr/releases/tag/2.0"},
                 {"draft":false,"tag_name":"1.0","published_at":"2026-10-01T00:00:00Z","html_url":"https://github.com/author/vr/releases/tag/1.0"}]
                """) });
        }
    }
}
