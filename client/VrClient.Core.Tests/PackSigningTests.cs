using System;
using System.IO;
using VrClient.Core.Modpack;
using Xunit;

public class PackSigningTests
{
    private sealed class StubVerifier : IPackVerifier
    {
        private readonly bool? _result;
        public StubVerifier(bool? result) => _result = result;
        public bool? Verify(string manifestPath, string zipPath) => _result;
    }

    private static string NewZip(bool withManifest)
    {
        var zip = Path.Combine(Path.GetTempPath(), Guid.NewGuid() + ".zip");
        File.WriteAllBytes(zip, new byte[] { 1, 2, 3 });
        if (withManifest)
            File.WriteAllText(zip + ".manifest.json", "{ \"artifacts\": [] }");
        return zip;
    }

    private static void Cleanup(string zip)
    {
        if (File.Exists(zip)) File.Delete(zip);
        if (File.Exists(zip + ".manifest.json")) File.Delete(zip + ".manifest.json");
    }

    [Fact]
    public void No_manifest_is_allowed_as_community_mod()
    {
        var zip = NewZip(withManifest: false);
        try
        {
            var (allowed, reason) = PackSigning.CheckBeforeInstall(zip, new StubVerifier(true));
            Assert.True(allowed);
            Assert.Equal("unsigned_community_mod_allowed", reason);
        }
        finally { Cleanup(zip); }
    }

    [Fact]
    public void Manifest_with_valid_signature_is_allowed()
    {
        var zip = NewZip(withManifest: true);
        try
        {
            var (allowed, reason) = PackSigning.CheckBeforeInstall(zip, new StubVerifier(true));
            Assert.True(allowed);
            Assert.Equal("pack_signature_ok", reason);
        }
        finally { Cleanup(zip); }
    }

    [Fact]
    public void Manifest_with_invalid_signature_is_refused()
    {
        var zip = NewZip(withManifest: true);
        try
        {
            var (allowed, reason) = PackSigning.CheckBeforeInstall(zip, new StubVerifier(false));
            Assert.False(allowed);
            Assert.Equal("pack_signature_invalid", reason);
        }
        finally { Cleanup(zip); }
    }

    [Fact]
    public void Manifest_with_unavailable_verifier_fails_closed()
    {
        var zip = NewZip(withManifest: true);
        try
        {
            var (allowed, reason) = PackSigning.CheckBeforeInstall(zip, new StubVerifier(null));
            Assert.False(allowed);
            Assert.Equal("pack_signature_unverifiable", reason);
        }
        finally { Cleanup(zip); }
    }
}
