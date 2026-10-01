namespace VrClient.Core;

public static class AtomicFile
{
    /// Replace a small settings file in one step, so a crash or power loss mid-write
    /// leaves the previous file intact instead of a truncated one.
    public static void WriteAllText(string path, string contents)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, contents);
        File.Move(temporary, path, overwrite: true);
    }
}
