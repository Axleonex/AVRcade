using VrClient.Core.App;
using VrClient.Core.Tests;
using VrClient.Core.Unreal;

public sealed class BackroomsCatalogueTests
{
    [Fact]
    public void Catalogue_discovers_backrooms_through_its_reviewed_uevr_route()
    {
        var root = TestRepoRoot.Find();
        var controller = new AppController(root,
            (id, exe) => id == "1943950" && exe == "Backrooms.exe" ? @"C:\Games\Backrooms" : null,
            _ => true, _ => true, () => true);
        var game = Assert.Single(controller.ListGames(), item => item.Slug == BackroomsTrackedHands.GameSlug);

        Assert.Equal("1943950", game.SteamAppId);
        Assert.Equal(GameEngine.Unreal, game.Engine);
        Assert.NotEqual(GameReadiness.Blocked, game.Readiness);
        Assert.Equal("private_modded_coop_requires_acknowledgement", game.SafetyReason);
        Assert.True(game.ProfileImportRequired);
        Assert.Contains("uevr-profiles/repo", game.ProfileSourceUrl);
    }
}
