using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Launch;

/// Selected-game recovery: no downloads, manager profile changes or preference reset.
public sealed class GameSetupRecovery(ControllerCustomization? controllers = null,
    Func<string, bool>? isRunning = null)
{
    public ActionOutcome Recheck(string repoRoot, GameView game)
    {
        try
        {
            if (game.InstallDir is not { } root || !Directory.Exists(root))
                return new(false, "Reconnect this game's disk or choose Locate installed game, then Recheck setup.");
            var executables = ControllerRuntimeBindings.Executables(repoRoot, game);
            if (executables.Count == 0)
                return new(false, "This game's executable is missing from the selected folder. Choose Locate installed game or Verify game files in Steam.");
            var route = GameModUpdateRoutes.Load(repoRoot, game.Slug).GetValueOrDefault(game.Slug);
            var processes = (route?.ProcessNames ?? []).Concat(executables.Select(Path.GetFileNameWithoutExtension));
            void EnsureClosed()
            {
                foreach (var name in processes.Where(name => !string.IsNullOrEmpty(name)).Distinct(StringComparer.OrdinalIgnoreCase))
                    if (isRunning?.Invoke(name!) ?? Running(name!))
                        throw new IOException("Close this game before repairing setup, then press Recheck setup again.");
            }
            EnsureClosed();
            var lockPath = ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/update.lock");
            Directory.CreateDirectory(Path.GetDirectoryName(lockPath)!);
            using var lease = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            var interrupted = File.Exists(ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/pending.json"));
            ModUpdateTransaction.Recover(root);
            EnsureClosed();
            if (game.Slug == "cyberpunk-2077" && game.Engine is GameEngine.Redengine &&
                CyberpunkUpstreamSession.ReadState(root) is not null)
            {
                using var previousSession = new CyberpunkUpstreamSession(root, vr: false);
            }
            var controls = new ControllerRuntimeBindings(controllers).RecheckRoutes(repoRoot, game);
            var native = new NativeDependencyHealth().Check(root, executables);
            if (!native.Healthy) return new(false, native.Message);
            if (!controls.Ok) return controls;
            return new(true, (interrupted ? "Interrupted update recovered. " : "Setup rechecked. ") + controls.Message +
                " " + native.Message + " Package and headset readiness are shown below for this installation: " + root);
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or InvalidOperationException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
        { return new(false, "Setup could not be repaired automatically: " + ex.Message); }
    }

    /// Requests Steam's repair for the exact selected installation; completion is
    /// reported by Steam. Existing interrupted updates are recovered first.
    public ActionOutcome VerifyGameFiles(string repoRoot, GameView game, Action<ProcessStartInfo>? request = null)
    {
        try
        {
            if (game.InstallDir is not { } root || !Directory.Exists(root))
                return new(false, "Locate the installed Steam game before verifying its files.");
            if (!uint.TryParse(game.SteamAppId, NumberStyles.None, CultureInfo.InvariantCulture, out var appId) || appId == 0)
                return new(false, "This game has no valid Steam identity for file verification.");
            var manifest = GameLaunchBinding.ManifestFor(root, game.SteamAppId);
            if (!File.Exists(manifest) || new FileInfo(manifest).Length > 1024 * 1024)
                return new(false, "The selected installation has no readable Steam manifest. Locate its registered Steam folder before verifying.");
            var text = File.ReadAllText(manifest);
            var identity = Regex.Match(text, "\"appid\"\\s+\"([0-9]+)\"").Groups[1].Value;
            var directory = Regex.Match(text, "\"installdir\"\\s+\"([^\"\\r\\n]+)\"").Groups[1].Value;
            if (identity != game.SteamAppId || !directory.Equals(Path.GetFileName(Path.TrimEndingDirectorySeparator(root)), StringComparison.OrdinalIgnoreCase))
                return new(false, "Steam's manifest does not match the selected game folder. Verification was not requested for another installation.");
            var executables = ControllerRuntimeBindings.Executables(repoRoot, game);
            var route = GameModUpdateRoutes.Load(repoRoot, game.Slug).GetValueOrDefault(game.Slug);
            void EnsureClosed()
            {
                foreach (var name in (route?.ProcessNames ?? []).Concat(executables.Select(Path.GetFileNameWithoutExtension))
                    .Where(name => !string.IsNullOrEmpty(name)).Distinct(StringComparer.OrdinalIgnoreCase))
                    if (isRunning?.Invoke(name!) ?? Running(name!))
                        throw new IOException("Close this game before verifying its files in Steam.");
            }
            EnsureClosed();
            var lockPath = ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/update.lock");
            Directory.CreateDirectory(Path.GetDirectoryName(lockPath)!);
            using var lease = new FileStream(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            ModUpdateTransaction.Recover(root);
            EnsureClosed();
            if (game.Slug == "cyberpunk-2077" && game.Engine is GameEngine.Redengine && CyberpunkUpstreamSession.ReadState(root) is not null)
            { using var previousSession = new CyberpunkUpstreamSession(root, vr: false); }
            var start = new ProcessStartInfo($"steam://validate/{appId}") { UseShellExecute = true };
            if (request is not null) request(start);
            else using (Process.Start(start)) { }
            return new(true, $"Steam verification requested for {game.DisplayName}. Wait for Steam to finish checking and repairing its original files, then press Recheck setup. Mod compatibility still needs its own checks.");
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or InvalidOperationException or
            UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException or System.ComponentModel.Win32Exception)
        { return new(false, "Game-file verification could not start: " + ex.Message); }
    }

    private static bool Running(string name)
    {
        var processes = Process.GetProcessesByName(name);
        try { return processes.Length > 0; }
        finally { foreach (var process in processes) process.Dispose(); }
    }
}
