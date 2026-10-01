using VrClient.Core.App;
using VrClient.Core.Launch;
using VrClient.Core.ModManagers;
using VrClient.Core.Model;
using VrClient.Core.Unreal;

namespace VrClient.Core.Tests;

/// The pieces behind the four play modes that touch files or build launch commands.
public sealed class FourModeLaunchTests : TempTree
{

    [Fact]
    public void RuntimePin_RewritesTheModsOwnKeyAndClearsItForSystemDefault()
    {
        var cfg = Touch("BepInEx", "config", "io.daxcess.repoxr.cfg");
        File.WriteAllText(cfg, "[Internal]\n\nOpenXRRuntimeFile = old.json\n\n[Input]\n\nSnapTurnSize = 45\n");

        Assert.True(ModRuntimePin.Apply(cfg, @"C:\vd\virtualdesktop-openxr.json"));
        Assert.Contains(@"OpenXRRuntimeFile = C:\vd\virtualdesktop-openxr.json", File.ReadAllText(cfg));
        Assert.Contains("SnapTurnSize = 45", File.ReadAllText(cfg));

        Assert.True(ModRuntimePin.Apply(cfg, null));
        Assert.Contains("OpenXRRuntimeFile = \n", File.ReadAllText(cfg).ReplaceLineEndings("\n"));
    }

    [Fact]
    public void RuntimePin_NeverInventsTheKeyInAnotherAuthorsCfg()
    {
        var cfg = Touch("BepInEx", "config", "com.andrey04o.PeakVR.cfg");
        File.WriteAllText(cfg, "[General]\n\nEnabled = true\n");

        Assert.False(ModRuntimePin.Apply(cfg, @"C:\steamvr\steamxr_win64.json"));
        Assert.DoesNotContain("OpenXRRuntimeFile", File.ReadAllText(cfg));
        Assert.False(ModRuntimePin.Apply(Path.Combine(Root, "missing.cfg"), null));
    }

    [Fact]
    public void UnrealMods_FindsPakModsAndScriptLoadersButNotTheGamesOwnPaks()
    {
        const string shipping = "Ride/Binaries/Win64/Ride-Win64-Shipping.exe";
        Touch("Ride", "Content", "Paks", "Ride-Windows.pak");
        Touch("Ride", "Binaries", "Win64", "Ride-Win64-Shipping.exe");
        Assert.False(UnrealMods.Scan(Root, shipping).HasMods);

        Touch("Ride", "Content", "Paks", "~mods", "BetterHorn_P.pak");
        Touch("Ride", "Content", "Paks", "LogicMods", "Speedo.pak");
        Touch("Ride", "Content", "Paks", "~mods", "readme.txt");
        Touch("Ride", "Binaries", "Win64", "dwmapi.dll");
        var mods = UnrealMods.Scan(Root, shipping);

        Assert.Equal(3, mods.Files.Count);
        Assert.EndsWith("~mods", mods.ModsFolder);
        Assert.DoesNotContain(mods.Files, file => file.EndsWith("readme.txt", StringComparison.Ordinal));
    }

    [Fact]
    public void GameModManager_RemembersOneApplicationPerGame()
    {
        var settings = new GameModManagerSettings(Path.Combine(Root, "managers.json"));
        var vortex = Touch("Vortex", "Vortex.exe");
        var other = Touch("Tools", "ModManager.exe");

        Assert.Null(settings.Load("cyberpunk-2077"));
        settings.Save("cyberpunk-2077", vortex);
        settings.Save("rv-there-yet", other);

        Assert.Equal(vortex, settings.Load("cyberpunk-2077"));
        Assert.Equal(other, settings.Load("rv-there-yet"));
        Assert.Throws<InvalidOperationException>(() => settings.Save("rv-there-yet", Touch("notes.txt")));
        File.Delete(other);
        Assert.Null(settings.Load("rv-there-yet"));
    }

    [Fact]
    public void Preferences_KeepTheVrAndFlatProfilesApart()
    {
        var preferences = new ModdedVrPreferences(Path.Combine(Root, "prefs.json"));

        preferences.Select("repo", "R2Modman:vr");
        preferences.SelectFlat("repo", "R2Modman:flat");
        Assert.Equal("R2Modman:vr", preferences.ForGame("repo")!.ProfileKey);
        Assert.Equal("R2Modman:flat", preferences.FlatProfileKey("repo"));

        preferences.UseManaged("repo");
        Assert.Null(preferences.ForGame("repo"));
        Assert.Equal("R2Modman:flat", preferences.FlatProfileKey("repo"));
        preferences.SelectFlat("repo", null);
        Assert.Null(preferences.FlatProfileKey("repo"));
    }

    private ModManagerProfile R2Profile(bool withVrMod)
    {
        var manager = new ModManagerInstall(ModManagerKind.R2Modman, "r2modman",
            Touch("r2modman", "r2modman.exe"), Path.Combine(Root, "data"));
        var profile = Path.Combine(Root, "data", "REPO", "profiles", withVrMod ? "VR" : "Flat");
        Touch("data", "REPO", "profiles", withVrMod ? "VR" : "Flat", "BepInEx", "core", "BepInEx.Preloader.dll");
        IReadOnlyList<string> mods = withVrMod
            ? ["BepInEx-BepInExPack", "DaXcess-RepoXR", "Zehs-MoreUpgrades"]
            : ["BepInEx-BepInExPack", "Zehs-MoreUpgrades"];
        return new ModManagerProfile(manager, "repo", profile, Path.GetFileName(profile), "REPO",
            profile, mods, withVrMod, true);
    }

    [Fact]
    public void ProfileLaunch_IsRefusedUntilTheManagerLinkedItsLoaderIntoTheGame()
    {
        var steam = Touch("Steam", "steam.exe");
        var game = Path.Combine(Root, "game");
        Directory.CreateDirectory(game);
        var launcher = new ModdedVrLauncher();

        var refused = launcher.Launch(R2Profile(withVrMod: true), null, Verdict.Warn, acknowledge: true,
            headsetConnected: true, dryRun: true, steamRoot: Path.GetDirectoryName(steam), gameDirectory: game);
        Assert.False(refused.GameLaunchRequested);
        Assert.Contains("Start modded", refused.Message);

        Touch("game", "winhttp.dll");
        var accepted = launcher.Launch(R2Profile(withVrMod: true), null, Verdict.Warn, acknowledge: true,
            headsetConnected: true, dryRun: true, steamRoot: Path.GetDirectoryName(steam), gameDirectory: game);
        Assert.True(accepted.GameLaunchRequested);
    }

    [Fact]
    public void FlatProfileLaunch_PointsDoorstopAtTheProfileAndSwitchesVrOff()
    {
        var flat = R2Profile(withVrMod: false);

        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(
            flat, null, ModdedLaunchMode.DesktopWithMods, out var arguments));
        Assert.Equal(["--doorstop-enabled", "true", "--doorstop-target-assembly",
            Path.Combine(flat.Directory!, "BepInEx", "core", "BepInEx.Preloader.dll"),
            WrapPlanner.DisableVrArg], arguments);
        // The flat profile can never be started as the VR mode, nor the VR profile as flat.
        Assert.False(ModdedVrLauncher.TryResolveProfileArguments(
            flat, null, ModdedLaunchMode.VrWithMods, out _));
        Assert.False(ModdedVrLauncher.TryResolveProfileArguments(
            R2Profile(withVrMod: true), null, ModdedLaunchMode.DesktopWithMods, out _));
    }

    [Fact]
    public void ManagerThatHidesItsModList_IsOpenedForEitherModdedMode()
    {
        // The play-mode rules offer "Open Vortex" for such a profile, so the launcher
        // must hand over to the manager rather than refuse the monitor mode.
        var vortex = new ModManagerProfile(
            new ModManagerInstall(ModManagerKind.Vortex, "Vortex", Touch("Vortex", "Vortex.exe"), null),
            "repo", "profile-id", "Default", "repo", null, [], null, null);
        var launcher = new ModdedVrLauncher();

        foreach (var mode in new[] { ModdedLaunchMode.VrWithMods, ModdedLaunchMode.DesktopWithMods })
        {
            var result = launcher.Launch(vortex, null, Verdict.Warn, acknowledge: false,
                headsetConnected: false, dryRun: true, mode: mode);
            Assert.True(result.ManagerOpened, result.Message);
            Assert.False(result.GameLaunchRequested);
        }
    }

    [Fact]
    public void Vanilla_DisablesBothDoorstopGenerationsAndTheVrMod()
    {
        Assert.Equal(["--doorstop-enable", "false", "--doorstop-enabled", "false", "--disable-vr"],
            ModdedVrLauncher.VanillaLaunchArguments);
    }

    [Fact]
    public void MonitorLaunches_GoThroughSteamWithoutVrInjection()
    {
        var steamRoot = Path.GetDirectoryName(Touch("Steam", "steam.exe"))!;
        var controller = new AppController(TestRepoRoot.Find(), (_, _) => null);

        var unreal = controller.LaunchUnrealFlat("rv-there-yet", dryRun: true, steamRoot);
        Assert.True(unreal.Ok, unreal.Message);
        Assert.False(controller.LaunchUnrealFlat("not-a-game", dryRun: true, steamRoot).Ok);

        Assert.True(controller.LaunchCyberpunkFlatViaSteam(withMods: true, dryRun: true, steamRoot).Ok);
        Assert.True(controller.LaunchCyberpunkFlatViaSteam(withMods: false, dryRun: true, steamRoot).Ok);
    }

    [Fact]
    public void EveryDisplayedGameHasAPlayModeRouteAndDeferredGamesStayHidden()
    {
        var games = new AppController(TestRepoRoot.Find(), (_, _) => null,
            headsetConnected: () => false, gtaSanAndreasLocator: () => null).ListGames();

        Assert.Equal(
            ["big-walk", "content-warning", "cyberpunk-2077", "gta-san-andreas",
             "lethal-company", "peak", "repo", "rv-there-yet"],
            games.Select(game => game.Slug).Order(StringComparer.Ordinal));
        Assert.All(games, game => Assert.Contains(game.Engine,
            new[] { GameEngine.Unity, GameEngine.Unreal, GameEngine.Redengine, GameEngine.LegacyD3D9 }));
        // The five Unity games must each have the community route "VR + mods" relies on.
        Assert.All(games.Where(game => game.Engine is GameEngine.Unity),
            game => Assert.True(CommunityVrRoutes.All.ContainsKey(game.Slug), game.Slug));
    }
}
