using VrClient.Core.Platform;

namespace VrClient.Core.Tests;

public sealed class PlatformCompatibilityTests
{
    [Fact]
    public void Windows_support_is_independent_of_linux_flags()
    {
        var result = ReleasePlatformCompatibility.Evaluate(
            new HostPlatformInfo(HostPlatform.Windows, false, true),
            new PackagePlatformSupport(Windows: true, NativeLinux: false, Proton: false));

        Assert.Equal(PlatformCompatibilityState.Compatible, result.State);
        Assert.Equal("windows_supported", result.ReasonCode);
    }

    [Fact]
    public void Steam_os_can_use_explicit_proton_support()
    {
        var result = ReleasePlatformCompatibility.Evaluate(
            new HostPlatformInfo(HostPlatform.Linux, true, true),
            new PackagePlatformSupport(Windows: true, NativeLinux: false, Proton: true));

        Assert.Equal(PlatformCompatibilityState.CompatibleViaProton, result.State);
        Assert.True(result.CanUse);
    }

    [Fact]
    public void Generic_linux_does_not_assume_proton_is_supported()
    {
        var result = ReleasePlatformCompatibility.Evaluate(
            new HostPlatformInfo(HostPlatform.Linux, false, true),
            new PackagePlatformSupport(Windows: true, NativeLinux: false, Proton: true));

        Assert.Equal(PlatformCompatibilityState.Unsupported, result.State);
    }

    [Theory]
    [InlineData("../escape")]
    [InlineData("C:\\absolute")]
    [InlineData("folder//file")]
    public void Package_paths_refuse_absolute_and_traversal_values(string invalid)
    {
        Assert.Throws<ArgumentException>(() => ReleasePlatformCompatibility.NormalizeRelativePackagePath(invalid));
    }

    [Fact]
    public void Package_path_normalizes_separator_without_changing_scope()
    {
        var normalized = ReleasePlatformCompatibility.NormalizeRelativePackagePath("profiles\\shared/map.json");

        Assert.Equal(Path.Combine("profiles", "shared", "map.json"), normalized);
    }
}
