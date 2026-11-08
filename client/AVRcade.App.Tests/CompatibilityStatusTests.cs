using VrClient.App.ViewModels;
using VrClient.Core.App;
using VrClient.Core.Config;
using Xunit;

namespace AVRcade.App.Tests;

public sealed class CompatibilityStatusTests
{
    [Theory, MemberData(nameof(ScopedCatalogueRefreshTests.Games), MemberType = typeof(ScopedCatalogueRefreshTests))]
    public void EveryGameKeepsItsCompatibilityReasonAndExpectedUpdateRoute(string slug, string appId)
    {
        var view = new GameView(slug, slug, appId, Path.GetTempPath(), true, "Block", "BuildMismatch",
            slug switch
            {
                "cyberpunk-2077" => GameEngine.Redengine,
                "titanfall-2" => GameEngine.Source,
                _ => GameEngine.Unity
            },
            GameReadiness.Blocked, "Installed version is not supported by this VR package.");
        using var game = new GameItemViewModel(view, null, null, default!, default!, null,
            [new GameModCredit("VR package", "Author", "https://github.com/author/vr")]);
        game.SetSetupProgress("File and controller checks passed.", false);
        game.SetCyberpunkSetupProgress("File and controller checks passed.", false);
        game.SetModUpdateProgress("Previously installed package.", false);
        Assert.Equal(view.ReadinessDetail, game.SetupStatus);
        if (game.IsCyberpunk) Assert.Equal(view.ReadinessDetail, game.CyberpunkSetupStatus);
        Assert.Equal(slug != "titanfall-2", game.CanUpdateMods);
        if (game.CanUpdateMods) Assert.Contains("compatible", game.ModUpdateButtonHint);
    }
}
