namespace VrClient.Core;

/// Branded user settings with non-destructive compatibility for earlier installs.
public static class AppDataPaths
{
    private static readonly object MigrationLock = new();
    public static string Local(string relative) => Resolve(Environment.SpecialFolder.LocalApplicationData, relative);
    public static string Roaming(string relative) => Resolve(Environment.SpecialFolder.ApplicationData, relative);

    private static string Resolve(Environment.SpecialFolder folder, string relative)
    {
        var baseDirectory = Environment.GetFolderPath(folder);
        var target = Path.Combine(baseDirectory, "AVRcade", relative);
        var legacy = Path.Combine(baseDirectory, "VRClient", relative);
        lock (MigrationLock)
        {
            if (File.Exists(legacy) && !File.Exists(target)) CopyMissing(legacy, target);
            // Controller preferences contain small JSON files. Profile/tool directories
            // can contain absolute references and large payloads; reuse those in place.
            if (Directory.Exists(legacy))
            {
                if (relative == "controllers") CopyDirectory(legacy, target);
                else if (!Directory.Exists(target)) return legacy;
            }
        }
        return target;
    }

    private static void CopyMissing(string source, string target)
    {
        if (File.Exists(target)) return;
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        var temporary = target + ".migration-" + Guid.NewGuid().ToString("N");
        try
        {
            File.Copy(source, temporary);
            try { File.Move(temporary, target, overwrite: false); }
            catch (IOException) when (File.Exists(target)) { }
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }

    private static void CopyDirectory(string source, string target)
    {
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0) return;
        foreach (var file in Directory.EnumerateFiles(source))
            if ((File.GetAttributes(file) & FileAttributes.ReparsePoint) == 0)
                CopyMissing(file, Path.Combine(target, Path.GetFileName(file)));
        foreach (var directory in Directory.EnumerateDirectories(source))
            CopyDirectory(directory, Path.Combine(target, Path.GetFileName(directory)));
    }
}
