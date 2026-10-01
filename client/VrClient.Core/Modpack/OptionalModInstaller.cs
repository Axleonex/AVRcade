using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Model;

namespace VrClient.Core.Modpack;

/// <summary>Installs additional Thunderstore BepInEx plugins without changing the pinned VR conversion.</summary>
public sealed class OptionalModInstaller
{
    public async Task<IReadOnlyList<string>> InstallAsync(string gameDir, Lockfile requested,
        Lockfile baseConversion, IHttpDownloader downloader, CancellationToken ct = default)
    {
        if (requested.GameSlug != baseConversion.GameSlug ||
            requested.Community != baseConversion.Community)
            throw new InvalidOperationException("The mod package does not match this game's VR conversion.");
        if (!new ModInstaller().IsInstalled(gameDir))
            throw new InvalidOperationException("Install AVRcade's VR conversion before adding optional mods.");

        var basePackages = baseConversion.Packages.ToDictionary(
            p => $"{p.Namespace}-{p.Name}", StringComparer.OrdinalIgnoreCase);
        var pending = new List<LockedPackage>();
        foreach (var package in requested.Packages)
        {
            var key = $"{package.Namespace}-{package.Name}";
            if (basePackages.TryGetValue(key, out var installed))
            {
                if (installed.Version != package.Version)
                    throw new InvalidOperationException($"{key} requires {package.Version}, but the VR conversion pins {installed.Version}.");
                continue;
            }
            pending.Add(package);
        }

        var manifestPath = Path.Combine(gameDir, "BepInEx", "vrclient-optional-mods.json");
        var previous = File.Exists(manifestPath)
            ? JsonSerializer.Deserialize<List<OptionalPackage>>(File.ReadAllText(manifestPath)) ?? []
            : new List<OptionalPackage>();
        var already = previous.ToDictionary(p => p.Key, StringComparer.OrdinalIgnoreCase);
        var prepared = new List<(LockedPackage Package, byte[] Bytes, List<(string Path, string ArchivePath)> Entries)>();
        foreach (var package in pending)
        {
            var key = $"{package.Namespace}-{package.Name}";
            if (!IsSafeIdentifier(package.Namespace) || !IsSafeIdentifier(package.Name))
                throw new InvalidOperationException($"Unsafe package identifier: {key}");
            if (already.TryGetValue(key, out var existing))
            {
                if (existing.Version != package.Version)
                    throw new InvalidOperationException($"{key} is already installed at {existing.Version}; remove it before changing versions.");
                continue;
            }
            if (!Uri.TryCreate(package.DownloadUrl, UriKind.Absolute, out var uri) ||
                uri.Scheme != Uri.UriSchemeHttps ||
                !(uri.Host.Equals("thunderstore.io", StringComparison.OrdinalIgnoreCase) ||
                  uri.Host.EndsWith(".thunderstore.io", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException($"Untrusted download URL for {key}.");
            if (package.SizeBytes <= 0 || package.SizeBytes > 100_000_000)
                throw new InvalidOperationException($"Unsupported archive size for {key}.");
            var bytes = await downloader.GetBytesAsync(package.DownloadUrl, ct);
            if (bytes.LongLength != package.SizeBytes || bytes.LongLength > 100_000_000)
                throw new InvalidOperationException($"Download size mismatch for {key}.");
            // Thunderstore's index supplies size but not a trustworthy digest. Record
            // the downloaded bytes' digest; never pretend it was an upstream pin.
            using var archive = new ZipArchive(new MemoryStream(bytes), ZipArchiveMode.Read);
            var entries = new List<(string, string)>();
            var destinations = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            long expanded = 0;
            foreach (var entry in archive.Entries)
            {
                if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) continue;
                var name = entry.FullName.Replace('\\', '/');
                if (name.StartsWith("BepInEx/plugins/", StringComparison.OrdinalIgnoreCase))
                    name = name["BepInEx/plugins/".Length..];
                else if (name.StartsWith("plugins/", StringComparison.OrdinalIgnoreCase))
                    name = name["plugins/".Length..];
                else if (name.Contains('/') || !name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
                {
                    if (name is "manifest.json" or "README.md" or "icon.png" || name.StartsWith("docs/", StringComparison.OrdinalIgnoreCase))
                        continue;
                    throw new InvalidOperationException($"{key} contains an unsupported game-root or patcher file: {name}");
                }
                if (string.IsNullOrWhiteSpace(name) || name.Split('/').Any(x => x is "" or "." or "..") ||
                    name.Contains(':') || name.StartsWith('/'))
                    throw new InvalidOperationException($"Unsafe path in {key}: {name}");
                if (!destinations.Add(name))
                    throw new InvalidOperationException($"Duplicate file in {key}: {name}");
                expanded += entry.Length;
                if (expanded > 250_000_000 || entries.Count >= 2000)
                    throw new InvalidOperationException($"Archive limit exceeded for {key}.");
                entries.Add((name, entry.FullName));
            }
            if (!entries.Any(e => e.Item1.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException($"{key} has no BepInEx plugin DLL.");
            prepared.Add((package, bytes, entries));
        }

        var written = new List<string>();
        var added = new List<OptionalPackage>();
        try
        {
            foreach (var item in prepared)
            {
                ct.ThrowIfCancellationRequested();
                using var archive = new ZipArchive(new MemoryStream(item.Bytes), ZipArchiveMode.Read);
                var key = $"{item.Package.Namespace}-{item.Package.Name}";
                var folder = Path.GetFullPath(Path.Combine(gameDir, "BepInEx", "plugins", key));
                var files = new List<string>();
                foreach (var (relative, archivePath) in item.Entries)
                {
                    var dest = Path.GetFullPath(Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar)));
                    if (!dest.StartsWith(folder + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ||
                        File.Exists(dest))
                        throw new InvalidOperationException($"Optional mod file already exists or escapes its package folder: {relative}");
                    Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                    archive.GetEntry(archivePath)!.ExtractToFile(dest);
                    written.Add(dest);
                    files.Add(Path.GetRelativePath(gameDir, dest).Replace('\\', '/'));
                }
                added.Add(new OptionalPackage(key, item.Package.Version,
                    Hashing.Sha256OfBytes(item.Bytes), files));
            }
            var updated = previous.Concat(added).ToList();
            Directory.CreateDirectory(Path.GetDirectoryName(manifestPath)!);
            File.WriteAllText(manifestPath, JsonSerializer.Serialize(updated,
                new JsonSerializerOptions { WriteIndented = true }));
            return added.Select(p => p.Key).ToArray();
        }
        catch
        {
            foreach (var file in written) File.Delete(file);
            throw;
        }
    }

    private sealed record OptionalPackage(string Key, string Version, string Sha256,
        IReadOnlyList<string> Files);

    private static bool IsSafeIdentifier(string value) => value.Length is > 0 and <= 100 &&
        value.All(c => char.IsAsciiLetterOrDigit(c) || c is '_' or '-');
}
