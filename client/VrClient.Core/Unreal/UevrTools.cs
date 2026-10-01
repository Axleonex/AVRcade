namespace VrClient.Core.Unreal;
using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Modpack;

/// ECO-2 / T-Unreal: UEVR orchestration data layer. UEVR is "All rights
/// reserved" — the pinned release is downloaded by the USER'S machine from
/// praydog's official GitHub release at fetch time and verified against the
/// sha256 we pinned from the official sidecar. Never bundled, never
/// redistributed, never vendored.
public sealed record UevrReleasePin(
    string Channel, string Tag, string DownloadUrl, string Sha256, string InjectorExe);

public sealed record UevrProfileFile(
    string Path, string Sha256, string ContentType, string Disposition);

public sealed record UevrProfileSource(
    string Repository, string Revision, string ProfileId, string TreeUrl,
    string License, string Redistribution, IReadOnlyList<UevrProfileFile> Files);

public sealed record UevrCapabilities(
    string HeadsetStereo, string HeadTracking, string Gamepad,
    string MotionControls, string PrivateCoop);

public sealed record UevrGame(
    string Slug, string GameId, string DisplayName, string SteamAppId,
    string Channel, string LauncherStub, string ShippingBinaryRelative,
    bool InjectionStable, bool HeadsetT1Verified, bool ProfileVerified,
    UevrProfileSource? ProfileSource = null,
    UevrCapabilities? Capabilities = null,
    IReadOnlyList<string>? SupportedBuilds = null);

public sealed record UevrProfileImportResult(
    string Destination, string ManifestPath, string? BackupDirectory, int FileCount);

public sealed record UevrProfileManifestEntry(
    string RelativePath, string InstalledSha256, string? BackupRelativePath);

public sealed record UevrProfileManifest(
    string GameSlug, string SourceRevision, string ImportedAtUtc,
    string? BackupDirectory, IReadOnlyList<UevrProfileManifestEntry> Files);

public sealed record DotNetDesktopPin(
    string Version,
    string RuntimeDownloadUrl, string RuntimeSha512,
    string DesktopDownloadUrl, string DesktopSha512);

public static class UevrTools
{
    /// Load the pinned release for a channel from config/unreal/uevr-release.json.
    public static UevrReleasePin LoadReleasePin(string releaseJsonPath, string channel)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(releaseJsonPath));
        var root = doc.RootElement;
        var injector = root.GetProperty("injector").GetProperty("exe").GetString()!;
        if (channel.Equals("nightly", StringComparison.OrdinalIgnoreCase))
        {
            var n = root.GetProperty("nightly_channel");
            return new UevrReleasePin("nightly",
                n.GetProperty("pinned_tag").GetString()!,
                n.GetProperty("download_url").GetString()!,
                n.GetProperty("sha256").GetString()!,
                injector);
        }
        return new UevrReleasePin("stable",
            root.GetProperty("pinned_tag").GetString()!,
            root.GetProperty("download_url").GetString()!,
            root.GetProperty("sha256").GetString()!,
            injector);
    }

    /// Load a T-Unreal game entry from config/unreal/<slug>.uevr.json.
    public static UevrGame LoadGame(string uevrGameJsonPath)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(uevrGameJsonPath));
        var root = doc.RootElement;
        var roles = root.GetProperty("executable_roles");
        var profile = root.TryGetProperty("uevr_profile", out var profileElement)
            ? profileElement : default;
        var source = profile.ValueKind is JsonValueKind.Object &&
            profile.TryGetProperty("source", out var sourceElement)
            ? ReadProfileSource(sourceElement) : null;
        var capabilities = profile.ValueKind is JsonValueKind.Object &&
            profile.TryGetProperty("capabilities", out var capabilitiesElement)
            ? new UevrCapabilities(
                ReadString(capabilitiesElement, "headset_stereo", "unknown"),
                ReadString(capabilitiesElement, "head_tracking", "unknown"),
                ReadString(capabilitiesElement, "gamepad", "unknown"),
                ReadString(capabilitiesElement, "motion_controls", "unknown"),
                ReadString(capabilitiesElement, "private_coop", "unknown"))
            : null;
        var supportedBuilds = profile.ValueKind is JsonValueKind.Object &&
            profile.TryGetProperty("supported_builds", out var builds) &&
            builds.ValueKind is JsonValueKind.Array
            ? builds.EnumerateArray().Select(item => item.GetString()!).ToArray()
            : [];
        return new UevrGame(
            root.GetProperty("game_slug").GetString()!,
            root.GetProperty("game_id").GetString()!,
            root.GetProperty("display_name").GetString()!,
            root.GetProperty("steam_app_id").GetString()!,
            root.TryGetProperty("uevr_channel", out var c) ? c.GetString()! : "stable",
            roles.GetProperty("launcher_stub").GetString()!,
            roles.GetProperty("shipping_binary").GetString()!,
            ReadVerification(root, "injection_stable"),
            ReadVerification(root, "headset_t1"),
            root.TryGetProperty("verification", out var verification) &&
                verification.TryGetProperty("profile_status", out var status) &&
                status.GetString() == "verified",
            source, capabilities, supportedBuilds);
    }

    private static UevrProfileSource ReadProfileSource(JsonElement source)
    {
        var files = source.TryGetProperty("files", out var fileElements)
            ? fileElements.EnumerateArray().Select(file => new UevrProfileFile(
                file.GetProperty("path").GetString()!,
                file.GetProperty("sha256").GetString()!,
                file.GetProperty("content_type").GetString()!,
                file.GetProperty("disposition").GetString()!)).ToArray()
            : [];
        return new UevrProfileSource(
            source.GetProperty("repository").GetString()!,
            source.GetProperty("revision").GetString()!,
            source.GetProperty("profile_id").GetString()!,
            source.GetProperty("tree_url").GetString()!,
            ReadString(source, "license", "not_declared"),
            ReadString(source, "redistribution", "user_import_only"),
            files);
    }

    private static string ReadString(JsonElement element, string name, string fallback)
        => element.TryGetProperty(name, out var value) && value.ValueKind is JsonValueKind.String
            ? value.GetString()! : fallback;

    private static bool ReadVerification(JsonElement root, string name)
        => root.TryGetProperty("verification", out var verification) &&
           verification.TryGetProperty(name, out var value) &&
           value.ValueKind is JsonValueKind.True;

    public static DotNetDesktopPin LoadDotNetDesktopPin(string pinJsonPath)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(pinJsonPath));
        var root = doc.RootElement;
        return new DotNetDesktopPin(
            root.GetProperty("version").GetString()!,
            root.GetProperty("runtime_download_url").GetString()!,
            root.GetProperty("runtime_sha512").GetString()!,
            root.GetProperty("desktop_download_url").GetString()!,
            root.GetProperty("desktop_sha512").GetString()!);
    }

    /// The process name (no extension) the injector must attach to.
    public static string ShippingProcessName(UevrGame game)
        => Path.GetFileNameWithoutExtension(game.ShippingBinaryRelative);

    /// Per-tag tool dir under the user's local app data — outside the game dir,
    /// outside the repo.
    public static string ToolsDir(string tag)
        => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "vrclient", "tools", "uevr", tag);

    public static string InjectorPath(UevrReleasePin pin, string? toolsDir = null)
        => Path.Combine(toolsDir ?? ToolsDir(pin.Tag), pin.InjectorExe);

    public static bool IsFetched(UevrReleasePin pin, string? toolsDir = null)
        => File.Exists(InjectorPath(pin, toolsDir));

    public static string DotNetToolsDir(DotNetDesktopPin pin)
        => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "vrclient", "tools", "dotnet", pin.Version);

    public static string DotNetHostPath(DotNetDesktopPin pin, string? toolsDir = null)
        => Path.Combine(toolsDir ?? DotNetToolsDir(pin), "dotnet.exe");

    public static bool IsDotNetFetched(DotNetDesktopPin pin, string? toolsDir = null)
    {
        var root = toolsDir ?? DotNetToolsDir(pin);
        return File.Exists(DotNetHostPath(pin, root)) &&
               Directory.Exists(Path.Combine(
                   root, "shared", "Microsoft.WindowsDesktop.App", pin.Version));
    }

    public static UevrProfileImportResult ImportProfileArchive(
        UevrGame game, string archivePath, string destination)
    {
        var source = game.ProfileSource ?? throw new InvalidOperationException("profile_source_missing");
        if (!source.Redistribution.Equals("user_import_only", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("profile_import_mode_not_supported");
        if (!File.Exists(archivePath))
            throw new FileNotFoundException("Profile archive not found.", archivePath);

        var expected = source.Files
            .Where(file => file.Disposition.Equals("import", StringComparison.OrdinalIgnoreCase))
            .ToDictionary(file => NormalizeRelativePath(file.Path), StringComparer.OrdinalIgnoreCase);
        if (expected.Count == 0) throw new InvalidDataException("profile_expected_files_empty");

        var parent = Directory.GetParent(Path.GetFullPath(destination))?.FullName ??
            throw new InvalidOperationException("profile_destination_has_no_parent");
        Directory.CreateDirectory(parent);
        var stage = Path.Combine(parent, $".vrclient-profile-stage-{Guid.NewGuid():N}");
        var backup = Directory.Exists(destination)
            ? Path.Combine(parent, $".vrclient-profile-backup-{DateTime.UtcNow:yyyyMMddHHmmssfff}") : null;
        var applied = new List<UevrProfileManifestEntry>();

        try
        {
            Directory.CreateDirectory(stage);
            using (var archive = ZipFile.OpenRead(archivePath))
            {
                var files = archive.Entries.Where(entry => !string.IsNullOrEmpty(entry.Name)).ToArray();
                const long maxFileBytes = 2 * 1024 * 1024;
                const long maxArchiveBytes = 8 * 1024 * 1024;
                if (files.Any(entry => entry.Length > maxFileBytes) || files.Sum(entry => entry.Length) > maxArchiveBytes)
                    throw new InvalidDataException("profile_archive_too_large");
                foreach (var entry in files)
                {
                    var relative = NormalizeRelativePath(entry.FullName);
                    if (!expected.TryGetValue(relative, out var pinned))
                        throw new InvalidDataException($"profile_unexpected_file:{relative}");
                    if (!IsReviewedProfileContent(pinned.ContentType, relative))
                        throw new InvalidDataException($"profile_unreviewed_content:{relative}");
                    using var stream = entry.Open();
                    using var memory = new MemoryStream();
                    stream.CopyTo(memory);
                    var bytes = memory.ToArray();
                    var actual = Hashing.Sha256OfBytes(bytes);
                    if (!actual.Equals(pinned.Sha256, StringComparison.OrdinalIgnoreCase))
                        throw new HashMismatchException(
                            $"Profile sha256 mismatch for {relative}: expected={pinned.Sha256} actual={actual}");
                    var stagedPath = SafeCombine(stage, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(stagedPath)!);
                    File.WriteAllBytes(stagedPath, bytes);
                }
                var missing = expected.Keys.Except(files.Select(entry => NormalizeRelativePath(entry.FullName)),
                    StringComparer.OrdinalIgnoreCase).ToArray();
                if (missing.Length > 0)
                    throw new InvalidDataException($"profile_missing_files:{string.Join(',', missing)}");
            }

            Directory.CreateDirectory(destination);
            foreach (var pinned in expected.Values)
            {
                var relative = NormalizeRelativePath(pinned.Path);
                var target = SafeCombine(destination, relative);
                string? backupRelative = null;
                if (File.Exists(target))
                {
                    backupRelative = relative;
                    var backupPath = SafeCombine(backup!, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
                    File.Copy(target, backupPath, overwrite: false);
                }
                applied.Add(new UevrProfileManifestEntry(relative, pinned.Sha256, backupRelative));
            }

            var manifest = new UevrProfileManifest(
                game.Slug, source.Revision, DateTime.UtcNow.ToString("O"), backup, applied);
            var manifestPath = Path.Combine(destination, ".vrclient-profile-manifest.json");
            WriteManifestAtomically(manifestPath, manifest);
            foreach (var entry in applied)
            {
                var target = SafeCombine(destination, entry.RelativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.Copy(SafeCombine(stage, entry.RelativePath), target, overwrite: true);
            }
            return new UevrProfileImportResult(destination, manifestPath, backup, applied.Count);
        }
        catch
        {
            RestoreAppliedFiles(destination, backup, applied);
            var failedManifest = Path.Combine(destination, ".vrclient-profile-manifest.json");
            if (File.Exists(failedManifest)) File.Delete(failedManifest);
            throw;
        }
        finally
        {
            if (Directory.Exists(stage)) Directory.Delete(stage, recursive: true);
        }
    }

    public static async Task<UevrProfileImportResult> DownloadAndImportProfileAsync(
        UevrGame game, IHttpDownloader downloader, string destination,
        CancellationToken ct = default)
    {
        var source = game.ProfileSource ?? throw new InvalidOperationException("profile_source_missing");
        if (!source.Redistribution.Equals("user_import_only", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("profile_download_mode_not_supported");
        if (!Uri.TryCreate(source.Repository, UriKind.Absolute, out var repository) ||
            repository.Scheme != Uri.UriSchemeHttps ||
            !repository.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("profile_repository_invalid");
        var repositoryParts = repository.AbsolutePath.Trim('/').Split('/');
        if (repositoryParts.Length != 2 ||
            repositoryParts.Any(part => !IsSafeUrlSegment(part)) ||
            source.Revision.Length != 40 ||
            !source.Revision.All(Uri.IsHexDigit) ||
            !Guid.TryParse(source.ProfileId, out _))
            throw new InvalidDataException("profile_source_identity_invalid");

        var pinnedFiles = source.Files
            .Where(file => file.Disposition.Equals("import", StringComparison.OrdinalIgnoreCase))
            .ToArray();
        if (pinnedFiles.Length == 0)
            throw new InvalidDataException("profile_expected_files_empty");

        var downloaded = new List<(string Path, byte[] Bytes)>();
        long totalBytes = 0;
        foreach (var file in pinnedFiles)
        {
            ct.ThrowIfCancellationRequested();
            var relative = NormalizeRelativePath(file.Path);
            if (!IsReviewedProfileContent(file.ContentType, relative))
                throw new InvalidDataException($"profile_unreviewed_content:{relative}");
            var segments = relative.Split('/').Select(Uri.EscapeDataString);
            var url = $"https://raw.githubusercontent.com/{repositoryParts[0]}/{repositoryParts[1]}/{source.Revision}/{source.ProfileId}/{string.Join('/', segments)}";
            var bytes = await downloader.GetBytesAsync(url, ct);
            totalBytes += bytes.Length;
            if (bytes.Length > 2 * 1024 * 1024 || totalBytes > 8 * 1024 * 1024)
                throw new InvalidDataException("profile_download_too_large");
            if (!Hashing.Sha256OfBytes(bytes).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new HashMismatchException($"Profile sha256 mismatch for {relative}");
            downloaded.Add((relative, bytes));
        }

        var temporary = Path.Combine(Path.GetTempPath(), $"vrclient-profile-{Guid.NewGuid():N}.zip");
        try
        {
            using (var archive = ZipFile.Open(temporary, ZipArchiveMode.Create))
            {
                foreach (var file in downloaded)
                {
                    var entry = archive.CreateEntry(file.Path);
                    using var stream = entry.Open();
                    stream.Write(file.Bytes);
                }
            }
            return ImportProfileArchive(game, temporary, destination);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static bool IsSafeUrlSegment(string segment) =>
        segment.Length > 0 && segment.All(ch =>
            char.IsAsciiLetterOrDigit(ch) || ch is '-' or '_' or '.');

    public static bool IsImportedProfileValid(UevrGame game, string destination)
    {
        var source = game.ProfileSource;
        var manifestPath = Path.Combine(destination, ".vrclient-profile-manifest.json");
        if (source is null || !File.Exists(manifestPath))
            return false;

        try
        {
            var manifest = JsonSerializer.Deserialize<UevrProfileManifest>(
                File.ReadAllText(manifestPath));
            if (manifest is null ||
                !manifest.GameSlug.Equals(game.Slug, StringComparison.OrdinalIgnoreCase) ||
                !manifest.SourceRevision.Equals(source.Revision, StringComparison.OrdinalIgnoreCase))
                return false;

            var expected = source.Files
                .Where(file => file.Disposition.Equals("import", StringComparison.OrdinalIgnoreCase))
                .ToDictionary(file => NormalizeRelativePath(file.Path), StringComparer.OrdinalIgnoreCase);
            if (manifest.Files.Count != expected.Count)
                return false;

            foreach (var entry in manifest.Files)
            {
                var relative = NormalizeRelativePath(entry.RelativePath);
                if (!expected.TryGetValue(relative, out var pinned) ||
                    !entry.InstalledSha256.Equals(pinned.Sha256, StringComparison.OrdinalIgnoreCase))
                    return false;
                var installed = SafeCombine(destination, relative);
                if (!File.Exists(installed) ||
                    !Hashing.Sha256OfFile(installed).Equals(pinned.Sha256, StringComparison.OrdinalIgnoreCase))
                    return false;
            }
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   JsonException or InvalidDataException)
        {
            return false;
        }
    }

    public static int RollbackImportedProfile(string destination)
    {
        var manifestPath = Path.Combine(destination, ".vrclient-profile-manifest.json");
        if (!File.Exists(manifestPath))
            throw new FileNotFoundException("Profile import manifest not found.", manifestPath);
        var manifest = JsonSerializer.Deserialize<UevrProfileManifest>(File.ReadAllText(manifestPath)) ??
            throw new InvalidDataException("profile_manifest_invalid");
        if (manifest.BackupDirectory is not null)
        {
            var destinationParent = Directory.GetParent(Path.GetFullPath(destination))?.FullName ?? string.Empty;
            var backupFull = Path.GetFullPath(manifest.BackupDirectory);
            var backupParent = Directory.GetParent(backupFull)?.FullName ?? string.Empty;
            if (!backupParent.Equals(destinationParent, StringComparison.OrdinalIgnoreCase) ||
                !Path.GetFileName(backupFull).StartsWith(".vrclient-profile-backup-", StringComparison.Ordinal))
                throw new InvalidDataException("profile_manifest_backup_invalid");
        }
        var restored = 0;
        foreach (var entry in manifest.Files)
        {
            var target = SafeCombine(destination, entry.RelativePath);
            if (!File.Exists(target) ||
                !Hashing.Sha256OfFile(target).Equals(entry.InstalledSha256, StringComparison.OrdinalIgnoreCase))
                continue;
            if (entry.BackupRelativePath is not null && manifest.BackupDirectory is not null)
                File.Copy(SafeCombine(manifest.BackupDirectory, entry.BackupRelativePath), target, overwrite: true);
            else
                File.Delete(target);
            restored++;
        }
        File.Delete(manifestPath);
        return restored;
    }

    private static void RestoreAppliedFiles(
        string destination, string? backup, IEnumerable<UevrProfileManifestEntry> entries)
    {
        foreach (var entry in entries.Reverse())
        {
            var target = SafeCombine(destination, entry.RelativePath);
            if (entry.BackupRelativePath is not null && backup is not null)
                File.Copy(SafeCombine(backup, entry.BackupRelativePath), target, overwrite: true);
            else if (File.Exists(target)) File.Delete(target);
        }
    }

    private static void WriteManifestAtomically(string manifestPath, UevrProfileManifest manifest)
    {
        var temporary = manifestPath + ".tmp-" + Guid.NewGuid().ToString("N");
        try
        {
            File.WriteAllText(temporary, JsonSerializer.Serialize(manifest,
                new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporary, manifestPath, overwrite: true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }

    private static bool IsReviewedProfileContent(string contentType, string relativePath)
    {
        var extension = Path.GetExtension(relativePath);
        if (extension.Equals(".exe", StringComparison.OrdinalIgnoreCase) ||
            extension.Equals(".dll", StringComparison.OrdinalIgnoreCase) ||
            extension.Equals(".lua", StringComparison.OrdinalIgnoreCase)) return false;
        return contentType is "uevr_config" or "camera_config" or "imgui_config" or "documentation" or "metadata";
    }

    private static string NormalizeRelativePath(string path)
    {
        var normalized = path.Replace('\\', '/').TrimStart('/');
        if (Path.IsPathRooted(path) || normalized.Split('/').Any(part => part is ".." or ""))
            throw new InvalidDataException($"profile_path_invalid:{path}");
        return normalized;
    }

    private static string SafeCombine(string root, string relative)
    {
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var combined = Path.GetFullPath(Path.Combine(fullRoot, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!combined.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"profile_path_escape:{relative}");
        return combined;
    }

    public static async Task EnsureFetchedAsync(
        UevrReleasePin pin, IHttpDownloader downloader, bool force = false,
        string? toolsDir = null, CancellationToken ct = default)
    {
        var destination = toolsDir ?? ToolsDir(pin.Tag);
        if (!force && IsFetched(pin, destination))
            return;
        var bytes = await downloader.GetBytesAsync(pin.DownloadUrl, ct);
        var actual = Hashing.Sha256OfBytes(bytes);
        if (!actual.Equals(pin.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new HashMismatchException(
                $"UEVR sha256 mismatch: expected={pin.Sha256} actual={actual}");
        ExtractVerifiedZip(bytes, destination);
        if (!IsFetched(pin, destination))
            throw new InvalidDataException($"UEVR archive did not contain {pin.InjectorExe}");
    }

    public static async Task EnsureDotNetFetchedAsync(
        DotNetDesktopPin pin, IHttpDownloader downloader, bool force = false,
        string? toolsDir = null, CancellationToken ct = default)
    {
        var destination = toolsDir ?? DotNetToolsDir(pin);
        if (!force && IsDotNetFetched(pin, destination))
            return;
        var runtimeTask = downloader.GetBytesAsync(pin.RuntimeDownloadUrl, ct);
        var desktopTask = downloader.GetBytesAsync(pin.DesktopDownloadUrl, ct);
        await Task.WhenAll(runtimeTask, desktopTask);
        var runtimeBytes = await runtimeTask;
        var desktopBytes = await desktopTask;
        var actualRuntime = Hashing.Sha512OfBytes(runtimeBytes);
        if (!actualRuntime.Equals(pin.RuntimeSha512, StringComparison.OrdinalIgnoreCase))
            throw new HashMismatchException(
                $".NET Runtime sha512 mismatch: expected={pin.RuntimeSha512} actual={actualRuntime}");
        var actualDesktop = Hashing.Sha512OfBytes(desktopBytes);
        if (!actualDesktop.Equals(pin.DesktopSha512, StringComparison.OrdinalIgnoreCase))
            throw new HashMismatchException(
                $".NET Desktop Runtime sha512 mismatch: expected={pin.DesktopSha512} actual={actualDesktop}");
        ExtractVerifiedZip(runtimeBytes, destination);
        ExtractVerifiedZip(desktopBytes, destination);
        if (!IsDotNetFetched(pin, destination))
            throw new InvalidDataException(
                ".NET archives did not contain both dotnet.exe and Microsoft.WindowsDesktop.App");
    }

    private static void ExtractVerifiedZip(byte[] bytes, string destination)
    {
        Directory.CreateDirectory(destination);
        using var stream = new MemoryStream(bytes, writable: false);
        using var archive = new ZipArchive(stream, ZipArchiveMode.Read);
        archive.ExtractToDirectory(destination, overwriteFiles: true);
    }
}
