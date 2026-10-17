using System.IO.Compression;
using VrClient.Core;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using VrClient.Core.Safety;
using VrClient.Core.Unreal;
using Xunit;

public class UevrToolsTests
{
    private sealed class BytesDownloader(byte[] bytes) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
            => Task.FromResult(bytes);
    }

    private sealed class MapDownloader(IReadOnlyDictionary<string, byte[]> bytes) : IHttpDownloader
    {
        public Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
            => Task.FromResult(bytes[url]);
    }

    private static byte[] ZipWith(string name)
    {
        using var stream = new MemoryStream();
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
        {
            var entry = archive.CreateEntry(name);
            using var writer = new StreamWriter(entry.Open());
            writer.Write("fixture");
        }
        return stream.ToArray();
    }

    private static void WriteZip(string path, params (string Name, byte[] Bytes)[] entries)
    {
        using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
        foreach (var item in entries)
        {
            var entry = archive.CreateEntry(item.Name);
            using var stream = entry.Open();
            stream.Write(item.Bytes);
        }
    }

    private static UevrGame ImportGame(string sha256, string contentType = "uevr_config",
        string path = "config.txt")
        => new("rv-there-yet", "rv-there-yet", "RV There Yet?", "3949040",
            "nightly", "Ride.exe", "Ride/Binaries/Win64/Ride-Win64-Shipping.exe",
            false, false, false,
            new UevrProfileSource("https://example.test/repo", new string('a', 40), "profile",
                "https://example.test/tree", "not_declared", "user_import_only",
                [new UevrProfileFile(path, sha256, contentType, "import")]));

    private static string WriteTemp(string content)
    {
        var path = Path.Combine(Path.GetTempPath(), $"uevr-test-{Guid.NewGuid():N}.json");
        File.WriteAllText(path, content);
        return path;
    }

    private const string ReleaseFixture = """
        {
          "schema": "uevr-release/1",
          "pinned_tag": "1.05",
          "download_url": "https://example.test/stable/UEVR.zip",
          "sha256": "aaaa",
          "injector": { "exe": "UEVRInjector.exe" },
          "nightly_channel": {
            "pinned_tag": "nightly-01135-abc",
            "download_url": "https://example.test/nightly/uevr.zip",
            "sha256": "bbbb"
          }
        }
        """;

    [Fact]
    public void LoadReleasePin_stable_and_nightly_channels()
    {
        var path = WriteTemp(ReleaseFixture);
        try
        {
            var stable = UevrTools.LoadReleasePin(path, "stable");
            Assert.Equal("1.05", stable.Tag);
            Assert.Equal("aaaa", stable.Sha256);
            Assert.Equal("UEVRInjector.exe", stable.InjectorExe);

            var nightly = UevrTools.LoadReleasePin(path, "nightly");
            Assert.Equal("nightly-01135-abc", nightly.Tag);
            Assert.Equal("bbbb", nightly.Sha256);
            Assert.Equal("UEVRInjector.exe", nightly.InjectorExe); // injector shared across channels
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void LoadGame_reads_identity_channel_and_shipping_process()
    {
        var path = WriteTemp("""
            {
              "schema": "uevr-game/1",
              "game_slug": "meccha-chameleon",
              "game_id": "meccha-chameleon",
              "display_name": "MECCHA CHAMELEON",
              "steam_app_id": "4704690",
              "uevr_channel": "nightly",
              "executable_roles": {
                "launcher_stub": "PenguinHotel.exe",
                "shipping_binary": "Chameleon\\Binaries\\Win64\\PenguinHotel-Win64-Shipping.exe"
              }
            }
            """);
        try
        {
            var game = UevrTools.LoadGame(path);
            Assert.Equal("meccha-chameleon", game.Slug);
            Assert.Equal("4704690", game.SteamAppId);
            Assert.Equal("nightly", game.Channel);
            Assert.Equal("PenguinHotel-Win64-Shipping", UevrTools.ShippingProcessName(game));
            Assert.False(game.ProfileVerified);
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public async Task Fetchers_verify_hash_and_extract_required_executables()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-fetch-{Guid.NewGuid():N}");
        try
        {
            var uevrBytes = ZipWith("UEVRInjector.exe");
            var uevr = new UevrReleasePin(
                "nightly", "n1", "https://example/uevr.zip",
                Hashing.Sha256OfBytes(uevrBytes), "UEVRInjector.exe");
            var uevrDir = Path.Combine(root, "uevr");
            await UevrTools.EnsureFetchedAsync(
                uevr, new BytesDownloader(uevrBytes), toolsDir: uevrDir);
            Assert.True(UevrTools.IsFetched(uevr, uevrDir));

            var runtimeUrl = "https://example/runtime.zip";
            var desktopUrl = "https://example/desktop.zip";
            var runtimeBytes = ZipWith("dotnet.exe");
            var desktopBytes = ZipWith("shared/Microsoft.WindowsDesktop.App/8.0.29/DesktopFixture.dll");
            var dotnet = new DotNetDesktopPin(
                "8.0.29",
                runtimeUrl, Hashing.Sha512OfBytes(runtimeBytes),
                desktopUrl, Hashing.Sha512OfBytes(desktopBytes));
            var dotnetDir = Path.Combine(root, "dotnet");
            await UevrTools.EnsureDotNetFetchedAsync(
                dotnet, new MapDownloader(new Dictionary<string, byte[]>
                {
                    [runtimeUrl] = runtimeBytes,
                    [desktopUrl] = desktopBytes
                }), toolsDir: dotnetDir);
            Assert.True(UevrTools.IsDotNetFetched(dotnet, dotnetDir));
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public async Task Uevr_fetch_refuses_bad_hash_before_extracting()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-bad-{Guid.NewGuid():N}");
        var bytes = ZipWith("UEVRInjector.exe");
        var pin = new UevrReleasePin(
            "nightly", "n1", "https://example/uevr.zip", new string('0', 64), "UEVRInjector.exe");
        try
        {
            await Assert.ThrowsAsync<HashMismatchException>(() =>
                UevrTools.EnsureFetchedAsync(pin, new BytesDownloader(bytes), toolsDir: root));
            Assert.False(File.Exists(Path.Combine(root, "UEVRInjector.exe")));
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void Channel_defaults_to_stable_when_absent()
    {
        var path = WriteTemp("""
            {
              "game_slug": "x", "game_id": "x", "display_name": "X", "steam_app_id": "1",
              "executable_roles": { "launcher_stub": "X.exe", "shipping_binary": "B\\X-Win64-Shipping.exe" }
            }
            """);
        try
        {
            Assert.Equal("stable", UevrTools.LoadGame(path).Channel);
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void Profile_import_verifies_hash_backs_up_and_preserves_later_user_edits()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-{Guid.NewGuid():N}");
        var archivePath = Path.Combine(root, "profile.zip");
        var destination = Path.Combine(root, "installed");
        var bytes = "reviewed profile"u8.ToArray();
        try
        {
            Directory.CreateDirectory(destination);
            File.WriteAllText(Path.Combine(destination, "config.txt"), "old user profile");
            WriteZip(archivePath, ("config.txt", bytes));
            var result = UevrTools.ImportProfileArchive(
                ImportGame(Hashing.Sha256OfBytes(bytes)), archivePath, destination);
            Assert.Equal(1, result.FileCount);
            Assert.NotNull(result.BackupDirectory);
            Assert.True(File.Exists(result.ManifestPath));
            Assert.True(UevrTools.IsImportedProfileValid(
                ImportGame(Hashing.Sha256OfBytes(bytes)), destination));
            // Existing catalogue games retain their original hash-readiness checks.
            File.WriteAllText(Path.Combine(destination, "config.txt"), "edited after import");
            Assert.False(UevrTools.IsImportedProfileValid(
                ImportGame(Hashing.Sha256OfBytes(bytes)), destination));
            Assert.Equal(0, UevrTools.RollbackImportedProfile(destination));
            Assert.Equal("edited after import", File.ReadAllText(Path.Combine(destination, "config.txt")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Imported_profile_stays_ready_after_uevr_saves_settings_but_not_when_structure_breaks()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-{Guid.NewGuid():N}");
        var archivePath = Path.Combine(root, "profile.zip");
        var destination = Path.Combine(root, "installed");
        var config = "UI_Distance=2.000000\n"u8.ToArray();
        var meta = "{\"name\":\"profile\"}"u8.ToArray();
        var game = ImportGame(Hashing.Sha256OfBytes(config)) with
        {
            Slug = BackroomsTrackedHands.GameSlug,
            ProfileSource = ImportGame(Hashing.Sha256OfBytes(config)).ProfileSource! with
            {
                Files =
                [
                    new UevrProfileFile("config.txt", Hashing.Sha256OfBytes(config), "uevr_config", "import"),
                    new UevrProfileFile("ProfileMeta.json", Hashing.Sha256OfBytes(meta), "metadata", "import")
                ]
            }
        };
        try
        {
            Directory.CreateDirectory(root);
            WriteZip(archivePath, ("config.txt", config), ("ProfileMeta.json", meta));
            UevrTools.ImportProfileArchive(game, archivePath, destination);
            File.WriteAllText(Path.Combine(destination, "config.txt"), "UI_Distance=1.500000\n");
            Assert.True(UevrTools.IsImportedProfileValid(game, destination));

            File.WriteAllText(Path.Combine(destination, "ProfileMeta.json"), "{\"name\":\"changed\"}");
            Assert.False(UevrTools.IsImportedProfileValid(game, destination));
            File.WriteAllBytes(Path.Combine(destination, "ProfileMeta.json"), meta);
            Assert.True(UevrTools.IsImportedProfileValid(game, destination));

            File.Delete(Path.Combine(destination, "config.txt"));
            Assert.False(UevrTools.IsImportedProfileValid(game, destination));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Profile_import_applies_headset_tested_config_overrides_and_rolls_them_back()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-{Guid.NewGuid():N}");
        var archivePath = Path.Combine(root, "profile.zip");
        var destination = Path.Combine(root, "installed");
        var bytes = "A=1\nUI_Distance=2.000000\nUI_Size=2.000000\n"u8.ToArray();
        var game = ImportGame(Hashing.Sha256OfBytes(bytes)) with
        {
            ConfigOverrides = new Dictionary<string, string>
            {
                ["UI_Distance"] = "1.500000",
                ["UI_Size"] = "1.000000"
            }
        };
        try
        {
            Directory.CreateDirectory(root);
            WriteZip(archivePath, ("config.txt", bytes));
            UevrTools.ImportProfileArchive(game, archivePath, destination);
            Assert.Equal("A=1\nUI_Distance=1.500000\nUI_Size=1.000000\n",
                File.ReadAllText(Path.Combine(destination, "config.txt")));
            Assert.True(UevrTools.IsImportedProfileValid(game, destination));
            Assert.Equal(1, UevrTools.RollbackImportedProfile(destination));
            Assert.False(File.Exists(Path.Combine(destination, "config.txt")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Config_overrides_keep_line_endings_and_append_missing_keys()
    {
        var overrides = new Dictionary<string, string> { ["UI_Size"] = "1.000000", ["New_Key"] = "x" };
        Assert.Equal("UI_Size=1.000000\r\nOther=2\r\nNew_Key=x\r\n",
            UevrTools.ApplyConfigOverrides("UI_Size=2.000000\r\nOther=2\r\n", overrides));
        Assert.Equal("UI_Size=1.000000\nNew_Key=x",
            UevrTools.ApplyConfigOverrides("UI_Size=2.0", overrides));
    }

    [Fact]
    public void Escape_the_backrooms_keeps_compact_world_fixed_ui_settings()
    {
        var game = UevrTools.LoadGame(Path.Combine(VrClient.Core.Tests.TestRepoRoot.Find(),
            "config", "unreal", "escape-the-backrooms.uevr.json"));
        Assert.Equal("1.500000", game.ConfigOverrides!["UI_Distance"]);
        Assert.Equal("1.000000", game.ConfigOverrides["UI_Size"]);
        Assert.Equal("false", game.ConfigOverrides["UI_FollowView"]);
        Assert.Equal("false", game.ConfigOverrides["UI_Framework_FollowView"]);
        Assert.Equal("false", game.ConfigOverrides["VR_DecoupledPitchUIAdjust"]);
    }

    [Fact]
    public void Invalid_config_overrides_are_refused()
    {
        var path = WriteTemp("""
            {
              "game_slug": "x", "game_id": "x", "display_name": "X", "steam_app_id": "1",
              "executable_roles": { "launcher_stub": "X.exe", "shipping_binary": "B\\X-Win64-Shipping.exe" },
              "uevr_profile": { "config_overrides": { "UI_Size": "1\nVR_Evil=1" } }
            }
            """);
        try
        {
            Assert.Throws<InvalidDataException>(() => UevrTools.LoadGame(path));
        }
        finally { File.Delete(path); }
    }

    [Fact]
    public void Profile_import_rolls_back_unchanged_imported_file()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-{Guid.NewGuid():N}");
        var archivePath = Path.Combine(root, "profile.zip");
        var destination = Path.Combine(root, "installed");
        var bytes = "reviewed profile"u8.ToArray();
        try
        {
            Directory.CreateDirectory(destination);
            File.WriteAllText(Path.Combine(destination, "config.txt"), "old user profile");
            WriteZip(archivePath, ("config.txt", bytes));
            UevrTools.ImportProfileArchive(ImportGame(Hashing.Sha256OfBytes(bytes)), archivePath, destination);
            Assert.Equal(1, UevrTools.RollbackImportedProfile(destination));
            Assert.Equal("old user profile", File.ReadAllText(Path.Combine(destination, "config.txt")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public async Task Profile_download_fetches_pinned_source_and_refuses_changed_content()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-download-{Guid.NewGuid():N}");
        var destination = Path.Combine(root, "installed");
        var bytes = "reviewed profile"u8.ToArray();
        var game = ImportGame(Hashing.Sha256OfBytes(bytes)) with
        {
            ProfileSource = ImportGame(Hashing.Sha256OfBytes(bytes)).ProfileSource! with
            {
                Repository = "https://github.com/uevr-profiles/repo",
                ProfileId = "a5d68bf1-cb90-09ba-0ef0-659645fb5007"
            }
        };
        var source = game.ProfileSource!;
        var url = $"https://raw.githubusercontent.com/uevr-profiles/repo/{source.Revision}/{source.ProfileId}/config.txt";
        try
        {
            var imported = await UevrTools.DownloadAndImportProfileAsync(
                game, new MapDownloader(new Dictionary<string, byte[]> { [url] = bytes }), destination);
            Assert.Equal(1, imported.FileCount);
            Assert.True(UevrTools.IsImportedProfileValid(game, destination));

            var changed = Path.Combine(root, "changed");
            await Assert.ThrowsAsync<HashMismatchException>(() => UevrTools.DownloadAndImportProfileAsync(
                game, new MapDownloader(new Dictionary<string, byte[]> { [url] = "changed"u8.ToArray() }), changed));
            Assert.False(Directory.Exists(changed));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Profile_import_rejects_hash_mismatch_traversal_and_code()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(root);
            var mismatch = Path.Combine(root, "mismatch.zip");
            WriteZip(mismatch, ("config.txt", "wrong"u8.ToArray()));
            Assert.Throws<HashMismatchException>(() => UevrTools.ImportProfileArchive(
                ImportGame(new string('0', 64)), mismatch, Path.Combine(root, "mismatch")));
            var traversal = Path.Combine(root, "traversal.zip");
            WriteZip(traversal, ("../escape.txt", "bad"u8.ToArray()));
            Assert.Throws<InvalidDataException>(() => UevrTools.ImportProfileArchive(
                ImportGame(new string('0', 64)), traversal, Path.Combine(root, "traversal")));
            var code = Path.Combine(root, "code.zip");
            var codeBytes = "code"u8.ToArray();
            WriteZip(code, ("config.txt", codeBytes));
            Assert.Throws<InvalidDataException>(() => UevrTools.ImportProfileArchive(
                ImportGame(Hashing.Sha256OfBytes(codeBytes), "executable"), code,
                Path.Combine(root, "code")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Profile_import_accepts_only_json_object_hook_data_under_the_uobjecthook_folder()
    {
        var root = Path.Combine(Path.GetTempPath(), $"uevr-profile-hook-{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(root);
            var valid = "{\"camera\":{}}"u8.ToArray();
            var archive = Path.Combine(root, "valid.zip");
            WriteZip(archive, ("uobjecthook/camera_state.json", valid));
            var game = ImportGame(Hashing.Sha256OfBytes(valid), "uevr_object_hook",
                "uobjecthook/camera_state.json");
            var result = UevrTools.ImportProfileArchive(game, archive, Path.Combine(root, "valid"));
            Assert.Equal(1, result.FileCount);

            var invalidJson = "not json"u8.ToArray();
            var invalid = Path.Combine(root, "invalid.zip");
            WriteZip(invalid, ("uobjecthook/camera_state.json", invalidJson));
            var invalidGame = ImportGame(Hashing.Sha256OfBytes(invalidJson), "uevr_object_hook",
                "uobjecthook/camera_state.json");
            Assert.Throws<InvalidDataException>(() => UevrTools.ImportProfileArchive(
                invalidGame, invalid, Path.Combine(root, "invalid")));

            var wrongPath = Path.Combine(root, "wrong-path.zip");
            WriteZip(wrongPath, ("arbitrary.json", valid));
            var wrongPathGame = ImportGame(Hashing.Sha256OfBytes(valid), "uevr_object_hook", "arbitrary.json");
            Assert.Throws<InvalidDataException>(() => UevrTools.ImportProfileArchive(
                wrongPathGame, wrongPath, Path.Combine(root, "wrong-path")));
        }
        finally { if (Directory.Exists(root)) Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void Real_catalog_files_parse_end_to_end()
    {
        // Guard: the actual repo data stays loadable (repo root found from test dir).
        var dir = AppContext.BaseDirectory;
        while (dir is not null && !File.Exists(Path.Combine(dir, "config", "unreal", "uevr-release.json")))
            dir = Path.GetDirectoryName(dir);
        Assert.NotNull(dir);
        var pin = UevrTools.LoadReleasePin(Path.Combine(dir!, "config", "unreal", "uevr-release.json"), "nightly");
        Assert.StartsWith("nightly-", pin.Tag);
        Assert.Equal(64, pin.Sha256.Length);
        var game = UevrTools.LoadGame(Path.Combine(dir!, "archive", "meccha-chameleon-2026-08-06", "config", "unreal", "meccha-chameleon.uevr.json"));
        Assert.Equal("nightly", game.Channel);
        Assert.Equal("PenguinHotel-Win64-Shipping", UevrTools.ShippingProcessName(game));
        Assert.False(game.InjectionStable);
        Assert.False(game.ProfileVerified);
        var rv = UevrTools.LoadGame(Path.Combine(dir!, "config", "unreal", "rv-there-yet.uevr.json"));
        Assert.Equal("nightly", rv.Channel);
        Assert.Equal("user_import_only", rv.ProfileSource?.Redistribution);
        Assert.Equal("not_present", rv.Capabilities?.MotionControls);
        Assert.Empty(rv.SupportedBuilds!);
        var escape = UevrTools.LoadGame(Path.Combine(dir!, "config", "unreal", "escape-the-backrooms.uevr.json"));
        Assert.Equal("Backrooms-Win64-Shipping", UevrTools.ShippingProcessName(escape));
        Assert.Equal("EscapeTheBackrooms/Binaries/Win64/Backrooms-Win64-Shipping.exe",
            escape.ShippingBinaryRelative);
        Assert.Equal(["-dx11"], escape.SteamLaunchArguments);
        Assert.Equal("user_import_only", escape.ProfileSource?.Redistribution);
        Assert.Contains(escape.ProfileSource!.Files, file => file.ContentType == "uevr_object_hook");
        Assert.False(escape.ProfileVerified);
        var safety = new SafetyGate().Evaluate(
            Path.Combine(dir!, "config", "unreal", "escape-the-backrooms.uevr.json"),
            Path.Combine(dir!, "config", "safety", "default-rules.json"));
        Assert.Equal(Verdict.Warn, safety.Verdict);
        Assert.Equal("private_modded_coop_requires_acknowledgement", safety.ReasonCode);
        var dotnet = UevrTools.LoadDotNetDesktopPin(
            Path.Combine(dir!, "config", "unreal", "dotnet-desktop-runtime.json"));
        Assert.Equal("8.0.29", dotnet.Version);
        Assert.Equal(128, dotnet.RuntimeSha512.Length);
        Assert.Equal(128, dotnet.DesktopSha512.Length);
    }
}
