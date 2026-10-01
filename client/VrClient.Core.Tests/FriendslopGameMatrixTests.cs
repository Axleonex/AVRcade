using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Launch;
using VrClient.Core.ModManagers;
using VrClient.Core.Model;
using VrClient.Core.Modpack;

namespace VrClient.Core.Tests;

/// The same checks for each of the five community-converted Unity games, so a
/// game that is not installed on the build machine is still exercised end to end
/// up to (not including) starting the process.
///
/// The folder, executable and Steam id columns are r2modman's own definitions of
/// these games (Thunderstore ecosystem schema, read 2026-09-30). AVRcade finds a
/// manager profile by that folder name and starts the game by that Steam id, so
/// its catalogue must agree with them exactly.
public sealed class FriendslopGameMatrixTests : TempTree
{
    public static TheoryData<string, string, string, string, string, string> Games => new()
    {
        { "repo", "REPO", "REPO.exe", "3241660", "BepInEx.Preloader.dll", "DaXcess-RepoXR" },
        { "lethal-company", "LethalCompany", "Lethal Company.exe", "1966720", "BepInEx.Preloader.dll", "DaXcess-LethalCompanyVR" },
        { "peak", "PEAK", "PEAK.exe", "3527290", "BepInEx.Preloader.dll", "Andrey04o-PeakVR" },
        { "content-warning", "ContentWarning", "Content Warning.exe", "2881650", "BepInEx.Preloader.dll", "DaXcess-CWVR" },
        { "big-walk", "BigWalk", "Big Walk.exe", "1478500", "BepInEx.Unity.IL2CPP.dll", "CircuitLord-Big_Walk_VR" }
    };

    private readonly string _repo = TestRepoRoot.Find();

    private ModManagerDiscovery Discovery() => new(
        Path.Combine(Root, "roaming"), Path.Combine(Root, "local"),
        Path.Combine(Root, "program-files"), Path.Combine(Root, "program-files-x86"),
        [Path.Combine(Root, "programs")], _ => null, () => []);

    /// A profile laid out the way r2modman (or Thunderstore Mod Manager) writes one.
    private ModManagerProfile ManagerProfile(string slug, string folder, string loader, string vrPackage,
        string name, bool vrModEnabled, bool thunderstoreManager = false)
    {
        string[] dataRoot;
        if (thunderstoreManager)
        {
            Touch("local", "Overwolf", "OverwolfLauncher.exe");
            Touch("programs", "Thunderstore Mod Manager.lnk");
            dataRoot = ["roaming", "Thunderstore Mod Manager", "DataFolder"];
        }
        else
        {
            Touch("local", "Programs", "r2modman", "r2modman.exe");
            dataRoot = ["roaming", "r2modmanPlus-local"];
        }
        string[] profile = [.. dataRoot, folder, "profiles", name];
        Touch([.. profile, "BepInEx", "core", loader]);
        Touch([.. profile, "BepInEx", "plugins", "Someone-ExtraMod", "ExtraMod.dll"]);
        File.WriteAllText(Touch([.. profile, "mods.yml"]), $"""
            - manifestVersion: 1
              name: BepInEx-BepInExPack
              enabled: true
            - manifestVersion: 1
              name: {vrPackage}
              enabled: {(vrModEnabled ? "true" : "false")}
            - manifestVersion: 1
              name: Someone-ExtraMod
              enabled: true

            """.ReplaceLineEndings("\n"));
        return Discovery().Discover(slug).Single(found => found.Name == name);
    }

    [Theory, MemberData(nameof(Games))]
    public void CatalogueAgreesWithTheModManagersDefinitionOfTheGame(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        _ = loader;
        var route = CommunityVrRoutes.All[slug];
        Assert.Equal(folder, route.GameFolder);
        Assert.Equal(appId, route.AppId);
        Assert.Equal(vrPackage, route.PackageKey);

        var modpack = ModpackResolver.LoadSpec(Path.Combine(_repo, "config", "modpacks", $"{slug}.modpack.json"));
        Assert.Equal(appId, modpack.SteamAppId);
        Assert.Contains(modpack.Mods, mod => $"{mod.Namespace}-{mod.Name}" == vrPackage);

        using var game = JsonDocument.Parse(File.ReadAllText(Path.Combine(_repo, "config", "games", $"{slug}.json")));
        Assert.Contains(exe, game.RootElement.GetProperty("executable_names").EnumerateArray().Select(e => e.GetString()));

        // "Install VR mod" needs every package pinned with a hash, including a mod loader.
        var packages = LockfileIo.Read(Path.Combine(_repo, "config", "modpacks", $"{slug}.lock.json")).Packages;
        LockfileIo.RequireHashesFilled(LockfileIo.Read(Path.Combine(_repo, "config", "modpacks", $"{slug}.lock.json")));
        Assert.Contains(packages, package => $"{package.Namespace}-{package.Name}" == vrPackage);
        Assert.Contains(packages, package => package.Name.StartsWith("BepInExPack", StringComparison.Ordinal));
    }

    [Theory, MemberData(nameof(Games))]
    public void VrWithMods_StartsTheVrProfileExactlyAsTheManagerWould(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        var steam = Touch("steam", "steam.exe");
        var profile = ManagerProfile(slug, folder, loader, vrPackage, "VR friends", vrModEnabled: true);

        Assert.True(profile.VrModPresent);
        Assert.True(profile.LoaderPresent);
        Assert.Contains("Someone-ExtraMod", profile.Mods);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(
            profile, null, ModdedLaunchMode.VrWithMods, out var arguments));
        var loaderPath = Path.Combine(profile.Directory!, "BepInEx", "core", loader);
        // r2modman's own arguments, plus AVRcade's "this launch is VR" marker for its Steam wrapper.
        Assert.Equal(["--doorstop-enabled", "true", "--doorstop-target-assembly", loaderPath,
            WrapPlanner.ForceVrArg], arguments);

        var command = new SteamOwnedLauncher().Launch(appId, Path.GetDirectoryName(steam), dryRun: true,
            launchArguments: arguments);
        Assert.Equal($"{steam} -applaunch {appId} --doorstop-enabled true --doorstop-target-assembly {loaderPath} " +
            WrapPlanner.ForceVrArg, command.Detail);

        // Through the wrapper the game gets exactly the manager's arguments, in VR, with or
        // without a detected runtime.
        var wrapped = WrapPlanner.Plan([Path.Combine(Root, "game", exe), .. arguments], WrapVrMode.Auto, [], []);
        Assert.False(wrapped.FlatMode);
        Assert.Equal(["--doorstop-enabled", "true", "--doorstop-target-assembly", loaderPath],
            wrapped.Command.Skip(1));
        Assert.Equal(profile.Directory, DoorstopProfile.ProfileRootFromCommand(wrapped.Command));

        // A VR profile is never started as the monitor mode.
        Assert.False(ModdedVrLauncher.TryResolveProfileArguments(
            profile, null, ModdedLaunchMode.DesktopWithMods, out _));
    }

    [Theory, MemberData(nameof(Games))]
    public void VrWithMods_WorksFromThunderstoreModManagerToo(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        _ = (exe, appId);
        var profile = ManagerProfile(slug, folder, loader, vrPackage, "VR", vrModEnabled: true,
            thunderstoreManager: true);

        Assert.Equal(ModManagerKind.Thunderstore, profile.Manager.Kind);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null, out var arguments));
        Assert.Contains(Path.Combine(profile.Directory!, "BepInEx", "core", loader), arguments);
    }

    [Theory, MemberData(nameof(Games))]
    public void ModsNoVr_StartsTheFlatProfileWithVrSwitchedOff(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        var steam = Touch("steam", "steam.exe");
        var game = Path.GetDirectoryName(Touch("game", "winhttp.dll"))!;
        Touch("game", "dotnet", "coreclr.dll");
        var profile = ManagerProfile(slug, folder, loader, vrPackage, "Flat", vrModEnabled: false);

        Assert.False(profile.VrModPresent);
        Assert.DoesNotContain(vrPackage, profile.Mods);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(
            profile, null, ModdedLaunchMode.DesktopWithMods, out var arguments));
        Assert.Equal(WrapPlanner.DisableVrArg, arguments[^1]);
        Assert.DoesNotContain(WrapPlanner.ForceVrArg, arguments);
        Assert.Contains(Path.Combine(profile.Directory!, "BepInEx", "core", loader), arguments);
        Assert.Contains(WrapPlanner.DisableVrArg, WrapPlanner.Plan(
            [Path.Combine(game, exe), .. arguments], WrapVrMode.Auto, ["vrserver"], []).Command);

        var launch = new ModdedVrLauncher().Launch(profile, null, Verdict.Warn, acknowledge: true,
            headsetConnected: false, dryRun: true, steamRoot: Path.GetDirectoryName(steam),
            mode: ModdedLaunchMode.DesktopWithMods, gameDirectory: game);
        Assert.True(launch.GameLaunchRequested, launch.Message);
        Assert.Contains(appId, new SteamOwnedLauncher().Launch(appId, Path.GetDirectoryName(steam), dryRun: true).Detail);
        // Without the player's acknowledgement a modded launch never goes out.
        Assert.False(new ModdedVrLauncher().Launch(profile, null, Verdict.Warn, acknowledge: false,
            headsetConnected: false, dryRun: true, steamRoot: Path.GetDirectoryName(steam),
            mode: ModdedLaunchMode.DesktopWithMods, gameDirectory: game).GameLaunchRequested);
    }

    [Theory, MemberData(nameof(Games))]
    public void ProfileLaunch_NeedsTheManagersLoaderFilesBesideTheGame(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        _ = (exe, appId);
        var profile = ManagerProfile(slug, folder, loader, vrPackage, "VR", vrModEnabled: true);
        var game = Path.Combine(Root, "game");
        Directory.CreateDirectory(game);

        Assert.False(ModdedVrLauncher.IsLoaderLinked(game, profile));
        Touch("game", "winhttp.dll");
        var needsDotNet = DoorstopProfile.NeedsDotNetRuntime(loader);
        // Mono games are ready with the proxy alone; an IL2CPP game also needs its .NET runtime.
        Assert.Equal(!needsDotNet, ModdedVrLauncher.IsLoaderLinked(game, profile));
        Touch("game", "dotnet", "coreclr.dll");
        Assert.True(ModdedVrLauncher.IsLoaderLinked(game, profile));
    }

    [Theory, MemberData(nameof(Games))]
    public void Vanilla_GoesThroughSteamWithTheModLoaderOff(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        _ = (folder, exe, loader, vrPackage);
        var steamRoot = Path.GetDirectoryName(Touch("steam", "steam.exe"));

        var launch = new ModdedVrLauncher().LaunchVanilla(slug, Verdict.Warn, acknowledge: true,
            dryRun: true, steamRoot: steamRoot);
        Assert.True(launch.GameLaunchRequested, launch.Message);
        Assert.Contains(appId, launch.Message);
    }

    [Theory, MemberData(nameof(Games))]
    public void VrOnly_PlansTheGamesOwnExecutableAndRefusesUntilTheVrModIsInstalled(
        string slug, string folder, string exe, string appId, string loader, string vrPackage)
    {
        _ = (folder, appId, loader);
        var game = Path.Combine(Root, "game");
        Touch("game", exe);
        var controller = new AppController(_repo, (_, _) => game, headsetConnected: () => false,
            gtaSanAndreasLocator: () => null);

        Assert.Contains("mod_not_installed", controller.LaunchUnityVr(slug, game, acknowledge: true, dryRun: true).Message);

        // The files "Install VR mod" leaves behind: the loader proxy, the VR plugin, the manifest.
        var plugin = $"BepInEx/plugins/{vrPackage}/Vr.dll";
        Touch("game", "winhttp.dll");
        Touch("game", "BepInEx", "plugins", vrPackage, "Vr.dll");
        File.WriteAllText(Touch("game", "BepInEx", "vrclient-install-manifest.json"),
            JsonSerializer.Serialize(new { files = new[] { "winhttp.dll", plugin } }));

        var view = controller.ListGames().Single(listed => listed.Slug == slug);
        Assert.Equal(game, view.InstallDir);
        Assert.True(view.ModInstalled);
        Assert.Equal("Warn", view.SafetyVerdict);
        Assert.True(ManagedModIsolation.IsVrOnly(game));
        Assert.True(controller.LaunchUnityVr(slug, game, acknowledge: true, dryRun: true).Ok);
        Assert.Contains("warn_not_acknowledged",
            controller.LaunchUnityVr(slug, game, acknowledge: false, dryRun: true).Message);
        var plan = new GameLauncher().Plan(game, controller.EvaluateSafety(slug), modInstalled: true, exe);
        Assert.Equal(Path.Combine(game, exe), plan.GameExePath);

        // Another mod dropped into the game folder turns "VR only" off.
        Touch("game", "BepInEx", "plugins", "Someone-ExtraMod", "ExtraMod.dll");
        Assert.False(ManagedModIsolation.IsVrOnly(game));
    }

    [Fact]
    public void LoaderLookupFollowsTheManagersOrderAndCarriesItsCorlibOverride()
    {
        var profile = ManagerProfile("repo", "REPO", "BepInEx.Preloader.dll", "DaXcess-RepoXR", "VR", vrModEnabled: true);
        var newer = Path.Combine(profile.Directory!, "BepInEx", "core", "BepInEx.Unity.Mono.Preloader.dll");
        File.WriteAllText(newer, "fixture");
        Directory.CreateDirectory(Path.Combine(profile.Directory!, "unstripped_corlib"));

        Assert.Equal(newer, DoorstopProfile.FindLoader(profile.Directory!));
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null, out var arguments));
        Assert.Equal(["--doorstop-enabled", "true", "--doorstop-target-assembly", newer,
            "--doorstop-mono-dll-search-path-override", Path.Combine(profile.Directory!, "unstripped_corlib"),
            WrapPlanner.ForceVrArg],
            arguments);
        Assert.Null(DoorstopProfile.FindLoader(Path.Combine(Root, "no-such-profile")));
    }
}
