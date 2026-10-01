using System;
using System.IO;
using System.Linq;
using VrClient.Core.Catalog;
using Xunit;

public class CatalogTests
{
    private static string Fx(params string[] parts)
        => Path.Combine(new[] { AppContext.BaseDirectory, "Fixtures", "demo-coop", "config" }.Concat(parts).ToArray());

    [Fact]
    public void Discovers_demo_coop_with_all_four_files()
    {
        var catalog = new GameCatalog();
        var games = catalog.Discover(Fx("modpacks"), Fx("games"), Fx("profiles"));

        var game = Assert.Single(games);
        Assert.Equal("demo-coop", game.Slug);
        Assert.Equal("999999", game.SteamAppId);
        Assert.Equal("demo-coop", game.Community);
        Assert.True(game.HasModpack);
        Assert.True(game.HasComfortMap);
        Assert.True(game.HasGameConfig);
        Assert.True(game.HasProfile);
    }
}
