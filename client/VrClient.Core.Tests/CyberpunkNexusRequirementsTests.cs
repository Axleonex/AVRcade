using System.IO.Compression;
using System.Text.Json;
using VrClient.Core.App;
using VrClient.Core.Redengine;

namespace VrClient.Core.Tests;

public sealed class CyberpunkNexusRequirementsTests : TempTree
{
    private const string Script = "r6/scripts/MoreOccluders/more_occluders.reds";
    private const string Stereo = "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_Stereo.dll";
    private string Game => Path.Combine(Root, "cyberpunk");
    private string Downloads => Path.Combine(Root, "downloads");

    private void Setup()
    {
        Directory.CreateDirectory(Downloads);
        var payload = CyberpunkUpstreamSession.PayloadPath(Game, Stereo);
        Directory.CreateDirectory(Path.GetDirectoryName(payload)!);
        File.WriteAllText(payload, "renderer");
        File.WriteAllText(CyberpunkUpstreamSession.StatePath(Game), JsonSerializer.Serialize(
            new CyberpunkUpstreamSession.InstallState("0.1.7", [Stereo], [])));
    }

    private string Zip(string entry = Script, string content = "// More Occluders by sosuine")
    {
        var path = Path.Combine(Downloads, "More Occluders (Scripted)-33136-0-3-1787760000.zip");
        using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
        using var writer = new StreamWriter(archive.CreateEntry(entry).Open());
        writer.Write(content);
        return path;
    }

    [Fact]
    public async Task CachedOfficialNamedDownloadInstallsOnlyForCyberpunkVr()
    {
        Setup();
        Zip();
        var otherGame = Path.Combine(Root, "another-game", "unchanged.txt");
        Directory.CreateDirectory(Path.GetDirectoryName(otherGame)!);
        File.WriteAllText(otherGame, "other game");
        var result = await new CyberpunkNexusRequirements().InstallAsync(Game,
            _ => throw new Exception("A completed cache download should not open the browser"), [Downloads]);
        Assert.True(result.Ok);
        Assert.True(CyberpunkNexusRequirements.Installed(Game));
        Assert.False(File.Exists(Path.Combine(Game, Script)));
        using (var session = new CyberpunkUpstreamSession(Game, vr: true))
            Assert.True(File.Exists(Path.Combine(Game, Script)));
        Assert.False(File.Exists(Path.Combine(Game, Script)));
        Assert.Equal("other game", File.ReadAllText(otherGame));
        var receipt = JsonSerializer.Deserialize<CyberpunkNexusRequirements.Receipt>(
            File.ReadAllText(CyberpunkNexusRequirements.ReceiptPath(Game)))!;
        Assert.Equal("sosuine", receipt.Creator);
        Assert.Equal(CyberpunkNexusRequirements.DownloadUrl, receipt.Source);
        Assert.Equal(64, receipt.ArchiveSha256.Length);
        CyberpunkUpstreamInstaller.RestorePrevious(Game);
        CyberpunkNexusRequirements.RemoveStoredFiles(Game);
        Assert.False(File.Exists(CyberpunkUpstreamSession.PayloadPath(Game, Script)));
        Assert.Equal("other game", File.ReadAllText(otherGame));
    }

    [Fact]
    public async Task FreeBrowserDownloadIsAutomaticallyDetectedAfterOpeningNexus()
    {
        Setup();
        string? opened = null;
        var result = await new CyberpunkNexusRequirements().InstallAsync(Game,
            url => { opened = url; Zip(); }, [Downloads], timeout: TimeSpan.FromSeconds(3));
        Assert.True(result.Ok);
        Assert.Equal(CyberpunkNexusRequirements.DownloadUrl, opened);
    }

    [Theory]
    [InlineData("r6/scripts/../../../../outside/occluders.reds")]
    [InlineData("bin/x64/unexpected.dll")]
    [InlineData("archive/pc/mod/occluders.archive")]
    public void WrongPackageContentsCannotWriteIntoGameOrOtherFolders(string entry)
    {
        Setup();
        var oldState = File.ReadAllText(CyberpunkUpstreamSession.StatePath(Game));
        Assert.Throws<InvalidDataException>(() => CyberpunkNexusRequirements.Import(Game, Zip(entry)));
        Assert.Equal(oldState, File.ReadAllText(CyberpunkUpstreamSession.StatePath(Game)));
        Assert.False(CyberpunkNexusRequirements.Installed(Game));
    }

    [Fact]
    public async Task AnotherGameCannotStartThisInstallerOrOpenNexus()
    {
        Setup();
        var result = await new CyberpunkNexusRequirements().InstallAsync(Path.Combine(Root, "fallout"),
            _ => throw new Exception("Must not open Nexus for another game"), [Downloads]);
        Assert.False(result.Ok);
    }

    [Fact]
    public async Task CancellationDoesNotInstallAnything()
    {
        Setup();
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => new CyberpunkNexusRequirements().InstallAsync(
            Game, _ => throw new Exception("Must not open Nexus after cancellation"), [Downloads], ct: cancelled.Token));
        Assert.False(CyberpunkNexusRequirements.Installed(Game));
    }

    [Fact]
    public void MissingRequirementChangesOnlyTheCyberpunkVrButtons()
    {
        var modes = PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(true, null, true, true, true,
            RequirementsInstalled: false));
        Assert.All(modes.Take(2), mode => Assert.Equal(PlayModeSetup.InstallCyberpunkRequirements, mode.Setup));
        Assert.All(modes.Skip(2), mode => Assert.True(mode.CanLaunch));
    }
}
