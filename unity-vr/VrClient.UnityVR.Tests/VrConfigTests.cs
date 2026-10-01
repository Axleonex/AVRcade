using VrClient.UnityVR.Logic;
using Xunit;

public class VrConfigTests
{
    [Fact]
    public void Only_camera_name_set_applies_defaults_to_the_rest()
    {
        var cfg = VrGameConfig.Parse("{ \"CameraObjectName\": \"Main Camera\" }");

        Assert.Equal("Main Camera", cfg.CameraObjectName);
        Assert.False(cfg.DuplicateCamera);
        Assert.Equal(1f, cfg.WorldScale);
        Assert.Empty(cfg.DisableObjects);
        Assert.Equal("follow", cfg.UiMode);
    }

    [Fact]
    public void All_fields_present_are_parsed()
    {
        var cfg = VrGameConfig.Parse(
            "{ \"CameraObjectName\": \"Cam\", \"DuplicateCamera\": true, \"WorldScale\": 1.5, " +
            "\"DisableObjects\": [\"UICanvas\"], \"UiMode\": \"world\" }");

        Assert.Equal("Cam", cfg.CameraObjectName);
        Assert.True(cfg.DuplicateCamera);
        Assert.Equal(1.5f, cfg.WorldScale);
        Assert.Equal(new[] { "UICanvas" }, cfg.DisableObjects);
        Assert.Equal("world", cfg.UiMode);
    }
}
