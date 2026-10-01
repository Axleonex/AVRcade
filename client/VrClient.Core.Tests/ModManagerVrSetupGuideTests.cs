using VrClient.Core.ModManagers;

namespace VrClient.Core.Tests;

public sealed class ModManagerVrSetupGuideTests
{
    [Theory]
    [InlineData("repo", "DaXcess-RepoXR")]
    [InlineData("lethal-company", "DaXcess-LethalCompanyVR")]
    [InlineData("peak", "Andrey04o-PeakVR")]
    [InlineData("content-warning", "DaXcess-CWVR")]
    [InlineData("big-walk", "CircuitLord-Big_Walk_VR")]
    public void EveryManagerNamesTheExactPackageForEveryFriendslopGame(string slug, string packageKey)
    {
        var route = CommunityVrRoutes.All[slug];
        Assert.Equal(packageKey, ModManagerVrSetupGuide.PackageIdentity(route));

        foreach (var manager in Enum.GetValues<ModManagerKind>())
        {
            var instructions = ModManagerVrSetupGuide.Instructions(route, manager);
            Assert.Contains(route.Package, instructions);
            Assert.Contains(route.Namespace, instructions);
            Assert.Contains(route.GameFolder, instructions);
            Assert.Contains("profile", instructions, StringComparison.OrdinalIgnoreCase);
        }
    }

    [Theory]
    [InlineData(ModManagerKind.R2Modman)]
    [InlineData(ModManagerKind.Thunderstore)]
    public void ThunderstoreManagersPointToTheirOwnModPagesAndDependencies(ModManagerKind manager)
    {
        var instructions = ModManagerVrSetupGuide.Instructions(CommunityVrRoutes.All["repo"], manager);
        Assert.Contains("search", instructions, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("dependencies", instructions, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("AVRcade search", instructions, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void VortexGuidanceDoesNotPromiseThunderstoreDiscoveryOrUnsupportedDeployment()
    {
        var instructions = ModManagerVrSetupGuide.Instructions(
            CommunityVrRoutes.All["big-walk"], ModManagerKind.Vortex);
        Assert.Contains("Thunderstore", instructions);
        Assert.Contains("supports", instructions);
        Assert.Contains("import", instructions);
        Assert.Contains("dependencies", instructions);
        Assert.DoesNotContain("search Vortex", instructions, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void BigWalkGuidanceCallsOutItsSteamVrPrerequisite()
    {
        foreach (var manager in Enum.GetValues<ModManagerKind>())
            Assert.Contains("Start SteamVR", ModManagerVrSetupGuide.Instructions(
                CommunityVrRoutes.All["big-walk"], manager));
    }
}
