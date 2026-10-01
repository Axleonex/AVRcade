using VrClient.Core.ModManagers;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using Xunit;

public sealed class ModManagerIntegrationTests
{
    [Fact]
    public void EveryFriendslopModpackHasAnExplicitRoute()
    {
        Assert.Equal(new[] { "big-walk", "content-warning", "lethal-company", "peak", "repo" },
            CommunityVrRoutes.All.Keys.OrderBy(key => key).ToArray());
        Assert.Equal("1966720", CommunityVrRoutes.All["lethal-company"].AppId);
    }

    [Fact]
    public void VrOnlyRequiresNoExtraOrUntrackedPlugins()
    {
        using var f = new Fixture();
        var game = Path.Combine(f.Root, "game");
        var dll = f.FileAt("game", "BepInEx", "plugins", "DaXcess-RepoXR", "RepoXR.dll");
        var manifest = f.FileAt("game", "BepInEx", "vrclient-install-manifest.json");
        File.WriteAllText(manifest, "{\"files\":[\"BepInEx/plugins/DaXcess-RepoXR/RepoXR.dll\"]}");
        Assert.True(ManagedModIsolation.IsVrOnly(game));
        f.FileAt("game", "BepInEx", "plugins", "Other-Cosmetic", "Cosmetic.dll");
        Assert.False(ManagedModIsolation.IsVrOnly(game));
        File.WriteAllText(Path.Combine(game, "BepInEx", "vrclient-optional-mods.json"), "[]");
        Assert.False(ManagedModIsolation.IsVrOnly(game));
        Assert.True(File.Exists(dll));
    }

    [Fact]
    public void DesktopProfileLaunchNeedsNoVrModAndNoHeadset()
    {
        using var f = new Fixture();
        var manager = f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var steam = f.FileAt("steam", "steam.exe");
        var loader = f.FileAt("roaming", "r2modmanPlus-local", "REPO", "profiles", "Flat",
            "BepInEx", "core", "BepInEx.Preloader.dll");
        var directory = Path.GetDirectoryName(Path.GetDirectoryName(Path.GetDirectoryName(loader))!)!;
        var profile = new ModManagerProfile(new(ModManagerKind.R2Modman, "r2modman", manager, null),
            "repo", directory, "Flat", "REPO", directory, ["Other-Cosmetic"], false, true);
        var launcher = new ModdedVrLauncher();
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null,
            ModdedLaunchMode.DesktopWithMods, out var args));
        Assert.Contains(loader, args);
        Assert.Contains("--disable-vr", args);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile,
            $"--doorstop-enabled true --doorstop-target-assembly \"{loader}\"",
            ModdedLaunchMode.DesktopWithMods, out var customArgs));
        Assert.Contains("--disable-vr", customArgs);
        Assert.True(launcher.Launch(profile, null, Verdict.Warn, true, false, true,
            Path.GetDirectoryName(steam), ModdedLaunchMode.DesktopWithMods).GameLaunchRequested);
        Assert.False(launcher.Launch(profile with { VrModPresent = true }, null, Verdict.Warn,
            true, false, true, Path.GetDirectoryName(steam), ModdedLaunchMode.DesktopWithMods)
            .GameLaunchRequested);
    }

    [Theory]
    [InlineData("repo", "3241660")]
    [InlineData("lethal-company", "1966720")]
    public void VanillaLaunchUsesSteamAndDisablesBepInEx(string slug, string appId)
    {
        using var f = new Fixture();
        f.FileAt("steam", "steam.exe");
        var result = new ModdedVrLauncher().LaunchVanilla(slug, Verdict.Warn,
            acknowledge: true, dryRun: true, steamRoot: Path.Combine(f.Root, "steam"));
        Assert.True(result.GameLaunchRequested);
        Assert.Contains(appId, result.Message);
        Assert.False(new ModdedVrLauncher().LaunchVanilla(slug, Verdict.Warn,
            acknowledge: false, dryRun: true, steamRoot: Path.Combine(f.Root, "steam")).GameLaunchRequested);
        Assert.False(new ModdedVrLauncher().LaunchVanilla(slug, Verdict.Block,
            acknowledge: true, dryRun: true, steamRoot: Path.Combine(f.Root, "steam")).GameLaunchRequested);
        var arguments = string.Join(' ', ModdedVrLauncher.VanillaLaunchArguments);
        Assert.Contains("--doorstop-enable false", arguments);
        Assert.Contains("--doorstop-enabled false", arguments);
    }
    [Fact]
    public void RepoHasModdedVrRouteAndDiscoversExistingR2Profile()
    {
        using var f = new Fixture();
        f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var preloader = f.FileAt("roaming", "r2modmanPlus-local", "REPO", "profiles", "Friends",
            "BepInEx", "core", "BepInEx.Preloader.dll");
        f.FileAt("roaming", "r2modmanPlus-local", "REPO", "profiles", "Friends",
            "BepInEx", "plugins", "DaXcess-RepoXR", "RepoXR.dll");

        var route = CommunityVrRoutes.All["repo"];
        Assert.Equal("3241660", route.AppId);
        var profile = Assert.Single(f.Discovery().Discover("repo"));
        Assert.True(profile.VrModPresent);
        Assert.True(profile.LoaderPresent);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null, out var args));
        Assert.Contains(preloader, args);
    }

    private sealed class Fixture : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), "vrclient-managers-" + Guid.NewGuid());
        public string Roaming => Path.Combine(Root, "roaming");
        public string Local => Path.Combine(Root, "local");
        public string Programs => Path.Combine(Root, "programs");
        public string ProgramFiles => Path.Combine(Root, "program-files");
        public string ProgramFilesX86 => Path.Combine(Root, "program-files-x86");

        public string FileAt(params string[] parts)
        {
            var path = Path.Combine(new[] { Root }.Concat(parts).ToArray());
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "fixture");
            return path;
        }

        public ModManagerDiscovery Discovery(string? vortexJson = null, IEnumerable<string>? vortexPaths = null) =>
            new(Roaming, Local, ProgramFiles, ProgramFilesX86, [Programs], _ => vortexJson,
                () => vortexPaths ?? []);

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    [Fact]
    public void DataFoldersAloneDoNotCountAsInstalledManagers()
    {
        using var f = new Fixture();
        f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "Default", "BepInEx", "plugins", "Andrey04o-PeakVR", "PeakVR.dll");
        f.FileAt("roaming", "Thunderstore Mod Manager", "DataFolder", "PEAK", "profiles", "Default", "note.txt");
        Assert.Empty(f.Discovery().FindInstalled());
        Assert.Empty(f.Discovery().Discover("peak"));
    }

    [Fact]
    public void VortexIsFoundOutsideStandardFoldersAndStaleInstallRecordsAreIgnored()
    {
        using var f = new Fixture();
        var external = f.FileAt("other-drive", "Mod Tools", "Vortex.exe");
        var stale = Path.Combine(f.Root, "disconnected-drive", "Vortex.exe");
        var managers = f.Discovery(vortexPaths: [stale, external]).FindInstalled();

        var vortex = Assert.Single(managers);
        Assert.Equal(ModManagerKind.Vortex, vortex.Kind);
        Assert.Equal(external, vortex.LaunchPath);
        Assert.Empty(f.Discovery(vortexPaths: [stale]).FindInstalled());
    }

    [Fact]
    public void DetectsInstalledManagersAndTheirExistingProfilesWithoutWrites()
    {
        using var f = new Fixture();
        f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        f.FileAt("program-files", "Vortex", "Vortex.exe");
        f.FileAt("program-files-x86", "Overwolf", "OverwolfLauncher.exe");
        f.FileAt("programs", "Thunderstore Mod Manager.lnk");
        var r2Profile = f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "VR and friends", "BepInEx", "plugins", "Andrey04o-PeakVR", "PeakVR.dll");
        f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "VR and friends", "BepInEx", "plugins", "Other-Cosmetic", "Other.dll");
        f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "VR and friends", "BepInEx", "core", "BepInEx.Preloader.dll");
        f.FileAt("roaming", "Thunderstore Mod Manager", "DataFolder", "PEAK", "profiles", "Default", "BepInEx", "plugins", "Andrey04o-PeakVR", "mod.dll");
        var discovery = f.Discovery("""
            {"vr-1":{"gameId":"peak","name":"Vortex VR"},"other":{"gameId":"skyrim","name":"Other"}}
            """);

        var before = File.ReadAllText(r2Profile);
        var profiles = discovery.Discover("peak");

        Assert.Equal(3, discovery.FindInstalled().Count);
        Assert.Equal(3, profiles.Count);
        var r2 = Assert.Single(profiles.Where(p => p.Manager.Kind == ModManagerKind.R2Modman));
        Assert.True(r2.VrModPresent);
        Assert.True(r2.LoaderPresent);
        Assert.Contains("Other-Cosmetic", r2.Mods);
        Assert.Equal("VR and friends", r2.Name);
        Assert.Equal("Vortex VR", Assert.Single(profiles.Where(p => p.Manager.Kind == ModManagerKind.Vortex)).Name);
        Assert.Equal(before, File.ReadAllText(r2Profile));
    }

    [Fact]
    public void CustomDataRootAndStaleSavedProfileNeverSelectAnotherProfile()
    {
        using var f = new Fixture();
        f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        f.FileAt("custom", "PEAK", "profiles", "Current", "note.txt");
        var profiles = f.Discovery().Discover("peak", new Dictionary<ModManagerKind, string>
        {
            [ModManagerKind.R2Modman] = Path.Combine(f.Root, "custom")
        });
        Assert.Equal("Current", Assert.Single(profiles).Name);

        var settings = new ModdedVrPreferences(Path.Combine(f.Root, "vrclient", "preferences.json"));
        settings.SetDataRoot(ModManagerKind.R2Modman, Path.Combine(f.Root, "custom"));
        Assert.Equal(Path.Combine(f.Root, "custom"),
            new ModdedVrPreferences(settings.PathOnDisk).LoadDataRoots()[ModManagerKind.R2Modman]);
        settings.Select("peak", "R2Modman:missing");
        Assert.DoesNotContain(profiles, p => p.Key == settings.ForGame("peak")!.ProfileKey);
        settings.Select("peak", profiles[0].Key);
        settings.SetR2LaunchArguments("peak", profiles[0].Key, "--example");
        Assert.Equal("--example", new ModdedVrPreferences(settings.PathOnDisk).ForGame("peak")!.R2LaunchArguments);
        settings.Select("peak", "R2Modman:missing");
        Assert.Null(settings.ForGame("peak")!.R2LaunchArguments);
    }

    [Fact]
    public void R2ArgumentsMustBindToTheChosenProfilePreloader()
    {
        using var f = new Fixture();
        var exe = f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var preloader = f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "VR and friends", "BepInEx", "core", "BepInEx.Preloader.dll");
        var profileDir = Path.GetDirectoryName(Path.GetDirectoryName(Path.GetDirectoryName(preloader))!)!;
        var profile = new ModManagerProfile(new(ModManagerKind.R2Modman, "r2modman", exe, null),
            "peak", profileDir, "VR and friends", "PEAK", profileDir,
            ["Andrey04o-PeakVR"], true, true);
        var valid = $"--doorstop-enable true --doorstop-target \"{preloader}\" --r2profile \"VR and friends\"";

        Assert.True(ModdedVrLauncher.TryParseR2Arguments(profile, valid, out var tokens, out _));
        Assert.Contains(preloader, tokens);
        Assert.True(ModdedVrLauncher.TryParseR2Arguments(profile,
            $"--doorstop-enable true --doorstop-target \"{preloader}\"", out _, out _));
        Assert.False(ModdedVrLauncher.TryParseR2Arguments(profile,
            valid.Replace("VR and friends\"", "Another\""), out _, out _));
        Assert.False(ModdedVrLauncher.TryParseR2Arguments(profile,
            valid.Replace("--doorstop-enable true", "--doorstop-enable false"), out _, out _));
        Assert.False(ModdedVrLauncher.TryParseR2Arguments(profile,
            "cmd /c " + valid, out _, out _));
        Assert.False(ModdedVrLauncher.TryParseR2Arguments(profile,
            valid + " %command%", out _, out _));
        Assert.False(ModdedVrLauncher.TryParseR2Arguments(profile,
            valid + " --doorstop-enable false", out _, out _));
    }

    [Fact]
    public void LaunchDistinguishesSteamRequestFromManagerHandoff()
    {
        using var f = new Fixture();
        var r2Exe = f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var steam = f.FileAt("steam", "steam.exe");
        var preloader = f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "Default", "BepInEx", "core", "BepInEx.Preloader.dll");
        var cosmetic = f.FileAt("roaming", "r2modmanPlus-local", "PEAK", "profiles", "Default", "BepInEx", "plugins", "Other-Cosmetic", "Other.dll");
        var profileDir = Path.GetDirectoryName(Path.GetDirectoryName(Path.GetDirectoryName(preloader))!)!;
        var r2 = new ModManagerProfile(new(ModManagerKind.R2Modman, "r2modman", r2Exe, null),
            "peak", profileDir, "Default", "PEAK", profileDir,
            ["Andrey04o-PeakVR", "Other-Cosmetic"], true, true);
        var args = $"--doorstop-enable true --doorstop-target \"{preloader}\" --r2profile Default";
        var launcher = new ModdedVrLauncher();

        Assert.False(launcher.Launch(r2, args, Verdict.Warn, false, true, true,
            Path.GetDirectoryName(steam)).GameLaunchRequested);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(r2, null, out var generated));
        Assert.Contains(preloader, generated);
        var automaticLaunch = launcher.Launch(r2, null, Verdict.Warn, true, true,
            dryRun: true, steamRoot: Path.GetDirectoryName(steam));
        Assert.True(automaticLaunch.GameLaunchRequested);
        Assert.False(automaticLaunch.ManagerOpened);
        Assert.True(File.Exists(cosmetic)); // wrapper does not alter the manager's other mods
        Assert.False(ModdedVrLauncher.TryResolveProfileArguments(r2,
            "--doorstop-enabled false", out _));
        var setupWithoutHeadset = launcher.Launch(r2 with { VrModPresent = false }, null,
            Verdict.Warn, false, false, dryRun: true);
        Assert.True(setupWithoutHeadset.ManagerOpened);
        Assert.Contains("Add Andrey04o-PeakVR", setupWithoutHeadset.Message);
        Assert.False(launcher.Launch(r2, args, Verdict.Warn, true, false, true,
            Path.GetDirectoryName(steam)).GameLaunchRequested);
        Assert.True(launcher.Launch(r2, args, Verdict.Warn, true, true, true,
            Path.GetDirectoryName(steam)).GameLaunchRequested);
        var vortex = r2 with
        {
            Manager = new(ModManagerKind.Vortex, "Vortex", f.FileAt("program-files", "Vortex", "Vortex.exe"), null),
            Id = "profile-id", Directory = null, VrModPresent = null, LoaderPresent = null
        };
        var handoff = launcher.Launch(vortex, null, Verdict.Warn, false, false, dryRun: true);
        Assert.False(handoff.GameLaunchRequested);
        Assert.True(handoff.ManagerOpened);
        Assert.Contains("not launched", handoff.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(launcher.Launch(vortex, null, Verdict.Block, true, true, dryRun: true)
            .ManagerOpened);
    }

    [Fact]
    public void Il2CppLoaderIsRecognizedForBigWalkWithoutInstallingAnotherLoader()
    {
        using var f = new Fixture();
        f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var target = f.FileAt("roaming", "r2modmanPlus-local", "BigWalk", "profiles", "VR",
            "BepInEx", "core", "BepInEx.Unity.IL2CPP.dll");
        f.FileAt("roaming", "r2modmanPlus-local", "BigWalk", "profiles", "VR",
            "BepInEx", "plugins", "CircuitLord-Big_Walk_VR", "BigWalkVR.dll");
        var profile = Assert.Single(f.Discovery().Discover("big-walk"));

        Assert.True(profile.LoaderPresent);
        Assert.True(profile.VrModPresent);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null, out var generated));
        Assert.Contains(target, generated);
        Assert.True(ModdedVrLauncher.TryParseR2Arguments(profile,
            $"--doorstop-enabled true --doorstop-target-assembly \"{target}\" --r2profile VR",
            out _, out _));
    }

    [Fact]
    public void ThunderstoreProfileCanRequestSteamLaunchWithoutOpeningManager()
    {
        using var f = new Fixture();
        f.FileAt("local", "Overwolf", "OverwolfLauncher.exe");
        f.FileAt("programs", "Thunderstore Mod Manager.lnk");
        var steam = f.FileAt("steam", "steam.exe");
        var preloader = f.FileAt("roaming", "Thunderstore Mod Manager", "DataFolder",
            "PEAK", "profiles", "Friends", "BepInEx", "core", "BepInEx.Preloader.dll");
        f.FileAt("roaming", "Thunderstore Mod Manager", "DataFolder",
            "PEAK", "profiles", "Friends", "BepInEx", "plugins", "Andrey04o-PeakVR", "PeakVR.dll");
        var profile = Assert.Single(f.Discovery().Discover("peak"));
        Assert.Equal(ModManagerKind.Thunderstore, profile.Manager.Kind);
        Assert.True(ModdedVrLauncher.TryResolveProfileArguments(profile, null, out var args));
        Assert.Contains(preloader, args);
        var result = new ModdedVrLauncher().Launch(profile, null, Verdict.Warn,
            acknowledge: true, headsetConnected: true, dryRun: true,
            steamRoot: Path.GetDirectoryName(steam));
        Assert.True(result.GameLaunchRequested);
        Assert.False(result.ManagerOpened);
    }

    [Fact]
    public void ManagerModInventoryRespectsDisabledModsAndPackagesOutsidePluginFolders()
    {
        using var f = new Fixture();
        f.FileAt("local", "Programs", "r2modman", "r2modman.exe");
        var list = f.FileAt("roaming", "r2modmanPlus-local", "ContentWarning", "profiles", "Friends", "mods.yml");
        File.WriteAllText(list, """
            - manifestVersion: 1
              name: DaXcess-CWVR
              enabled: false
            - manifestVersion: 1
              name: BepInEx-BepInExPack
              enabled: true
            - manifestVersion: 1
              name: Existing-Cosmetic
              enabled: true
            """);
        var before = File.ReadAllText(list);
        var profile = Assert.Single(f.Discovery().Discover("content-warning"));
        Assert.False(profile.VrModPresent);
        Assert.Contains("Existing-Cosmetic", profile.Mods);
        Assert.DoesNotContain("DaXcess-CWVR", profile.Mods);
        Assert.Equal(before, File.ReadAllText(list));
    }
}
