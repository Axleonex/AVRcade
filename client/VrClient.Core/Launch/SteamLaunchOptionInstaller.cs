namespace VrClient.Core.Launch;
using System.Diagnostics;

public sealed record LaunchOptionResult(bool Ok, bool Changed, string Message);

/// LI-1b: installs/removes the wrap launch option in Steam's localconfig.vdf for
/// EXACTLY ONE app id. Per-game opt-in by design (user directive 2026-07-10):
/// this runs as part of converting THAT game from the VRClient library (or the
/// standalone wrap-install verb) — never as a blanket operation, and never for
/// an app id that is not verifiably numeric.
public static class SteamLaunchOptionInstaller
{
    /// Guard: a real Steam app id is all digits. Placeholder configs (e.g. a
    /// scaffolded game whose app id was never filled in) must never reach Steam.
    public static bool IsValidAppId(string appId) =>
        appId.Length > 0 && appId.All(char.IsAsciiDigit);

    public static LaunchOptionResult Install(
        string appId, string wrapperExePath, bool remove, bool dryRun,
        Action<string> log, string? steamRootOverride = null)
    {
        if (!IsValidAppId(appId))
            return new(false, false,
                $"launch-option refused: app id '{appId}' is not numeric (placeholder or misconfigured game config)");

        var steamRoot = steamRootOverride
            ?? new[] { @"C:\Program Files (x86)\Steam", @"C:\Program Files\Steam" }
                .FirstOrDefault(r => File.Exists(Path.Combine(r, "steam.exe")));
        if (steamRoot is null)
            return new(false, false, "steam_not_found - pass --steam-root <dir> (the folder containing steam.exe)");

        // Most recently written per-user localconfig.vdf = the active Steam account.
        var localConfig = Directory
            .EnumerateDirectories(Path.Combine(steamRoot, "userdata"))
            .Select(d => Path.Combine(d, "config", "localconfig.vdf"))
            .Where(File.Exists)
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault();
        if (localConfig is null)
            return new(false, false, $"steam_userdata_not_found - no userdata/*/config/localconfig.vdf under {steamRoot}");

        var current = SteamLaunchOptions.GetLaunchOptions(File.ReadAllText(localConfig), appId) ?? "";
        var desired = remove
            ? SteamLaunchOptions.Strip(current, wrapperExePath)
            : SteamLaunchOptions.Compose(current, wrapperExePath);
        if (desired == current.Trim())
            return new(true, false, remove ? "unchanged (not installed)" : "unchanged (already installed)");
        if (dryRun)
            return new(true, false, $"dry-run file={localConfig} current={(current.Length == 0 ? "(empty)" : current)} new={(desired.Length == 0 ? "(empty)" : desired)}");

        // Steam must not be running while we edit, or it clobbers the file on exit.
        var steamWasRunning = Process.GetProcessesByName("steam").Length > 0;
        if (steamWasRunning)
        {
            log("closing Steam (it rewrites the config file on exit)...");
            Process.Start(new ProcessStartInfo
            {
                FileName = Path.Combine(steamRoot, "steam.exe"),
                Arguments = "-shutdown",
                UseShellExecute = false,
            })?.WaitForExit();
            for (var waited = 0; waited < 45 && Process.GetProcessesByName("steam").Length > 0; waited++)
                Thread.Sleep(1000);
            if (Process.GetProcessesByName("steam").Length > 0)
                return new(false, false, "steam_shutdown_timeout - close Steam manually and rerun");
        }

        var backup = localConfig + $".vrclient-backup-{DateTime.UtcNow:yyyyMMddTHHmmssZ}";
        File.Copy(localConfig, backup);
        File.WriteAllText(localConfig,
            SteamLaunchOptions.SetLaunchOptions(File.ReadAllText(localConfig), appId, desired));
        log($"backup={backup}");

        if (steamWasRunning)
        {
            log("restarting Steam...");
            // Relaunch via explorer so Steam gets a CLEAN environment - a Steam
            // started as our child inherits this process's env and passes it to
            // every game it launches (observed 2026-07-10 breaking Unity Doorstop
            // for all modded launches until Steam was restarted).
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = $"\"{Path.Combine(steamRoot, "steam.exe")}\"",
                UseShellExecute = true,
            });
        }
        return new(true, true,
            $"{(remove ? "removed" : "installed")} options={(desired.Length == 0 ? "(empty)" : desired)}");
    }
}
