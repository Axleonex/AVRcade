using System.Text.Json;

namespace VrClient.Core.Modpack;

/// A game-local update journal. Only the explicitly listed files are changed.
public static class ModUpdateTransaction
{
    private const string Pending = ".avrcade/mod-updates/pending.json";
    private sealed record Entry(string Path, string Backup, bool Existed, string? Before, string? After);
    private sealed record Journal(IReadOnlyList<Entry> Entries);

    public static string Resolve(string root, string relative)
    {
        if (Path.IsPathRooted(relative) || relative.Contains(':') ||
            relative.Replace('\\', '/').Split('/').Any(part => part is "" or "." or ".."))
            throw new InvalidDataException("Update paths must be relative to the selected game.");
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var full = Path.GetFullPath(Path.Combine(prefix, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("An update path leaves its game folder.");
        for (var dir = Path.GetDirectoryName(full); dir is not null &&
            dir.StartsWith(prefix, StringComparison.OrdinalIgnoreCase); dir = Path.GetDirectoryName(dir))
            if (Directory.Exists(dir) && (File.GetAttributes(dir) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("An update path passes through a directory link.");
        if ((File.Exists(full) || Directory.Exists(full)) && (File.GetAttributes(full) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("An update target is a file link.");
        return full;
    }

    public static void Recover(string root)
    {
        var pending = Resolve(root, Pending);
        if (!File.Exists(pending)) return;
        var journal = JsonSerializer.Deserialize<Journal>(File.ReadAllText(pending))
            ?? throw new InvalidDataException("The interrupted update journal could not be read.");
        // Check every target before restoring any file. Preserve foreign edits.
        foreach (var entry in journal.Entries)
        {
            var target = Resolve(root, entry.Path);
            var current = File.Exists(target) ? Hashing.Sha256OfFile(target) : null;
            if (current != entry.Before && current != entry.After)
                throw new IOException($"{entry.Path} changed after an interrupted update. Its backup was preserved; resolve this conflict before updating.");
            if (entry.Existed && (!File.Exists(Resolve(root, entry.Backup)) ||
                Hashing.Sha256OfFile(Resolve(root, entry.Backup)) != entry.Before))
                throw new IOException($"The update backup is missing or changed: {entry.Path}");
        }
        foreach (var entry in journal.Entries.Reverse())
        {
            var target = Resolve(root, entry.Path);
            if (entry.Existed) Replace(target, File.ReadAllBytes(Resolve(root, entry.Backup)));
            else if (File.Exists(target)) File.Delete(target);
        }
        File.Delete(pending);
    }

    public static int Apply(string root, IReadOnlyDictionary<string, byte[]?> changes, Action ensureGameClosed)
    {
        ensureGameClosed();
        Recover(root);
        var transaction = ".avrcade/mod-updates/" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N");
        var entries = new List<Entry>();
        foreach (var (relative, data) in changes)
        {
            var target = Resolve(root, relative);
            var existed = File.Exists(target);
            var before = existed ? Hashing.Sha256OfFile(target) : null;
            var after = data is null ? null : Hashing.Sha256OfBytes(data);
            if (before == after) continue;
            var backup = transaction + "/backup/" + relative.Replace('\\', '/');
            if (existed)
            {
                var backupPath = Resolve(root, backup);
                Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
                File.Copy(target, backupPath);
            }
            entries.Add(new(relative, backup, existed, before, after));
        }
        if (entries.Count == 0) return 0;
        var pending = Resolve(root, Pending);
        AtomicFile.WriteAllText(pending, JsonSerializer.Serialize(new Journal(entries)));
        try
        {
            ensureGameClosed();
            foreach (var entry in entries)
            {
                var target = Resolve(root, entry.Path);
                var current = File.Exists(target) ? Hashing.Sha256OfFile(target) : null;
                if (current != entry.Before) throw new IOException($"A file changed while preparing the update: {entry.Path}");
                if (changes[entry.Path] is { } bytes) Replace(target, bytes);
                else if (File.Exists(target)) File.Delete(target);
            }
            File.Delete(pending);
            return entries.Count;
        }
        catch
        {
            Recover(root);
            throw;
        }
    }

    private static void Replace(string target, byte[] data)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        var temporary = target + ".avrcade-update-" + Guid.NewGuid().ToString("N");
        try { File.WriteAllBytes(temporary, data); File.Move(temporary, target, overwrite: true); }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
}
