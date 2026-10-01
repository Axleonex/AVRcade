using System.Text.Json;

namespace VrClient.Core.Modpack;

/// <summary>Fail closed when a managed conversion is not the only BepInEx plugin set.</summary>
public static class ManagedModIsolation
{
    public static bool IsVrOnly(string gameDir)
    {
        try
        {
            var manifest = Path.Combine(gameDir, "BepInEx", "vrclient-install-manifest.json");
            if (!File.Exists(manifest) ||
                File.Exists(Path.Combine(gameDir, "BepInEx", "vrclient-optional-mods.json")))
                return false;
            using var document = JsonDocument.Parse(File.ReadAllText(manifest));
            var installed = document.RootElement.GetProperty("files").EnumerateArray()
                .Select(item => item.GetString()?.Replace('\\', '/'))
                .Where(path => path is not null)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            var bepinex = Path.Combine(gameDir, "BepInEx");
            var plugins = Path.Combine(bepinex, "plugins");
            if (!Directory.Exists(plugins)) return false;
            var scanned = new[] { plugins, Path.Combine(bepinex, "patchers") };
            var actual = scanned.Where(Directory.Exists)
                .SelectMany(folder => Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories))
                .Select(path => Path.GetRelativePath(gameDir, path).Replace('\\', '/'))
                .ToArray();
            return actual.Length > 0 && actual.All(installed.Contains);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or
                                   InvalidOperationException or ArgumentException)
        {
            return false;
        }
    }
}
