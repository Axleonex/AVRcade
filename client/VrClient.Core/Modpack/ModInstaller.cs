namespace VrClient.Core.Modpack;
using System.IO.Compression;
using System.Text.Json;
using System.Text;
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
        if (File.Exists(Path.Combine(gameDir, ".avrcade", "mod-updates", "pending.json")))
            return new InstallResult(false, Array.Empty<string>(),
                "An interrupted mod update needs recovery. Choose Recheck setup before reinstalling this conversion.");
        var expectedExes = executableNames is { Count: > 0 } ? executableNames : new[] { "REPO.exe" };
        if (!expectedExes.Any(name => File.Exists(SafeGamePath(gameDir, name))))
            return new InstallResult(false, Array.Empty<string>(),
                $"game directory has none of [{string.Join(", ", expectedExes)}]: {gameDir}");

        var leasePath = ModUpdateTransaction.Resolve(gameDir, ".avrcade/mod-updates/update.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(leasePath)!);
        using var lease = new FileStream(leasePath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var manifestPath = SafeGamePath(gameDir, ManifestRelativePath);
        var owned = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string[]? previousRuntime = null;
        if (File.Exists(manifestPath))
        {
            using var previous = JsonDocument.Parse(File.ReadAllText(manifestPath));
            owned.UnionWith(previous.RootElement.GetProperty("files").EnumerateArray().Select(item => item.GetString()!));
            if (previous.RootElement.TryGetProperty("preexisting_runtime_files", out var prior))
                previousRuntime = prior.Deserialize<string[]>() ?? [];
        }
        var writtenFiles = new List<string>();
        var changes = new Dictionary<string, byte[]?>(StringComparer.OrdinalIgnoreCase);
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

                relative = relative.Replace('\\', '/');
                if (File.Exists(destination) && !owned.Contains(relative))
                    throw new InvalidOperationException($"Another mod or manager already supplies {relative}. Use its VR profile or remove that deployment before installing this game's VR conversion.");
                using var stream = entry.Open();
                using var bytes = new MemoryStream();
                stream.CopyTo(bytes);
                changes[relative] = File.Exists(destination) &&
                    Path.GetExtension(relative).ToLowerInvariant() is ".cfg" or ".ini" or ".json"
                    ? File.ReadAllBytes(destination) : bytes.ToArray();
                writtenFiles.Add(relative);
            }
        }

        // Big Walk's preloader copies these payloads into the game root on
        // first launch. Remember files that were already there before it runs.
        var preexistingRuntimeFiles = previousRuntime?.ToList() ?? writtenFiles
            .Where(path => path.StartsWith(BigWalkRuntimePrefix, StringComparison.OrdinalIgnoreCase))
            .Select(path => path[BigWalkRuntimePrefix.Length..])
            .Where(path => File.Exists(SafeGamePath(gameDir, path)))
            .ToList();
        changes[ManifestRelativePath] = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(
            new Dictionary<string, object?>
            {
                ["files"] = writtenFiles,
                ["preexisting_runtime_files"] = preexistingRuntimeFiles
            },
            new JsonSerializerOptions { WriteIndented = true }));
        ModUpdateTransaction.Apply(gameDir, changes, () => { });

        return new InstallResult(true, writtenFiles, null);
    }

    /// Remove exactly the files listed in the install manifest, then the manifest.
    public void Uninstall(string gameDir)
    {
        var manifestPath = SafeGamePath(gameDir, ManifestRelativePath);
        if (!File.Exists(manifestPath))
            return;
        var leasePath = ModUpdateTransaction.Resolve(gameDir, ".avrcade/mod-updates/update.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(leasePath)!);
        using var lease = new FileStream(leasePath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        if (File.Exists(ModUpdateTransaction.Resolve(gameDir, ".avrcade/mod-updates/pending.json")))
            throw new InvalidOperationException("Choose Recheck setup to recover the interrupted update before removing this conversion.");
        var changes = new Dictionary<string, byte[]?>(StringComparer.OrdinalIgnoreCase);
        using (var doc = JsonDocument.Parse(File.ReadAllText(manifestPath)))
        {
            var files = doc.RootElement.GetProperty("files").EnumerateArray()
                .Select(item => item.GetString()!).ToList();
            var preexisting = doc.RootElement.TryGetProperty("preexisting_runtime_files", out var prior)
                ? prior.EnumerateArray().Select(item => item.GetString()!)
                    .ToHashSet(StringComparer.OrdinalIgnoreCase)
                : new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            // Validate the entire removal plan before touching any file. Directory
            // junctions/symlinks can otherwise redirect cleanup to another game/disk.
            foreach (var file in files)
            {
                _ = SafeGamePath(gameDir, file);
                if (file.StartsWith(BigWalkRuntimePrefix, StringComparison.OrdinalIgnoreCase))
                    _ = SafeGamePath(gameDir, file[BigWalkRuntimePrefix.Length..]);
            }
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
                    changes[generated] = null;
            }
            foreach (var file in files)
            {
                var candidate = SafeGamePath(gameDir, file);
                if (File.Exists(candidate)) changes[file] = null;
            }
        }
        changes[ManifestRelativePath] = null;
        ModUpdateTransaction.Apply(gameDir, changes, () => { });
    }

    /// True iff winhttp.dll exists next to the game exe AND BepInEx/plugins contains
    /// at least one plugin dll AND the install manifest exists. Mod-name-agnostic
    /// (data-driven â€” works for any BepInEx VR mod, not just RepoXR/LCVR).
    public bool IsInstalled(string gameDir)
    {
        try
        {
            if (File.Exists(Path.Combine(gameDir, ".avrcade", "mod-updates", "pending.json")))
                return false;
            if (!File.Exists(SafeGamePath(gameDir, "winhttp.dll")))
                return false;
            if (!File.Exists(SafeGamePath(gameDir, ManifestRelativePath)))
                return false;
            var pluginsDir = SafeGamePath(gameDir, "BepInEx/plugins");
            if (!Directory.Exists(pluginsDir))
                return false;
            return Directory.EnumerateFiles(pluginsDir, "*.dll", new EnumerationOptions
                { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint }).Any();
        }
        catch (Exception ex) when (ex is IOException or InvalidOperationException or UnauthorizedAccessException) { return false; }
    }

    private static string SafeGamePath(string gameDir, string relative)
    {
        try { return ModUpdateTransaction.Resolve(gameDir, relative); }
        catch (InvalidDataException ex)
        { throw new InvalidOperationException($"file path escapes the game directory or crosses a link: {relative}", ex); }
    }
}
