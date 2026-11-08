using VrClient.Core.App;
using VrClient.Core.Config;

namespace VrClient.Core.Tests;

public sealed class Titanfall2CatalogueTests : TempTree
{
    [Fact]
    public void RecognizesOnlyACompleteCircuitLordProfileInTheSelectedInstall()
    {
        var exe = Touch("Titanfall 2", "Titanfall2.exe");
        var gameDir = Path.GetDirectoryName(exe)!;
        var root = TestRepoRoot.Find();
        var controller = new AppController(root,
            (id, name) => id == "1237970" && name == "Titanfall2.exe" ? gameDir : null);

        var game = Assert.IsType<GameView>(controller.GetGame("titanfall-2", headsetConnected: false));
        Assert.Equal(GameReadiness.NeedsVrMod, game.Readiness);
        Assert.Equal(new[] { exe }, ControllerRuntimeBindings.Executables(root, game));
        var receipt = Touch("Titanfall 2", ".circuitlord-vr-mods", "Titanfall2VR.json");
        File.WriteAllText(receipt, """{"id":"Titanfall2VR","version":"1.0.25"}""");
        Touch("Titanfall 2", "Titanfall2VRLauncher.exe");
        Touch("Titanfall 2", "TF2VR", "Northstar.dll");
        Touch("Titanfall 2", "TF2VR", "tools", "xr_probe.exe");
        Touch("Titanfall 2", "TF2VR", "tools", "crash_monitor.exe");
        Touch("Titanfall 2", "TF2VR", "tools", "launch.json");
        Assert.False(controller.GetGame("titanfall-2")!.ModInstalled);

        Touch("Titanfall 2", "TF2VR", "plugins", "Titanfall2VR.dll");
        game = controller.GetGame("titanfall-2")!;
        Assert.True(game.ModInstalled);
        Assert.Equal(GameReadiness.ProfileUnverified, game.Readiness);

        File.WriteAllText(receipt, "{");
        Assert.False(controller.GetGame("titanfall-2")!.ModInstalled);
    }

    [Theory]
    [InlineData(false, false, false)]
    [InlineData(true, false, true)]
    [InlineData(true, true, false)]
    [InlineData(true, true, true)]
    public void PageOffersAnHonestHandoffAndIndependentVanillaMode(bool game, bool profile, bool installer)
    {
        var modes = PlayModePlanner.Titanfall2(game, profile, installer);
        Assert.Equal(Enum.GetValues<PlayMode>(), modes.Select(mode => mode.Mode));
        foreach (var mode in modes.Take(2))
        {
            Assert.Equal(profile && installer, mode.CanLaunch);
            Assert.Equal(profile && installer ? PlayModeSetup.None : PlayModeSetup.OpenVrSetup, mode.Setup);
        }
        Assert.False(modes[2].CanAct);
        Assert.Equal(game, modes[3].CanLaunch);
    }

    [Fact]
    public void VanillaUsesSteamWithoutVrFlags()
    {
        var steam = Touch("Steam", "steam.exe");
        var game = Touch("Titanfall 2", "Titanfall2.exe");
        var controller = new AppController(TestRepoRoot.Find(), (_, _) => Path.GetDirectoryName(game));

        var launch = controller.LaunchTitanfall2Vanilla(dryRun: true, steamRoot: Path.GetDirectoryName(steam));

        Assert.True(launch.Ok, launch.Message);
        Assert.Equal($"{steam} -applaunch 1237970", launch.Message);
    }
}
