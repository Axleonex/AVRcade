using VrClient.Core.Distribution;
using VrClient.Core.ReleaseDiagnostics;
using System.Text.Json;

namespace VrClient.Core.Tests;

public sealed class ReleaseDiagnosticsTests
{
    [Fact]
    public void Doctor_blocks_missing_runtime_and_corrupt_package()
    {
        var report = ReleaseDoctor.Evaluate(new ReleaseDoctorInput(false, ReleasePackageHealth.Corrupt, true, true));

        Assert.False(report.CanLaunch);
        Assert.Contains(report.Findings, finding => finding.Code == "openxr_runtime_missing");
        Assert.Contains(report.Findings, finding => finding.Code == "release_package_corrupt");
    }

    [Fact]
    public void Doctor_reports_ready_when_all_shared_prerequisites_are_present()
    {
        var report = ReleaseDoctor.Evaluate(new ReleaseDoctorInput(true, ReleasePackageHealth.Healthy, true, true));

        Assert.True(report.CanLaunch);
        Assert.Single(report.Findings);
        Assert.Equal("ready", report.Findings[0].Code);
    }

    [Fact]
    public void Diagnostic_export_redacts_user_directory()
    {
        using var fixture = new DiagnosticFixture();
        var output = fixture.Writer.Write(fixture.Output, [
            new DiagnosticEntry("sample", "Failure at C:\\Users\\Alice\\AppData\\Local\\VRClient\\logs\\latest.log")
        ], "C:\\Users\\Alice");

        var content = File.ReadAllText(output);
        Assert.DoesNotContain("Alice", content, StringComparison.OrdinalIgnoreCase);
        var decoded = JsonSerializer.Deserialize<DiagnosticEntry[]>(content)!;
        Assert.True(
            decoded[0].Message.Contains("<user-profile>", StringComparison.Ordinal) || decoded[0].Message.Contains("<user>", StringComparison.Ordinal),
            $"Expected a redaction token in diagnostic output: {decoded[0].Message}");
    }

    [Fact]
    public void Qualification_requires_all_cross_game_acceptance_gates()
    {
        Assert.True(new ReleaseQualification(true, true, true, true, true, true).Passed);
        Assert.False(new ReleaseQualification(true, true, false, true, true, true).Passed);
    }

    private sealed class DiagnosticFixture : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), $"vrclient-diagnostics-{Guid.NewGuid():N}");
        public string Output => Path.Combine(Root, "output");
        public DiagnosticBundleWriter Writer { get; } = new();

        public DiagnosticFixture() => Directory.CreateDirectory(Root);

        public void Dispose()
        {
            if (Directory.Exists(Root))
                Directory.Delete(Root, recursive: true);
        }
    }
}
