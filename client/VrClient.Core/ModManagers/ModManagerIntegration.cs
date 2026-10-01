using System.Diagnostics;
using System.Text.Json;
using Microsoft.Win32;
using System.Runtime.Versioning;
using VrClient.Core.Launch;

namespace VrClient.Core.ModManagers;

public enum ModManagerKind { R2Modman, Thunderstore, Vortex }

public sealed record CommunityVrRoute(string Slug, string GameFolder, string AppId,
    string Namespace, string Package, string Creator, string Summary, string PageUrl,
    string Warning, bool RequiresSteamVr = false)
{
    public string PackageKey => $"{Namespace}-{Package}";
}

public static class CommunityVrRoutes
{
    public static readonly IReadOnlyDictionary<string, CommunityVrRoute> All =
        new Dictionary<string, CommunityVrRoute>(StringComparer.OrdinalIgnoreCase)
        {
            ["repo"] = new("repo", "REPO", "3241660", "DaXcess", "RepoXR", "DaXcess",
                "Community OpenXR conversion for R.E.P.O. with VR controls.",
                "https://thunderstore.io/c/repo/p/DaXcess/RepoXR/",
                "Use private, consenting modded sessions. Other mod combinations are unverified."),
            ["lethal-company"] = new("lethal-company", "LethalCompany", "1966720", "DaXcess", "LethalCompanyVR", "DaXcess",
                "Community OpenXR conversion for Lethal Company with VR controls.",
                "https://thunderstore.io/c/lethal-company/p/DaXcess/LethalCompanyVR/",
                "Use private, consenting modded sessions. Check the current game build against LCVR's supported builds."),
            ["peak"] = new("peak", "PEAK", "3527290", "Andrey04o", "PeakVR", "Andrey04o",
                "6DoF head and hands, motion controls, and OpenXR. Climbing still uses the game's usual action.",
                "https://thunderstore.io/c/peak/p/Andrey04o/PeakVR/",
                "The author tested specific PEAK builds. Your current build and other mods are not verified by AVRcade."),
            ["content-warning"] = new("content-warning", "ContentWarning", "2881650", "DaXcess", "CWVR", "DaXcess",
                "Community OpenXR conversion with 6DoF VR controls.",
                "https://thunderstore.io/c/content-warning/p/DaXcess/CWVR/",
                "CWVR is deprecated. Current game compatibility is unverified, and motion sickness is possible."),
            ["big-walk"] = new("big-walk", "BigWalk", "1478500", "CircuitLord", "Big_Walk_VR", "CircuitLord",
                "Early community stereo VR and motion controls for Big Walk.",
                "https://old.thunderstore.io/c/big-walk/p/CircuitLord/Big_Walk_VR/",
                "Start SteamVR first. The author reports possible stutter with other mods; combinations are unverified.", true)
        };
}

public sealed record ModManagerInstall(ModManagerKind Kind, string DisplayName,
    string LaunchPath, string? DataRoot);

public sealed record ModManagerProfile(ModManagerInstall Manager, string GameSlug,
    string Id, string Name, string ManagerGameId, string? Directory, IReadOnlyList<string> Mods,
    bool? VrModPresent, bool? LoaderPresent)
{
    public string Key => $"{Manager.Kind}:{Id}";
    public string DisplayName => $"{Manager.DisplayName} · {Name}";
    /// At least one enabled mod besides the mod loader itself.
    public bool HasOtherMods => Mods.Any(mod => !mod.StartsWith("BepInEx-", StringComparison.OrdinalIgnoreCase));
}

/// Read-only discovery. A data folder alone never counts as an installed manager.
/// Manager data roots may be overridden when the user moved them in manager settings.
public sealed class ModManagerDiscovery
{
    private readonly string _roaming;
    private readonly string _local;
    private readonly string _programFiles;
    private readonly string _programFilesX86;
    private readonly IReadOnlyList<string> _startMenus;
    private readonly Func<string, string?> _vortexState;
    private readonly Func<IEnumerable<string>> _vortexInstallPaths;

    public ModManagerDiscovery(string? roaming = null, string? local = null,
        string? programFiles = null, string? programFilesX86 = null,
        IReadOnlyList<string>? startMenus = null, Func<string, string?>? vortexState = null,
        Func<IEnumerable<string>>? vortexInstallPaths = null)
    {
        _roaming = roaming ?? Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        _local = local ?? Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        _programFiles = programFiles ?? Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        _programFilesX86 = programFilesX86 ?? Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        _startMenus = startMenus ??
        [
            Environment.GetFolderPath(Environment.SpecialFolder.Programs),
            Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms)
        ];
        _vortexState = vortexState ?? ReadVortexStateCached;
        _vortexInstallPaths = vortexInstallPaths ?? DefaultVortexInstallPaths;
    }

    public IReadOnlyList<ModManagerInstall> FindInstalled(IReadOnlyDictionary<ModManagerKind, string>? dataRoots = null)
    {
        var found = new List<ModManagerInstall>();
        var r2 = FirstExistingFile(
            PreferredR2ModmanExecutable(),
            Path.Combine(_local, "Programs", "r2modman", "r2modman.exe"),
            Path.Combine(_programFiles, "r2modman", "r2modman.exe"),
            Path.Combine(_programFilesX86, "r2modman", "r2modman.exe"));
        if (r2 is not null)
            found.Add(new(ModManagerKind.R2Modman, "r2modman", r2,
                Root(dataRoots, ModManagerKind.R2Modman, Path.Combine(_roaming, "r2modmanPlus-local"))));

        var vortex = FirstExistingFile(_vortexInstallPaths().Concat(new[]
        {
            Path.Combine(_local, "Programs", "Vortex", "Vortex.exe"),
            Path.Combine(_programFiles, "Vortex", "Vortex.exe"),
            Path.Combine(_programFiles, "Black Tree Gaming Ltd", "Vortex", "Vortex.exe"),
            Path.Combine(_programFilesX86, "Vortex", "Vortex.exe")
        }).ToArray());
        if (vortex is not null)
            found.Add(new(ModManagerKind.Vortex, "Vortex", vortex, null));

        // Thunderstore is an Overwolf app. Require both an app shortcut and an
        // installed Overwolf launcher; a leftover profile directory is insufficient.
        var overwolf = FirstExistingFile(
            Path.Combine(_local, "Overwolf", "OverwolfLauncher.exe"),
            Path.Combine(_programFilesX86, "Overwolf", "OverwolfLauncher.exe"),
            Path.Combine(_programFiles, "Overwolf", "OverwolfLauncher.exe"));
        var shortcut = _startMenus.SelectMany(path => SafeFiles(path,
            "*Thunderstore*Mod*Manager*.lnk", SearchOption.AllDirectories)).FirstOrDefault();
        if (overwolf is not null && shortcut is not null)
            found.Add(new(ModManagerKind.Thunderstore, "Thunderstore Mod Manager", shortcut,
                Root(dataRoots, ModManagerKind.Thunderstore,
                    Path.Combine(_roaming, "Thunderstore Mod Manager", "DataFolder"))));
        return found;
    }

    /// installed lets a caller that lists several games look for the managers once.
    public IReadOnlyList<ModManagerProfile> Discover(string gameSlug,
        IReadOnlyDictionary<ModManagerKind, string>? dataRoots = null,
        IReadOnlyList<ModManagerInstall>? installed = null)
    {
        if (!CommunityVrRoutes.All.TryGetValue(gameSlug, out var route))
            return [];
        var profiles = new List<ModManagerProfile>();
        foreach (var manager in installed ?? FindInstalled(dataRoots))
        {
            if (manager.Kind == ModManagerKind.Vortex)
            {
                profiles.AddRange(ReadVortexProfiles(manager, route));
                continue;
            }
            if (manager.DataRoot is null)
                continue;
            foreach (var gameDir in SafeDirectories(manager.DataRoot))
            {
                if (Normalize(Path.GetFileName(gameDir)) != Normalize(route.GameFolder))
                    continue;
                var profilesDir = Path.Combine(gameDir, "profiles");
                foreach (var profileDir in SafeDirectories(profilesDir))
                {
                    IReadOnlyList<string> mods;
                    try { mods = ReadPackageNames(profileDir); }
                    catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                    { mods = []; }
                    var loader = DoorstopProfile.FindLoader(profileDir) is not null;
                    profiles.Add(new(manager, gameSlug, Path.GetFullPath(profileDir),
                        Path.GetFileName(profileDir), Path.GetFileName(gameDir), profileDir, mods,
                        mods.Contains(route.PackageKey, StringComparer.OrdinalIgnoreCase), loader));
                }
            }
        }
        return profiles.OrderBy(p => p.Manager.DisplayName).ThenBy(p => p.Name).ToArray();
    }

    private IEnumerable<ModManagerProfile> ReadVortexProfiles(ModManagerInstall manager, CommunityVrRoute route)
    {
        var json = _vortexState(manager.LaunchPath);
        if (string.IsNullOrWhiteSpace(json)) yield break;
        JsonDocument? doc = null;
        try { doc = JsonDocument.Parse(json); }
        catch (JsonException) { yield break; }
        using (doc)
        {
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object) yield break;
            foreach (var property in root.EnumerateObject())
            {
                var value = property.Value;
                if (value.ValueKind != JsonValueKind.Object ||
                    !value.TryGetProperty("gameId", out var game) ||
                    game.ValueKind != JsonValueKind.String ||
                    Normalize(game.GetString()!) != Normalize(route.GameFolder))
                    continue;
                var name = value.TryGetProperty("name", out var n) && n.ValueKind == JsonValueKind.String
                    ? n.GetString()! : property.Name;
                yield return new(manager, route.Slug, property.Name, name, game.GetString()!, null, [], null, null);
            }
        }
    }

    private (string Executable, DateTime ReadAt, string? State)? _vortexStateCache;

    /// One catalogue refresh asks for every game's profiles in turn; asking Vortex
    /// once is enough, and each query starts a Vortex process.
    private string? ReadVortexStateCached(string executable)
    {
        if (_vortexStateCache is { } cached && cached.Executable == executable &&
            DateTime.UtcNow - cached.ReadAt < TimeSpan.FromSeconds(5))
            return cached.State;
        var state = ReadVortexState(executable);
        _vortexStateCache = (executable, DateTime.UtcNow, state);
        return state;
    }

    private static string? ReadVortexState(string executable)
    {
        try
        {
            using var process = new Process();
            process.StartInfo = new ProcessStartInfo(executable)
            {
                UseShellExecute = false, RedirectStandardOutput = true, CreateNoWindow = true
            };
            process.StartInfo.ArgumentList.Add("--get");
            process.StartInfo.ArgumentList.Add("persistent.profiles");
            process.Start();
            var output = process.StandardOutput.ReadToEndAsync();
            if (!process.WaitForExit(5000)) { process.Kill(entireProcessTree: true); return null; }
            return process.ExitCode == 0 ? output.GetAwaiter().GetResult() : null;
        }
        catch (Exception) { return null; }
    }

    private static IReadOnlyList<string> ReadPackageNames(string profileDir)
    {
        var mods = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var managerList = Path.Combine(profileDir, "mods.yml");
        if (File.Exists(managerList))
        {
            // Read only the two scalar fields needed for a conservative inventory.
            // If this manager-owned format changes, fall back to folder discovery.
            string? currentName = null;
            var enabled = false;
            var sawEntry = false;
            void Finish()
            {
                if (enabled && !string.IsNullOrWhiteSpace(currentName)) mods.Add(currentName);
                currentName = null;
                enabled = false;
            }
            foreach (var line in File.ReadLines(managerList))
            {
                if (line.StartsWith("- ", StringComparison.Ordinal))
                {
                    Finish();
                    sawEntry = true;
                }
                else if (line.StartsWith("  name: ", StringComparison.Ordinal))
                    currentName = line[8..].Trim().Trim('"', '\'');
                else if (line.StartsWith("  enabled: ", StringComparison.Ordinal))
                    enabled = line[11..].Trim() == "true";
            }
            Finish();
            if (sawEntry) return mods.OrderBy(x => x).ToArray();
        }
        foreach (var folder in new[] { "plugins", "patchers" })
        {
            var root = Path.Combine(profileDir, "BepInEx", folder);
            if (!Directory.Exists(root)) continue;
            foreach (var dir in Directory.EnumerateDirectories(root))
                mods.Add(Path.GetFileName(dir));
        }
        return mods.OrderBy(x => x).ToArray();
    }

    private static string Root(IReadOnlyDictionary<ModManagerKind, string>? roots,
        ModManagerKind kind, string fallback) =>
        roots is not null && roots.TryGetValue(kind, out var path) && !string.IsNullOrWhiteSpace(path)
            ? Path.GetFullPath(path) : fallback;
    private static string? FirstExistingFile(params string[] paths) => paths.FirstOrDefault(File.Exists);

    /// <summary>Remember a user-selected portable r2modman executable for later sessions.</summary>
    public static void RememberR2ModmanExecutable(string executable)
    {
        if (string.IsNullOrWhiteSpace(executable) ||
            !Path.IsPathFullyQualified(executable) ||
            !Path.GetFileName(executable).Equals("r2modman.exe", StringComparison.OrdinalIgnoreCase) ||
            !File.Exists(executable))
            throw new ArgumentException("Select an existing r2modman.exe.", nameof(executable));
        var preference = R2ModmanPreferencePath();
        Directory.CreateDirectory(Path.GetDirectoryName(preference)!);
        File.WriteAllText(preference, Path.GetFullPath(executable));
    }

    private static string R2ModmanPreferencePath() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AVRcade", "r2modman-executable.txt");

    private static string PreferredR2ModmanExecutable()
    {
        try
        {
            var path = File.ReadAllText(R2ModmanPreferencePath()).Trim();
            return Path.IsPathFullyQualified(path) &&
                Path.GetFileName(path).Equals("r2modman.exe", StringComparison.OrdinalIgnoreCase)
                ? path : string.Empty;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return string.Empty; }
    }

    /// <summary>Remember a user-selected portable Vortex install for later sessions.</summary>
    public static void RememberVortexExecutable(string executable)
    {
        if (!TryVortexExecutable(executable, out var path) || !File.Exists(path))
            throw new ArgumentException("Select an existing Vortex.exe.", nameof(executable));
        var preference = VortexPreferencePath();
        Directory.CreateDirectory(Path.GetDirectoryName(preference)!);
        File.WriteAllText(preference, path);
    }

    private static IEnumerable<string> DefaultVortexInstallPaths()
    {
        string? preferred = null;
        try { preferred = File.ReadAllText(VortexPreferencePath()).Trim(); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        if (TryVortexExecutable(preferred, out var path)) yield return path;
        foreach (var fromRegistry in VortexInstallPathsFromRegistry()) yield return fromRegistry;
    }

    private static string VortexPreferencePath() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AVRcade", "vortex-executable.txt");

    private static IEnumerable<string> VortexInstallPathsFromRegistry()
    {
        if (!OperatingSystem.IsWindows()) yield break;
        foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            RegistryKey? root = null;
            try { root = RegistryKey.OpenBaseKey(hive, view); }
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { }
            if (root is null) continue;
            using (root)
            {
                foreach (var path in RegistryVortexPaths(root)) yield return path;
            }
        }
    }

    [SupportedOSPlatform("windows")]
    private static IEnumerable<string> RegistryVortexPaths(RegistryKey root)
    {
        const string uninstall = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall";
        RegistryKey? apps = null;
        try { apps = root.OpenSubKey(uninstall); }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { }
        if (apps is not null)
        using (apps)
        {
            string[] names;
            try { names = apps.GetSubKeyNames(); }
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { names = []; }
            foreach (var name in names)
            {
                RegistryKey? entry = null;
                try { entry = apps.OpenSubKey(name); }
                catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { }
                if (entry is null) continue;
                using (entry)
                {
                    if (!string.Equals(entry.GetValue("DisplayName") as string, "Vortex", StringComparison.OrdinalIgnoreCase))
                        continue;
                    var icon = entry.GetValue("DisplayIcon") as string;
                    if (TryVortexExecutable(icon, out var exe)) yield return exe;
                    var location = entry.GetValue("InstallLocation") as string;
                    if (!string.IsNullOrWhiteSpace(location)) yield return Path.Combine(location, "Vortex.exe");
                }
            }
        }
        RegistryKey? appPath = null;
        try { appPath = root.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Vortex.exe"); }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) { }
        if (appPath is not null)
        using (appPath)
            if (TryVortexExecutable(appPath.GetValue(null) as string, out var exe)) yield return exe;
    }

    internal static bool TryVortexExecutable(string? value, out string executable)
    {
        executable = "";
        if (string.IsNullOrWhiteSpace(value)) return false;
        var candidate = value.Trim().Trim('"');
        if (candidate.EndsWith(",0", StringComparison.Ordinal)) candidate = candidate[..^2].TrimEnd('"');
        if (!string.Equals(Path.GetFileName(candidate), "Vortex.exe", StringComparison.OrdinalIgnoreCase))
            return false;
        if (!Path.IsPathFullyQualified(candidate)) return false;
        executable = candidate;
        return true;
    }
    private static IReadOnlyList<string> SafeDirectories(string path)
    {
        try { return Directory.Exists(path) ? Directory.GetDirectories(path) : []; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return []; }
    }
    private static IReadOnlyList<string> SafeFiles(string path, string pattern, SearchOption option)
    {
        try { return Directory.Exists(path) ? Directory.GetFiles(path, pattern, option) : []; }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { return []; }
    }
    private static string Normalize(string name) => new(name.Where(char.IsLetterOrDigit)
        .Select(char.ToLowerInvariant).ToArray());
}
