namespace VrClient.Core.Redengine;

using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.Modpack;

public sealed record CyberpunkBackendFile(string Path, string Sha256);

/// A modding framework the backend needs. It is downloaded from its maintainer's
/// own release, never bundled; `Marker` is the file that says it is already there.
public sealed record CyberpunkFramework(
    string Name, string Version, string DownloadUrl, string ArchiveSha256, string Marker);

public sealed record CyberpunkBackendPackage(
    string Version,
    IReadOnlyList<CyberpunkBackendFile> Files,
    IReadOnlyList<string> Directories,
    IReadOnlyList<CyberpunkFramework> Frameworks);

/// `Framework` names the framework a file came from; null for the VR backend's own files.
public sealed record CyberpunkInstalledFile(
    string Path, string Sha256, string? Framework, string? BackupPath);

public sealed record CyberpunkInstallRecord(
    string GameRoot, string BackendVersion, string InstalledAtUtc,
    IReadOnlyList<CyberpunkInstalledFile> Files);

public sealed record CyberpunkInstallPlan(
    IReadOnlyList<CyberpunkFramework> ToDownload,
    IReadOnlyList<CyberpunkFramework> AlreadyPresent,
    int BackendFilesToCopy,
    int BackendFilesToReplace);

public sealed record CyberpunkBackendOutcome(bool Ok, string Message);

/// Puts AVRcade's Cyberpunk 2077 VR backend into a game folder and takes it out
/// again. It never overwrites a framework the player already has, backs up any
/// backend file it replaces, and removes only files that are still byte-identical
/// to what it installed. Game-build checks belong to CyberpunkNativePreflight.
public sealed class CyberpunkBackendInstaller
{
    private readonly string _payloadDir;
    private readonly string _recordPath;
    private readonly string _backupRoot;
    private readonly Func<bool> _gameRunning;
    private IReadOnlyList<string>? _missingPayload;

    public CyberpunkBackendPackage Package { get; }

    public CyberpunkBackendInstaller(
        string packagePath,
        string payloadDir,
        string? stateDir = null,
        Func<bool>? gameRunning = null)
    {
        Package = LoadPackage(packagePath);
        _payloadDir = payloadDir;
        stateDir ??= Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient");
        _recordPath = Path.Combine(stateDir, "cyberpunk-2077-vr-install.json");
        _backupRoot = Path.Combine(stateDir, "cyberpunk-2077-vr-backup");
        _gameRunning = gameRunning ?? (() => Process.GetProcessesByName("Cyberpunk2077").Length > 0);
    }

    public static CyberpunkBackendPackage LoadPackage(string packagePath)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(packagePath));
        var root = document.RootElement;
        var backend = root.GetProperty("backend");
        return new CyberpunkBackendPackage(
            backend.GetProperty("version").GetString()!,
            backend.GetProperty("files").EnumerateArray().Select(file => new CyberpunkBackendFile(
                file.GetProperty("path").GetString()!,
                file.GetProperty("sha256").GetString()!)).ToArray(),
            backend.GetProperty("directories").EnumerateArray().Select(item => item.GetString()!).ToArray(),
            root.GetProperty("frameworks").EnumerateArray().Select(item => new CyberpunkFramework(
                item.GetProperty("name").GetString()!,
                item.GetProperty("version").GetString()!,
                item.GetProperty("download_url").GetString()!,
                item.GetProperty("archive_sha256").GetString()!,
                item.GetProperty("marker").GetString()!)).ToArray());
    }

    /// Backend files this copy of AVRcade does not carry, or carries with the wrong
    /// contents. Hashing is done once; the payload does not change while the app runs.
    public IReadOnlyList<string> MissingPayloadFiles() => _missingPayload ??= Package.Files
        .Where(file =>
        {
            var source = Resolve(_payloadDir, file.Path);
            return !File.Exists(source) || !HashEquals(Hashing.Sha256OfFile(source), file.Sha256);
        })
        .Select(file => file.Path)
        .ToArray();

    public bool PayloadReady => MissingPayloadFiles().Count == 0;

    public CyberpunkInstallRecord? ReadRecord()
    {
        if (!File.Exists(_recordPath))
            return null;
        try
        {
            return JsonSerializer.Deserialize<CyberpunkInstallRecord>(File.ReadAllText(_recordPath));
        }
        catch (JsonException)
        {
            return null;
        }
    }

    public CyberpunkInstallPlan Plan(string gameRoot)
    {
        var present = Package.Frameworks
            .Where(framework => File.Exists(Resolve(gameRoot, framework.Marker)))
            .ToArray();
        var copy = 0;
        var replace = 0;
        foreach (var file in Package.Files)
        {
            var target = Resolve(gameRoot, file.Path);
            if (!File.Exists(target)) copy++;
            else if (!HashEquals(Hashing.Sha256OfFile(target), file.Sha256)) replace++;
        }
        return new CyberpunkInstallPlan(Package.Frameworks.Except(present).ToArray(), present, copy, replace);
    }

    public async Task<CyberpunkBackendOutcome> InstallAsync(
        string gameRoot, IHttpDownloader downloader, CancellationToken ct = default)
    {
        if (!Directory.Exists(gameRoot))
            return new(false, "The Cyberpunk 2077 folder was not found.");
        if (_gameRunning())
            return new(false, "Close Cyberpunk 2077 before installing the VR backend.");
        if (!PayloadReady)
            return new(false,
                $"This copy of AVRcade does not include the complete Cyberpunk VR backend ({MissingPayloadFiles().Count} file(s) missing or changed). Nothing was installed.");

        var plan = Plan(gameRoot);

        // Every download is verified before the first file is written, so a bad
        // or interrupted download leaves the game folder exactly as it was.
        var archives = new List<(CyberpunkFramework Framework, byte[] Bytes)>();
        foreach (var framework in plan.ToDownload)
        {
            ct.ThrowIfCancellationRequested();
            var bytes = await downloader.GetBytesAsync(framework.DownloadUrl, ct);
            var actual = Hashing.Sha256OfBytes(bytes);
            if (!HashEquals(actual, framework.ArchiveSha256))
                throw new HashMismatchException(
                    $"{framework.Name} {framework.Version} sha256 mismatch: expected={framework.ArchiveSha256} actual={actual}");
            archives.Add((framework, bytes));
        }

        var previous = ReadRecord();
        var installed = new List<CyberpunkInstalledFile>();
        var backupDir = Path.Combine(_backupRoot, DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff"));
        try
        {
            foreach (var (framework, bytes) in archives)
                ExtractFramework(framework, bytes, gameRoot, installed);

            foreach (var file in Package.Files)
            {
                var target = Resolve(gameRoot, file.Path);
                string? backup = null;
                if (File.Exists(target) && !HashEquals(Hashing.Sha256OfFile(target), file.Sha256))
                {
                    backup = Resolve(backupDir, file.Path);
                    Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                    File.Copy(target, backup, overwrite: false);
                }
                // Record before copying: a failed copy is then undone like any other step.
                installed.Add(new CyberpunkInstalledFile(file.Path, file.Sha256, null, backup));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.Copy(Resolve(_payloadDir, file.Path), target, overwrite: true);
            }

            foreach (var directory in Package.Directories)
                Directory.CreateDirectory(Resolve(gameRoot, directory));

            WriteRecord(gameRoot, MergeWithPrevious(previous, gameRoot, installed));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            Undo(gameRoot, installed);
            return new(false, $"Cyberpunk VR backend install failed and was rolled back: {ex.Message}");
        }

        var summary = $"Cyberpunk VR backend {Package.Version} installed.";
        if (archives.Count > 0)
            summary += $" Downloaded and verified: {string.Join(", ", archives.Select(a => $"{a.Framework.Name} {a.Framework.Version}"))}.";
        if (plan.AlreadyPresent.Count > 0)
            summary += $" Kept your existing {string.Join(", ", plan.AlreadyPresent.Select(f => f.Name))}.";
        if (plan.BackendFilesToReplace > 0)
            summary += $" {plan.BackendFilesToReplace} older VR file(s) were backed up and replaced.";
        return new(true, summary);
    }

    /// Removes the VR backend's own files. The frameworks stay unless asked for,
    /// because other mods the player installed afterwards may depend on them.
    public CyberpunkBackendOutcome Uninstall(string gameRoot, bool removeFrameworks = false)
    {
        var record = ReadRecord();
        if (record is null || !SamePath(record.GameRoot, gameRoot))
            return new(false, "AVRcade has no install record for the VR backend in this game folder, so it removed nothing.");
        if (_gameRunning())
            return new(false, "Close Cyberpunk 2077 before removing the VR backend.");

        var removed = 0;
        var changed = 0;
        var kept = new List<CyberpunkInstalledFile>();
        try
        {
            foreach (var file in record.Files.Reverse())
            {
                if (file.Framework is not null && !removeFrameworks)
                {
                    kept.Add(file);
                    continue;
                }
                var target = Resolve(gameRoot, file.Path);
                if (!File.Exists(target))
                    continue;
                if (!HashEquals(Hashing.Sha256OfFile(target), file.Sha256))
                {
                    changed++;
                    continue;
                }
                if (file.BackupPath is not null && File.Exists(file.BackupPath))
                    File.Copy(file.BackupPath, target, overwrite: true);
                else
                {
                    File.Delete(target);
                    RemoveEmptyParents(gameRoot, target);
                }
                removed++;
            }
            foreach (var directory in Package.Directories)
                RemoveEmptyDirectories(gameRoot, Resolve(gameRoot, directory));

            kept.Reverse();
            if (kept.Count > 0)
                WriteRecord(gameRoot, kept);
            else
                File.Delete(_recordPath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return new(false, $"Could not finish removing the Cyberpunk VR backend: {ex.Message}");
        }

        var summary = $"Removed {removed} Cyberpunk VR file(s).";
        if (changed > 0)
            summary += $" {changed} file(s) changed since install were left in place.";
        if (kept.Count > 0)
            summary += " The modding frameworks (RED4ext, Cyber Engine Tweaks and others) were left in place for your other mods.";
        return new(true, summary);
    }

    private static void ExtractFramework(
        CyberpunkFramework framework, byte[] bytes, string gameRoot, List<CyberpunkInstalledFile> installed)
    {
        using var stream = new MemoryStream(bytes, writable: false);
        using var archive = new ZipArchive(stream, ZipArchiveMode.Read);
        foreach (var entry in archive.Entries.Where(entry => !string.IsNullOrEmpty(entry.Name)))
        {
            var target = Resolve(gameRoot, entry.FullName);
            // A file that is already there belongs to the player or to another mod.
            if (File.Exists(target))
                continue;
            using var content = entry.Open();
            using var memory = new MemoryStream();
            content.CopyTo(memory);
            var data = memory.ToArray();
            var relative = Path.GetRelativePath(gameRoot, target);
            installed.Add(new CyberpunkInstalledFile(relative, Hashing.Sha256OfBytes(data), framework.Name, null));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.WriteAllBytes(target, data);
        }
    }

    private static void Undo(string gameRoot, IEnumerable<CyberpunkInstalledFile> installed)
    {
        foreach (var file in installed.Reverse())
        {
            var target = Resolve(gameRoot, file.Path);
            try
            {
                if (file.BackupPath is not null && File.Exists(file.BackupPath))
                    File.Copy(file.BackupPath, target, overwrite: true);
                else if (File.Exists(target))
                {
                    File.Delete(target);
                    RemoveEmptyParents(gameRoot, target);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Best effort: keep undoing the remaining files.
            }
        }
    }

    /// A reinstall keeps what an earlier run recorded: the frameworks it added then
    /// (they are skipped as "already present" now) and the first backup of each
    /// file, which is the one that predates AVRcade.
    private static IReadOnlyList<CyberpunkInstalledFile> MergeWithPrevious(
        CyberpunkInstallRecord? previous, string gameRoot, IReadOnlyList<CyberpunkInstalledFile> installed)
    {
        if (previous is null || !SamePath(previous.GameRoot, gameRoot))
            return installed;
        var earlier = previous.Files.ToDictionary(file => file.Path, StringComparer.OrdinalIgnoreCase);
        var merged = installed
            .Select(file => earlier.Remove(file.Path, out var old) && old.BackupPath is not null
                ? file with { BackupPath = old.BackupPath }
                : file)
            .ToList();
        merged.InsertRange(0, earlier.Values);
        return merged;
    }

    private void WriteRecord(string gameRoot, IReadOnlyList<CyberpunkInstalledFile> files) =>
        AtomicFile.WriteAllText(_recordPath, JsonSerializer.Serialize(
            new CyberpunkInstallRecord(
                Path.GetFullPath(gameRoot), Package.Version, DateTime.UtcNow.ToString("O"), files),
            new JsonSerializerOptions { WriteIndented = true }));

    private static void RemoveEmptyParents(string gameRoot, string removedFile) =>
        RemoveEmptyDirectories(gameRoot, Path.GetDirectoryName(Path.GetFullPath(removedFile)));

    /// Deletes a folder and its parents while they are empty, never the game root.
    private static void RemoveEmptyDirectories(string gameRoot, string? directory)
    {
        var root = Path.GetFullPath(gameRoot).TrimEnd(Path.DirectorySeparatorChar);
        while (directory is not null && directory.Length > root.Length &&
               Directory.Exists(directory) && !Directory.EnumerateFileSystemEntries(directory).Any())
        {
            Directory.Delete(directory);
            directory = Path.GetDirectoryName(directory);
        }
    }

    private static string Resolve(string root, string relative)
    {
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var combined = Path.GetFullPath(Path.Combine(
            fullRoot, relative.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar)));
        if (!combined.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Path leaves the game folder: {relative}");
        return combined;
    }

    private static bool HashEquals(string left, string right) =>
        left.Equals(right, StringComparison.OrdinalIgnoreCase);

    private static bool SamePath(string left, string right) =>
        Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar)
            .Equals(Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase);
}
