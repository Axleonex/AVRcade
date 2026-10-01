using System;
using System.IO;
using System.Linq;
using VrClient.Core.Discovery;
using VrClient.Core.Launch;
using VrClient.Core.Model;
using Xunit;

public class DiscoveryLaunchTests
{
    private static string NewTempDir()
    {
        var dir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static string BuildSteamFixture(string root)
    {
        var steamapps = Path.Combine(root, "steamapps");
        Directory.CreateDirectory(steamapps);
        var escapedRoot = root.Replace("\\", "\\\\");
        File.WriteAllText(Path.Combine(steamapps, "libraryfolders.vdf"),
            "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + escapedRoot + "\"\n\t}\n}\n");
        File.WriteAllText(Path.Combine(steamapps, "appmanifest_3241660.acf"),
            "\"AppState\"\n{\n\t\"appid\"\t\t\"3241660\"\n\t\"installdir\"\t\t\"REPO\"\n}\n");
        var gameDir = Path.Combine(steamapps, "common", "REPO");
        Directory.CreateDirectory(gameDir);
        File.WriteAllBytes(Path.Combine(gameDir, "REPO.exe"), Array.Empty<byte>());
        return gameDir;
    }

    [Fact]
    public void FindGame_returns_repo_path_from_fixture_layout_and_null_for_missing_app()
    {
        var root = NewTempDir();
        try
        {
            var expectedGameDir = BuildSteamFixture(root);
            var scanner = new SteamLibraryScanner();

            var found = scanner.FindGame("3241660", "REPO.exe", root);
            Assert.NotNull(found);
            Assert.Equal(Path.GetFullPath(expectedGameDir), Path.GetFullPath(found!.InstallDir));
            Assert.Equal("REPO.exe", found.ExeName);

            Assert.Null(scanner.FindGame("999999", "REPO.exe", root));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void ManualSteamFolderRequiresMatchingManifestAndPersistsOnlyTheLibraryRoot()
    {
        var root = NewTempDir();
        try
        {
            var gameDir = BuildSteamFixture(root);
            var preference = Path.Combine(root, "saved-libraries.txt");
            var scanner = new SteamLibraryScanner(preference);
            Assert.False(scanner.RememberGameFolder("999999", "REPO.exe", gameDir));
            Assert.False(File.Exists(preference));
            Assert.True(scanner.RememberGameFolder("3241660", "REPO.exe", gameDir));
            Assert.Equal(root, Assert.Single(File.ReadAllLines(preference)));
            Assert.Equal(gameDir, scanner.FindGame("3241660", "REPO.exe")?.InstallDir);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void FindGame_resolves_nested_executable_and_build_from_secondary_library()
    {
        var primary = NewTempDir();
        var secondary = NewTempDir();
        try
        {
            var primarySteamApps = Path.Combine(primary, "steamapps");
            Directory.CreateDirectory(primarySteamApps);
            var escapedSecondary = secondary.Replace("\\", "\\\\");
            File.WriteAllText(Path.Combine(primarySteamApps, "libraryfolders.vdf"),
                "\"libraryfolders\"\n{\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" +
                escapedSecondary + "\"\n\t}\n}\n");

            var secondarySteamApps = Path.Combine(secondary, "steamapps");
            Directory.CreateDirectory(secondarySteamApps);
            var manifest = Path.Combine(secondarySteamApps, "appmanifest_4704690.acf");
            File.WriteAllText(manifest,
                "\"AppState\"\n{\n\t\"appid\"\t\t\"4704690\"\n" +
                "\t\"installdir\"\t\t\"MECCHA CHAMELEON\"\n" +
                "\t\"buildid\"\t\t\"24396565\"\n}\n");
            var gameDir = Path.Combine(
                secondarySteamApps, "common", "MECCHA CHAMELEON");
            var shippingRelative = Path.Combine(
                "Chameleon", "Binaries", "Win64",
                "PenguinHotel-Win64-Shipping.exe");
            var shippingExe = Path.Combine(gameDir, shippingRelative);
            Directory.CreateDirectory(Path.GetDirectoryName(shippingExe)!);
            File.WriteAllBytes(shippingExe, [1, 2, 3]);

            var found = new SteamLibraryScanner().FindGame(
                "4704690", shippingRelative, primary);

            Assert.NotNull(found);
            Assert.Equal(Path.GetFullPath(gameDir), Path.GetFullPath(found!.InstallDir));
            Assert.Equal(Path.GetFullPath(shippingExe), Path.GetFullPath(found.ExecutablePath));
            Assert.Equal(Path.GetFullPath(manifest), Path.GetFullPath(found.ManifestPath));
            Assert.Equal("24396565", found.BuildId);
        }
        finally
        {
            Directory.Delete(primary, recursive: true);
            Directory.Delete(secondary, recursive: true);
        }
    }

    [Fact]
    public void FindOfflineLibrary_reports_listed_game_only_while_its_drive_is_unavailable()
    {
        var primary = NewTempDir();
        var detached = Path.Combine(primary, "detached-library");
        try
        {
            var steamapps = Path.Combine(primary, "steamapps");
            Directory.CreateDirectory(steamapps);
            File.WriteAllText(Path.Combine(steamapps, "libraryfolders.vdf"),
                "\"libraryfolders\"\n{\n\"1\"\n{\n\"path\"\t\"" +
                detached.Replace("\\", "\\\\") +
                "\"\n\"apps\"\n{\n\"1478500\"\t\"123\"\n}\n}\n}\n");

            var scanner = new SteamLibraryScanner();
            Assert.Equal(detached, scanner.FindOfflineLibrary("1478500", primary));
            Assert.Null(scanner.FindOfflineLibrary("3949040", primary));
            Directory.CreateDirectory(detached);
            Assert.Null(scanner.FindOfflineLibrary("1478500", primary));
        }
        finally { Directory.Delete(primary, recursive: true); }
    }

    private static SafetyVerdict Verdict(Verdict v) => new(v, "test_reason", "test.key", "test explanation");

    [Fact]
    public void Launch_refuses_block_and_unacknowledged_warn_and_dry_runs_allow()
    {
        var launcher = new GameLauncher();

        var blocked = launcher.Plan(@"C:\nonexistent", Verdict(VrClient.Core.Model.Verdict.Block), modInstalled: true);
        var ex = Assert.Throws<InvalidOperationException>(() => launcher.Launch(blocked, acknowledgeWarn: false, dryRun: true));
        Assert.Contains("test_reason", ex.Message);

        var allowed = launcher.Plan(@"C:\nonexistent", Verdict(VrClient.Core.Model.Verdict.Allow), modInstalled: true);
        Assert.Equal(-1, launcher.Launch(allowed, acknowledgeWarn: false, dryRun: true));

        var warned = launcher.Plan(@"C:\nonexistent", Verdict(VrClient.Core.Model.Verdict.Warn), modInstalled: true);
        Assert.Throws<InvalidOperationException>(() => launcher.Launch(warned, acknowledgeWarn: false, dryRun: true));
        Assert.Equal(-1, launcher.Launch(warned, acknowledgeWarn: true, dryRun: true));
    }

    [Fact]
    public void WriteEvidence_writes_json_with_confirmation_fields_and_slug()
    {
        var artifactsRoot = NewTempDir();
        try
        {
            new GameLauncher().WriteEvidence(artifactsRoot, new SessionEvidence(
                "repo", new string('a', 64), "2026-07-02T00:00:00Z",
                UserConfirmedVrStereo: false, UserConfirmedHeadTracking: false,
                Notes: "unit test"));

            var files = Directory.GetFiles(Path.Combine(artifactsRoot, "friendslop", "repo"));
            var evidence = Assert.Single(files);
            var json = File.ReadAllText(evidence);
            Assert.Contains("user_confirmed_vr_stereo", json);
            Assert.Contains("\"repo\"", json);
        }
        finally { Directory.Delete(artifactsRoot, recursive: true); }
    }
}
