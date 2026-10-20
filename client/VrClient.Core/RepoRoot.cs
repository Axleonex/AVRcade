namespace VrClient.Core;

/// Resolves the AVRcade configuration root (the dir holding config/modpacks/repo.modpack.json).
/// Order: explicit AVRCADE_REPO_ROOT, configuration beside the executable, legacy
/// VRCLIENT_REPO_ROOT, then walk up from the executable and current directories.
/// Installed copies keep their bundled configuration even when a legacy development
/// environment variable is still set. Development builds retain the legacy fallback.
public static class RepoRoot
{
    public static string? Find() => Find(AppContext.BaseDirectory, Environment.CurrentDirectory,
        Environment.GetEnvironmentVariable("AVRCADE_REPO_ROOT"), Environment.GetEnvironmentVariable("VRCLIENT_REPO_ROOT"));

    public static string? Find(string executableDirectory, string currentDirectory, string? overrideRoot, string? legacyRoot)
    {
        var fromEnv = overrideRoot;
        if (!string.IsNullOrEmpty(fromEnv) && IsRoot(fromEnv))
            return Path.TrimEndingDirectorySeparator(fromEnv);

        var besideExecutable = Path.TrimEndingDirectorySeparator(executableDirectory);
        if (IsRoot(besideExecutable)) return besideExecutable;

        fromEnv = legacyRoot;
        if (!string.IsNullOrEmpty(fromEnv) && IsRoot(fromEnv))
            return Path.TrimEndingDirectorySeparator(fromEnv);

        return WalkUp(executableDirectory) ?? WalkUp(currentDirectory);
    }

    private static string? WalkUp(string start)
    {
        var dir = Path.TrimEndingDirectorySeparator(start);
        while (!string.IsNullOrEmpty(dir))
        {
            if (IsRoot(dir))
                return dir;
            dir = Path.GetDirectoryName(dir);
        }
        return null;
    }

    private static bool IsRoot(string dir) =>
        File.Exists(Path.Combine(dir, "config", "modpacks", "repo.modpack.json"));
}
