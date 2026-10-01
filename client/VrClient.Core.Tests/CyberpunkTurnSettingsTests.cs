using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkTurnSettingsTests
{
    [Fact]
    public void WritesPluginSettingsWithoutDiscardingOtherOptions()
    {
        var root = Path.Combine(Path.GetTempPath(), "vrclient-turn-" + Guid.NewGuid());
        var path = CyberpunkTurnSettings.SettingsPath(root);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        try
        {
            File.WriteAllText(path,
                "xr_head_offset_x=0.1234\r\nxr_snap_turn=0\r\nxr_snap_turn_angle_deg=30.00\r\n# Keep this\r\n");
            CyberpunkTurnSettings.Write(root, new CyberpunkTurnPreference(CyberpunkTurnMode.Snap, 45));
            Assert.Equal(new CyberpunkTurnPreference(CyberpunkTurnMode.Snap, 45),
                CyberpunkTurnSettings.Read(root));
            var content = File.ReadAllText(path);
            Assert.Contains("xr_head_offset_x=0.1234\r\n", content);
            Assert.Contains("# Keep this\r\n", content);
            Assert.Contains("xr_snap_turn=1\r\n", content);
            Assert.Contains("xr_snap_turn_angle_deg=45.00\r\n", content);

            CyberpunkTurnSettings.Write(root, new CyberpunkTurnPreference(CyberpunkTurnMode.Smooth, 45));
            Assert.Equal(CyberpunkTurnMode.Smooth, CyberpunkTurnSettings.Read(root).Mode);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void RefusesToCreatePluginSettingsWhenConversionIsMissing()
    {
        var root = Path.Combine(Path.GetTempPath(), "vrclient-turn-" + Guid.NewGuid());
        Assert.Throws<FileNotFoundException>(() =>
            CyberpunkTurnSettings.Write(root, new CyberpunkTurnPreference(CyberpunkTurnMode.Snap, 30)));
        Assert.False(File.Exists(CyberpunkTurnSettings.SettingsPath(root)));
    }
}
