namespace VrClient.Core.FalloutNewVegas;

using System.Diagnostics;
using Microsoft.Win32;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

public sealed class FalloutNewVegasDiscovery
{
    public const string SteamAppId = "22380";
    public const string ExecutableName = "FalloutNV.exe";

    public FnvDiscoveryResult Discover(
        IEnumerable<string>? steamRoots = null,
        IEnumerable<string>? gogCandidateDirectories = null,
        string? manualOverride = null)
    {
        var installs = new List<FnvGameInstall>();
        var diagnostics = new List<string>();

        foreach (var steamRoot in steamRoots ?? DefaultSteamRoots())
        {
            var root = FindSteamInstall(steamRoot);
            if (root is not null)
                installs.Add(Inspect(root, FnvStorefront.Steam));
        }

        foreach (var candidate in gogCandidateDirectories ?? DefaultGogCandidates())
        {
            if (Directory.Exists(candidate))
                installs.Add(Inspect(candidate, FnvStorefront.Gog));
        }

        if (!string.IsNullOrWhiteSpace(manualOverride))
        {
            if (!Directory.Exists(manualOverride))
                diagnostics.Add($"manual_location_missing: {Path.GetFullPath(manualOverride)}");
            else
                installs.Add(Inspect(manualOverride, FnvStorefront.Manual));
        }

        var unique = installs
            .GroupBy(install => Path.GetFullPath(install.RootDirectory), StringComparer.OrdinalIgnoreCase)
            .Select(group => group.OrderBy(install => install.Storefront == FnvStorefront.Manual).First())
            .ToArray();

        if (unique.Length == 0)
            diagnostics.Add("fallout_new_vegas_not_found: supply a Steam root, GOG location, or manual override");
        return new FnvDiscoveryResult(unique, diagnostics);
    }

    public FnvGameInstall Inspect(string directory, FnvStorefront storefront)
    {
        var root = Path.GetFullPath(directory);
        var exe = Path.Combine(root, ExecutableName);
        var notes = new List<string>();
        var support = storefront == FnvStorefront.Manual
            ? FnvInstallSupport.ManualUnverified
            : FnvInstallSupport.Supported;

        if (!File.Exists(exe))
        {
            support = FnvInstallSupport.WrongDirectory;
            notes.Add("wrong_directory: FalloutNV.exe is absent; select the folder containing FalloutNV.exe and FalloutNVLauncher.exe");
            var otherBethesdaExe = Directory.EnumerateFiles(root, "Fallout*.exe", SearchOption.TopDirectoryOnly)
                .FirstOrDefault();
            if (otherBethesdaExe is not null)
                notes.Add($"wrong_game_executable_detected: {Path.GetFileName(otherBethesdaExe)}");
            return new FnvGameInstall(root, exe, storefront, support, "missing-executable", string.Empty, null, notes);
        }

        if (File.Exists(Path.Combine(root, "MicrosoftGame.Config")) ||
            Directory.Exists(Path.Combine(root, ".egstore")) ||
            File.Exists(Path.Combine(root, "EOSSDK-Win32-Shipping.dll")))
        {
            support = FnvInstallSupport.UnsupportedEdition;
            notes.Add("unsupported_edition: xNVSE and the supported FNV 4GB Patcher target legitimate Steam and GOG builds");
        }
        else if (storefront == FnvStorefront.Manual)
        {
            notes.Add("manual_edition_unverified: AVRcade found the game but cannot prove Steam or GOG ownership from this path alone");
        }

        var version = TryGetFileVersion(exe);
        var sha256 = ComputeSha256(exe);
        var identity = $"{storefront.ToString().ToLowerInvariant()}:{version ?? "unknown-version"}:{sha256[..Math.Min(12, sha256.Length)]}";
        if (!File.Exists(Path.Combine(root, "FalloutNVLauncher.exe")))
            notes.Add("launcher_missing: FalloutNVLauncher.exe was not found; verify the game installation");
        return new FnvGameInstall(root, exe, storefront, support, identity, sha256, version, notes);
    }

    private static string? FindSteamInstall(string steamRoot)
    {
        if (!Directory.Exists(steamRoot))
            return null;
        var libraries = new List<string> { Path.GetFullPath(steamRoot) };
        var libraryFile = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
        if (File.Exists(libraryFile))
        {
            foreach (Match match in Regex.Matches(File.ReadAllText(libraryFile), "\"path\"\\s+\"([^\"]+)\""))
                libraries.Add(match.Groups[1].Value.Replace("\\\\", "\\"));
        }

        foreach (var library in libraries.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var manifest = Path.Combine(library, "steamapps", $"appmanifest_{SteamAppId}.acf");
            if (!File.Exists(manifest))
                continue;
            var match = Regex.Match(File.ReadAllText(manifest), "\"installdir\"\\s+\"([^\"]+)\"");
            if (!match.Success)
                continue;
            var candidate = Path.Combine(library, "steamapps", "common", match.Groups[1].Value);
            if (File.Exists(Path.Combine(candidate, ExecutableName)))
                return candidate;
        }
        return null;
    }

    private static IEnumerable<string> DefaultSteamRoots()
    {
        foreach (var programFiles in new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
        })
        {
            if (!string.IsNullOrWhiteSpace(programFiles))
                yield return Path.Combine(programFiles, "Steam");
        }
    }

    private static IEnumerable<string> DefaultGogCandidates()
    {
        foreach (var registryCandidate in GogRegistryCandidates())
            yield return registryCandidate;
        foreach (var programFiles in new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86)
        })
        {
            if (string.IsNullOrWhiteSpace(programFiles))
                continue;
            yield return Path.Combine(programFiles, "GOG Galaxy", "Games", "Fallout New Vegas");
            yield return Path.Combine(programFiles, "GOG.com", "Fallout New Vegas");
        }
    }

    private static IEnumerable<string> GogRegistryCandidates()
    {
        if (!OperatingSystem.IsWindows())
            return Array.Empty<string>();
        var results = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser })
        foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            try
            {
                using var baseKey = RegistryKey.OpenBaseKey(hive, view);
                using var uninstall = baseKey.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
                if (uninstall is not null)
                {
                    foreach (var name in uninstall.GetSubKeyNames())
                    {
                        using var entry = uninstall.OpenSubKey(name);
                        var displayName = entry?.GetValue("DisplayName") as string;
                        var location = entry?.GetValue("InstallLocation") as string;
                        var publisher = entry?.GetValue("Publisher") as string;
                        if (!string.IsNullOrWhiteSpace(location) && displayName?.Contains("Fallout", StringComparison.OrdinalIgnoreCase) == true &&
                            displayName.Contains("New Vegas", StringComparison.OrdinalIgnoreCase) &&
                            (name.Contains("GOG", StringComparison.OrdinalIgnoreCase) || publisher?.Contains("GOG", StringComparison.OrdinalIgnoreCase) == true))
                            results.Add(location);
                    }
                }

                using var gogGames = baseKey.OpenSubKey(@"SOFTWARE\GOG.com\Games");
                if (gogGames is not null)
                {
                    foreach (var name in gogGames.GetSubKeyNames())
                    {
                        using var entry = gogGames.OpenSubKey(name);
                        var gameName = entry?.GetValue("gameName") as string;
                        var path = entry?.GetValue("path") as string;
                        if (!string.IsNullOrWhiteSpace(path) && gameName?.Contains("New Vegas", StringComparison.OrdinalIgnoreCase) == true)
                            results.Add(path);
                    }
                }
            }
            catch (Exception error) when (error is UnauthorizedAccessException or IOException or System.Security.SecurityException)
            {
                // Registry discovery is best-effort; manual override remains available.
            }
        }
        return results;
    }

    private static string ComputeSha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    private static string? TryGetFileVersion(string path)
    {
        try
        {
            var info = FileVersionInfo.GetVersionInfo(path);
            return string.IsNullOrWhiteSpace(info.FileVersion) ? null : info.FileVersion;
        }
        catch
        {
            return null;
        }
    }
}
