using VrClient.Core.Discovery;

namespace VrClient.Core.Tests;

public sealed class SteamLibraryArtworkLocatorTests
{
    private static string NewTempDirectory()
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            $"vrclient-steam-art-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        return directory;
    }

    [Fact]
    public void FindPortrait_resolves_flat_cache_layout()
    {
        var root = NewTempDirectory();
        try
        {
            var expected = Path.Combine(
                root, "appcache", "librarycache", "1174180", "library_600x900.jpg");
            Directory.CreateDirectory(Path.GetDirectoryName(expected)!);
            File.WriteAllText(expected, "fixture");

            var actual = SteamLibraryArtworkLocator.FindPortrait("1174180", [root]);

            Assert.Equal(Path.GetFullPath(expected), actual);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void FindPortrait_resolves_nested_hash_layout()
    {
        var root = NewTempDirectory();
        try
        {
            var expected = Path.Combine(
                root, "appcache", "librarycache", "3241660", "hash", "library_600x900.jpg");
            Directory.CreateDirectory(Path.GetDirectoryName(expected)!);
            File.WriteAllText(expected, "fixture");

            var actual = SteamLibraryArtworkLocator.FindPortrait("3241660", [root]);

            Assert.Equal(Path.GetFullPath(expected), actual);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void FindPortrait_uses_the_legacy_library_capsule_portrait()
    {
        var root = NewTempDirectory();
        try
        {
            var expected = Path.Combine(
                root, "appcache", "librarycache", "1091500", "hash", "library_capsule.jpg");
            Directory.CreateDirectory(Path.GetDirectoryName(expected)!);
            File.WriteAllText(expected, "fixture");

            var actual = SteamLibraryArtworkLocator.FindPortrait("1091500", [root]);

            Assert.Equal(Path.GetFullPath(expected), actual);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void FindPortrait_prefers_jpg_before_png_and_direct_before_nested()
    {
        var root = NewTempDirectory();
        try
        {
            var appCache = Path.Combine(root, "appcache", "librarycache", "1966720");
            var png = Path.Combine(appCache, "library_600x900.png");
            var jpg = Path.Combine(appCache, "library_600x900.jpg");
            var nestedJpg = Path.Combine(appCache, "hash", "library_600x900.jpg");
            Directory.CreateDirectory(Path.GetDirectoryName(nestedJpg)!);
            File.WriteAllText(png, "png");
            File.WriteAllText(jpg, "jpg");
            File.WriteAllText(nestedJpg, "nested");

            var actual = SteamLibraryArtworkLocator.FindPortrait("1966720", [root]);

            Assert.Equal(Path.GetFullPath(jpg), actual);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("manual")]
    [InlineData("12-34")]
    public void FindPortrait_refuses_non_numeric_identifiers(string? appId)
    {
        var root = NewTempDirectory();
        try
        {
            Assert.Null(SteamLibraryArtworkLocator.FindPortrait(appId, [root]));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void FindPortrait_returns_null_when_the_exact_app_cache_is_missing()
    {
        var root = NewTempDirectory();
        try
        {
            Assert.Null(SteamLibraryArtworkLocator.FindPortrait("999999", [root]));
        }
        finally { Directory.Delete(root, recursive: true); }
    }
}
