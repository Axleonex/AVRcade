using System.Security.Cryptography;
using System.Text.Json;
using VrClient.Core.App;

namespace VrClient.Core.Tests;

public sealed class YapyapCatalogueTests : TempTree
{
    [Fact]
    public void CatalogueIncludesYapyapWithoutAdvertisingBackrooms()
    {
        var controller = new AppController(TestRepoRoot.Find(), (_, _) => null,
            headsetConnected: () => false, gtaSanAndreasLocator: () => null);
        var games = controller.ListGames();
        var game = Assert.Single(games.Where(g => g.Slug == "yapyap"));
        Assert.Equal("3834090", game.SteamAppId);
        Assert.Equal(GameReadiness.NeedsGame, game.Readiness);
        Assert.DoesNotContain(games, g => g.Slug == "escape-the-backrooms");
        var guide = controller.GetControllerReference("yapyap");
        Assert.NotNull(guide);
        Assert.Equal(new[] { "quest-touch", "valve-index" }, guide!.Devices.Select(d => d.Id));
        Assert.All(guide.Devices, d => Assert.StartsWith("Grab", d.RightSecondary.Action));
    }

    [Fact]
    public void MissingVrPackageKeepsDesktopModesAvailable()
    {
        var modes = PlayModePlanner.Yapyap(true, new(false, 0, "Needs package"), true);
        Assert.Equal(PlayModeAvailability.NeedsSetup, modes[0].Availability);
        Assert.Equal(PlayModeAvailability.NeedsSetup, modes[1].Availability);
        Assert.True(modes[2].CanLaunch);
        Assert.True(modes[3].CanLaunch);
    }

    [Fact]
    public void PreparedVrOnlyDoesNotNeedManagerAndModsCanChooseProfile()
    {
        var modes = PlayModePlanner.Yapyap(true, new(true, 0, "Verified"), true, false);
        Assert.True(modes[0].CanLaunch);
        Assert.Contains("SteamVR", modes[0].Status);
        Assert.True(modes[1].CanLaunch);
        Assert.Contains("manager", modes[1].Action);
    }

    [Fact]
    public void ExtraPluginPreventsAccidentalVrOnlyLaunch()
    {
        var modes = PlayModePlanner.Yapyap(true, new(true, 1, "Verified"));
        Assert.False(modes[0].CanLaunch);
        Assert.True(modes[1].CanLaunch);
    }

    [Fact]
    public async Task FourModesUseSteamOrExplicitManagerWithoutStartingGame()
    {
        Touch("yapyap.exe");
        string[] files = [
            "winhttp.dll", "doorstop_config.ini", "BepInEx/core/BepInEx.dll",
            "BepInEx/plugins/AVRcade-UUVR/Uuvr.dll",
            "BepInEx/plugins/AVRcade-UUVR/Uuvr.XR.OpenXR.dll",
            "BepInEx/plugins/AVRcade-YAPYAP-VR/YapyapVrControls.dll",
            "yapyap_Data/Plugins/UnityOpenXR.dll"
        ];
        var entries = files.Select(relative =>
        {
            var path = Touch(relative.Split('/'));
            return new { path = relative, sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))) };
        }).ToArray();
        File.WriteAllText(Touch("BepInEx", "avrcade-yapyap-vr-install.json"), JsonSerializer.Serialize(new
        {
            schema = "avrcade-yapyap-install/1",
            uuvr_revision = "de8e9218f8a9310778f2f57c189bef9ccc8c4dc8",
            installed = entries
        }));
        var steam = Path.GetDirectoryName(Touch("Steam", "steam.exe"))!;
        Touch("Steam", "steamapps", "common", "SteamVR", "bin", "win64", "vrstartup.exe");
        File.WriteAllText(Touch("Steam", "steamapps", "appmanifest_250820.acf"),
            "\"AppState\" { \"installdir\" \"SteamVR\" }");
        var manager = Touch("r2modman.exe");
        var controller = new AppController(TestRepoRoot.Find(),
            (id, _) => id == "3834090" ? Root : null);
        Assert.True(YapyapVrInstall.Inspect(Root).Installed);
        var vr = await controller.LaunchYapyapVrAsync(PlayMode.VrOnly, true, steam);
        Assert.True(vr.Ok, vr.Message);
        Assert.Contains("3834090", vr.Message);
        var mods = await controller.LaunchYapyapVrAsync(PlayMode.VrWithMods, true, steam, manager);
        Assert.True(mods.Ok, mods.Message);
        Assert.Contains("handoff", mods.Message);
        var flat = controller.LaunchYapyapDesktop(PlayMode.ModsNoVr, true, steam, manager);
        Assert.True(flat.Ok, flat.Message);
        Assert.Contains("without UUVR", flat.Message);
        var vanilla = controller.LaunchYapyapDesktop(PlayMode.Vanilla, true, steam);
        Assert.True(vanilla.Ok, vanilla.Message);
        Assert.Contains("--disable-vr", vanilla.Message);
    }
}
