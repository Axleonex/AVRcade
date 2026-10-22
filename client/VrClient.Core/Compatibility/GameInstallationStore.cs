using System.Text.Json;

namespace VrClient.Core.Compatibility;

public sealed record GameBuildIdentity(string ExecutableSha256, string? Version, string? SteamBuild);
public sealed record InstallationProfile(string Root, string Label, bool AutomaticUpdates = true,
    GameBuildIdentity? FirstObserved = null, GameBuildIdentity? HeadsetTested = null,
    string? TestedPackage = null, string? PreparedPackage = null);
public sealed record GameInstallations(string? SelectedRoot, IReadOnlyList<InstallationProfile> Profiles, string? SteamAppId = null);

/// Preferences are keyed by both game and installation. Selecting a missing drive never
/// silently falls back to a different copy. Nothing in this store edits Steam or saves.
public sealed class GameInstallationStore(string? path = null)
{
    private readonly object _gate = new();
    public string PathOnDisk { get; } = path ?? AppDataPaths.Local("compatibility/installations.json");
    private Dictionary<string, GameInstallations> Read() => File.Exists(PathOnDisk)
        ? JsonSerializer.Deserialize<Dictionary<string, GameInstallations>>(File.ReadAllText(PathOnDisk))
            ?? throw new InvalidDataException("Installation preferences are empty or unreadable.")
        : new(StringComparer.OrdinalIgnoreCase);
    private void Write(Dictionary<string, GameInstallations> entries) =>
        AtomicFile.WriteAllText(PathOnDisk, JsonSerializer.Serialize(entries, new JsonSerializerOptions { WriteIndented = true }));
    public GameInstallations ForGame(string slug) { lock (_gate) return Read().GetValueOrDefault(slug) ?? new(null, []); }
    public string? SelectedRoot(string slug) => ForGame(slug).SelectedRoot;
    public InstallationProfile Profile(string slug, string root) => ForGame(slug).Profiles
        .FirstOrDefault(p => SameRoot(p.Root, root)) ?? new(Normalize(root), Path.GetFileName(Normalize(root)));
    public string? SelectedForApp(string appId) { lock (_gate) return Read().Values.FirstOrDefault(g => g.SteamAppId == appId && g.SelectedRoot is not null)?.SelectedRoot; }
    public void Update(string slug, InstallationProfile profile, bool select = false, string? steamAppId = null)
    {
        lock (_gate)
        {
            using var lease = AcquireLease();
            var entries = Read();
            var prior = entries.GetValueOrDefault(slug) ?? new(null, []);
            entries[slug] = new(select ? Normalize(profile.Root) : prior.SelectedRoot,
                prior.Profiles.Where(p => !SameRoot(p.Root, profile.Root)).Append(profile with { Root = Normalize(profile.Root) }).ToArray(), steamAppId ?? prior.SteamAppId);
            Write(entries);
        }
    }
    public void UseDiscovered(string slug)
    {
        lock (_gate)
        {
            using var lease = AcquireLease();
            var entries = Read();
            if (entries.TryGetValue(slug, out var prior)) { entries[slug] = prior with { SelectedRoot = null }; Write(entries); }
        }
    }
    private FileStream AcquireLease()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(PathOnDisk)!);
        return new FileStream(PathOnDisk + ".lock", FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    }
    public static string Normalize(string root)
    {
        if (!Path.IsPathFullyQualified(root)) throw new ArgumentException("Choose an absolute game folder.");
        return Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }
    public static bool SameRoot(string? left, string? right) => left is not null && right is not null &&
        Normalize(left).Equals(Normalize(right), OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
}
