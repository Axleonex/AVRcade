using System.Diagnostics;
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
                return new(false, "This game's executable is missing from the selected folder. Choose Locate installed game.");
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
            if (!controls.Ok) return controls;
            return new(true, (interrupted ? "Interrupted update recovered. " : "Setup rechecked. ") + controls.Message +
                " Package and headset readiness are shown below for this installation: " + root);
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or InvalidOperationException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException)
        { return new(false, "Setup could not be repaired automatically: " + ex.Message); }
    }

    private static bool Running(string name)
    {
        var processes = Process.GetProcessesByName(name);
        try { return processes.Length > 0; }
        finally { foreach (var process in processes) process.Dispose(); }
    }
}
