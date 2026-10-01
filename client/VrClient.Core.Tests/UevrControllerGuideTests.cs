using VrClient.Core.Config;
using VrClient.Core.Unreal;

namespace VrClient.Core.Tests;

public sealed class UevrControllerGuideTests
{
    [Fact]
    public void GuideEscapesLuaStringsAndIncludesBothFamilies()
    {
        var guide = UevrControllerGuide.Build(Reference());
        Assert.Contains("uevr.sdk.callbacks.on_draw_ui", guide);
        Assert.Contains("Quest Touch", guide);
        Assert.Contains("Valve Index", guide);
        Assert.Contains("Xbox B", guide);
        Assert.Contains("\\\"quoted\\\"\\n", guide);
        Assert.DoesNotContain("on_xinput", guide);
    }

    [Fact]
    public void InstallPreservesSettingsAndBacksUpChangedGuideOnly()
    {
        var root = Path.Combine(Path.GetTempPath(), $"vrclient-guide-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        try
        {
            File.WriteAllText(Path.Combine(root, "config.txt"), "user camera tuning");
            var path = UevrControllerGuide.Install(Reference(), root);
            var first = File.ReadAllText(path);
            UevrControllerGuide.Install(Reference(), root);
            Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(path)!, "*.bak"));
            File.WriteAllText(path, "user-edited guide");
            UevrControllerGuide.Install(Reference(), root);
            var backup = Assert.Single(Directory.GetFiles(Path.GetDirectoryName(path)!, "*.bak"));
            Assert.Equal("user-edited guide", File.ReadAllText(backup));
            Assert.Equal(first, File.ReadAllText(path));
            Assert.Equal("user camera tuning", File.ReadAllText(Path.Combine(root, "config.txt")));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static ControllerReference Reference() => new("source_verified", "rv-there-yet", "Controls",
        [new("quest-touch", "Quest Touch", [new("left_primary", "X", "Xbox B")]),
         new("valve-index", "Valve Index", [new("left_primary", "Left A", "Xbox B")])],
        Note: "A \"quoted\"\nline");
}
