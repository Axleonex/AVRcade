using System.Text;
using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Model;

namespace VrClient.Core.Modpack;

/// Resolve a fresh dependency graph without modifying AVRcade's shipped lockfiles.
public static class ThunderstoreModUpdater
{
    private const string Manifest = "BepInEx/vrclient-install-manifest.json";

    public static async Task<ActionOutcome> UpdateAsync(string repoRoot, GameView game,
        IThunderstoreApi api, IHttpDownloader downloader, Action ensureGameClosed,
        IProgress<string>? progress = null, CancellationToken ct = default, Lockfile? reviewedLock = null)
    {
        var root = game.InstallDir ?? throw new InvalidOperationException("Install the game first.");
        var manifestPath = ModUpdateTransaction.Resolve(root, Manifest);
        if (!File.Exists(manifestPath) && reviewedLock is null) return new(false, "Install this game's VR mod through AVRcade first. Mod-manager profiles are updated in their manager.");
        var spec = ModpackResolver.LoadSpec(Path.Combine(repoRoot, "config", "modpacks", game.Slug + ".modpack.json"));
        if (spec.GameSlug != game.Slug) throw new InvalidDataException("The mod specification belongs to another game.");
        using var oldManifest = JsonDocument.Parse(File.Exists(manifestPath) ? File.ReadAllText(manifestPath) : "{\"files\":[]}");
        var owned = oldManifest.RootElement.GetProperty("files").EnumerateArray()
            .Select(item => item.GetString()!.Replace('\\', '/')).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var path in owned) ValidatePackagePath(root, path);

        progress?.Report("Checking current mod releases and their dependencies...");
        if (reviewedLock is not null)
        {
            if (reviewedLock.GameSlug != game.Slug) throw new InvalidDataException("The reviewed package belongs to another game.");
            LockfileIo.RequireHashesFilled(reviewedLock);
        }
        var resolved = reviewedLock ?? await new ModpackResolver(api).ResolveAsync(spec, DateTime.UtcNow.ToString("O"), ct);
        if (oldManifest.RootElement.TryGetProperty("packages", out var previousPackages))
        {
            var recorded = previousPackages.Deserialize<LockedPackage[]>(new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? [];
            foreach (var next in resolved.Packages)
            {
                var previous = recorded.FirstOrDefault(package => package.Namespace == next.Namespace && package.Name == next.Name);
                if (previous is not null && Version.TryParse(previous.Version, out var before) &&
                    Version.TryParse(next.Version, out var after) && after < before)
                    throw new InvalidDataException($"The current package index would downgrade {next.Name} from {previous.Version} to {next.Version}. Nothing was updated.");
            }
        }
        var temporaryRoot = Path.Combine(Path.GetTempPath(), "avrcade-mod-update-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryRoot);
        try
        {
            var packages = new List<LockedPackage>();
            var zips = new List<string>();
            var index = 0;
            foreach (var package in resolved.Packages)
            {
                if (!Uri.TryCreate(package.DownloadUrl, UriKind.Absolute, out var uri) || uri.Scheme != "https" ||
                    !(uri.Host == "thunderstore.io" || uri.Host.EndsWith(".thunderstore.io", StringComparison.OrdinalIgnoreCase)))
                    throw new InvalidDataException("A mod download is outside the configured Thunderstore source.");
                progress?.Report($"Downloading {package.Name} {package.Version} ({++index}/{resolved.Packages.Count})...");
                var bytes = await downloader.GetBytesAsync(package.DownloadUrl, ct);
                if (reviewedLock is not null && !Hashing.Sha256OfBytes(bytes).Equals(package.Sha256, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException($"Reviewed package hash mismatch: {package.Name}. Nothing was installed.");
                if (package.SizeBytes > 0 && bytes.LongLength != package.SizeBytes)
                    throw new InvalidDataException($"Download size mismatch: {package.Name}");
                var pinned = package with { Sha256 = Hashing.Sha256OfBytes(bytes) };
                var zip = Path.Combine(temporaryRoot, index + ".zip");
                File.WriteAllBytes(zip, bytes);
                if (Hashing.Sha256OfFile(zip) != pinned.Sha256) throw new IOException("The staged mod archive changed.");
                packages.Add(pinned);
                zips.Add(zip);
            }

            var executableNames = ReadExecutables(repoRoot, game.Slug);
            if (!executableNames.Any(name => File.Exists(ModUpdateTransaction.Resolve(root, name))))
                throw new InvalidDataException("The selected folder no longer contains this game's executable.");
            var staging = Path.Combine(temporaryRoot, "stage");
            Directory.CreateDirectory(staging);
            var placeholder = ModUpdateTransaction.Resolve(staging, executableNames[0]);
            Directory.CreateDirectory(Path.GetDirectoryName(placeholder)!);
            File.WriteAllBytes(placeholder, []);
            var install = new ModInstaller().Install(zips, staging, executableNames);
            if (!install.Installed || !install.InstalledFiles.Any(path => path.StartsWith("BepInEx/plugins/", StringComparison.OrdinalIgnoreCase) && path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("The downloaded package does not contain a VR plugin.");
            var incoming = install.InstalledFiles.Select(path => path.Replace('\\', '/')).ToHashSet(StringComparer.OrdinalIgnoreCase);
            var changes = new Dictionary<string, byte[]?>(StringComparer.OrdinalIgnoreCase);
            foreach (var path in incoming)
            {
                ValidatePackagePath(root, path);
                var target = ModUpdateTransaction.Resolve(root, path);
                var source = ModUpdateTransaction.Resolve(staging, path);
                if (File.Exists(target) && IsPreference(path)) continue;
                if (File.Exists(target) && !owned.Contains(path) && Hashing.Sha256OfFile(target) != Hashing.Sha256OfFile(source))
                    throw new IOException($"Another mod owns {path}. Resolve that conflict before updating.");
                changes[path] = File.ReadAllBytes(source);
            }
            foreach (var path in owned.Except(incoming, StringComparer.OrdinalIgnoreCase))
                if (!IsPreference(path)) changes[path] = null;

            // Keep custom configuration bytes and the original preloader ownership record.
            var finalFiles = incoming.Concat(owned.Where(IsPreference)).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            object priorRuntime = oldManifest.RootElement.TryGetProperty("preexisting_runtime_files", out var prior)
                ? prior.Deserialize<string[]>() ?? [] : Array.Empty<string>();
            var updated = new Dictionary<string, object?>
            {
                ["files"] = finalFiles,
                ["preexisting_runtime_files"] = priorRuntime,
                ["packages"] = packages,
                ["updated_at_utc"] = DateTime.UtcNow.ToString("O")
            };
            changes[Manifest] = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(updated, new JsonSerializerOptions { WriteIndented = true }));
            ct.ThrowIfCancellationRequested();
            progress?.Report("Installing updated VR components; keeping your configuration and controller preferences...");
            var changed = ModUpdateTransaction.Apply(root, changes, ensureGameClosed);
            return new(true, changed > 1
                ? $"Updated {string.Join(", ", packages.Select(package => package.Name + " " + package.Version))}. Configuration and controller preferences kept."
                : "No newer mod package files were found. Current release versions were checked.");
        }
        finally
        {
            var full = Path.GetFullPath(temporaryRoot);
            var parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (full.StartsWith(parent, StringComparison.OrdinalIgnoreCase) && Path.GetFileName(full).StartsWith("avrcade-mod-update-", StringComparison.Ordinal) && Directory.Exists(full))
                Directory.Delete(full, recursive: true);
        }
    }

    private static bool IsPreference(string path) => path.StartsWith("BepInEx/config/", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".cfg", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".ini", StringComparison.OrdinalIgnoreCase);

    private static void ValidatePackagePath(string root, string path)
    {
        ModUpdateTransaction.Resolve(root, path);
        if (!path.StartsWith("BepInEx/", StringComparison.OrdinalIgnoreCase) && !path.StartsWith("doorstop_libs/", StringComparison.OrdinalIgnoreCase) &&
            path != "winhttp.dll" && path != "doorstop_config.ini" &&
            !(path.IndexOf('/') < 0 && (path.EndsWith(".txt", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".md", StringComparison.OrdinalIgnoreCase))))
            throw new InvalidDataException($"A package contains an unsupported game-root file: {path}");
    }

    private static string[] ReadExecutables(string root, string slug)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "config", "games", slug + ".json")));
        return doc.RootElement.GetProperty("executable_names").EnumerateArray().Select(item => item.GetString()!).ToArray();
    }
}
