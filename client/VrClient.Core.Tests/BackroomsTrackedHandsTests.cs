using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using VrClient.Core.Unreal;

namespace VrClient.Core.Tests;

public sealed class BackroomsTrackedHandsTests
{
    [Fact]
    public void OtherGamesAndMissingProfilesAreNeverCreated()
    {
        using var f = new Fixture(false);
        Assert.False(BackroomsTrackedHands.PrepareForLaunch("rv-there-yet", f.Profile));
        Assert.False(Directory.Exists(f.Profile));
        Assert.Throws<InvalidOperationException>(() => BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile));
        Assert.False(Directory.Exists(f.Profile));
    }

    [Fact]
    public void DeploysEmbeddedDriverAndPreservesKeyboardAndCommunityProfile()
    {
        using var f = new Fixture();
        File.WriteAllText(f.Keyboard, "existing keyboard");
        Assert.True(BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile));
        Assert.Equal(BackroomsTrackedHands.Script, File.ReadAllText(f.Driver));
        Assert.Contains("\"enabled\":true", File.ReadAllText(f.Data));
        Assert.Equal("existing keyboard", File.ReadAllText(f.Keyboard));
        Assert.Equal("community config", File.ReadAllText(Path.Combine(f.Profile, "config.txt")));
    }

    [Fact]
    public void RepeatedLaunchPreservesDisablePreferenceAndScriptTimestamp()
    {
        using var f = new Fixture();
        BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile);
        File.WriteAllText(f.Data, "{\"enabled\":false}");
        var timestamp = File.GetLastWriteTimeUtc(f.Driver);
        BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile);
        Assert.Equal(timestamp, File.GetLastWriteTimeUtc(f.Driver));
        Assert.Contains("\"enabled\":false", File.ReadAllText(f.Data));
        Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(f.Driver)!, "*.bak"));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ExternallyEditedDriverIsPreserved(bool malformedManifest)
    {
        using var f = new Fixture();
        BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile);
        File.WriteAllText(f.Driver, "user customization");
        if (malformedManifest) File.WriteAllText(f.Manifest, "{broken");
        Assert.Throws<InvalidOperationException>(() => BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile));
        Assert.Equal("user customization", File.ReadAllText(f.Driver));
    }

    [Fact]
    public void OwnedPreviousVersionUpdatesWithRecoverableBackup()
    {
        using var f = new Fixture();
        File.WriteAllText(f.Driver, "old owned version");
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("old owned version"))).ToLowerInvariant();
        File.WriteAllText(f.Manifest, JsonSerializer.Serialize(new { installedSha256 = hash }));
        BackroomsTrackedHands.PrepareForLaunch(BackroomsTrackedHands.GameSlug, f.Profile);
        Assert.Equal(BackroomsTrackedHands.Script, File.ReadAllText(f.Driver));
        Assert.Equal("old owned version", File.ReadAllText(Assert.Single(Directory.GetFiles(Path.GetDirectoryName(f.Driver)!, "*.bak"))));
    }

    private sealed class Fixture : IDisposable
    {
        private readonly string _root = Path.Combine(Path.GetTempPath(), $"avrcade-backrooms-hands-{Guid.NewGuid():N}");
        public Fixture(bool createProfile = true)
        {
            if (!createProfile) return;
            Directory.CreateDirectory(Path.Combine(Profile, "scripts"));
            File.WriteAllText(Path.Combine(Profile, "config.txt"), "community config");
        }
        public string Profile => Path.Combine(_root, "Backrooms-Win64-Shipping");
        public string Driver => Path.Combine(Profile, "scripts", BackroomsTrackedHands.ScriptFileName);
        public string Keyboard => Path.Combine(Profile, "scripts", "backrooms_names.lua");
        public string Data => Path.Combine(Profile, "data", BackroomsTrackedHands.DataFileName);
        public string Manifest => Path.Combine(Profile, ".avrcade-backrooms-hands.json");
        public void Dispose() { if (Directory.Exists(_root)) Directory.Delete(_root, true); }
    }
}
