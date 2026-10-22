using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using VrClient.Core.App;
using VrClient.Core.Discovery;
using VrClient.Core.Config;
using VrClient.Core.Modpack;
using VrClient.Core.Redengine;
using VrClient.Core.Unreal;
using VrClient.Core.Launch;

namespace VrClient.Core.Compatibility;

public sealed record CompatibilityDescriptor(string Executable, string? ExpectedHash = null,
    string? ExpectedVersion = null, bool StoreLaunchRequired = false);
public sealed record GameCompatibilityState(bool CanTryVr, bool BuildChanged, bool ReviewedBuild,
    bool NeedsPackageUpdate, string Message, string PackageKey, GameBuildIdentity? Identity,
    InstallationProfile? Profile, bool ModsParked = false);
public enum StartupObservation { Running, ExitedWithError, ExitedNormally, NotObserved }

/// Shared catalogue policy. An observed process is startup evidence, never proof of
/// stereo, tracking or game compatibility. Exact native build guards remain mandatory.
public sealed class GameCompatibilityService(string repoRoot, GameInstallationStore? installations = null,
    Func<string, string?>? versionReader = null)
{
    public GameInstallationStore Installations { get; } = installations ?? new();
    public const string SaveWarning = "Older game versions may not load newer saves. Back up saves in the game or store before switching. AVRcade does not downgrade games or rewrite saves. Mod and VR settings in each game folder remain separate.";

    public CompatibilityDescriptor Describe(GameView game)
    {
        JsonElement Read(string relative) { using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(repoRoot, relative))); return doc.RootElement.Clone(); }
        if (game.Engine == GameEngine.Redengine)
        {
            var data = Read("config/redengine/native/" + game.Slug + ".json").GetProperty("game");
            return new(Text(data, "shipping_binary")!, Read("config/redengine/native/" + game.Slug + ".json").TryGetProperty("game_compatibility", out _) ? null : Text(data, "executable_sha256_observed"));
        }
        if (game.Engine == GameEngine.LegacyD3D9)
        {
            var data = Read("config/legacy/" + game.Slug + ".json").GetProperty("game");
            return new(Text(data, "executable_name")!, Text(data, "executable_sha256_observed"));
        }
        if (game.Engine is GameEngine.Fallout3 or GameEngine.FalloutNewVegas)
        {
            var directory = game.Engine == GameEngine.Fallout3 ? "fallout_3" : "fallout_new_vegas";
            var data = Read("adapters/" + directory + "/adapter.json");
            string? hash = null;
            if (data.TryGetProperty("native", out var native) &&
                native.TryGetProperty("reviewed_experimental_hook_profiles", out var profiles))
                hash = profiles.EnumerateArray().Select(p => Text(p, "executable_sha256")).FirstOrDefault();
            return new(game.Engine == GameEngine.Fallout3 ? "Fallout3.exe" : "FalloutNV.exe", hash,
                game.Engine == GameEngine.Fallout3 ? "1.7.0.3" : "1.4.0.525");
        }
        var unreal = Path.Combine(repoRoot, "config", "unreal", game.Slug + ".uevr.json");
        if (File.Exists(unreal)) return new(UevrTools.LoadGame(unreal).ShippingBinaryRelative);
        foreach (var folder in new[] { "games", "catalogue" })
        {
            var path = Path.Combine(repoRoot, "config", folder, game.Slug + ".json");
            if (!File.Exists(path)) continue;
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            if (doc.RootElement.TryGetProperty("executable_names", out var names) && names.GetArrayLength() > 0)
                return new(names[0].GetString()!);
        }
        throw new InvalidDataException("This catalogue entry has no executable identity: " + game.Slug);
    }

    private GameVersionCompatibility? ReadVersionPolicy(GameView game, string? version)
    {
        foreach (var relative in new[] { $"config/redengine/native/{game.Slug}.json", $"config/games/{game.Slug}.json", $"config/unreal/{game.Slug}.uevr.json" })
        {
            var path = Path.Combine(repoRoot, relative);
            if (!File.Exists(path)) continue;
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            if (GameVersionCompatibility.Read(doc.RootElement, version) is { } policy) return policy;
        }
        return null;
    }

    private static string? Text(JsonElement item, string property) => item.TryGetProperty(property, out var value) &&
        value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    public GameBuildIdentity ReadIdentity(GameView game)
    {
        var descriptor = Describe(game);
        var exe = ModUpdateTransaction.Resolve(game.InstallDir ?? throw new IOException("Select a game installation."), descriptor.Executable);
        if (!File.Exists(exe)) throw new FileNotFoundException("The selected game executable is missing.", exe);
        var before = new FileInfo(exe);
        var beforeLength = before.Length; var beforeWrite = before.LastWriteTimeUtc;
        var version = versionReader is null ? FileVersionInfo.GetVersionInfo(exe).ProductVersion : versionReader(exe);
        if (descriptor.ExpectedVersion is not null) version = FileVersionInfo.GetVersionInfo(exe).FileVersion;
        if (string.IsNullOrWhiteSpace(version)) version = null;
        var manifest = Path.Combine(Directory.GetParent(game.InstallDir!)?.Parent?.FullName ?? game.InstallDir!,
            "appmanifest_" + game.SteamAppId + ".acf");
        string? build = null;
        if (File.Exists(manifest))
        {
            var text = File.ReadAllText(manifest);
            var directory = Regex.Match(text, "\"installdir\"\\s+\"([^\"]+)\"").Groups[1].Value;
            if (GameInstallationStore.SameRoot(Path.Combine(Path.GetDirectoryName(manifest)!, "common", directory), game.InstallDir))
                build = Regex.Match(text, "\"buildid\"\\s+\"([^\"]+)\"").Groups[1].Value;
        }
        if (string.IsNullOrWhiteSpace(build)) build = null;
        var hash = Hashing.Sha256OfFile(exe);
        var after = new FileInfo(exe);
        if (after.Length != beforeLength || after.LastWriteTimeUtc != beforeWrite)
            throw new IOException("The game executable changed during its compatibility check. Wait for the update to finish, then recheck.");
        return new(hash, version, build);
    }

    public string PackageKey(GameView game)
    {
        var inputs = new List<string>();
        foreach (var relative in new[] {
            $"config/modpacks/{game.Slug}.lock.json", $"config/redengine/native/{game.Slug}-backend.json",
            $"config/gta-vr/{game.Slug}.json", $"config/unreal/{game.Slug}.uevr.json",
            $"config/legacy/{game.Slug}-native.json" })
        {
            var path = Path.Combine(repoRoot, relative);
            if (File.Exists(path)) inputs.Add(relative + ":" + Hashing.Sha256OfFile(path));
        }
        if (game.Engine == GameEngine.Unreal)
            foreach (var relative in new[] { "config/unreal/uevr-release.json", "config/unreal/dotnet-desktop-runtime.json" })
            { var path = Path.Combine(repoRoot, relative); if (File.Exists(path)) inputs.Add(relative + ":" + Hashing.Sha256OfFile(path)); }
        return Hashing.Sha256OfBytes(Encoding.UTF8.GetBytes(string.Join('\n', inputs)));
    }

    public GameCompatibilityState Inspect(GameView game, bool observe = true)
    {
        try
        {
        if (game.InstallDir is null)
        {
            var selected = Installations.SelectedRoot(game.Slug);
            return new(false, false, false, false, selected is null ? "Select or install this game first."
                : "The selected installation is unavailable: " + selected + ". Reconnect its drive or choose another installation; no other copy was substituted.", "", null, null);
        }
            var descriptor = Describe(game);
            var identity = ReadIdentity(game);
            var key = PackageKey(game);
            var saved = Installations.Profile(game.Slug, game.InstallDir);
            if (saved.FirstObserved is null && observe)
            {
                saved = saved with { FirstObserved = identity };
                Installations.Update(game.Slug, saved);
            }
            var accepted = saved.HeadsetTested ?? saved.FirstObserved;
            var changed = accepted is not null && !SameBuild(accepted, identity);
            var versionPolicy = ReadVersionPolicy(game, identity.Version);
            var pinned = versionPolicy is not null || !string.IsNullOrWhiteSpace(descriptor.ExpectedHash) || descriptor.ExpectedVersion is not null;
            var supported = (versionPolicy?.Supported ?? true) && (string.IsNullOrWhiteSpace(descriptor.ExpectedHash) ||
                    descriptor.ExpectedHash.Equals(identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase)) &&
                (descriptor.ExpectedVersion is null || descriptor.ExpectedVersion == identity.Version);
            var needsUpdate = !game.ModInstalled || saved.PreparedPackage is not null && saved.PreparedPackage != key || !InstalledPackageMatches(game);
            var humanVerified = SameBuild(saved.HeadsetTested, identity) && saved.TestedPackage == key;
            var canTry = game.Readiness != GameReadiness.Blocked && supported && (!changed || pinned || humanVerified);
            var label = identity.Version ?? identity.SteamBuild ?? identity.ExecutableSha256[..12];
            var message = game.Readiness == GameReadiness.Blocked && supported ? game.ReadinessDetail
                : !supported ? $"Game build {label} does not match this VR integration's reviewed build. Choose an older compatible installation, use clean monitor play, or wait for a compatible package."
                : changed && !pinned && !humanVerified ? $"The game executable changed (now {label}). VR compatibility needs a new review or a headset test. An upstream release alone is not compatibility proof."
                : humanVerified ? $"You confirmed VR for this game build and package ({label})."
                : pinned ? $"Game build matches the reviewed native integration ({label}). Headset verification is still required."
                : $"Game build {label} recorded. This conversion remains experimental; startup checks do not establish headset compatibility.";
            return new(canTry, changed, pinned && supported, needsUpdate, message, key, identity, saved,
                MonitorModSession.IsParked(game.InstallDir));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        { return new(false, false, false, false, "Compatibility check could not complete: " + ex.Message, "", null, null); }
    }

    public static bool SameBuild(GameBuildIdentity? left, GameBuildIdentity? right) => left is not null && right is not null &&
        left.ExecutableSha256.Equals(right.ExecutableSha256, StringComparison.OrdinalIgnoreCase);

    private bool InstalledPackageMatches(GameView game)
    {
        if (game.InstallDir is null) return false;
        if (game.Engine == GameEngine.Redengine)
        {
            var manifest = Path.Combine(repoRoot, "config", "redengine", "native", game.Slug + "-backend.json");
            if (!File.Exists(manifest)) return true;
            var package = CyberpunkBackendInstaller.LoadPackage(manifest);
            return CyberpunkUpstreamSession.ReadState(game.InstallDir)?.Version == package.Version;
        }
        var lockPath = Path.Combine(repoRoot, "config", "modpacks", game.Slug + ".lock.json");
        if (!File.Exists(lockPath)) return true;
        var manifestPath = Path.Combine(game.InstallDir, "BepInEx", "vrclient-install-manifest.json");
        if (!File.Exists(manifestPath)) return false;
        using var doc = JsonDocument.Parse(File.ReadAllText(manifestPath));
        if (!doc.RootElement.TryGetProperty("packages", out var packages)) return false;
        var recorded = packages.Deserialize<VrClient.Core.Model.LockedPackage[]>(new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? [];
        return LockfileIo.Read(lockPath).Packages.All(expected => recorded.Any(actual => actual.Namespace == expected.Namespace && actual.Name == expected.Name && actual.Version == expected.Version));
    }

    public ActionOutcome SelectInstallation(GameView game, string root)
    {
        var candidate = game with { InstallDir = GameInstallationStore.Normalize(root) };
        var identity = ReadIdentity(candidate); // Validate the selected executable before saving anything.
        var profile = Installations.Profile(game.Slug, root);
        EnsureClosed(candidate);
        Installations.Update(game.Slug, profile with { FirstObserved = profile.FirstObserved ?? identity }, select: true, steamAppId: game.SteamAppId);
        return new(true, "Installation selected. " + SaveWarning);
    }

    public ActionOutcome ConfirmHeadsetTest(GameView game)
    {
        var state = Inspect(game);
        if (state.Identity is null || state.Profile is null) return new(false, state.Message);
        var descriptor = Describe(game);
        if (ReadVersionPolicy(game, state.Identity.Version) is { Supported: false }) return new(false, "This package does not support the installed game version.");
        if (descriptor.ExpectedHash is not null && !descriptor.ExpectedHash.Equals(state.Identity.ExecutableSha256, StringComparison.OrdinalIgnoreCase) ||
            descriptor.ExpectedVersion is not null && descriptor.ExpectedVersion != state.Identity.Version)
            return new(false, "A headset confirmation cannot override an exact-build native restriction.");
        Installations.Update(game.Slug, state.Profile with { HeadsetTested = state.Identity, TestedPackage = state.PackageKey });
        return new(true, "Your stereo, tracking and controls confirmation was saved for this installation and package only.");
    }

    public void SetAutomaticUpdates(GameView game, bool enabled)
    {
        if (game.InstallDir is null) return;
        Installations.Update(game.Slug, Installations.Profile(game.Slug, game.InstallDir) with { AutomaticUpdates = enabled });
    }

    public ControllerCustomization ControllersFor(GameView game)
    {
        var shared = new ControllerCustomization();
        if (game.InstallDir is null) return shared;
        var normalized = GameInstallationStore.Normalize(game.InstallDir);
        if (OperatingSystem.IsWindows()) normalized = normalized.ToLowerInvariant();
        var key = Hashing.Sha256OfBytes(Encoding.UTF8.GetBytes(normalized));
        var root = Path.Combine(Path.GetDirectoryName(Installations.PathOnDisk)!, "controller-profiles", game.Slug, key);
        var scoped = new ControllerCustomization(root, runtimeRoot: shared.RuntimeRoot);
        foreach (var name in new[] { "quest-touch.json", "valve-index.json", "defaults/quest-touch.json", "defaults/valve-index.json", "active-device.txt" })
        {
            var source = Path.Combine(shared.Root, game.Slug, name);
            var target = Path.Combine(scoped.Root, game.Slug, name);
            if (!File.Exists(source) || File.Exists(target)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            try { File.Copy(source, target); }
            catch (IOException) when (File.Exists(target)) { }
        }
        return scoped;
    }

    public async Task<ActionOutcome> PrepareVrAsync(GameView game,
        Func<CancellationToken, Task<ActionOutcome>> updateReviewed, CancellationToken ct = default,
        Func<GameView>? refreshGame = null, bool forceUpdate = false)
    {
        if (game.InstallDir is null) return new(false, "Select this game's installation first.");
        EnsureClosed(game);
        using var updateLease = MonitorModSession.AcquireUpdateLease(game.InstallDir);
        ModUpdateTransaction.Recover(game.InstallDir);
        if (PackageRecovery.Exists(game.InstallDir) && !PackageRecovery.Open(game.InstallDir).IsSealed)
            return new(false, "A previous package update was interrupted. Its backups were kept; resolve that recovery before installing another package.");
        var state = Inspect(game);
        if (!state.CanTryVr) return new(false, state.Message);
        MonitorModSession.Restore(game.InstallDir, () => EnsureClosed(game), updateLeaseHeld: true);
        if (state.ModsParked && refreshGame is not null)
        {
            var refreshed = refreshGame();
            if (!GameInstallationStore.SameRoot(refreshed.InstallDir, game.InstallDir))
                return new(false, "The selected installation changed during recovery. Recheck the intended game copy before playing.");
            game = refreshed;
            state = Inspect(game);
            if (!state.CanTryVr) return new(false, state.Message);
        }
        if (game.InstallDir is null) return new(false, "The selected installation became unavailable during recovery.");
        if (state.NeedsPackageUpdate && state.Profile?.AutomaticUpdates != true && !forceUpdate)
            return new(false, "This installation needs its approved VR package, but automatic updates are off. Choose Prepare approved VR package to install it manually, or use clean monitor play.");
        if (state.NeedsPackageUpdate)
        {
            var before = PackageRecovery.Capture(game.InstallDir, ManagedPaths(game));
            try
            {
                var outcome = await updateReviewed(ct).ConfigureAwait(false);
                before.Seal(ManagedPaths(game));
                if (!outcome.Ok) { before.Restore(() => EnsureClosed(game), updateLeaseHeld: true); return outcome; }
                var after = ReadIdentity(game);
                if (!SameBuild(state.Identity, after))
                { before.Restore(() => EnsureClosed(game), updateLeaseHeld: true); return new(false, "The game updated during VR preparation. Mod changes were restored; recheck the new game build."); }
            }
            catch { if (!before.IsSealed) before.Seal(ManagedPaths(game)); before.Restore(() => EnsureClosed(game), updateLeaseHeld: true); throw; }
        }
        if (state.Profile is not null)
            Installations.Update(game.Slug, state.Profile with { PreparedPackage = state.PackageKey });
        return new(true, "Compatibility checks finished. Verify the game image and controls in the headset after startup.");
    }

    public IReadOnlyList<string> ManagedPaths(GameView game)
    {
        if (game.InstallDir is null) return [];
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var relative in new[] { "BepInEx/vrclient-install-manifest.json", ".avrcade/cyberpunk-2077/install.json", "BepInEx/avrcade-yapyap-vr-install.json" })
        {
            var full = ModUpdateTransaction.Resolve(game.InstallDir, relative);
            if (!File.Exists(full)) continue;
            paths.Add(relative);
            using var doc = JsonDocument.Parse(File.ReadAllText(full));
            var values = doc.RootElement.TryGetProperty("files", out var lower) ? lower
                : doc.RootElement.TryGetProperty("Files", out var upper) ? upper : default;
            if (values.ValueKind != JsonValueKind.Array) continue;
            foreach (var file in values.EnumerateArray())
            {
                var name = file.ValueKind == JsonValueKind.String ? file.GetString()
                    : Text(file, "path") ?? Text(file, "Path");
                if (name is null) continue;
                paths.Add(name.Replace('\\', '/'));
                if (game.Engine == GameEngine.Redengine) paths.Add(".avrcade/cyberpunk-2077/payload/" + name.Replace('\\', '/'));
            }
        }
        foreach (var path in CleanMonitorPaths(game)) paths.Add(path);
        return paths.Where(p => !Directory.Exists(ModUpdateTransaction.Resolve(game.InstallDir, p))).ToArray();
    }

    public IReadOnlyList<string> CleanMonitorPaths(GameView game)
    {
        var binaryFolder = Path.GetDirectoryName(Describe(game).Executable)?.Replace('\\', '/') ?? "";
        string At(string name) => binaryFolder.Length == 0 ? name : binaryFolder + "/" + name;
        var paths = new List<string>();
        switch (game.Engine)
        {
            case GameEngine.Unity:
                paths.AddRange(new[] { "winhttp.dll", "doorstop_config.ini" }); break;
            case GameEngine.Redengine:
                paths.AddRange(new[] { At("version.dll"), At("winmm.dll"), "engine/config/base/scripts.ini",
                    "archive/pc/mod", "r6/scripts", "r6/tweaks", "mods" }); break;
            case GameEngine.Unreal:
                paths.AddRange(new[] { At("dwmapi.dll"), At("UE4SS.dll") });
                if (game.InstallDir is not null) paths.AddRange(UnrealMods.Scan(game.InstallDir, Describe(game).Executable).Files);
                break;
            case GameEngine.LegacyD3D9:
                paths.AddRange(new[] { At("dinput8.dll"), At("dsound.dll"), At("d3d9.dll"), At("dxgi.dll"), At("version.dll") }); break;
            case GameEngine.Fallout3:
            case GameEngine.FalloutNewVegas:
                paths.AddRange(new[] { At("d3d9.dll"), At("dxgi.dll"), At("dinput8.dll"), At("version.dll") }); break;
        }
        return paths.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
    }

    public ActionOutcome LaunchCleanMonitor(GameView game, bool dryRun = false)
    {
        if (game.InstallDir is null) return new(false, "Select this game's installation first.");
        var executable = ModUpdateTransaction.Resolve(game.InstallDir, Describe(game).Executable);
        if (!File.Exists(executable)) return new(false, "The selected executable is missing. No other game copy was launched.");
        if (dryRun) return new(true, "Clean monitor plan: " + executable);
        EnsureClosed(game);
        using var updateLease = MonitorModSession.AcquireUpdateLease(game.InstallDir);
        ModUpdateTransaction.Recover(game.InstallDir);
        var parked = MonitorModSession.Park(game.InstallDir, CleanMonitorPaths(game), () => EnsureClosed(game), updateLeaseHeld: true);
        // This is the selected game's normal executable, with no injection or
        // entitlement bypass. Games that require a store still require that store.
        var start = new ProcessStartInfo(executable) { WorkingDirectory = Path.GetDirectoryName(executable)!, UseShellExecute = false };
        if (game.Engine == GameEngine.Unity)
            foreach (var argument in new[] { "--doorstop-enabled", "false", "-vrmode", "none" }) start.ArgumentList.Add(argument);
        try
        {
            using var process = Process.Start(start);
            return process is null ? new(false, "The selected game did not start. Parked mods remain backed up.")
                : new ActionOutcome(true, $"Started the selected game on the monitor with {parked} detected mod files parked. Check the game window. Mods remain parked until Restore parked mods or the next compatible VR preparation.")
                    { LaunchedProcessId = process.Id, LaunchedExecutable = executable };
        }
        catch { MonitorModSession.Restore(game.InstallDir, () => EnsureClosed(game), updateLeaseHeld: true); throw; }
    }

    public bool CanUseStoreHandoff(GameView game)
    {
        if (game.InstallDir is null) return false;
        var registered = new SteamLibraryScanner().FindGame(game.SteamAppId, Describe(game).Executable);
        return registered is not null && GameInstallationStore.SameRoot(registered.InstallDir, game.InstallDir);
    }

    public ActionOutcome LaunchSelectedExecutable(GameView game, IEnumerable<string>? arguments = null, bool dryRun = false)
    {
        if (game.InstallDir is null) return new(false, "Select this game's installation first.");
        var executable = ModUpdateTransaction.Resolve(game.InstallDir, Describe(game).Executable);
        if (!File.Exists(executable)) return new(false, "The selected executable is missing. No other copy was launched.");
        if (dryRun) return new(true, "Selected installation launch plan: " + executable);
        EnsureClosed(game);
        var start = new ProcessStartInfo(executable) { WorkingDirectory = Path.GetDirectoryName(executable)!, UseShellExecute = false };
        foreach (var argument in arguments ?? []) start.ArgumentList.Add(argument);
        using var process = Process.Start(start);
        return process is null ? new(false, "The selected game did not start.")
            : new ActionOutcome(true, "Started the selected installation. Check the game window; normal store authentication still applies.")
                { LaunchedProcessId = process.Id, LaunchedExecutable = executable };
    }

    public string CompleteStartup(GameView game, bool running, bool confirmedCrash)
    {
        if (game.InstallDir is null || !PackageRecovery.Exists(game.InstallDir)) return "";
        var recovery = PackageRecovery.Open(game.InstallDir);
        if (running) { recovery.ConfirmStartup(); return " Package startup was observed; headset compatibility still needs your confirmation."; }
        if (!confirmedCrash || !recovery.PendingValidation) return "";
        recovery.Restore(() => EnsureClosed(game));
        SetAutomaticUpdates(game, false);
        return " The provisional mod update was rolled back. Automatic updates for this installation were paused to prevent a retry loop. Clean monitor play remains available.";
    }

    public void EnsureClosed(GameView game)
    {
        if (game.InstallDir is null) throw new IOException("The game folder is unavailable.");
        // Any same-title process is conservative: never switch files under another session.
        var names = new[] { Path.GetFileNameWithoutExtension(Describe(game).Executable) };
        foreach (var name in names)
            foreach (var process in Process.GetProcessesByName(name))
                using (process) throw new IOException("Close the game before switching installations or changing its VR files.");
    }

    public async Task<StartupObservation> ObserveStartupAsync(GameView game, TimeSpan? timeout = null,
        CancellationToken ct = default)
    {
        if (game.InstallDir is null) return StartupObservation.NotObserved;
        var expected = ModUpdateTransaction.Resolve(game.InstallDir, Describe(game).Executable);
        var deadline = DateTime.UtcNow + (timeout ?? TimeSpan.FromSeconds(30));
        Process? found = null;
        try
        {
            while (DateTime.UtcNow < deadline)
            {
                ct.ThrowIfCancellationRequested();
                foreach (var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(expected)))
                {
                    try
                    {
                        if (string.Equals(GameLaunchBinding.ProcessExecutable(process), expected, StringComparison.OrdinalIgnoreCase)) { found = process; break; }
                    }
                    catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException) { }
                    if (process != found) process.Dispose();
                }
                if (found is not null)
                {
                    var exit = found.WaitForExitAsync(ct);
                    if (await Task.WhenAny(exit, Task.Delay(TimeSpan.FromSeconds(8), ct)).ConfigureAwait(false) != exit)
                        return StartupObservation.Running;
                    await exit.ConfigureAwait(false);
                    if (found.ExitCode == 0) return StartupObservation.ExitedNormally;
                    return StartupObservation.ExitedWithError;
                }
                await Task.Delay(350, ct).ConfigureAwait(false);
            }
            return StartupObservation.NotObserved;
        }
        finally { found?.Dispose(); }
    }
}

/// A completed package update remains reversible until it is superseded. Foreign edits
/// and user configuration are never overwritten by automatic startup recovery.
public sealed class PackageRecovery
{
    private const string State = ".avrcade/compatibility/package-update.json";
    public sealed record Entry(string Path, string Backup, string? Before, string? After);
    public sealed record Record(string Id, bool Sealed, IReadOnlyList<Entry> Entries, bool StartupObserved = false);
    private readonly string _root;
    private Record _record;
    private PackageRecovery(string root, Record record) { _root = root; _record = record; }
    public bool IsSealed => _record.Sealed;
    public bool PendingValidation => _record.Sealed && !_record.StartupObserved;
    public void ConfirmStartup() { _record = _record with { StartupObserved = true }; Save(); }
    public static bool Exists(string root) => File.Exists(ModUpdateTransaction.Resolve(root, State));
    public static PackageRecovery Open(string root) => new(root,
        JsonSerializer.Deserialize<Record>(File.ReadAllText(ModUpdateTransaction.Resolve(root, State)))
        ?? throw new InvalidDataException("Package recovery metadata is unreadable."));
    private void Save() => AtomicFile.WriteAllText(ModUpdateTransaction.Resolve(_root, State), JsonSerializer.Serialize(_record));
    private static bool Preference(string path) => path.EndsWith(".ini", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".cfg", StringComparison.OrdinalIgnoreCase) || path.Contains("/config/", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith("UserSettings.json", StringComparison.OrdinalIgnoreCase) || path.EndsWith("vrport.ini", StringComparison.OrdinalIgnoreCase);
    public static PackageRecovery Capture(string root, IEnumerable<string> paths)
    {
        var id = Guid.NewGuid().ToString("N");
        var entries = new List<Entry>();
        foreach (var relative in paths.Distinct(StringComparer.OrdinalIgnoreCase).Where(p => !Preference(p)))
        {
            var target = ModUpdateTransaction.Resolve(root, relative);
            var backup = ".avrcade/compatibility/backups/" + id + "/" + relative.Replace('\\', '/');
            var before = File.Exists(target) ? Hashing.Sha256OfFile(target) : null;
            if (before is not null)
            {
                var fullBackup = ModUpdateTransaction.Resolve(root, backup);
                Directory.CreateDirectory(Path.GetDirectoryName(fullBackup)!);
                File.Copy(target, fullBackup);
            }
            entries.Add(new(relative, backup, before, null));
        }
        var snapshot = new PackageRecovery(root, new(id, false, entries)); snapshot.Save(); return snapshot;
    }
    public void Seal(IEnumerable<string> currentPaths)
    {
        if (_record.Sealed) return; // A retry must never bless later foreign edits as package output.
        var entries = _record.Entries.ToDictionary(e => e.Path, StringComparer.OrdinalIgnoreCase);
        foreach (var relative in currentPaths.Where(p => !Preference(p)))
            if (!entries.ContainsKey(relative)) entries[relative] = new(relative,
                ".avrcade/compatibility/backups/" + _record.Id + "/" + relative.Replace('\\', '/'), null, null);
        _record = _record with { Sealed = true, Entries = entries.Values.Select(e => e with {
            After = File.Exists(ModUpdateTransaction.Resolve(_root, e.Path)) ? Hashing.Sha256OfFile(ModUpdateTransaction.Resolve(_root, e.Path)) : null }).ToArray() };
        Save();
    }
    public void Restore(Action ensureClosed, bool updateLeaseHeld = false)
    {
        ensureClosed();
        using var updateLease = updateLeaseHeld ? null : MonitorModSession.AcquireUpdateLease(_root);
        if (!_record.Sealed) throw new IOException("A package update was interrupted before validation. Backups were preserved; repair the update before launching VR.");
        foreach (var entry in _record.Entries)
        {
            var full = ModUpdateTransaction.Resolve(_root, entry.Path);
            var current = File.Exists(full) ? Hashing.Sha256OfFile(full) : null;
            if (current != entry.After && current != entry.Before) throw new IOException("A mod changed after the update: " + entry.Path + ". Its backup was preserved.");
            if (entry.Before is not null && (!File.Exists(ModUpdateTransaction.Resolve(_root, entry.Backup)) ||
                Hashing.Sha256OfFile(ModUpdateTransaction.Resolve(_root, entry.Backup)) != entry.Before))
                throw new IOException("The mod update backup is missing or changed: " + entry.Path);
        }
        foreach (var entry in _record.Entries.Reverse())
        {
            ensureClosed();
            var target = ModUpdateTransaction.Resolve(_root, entry.Path);
            var current = File.Exists(target) ? Hashing.Sha256OfFile(target) : null;
            if (current != entry.After && current != entry.Before) throw new IOException("A mod changed during rollback: " + entry.Path);
            if (entry.Before is not null)
            { Directory.CreateDirectory(Path.GetDirectoryName(target)!); File.Copy(ModUpdateTransaction.Resolve(_root, entry.Backup), target, true); }
            else if (File.Exists(target)) File.Delete(target);
        }
        File.Delete(ModUpdateTransaction.Resolve(_root, State));
    }
}
