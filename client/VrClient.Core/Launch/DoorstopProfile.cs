namespace VrClient.Core.Launch;

/// LI-3: mod-manager coexistence. Mod managers (r2modman, Thunderstore Mod
/// Manager, Gale) launch through Steam with Unity Doorstop args that redirect
/// BepInEx to a profile folder — when they do, the runtime pin must be written
/// into THAT profile's mod cfg, because the game-dir BepInEx (and its cfg)
/// never loads. Parsing is data-in-the-args: no mod-manager install paths are
/// hardcoded.
public static class DoorstopProfile
{
    /// The BepInEx entry assemblies a profile may carry, in the order r2modman
    /// itself looks for them (GameInstructionParser.bepInExPreloaderPathResolver).
    public static readonly IReadOnlyList<string> LoaderAssemblies =
    [
        "BepInEx.Unity.Mono.Preloader.dll",
        "BepInEx.Unity.IL2CPP.dll",
        "BepInEx.Preloader.dll",
        "BepInEx.IL2CPP.dll",
        "BepInEx.NET.CoreCLR.dll"
    ];

    /// The loader assembly Doorstop must be pointed at for this profile, or null.
    public static string? FindLoader(string profileRoot)
    {
        var core = Path.Combine(profileRoot, "BepInEx", "core");
        return LoaderAssemblies.Select(name => Path.Combine(core, name)).FirstOrDefault(File.Exists);
    }

    public static bool IsLoaderAssembly(string path) =>
        LoaderAssemblies.Contains(Path.GetFileName(path), StringComparer.OrdinalIgnoreCase);

    /// IL2CPP and CoreCLR loaders run on a bundled .NET runtime that Doorstop
    /// finds through a path relative to the game folder.
    public static bool NeedsDotNetRuntime(string loaderPath)
    {
        var name = Path.GetFileName(loaderPath);
        return name.Contains("IL2CPP", StringComparison.OrdinalIgnoreCase) ||
               name.Contains("CoreCLR", StringComparison.OrdinalIgnoreCase);
    }

    /// r2modman-family managers copy the profile's Doorstop proxy beside the game
    /// when they start it; an IL2CPP game (Big Walk) also gets the loader's .NET
    /// runtime the same way. Without them Steam starts the unmodded game and
    /// silently ignores the Doorstop arguments.
    public static bool IsLinkedIntoGame(string? gameDirectory, string profileRoot)
    {
        if (gameDirectory is null || !File.Exists(Path.Combine(gameDirectory, "winhttp.dll")))
            return false;
        return FindLoader(profileRoot) is not { } loader || !NeedsDotNetRuntime(loader) ||
               File.Exists(Path.Combine(gameDirectory, "dotnet", "coreclr.dll"));
    }

    public static bool IsExplicitlyDisabled(IReadOnlyList<string> command)
    {
        for (var i = 0; i + 1 < command.Count; i++)
            if ((command[i].Equals("--doorstop-enabled", StringComparison.OrdinalIgnoreCase) ||
                 command[i].Equals("--doorstop-enable", StringComparison.OrdinalIgnoreCase)) &&
                command[i + 1].Equals("false", StringComparison.OrdinalIgnoreCase))
                return true;
        return false;
    }

    /// The profile root the doorstop args redirect to (the folder containing
    /// `BepInEx/`), or null when the command has no usable doorstop redirect.
    /// Handles Doorstop 4 (`--doorstop-enabled`, `--doorstop-target-assembly`)
    /// and Doorstop 3 (`--doorstop-enable`, `--doorstop-target`) arg styles;
    /// an explicit `--doorstop-enable(d) false` means "launch unmodded" and
    /// yields null.
    public static string? ProfileRootFromCommand(IReadOnlyList<string> command)
    {
        if (IsExplicitlyDisabled(command)) return null;
        string? target = null;
        for (var i = 0; i < command.Count; i++)
        {
            var arg = command[i];
            var hasValue = i + 1 < command.Count;
            if ((arg.Equals("--doorstop-target-assembly", StringComparison.OrdinalIgnoreCase) ||
                 arg.Equals("--doorstop-target", StringComparison.OrdinalIgnoreCase)) && hasValue)
                target ??= command[i + 1];
        }
        if (target is null)
            return null;

        // target = <profile>\BepInEx\core\BepInEx.Preloader.dll -> profile root
        var core = Path.GetDirectoryName(target);
        var bepInExRoot = core is null ? null : Path.GetDirectoryName(core);
        return bepInExRoot is null ? null : Path.GetDirectoryName(bepInExRoot);
    }

    /// Minimal mod cfg content carrying only the runtime pin — used when the
    /// profile's cfg does not exist yet (BepInEx creates the real file on first
    /// run and merges orphaned entries like this one).
    public static string MinimalPinCfg(string runtimeJsonPath) =>
        $"[Internal]{Environment.NewLine}{Environment.NewLine}OpenXRRuntimeFile = {runtimeJsonPath}{Environment.NewLine}";

    /// Opt-in gate: VRClient may only write into a profile whose owner chose to
    /// install the VR mod there (via the mod manager). True when the profile's
    /// BepInEx/plugins contains a folder named like one of the modpack's mods
    /// (mod managers install as "<Namespace>-<Name>"). A profile without the VR
    /// mod is NEVER touched.
    public static bool HasVrMod(string profileRoot, IEnumerable<string> modFolderNames)
    {
        var plugins = Path.Combine(profileRoot, "BepInEx", "plugins");
        if (!Directory.Exists(plugins))
            return false;
        var installed = Directory.EnumerateDirectories(plugins)
            .Select(Path.GetFileName)
            .Where(n => n is not null)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        return modFolderNames.Any(installed.Contains);
    }
}
