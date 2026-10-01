using System.Collections.Generic;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using Xunit;

public class LockfileIoTests
{
    private static Lockfile TwoPackages(string shaFirst, string shaSecond) => new(
        "repo", "repo", "2026-07-02T00:00:00Z",
        new List<LockedPackage>
        {
            new("BepInEx", "BepInExPack", "5.4.2100", "https://example.invalid/bepinexpack.zip", shaFirst, 2000000),
            new("DaXcess", "RepoXR", "1.2.2", "https://example.invalid/repoxr.zip", shaSecond, 3000000)
        });

    [Fact]
    public void Write_read_roundtrip_preserves_versions_and_filled_hashes_pass_guard()
    {
        var shaA = new string('a', 64);
        var shaB = new string('b', 64);
        var path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), System.Guid.NewGuid() + ".lock.json");
        try
        {
            LockfileIo.Write(path, TwoPackages(shaA, shaB));
            var read = LockfileIo.Read(path);
            Assert.Equal("5.4.2100", read.Packages[0].Version);
            Assert.Equal("1.2.2", read.Packages[1].Version);
            Assert.Equal(shaA, read.Packages[0].Sha256);
            Assert.Equal(shaB, read.Packages[1].Sha256);
            LockfileIo.RequireHashesFilled(read); // must not throw when all filled
        }
        finally
        {
            System.IO.File.Delete(path);
        }
    }

    [Fact]
    public void RequireHashesFilled_throws_when_one_sha256_is_empty()
    {
        var shaA = new string('a', 64);
        var ex = Assert.Throws<System.InvalidOperationException>(
            () => LockfileIo.RequireHashesFilled(TwoPackages(shaA, "")));
        Assert.Contains("NOT-IMPLEMENTED", ex.Message);
    }
}
