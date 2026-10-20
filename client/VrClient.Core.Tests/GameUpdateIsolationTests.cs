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
}
