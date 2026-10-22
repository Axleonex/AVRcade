using System.Text.Json;
using VrClient.Core.Modpack;

namespace VrClient.Core.Compatibility;

/// Durable file parking, also used for clean monitor sessions. Restoration is explicit
/// and refuses foreign changes. The journal survives closing AVRcade or a game crash.
public sealed class MonitorModSession
{
    public const string JournalPath = ".avrcade/monitor-session/pending.json";
    public sealed record Entry(string Path, string Backup, string Sha256);
    public sealed record Journal(IReadOnlyList<Entry> Entries);

    public static bool IsParked(string root) => File.Exists(ModUpdateTransaction.Resolve(root, JournalPath));

    public static FileStream AcquireUpdateLease(string root)
    {
        var path = ModUpdateTransaction.Resolve(root, ".avrcade/mod-updates/update.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }

    public static int Park(string root, IEnumerable<string> relativePaths, Action ensureClosed, bool updateLeaseHeld = false)
    {
        ensureClosed();
        using var updateLease = updateLeaseHeld ? null : AcquireUpdateLease(root);
        var journalPath = ModUpdateTransaction.Resolve(root, JournalPath);
        Directory.CreateDirectory(Path.GetDirectoryName(journalPath)!);
        using var lease = new FileStream(Path.Combine(Path.GetDirectoryName(journalPath)!, "session.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var files = new List<string>();
        foreach (var relative in relativePaths.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            var full = ModUpdateTransaction.Resolve(root, relative);
            if (Directory.Exists(full)) AddDirectory(root, full, files);
            else if (File.Exists(full)) files.Add(relative.Replace('\\', '/'));
        }
        if (File.Exists(journalPath))
        {
            if (files.Count > 0) throw new IOException("Mods were deployed while the monitor session was parked. Restore or reconcile the parked files before starting another clean monitor session.");
            var previous = Read(journalPath);
            foreach (var entry in previous.Entries)
            {
                var backup = ModUpdateTransaction.Resolve(root, entry.Backup);
                if (!File.Exists(backup) || Hashing.Sha256OfFile(backup) != entry.Sha256)
                    throw new IOException("A parked mod backup is missing or changed: " + entry.Path);
            }
            return previous.Entries.Count;
        }
        var prefix = ".avrcade/monitor-session/" + Guid.NewGuid().ToString("N") + "/";
        var entries = files.Distinct(StringComparer.OrdinalIgnoreCase).Select(relative => new Entry(relative,
            prefix + relative, Hashing.Sha256OfFile(ModUpdateTransaction.Resolve(root, relative)))).ToArray();
        if (entries.Length == 0) return 0;
        foreach (var entry in entries) ModUpdateTransaction.Resolve(root, entry.Backup);
        AtomicFile.WriteAllText(journalPath, JsonSerializer.Serialize(new Journal(entries)));
        try
        {
            foreach (var entry in entries)
            {
                ensureClosed();
                var source = ModUpdateTransaction.Resolve(root, entry.Path);
                if (Hashing.Sha256OfFile(source) != entry.Sha256) throw new IOException($"{entry.Path} changed during monitor preparation.");
                var backup = ModUpdateTransaction.Resolve(root, entry.Backup);
                Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                File.Move(source, backup);
            }
            return entries.Length;
        }
        catch { RestoreCore(root, journalPath, ensureClosed); throw; }
    }

    public static int Restore(string root, Action ensureClosed, bool updateLeaseHeld = false)
    {
        ensureClosed();
        var path = ModUpdateTransaction.Resolve(root, JournalPath);
        if (!File.Exists(path)) return 0;
        using var updateLease = updateLeaseHeld ? null : AcquireUpdateLease(root);
        using var lease = new FileStream(Path.Combine(Path.GetDirectoryName(path)!, "session.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        return RestoreCore(root, path, ensureClosed);
    }

    private static Journal Read(string path) => JsonSerializer.Deserialize<Journal>(File.ReadAllText(path))
        ?? throw new InvalidDataException("The parked mod journal is unreadable.");
    private static int RestoreCore(string root, string path, Action ensureClosed)
    {
        var entries = Read(path).Entries;
        foreach (var entry in entries)
        {
            var target = ModUpdateTransaction.Resolve(root, entry.Path);
            var backup = ModUpdateTransaction.Resolve(root, entry.Backup);
            if (File.Exists(target) && Hashing.Sha256OfFile(target) != entry.Sha256)
                throw new IOException($"{entry.Path} changed while mods were parked. Both files were preserved; resolve the conflict before restoring.");
            if (File.Exists(backup) && Hashing.Sha256OfFile(backup) != entry.Sha256)
                throw new IOException($"The parked backup changed: {entry.Path}");
            if (!File.Exists(backup) && !File.Exists(target)) throw new IOException($"The parked backup is missing: {entry.Path}");
        }
        foreach (var entry in entries.Reverse())
        {
            ensureClosed();
            var target = ModUpdateTransaction.Resolve(root, entry.Path);
            var backup = ModUpdateTransaction.Resolve(root, entry.Backup);
            if (File.Exists(target) && Hashing.Sha256OfFile(target) != entry.Sha256)
                throw new IOException("A mod changed during restoration: " + entry.Path);
            if (!File.Exists(target)) { Directory.CreateDirectory(Path.GetDirectoryName(target)!); File.Move(backup, target); }
        }
        File.Delete(path);
        return entries.Count;
    }
    private static void AddDirectory(string root, string directory, List<string> files)
    {
        if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("A mod folder is a directory link.");
        foreach (var file in Directory.EnumerateFiles(directory))
        {
            var relative = Path.GetRelativePath(root, file);
            ModUpdateTransaction.Resolve(root, relative);
            files.Add(relative.Replace('\\', '/'));
        }
        foreach (var child in Directory.EnumerateDirectories(directory)) AddDirectory(root, child, files);
    }
}
