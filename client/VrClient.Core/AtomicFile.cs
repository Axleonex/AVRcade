namespace VrClient.Core;

public static class AtomicFile
{
    /// Replace a small settings file in one step, so a crash or power loss mid-write
    /// leaves the previous file intact instead of a truncated one.
    public static void WriteAllText(string path, string contents)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        // Serialize only this settings path across application copies. Different
        // games/settings remain independent, with no persistent lock files.
        var identity = Hashing.Sha256OfBytes(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(path).ToUpperInvariant()));
        using var lease = new Mutex(false, "Local\\AVRcade.Settings." + identity);
        try
        {
            if (!lease.WaitOne(TimeSpan.FromSeconds(5)))
                throw new IOException("This setting is being updated in another AVRcade window. Try again.");
        }
        catch (AbandonedMutexException) { /* The previous writer exited; its destination remains atomic. */ }
        try
        {
            var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            var owned = false;
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    owned = true;
                    using var writer = new StreamWriter(stream, new System.Text.UTF8Encoding(false));
                    writer.Write(contents);
                }
                File.Move(temporary, path, overwrite: true);
            }
            finally { if (owned && File.Exists(temporary)) File.Delete(temporary); }
        }
        finally { lease.ReleaseMutex(); }
    }
}
