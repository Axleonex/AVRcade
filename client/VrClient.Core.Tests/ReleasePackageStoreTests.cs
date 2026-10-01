using VrClient.Core.Distribution;

namespace VrClient.Core.Tests;

public sealed class ReleasePackageStoreTests
{
    [Fact]
    public void Static_manifest_loads_a_stable_package_descriptor()
    {
        using var fixture = new PackageFixture();
        var path = Path.Combine(fixture.Root, "manifest.json");
        File.WriteAllText(path, """
        { "schemaVersion": 1, "packages": [
          { "packageId": "shared-controls", "version": "1.0.0", "channel": "stable", "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }
        ] }
        """);

        var manifest = StaticReleaseManifestReader.Load(path);

        Assert.Equal(ReleaseChannel.Stable, manifest.Packages.Single().Channel);
    }

    [Fact]
    public void Install_verifies_before_activating_and_retains_versioned_artifact()
    {
        using var fixture = new PackageFixture();
        var package = fixture.Package("shared-controls", "1.0.0", new byte[] { 1, 2, 3 });

        fixture.Store.Install(package.Descriptor, package.ArtifactPath);

        Assert.Equal(ReleasePackageHealth.Healthy, fixture.Store.CheckHealth("shared-controls"));
        Assert.Equal("1.0.0", fixture.Store.ReadState("shared-controls").ActiveVersion);
    }

    [Fact]
    public void Hash_mismatch_does_not_create_or_activate_a_package()
    {
        using var fixture = new PackageFixture();
        var package = fixture.Package("shared-controls", "1.0.0", new byte[] { 1, 2, 3 });
        var invalid = package.Descriptor with { Sha256 = new string('0', 64) };

        Assert.Throws<InvalidDataException>(() => fixture.Store.Install(invalid, package.ArtifactPath));

        Assert.Null(fixture.Store.ReadState("shared-controls").ActiveVersion);
        Assert.False(Directory.Exists(Path.Combine(fixture.Root, "packages", "shared-controls", "1.0.0")));
    }

    [Fact]
    public void Health_reports_corruption_without_replacing_the_active_version()
    {
        using var fixture = new PackageFixture();
        var package = fixture.Package("shared-controls", "1.0.0", new byte[] { 1, 2, 3 });
        fixture.Store.Install(package.Descriptor, package.ArtifactPath);
        var installed = fixture.Store.ReadState("shared-controls").InstalledVersions.Single();
        File.WriteAllBytes(Path.Combine(fixture.Root, "packages", "shared-controls", "1.0.0", installed.ArtifactFileName), new byte[] { 9 });

        Assert.Equal(ReleasePackageHealth.Corrupt, fixture.Store.CheckHealth("shared-controls"));
        Assert.Equal("1.0.0", fixture.Store.ReadState("shared-controls").ActiveVersion);
    }

    [Fact]
    public void Rollback_restores_the_previous_verified_version()
    {
        using var fixture = new PackageFixture();
        var one = fixture.Package("shared-controls", "1.0.0", new byte[] { 1 });
        var two = fixture.Package("shared-controls", "1.1.0", new byte[] { 2 });
        fixture.Store.Install(one.Descriptor, one.ArtifactPath);
        fixture.Store.Install(two.Descriptor, two.ArtifactPath);

        Assert.True(fixture.Store.Rollback("shared-controls"));
        Assert.Equal("1.0.0", fixture.Store.ReadState("shared-controls").ActiveVersion);
    }

    private sealed class PackageFixture : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"vrclient-release-{Guid.NewGuid():N}");
        public ReleasePackageStore Store { get; }

        public PackageFixture()
        {
            Directory.CreateDirectory(Root);
            Store = new ReleasePackageStore(Root);
        }

        public (ReleasePackageDescriptor Descriptor, string ArtifactPath) Package(string id, string version, byte[] bytes)
        {
            var artifactPath = Path.Combine(Root, $"{id}-{version}.bin");
            File.WriteAllBytes(artifactPath, bytes);
            return (new ReleasePackageDescriptor(id, version, ReleaseChannel.Stable, Hashing.Sha256OfBytes(bytes)), artifactPath);
        }

        public void Dispose()
        {
            if (Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
