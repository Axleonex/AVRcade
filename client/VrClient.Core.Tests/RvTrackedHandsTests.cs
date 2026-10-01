using VrClient.Core.Unreal;

namespace VrClient.Core.Tests;

public sealed class RvTrackedHandsTests
{
    [Fact]
    public void DisabledByDefaultAndDoesNotInstallScript()
    {
        using var fixture = new Fixture();
        Assert.False(fixture.Hands.Enabled);

        fixture.Hands.PrepareForLaunch();

        Assert.False(File.Exists(fixture.Hands.ScriptPath));
        Assert.Contains("\"enabled\":false", File.ReadAllText(fixture.DataPath));
    }

    [Fact]
    public void EnablePersistsAndInstallsOnlyOwnedScript()
    {
        using var fixture = new Fixture();
        var communityConfig = Path.Combine(fixture.Profile, "config.txt");
        Directory.CreateDirectory(fixture.Profile);
        File.WriteAllText(communityConfig, "community camera settings");
        fixture.Hands.SetEnabled(true);
        Assert.True(new RvTrackedHands(fixture.Settings, fixture.Profile).Enabled);

        fixture.Hands.PrepareForLaunch();

        Assert.Equal(RvTrackedHands.Script, File.ReadAllText(fixture.Hands.ScriptPath));
        Assert.Contains("\"enabled\":true", File.ReadAllText(fixture.DataPath));
        Assert.Equal("community camera settings", File.ReadAllText(communityConfig));
        fixture.Hands.PrepareForLaunch();
    }

    [Fact]
    public void DisablingTellsRunningScriptToStopWithoutRemovingCommunityFiles()
    {
        using var fixture = new Fixture();
        fixture.Hands.SetEnabled(true);
        fixture.Hands.PrepareForLaunch();

        fixture.Hands.SetEnabled(false);

        Assert.False(new RvTrackedHands(fixture.Settings, fixture.Profile).Enabled);
        Assert.Contains("\"enabled\":false", File.ReadAllText(fixture.DataPath));
        Assert.True(File.Exists(fixture.Hands.ScriptPath));
    }

    [Fact]
    public void RefusesToOverwriteExternallyEditedScriptAndFailsClosed()
    {
        using var fixture = new Fixture();
        fixture.Hands.SetEnabled(true);
        fixture.Hands.PrepareForLaunch();
        File.WriteAllText(fixture.Hands.ScriptPath, "user edit");

        Assert.Throws<InvalidOperationException>(() => fixture.Hands.PrepareForLaunch());

        Assert.Equal("user edit", File.ReadAllText(fixture.Hands.ScriptPath));
        Assert.Contains("\"enabled\":false", File.ReadAllText(fixture.DataPath));
    }

    [Fact]
    public void InvalidPreferenceFailsClosed()
    {
        using var fixture = new Fixture();
        File.WriteAllText(fixture.Settings, "{invalid");
        Assert.False(fixture.Hands.Enabled);
    }

    [Fact]
    public void TogglingBeforeFirstLaunchDoesNotCreateUevrProfile()
    {
        using var fixture = new Fixture();
        fixture.Hands.SetEnabled(true);
        fixture.Hands.SetEnabled(false);
        Assert.False(Directory.Exists(fixture.Profile));
    }

    private sealed class Fixture : IDisposable
    {
        private readonly string _root = Path.Combine(Path.GetTempPath(),
            $"vrclient-rv-hands-{Guid.NewGuid():N}");

        public Fixture()
        {
            Directory.CreateDirectory(_root);
            Hands = new RvTrackedHands(Settings, Profile);
        }

        public string Settings => Path.Combine(_root, "settings.json");
        public string Profile => Path.Combine(_root, "Ride-Win64-Shipping");
        public string DataPath => Path.Combine(Profile, "data", "vrclient-rv-hands.json");
        public RvTrackedHands Hands { get; }

        public void Dispose()
        {
            if (Directory.Exists(_root)) Directory.Delete(_root, recursive: true);
        }
    }
}
