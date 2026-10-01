namespace VrClient.Core.Modpack;
using System.Text.Json;
using VrClient.Core.Model;

public static class LockfileIo
{
    private static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true };

    public static void Write(string path, Lockfile lockfile)
    {
        var document = new Dictionary<string, object?>
        {
            ["schema"] = "modpack-lock/1",
            ["game_slug"] = lockfile.GameSlug,
            ["community"] = lockfile.Community,
            ["resolved_at_utc"] = lockfile.ResolvedAtUtc,
            ["packages"] = lockfile.Packages.Select(p => new Dictionary<string, object?>
            {
                ["namespace"] = p.Namespace,
                ["name"] = p.Name,
                ["version"] = p.Version,
                ["download_url"] = p.DownloadUrl,
                ["sha256"] = p.Sha256,
                ["size_bytes"] = p.SizeBytes
            }).ToList()
        };
        File.WriteAllText(path, JsonSerializer.Serialize(document, WriteOptions));
    }

    public static Lockfile Read(string path)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        var root = doc.RootElement;
        var packages = new List<LockedPackage>();
        foreach (var p in root.GetProperty("packages").EnumerateArray())
            packages.Add(new LockedPackage(
                p.GetProperty("namespace").GetString()!,
                p.GetProperty("name").GetString()!,
                p.GetProperty("version").GetString()!,
                p.GetProperty("download_url").GetString()!,
                p.GetProperty("sha256").GetString()!,
                p.GetProperty("size_bytes").GetInt64()));
        return new Lockfile(
            root.GetProperty("game_slug").GetString()!,
            root.GetProperty("community").GetString()!,
            root.GetProperty("resolved_at_utc").GetString()!,
            packages);
    }

    // Guard used by the downloader: throws if any package has an empty sha256.
    public static void RequireHashesFilled(Lockfile lockfile)
    {
        foreach (var p in lockfile.Packages)
            if (string.IsNullOrEmpty(p.Sha256))
                throw new InvalidOperationException(
                    $"NOT-IMPLEMENTED: lockfile package {p.Namespace}-{p.Name} has no sha256; run 'resolve' to fill it");
    }
}
