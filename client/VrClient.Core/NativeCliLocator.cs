namespace VrClient.Core;

/// Locates a built native helper CLI (M5's vrclient_safety_cli / vrclient_sign_cli).
/// Shared so the safety cross-check and the pack verifier resolve the same way.
public static class NativeCliLocator
{
    /// Resolve a native CLI exe: the `envVar` override first, else the release
    /// `native` directory under the repo root, then the development build outputs.
    /// Returns null when the helper is not present.
    /// NOTE: the probe is repo-root-relative (found by walking up from the app base
    /// dir), NOT relative to the app's bin dir — the native build lands at the repo
    /// root, so a bin-relative probe would never match.
    public static string? Locate(string exeName, string envVar)
    {
        var fromEnv = Environment.GetEnvironmentVariable(envVar);
        if (!string.IsNullOrEmpty(fromEnv) && File.Exists(fromEnv))
            return fromEnv;

        var root = FindRepoRoot();
        if (root is null)
            return null;

        foreach (var dir in new[]
        {
            "native",
            Path.Combine("build", "ci"),
            Path.Combine("build", "Release"),
            "build"
        })
        {
            var candidate = Path.Combine(root, dir, exeName);
            if (File.Exists(candidate))
                return candidate;
        }
        return null;
    }

    private static string? FindRepoRoot() => RepoRoot.Find();
}
