namespace VrClient.Core;

/// Resolves the VRClient repo root (the dir holding config/modpacks/repo.modpack.json).
/// Order: VRCLIENT_REPO_ROOT env var, walk up from the app base dir (dev builds run
/// from bin/ under the repo), walk up from the current dir (published exe run from
/// inside the repo). A published exe outside the repo needs the env var.
public static class RepoRoot
{
    public static string? Find()
    {
        var fromEnv = Environment.GetEnvironmentVariable("VRCLIENT_REPO_ROOT");
        if (!string.IsNullOrEmpty(fromEnv) && IsRoot(fromEnv))
            return Path.TrimEndingDirectorySeparator(fromEnv);

        return WalkUp(AppContext.BaseDirectory) ?? WalkUp(Environment.CurrentDirectory);
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
