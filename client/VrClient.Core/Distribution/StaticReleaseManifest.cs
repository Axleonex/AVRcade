using System.Text.Json;
using System.Text.Json.Serialization;

namespace VrClient.Core.Distribution;

public sealed record StaticReleaseManifest(int SchemaVersion, IReadOnlyList<ReleaseManifestPackage> Packages);

public sealed record ReleaseManifestPackage(string PackageId, string Version, ReleaseChannel Channel, string Sha256)
{
    public ReleasePackageDescriptor ToDescriptor() => new(PackageId, Version, Channel, Sha256);
}

public static class StaticReleaseManifestReader
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) }
    };

    public static StaticReleaseManifest Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var manifest = JsonSerializer.Deserialize<StaticReleaseManifest>(File.ReadAllText(path), JsonOptions)
            ?? throw new InvalidDataException("Release manifest is empty or invalid.");
        if (manifest.SchemaVersion != 1)
            throw new InvalidDataException($"Unsupported release manifest schema {manifest.SchemaVersion}.");
        if (manifest.Packages.Count == 0 || manifest.Packages.GroupBy(package => package.PackageId).Any(group => group.Count() > 1))
            throw new InvalidDataException("Release manifest must contain uniquely identified packages.");
        return manifest;
    }
}
