using System.Text.Json;
using VrClient.Core.Rage;

namespace VrClient.Core.Tests;

public sealed class Rdr2DiagnosticsTests
{
    [Fact]
    public void ProcessIdentity_RequiresThePreflightExecutable()
    {
        var expected = Path.Combine(Path.GetTempPath(), "rdr2-current", "RDR2.exe");

        Assert.True(Rdr2Diagnostics.ProcessMatchesExpectedExecutable(expected, expected.ToUpperInvariant()));
        Assert.False(Rdr2Diagnostics.ProcessMatchesExpectedExecutable(
            expected, Path.Combine(Path.GetTempPath(), "rdr2-other", "RDR2.exe")));
        Assert.False(Rdr2Diagnostics.ProcessMatchesExpectedExecutable(expected, null));
    }

    [Fact]
    public void BridgeLogPath_IsScopedToTheLiveRdr2Process()
    {
        var root = Path.Combine(Path.GetTempPath(), "rdr2-game");

        Assert.Equal(
            Path.Combine(Path.GetFullPath(root), "rdr2-bridge-4242.log"),
            Rdr2Diagnostics.BridgeLogPath(root, 4242));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Rdr2Diagnostics.BridgeLogPath(root, 0));
    }

    [Fact]
    public void ReadinessEvidence_RequiresTheCurrentBuildAndExecutableFingerprint()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-evidence-{Guid.NewGuid():N}");
        try
        {
            var currentHash = new string('a', 64);
            var evidence = Rdr2Diagnostics.CreateCandidate(
                "live-current", "13773296", currentHash,
                "d3d12_live_candidate", "native_bridge_verified");
            var path = Rdr2Diagnostics.Write(root, evidence);

            Assert.Equal(
                "ready_for_smoke",
                Rdr2Diagnostics.EvaluateReadinessEvidence(path, "13773296", currentHash).Status);
            Assert.Equal(
                "evidence_identity_mismatch",
                Rdr2Diagnostics.EvaluateReadinessEvidence(path, "99999999", currentHash).Status);
            Assert.Equal(
                "evidence_identity_mismatch",
                Rdr2Diagnostics.EvaluateReadinessEvidence(path, "13773296", new string('b', 64)).Status);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData("d3d12_candidate", "native_bridge_verified", "renderer_evidence_missing")]
    [InlineData("d3d12_live_candidate", "native_bridge_candidate", "camera_bridge_evidence_missing")]
    [InlineData("unknown", "unknown", "renderer_evidence_missing")]
    public void ReadinessEvidence_RejectsStaticOrUnverifiedBridgeObservations(
        string rendererObservation,
        string cameraObservation,
        string expectedStatus)
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-evidence-{Guid.NewGuid():N}");
        try
        {
            var hash = new string('d', 64);
            var evidence = Rdr2Diagnostics.CreateCandidate(
                "not-live", "13773296", hash, rendererObservation, cameraObservation);
            var path = Rdr2Diagnostics.Write(root, evidence);

            Assert.Equal(
                expectedStatus,
                Rdr2Diagnostics.EvaluateReadinessEvidence(path, "13773296", hash).Status);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void CreateCandidate_IsRedactedAndCandidateOnly()
    {
        var evidence = Rdr2Diagnostics.CreateCandidate("run-1", "13773296", new string('A', 64));

        Assert.Equal(Rdr2DiagnosticEvidence.SchemaName, evidence.Schema);
        Assert.Equal("red-dead-redemption-2", evidence.Title);
        Assert.Equal("candidate", evidence.Compatibility);
        Assert.Equal(new string('a', 64), evidence.ExecutableSha256);
        Assert.Equal("unknown", evidence.RendererObservation);
    }

    [Fact]
    public void Write_UsesOnlyTheEvidenceRootAndValidJson()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-evidence-{Guid.NewGuid():N}");
        try
        {
            var evidence = Rdr2Diagnostics.CreateCandidate("run_2", "13773296", new string('b', 64));
            var path = Rdr2Diagnostics.Write(root, evidence);
            Assert.StartsWith(Path.GetFullPath(root), Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase);
            using var json = JsonDocument.Parse(File.ReadAllText(path));
            Assert.Equal("candidate", json.RootElement.GetProperty("Compatibility").GetString());
            Assert.False(File.Exists(path + ".tmp"));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Theory]
    [InlineData("../escape")]
    [InlineData("bad run")]
    public void CreateCandidate_RejectsUnsafeRunId(string runId) =>
        Assert.Throws<ArgumentException>(() => Rdr2Diagnostics.CreateCandidate(runId, "13773296", new string('c', 64)));

    [Theory]
    [InlineData("vulkan-1.dll\0d3d12.dll", "vulkan_and_d3d12_candidate")]
    [InlineData("vkGetInstanceProcAddr", "vulkan_candidate")]
    [InlineData("D3D12CreateDevice", "d3d12_candidate")]
    [InlineData("no graphics imports", "unknown")]
    public void ObserveRendererImports_IsCandidateOnly(string content, string expected)
    {
        var file = Path.GetTempFileName();
        try
        {
            File.WriteAllText(file, content);
            Assert.Equal(expected, Rdr2Diagnostics.ObserveRendererImports(file));
        }
        finally { File.Delete(file); }
    }

    [Theory]
    [InlineData(new[] { "C:/Windows/System32/vulkan-1.dll" }, "vulkan_live_candidate")]
    [InlineData(new[] { "d3d12.dll" }, "d3d12_live_candidate")]
    [InlineData(new[] { "vulkan-1.dll", "dxgi.dll" }, "vulkan_and_d3d12_live_candidate")]
    [InlineData(new[] { "kernel32.dll" }, "unknown")]
    public void ClassifyRendererModules_UsesNamesOnly(string[] modules, string expected) =>
        Assert.Equal(expected, Rdr2Diagnostics.ClassifyRendererModules(modules));
}
