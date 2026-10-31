using System.Diagnostics;
using System.Text.Json;
using System.Text.RegularExpressions;
using VrClient.Core.Modpack;

namespace VrClient.Core.Redengine;

/// Recovers framework-generated log aliases before native log enumeration.
public static class CyberpunkFrameworkLogRecovery
{
    private sealed record Link(string Framework, string Source, string Relative, string Target);

    public static IReadOnlyList<string> Prepare(string gameRoot, CyberpunkBackendPackage package, bool dryRun = false)
    {
        if (dryRun) return [];
        if (Process.GetProcessesByName("Cyberpunk2077").Length != 0)
            throw new InvalidOperationException("Close Cyberpunk 2077 before recovering its framework logs.");

        var links = new List<Link>();
        foreach (var name in new[] { "ArchiveXL", "Codeware", "TweakXL" })
        {
            var folderRelative = $"red4ext/plugins/{name}";
            // Validate the parent with the usual no-link traversal guard. Only
            // the exact generated alias at the leaf is intentionally inspected.
            var folder = CyberpunkUpstreamSession.Resolve(gameRoot, folderRelative);
            if (!Directory.Exists(folder)) continue;
            var source = Path.Combine(folder, name + ".log");
            FileAttributes attributes;
            try { attributes = File.GetAttributes(source); }
            catch (FileNotFoundException) { continue; }
            catch (DirectoryNotFoundException) { continue; }
            if ((attributes & FileAttributes.ReparsePoint) == 0) continue;
            if ((attributes & FileAttributes.Directory) != 0)
                throw new InvalidDataException($"The {name} log alias is a directory link. It was preserved; inspect it before launching.");
            // Read reparse metadata, never the linked contents. GUI child-process
            // redirection trust can reject even an existing relative log target.
            var target = new FileInfo(source).LinkTarget;
            if (target is null || !Regex.IsMatch(target,
                    "^" + Regex.Escape(name) + @"-[0-9]{4}(?:-[0-9]{2}){5}\.log$", RegexOptions.IgnoreCase))
                throw new InvalidDataException($"The {name} log link has an unknown target. Its link and target were preserved.");
            var markerRelative = folderRelative + "/" + name + ".dll";
            var framework = package.Frameworks.FirstOrDefault(f =>
                f.Name.Equals(name, StringComparison.OrdinalIgnoreCase) &&
                f.Marker.Replace('\\', '/').Equals(markerRelative, StringComparison.OrdinalIgnoreCase));
            var marker = CyberpunkUpstreamSession.Resolve(gameRoot, markerRelative);
            if (string.IsNullOrWhiteSpace(framework?.MarkerSha256) || !File.Exists(marker) ||
                !Hashing.Sha256OfFile(marker).Equals(framework.MarkerSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"The installed {name} does not match AVRcade's verified framework. Its log link was preserved; use Update mods.");
            links.Add(new(name, source, folderRelative + "/" + name + ".log", target));
        }
        if (links.Count == 0) return [];

        // Preflight all aliases before moving any. Keep the aliases themselves
        // outside RED4ext's scan, with restoration metadata. Historical logs stay
        // in place and no DLL, script, preference or Windows policy is changed.
        var backupRoot = CyberpunkUpstreamSession.StoreDirectory + "/log-link-backups/" + Guid.NewGuid().ToString("N");
        Directory.CreateDirectory(CyberpunkUpstreamSession.Resolve(gameRoot, backupRoot));
        var receipts = new List<string>();
        foreach (var link in links)
        {
            if (new FileInfo(link.Source).LinkTarget != link.Target)
                throw new IOException($"The {link.Framework} log link changed during recovery. It was preserved; retry after closing other launchers.");
            var backup = backupRoot + "/" + link.Framework + ".log";
            var saved = CyberpunkUpstreamSession.Resolve(gameRoot, backup);
            var receipt = CyberpunkUpstreamSession.Resolve(gameRoot, backupRoot + "/" + link.Framework + ".json");
            string Evidence(string state) => JsonSerializer.Serialize(new {
                schema = "avrcade-framework-log-link/1", framework = link.Framework,
                source = link.Relative, target = link.Target, backup, state
            });
            AtomicFile.WriteAllText(receipt, Evidence("planned"));
            File.Move(link.Source, saved); // Rename the link itself; do not follow it.
            if (new FileInfo(saved).LinkTarget != link.Target)
                throw new IOException($"The saved {link.Framework} log alias could not be verified. Its recovery receipt was retained.");
            AtomicFile.WriteAllText(receipt, Evidence("parked"));
            receipts.Add(receipt);
        }
        return receipts;
    }
}
