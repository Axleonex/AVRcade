using System.Text;
using System.Text.Json;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkFrameworkLogTests : TempTree
{
    private static readonly string[] Names = ["ArchiveXL", "Codeware", "TweakXL"];
    private string At(string relative) => Path.Combine(Root, relative.Replace('/', Path.DirectorySeparatorChar));
    private string Log(string name) => At($"red4ext/plugins/{name}/{name}.log");
    private string Target(string name) => $"{name}-2026-10-31-12-30-00.log";

    private CyberpunkBackendPackage Setup(bool links = true)
    {
        var frameworks = new List<CyberpunkFramework>();
        foreach (var name in Names)
        {
            var marker = $"red4ext/plugins/{name}/{name}.dll";
            Directory.CreateDirectory(Path.GetDirectoryName(At(marker))!);
            var data = Encoding.UTF8.GetBytes(name + " pinned fixture");
            File.WriteAllBytes(At(marker), data);
            frameworks.Add(new(name, "test", "unused", "unused", marker, Hashing.Sha256OfBytes(data)));
            File.WriteAllText(Path.Combine(Path.GetDirectoryName(Log(name))!, Target(name)), "keep " + name + " log");
            if (links) File.CreateSymbolicLink(Log(name), Target(name));
            else File.WriteAllText(Log(name), "regular log");
        }
        return new("0.1.9", [], [], frameworks);
    }

    [Fact]
    public void RecognizedLogLinksArePreservedOutsidePluginScanAndTargetsStayUntouched()
    {
        var package = Setup();
        var receipts = CyberpunkFrameworkLogRecovery.Prepare(Root, package);
        Assert.Equal(3, receipts.Count);
        foreach (var name in Names)
        {
            Assert.False(File.Exists(Log(name)));
            Assert.Equal("keep " + name + " log", File.ReadAllText(Path.Combine(Path.GetDirectoryName(Log(name))!, Target(name))));
            var receipt = receipts.Single(path => Path.GetFileName(path) == name + ".json");
            using var json = JsonDocument.Parse(File.ReadAllText(receipt));
            Assert.Equal(Target(name), json.RootElement.GetProperty("target").GetString());
            var backup = At(json.RootElement.GetProperty("backup").GetString()!);
            Assert.Equal(Target(name), new FileInfo(backup).LinkTarget);
        }
        Assert.Empty(CyberpunkFrameworkLogRecovery.Prepare(Root, package));
    }

    [Fact]
    public void BrokenGeneratedLogLinksAreRecoveredWithoutFollowingMissingTargets()
    {
        var package = Setup();
        foreach (var name in Names)
            File.Delete(Path.Combine(Path.GetDirectoryName(Log(name))!, Target(name)));
        Assert.Equal(3, CyberpunkFrameworkLogRecovery.Prepare(Root, package).Count);
        foreach (var name in Names) Assert.Null(new FileInfo(Log(name)).LinkTarget);
    }

    [Fact]
    public void LinkedFrameworkDirectoryIsRefusedWithoutWritingIntoItsTarget()
    {
        var package = Setup();
        var original = At("red4ext/plugins/ArchiveXL");
        var outside = At("outside");
        Directory.Move(original, outside);
        Directory.CreateSymbolicLink(original, outside);
        Assert.Throws<InvalidDataException>(() => CyberpunkFrameworkLogRecovery.Prepare(Root, package));
        Assert.Equal(Target("ArchiveXL"), new FileInfo(Path.Combine(outside, "ArchiveXL.log")).LinkTarget);
        Assert.False(Directory.Exists(At(".avrcade")));
    }

    [Fact]
    public void DryRunLeavesLinksAndBackupDirectoryUntouched()
    {
        var package = Setup();
        Assert.Empty(CyberpunkFrameworkLogRecovery.Prepare(Root, package, dryRun: true));
        foreach (var name in Names) Assert.Equal(Target(name), new FileInfo(Log(name)).LinkTarget);
        Assert.False(Directory.Exists(At(".avrcade")));
    }

    [Fact]
    public void RegularLogFilesAreNotMovedOrReplaced()
    {
        var package = Setup(links: false);
        Assert.Empty(CyberpunkFrameworkLogRecovery.Prepare(Root, package));
        foreach (var name in Names) Assert.Equal("regular log", File.ReadAllText(Log(name)));
        Assert.False(Directory.Exists(At(".avrcade")));
    }

    [Fact]
    public void ForeignLogLinkIsPreservedAndNoOtherLinkMoves()
    {
        var package = Setup();
        File.Delete(Log("Codeware"));
        var foreign = At("personal.txt");
        File.WriteAllText(foreign, "keep personal file");
        File.CreateSymbolicLink(Log("Codeware"), foreign);
        Assert.Throws<InvalidDataException>(() => CyberpunkFrameworkLogRecovery.Prepare(Root, package));
        Assert.Equal(foreign, new FileInfo(Log("Codeware")).LinkTarget);
        Assert.Equal(Target("ArchiveXL"), new FileInfo(Log("ArchiveXL")).LinkTarget);
        Assert.Equal("keep personal file", File.ReadAllText(foreign));
    }

    [Fact]
    public void UnknownFrameworkBytesDoNotAuthorizeLogLinkRecovery()
    {
        var package = Setup();
        File.WriteAllText(At("red4ext/plugins/ArchiveXL/ArchiveXL.dll"), "unknown version");
        Assert.Throws<InvalidDataException>(() => CyberpunkFrameworkLogRecovery.Prepare(Root, package));
        Assert.Equal(Target("ArchiveXL"), new FileInfo(Log("ArchiveXL")).LinkTarget);
        Assert.False(Directory.Exists(At(".avrcade")));
    }
}
