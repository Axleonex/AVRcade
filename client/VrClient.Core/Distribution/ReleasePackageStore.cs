using System.Text.Json;

namespace VrClient.Core.Distribution;

public enum ReleaseChannel
{
    Stable,
    Nightly,
    Rollback
}

public sealed record ReleasePackageDescriptor(
    string PackageId,
    string Version,
    ReleaseChannel Channel,
    string Sha256);

public sealed record InstalledReleasePackage(
    string PackageId,
    string Version,
    ReleaseChannel Channel,
    string Sha256,
    string ArtifactFileName,
    DateTimeOffset InstalledAtUtc);

public sealed record ReleasePackageState(
    string PackageId,
    string? ActiveVersion,
    IReadOnlyList<InstalledReleasePackage> InstalledVersions);

public enum ReleasePackageHealth
{
    Healthy,
    Missing,
    Corrupt
}

/// <summary>
/// Local, title-neutral package storage. It intentionally has no downloader or game-install
/// knowledge: artifacts are verified before activation and are retained by version for rollback.
/// </summary>
public sealed class ReleasePackageStore
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private readonly string _root;

    public ReleasePackageStore(string root)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(root);
        _root = Path.GetFullPath(root);
    }

    public InstalledReleasePackage Install(ReleasePackageDescriptor package, string sourceArtifactPath)
    {
        ValidateDescriptor(package);
        ArgumentException.ThrowIfNullOrWhiteSpace(sourceArtifactPath);
        if (!File.Exists(sourceArtifactPath))
            throw new FileNotFoundException("Release artifact was not found.", sourceArtifactPath);

        var actualHash = Hashing.Sha256OfFile(sourceArtifactPath);
        if (!string.Equals(actualHash, package.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Package hash mismatch for {package.PackageId}@{package.Version}.");

        var packageRoot = PackageRoot(package.PackageId);
        var versionRoot = Path.Combine(packageRoot, package.Version);
        var artifactFileName = Path.GetFileName(sourceArtifactPath);
        var installed = new InstalledReleasePackage(
            package.PackageId,
            package.Version,
            package.Channel,
            actualHash,
            artifactFileName,
            DateTimeOffset.UtcNow);

        Directory.CreateDirectory(packageRoot);
        if (!Directory.Exists(versionRoot))
        {
            var stagingRoot = Path.Combine(packageRoot, $".staging-{Guid.NewGuid():N}");
            Directory.CreateDirectory(stagingRoot);
            try
            {
                File.Copy(sourceArtifactPath, Path.Combine(stagingRoot, artifactFileName));
                if (!string.Equals(Hashing.Sha256OfFile(Path.Combine(stagingRoot, artifactFileName)), actualHash, StringComparison.Ordinal))
                    throw new InvalidDataException("Package changed while being staged.");
                Directory.Move(stagingRoot, versionRoot);
            }
            finally
            {
                if (Directory.Exists(stagingRoot))
                    Directory.Delete(stagingRoot, recursive: true);
            }
        }

        var state = ReadState(package.PackageId);
        var versions = state.InstalledVersions
            .Where(version => !string.Equals(version.Version, package.Version, StringComparison.Ordinal))
            .Append(installed)
            .OrderBy(version => version.InstalledAtUtc)
            .ToArray();
        WriteState(new ReleasePackageState(package.PackageId, package.Version, versions));
        return installed;
    }

    public ReleasePackageState ReadState(string packageId)
    {
        ValidateSegment(packageId, nameof(packageId));
        var statePath = StatePath(packageId);
        if (!File.Exists(statePath))
            return new ReleasePackageState(packageId, null, Array.Empty<InstalledReleasePackage>());

        return JsonSerializer.Deserialize<ReleasePackageState>(File.ReadAllText(statePath), JsonOptions)
            ?? throw new InvalidDataException($"Package state for '{packageId}' is invalid.");
    }

    public ReleasePackageHealth CheckHealth(string packageId)
    {
        var state = ReadState(packageId);
        var active = state.InstalledVersions.SingleOrDefault(version => version.Version == state.ActiveVersion);
        if (active is null)
            return ReleasePackageHealth.Missing;

        var artifactPath = Path.Combine(PackageRoot(packageId), active.Version, active.ArtifactFileName);
        if (!File.Exists(artifactPath))
            return ReleasePackageHealth.Missing;

        return string.Equals(Hashing.Sha256OfFile(artifactPath), active.Sha256, StringComparison.OrdinalIgnoreCase)
            ? ReleasePackageHealth.Healthy
            : ReleasePackageHealth.Corrupt;
    }

    public bool Rollback(string packageId)
    {
        var state = ReadState(packageId);
        if (state.ActiveVersion is null)
            return false;

        var activeIndex = state.InstalledVersions
            .Select((version, index) => (version, index))
            .SingleOrDefault(item => item.version.Version == state.ActiveVersion)
            .index;
        if (activeIndex <= 0)
            return false;

        var previous = state.InstalledVersions[activeIndex - 1];
        if (CheckHealth(packageId) == ReleasePackageHealth.Missing)
            return false;

        WriteState(state with { ActiveVersion = previous.Version });
        return true;
    }

    private void WriteState(ReleasePackageState state)
    {
        var packageRoot = PackageRoot(state.PackageId);
        Directory.CreateDirectory(packageRoot);
        var temporary = Path.Combine(packageRoot, $".state-{Guid.NewGuid():N}.json");
        File.WriteAllText(temporary, JsonSerializer.Serialize(state, JsonOptions));
        File.Move(temporary, StatePath(state.PackageId), overwrite: true);
    }

    private string PackageRoot(string packageId)
    {
        ValidateSegment(packageId, nameof(packageId));
        return Path.Combine(_root, "packages", packageId);
    }

    private string StatePath(string packageId) => Path.Combine(PackageRoot(packageId), "package-state.json");

    private static void ValidateDescriptor(ReleasePackageDescriptor package)
    {
        ArgumentNullException.ThrowIfNull(package);
        ValidateSegment(package.PackageId, nameof(package.PackageId));
        ValidateSegment(package.Version, nameof(package.Version));
        if (package.Sha256.Length != 64 || package.Sha256.Any(character => !Uri.IsHexDigit(character)))
            throw new ArgumentException("Package SHA-256 must be a 64-character hexadecimal value.", nameof(package));
    }

    private static void ValidateSegment(string value, string parameterName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(value, parameterName);
        if (value.Any(character => !(char.IsLetterOrDigit(character) || character is '.' or '-' or '_')))
            throw new ArgumentException("Package identifiers may contain only letters, digits, '.', '-', and '_'.", parameterName);
    }
}
