using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.Modpack;

namespace VrClient.Core.Redengine;

/// Keeps the unmodified upstream release outside the game's mod scan paths between
/// sessions. The official plugin has no VRCLIENT_VR_ACTIVE gate. Its scripts and
/// archives must also leave the scan paths for monitor play.
public sealed class CyberpunkUpstreamSession : IDisposable
{
    public const string StoreDirectory = ".avrcade/cyberpunk-2077";
    private readonly string _gameRoot;
    private readonly FileStream _lock;
    private readonly List<string> _active;
    private bool _disposed;

    public sealed record InstallState(string Version, IReadOnlyList<string> Files,
        IReadOnlyList<string> PreviousFiles, IReadOnlyList<string>? RequirementFiles = null);

    public static string Store(string gameRoot) => Resolve(gameRoot, StoreDirectory);
    public static string PayloadPath(string gameRoot, string relative) =>
        Resolve(gameRoot, StoreDirectory + "/payload/" + relative);
    public static string StatePath(string gameRoot) => Path.Combine(Store(gameRoot), "install.json");
    private static string JournalPath(string gameRoot) => Path.Combine(Store(gameRoot), "active.json");

    public static InstallState? ReadState(string gameRoot) => File.Exists(StatePath(gameRoot))
        ? JsonSerializer.Deserialize<InstallState>(File.ReadAllText(StatePath(gameRoot))) : null;

    public static bool HasDependency(string gameRoot, string relative) =>
        File.Exists(Resolve(gameRoot, relative)) ||
        (ReadState(gameRoot)?.Files.Contains(relative, StringComparer.OrdinalIgnoreCase) == true &&
         File.Exists(PayloadPath(gameRoot, relative)));

    public CyberpunkUpstreamSession(string gameRoot, bool vr)
    {
        _gameRoot = Path.GetFullPath(gameRoot);
        var state = ReadState(gameRoot) ?? throw new InvalidOperationException("No managed upstream Cyberpunk VR package is installed.");
        if (Process.GetProcessesByName("Cyberpunk2077").Length != 0)
            throw new InvalidOperationException("Close Cyberpunk 2077 before switching the VR package.");
        _lock = new FileStream(Path.Combine(Store(gameRoot), "session.lock"), FileMode.OpenOrCreate,
            FileAccess.ReadWrite, FileShare.None);
        _active = File.Exists(JournalPath(gameRoot))
            ? JsonSerializer.Deserialize<List<string>>(File.ReadAllText(JournalPath(gameRoot))) ?? [] : [];
        try
        {
            Park(); // Recover a session whose launcher was interrupted.
            if (!vr) return;
            // Preflight every collision before publishing any part of the package.
            foreach (var relative in state.Files)
            {
                if (!File.Exists(PayloadPath(gameRoot, relative)))
                    throw new FileNotFoundException($"Cyberpunk VR package file is missing: {relative}");
                if (File.Exists(Resolve(gameRoot, relative)))
                    throw new IOException($"Another mod manager deployed {relative}. Disable CyberpunkVR Port in Vortex before using AVRcade's managed copy.");
            }
            foreach (var relative in state.Files)
            {
                var temporary = Path.Combine(Store(gameRoot), "activating.tmp");
                File.Copy(PayloadPath(gameRoot, relative), temporary, overwrite: true);
                _active.Add(relative);
                SaveJournal(); // Recovery owns a destination before the copy begins.
                var target = Resolve(gameRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                try { File.Move(temporary, target, overwrite: false); }
                catch
                {
                    _active.Remove(relative);
                    SaveJournal();
                    throw;
                }
            }
        }
        catch
        {
            try { Park(); }
            finally { _lock.Dispose(); }
            throw;
        }
    }

    private void SaveJournal() => AtomicFile.WriteAllText(JournalPath(_gameRoot), JsonSerializer.Serialize(_active));

    private void Park()
    {
        // An interrupted launcher does not grant ownership of files a manager
        // replaced afterwards. Only mutable preferences may differ from storage.
        foreach (var relative in _active)
        {
            var target = Resolve(_gameRoot, relative);
            var saved = PayloadPath(_gameRoot, relative);
            if (File.Exists(target) && File.Exists(saved) &&
                Path.GetExtension(relative).ToLowerInvariant() is not (".ini" or ".cfg" or ".json") &&
                !Hashing.Sha256OfFile(target).Equals(Hashing.Sha256OfFile(saved), StringComparison.OrdinalIgnoreCase))
                throw new IOException($"Another mod changed {relative} during the previous VR session. Its file and AVRcade's stored copy were preserved. Disable that deployment in your manager, then Recheck setup.");
        }
        // The native plugin leaves first. Every DLL backup is outside RED4ext.
        foreach (var relative in _active.OrderByDescending(p => p.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)).ToArray())
        {
            var target = Resolve(_gameRoot, relative);
            if (File.Exists(target))
            {
                var saved = PayloadPath(_gameRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(saved)!);
                // Keep hand-pose and other edits made through the upstream overlay.
                File.Copy(target, saved, overwrite: true);
                File.Delete(target);
            }
            _active.Remove(relative);
            SaveJournal();
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        // Never remove code or assets from a game that is still using them.
        if (Process.GetProcessesByName("Cyberpunk2077").Length != 0)
            throw new InvalidOperationException("Cyberpunk is still running; its VR package remains active until the next recovery.");
        try { Park(); }
        finally { _disposed = true; _lock.Dispose(); }
    }

    public static string Resolve(string root, string relative)
    {
        return ModUpdateTransaction.Resolve(root, relative);
    }
}
