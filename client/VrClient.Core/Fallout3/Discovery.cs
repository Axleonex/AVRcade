namespace VrClient.Core.Fallout3;

using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using Microsoft.Win32;

public sealed class Fallout3Discovery
{
    public const string SteamAppId = "22370";
    public const string LegacySteamAppId = "22300";
    public static readonly IReadOnlyList<string> SteamAppIds = [SteamAppId, LegacySteamAppId];
    public const string ExecutableName = "Fallout3.exe";
    public static readonly IReadOnlyDictionary<string, string> RequiredDlc =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["Anchorage"] = "Anchorage.esm",
            ["ThePitt"] = "ThePitt.esm",
            ["BrokenSteel"] = "BrokenSteel.esm",
            ["PointLookout"] = "PointLookout.esm",
            ["Zeta"] = "Zeta.esm"
        };

    public Fallout3DiscoveryResult Discover(
        IEnumerable<string>? steamRoots = null,
        IEnumerable<string>? gogCandidateDirectories = null,
        string? manualOverride = null)
    {
        var installs = new List<Fallout3Install>();
        var diagnostics = new List<string>();
        foreach (var root in steamRoots ?? DefaultSteamRoots())
        {
            var install = FindSteamInstall(root);
            if (install is not null) installs.Add(Inspect(install, Fallout3Storefront.Steam));
        }
        foreach (var candidate in gogCandidateDirectories ?? DefaultGogCandidates())
            if (Directory.Exists(candidate)) installs.Add(Inspect(candidate, Fallout3Storefront.Gog));
        if (!string.IsNullOrWhiteSpace(manualOverride))
        {
            if (Directory.Exists(manualOverride)) installs.Add(Inspect(manualOverride, Fallout3Storefront.Manual));
            else diagnostics.Add($"manual_location_missing: {Path.GetFullPath(manualOverride)}");
        }
        var unique = installs.GroupBy(item => Path.GetFullPath(item.RootDirectory), StringComparer.OrdinalIgnoreCase)
            .Select(group => group.OrderBy(item => item.Storefront == Fallout3Storefront.Manual).First()).ToArray();
        if (unique.Length == 0)
            diagnostics.Add("fallout_3_not_found: supply a Steam root, GOG location, or manual override");
        return new Fallout3DiscoveryResult(unique, diagnostics);
    }

    public Fallout3Install Inspect(string directory, Fallout3Storefront storefront)
    {
        var root = Path.GetFullPath(directory);
        var executable = Path.Combine(root, ExecutableName);
        var notes = new List<string>();
        var support = storefront == Fallout3Storefront.Manual
            ? Fallout3InstallSupport.ManualUnverified : Fallout3InstallSupport.Supported;
        if (!File.Exists(executable))
        {
            notes.Add("wrong_directory: select the folder containing Fallout3.exe and Fallout3Launcher.exe");
            return new Fallout3Install(root, executable, storefront, Fallout3InstallSupport.WrongDirectory,
                "missing-executable", string.Empty, null, EmptyDlc(), notes);
        }
        var version = TryGetVersion(executable);
        var sha = ComputeSha256(executable);
        if (version is not null && version != "1.7.0.3" && version != "1.7.0.4")
        {
            support = Fallout3InstallSupport.UnsupportedBuild;
            notes.Add($"unsupported_build: Fallout3.exe {version} is not a recognized Steam/GOG compatibility target");
        }
        if (version == "1.7.0.4")
            notes.Add("anniversary_patch_required_for_fose: use the author-provided patcher after reviewing its permissions and backup behavior");
        if (storefront == Fallout3Storefront.Manual)
            notes.Add("manual_edition_unverified: the path alone does not prove storefront ownership or build support");
        var dlc = RequiredDlc.ToDictionary(pair => pair.Key,
            pair => File.Exists(Path.Combine(root, "Data", pair.Value)), StringComparer.OrdinalIgnoreCase);
        foreach (var missing in dlc.Where(pair => !pair.Value).Select(pair => pair.Key))
            notes.Add($"goty_dlc_missing: {missing}");
        if (!File.Exists(Path.Combine(root, "Fallout3Launcher.exe")) &&
            !File.Exists(Path.Combine(root, "FalloutLauncher.exe")) &&
            !File.Exists(Path.Combine(root, "FalloutLauncherSteam.exe")))
            notes.Add("launcher_missing: run storefront verification/repair before conversion");
        return new Fallout3Install(root, executable, storefront, support,
            $"{storefront.ToString().ToLowerInvariant()}:{version ?? "unknown"}:{sha[..12]}", sha, version, dlc, notes);
    }

    private static IReadOnlyDictionary<string, bool> EmptyDlc() =>
        RequiredDlc.Keys.ToDictionary(key => key, _ => false, StringComparer.OrdinalIgnoreCase);

    private static string? FindSteamInstall(string steamRoot)
    {
        if (!Directory.Exists(steamRoot)) return null;
        var libraries = new List<string> { Path.GetFullPath(steamRoot) };
        var libraryFile = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
        if (File.Exists(libraryFile))
            foreach (Match match in Regex.Matches(File.ReadAllText(libraryFile), "\"path\"\\s+\"([^\"]+)\""))
                libraries.Add(match.Groups[1].Value.Replace("\\\\", "\\"));
        var distinctLibraries = libraries.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        // Prefer the GOTY package even when the legacy base game is installed
        // in an earlier Steam library. GOTY is the supported primary edition.
        foreach (var appId in SteamAppIds)
        foreach (var library in distinctLibraries)
        {
            var manifest = Path.Combine(library, "steamapps", $"appmanifest_{appId}.acf");
            if (!File.Exists(manifest)) continue;
            var match = Regex.Match(File.ReadAllText(manifest), "\"installdir\"\\s+\"([^\"]+)\"");
            if (!match.Success) continue;
            var candidate = Path.Combine(library, "steamapps", "common", match.Groups[1].Value);
            if (File.Exists(Path.Combine(candidate, ExecutableName))) return candidate;
        }
        return null;
    }

    private static IEnumerable<string> DefaultSteamRoots()
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in SteamRegistryCandidates())
            if (!string.IsNullOrWhiteSpace(candidate) && seen.Add(candidate)) yield return candidate;
        foreach (var programFiles in new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
        })
        {
            if (string.IsNullOrWhiteSpace(programFiles)) continue;
            var candidate = Path.Combine(programFiles, "Steam");
            if (seen.Add(candidate)) yield return candidate;
        }
    }

    private static IEnumerable<string> SteamRegistryCandidates()
    {
        if (!OperatingSystem.IsWindows()) yield break;
        foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            RegistryKey? steam = null;
            try
            {
                steam = RegistryKey.OpenBaseKey(hive, view).OpenSubKey(@"SOFTWARE\Valve\Steam");
                var path = steam?.GetValue("SteamPath") as string
                    ?? steam?.GetValue("InstallPath") as string;
                if (!string.IsNullOrWhiteSpace(path)) yield return Path.GetFullPath(path);
            }
            finally { steam?.Dispose(); }
        }
    }

    private static IEnumerable<string> DefaultGogCandidates()
    {
        foreach (var path in GogRegistryCandidates()) yield return path;
        yield return @"C:\GOG Games\Fallout 3";
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            "GOG Galaxy", "Games", "Fallout 3");
        yield return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "GOG Galaxy", "Games", "Fallout 3");
    }

    private static IEnumerable<string> GogRegistryCandidates()
    {
        if (!OperatingSystem.IsWindows()) yield break;
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser })
        foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            RegistryKey? games = null;
            try
            {
                games = RegistryKey.OpenBaseKey(hive, view).OpenSubKey(@"SOFTWARE\GOG.com\Games");
                if (games is null) continue;
                foreach (var name in games.GetSubKeyNames())
                {
                    using var entry = games.OpenSubKey(name);
                    var gameName = entry?.GetValue("gameName") as string;
                    var path = entry?.GetValue("path") as string;
                    if (!string.IsNullOrWhiteSpace(path) &&
                        gameName?.Contains("Fallout 3", StringComparison.OrdinalIgnoreCase) == true && seen.Add(path))
                        yield return path;
                }
            }
            finally { games?.Dispose(); }
        }
    }

    private static string ComputeSha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static string? TryGetVersion(string path)
    {
        try
        {
            var version = FileVersionInfo.GetVersionInfo(path).FileVersion;
            if (string.IsNullOrWhiteSpace(version)) return null;
            return version.Split(' ').First().TrimEnd('.', '0') switch
            {
                "1.7.0.3" => "1.7.0.3",
                "1.7.0.4" => "1.7.0.4",
                _ => version.Split(' ').First()
            };
        }
        catch { return null; }
    }
}
