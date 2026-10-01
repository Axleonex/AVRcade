using VrClient.Core.ReleaseProfiles;

namespace VrClient.Core.Tests;

public sealed class ReleaseProfileTests
{
    [Fact]
    public void Vr_variant_requires_supported_build_safety_and_packages()
    {
        var variant = new ReleaseVariant("vr", "sample", "VR", ReleaseLaunchMode.VrClient, true, true, ["runtime", "profile"]);

        var availability = ReleaseVariantEvaluator.Evaluate(variant, new HashSet<string> { "runtime" });

        Assert.Equal(VariantAvailabilityState.MissingPackage, availability.State);
        Assert.False(availability.CanLaunch);
        Assert.Equal("missing_required_package", availability.ReasonCode);
    }

    [Fact]
    public void External_vr_variant_remains_launchable_but_never_injectable()
    {
        var variant = new ReleaseVariant("external", "sample", "External VR", ReleaseLaunchMode.ExternalVrLaunchOnly, true, true, Array.Empty<string>());

        var availability = ReleaseVariantEvaluator.Evaluate(variant, new HashSet<string>());

        Assert.Equal(VariantAvailabilityState.LaunchOnly, availability.State);
        Assert.True(availability.CanLaunch);
        Assert.False(availability.CanInject);
    }

    [Fact]
    public void Safety_blocked_variant_cannot_be_launched_or_injected()
    {
        var variant = new ReleaseVariant("unsafe", "sample", "Unsafe", ReleaseLaunchMode.VrClient, true, false, Array.Empty<string>());

        var availability = ReleaseVariantEvaluator.Evaluate(variant, new HashSet<string>());

        Assert.Equal(VariantAvailabilityState.SafetyBlocked, availability.State);
        Assert.False(availability.CanLaunch);
        Assert.False(availability.CanInject);
    }

    [Fact]
    public void Preferences_round_trip_without_game_specific_paths()
    {
        using var fixture = new ProfileFixture();
        var preferences = new PlayerVrPreferences(ControllerPreference.IndexPlayStationStyle, false, 0.8, TurnPreference.Snap, true);

        fixture.Store.Save("global", preferences);

        Assert.Equal(preferences, fixture.Store.Load("global"));
    }

    [Fact]
    public void Invalid_hud_scale_is_refused()
    {
        using var fixture = new ProfileFixture();
        var invalid = PlayerVrPreferences.Default with { HudScale = 2.1 };

        Assert.Throws<ArgumentOutOfRangeException>(() => fixture.Store.Save("global", invalid));
    }

    private sealed class ProfileFixture : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"vrclient-profile-{Guid.NewGuid():N}");
        public ReleaseProfileStore Store { get; }

        public ProfileFixture()
        {
            Directory.CreateDirectory(Root);
            Store = new ReleaseProfileStore(Root);
        }

        public void Dispose()
        {
            if (Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
