namespace VrClient.Core.ModProfiles;

using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using System.Text.RegularExpressions;

public interface IRdr2ProcessState
{
    bool IsGameRunning();
}

public sealed class Rdr2ProcessState : IRdr2ProcessState
{
    public bool IsGameRunning() => Process.GetProcessesByName("RDR2").Length != 0 || Process.GetProcessesByName("PlayRDR2").Length != 0;
}

public sealed record Rdr2ProfileResult(bool Success, string Status, string? Detail = null)
{
    public static Rdr2ProfileResult Ok(string status = "ok") => new(true, status);
    public static Rdr2ProfileResult Refuse(string status, string? detail = null) => new(false, status, detail);
}

public sealed record Rdr2ProfileInfo(string Id, bool Active);

/// <summary>
/// RDR2-owned, title-scoped profile storage. Staged files and recovery data stay
/// outside the game root until a validated, journaled apply operation occurs.
/// </summary>
public sealed class Rdr2ModProfileService
{
    private static readonly Regex ProfileId = new("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$", RegexOptions.Compiled);
    private readonly string _gameRoot;
    private readonly string _stateRoot;
    private readonly IRdr2ProcessState _processState;

    public Rdr2ModProfileService(string gameRoot, string stateRoot, IRdr2ProcessState? processState = null)
    {
        _gameRoot = Path.GetFullPath(gameRoot);
        _stateRoot = Path.GetFullPath(stateRoot);
        _processState = processState ?? new Rdr2ProcessState();
    }

    public Rdr2ProfileResult CreateProfile(string profileId)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        var root = ProfileRoot(profileId);
        if (Directory.Exists(root)) return Rdr2ProfileResult.Refuse("profile_exists");
        Directory.CreateDirectory(Path.Combine(root, "staged"));
        AtomicWrite(Path.Combine(root, "profile.json"), JsonSerializer.Serialize(new ProfileMetadata(profileId), JsonOptions));
        return Rdr2ProfileResult.Ok("created");
    }

    public Rdr2ProfileResult CloneProfile(string sourceId, string targetId)
    {
        if (!IsValidProfileId(sourceId) || !IsValidProfileId(targetId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        var source = ProfileRoot(sourceId);
        var target = ProfileRoot(targetId);
        if (!Directory.Exists(source)) return Rdr2ProfileResult.Refuse("profile_not_found");
        if (Directory.Exists(target)) return Rdr2ProfileResult.Refuse("profile_exists");
        if (HasReparsePoint(source)) return Rdr2ProfileResult.Refuse("reparse_point_refused");
        CopyTree(source, target);
        AtomicWrite(Path.Combine(target, "profile.json"), JsonSerializer.Serialize(new ProfileMetadata(targetId), JsonOptions));
        return Rdr2ProfileResult.Ok("cloned");
    }

    /// <summary>
    /// Stages a mod directory into an external named profile and writes a
    /// build-pinned manifest. The game directory is never modified by this
    /// operation; only ApplyProfile performs a tracked game-root transaction.
    /// </summary>
    public Rdr2ProfileResult StageDirectory(
        string profileId,
        string sourceDirectory,
        string steamBuildId)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        if (!IsValidBuildId(steamBuildId)) return Rdr2ProfileResult.Refuse("invalid_build_id");

        var profileRoot = ProfileRoot(profileId);
        if (!Directory.Exists(profileRoot)) return Rdr2ProfileResult.Refuse("profile_not_found");
        if (ReadActive() is not null) return Rdr2ProfileResult.Refuse("profile_active");
        if (ReadJournal() is not null) return Rdr2ProfileResult.Refuse("transaction_incomplete");
        if (!TryGetSafeExternalDirectory(sourceDirectory, out var sourceRoot, out var refusal))
            return Rdr2ProfileResult.Refuse(refusal);

        var temporary = Path.Combine(profileRoot, ".staged-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(temporary);
            var files = new List<StagingFile>();
            var targets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var sourceFile in Directory.EnumerateFiles(sourceRoot, "*", SearchOption.AllDirectories))
            {
                if (HasReparsePoint(sourceFile)) return Rdr2ProfileResult.Refuse("reparse_point_refused");
                var target = Path.GetRelativePath(sourceRoot, sourceFile).Replace(Path.DirectorySeparatorChar, '/');
                if (!TrySafeRelative(target, out var relative, out refusal)) return Rdr2ProfileResult.Refuse(refusal);
                if (!targets.Add(relative)) return Rdr2ProfileResult.Refuse("target_collision");

                var destination = Path.GetFullPath(Path.Combine(temporary, relative));
                if (!IsUnder(destination, temporary)) return Rdr2ProfileResult.Refuse("invalid_target");
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(sourceFile, destination, overwrite: false);
                files.Add(new StagingFile(relative, Hashing.Sha256OfFile(destination)));
            }

            if (files.Count == 0) return Rdr2ProfileResult.Refuse("source_empty");

            var manifest = new StagingManifest(steamBuildId, files, [], []);
            var oldStaged = Path.Combine(profileRoot, "staged");
            var oldManifest = Path.Combine(profileRoot, "staging-manifest.json");
            var displaced = Path.Combine(profileRoot, ".staged-old-" + Guid.NewGuid().ToString("N"));
            var previousManifest = File.Exists(oldManifest) ? File.ReadAllText(oldManifest) : null;
            if (Directory.Exists(oldStaged)) Directory.Move(oldStaged, displaced);
            try
            {
                Directory.Move(temporary, oldStaged);
                AtomicWrite(oldManifest, JsonSerializer.Serialize(manifest, JsonOptions));
            }
            catch
            {
                if (Directory.Exists(oldStaged)) Directory.Move(oldStaged, temporary);
                if (Directory.Exists(displaced)) Directory.Move(displaced, oldStaged);
                try
                {
                    if (previousManifest is null)
                    {
                        if (File.Exists(oldManifest)) File.Delete(oldManifest);
                    }
                    else
                    {
                        AtomicWrite(oldManifest, previousManifest);
                    }
                }
                catch (IOException) { /* preserve the original staging failure */ }
                catch (UnauthorizedAccessException) { /* preserve the original staging failure */ }
                throw;
            }
            if (Directory.Exists(displaced)) Directory.Delete(displaced, recursive: true);
            return Rdr2ProfileResult.Ok("staged");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return Rdr2ProfileResult.Refuse("stage_failed");
        }
        finally
        {
            if (Directory.Exists(temporary)) Directory.Delete(temporary, recursive: true);
        }
    }

    /// <summary>Exports an external profile as a portable ZIP archive.</summary>
    public Rdr2ProfileResult ExportProfile(string profileId, string destinationArchive)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        var profileRoot = ProfileRoot(profileId);
        if (!Directory.Exists(profileRoot)) return Rdr2ProfileResult.Refuse("profile_not_found");
        if (HasReparsePoint(profileRoot)) return Rdr2ProfileResult.Refuse("reparse_point_refused");
        if (string.IsNullOrWhiteSpace(destinationArchive)) return Rdr2ProfileResult.Refuse("missing_destination");

        var destination = Path.GetFullPath(destinationArchive);
        if (IsUnder(destination, profileRoot)) return Rdr2ProfileResult.Refuse("destination_inside_profile");
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            var temporary = destination + ".tmp-" + Guid.NewGuid().ToString("N");
            ZipFile.CreateFromDirectory(profileRoot, temporary, CompressionLevel.Fastest, includeBaseDirectory: false);
            File.Move(temporary, destination, overwrite: true);
            return Rdr2ProfileResult.Ok("exported");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return Rdr2ProfileResult.Refuse("export_failed");
        }
    }

    /// <summary>
    /// Imports a previously exported profile into external storage. ZIP entry
    /// names are validated before extraction to prevent traversal or protected
    /// game targets from entering the profile.
    /// </summary>
    public Rdr2ProfileResult ImportProfile(string profileId, string sourceArchive)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        if (string.IsNullOrWhiteSpace(sourceArchive) || !File.Exists(sourceArchive)) return Rdr2ProfileResult.Refuse("archive_not_found");
        var target = ProfileRoot(profileId);
        if (Directory.Exists(target)) return Rdr2ProfileResult.Refuse("profile_exists");
        if (HasReparsePoint(sourceArchive)) return Rdr2ProfileResult.Refuse("reparse_point_refused");

        var temporary = Path.Combine(ProfilesRoot, ".import-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(temporary);
            using var archive = ZipFile.OpenRead(Path.GetFullPath(sourceArchive));
            foreach (var entry in archive.Entries)
            {
                var name = entry.FullName.Replace('\\', '/').Trim('/');
                if (string.IsNullOrWhiteSpace(name)) continue;
                if (!TrySafeArchiveEntry(name, out var relative)) return Rdr2ProfileResult.Refuse("archive_entry_invalid");
                var destination = Path.GetFullPath(Path.Combine(temporary, relative));
                if (!IsUnder(destination, temporary)) return Rdr2ProfileResult.Refuse("archive_entry_invalid");
                if (name.EndsWith("/", StringComparison.Ordinal))
                {
                    Directory.CreateDirectory(destination);
                    continue;
                }
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                using var input = entry.Open();
                using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                input.CopyTo(output);
            }

            var importedManifest = Path.Combine(temporary, "staging-manifest.json");
            if (!File.Exists(Path.Combine(temporary, "profile.json")) || !File.Exists(importedManifest))
                return Rdr2ProfileResult.Refuse("archive_manifest_missing");
            var metadata = JsonSerializer.Deserialize<ProfileMetadata>(File.ReadAllText(Path.Combine(temporary, "profile.json")), JsonOptions);
            if (metadata is null || !IsValidProfileId(metadata.Id))
                return Rdr2ProfileResult.Refuse("archive_profile_mismatch");
            // Import may intentionally rename a profile, just like CloneProfile;
            // the archive's source ID is not an authority over the destination.
            AtomicWrite(Path.Combine(temporary, "profile.json"), JsonSerializer.Serialize(new ProfileMetadata(profileId), JsonOptions));
            var manifest = JsonSerializer.Deserialize<StagingManifest>(File.ReadAllText(importedManifest), JsonOptions);
            if (manifest is null || !IsValidBuildId(manifest.SteamBuildId)) return Rdr2ProfileResult.Refuse("staging_manifest_invalid");
            if (!VerifyStagedManifest(temporary, manifest)) return Rdr2ProfileResult.Refuse("staged_hash_mismatch");

            Directory.CreateDirectory(ProfilesRoot);
            Directory.Move(temporary, target);
            return Rdr2ProfileResult.Ok("imported");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
        {
            return Rdr2ProfileResult.Refuse("import_failed");
        }
        finally
        {
            if (Directory.Exists(temporary)) Directory.Delete(temporary, recursive: true);
        }
    }

    public IReadOnlyList<Rdr2ProfileInfo> ListProfiles()
    {
        var active = ReadActive()?.ProfileId;
        var profiles = ProfilesRoot;
        if (!Directory.Exists(profiles)) return [];
        return Directory.EnumerateDirectories(profiles)
            .Select(Path.GetFileName)
            .Where(id => id is not null && IsValidProfileId(id))
            .Select(id => new Rdr2ProfileInfo(id!, string.Equals(id, active, StringComparison.Ordinal)))
            .OrderBy(p => p.Id, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public Rdr2ProfileResult DeleteProfile(string profileId)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        if (string.Equals(ReadActive()?.ProfileId, profileId, StringComparison.OrdinalIgnoreCase)) return Rdr2ProfileResult.Refuse("profile_active");
        if (ReadJournal()?.ProfileId is { } journalId && string.Equals(journalId, profileId, StringComparison.OrdinalIgnoreCase)) return Rdr2ProfileResult.Refuse("transaction_incomplete");
        var root = ProfileRoot(profileId);
        if (!Directory.Exists(root)) return Rdr2ProfileResult.Refuse("profile_not_found");
        if (HasReparsePoint(root)) return Rdr2ProfileResult.Refuse("reparse_point_refused");
        Directory.Delete(root, recursive: true);
        return Rdr2ProfileResult.Ok("deleted");
    }

    public Rdr2ProfileResult ValidateStaging(string profileId, string steamBuildId)
    {
        if (!IsValidProfileId(profileId)) return Rdr2ProfileResult.Refuse("invalid_profile_id");
        var root = ProfileRoot(profileId);
        if (!Directory.Exists(root)) return Rdr2ProfileResult.Refuse("profile_not_found");
        if (HasReparsePoint(root)) return Rdr2ProfileResult.Refuse("reparse_point_refused");
        var manifestPath = Path.Combine(root, "staging-manifest.json");
        if (!File.Exists(manifestPath)) return Rdr2ProfileResult.Refuse("staging_manifest_missing");
        StagingManifest? manifest;
        try { manifest = JsonSerializer.Deserialize<StagingManifest>(File.ReadAllText(manifestPath), JsonOptions); }
        catch (JsonException) { return Rdr2ProfileResult.Refuse("staging_manifest_invalid"); }
        if (manifest is null || !string.Equals(manifest.SteamBuildId, steamBuildId, StringComparison.Ordinal)) return Rdr2ProfileResult.Refuse("build_mismatch");

        var targets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in manifest.Files ?? [])
        {
            if (!TrySafeRelative(entry.Target, out var relative, out var refusal)) return Rdr2ProfileResult.Refuse(refusal);
            if (!targets.Add(relative)) return Rdr2ProfileResult.Refuse("target_collision");
        }

        foreach (var entry in manifest.Files ?? [])
        {
            TrySafeRelative(entry.Target, out var relative, out _);
            var staged = Path.GetFullPath(Path.Combine(root, "staged", relative));
            if (!IsUnder(staged, Path.Combine(root, "staged")) || !File.Exists(staged) || HasReparsePoint(staged)) return Rdr2ProfileResult.Refuse("staged_file_invalid");
            if (!string.Equals(Hashing.Sha256OfFile(staged), entry.Sha256, StringComparison.OrdinalIgnoreCase)) return Rdr2ProfileResult.Refuse("staged_hash_mismatch");
        }

        foreach (var dependency in manifest.Dependencies ?? [])
            if (!targets.Contains(Normalize(dependency))) return Rdr2ProfileResult.Refuse("missing_dependency");

        foreach (var conflict in manifest.Conflicts ?? [])
            if (targets.Contains(Normalize(conflict))) return Rdr2ProfileResult.Refuse("declared_conflict");

        return Rdr2ProfileResult.Ok("validated");
    }

    public Rdr2ProfileResult ApplyProfile(string profileId, string steamBuildId)
    {
        var guard = EnsureGameNotRunning();
        if (guard is not null) return guard;
        var active = ReadActive();
        if (active is not null)
            return string.Equals(active.ProfileId, profileId, StringComparison.OrdinalIgnoreCase)
                ? Rdr2ProfileResult.Refuse("profile_already_active")
                : Rdr2ProfileResult.Refuse("different_profile_active", active.ProfileId);
        if (ReadJournal() is not null) return Rdr2ProfileResult.Refuse("transaction_incomplete");

        var validation = ValidateStaging(profileId, steamBuildId);
        if (!validation.Success) return validation;

        var manifest = ReadManifest(profileId)!;
        var transactionId = Guid.NewGuid().ToString("N");
        var backupRoot = Path.Combine(BackupsRoot, transactionId);
        var journal = new TransactionJournal(transactionId, profileId, backupRoot, "prepared", []);
        var entries = new List<TransactionEntry>();

        try
        {
            Directory.CreateDirectory(backupRoot);

            foreach (var item in manifest.Files!)
            {
                TrySafeRelative(item.Target, out var relative, out _);
                var staged = Path.Combine(ProfileRoot(profileId), "staged", relative);
                if (!File.Exists(staged) || !string.Equals(Hashing.Sha256OfFile(staged), item.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    return Rdr2ProfileResult.Refuse("staged_hash_mismatch");
                }

                var target = GamePath(relative);
                var existed = File.Exists(target);
                string? originalHash = null;
                string? backupRelative = null;
                if (existed)
                {
                    originalHash = Hashing.Sha256OfFile(target);
                    backupRelative = relative;
                    var backup = Path.Combine(backupRoot, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                    File.Copy(target, backup, overwrite: false);
                    if (!string.Equals(Hashing.Sha256OfFile(backup), originalHash, StringComparison.OrdinalIgnoreCase))
                    {
                        return Rdr2ProfileResult.Refuse("backup_verification_failed");
                    }
                }

                entries.Add(new TransactionEntry(relative, existed, originalHash, item.Sha256, backupRelative, "prepared"));
            }

            journal = journal with { Entries = entries.ToArray() };
            WriteJournalVerified(journal);

            for (var i = 0; i < entries.Count; i++)
            {
                var entry = entries[i];
                var staged = Path.Combine(ProfileRoot(profileId), "staged", entry.Target);
                if (!string.Equals(Hashing.Sha256OfFile(staged), entry.StagedSha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new IOException("staged hash changed during apply");
                }

                var target = GamePath(entry.Target);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                var temp = target + ".vrclient-" + transactionId + ".tmp";
                try
                {
                    File.Copy(staged, temp, overwrite: true);
                    if (!string.Equals(Hashing.Sha256OfFile(temp), entry.StagedSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        throw new IOException("staged hash changed during apply");
                    }

                    File.Move(temp, target, overwrite: true);
                    if (!string.Equals(Hashing.Sha256OfFile(target), entry.StagedSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        throw new IOException("target hash mismatch after apply");
                    }
                }
                finally
                {
                    if (File.Exists(temp))
                    {
                        File.Delete(temp);
                    }
                }

                entries[i] = entry with { State = "applied" };
                journal = journal with { State = "applied", Entries = entries.ToArray() };
                WriteJournalVerified(journal);
            }

            WriteActiveVerified(new ActiveProfile(profileId, transactionId));
            return Rdr2ProfileResult.Ok("applied");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            var recovery = RestoreJournal(ReadJournal() ?? journal);
            return recovery.Success ? Rdr2ProfileResult.Refuse("apply_failed_rolled_back") : recovery;
        }
    }

    public Rdr2ProfileResult DisableProfile(string profileId)
    {
        var guard = EnsureGameNotRunning();
        if (guard is not null) return guard;

        var journal = ReadJournal();
        if (journal is not null)
        {
            return string.Equals(journal.ProfileId, profileId, StringComparison.OrdinalIgnoreCase)
                ? RestoreJournal(journal)
                : Rdr2ProfileResult.Refuse("profile_not_active");
        }

        var active = ReadActive();
        return active is not null && string.Equals(active.ProfileId, profileId, StringComparison.OrdinalIgnoreCase)
            ? Rdr2ProfileResult.Refuse("transaction_missing")
            : Rdr2ProfileResult.Refuse("profile_not_active");
    }

    public Rdr2ProfileResult RecoverIncompleteTransaction()
    {
        var guard = EnsureGameNotRunning();
        if (guard is not null) return guard;
        var journal = ReadJournal();
        return journal is null ? Rdr2ProfileResult.Ok("no_transaction") : RestoreJournal(journal);
    }

    private Rdr2ProfileResult RestoreJournal(TransactionJournal? journal)
    {
        if (journal is null) return Rdr2ProfileResult.Refuse("transaction_missing");

        try
        {
            foreach (var entry in journal.Entries)
            {
                var target = GamePath(entry.Target);
                if (entry.Existed)
                {
                    var backup = Path.Combine(journal.BackupRoot, entry.BackupRelative!);
                    if (!File.Exists(backup)) return Rdr2ProfileResult.Refuse("manual_recovery_required");
                    if (!string.Equals(Hashing.Sha256OfFile(backup), entry.OriginalSha256, StringComparison.OrdinalIgnoreCase)) return Rdr2ProfileResult.Refuse("manual_recovery_required");
                    if (!File.Exists(target)) return Rdr2ProfileResult.Refuse("manual_recovery_required");

                    var currentHash = Hashing.Sha256OfFile(target);
                    if (!string.Equals(currentHash, entry.StagedSha256, StringComparison.OrdinalIgnoreCase) &&
                        !string.Equals(currentHash, entry.OriginalSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        return Rdr2ProfileResult.Refuse("manual_recovery_required");
                    }
                }
                else if (File.Exists(target))
                {
                    var currentHash = Hashing.Sha256OfFile(target);
                    if (!string.Equals(currentHash, entry.StagedSha256, StringComparison.OrdinalIgnoreCase))
                    {
                        return Rdr2ProfileResult.Refuse("manual_recovery_required");
                    }
                }
            }

            foreach (var entry in journal.Entries)
            {
                var target = GamePath(entry.Target);
                if (entry.Existed)
                {
                    var backup = Path.Combine(journal.BackupRoot, entry.BackupRelative!);
                    var temp = target + ".vrclient-restore-" + journal.TransactionId + ".tmp";
                    try
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                        File.Copy(backup, temp, overwrite: true);
                        if (!string.Equals(Hashing.Sha256OfFile(temp), entry.OriginalSha256, StringComparison.OrdinalIgnoreCase))
                        {
                            return Rdr2ProfileResult.Refuse("manual_recovery_required");
                        }

                        File.Move(temp, target, overwrite: true);
                        if (!string.Equals(Hashing.Sha256OfFile(target), entry.OriginalSha256, StringComparison.OrdinalIgnoreCase))
                        {
                            return Rdr2ProfileResult.Refuse("manual_recovery_required");
                        }
                    }
                    finally
                    {
                        if (File.Exists(temp))
                        {
                            File.Delete(temp);
                        }
                    }
                }
                else if (File.Exists(target))
                {
                    File.Delete(target);
                    if (File.Exists(target))
                    {
                        return Rdr2ProfileResult.Refuse("manual_recovery_required");
                    }
                }
            }

            if (!TryDeleteTransactionMarker(ActivePath)) return Rdr2ProfileResult.Refuse("manual_recovery_required");
            if (!TryDeleteTransactionMarker(JournalPath)) return Rdr2ProfileResult.Refuse("manual_recovery_required");
            return Rdr2ProfileResult.Ok("rolled_back");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return Rdr2ProfileResult.Refuse("manual_recovery_required");
        }
    }

    private Rdr2ProfileResult? EnsureGameNotRunning() => _processState.IsGameRunning() ? Rdr2ProfileResult.Refuse("game_running") : null;
    private StagingManifest? ReadManifest(string profileId) => JsonSerializer.Deserialize<StagingManifest>(File.ReadAllText(Path.Combine(ProfileRoot(profileId), "staging-manifest.json")), JsonOptions);
    private ActiveProfile? ReadActive() => ReadJson<ActiveProfile>(ActivePath);
    private TransactionJournal? ReadJournal() => ReadJson<TransactionJournal>(JournalPath);
    private T? ReadJson<T>(string path) => File.Exists(path) ? JsonSerializer.Deserialize<T>(File.ReadAllText(path), JsonOptions) : default;

    private void WriteJournalVerified(TransactionJournal journal)
    {
        Directory.CreateDirectory(TransactionsRoot);
        WriteJsonVerified(JournalPath, journal);
    }

    private void WriteActiveVerified(ActiveProfile active)
    {
        WriteJsonVerified(ActivePath, active);
    }

    private static void WriteJsonVerified<T>(string path, T value)
    {
        var json = JsonSerializer.Serialize(value, JsonOptions);
        AtomicWrite(path, json);
        if (!File.Exists(path) || !string.Equals(File.ReadAllText(path), json, StringComparison.Ordinal))
        {
            throw new IOException("transaction marker verification failed");
        }
    }

    private string ProfilesRoot => Path.Combine(_stateRoot, "profiles");
    private string BackupsRoot => Path.Combine(_stateRoot, "backups");
    private string TransactionsRoot => Path.Combine(_stateRoot, "transactions");
    private string JournalPath => Path.Combine(TransactionsRoot, "active.json");
    private string ActivePath => Path.Combine(_stateRoot, "active-profile.json");
    private string ProfileRoot(string id) => Path.Combine(ProfilesRoot, id);

    private string GamePath(string relative)
    {
        var path = Path.GetFullPath(Path.Combine(_gameRoot, relative));
        if (!IsUnder(path, _gameRoot) || HasReparsePoint(path)) throw new InvalidOperationException("unsafe target");
        return path;
    }

    private static bool IsValidProfileId(string id) => !string.IsNullOrWhiteSpace(id) && ProfileId.IsMatch(id);
    private static bool IsValidBuildId(string? buildId) => !string.IsNullOrWhiteSpace(buildId) && Regex.IsMatch(buildId, "^[0-9]+$");
    private static string Normalize(string path) => path.Replace('\\', '/').Trim('/');

    private bool TryGetSafeExternalDirectory(string? input, out string directory, out string refusal)
    {
        directory = string.Empty;
        refusal = "source_not_found";
        if (string.IsNullOrWhiteSpace(input)) return false;
        var candidate = Path.GetFullPath(input);
        if (!Directory.Exists(candidate)) return false;
        if (HasReparsePoint(candidate))
        {
            refusal = "reparse_point_refused";
            return false;
        }
        if (IsUnder(candidate, _gameRoot) || string.Equals(candidate, _gameRoot, StringComparison.OrdinalIgnoreCase))
        {
            refusal = "source_inside_game_root";
            return false;
        }
        if (IsUnder(candidate, _stateRoot) || string.Equals(candidate, _stateRoot, StringComparison.OrdinalIgnoreCase))
        {
            refusal = "source_inside_state_root";
            return false;
        }
        directory = candidate;
        return true;
    }

    private static bool TrySafeArchiveEntry(string input, out string relative)
    {
        relative = Normalize(input);
        var isStagedDirectory = string.Equals(relative, "staged", StringComparison.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) ||
            relative.Split('/').Any(part => part is "" or "." or "..") ||
            (!isStagedDirectory &&
             !string.Equals(relative, "profile.json", StringComparison.OrdinalIgnoreCase) &&
             !string.Equals(relative, "staging-manifest.json", StringComparison.OrdinalIgnoreCase) &&
             !relative.StartsWith("staged/", StringComparison.OrdinalIgnoreCase)))
        {
            return false;
        }
        return true;
    }

    private static bool VerifyStagedManifest(string temporaryRoot, StagingManifest manifest)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in manifest.Files ?? [])
        {
            if (!TrySafeRelative(entry.Target, out var relative, out _)) return false;
            if (!seen.Add(relative)) return false;
            var staged = Path.GetFullPath(Path.Combine(temporaryRoot, "staged", relative));
            if (!IsUnder(staged, Path.Combine(temporaryRoot, "staged")) || !File.Exists(staged) || HasReparsePoint(staged)) return false;
            if (!string.Equals(Hashing.Sha256OfFile(staged), entry.Sha256, StringComparison.OrdinalIgnoreCase)) return false;
        }
        return seen.Count > 0;
    }

    private static bool TrySafeRelative(string? input, out string relative, out string refusal)
    {
        relative = Normalize(input ?? string.Empty);
        refusal = "invalid_target";
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Split('/').Any(x => x is "" or "." or "..")) return false;
        if (relative.StartsWith(".vrclient/", StringComparison.OrdinalIgnoreCase) || string.Equals(relative, ".vrclient", StringComparison.OrdinalIgnoreCase) || string.Equals(relative, "RDR2.exe", StringComparison.OrdinalIgnoreCase) || string.Equals(relative, "PlayRDR2.exe", StringComparison.OrdinalIgnoreCase))
        {
            refusal = "protected_target";
            return false;
        }

        return true;
    }

    private static bool IsUnder(string candidate, string root) => Path.GetFullPath(candidate).StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    private static bool HasReparsePoint(string path)
    {
        var current = Path.GetFullPath(path);
        while (!string.IsNullOrWhiteSpace(current))
        {
            FileSystemInfo info = File.Exists(current) ? new FileInfo(current) : new DirectoryInfo(current);
            if (info.Exists && (info.Attributes & FileAttributes.ReparsePoint) != 0) return true;
            var parent = Path.GetDirectoryName(current);
            if (string.Equals(parent, current, StringComparison.OrdinalIgnoreCase)) break;
            current = parent;
        }

        return false;
    }

    private static bool TryDeleteTransactionMarker(string path)
    {
        if (Directory.Exists(path)) return false;
        if (!File.Exists(path)) return true;
        File.Delete(path);
        return !File.Exists(path);
    }

    private static void AtomicWrite(string path, string content)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temp, content);
        File.Move(temp, path, overwrite: true);
    }

    private static void CopyTree(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (HasReparsePoint(file)) throw new IOException("reparse point refused");
            var dest = Path.Combine(target, Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            File.Copy(file, dest);
        }
    }

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true };

    private sealed record ProfileMetadata(string Id);
    private sealed record ActiveProfile(string ProfileId, string TransactionId);
    private sealed record StagingManifest(string SteamBuildId, IReadOnlyList<StagingFile>? Files, IReadOnlyList<string>? Dependencies, IReadOnlyList<string>? Conflicts);
    private sealed record StagingFile(string Target, string Sha256);
    private sealed record TransactionJournal(string TransactionId, string ProfileId, string BackupRoot, string State, IReadOnlyList<TransactionEntry> Entries);
    private sealed record TransactionEntry(string Target, bool Existed, string? OriginalSha256, string StagedSha256, string? BackupRelative, string State);
}
