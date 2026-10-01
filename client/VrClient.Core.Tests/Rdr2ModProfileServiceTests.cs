using System.Text.Json;
using VrClient.Core;
using VrClient.Core.ModProfiles;

namespace VrClient.Core.Tests;

public sealed class Rdr2ModProfileServiceTests
{
    [Fact]
    public void Create_clone_list_and_delete_keep_profile_data_outside_the_game_root()
    {
        using var fixture = ProfileFixture.Create();
        Assert.True(fixture.Service.CreateProfile("alpha").Success);
        Assert.Equal("profile_exists", fixture.Service.CreateProfile("alpha").Status);
        Assert.True(fixture.Service.CloneProfile("alpha", "beta").Success);
        Assert.Equal(["alpha", "beta"], fixture.Service.ListProfiles().Select(p => p.Id).ToArray());
        Assert.True(fixture.Service.DeleteProfile("alpha").Success);
        Assert.False(Directory.Exists(Path.Combine(fixture.GameRoot, "profiles")));
        Assert.Equal("invalid_profile_id", fixture.Service.CreateProfile("../escape").Status);
    }

    [Theory]
    [InlineData("../outside.txt", "invalid_target")]
    [InlineData("RDR2.exe", "protected_target")]
    [InlineData("PlayRDR2.exe", "protected_target")]
    [InlineData(".vrclient/loader.dll", "protected_target")]
    public void Staging_validation_refuses_unsafe_or_protected_targets(string target, string expected)
    {
        using var fixture = ProfileFixture.Create();
        fixture.CreateStaging("safe", target, [1]);
        Assert.Equal(expected, fixture.Service.ValidateStaging("safe", "123").Status);
    }

    [Fact]
    public void Validation_detects_build_dependency_conflict_and_case_collisions_before_apply()
    {
        using var fixture = ProfileFixture.Create();
        fixture.CreateStaging("safe", "mods/a.asi", [1], build: "999");
        Assert.Equal("build_mismatch", fixture.Service.ValidateStaging("safe", "123").Status);
        fixture.CreateStaging("safe", "mods/a.asi", [1], dependencies: ["mods/missing.asi"]);
        Assert.Equal("missing_dependency", fixture.Service.ValidateStaging("safe", "123").Status);
        fixture.CreateStaging("safe", "mods/a.asi", [1], conflicts: ["mods/a.asi"]);
        Assert.Equal("declared_conflict", fixture.Service.ValidateStaging("safe", "123").Status);
        fixture.CreateStaging("safe", "mods/a.asi", [1], additional: ("mods/A.asi", new byte[] { 2 }));
        Assert.Equal("target_collision", fixture.Service.ValidateStaging("safe", "123").Status);
    }

    [Fact]
    public void Apply_and_disable_restore_original_and_remove_introduced_files()
    {
        using var fixture = ProfileFixture.Create();
        var original = Path.Combine(fixture.GameRoot, "mods", "original.asi");
        Directory.CreateDirectory(Path.GetDirectoryName(original)!);
        File.WriteAllBytes(original, [9, 9]);
        fixture.CreateStaging("story", "mods/original.asi", [1, 2], additional: ("mods/new.asi", new byte[] { 3 }));
        Assert.Equal("applied", fixture.Service.ApplyProfile("story", "123").Status);
        Assert.Equal(new byte[] { 1, 2 }, File.ReadAllBytes(original));
        Assert.True(File.Exists(Path.Combine(fixture.GameRoot, "mods", "new.asi")));
        Assert.Equal("profile_active", fixture.Service.DeleteProfile("story").Status);
        Assert.Equal("rolled_back", fixture.Service.DisableProfile("story").Status);
        Assert.Equal(new byte[] { 9, 9 }, File.ReadAllBytes(original));
        Assert.False(File.Exists(Path.Combine(fixture.GameRoot, "mods", "new.asi")));
    }

    [Fact]
    public void Apply_distinguishes_the_requested_active_profile_from_a_different_one()
    {
        using var fixture = ProfileFixture.Create();
        fixture.CreateStaging("alpha", "mods/alpha.asi", [1]);
        fixture.CreateStaging("beta", "mods/beta.asi", [2]);

        Assert.Equal("applied", fixture.Service.ApplyProfile("alpha", "123").Status);
        Assert.Equal("profile_already_active", fixture.Service.ApplyProfile("alpha", "123").Status);
        Assert.Equal("different_profile_active", fixture.Service.ApplyProfile("beta", "123").Status);
        Assert.False(File.Exists(Path.Combine(fixture.GameRoot, "mods", "beta.asi")));
    }

    [Fact]
    public void Mutating_operations_refuse_before_transaction_artifacts_when_game_is_running()
    {
        using var fixture = ProfileFixture.Create(running: true);
        fixture.CreateStaging("story", "mods/file.asi", [1]);
        Assert.Equal("game_running", fixture.Service.ApplyProfile("story", "123").Status);
        Assert.False(Directory.Exists(Path.Combine(fixture.StateRoot, "transactions")));
        Assert.Equal("game_running", fixture.Service.RecoverIncompleteTransaction().Status);
    }

    [Fact]
    public void Changed_current_file_requires_manual_recovery_instead_of_overwrite()
    {
        using var fixture = ProfileFixture.Create();
        fixture.CreateStaging("story", "mods/file.asi", [1]);
        Assert.True(fixture.Service.ApplyProfile("story", "123").Success);
        File.WriteAllBytes(Path.Combine(fixture.GameRoot, "mods", "file.asi"), [7]);
        Assert.Equal("manual_recovery_required", fixture.Service.DisableProfile("story").Status);
        Assert.Equal(new byte[] { 7 }, File.ReadAllBytes(Path.Combine(fixture.GameRoot, "mods", "file.asi")));
    }

    [Fact]
    public void Stage_directory_builds_a_verified_external_manifest_without_touching_game_root()
    {
        using var fixture = ProfileFixture.Create();
        var source = Path.Combine(fixture.Root, "downloaded-mod");
        Directory.CreateDirectory(Path.Combine(source, "mods"));
        File.WriteAllBytes(Path.Combine(source, "mods", "camera.asi"), [1, 2, 3]);

        Assert.True(fixture.Service.CreateProfile("camera").Success);
        Assert.Equal("staged", fixture.Service.StageDirectory("camera", source, "123").Status);
        Assert.Equal("validated", fixture.Service.ValidateStaging("camera", "123").Status);
        Assert.False(File.Exists(Path.Combine(fixture.GameRoot, "mods", "camera.asi")));
        Assert.True(File.Exists(Path.Combine(fixture.StateRoot, "profiles", "camera", "staged", "mods", "camera.asi")));
        Assert.False(File.Exists(Path.Combine(fixture.StateRoot, "profiles", "camera", "staged", "staging-manifest.json")));
    }

    [Fact]
    public void Stage_directory_refuses_the_profile_state_root_as_a_source()
    {
        using var fixture = ProfileFixture.Create();
        Assert.True(fixture.Service.CreateProfile("camera").Success);
        Assert.Equal("source_inside_state_root", fixture.Service.StageDirectory(
            "camera", fixture.StateRoot, "123").Status);
    }

    [Fact]
    public void Export_and_import_round_trip_profile_contents_and_identity()
    {
        using var fixture = ProfileFixture.Create();
        var source = Path.Combine(fixture.Root, "downloaded-mod");
        Directory.CreateDirectory(source);
        File.WriteAllBytes(Path.Combine(source, "camera.asi"), [4, 5]);
        Assert.True(fixture.Service.CreateProfile("camera").Success);
        Assert.Equal("staged", fixture.Service.StageDirectory("camera", source, "123").Status);

        var archive = Path.Combine(fixture.Root, "camera.vrprofile.zip");
        Assert.Equal("exported", fixture.Service.ExportProfile("camera", archive).Status);

        var importedState = Path.Combine(fixture.Root, "imported-state");
        var imported = new Rdr2ModProfileService(fixture.GameRoot, importedState, new ProcessState(false));
        Assert.Equal("imported", imported.ImportProfile("camera-copy", archive).Status);
        Assert.Equal("validated", imported.ValidateStaging("camera-copy", "123").Status);
        Assert.Equal([4, 5], File.ReadAllBytes(Path.Combine(importedState, "profiles", "camera-copy", "staged", "camera.asi")));
    }

    [Fact]
    public void Corrupt_backup_requires_manual_recovery_and_preserves_current_targets()
    {
        using var fixture = ProfileFixture.Create();
        var original = Path.Combine(fixture.GameRoot, "mods", "original.asi");
        Directory.CreateDirectory(Path.GetDirectoryName(original)!);
        File.WriteAllBytes(original, [9, 9]);
        fixture.CreateStaging("story", "mods/original.asi", [1, 2], additional: ("mods/new.asi", new byte[] { 3 }));

        Assert.Equal("applied", fixture.Service.ApplyProfile("story", "123").Status);

        var transactionId = fixture.ReadActiveTransactionId();
        var backupPath = Path.Combine(fixture.StateRoot, "backups", transactionId, "mods", "original.asi");
        File.WriteAllBytes(backupPath, [8, 8]);

        Assert.Equal("manual_recovery_required", fixture.Service.DisableProfile("story").Status);
        Assert.Equal(new byte[] { 1, 2 }, File.ReadAllBytes(original));
        Assert.True(File.Exists(Path.Combine(fixture.GameRoot, "mods", "new.asi")));
    }

    [Fact]
    public void Disable_uses_recorded_journal_even_when_active_marker_is_missing()
    {
        using var fixture = ProfileFixture.Create();
        var original = Path.Combine(fixture.GameRoot, "mods", "original.asi");
        Directory.CreateDirectory(Path.GetDirectoryName(original)!);
        File.WriteAllBytes(original, [9, 9]);
        fixture.CreateStaging("story", "mods/original.asi", [1, 2], additional: ("mods/new.asi", new byte[] { 3 }));

        Assert.Equal("applied", fixture.Service.ApplyProfile("story", "123").Status);
        File.Delete(Path.Combine(fixture.StateRoot, "active-profile.json"));

        Assert.Equal("rolled_back", fixture.Service.DisableProfile("story").Status);
        Assert.Equal(new byte[] { 9, 9 }, File.ReadAllBytes(original));
        Assert.False(File.Exists(Path.Combine(fixture.GameRoot, "mods", "new.asi")));
    }

    [Fact]
    public void Recover_incomplete_transaction_keeps_journal_until_active_marker_cleanup_succeeds()
    {
        using var fixture = ProfileFixture.Create();
        var original = Path.Combine(fixture.GameRoot, "mods", "original.asi");
        Directory.CreateDirectory(Path.GetDirectoryName(original)!);
        File.WriteAllBytes(original, [9, 9]);
        fixture.CreateStaging("story", "mods/original.asi", [1, 2], additional: ("mods/new.asi", new byte[] { 3 }));

        Assert.Equal("applied", fixture.Service.ApplyProfile("story", "123").Status);

        var activePath = Path.Combine(fixture.StateRoot, "active-profile.json");
        var journalPath = Path.Combine(fixture.StateRoot, "transactions", "active.json");
        File.Delete(activePath);
        Directory.CreateDirectory(activePath);

        Assert.Equal("manual_recovery_required", fixture.Service.RecoverIncompleteTransaction().Status);
        Assert.Equal(new byte[] { 9, 9 }, File.ReadAllBytes(original));
        Assert.False(File.Exists(Path.Combine(fixture.GameRoot, "mods", "new.asi")));
        Assert.True(Directory.Exists(activePath));
        Assert.True(File.Exists(journalPath));

        Directory.Delete(activePath);

        Assert.Equal("rolled_back", fixture.Service.RecoverIncompleteTransaction().Status);
        Assert.False(File.Exists(journalPath));
        Assert.False(File.Exists(activePath));
        Assert.False(Directory.Exists(activePath));
    }

    private sealed class ProfileFixture : IDisposable
    {
        private ProfileFixture(string root, string gameRoot, string stateRoot, Rdr2ModProfileService service) => (Root, GameRoot, StateRoot, Service) = (root, gameRoot, stateRoot, service);
        public string Root { get; }
        public string GameRoot { get; }
        public string StateRoot { get; }
        public Rdr2ModProfileService Service { get; }

        public static ProfileFixture Create(bool running = false)
        {
            var root = Path.Combine(Path.GetTempPath(), $"vrclient-rdr2-profile-{Guid.NewGuid():N}");
            var game = Path.Combine(root, "game");
            var state = Path.Combine(root, "state");
            Directory.CreateDirectory(game);
            return new(root, game, state, new Rdr2ModProfileService(game, state, new ProcessState(running)));
        }

        public void CreateStaging(string id, string target, byte[] bytes, string build = "123", IReadOnlyList<string>? dependencies = null, IReadOnlyList<string>? conflicts = null, (string Target, byte[] Bytes)? additional = null)
        {
            Assert.True(Service.CreateProfile(id).Success || Directory.Exists(Path.Combine(StateRoot, "profiles", id)));
            var root = Path.Combine(StateRoot, "profiles", id);
            WriteStaged(root, target, bytes);
            var files = new List<object> { new { target, sha256 = Hashing.Sha256OfBytes(bytes) } };
            if (additional is { } second)
            {
                WriteStaged(root, second.Target, second.Bytes);
                files.Add(new { target = second.Target, sha256 = Hashing.Sha256OfBytes(second.Bytes) });
            }

            File.WriteAllText(Path.Combine(root, "staging-manifest.json"), JsonSerializer.Serialize(new { steamBuildId = build, files, dependencies = dependencies ?? [], conflicts = conflicts ?? [] }));
        }

        public string ReadActiveTransactionId()
        {
            using var document = JsonDocument.Parse(File.ReadAllText(Path.Combine(StateRoot, "active-profile.json")));
            return document.RootElement.GetProperty("transactionId").GetString()!;
        }

        private static void WriteStaged(string root, string target, byte[] bytes)
        {
            var path = Path.Combine(root, "staged", target.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, bytes);
        }

        public void Dispose() => Directory.Delete(Root, recursive: true);
    }

    private sealed class ProcessState(bool running) : IRdr2ProcessState
    {
        public bool IsGameRunning() => running;
    }
}
