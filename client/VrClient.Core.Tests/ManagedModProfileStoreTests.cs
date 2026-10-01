using VrClient.Core.ModManagers;

namespace VrClient.Core.Tests;

public sealed class ManagedModProfileStoreTests
{
    [Fact]
    public void Creates_and_lists_only_avrcade_owned_profiles_for_supported_games()
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-managed-test-" + Guid.NewGuid().ToString("N"));
        try
        {
            var store = new ManagedModProfileStore(root);
            var peak = store.Create("peak", "VR and friends");
            store.Create("big-walk", "Big Walk VR");
            Assert.StartsWith(Path.Combine(root, "peak") + Path.DirectorySeparatorChar,
                peak.Directory, StringComparison.OrdinalIgnoreCase);
            Assert.True(File.Exists(Path.Combine(peak.Directory, "profile.json")));
            Assert.Equal(peak, Assert.Single(store.List("peak")));
            Assert.Empty(store.List("content-warning"));
            Assert.Empty(store.List("rv-there-yet"));
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Theory]
    [InlineData("rv-there-yet", "VR")]
    [InlineData("peak/../../elsewhere", "VR")]
    [InlineData("peak", "")]
    public void Rejects_unsupported_games_and_invalid_names(string slug, string name)
    {
        var root = Path.Combine(Path.GetTempPath(), "avrcade-managed-test-" + Guid.NewGuid().ToString("N"));
        var store = new ManagedModProfileStore(root);
        Assert.Throws<ArgumentException>(() => store.Create(slug, name));
        Assert.False(Directory.Exists(root));
    }
}
