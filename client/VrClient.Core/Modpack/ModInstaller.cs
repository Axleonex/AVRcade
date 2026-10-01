namespace VrClient.Core.Modpack;
using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Model;

public sealed class ModInstaller
{
    private const string ManifestRelativePath = "BepInEx/vrclient-install-manifest.json";
    private const string BigWalkRuntimePrefix = "BepInEx/patchers/BigWalkVR.Runtime/";

    /// Extract each verified zip into gameDir. For a BepInExPack zip, the inner
    /// "BepInExPack/" folder's CONTENTS map to gameDir root (so winhttp.dll lands
    /// next to the game exe); all other zips extract with their paths relative to
    /// gameDir (they already start with "BepInEx/"). Records every written file
    /// (gameDir-relative) into <gameDir>/BepInEx/vrclient-install-manifest.json.
    /// Refuses (returns Installed=false, RefusalReason set) if gameDir contains none
    /// of the expected executables. executableNames comes from the game's config
    /// (data-driven, per-game); when null/empty it defaults to ["REPO.exe"] so M1
    /// R.E.P.O. callers are unchanged.
    // ponytail: game-dir install, add profile isolation when multi-variant is needed
    // ponytail: two known packages; add general placement only when M3 needs arbitrary mods
    public InstallResult Install(
        IReadOnlyList<string> verifiedZipPaths, string gameDir, IReadOnlyList<string>? executableNames = null)
    {
        var expectedExes = executableNames is { Count: > 0 } ? executableNames : new[] { "REPO.exe" };
        if (!expectedExes.Any(name => File.Exists(Path.Combine(gameDir, name))))
            return new InstallResult(false, Array.Empty<string>(),
                $"game directory has none of [{string.Join(", ", expectedExes)}]: {gameDir}");

        var writtenFiles = new List<string>();
        foreach (var zipPath in verifiedZipPaths)
        {
            using var archive = ZipFile.OpenRead(zipPath);
            // Thunderstore loaders use either BepInExPack/ or a game-specific
            // wrapper such as BepInExPack_PEAK/. Other mod archives may put
            // plugins/ and patchers/ directly at their root.
            var packPrefix = archive.Entries
                .Select(e => e.FullName.Replace('\\', '/').Split('/')[0])
                .FirstOrDefault(name => name.StartsWith("BepInExPack", StringComparison.OrdinalIgnoreCase));
            foreach (var entry in archive.Entries)
            {
                if (entry.FullName.EndsWith("/", StringComparison.Ordinal) ||
                    entry.FullName.EndsWith("\\", StringComparison.Ordinal))
                    continue; // directory entry

                var entryPath = entry.FullName.Replace('\\', '/');
                string relative;
                if (packPrefix is not null)
                {
                    // Only the loader wrapper's contents map to game root;
                    // Thunderstore wrapper metadata outside it is not installed.
                    var prefix = packPrefix + "/";
                    if (!entryPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                        continue;
                    relative = entryPath[prefix.Length..];
                }
                else
                {
                    // Package metadata is not a game file. Both recognized
                    // mod layouts are placed under BepInEx in the game root.
                    if (entryPath.StartsWith("BepInEx/", StringComparison.OrdinalIgnoreCase))
                        relative = entryPath;
                    else if (entryPath.StartsWith("plugins/", StringComparison.OrdinalIgnoreCase) ||
                             entryPath.StartsWith("patchers/", StringComparison.OrdinalIgnoreCase))
                        relative = "BepInEx/" + entryPath;
                    else
                        continue;
                }
                if (relative.Length == 0)
                    continue;

                // Zip-Slip guard: reject any entry whose normalized path escapes gameDir.
                var destination = SafeGamePath(gameDir, relative);

                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                entry.ExtractToFile(destination, overwrite: true);
                writtenFiles.Add(relative.Replace('\\', '/'));
            }
        }

        var manifestPath = Path.Combine(gameDir, ManifestRelativePath);
        // Big Walk's preloader copies these payloads into the game root on
        // first launch. Remember files that were already there before it runs.
        var preexistingRuntimeFiles = writtenFiles
            .Where(path => path.StartsWith(BigWalkRuntimePrefix, StringComparison.OrdinalIgnoreCase))
            .Select(path => path[BigWalkRuntimePrefix.Length..])
            .Where(path => File.Exists(SafeGamePath(gameDir, path)))
            .ToList();
        Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
        File.WriteAllText(manifestPath, JsonSerializer.Serialize(
            new Dictionary<string, object?>
            {
                ["files"] = writtenFiles,
                ["preexisting_runtime_files"] = preexistingRuntimeFiles
            },
            new JsonSerializerOptions { WriteIndented = true }));

        return new InstallResult(true, writtenFiles, null);
    }

    /// Remove exactly the files listed in the install manifest, then the manifest.
    public void Uninstall(string gameDir)
    {
        var manifestPath = Path.Combine(gameDir, ManifestRelativePath);
        if (!File.Exists(manifestPath))
            return;
        using (var doc = JsonDocument.Parse(File.ReadAllText(manifestPath)))
        {
            var files = doc.RootElement.GetProperty("files").EnumerateArray()
                .Select(item => item.GetString()!).ToList();
            var preexisting = doc.RootElement.TryGetProperty("preexisting_runtime_files", out var prior)
                ? prior.EnumerateArray().Select(item => item.GetString()!)
                    .ToHashSet(StringComparer.OrdinalIgnoreCase)
                : new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in files.Where(path => path.StartsWith(BigWalkRuntimePrefix,
                         StringComparison.OrdinalIgnoreCase)))
            {
                var generated = file[BigWalkRuntimePrefix.Length..];
                if (preexisting.Contains(generated))
                    continue;
                var source = SafeGamePath(gameDir, file);
                var destination = SafeGamePath(gameDir, generated);
                if (File.Exists(source) && File.Exists(destination) &&
                    Hashing.Sha256OfFile(source) == Hashing.Sha256OfFile(destination))
                    File.Delete(destination);
            }
            foreach (var file in files)
            {
                var candidate = SafeGamePath(gameDir, file);
                if (File.Exists(candidate))
                    File.Delete(candidate);
            }
        }
        File.Delete(manifestPath);
    }

    /// True iff winhttp.dll exists next to the game exe AND BepInEx/plugins contains
    /// at least one plugin dll AND the install manifest exists. Mod-name-agnostic
    /// (data-driven — works for any BepInEx VR mod, not just RepoXR/LCVR).
    public bool IsInstalled(string gameDir)
    {
        if (!File.Exists(Path.Combine(gameDir, "winhttp.dll")))
            return false;
        if (!File.Exists(Path.Combine(gameDir, ManifestRelativePath)))
            return false;
        var pluginsDir = Path.Combine(gameDir, "BepInEx", "plugins");
        if (!Directory.Exists(pluginsDir))
            return false;
        return Directory.EnumerateFiles(pluginsDir, "*.dll", SearchOption.AllDirectories).Any();
    }

    private static string SafeGamePath(string gameDir, string relative)
    {
        var root = Path.GetFullPath(gameDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var destination = Path.GetFullPath(Path.Combine(root, relative));
        if (Path.IsPathRooted(relative) ||
            !destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException($"file path escapes the game directory: {relative}");
        return destination;
    }
}
