using System.IO.Compression;
using System.Text.Json;
using System.Text.RegularExpressions;
using VrClient.Core.Modpack;

namespace VrClient.Core.Redengine;

/// Nexus download authorization remains in the user's account flow. AVRcade
/// discovers its completed ZIP and stages the scripts only for Cyberpunk VR.
public sealed class CyberpunkNexusRequirements
{
    public const string DownloadUrl = "https://www.nexusmods.com/cyberpunk2077/mods/33136?tab=files&file_id=157829";
    public sealed record Receipt(string Creator, string Source, string ArchiveSha256,
        Dictionary<string, string> Files);
    public static string ReceiptPath(string gameRoot) =>
        Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), "more-occluders.json");

    public static bool Installed(string gameRoot)
    {
        try
        {
            if (!File.Exists(ReceiptPath(gameRoot))) return false;
            var receipt = JsonSerializer.Deserialize<Receipt>(File.ReadAllText(ReceiptPath(gameRoot)));
            var state = CyberpunkUpstreamSession.ReadState(gameRoot);
            return receipt is { Files.Count: > 0 } && state is not null && receipt.Files.All(file =>
                state.Files.Contains(file.Key, StringComparer.OrdinalIgnoreCase) &&
                File.Exists(CyberpunkUpstreamSession.PayloadPath(gameRoot, file.Key)) &&
                Hashing.Sha256OfFile(CyberpunkUpstreamSession.PayloadPath(gameRoot, file.Key))
                    .Equals(file.Value, StringComparison.OrdinalIgnoreCase) &&
                CyberpunkOccludersCompatibility.IsCompatible(gameRoot, file.Key));
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        { return false; }
    }

    public static IReadOnlyList<string> DownloadFolders()
    {
        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        var downloads = Path.Combine(home, "Downloads");
        if (OperatingSystem.IsWindows())
        {
            using var folders = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders");
            if (folders?.GetValue("{374DE290-123F-4565-9164-39C4925E467B}") is string configured)
                downloads = Environment.ExpandEnvironmentVariables(configured);
        }
        return [downloads, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "Vortex", "downloads", "cyberpunk2077")];
    }

    public async Task<CyberpunkBackendOutcome> InstallAsync(string gameRoot, Action<string> openDownload,
        IEnumerable<string>? downloadFolders = null, IProgress<string>? progress = null,
        CancellationToken ct = default, TimeSpan? timeout = null)
    {
        if (CyberpunkUpstreamSession.ReadState(gameRoot) is null)
            return new(false, "Install CyberpunkVR Port through AVRcade first.");
        if (Installed(gameRoot)) return new(true, "Cyberpunk VR requirements are installed: More Occluders by sosuine.");
        var directories = (downloadFolders ?? DownloadFolders()).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var deadline = DateTime.UtcNow + (timeout ?? TimeSpan.FromMinutes(10));
        var opened = false;
        string? lastProgress = null;
        void Report(string message)
        {
            if (message == lastProgress) return;
            lastProgress = message;
            progress?.Report(message);
        }
        while (true)
        {
            ct.ThrowIfCancellationRequested();
            if (Installed(gameRoot)) return new(true, "Cyberpunk VR requirements are installed.");
            foreach (var directory in directories.Where(Directory.Exists))
            {
                string[] candidates;
                try
                {
                    var downloads = Directory.EnumerateFiles(directory, "*33136*", SearchOption.TopDirectoryOnly).ToArray();
                    foreach (var partial in downloads.Where(path =>
                        IsDownloadName(Path.GetFileName(path).Replace(".crdownload", "", StringComparison.OrdinalIgnoreCase)
                            .Replace(".part", "", StringComparison.OrdinalIgnoreCase)) &&
                        !IsDownloadName(Path.GetFileName(path))))
                        Report($"Download started: More Occluders ({new FileInfo(partial).Length:N0} bytes received). Waiting for download completion.");
                    candidates = downloads.Where(path => IsDownloadName(Path.GetFileName(path)))
                        .OrderByDescending(File.GetLastWriteTimeUtc).ToArray();
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { continue; }
                foreach (var candidate in candidates)
                {
                    try
                    {
                        using var complete = ZipFile.OpenRead(candidate);
                        _ = complete.Entries.Count;
                    }
                    catch (Exception ex) when (ex is IOException or InvalidDataException)
                    {
                        // Browsers may reserve the final name before finishing.
                        Report("Download detected: waiting for the More Occluders ZIP to finish.");
                        continue;
                    }
                    Report("Download complete: installing More Occluders for Cyberpunk VR...");
                    return await Task.Run(() => Import(gameRoot, candidate), ct);
                }
            }
            if (!opened)
            {
                Report("Waiting for Nexus: no completed download detected yet. AVRcade is watching Downloads and Vortex.");
                openDownload(DownloadUrl);
                opened = true;
            }
            if (DateTime.UtcNow >= deadline)
                return new(false, "The Nexus download has not finished. Resume Cyberpunk setup to continue installation.");
            await Task.Delay(TimeSpan.FromSeconds(1), ct);
        }
    }

    public static bool IsDownloadName(string name) =>
        name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) &&
        (Regex.IsMatch(name, @"-33136-0-3-[0-9]{10}(?: \([0-9]+\))?\.zip$", RegexOptions.IgnoreCase) ||
         Regex.IsMatch(name, @" 33136 0\.3 [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}-[0-9]{2}Z [A-Za-z0-9_-]+(?: ?\([0-9]+\))?\.zip$", RegexOptions.IgnoreCase)) &&
        name.Contains("occlud", StringComparison.OrdinalIgnoreCase) &&
        name.Contains("scripted", StringComparison.OrdinalIgnoreCase);

    public static CyberpunkBackendOutcome Import(string gameRoot, string archivePath)
    {
        if (!IsDownloadName(Path.GetFileName(archivePath)))
            throw new InvalidDataException("This is not the Nexus More Occluders Scripted v0.3 ZIP.");
        if (new FileInfo(archivePath).Length > 2 * 1024 * 1024)
            throw new InvalidDataException("Unexpectedly large More Occluders download.");
        var bytes = File.ReadAllBytes(archivePath);
        using var zip = new ZipArchive(new MemoryStream(bytes), ZipArchiveMode.Read);
        var scripts = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in zip.Entries.Where(e => e.Name.Length != 0))
        {
            var relative = entry.FullName.Replace('\\', '/');
            CyberpunkUpstreamSession.Resolve(gameRoot, relative);
            if (relative.EndsWith(".txt", StringComparison.OrdinalIgnoreCase) ||
                relative.EndsWith(".md", StringComparison.OrdinalIgnoreCase)) continue;
            if (!relative.StartsWith("r6/scripts/", StringComparison.OrdinalIgnoreCase) ||
                !relative.EndsWith(".reds", StringComparison.OrdinalIgnoreCase) ||
                !relative.Contains("occlud", StringComparison.OrdinalIgnoreCase) || entry.Length > 1024 * 1024 ||
                scripts.Count >= 16 || scripts.Sum(s => s.Value.Length) + entry.Length > 2 * 1024 * 1024)
                throw new InvalidDataException("More Occluders ZIP contains unexpected files; only its REDscript package is accepted.");
            using var stream = entry.Open();
            using var data = new MemoryStream();
            stream.CopyTo(data);
            scripts.Add(relative, data.ToArray());
        }
        if (scripts.Count is 0 or > 16) throw new InvalidDataException("The Scripted v0.3 REDscript files were not found.");
        using var session = new CyberpunkUpstreamSession(gameRoot, vr: false);
        var state = CyberpunkUpstreamSession.ReadState(gameRoot)!;
        var originalState = File.ReadAllText(CyberpunkUpstreamSession.StatePath(gameRoot));
        var previous = state.PreviousFiles.ToList();
        var retired = new List<string>();
        var written = new Dictionary<string, byte[]?>();
        var receiptPath = ReceiptPath(gameRoot);
        var oldReceipt = File.Exists(receiptPath) ? File.ReadAllText(receiptPath) : null;
        var hashes = scripts.ToDictionary(pair => pair.Key, pair => Hashing.Sha256OfBytes(pair.Value));
        // Validate all collisions before copying or retiring anything.
        foreach (var (relative, hash) in hashes)
        {
            foreach (var existing in new[] { CyberpunkUpstreamSession.Resolve(gameRoot, relative),
                         CyberpunkUpstreamSession.PayloadPath(gameRoot, relative) })
                if (File.Exists(existing) && !Hashing.Sha256OfFile(existing).Equals(hash, StringComparison.OrdinalIgnoreCase))
                    throw new IOException($"An existing {relative} differs from this download and was preserved.");
        }
        try
        {
            foreach (var (relative, content) in scripts)
            {
                var payload = CyberpunkUpstreamSession.PayloadPath(gameRoot, relative);
                written[payload] = File.Exists(payload) ? File.ReadAllBytes(payload) : null;
                Directory.CreateDirectory(Path.GetDirectoryName(payload)!);
                File.WriteAllBytes(payload, content);
                var active = CyberpunkUpstreamSession.Resolve(gameRoot, relative);
                if (!File.Exists(active)) continue;
                var backup = CyberpunkUpstreamSession.Resolve(Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), "previous"), relative);
                Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                File.Move(active, backup, overwrite: false);
                retired.Add(relative);
                previous.Add(relative);
            }
            AtomicFile.WriteAllText(receiptPath, JsonSerializer.Serialize(
                new Receipt("sosuine", DownloadUrl, Hashing.Sha256OfBytes(bytes), hashes)));
            AtomicFile.WriteAllText(CyberpunkUpstreamSession.StatePath(gameRoot), JsonSerializer.Serialize(state with
            {
                Files = state.Files.Concat(scripts.Keys).Distinct(StringComparer.OrdinalIgnoreCase).ToArray(),
                PreviousFiles = previous,
                RequirementFiles = scripts.Keys.ToArray()
            }));
            CyberpunkOccludersCompatibility.Apply(gameRoot);
        }
        catch
        {
            foreach (var relative in retired)
                File.Move(CyberpunkUpstreamSession.Resolve(Path.Combine(CyberpunkUpstreamSession.Store(gameRoot), "previous"), relative),
                    CyberpunkUpstreamSession.Resolve(gameRoot, relative));
            foreach (var (file, old) in written)
                if (old is null) File.Delete(file); else File.WriteAllBytes(file, old);
            AtomicFile.WriteAllText(CyberpunkUpstreamSession.StatePath(gameRoot), originalState);
            if (oldReceipt is null) File.Delete(receiptPath); else AtomicFile.WriteAllText(receiptPath, oldReceipt);
            throw;
        }
        return new(true, "Installed More Occluders Scripted v0.3 by sosuine for Cyberpunk 2077 VR only. It activates and parks with the VR port.");
    }

    public static void RemoveStoredFiles(string gameRoot)
    {
        var path = ReceiptPath(gameRoot);
        if (!File.Exists(path)) return;
        var receipt = JsonSerializer.Deserialize<Receipt>(File.ReadAllText(path));
        if (receipt is null) return;
        var changed = false;
        foreach (var (relative, hash) in receipt.Files)
        {
            if (!relative.StartsWith("r6/scripts/", StringComparison.OrdinalIgnoreCase) ||
                !relative.EndsWith(".reds", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Unexpected More Occluders ownership record.");
            var file = CyberpunkUpstreamSession.PayloadPath(gameRoot, relative);
            if (!File.Exists(file)) continue;
            if (Hashing.Sha256OfFile(file).Equals(hash, StringComparison.OrdinalIgnoreCase)) File.Delete(file);
            else changed = true;
        }
        if (!changed) File.Delete(path);
    }
}
