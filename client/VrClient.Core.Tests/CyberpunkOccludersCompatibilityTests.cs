using System.Text.Json;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkOccludersCompatibilityTests : TempTree
{
    [Fact]
    public void RewriteKeepsOrdinaryMeshOcclusionAndSkipsOnlyTheRemovedInstancedFields()
    {
        var source = "ordinary mesh code\n" +
            "\t\t\telse\n\t\t\t{\n\t\t\t\tlet instMesh = node as worldInstancedMeshNode;\n" +
            "\t\t\t\tif(IsDefined(instMesh))\n\t\t\t\t{\n" +
            "\t\t\t\t\tif(Equals(instMesh.occluderType, visWorldOccluderType.None) && !Equals(instMesh.meshAppearance,  n\"transparent\"))\n" +
            "\t\t\t\t\t{\n\t\t\t\t\t\tinstMesh.occluderAutohideDistanceScale = this.distanceScale;\n" +
            "\t\t\t\t\t\tinstMesh.occluderType = visWorldOccluderType.Default;\n" +
            "\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n" + "index += 1;\n";
        var result = CyberpunkOccludersCompatibility.RewriteInstancedBranch(source.Replace("\n", "\r\n"));
        Assert.Contains("ordinary mesh code", result);
        Assert.Contains("index += 1;", result);
        Assert.Contains("instanced meshes use engine occlusion", result);
        Assert.DoesNotContain("instMesh.", result);
    }

    [Theory]
    [InlineData("unknown implementation")]
    [InlineData("let instMesh = node as worldInstancedMeshNode; instMesh.occluderType = 0;")]
    public void UnknownBranchIsNotRewritten(string source) =>
        Assert.Throws<InvalidDataException>(() => CyberpunkOccludersCompatibility.RewriteInstancedBranch(source));

    [Fact]
    public void ModifiedUnsupportedScriptIsPreservedAndDoesNotPassReadiness()
    {
        const string relative = "r6/scripts/MoreOccluders.reds";
        var payload = CyberpunkUpstreamSession.PayloadPath(Root, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(payload)!);
        File.WriteAllText(payload, "custom instMesh.occluderType");
        File.WriteAllText(CyberpunkUpstreamSession.StatePath(Root), JsonSerializer.Serialize(
            new CyberpunkUpstreamSession.InstallState("0.1.9", [relative], [], [relative])));
        var receiptPath = CyberpunkNexusRequirements.ReceiptPath(Root);
        var receipt = JsonSerializer.Serialize(new CyberpunkNexusRequirements.Receipt("sosuine", "source", "archive",
            new() { [relative] = Hashing.Sha256OfFile(payload) }));
        File.WriteAllText(receiptPath, receipt);
        Assert.False(CyberpunkNexusRequirements.Installed(Root));
        Assert.Throws<IOException>(() => CyberpunkOccludersCompatibility.Apply(Root));
        Assert.Equal("custom instMesh.occluderType", File.ReadAllText(payload));
        Assert.Equal(receipt, File.ReadAllText(receiptPath));
    }
}
