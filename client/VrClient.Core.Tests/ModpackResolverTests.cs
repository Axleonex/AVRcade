using System.Threading;
using System.Threading.Tasks;
using VrClient.Core.Modpack;
using VrClient.Core.Model;
using Xunit;

public class ModpackResolverTests
{
    private sealed class FixtureApi : IThunderstoreApi
    {
        private readonly string _json;
        public FixtureApi(string json) => _json = json;
        public Task<string> GetPackageIndexJsonAsync(string community, CancellationToken ct = default)
            => Task.FromResult(_json);
    }

    [Fact]
    public async Task Resolves_repo_modpack_deps_first_with_pinned_versions()
    {
        var spec = ModpackResolver.LoadSpec("config/modpacks/repo.modpack.json");
        var json = System.IO.File.ReadAllText("Fixtures/repo-package-index.json");
        var resolver = new ModpackResolver(new FixtureApi(json));

        var lockfile = await resolver.ResolveAsync(spec, "2026-07-02T00:00:00Z");

        Assert.Equal("repo", lockfile.GameSlug);
        Assert.Equal(2, lockfile.Packages.Count);
        // Dependency (BepInExPack) must come before the plugin (RepoXR).
        Assert.Equal("BepInExPack", lockfile.Packages[0].Name);
        Assert.Equal("5.4.2100", lockfile.Packages[0].Version);
        Assert.Equal("RepoXR", lockfile.Packages[1].Name);
        Assert.Equal("1.2.2", lockfile.Packages[1].Version);
        Assert.StartsWith("https://", lockfile.Packages[1].DownloadUrl);
    }

    [Fact]
    public async Task Resolves_game_specific_loader_and_upgrades_conflicting_transitive_minimum()
    {
        var json = """
            [
              {"full_name":"VR-PeakVR","owner":"VR","name":"PeakVR","versions":[
                {"version_number":"1.0.0","download_url":"https://example/vr.zip","file_size":1,
                 "dependencies":["BepInEx-BepInExPack_PEAK-5.4.75301","Tools-Core-1.0.0"]}]},
              {"full_name":"BepInEx-BepInExPack_PEAK","owner":"BepInEx","name":"BepInExPack_PEAK","versions":[
                {"version_number":"5.4.75301","download_url":"https://example/pack.zip","file_size":1,"dependencies":[]}]},
              {"full_name":"Tools-Core","owner":"Tools","name":"Core","versions":[
                {"version_number":"1.0.0","download_url":"https://example/core.zip","file_size":1,
                 "dependencies":["Tools-SoftFix-1.0.0","MonoDetour-Bridge-0.6.7"]}]},
              {"full_name":"Tools-SoftFix","owner":"Tools","name":"SoftFix","versions":[
                {"version_number":"1.0.0","download_url":"https://example/fix.zip","file_size":1,
                 "dependencies":["BepInEx-BepInExPack-5.4.2100","MonoDetour-Bridge-0.6.6"]}]},
              {"full_name":"MonoDetour-Bridge","owner":"MonoDetour","name":"Bridge","versions":[
                {"version_number":"0.6.7","download_url":"https://example/bridge7.zip","file_size":1,"dependencies":[]},
                {"version_number":"0.6.6","download_url":"https://example/bridge6.zip","file_size":1,"dependencies":[]}]}
            ]
            """;
        var spec = new ModpackSpec("peak", "3527290", "peak",
            [new ModDependency("VR", "PeakVR")]);

        var lockfile = await new ModpackResolver(new FixtureApi(json))
            .ResolveAsync(spec, "2026-09-23T00:00:00Z");

        Assert.Equal(5, lockfile.Packages.Count);
        Assert.Equal("BepInExPack_PEAK", lockfile.Packages[0].Name);
        Assert.Equal("0.6.7", Assert.Single(lockfile.Packages, p => p.Name == "Bridge").Version);
        Assert.Equal("PeakVR", lockfile.Packages[^1].Name);
    }
}
