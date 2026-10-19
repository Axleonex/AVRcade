using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Modpack;

namespace VrClient.Core.Redengine;

public sealed record CyberpunkUpstreamRelease(string Url, string Sha256,
    IReadOnlyList<CyberpunkBackendFile> LegacyFiles);

/// Downloads the author's release without redistributing it in AVRcade. Package
/// contents are staged before any older files are retired from the game folder.
public static class CyberpunkUpstreamInstaller
{
    public static async Task<IReadOnlyList<CyberpunkInstalledFile>> InstallAsync(
        string gameRoot, CyberpunkBackendPackage package, IHttpDownloader downloader,
        CancellationToken ct)
    {
        var release = package.UpstreamRelease!;
        var bytes = await downloader.GetBytesAsync(release.Url, ct);
        if (!Hashing.Sha256OfBytes(bytes).Equals(release.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new HashMismatchException("CyberpunkVR Port release archive SHA-256 did not match the author's pinned release.");
        using var archive = new ZipArchive(new MemoryStream(bytes), ZipArchiveMode.Read);
        var entries = archive.Entries.Where(e => e.Name.Length != 0)
            .ToDictionary(e => e.FullName.Replace('\\', '/'), StringComparer.OrdinalIgnoreCase);
        var staged = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in package.Files)
        {
            CyberpunkUpstreamSession.Resolve(gameRoot, file.Path);
            if (!entries.TryGetValue(file.Path.Replace('\\', '/'), out var entry))
                throw new InvalidDataException($"Author's release is missing {file.Path}");
            using var source = entry.Open();
            using var content = new MemoryStream();
            source.CopyTo(content);
            var data = content.ToArray();
            if (!Hashing.Sha256OfBytes(data).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new HashMismatchException($"CyberpunkVR Port file hash mismatch: {file.Path}");
            staged.Add(file.Path, data);
        }

        var store = CyberpunkUpstreamSession.Store(gameRoot);
        Directory.CreateDirectory(store);
        var previousState = CyberpunkUpstreamSession.ReadState(gameRoot);
        using var initialLock = previousState is null
            ? new FileStream(Path.Combine(store, "session.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)
            : null;
        using var lease = previousState is null ? null : new CyberpunkUpstreamSession(gameRoot, vr: false);
        var candidates = package.Files.Concat(release.LegacyFiles)
            .GroupBy(f => f.Path, StringComparer.OrdinalIgnoreCase).ToArray();
        // Refuse unknown modifications rather than silently replacing a Vortex deployment.
        foreach (var group in candidates)
        {
            var current = CyberpunkUpstreamSession.Resolve(gameRoot, group.Key);
            if (File.Exists(current) && !IsCameraSelection(group.Key, current) && !group.Any(f =>
                    Hashing.Sha256OfFile(current).Equals(f.Sha256, StringComparison.OrdinalIgnoreCase)))
                throw new IOException($"The existing {group.Key} differs from the known AVRcade/upstream package. Preserve it in Vortex and disable that VR package before updating through AVRcade.");
        }

        Directory.CreateDirectory(store);
        var previous = previousState?.PreviousFiles.ToList() ?? [];
        var retired = new List<string>();
        var added = new List<CyberpunkInstalledFile>();
        var changedPayload = new Dictionary<string, byte[]?>();
        const string settingsRelative = "bin/x64/vrport.ini";
        var settings = CyberpunkUpstreamSession.Resolve(gameRoot, settingsRelative);
        var migratedSettings = false;
        try
        {
            foreach (var (relative, data) in staged)
            {
                var destination = CyberpunkUpstreamSession.PayloadPath(gameRoot, relative);
                changedPayload[destination] = File.Exists(destination) ? File.ReadAllBytes(destination) : null;
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.WriteAllBytes(destination, data);
                added.Add(new CyberpunkInstalledFile(
                    $"{CyberpunkUpstreamSession.StoreDirectory}/payload/{relative}",
                    Hashing.Sha256OfBytes(data), null, null));
            }
            foreach (var group in candidates)
            {
                var current = CyberpunkUpstreamSession.Resolve(gameRoot, group.Key);
                if (!File.Exists(current)) continue;
                var backup = CyberpunkUpstreamSession.Resolve(Path.Combine(store, "previous"), group.Key);
                if (File.Exists(backup))
                    throw new IOException($"An earlier migration backup already exists for {group.Key}. Nothing will overwrite it.");
                Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                File.Move(current, backup);
                retired.Add(group.Key);
                previous.Add(group.Key);
            }
            if (previousState is null && File.Exists(settings))
            {
                // Old diagnostic flags (notably xr_mono_submit=1) must not
                // override the new renderer's defaults. Keep personal offsets
                // and turning, and let the author's first-launch preset run.
                var kept = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
                {
                    "xr_head_offset_x", "xr_head_offset_y", "xr_head_offset_z",
                    "xr_snap_turn", "xr_snap_turn_angle_deg", "show_launcher"
                };
                var preferences = File.ReadAllLines(settings).Where(line =>
                    kept.Contains(line.Split('=', 2)[0].Trim())).ToArray();
                var backup = CyberpunkUpstreamSession.Resolve(Path.Combine(store, "previous"), settingsRelative);
                if (File.Exists(backup)) throw new IOException("An earlier vrport.ini migration backup already exists.");
                Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                File.Move(settings, backup);
                retired.Add(settingsRelative);
                previous.Add(settingsRelative);
                migratedSettings = true;
                File.WriteAllLines(settings, preferences.Append("first_launch=1"));
            }
            AtomicFile.WriteAllText(CyberpunkUpstreamSession.StatePath(gameRoot), JsonSerializer.Serialize(
                new CyberpunkUpstreamSession.InstallState(package.Version,
                    staged.Keys.Concat(previousState?.RequirementFiles ?? []).Distinct(StringComparer.OrdinalIgnoreCase).ToArray(),
                    previous, previousState?.RequirementFiles)));
        }
        catch
        {
            if (migratedSettings && File.Exists(settings)) File.Delete(settings);
            foreach (var relative in retired.AsEnumerable().Reverse())
                File.Move(CyberpunkUpstreamSession.Resolve(Path.Combine(store, "previous"), relative),
                    CyberpunkUpstreamSession.Resolve(gameRoot, relative));
            foreach (var (path, old) in changedPayload)
                if (old is null) File.Delete(path); else File.WriteAllBytes(path, old);
            throw;
        }
        return added;
    }

    private static bool IsCameraSelection(string relative, string current)
    {
        if (!relative.Replace('\\', '/').Equals(
                "bin/x64/plugins/cyber_engine_tweaks/mods/CyberpunkVRPort_Stereo/vrcam.json",
                StringComparison.OrdinalIgnoreCase)) return false;
        // The upstream launcher rewrites this selection during normal use.
        // Preserve the old choice in the migration backup, not in the new catalogue.
        try
        {
            using var json = JsonDocument.Parse(File.ReadAllText(current));
            return json.RootElement.TryGetProperty("component", out var component) &&
                component.ValueKind == JsonValueKind.String &&
                component.GetString()!.StartsWith("vrcam_", StringComparison.Ordinal);
        }
        catch (JsonException) { return false; }
    }

    public static void RestorePrevious(string gameRoot)
    {
        var state = CyberpunkUpstreamSession.ReadState(gameRoot);
        if (state is null) return;
        using var lease = new CyberpunkUpstreamSession(gameRoot, vr: false);
        foreach (var relative in state.PreviousFiles)
        {
            var current = CyberpunkUpstreamSession.Resolve(gameRoot, relative);
            var backup = CyberpunkUpstreamSession.Resolve(Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), "previous"), relative);
            if (relative.Replace('\\', '/').Equals("bin/x64/vrport.ini", StringComparison.OrdinalIgnoreCase) && File.Exists(current))
                File.Move(current, Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), $"last-vrport-{Guid.NewGuid():N}.ini"));
            if (File.Exists(current)) throw new IOException($"Cannot restore the older package over {relative}.");
            if (File.Exists(backup))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(current)!);
                File.Move(backup, current);
            }
        }
        File.Delete(CyberpunkUpstreamSession.StatePath(gameRoot));
    }
}
