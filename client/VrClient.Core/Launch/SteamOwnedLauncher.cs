namespace VrClient.Core.Launch;

using Microsoft.Win32;
using System.Diagnostics;

/// Launches a Steam-owned title through Steam's normal ownership/launcher
/// chain. It deliberately has no direct-executable fallback.
public sealed record SteamOwnedLaunchResult(
    bool Success,
    string Status,
    int? ProcessId = null,
    string? SteamExecutable = null,
    string? Detail = null)
{
    /// What to tell the player when the request did not go out.
    public string FailureText => Detail ?? Status;
}

public sealed class SteamOwnedLauncher
{
    public const string Rdr2AppId = "1174180";

    public SteamOwnedLaunchResult Launch(
        string appId,
        string? steamRoot = null,
        bool dryRun = false,
        IReadOnlyDictionary<string, string>? childEnvironment = null,
        IReadOnlyList<string>? launchArguments = null)
    {
        if (string.IsNullOrWhiteSpace(appId) || !appId.All(char.IsAsciiDigit))
            return new(false, "invalid_app_id", Detail: "Steam AppID must be numeric.");

        var steamExe = FindSteamExecutable(steamRoot);
        if (steamExe is null)
            return new(false, "steam_not_found",
                Detail: "Steam was not found; pass --steam-root pointing at its install folder.");

        var startInfo = new ProcessStartInfo
        {
            FileName = steamExe,
            WorkingDirectory = Path.GetDirectoryName(steamExe)!,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("-applaunch");
        startInfo.ArgumentList.Add(appId);
        if (launchArguments is not null)
            foreach (var argument in launchArguments)
                if (!string.IsNullOrWhiteSpace(argument))
                    startInfo.ArgumentList.Add(argument);
        if (childEnvironment is not null)
            foreach (var (name, value) in childEnvironment)
                startInfo.Environment[name] = value;

        if (dryRun)
            return new(true, "dry_run", SteamExecutable: steamExe,
                Detail: $"{steamExe} -applaunch {appId}" +
                    (launchArguments is { Count: > 0 }
                        ? " " + string.Join(" ", launchArguments)
                        : string.Empty));

        try
        {
            var process = Process.Start(startInfo);
            return process is null
                ? new(false, "process_start_failed", SteamExecutable: steamExe)
                : new(true, "steam_launch_started", process.Id, steamExe,
                    $"Steam owns AppID {appId}; Rockstar's normal launcher chain remains in control.");
        }
        catch (Exception ex)
        {
            return new(false, "process_start_failed", SteamExecutable: steamExe,
                Detail: ex.Message);
        }
    }

    public static string? FindSteamExecutable(string? steamRoot = null)
    {
        var roots = new List<string>();
        if (!string.IsNullOrWhiteSpace(steamRoot)) roots.Add(steamRoot);
        if (OperatingSystem.IsWindows())
        {
            try
            {
                var registryRoot = Registry.GetValue(
                    @"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamExe", null) as string;
                if (!string.IsNullOrWhiteSpace(registryRoot))
                    roots.Add(Path.GetDirectoryName(registryRoot.Replace('/', '\\'))!);
                var installPath = Registry.GetValue(
                    @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", null) as string;
                if (!string.IsNullOrWhiteSpace(installPath)) roots.Add(installPath);
            }
            catch (Exception) { /* registry is optional; filesystem candidates remain */ }
        }

        roots.AddRange([
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) is { Length: > 0 } pf86
                ? Path.Combine(pf86, "Steam") : "",
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles) is { Length: > 0 } pf
                ? Path.Combine(pf, "Steam") : ""
        ]);
        return roots.Where(path => !string.IsNullOrWhiteSpace(path))
            .Select(path => Path.Combine(path, "steam.exe"))
            .FirstOrDefault(File.Exists);
    }
}
