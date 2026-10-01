using System.Text.Json;
using System.Runtime.InteropServices;
using VrClient.Core;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Launch;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using VrClient.Core.ModManagers;
using VrClient.Core.Redengine;
using VrClient.Core.Rage;
using VrClient.Core.ModProfiles;
using VrClient.Core.Legacy;
using VrClient.Core.Safety;
using VrClient.Core.Unreal;

var repoRoot = FindRepoRoot();
if (repoRoot is null)
{
    Console.WriteLine("RESULT: error=repo-root-not-found");
    return 2;
}

const string GtaSanAndreasNativeConfigFileName = "vrclient_gtasa_theater.json";

if (args.Length == 0)
{
    Console.WriteLine("RESULT: error=no-verb (expected: status|safety|resolve|verify|install|configure|convert|launch|uninstall|games|native-preflight|add-game|check-game|doctor|rdr2-readiness|rdr2-preflight|rdr2-diagnostics|rdr2-live-diagnostics|rdr2-profile (list|create|clone|stage|stage-bridge|import|export|validate|apply|enable|disable|delete|recover)|rdr2-launch|cyberpunk-preflight|uevr-prepare|uevr-fetch|uevr-profile-import|uevr-profile-rollback|uevr-launch|gta-sa-preflight|gta-sa-convert|gta-sa-launch)");
    return 1;
}

var verb = args[0];
// Optional leading <slug> positional: the first token after the verb that is not
// an option (does not start with '-'). Absent -> default "repo" (M1 compat).
var slug = (args.Length >= 2 && !args[1].StartsWith("-", StringComparison.Ordinal)) ? args[1] : "repo";

string ModpackPath() => Path.Combine(repoRoot, "config", "modpacks", $"{slug}.modpack.json");
string LockfilePath() => Path.Combine(repoRoot, "config", "modpacks", $"{slug}.lock.json");
string ComfortMapPath() => Path.Combine(repoRoot, "config", "modpacks", $"{slug}.comfort-map.json");
string ProfilePath() => Path.Combine(repoRoot, "config", "profiles", $"{slug}-game-profile.json");

string? GetOption(string name)
{
    for (var i = 1; i < args.Length - 1; i++)
        if (args[i] == name)
            return args[i + 1];
    return null;
}
bool HasFlag(string name) => args.Skip(1).Contains(name);

Rdr2ProfileResult PrintProfiles(IReadOnlyList<Rdr2ProfileInfo> profiles)
{
    foreach (var profile in profiles)
        Console.WriteLine($"RDR2-PROFILE: id={profile.Id} active={profile.Active.ToString().ToLowerInvariant()}");
    return Rdr2ProfileResult.Ok("listed");
}

Rdr2ProfileResult StageRdr2Bridge(
    Rdr2ModProfileService service,
    string profileId,
    string buildId)
{
    var bridge = Rdr2NativeAdapterContract.FindBridgeBinary(repoRoot);
    if (bridge is null)
        return Rdr2ProfileResult.Refuse("bridge_artifact_missing");
    var temporary = Path.Combine(
        Path.GetTempPath(), "vrclient-rdr2-bridge-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(temporary);
        File.Copy(bridge, Path.Combine(temporary, "vrclient_rdr2_bridge.asi"));
        var bridgeRoot = Path.GetDirectoryName(bridge)!;
        foreach (var supportFile in new[] { "vulkan-1.dll", "runtime-profile.json" })
        {
            var source = Path.Combine(bridgeRoot, supportFile);
            if (File.Exists(source)) File.Copy(source, Path.Combine(temporary, supportFile));
        }
        return service.StageDirectory(profileId, temporary, buildId);
    }
    catch (IOException)
    {
        return Rdr2ProfileResult.Refuse("bridge_stage_failed");
    }
    catch (UnauthorizedAccessException)
    {
        return Rdr2ProfileResult.Refuse("bridge_stage_failed");
    }
    finally
    {
        if (Directory.Exists(temporary)) Directory.Delete(temporary, recursive: true);
    }
}

static string DefaultRdr2EvidenceRoot() =>
    Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "games", "red-dead-redemption-2", "evidence");

static string DefaultRdr2StateRoot() =>
    Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VRClient", "games", "red-dead-redemption-2");

string RequireGameDir()
    => GetOption("--game-dir")
       ?? throw new InvalidOperationException("missing required option --game-dir <dir>");

XrRuntimeChoice? SelectXrRuntime(bool for32BitProcess = false)
{
    var customManifest = GetOption("--xr-runtime-manifest");
    if (!string.IsNullOrWhiteSpace(customManifest))
    {
        var resolvedManifest = Path.GetFullPath(customManifest);
        if (!File.Exists(resolvedManifest))
            throw new InvalidOperationException($"xr_runtime_not_found - manifest does not exist: {resolvedManifest}");
        var custom = new XrRuntimeChoice("Custom", resolvedManifest);
        if (for32BitProcess && OpenXrRuntimeSelector.For32BitProcess(custom) is null)
            throw new InvalidOperationException(
                $"xr_runtime_architecture_mismatch - custom runtime manifest is not backed by a PE32/x86 runtime: {resolvedManifest}");
        return custom;
    }

    var requestedXr = (GetOption("--xr-runtime") ?? "auto").ToLowerInvariant();
    var availableXr = OpenXrRuntimeSelector.DiscoverAvailableRuntimes();
    if (requestedXr == "auto")
    {
        var running = OpenXrRuntimeSelector.GetRunningVrProcessNames();
        return for32BitProcess
            ? OpenXrRuntimeSelector.SelectFor32BitProcess(running, availableXr)
            : OpenXrRuntimeSelector.Select(running, availableXr);
    }

    var selected = requestedXr switch
    {
        "system" => null,
        "steamvr" => availableXr.FirstOrDefault(r => r.Name == OpenXrRuntimeSelector.SteamVrName)
            ?? throw new InvalidOperationException("xr_runtime_not_found - --xr-runtime steamvr requested but no SteamVR runtime manifest was found"),
        "vd" or "virtualdesktop" => availableXr.FirstOrDefault(r => r.Name == OpenXrRuntimeSelector.VirtualDesktopName)
            ?? throw new InvalidOperationException("xr_runtime_not_found - --xr-runtime vd requested but no Virtual Desktop runtime manifest was found"),
        _ => OpenXrRuntimeSelector.Select(OpenXrRuntimeSelector.GetRunningVrProcessNames(), availableXr)
    };
    var compatible = for32BitProcess
        ? OpenXrRuntimeSelector.For32BitProcess(selected)
        : selected;
    if (for32BitProcess && selected is not null && compatible is null && requestedXr is not ("auto" or "system"))
        throw new InvalidOperationException(
            $"xr_runtime_architecture_mismatch - {selected.Name} does not expose a usable PE32/x86 OpenXR runtime");
    return compatible;
}

VrClient.Core.Discovery.SteamGame? DiscoverRdr2()
    => new VrClient.Core.Discovery.SteamLibraryScanner()
        .FindGame(VrClient.Core.Launch.SteamOwnedLauncher.Rdr2AppId, "RDR2.exe", GetOption("--steam-root"));

string? DiscoverRdr2Build(string gameDir)
{
    var discovered = DiscoverRdr2();
    if (discovered is not null &&
        string.Equals(Path.GetFullPath(discovered.InstallDir), Path.GetFullPath(gameDir), StringComparison.OrdinalIgnoreCase))
        return discovered.BuildId;
    var manifest = GetOption("--steam-manifest");
    if (string.IsNullOrWhiteSpace(manifest)) return null;
    var match = System.Text.RegularExpressions.Regex.Match(
        File.ReadAllText(manifest), "\\\"buildid\\\"\\s+\\\"([0-9]+)\\\"");
    return match.Success ? match.Groups[1].Value : null;
}

SafetyVerdict EvaluateSafety() => new SafetyGate().Evaluate(
    Path.Combine(repoRoot, "config", "games", $"{slug}.json"),
    Path.Combine(repoRoot, "config", "safety", "default-rules.json"));

// The game's expected executable names (data-driven per game); null when absent.
IReadOnlyList<string>? ExecutableNames()
{
    var gameConfigPath = Path.Combine(repoRoot, "config", "games", $"{slug}.json");
    if (!File.Exists(gameConfigPath))
        return null;
    using var doc = JsonDocument.Parse(File.ReadAllText(gameConfigPath));
    if (!doc.RootElement.TryGetProperty("executable_names", out var names) || names.ValueKind != JsonValueKind.Array)
        return null;
    return names.EnumerateArray()
        .Where(n => n.ValueKind == JsonValueKind.String)
        .Select(n => n.GetString()!)
        .ToList();
}

// Returns non-null exit code when safety refuses (Block/UnknownBlocked, or Warn without --acknowledge).
int? RefuseUnlessSafe(SafetyVerdict verdict)
{
    if (verdict.Verdict is Verdict.Block or Verdict.UnknownBlocked)
    {
        Console.WriteLine($"RESULT: error=safety-refused reason={verdict.ReasonCode}");
        return 3;
    }
    if (verdict.Verdict == Verdict.Warn && !HasFlag("--acknowledge"))
    {
        Console.WriteLine($"RESULT: error=warn_not_acknowledged reason={verdict.ReasonCode} (pass --acknowledge)");
        return 3;
    }
    return null;
}

async Task<Lockfile> LiveResolveAndWriteLockfileAsync(HttpClient http)
{
    var spec = ModpackResolver.LoadSpec(ModpackPath());
    var resolver = new ModpackResolver(new LiveThunderstoreApi(http));
    var resolvedAt = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
    var unfilled = await resolver.ResolveAsync(spec, resolvedAt);

    // Trust-on-first-use: download each package once and record its sha256 in the
    // lockfile (no prior hash exists yet, so RequireHashesFilled is NOT called here).
    var filled = new List<LockedPackage>();
    foreach (var package in unfilled.Packages)
    {
        var bytes = await http.GetByteArrayAsync(package.DownloadUrl);
        filled.Add(package with { Sha256 = Hashing.Sha256OfBytes(bytes) });
    }
    var lockfile = unfilled with { Packages = filled };
    LockfileIo.Write(LockfilePath(), lockfile);
    return lockfile;
}

async Task<IReadOnlyList<string>> DownloadVerifiedAsync(HttpClient http, Lockfile lockfile)
{
    var cacheDir = Path.Combine(Path.GetTempPath(), "vrclient-cache", lockfile.GameSlug);
    return await new PackageDownloader(new CliHttpDownloader(http)).DownloadAllAsync(lockfile, cacheDir);
}

// M5 UNIFY-05: verify each pack's signature before install, fail-closed. Community
// packs (no sibling <zip>.manifest.json) are allowed (trust-on-first-use); a
// client-produced pack WITH a manifest must verify. Returns a non-null exit code on
// the first refusal. Uses the B6 trust-root/revocation fixtures; the native verify
// CLI is auto-located (NativeCliLocator: env var or the build output) when present,
// and a manifested pack fails closed if it cannot be verified.
int? RefuseUnsignedPacks(IReadOnlyList<string> zipPaths)
{
    var verifier = new NativeCliPackVerifier(
        Path.Combine(repoRoot, "config", "supply-chain", "trust-root.test.json"),
        Path.Combine(repoRoot, "config", "supply-chain", "revocations.empty.test.json"));
    foreach (var zip in zipPaths)
    {
        var (allowed, reason) = PackSigning.CheckBeforeInstall(zip, verifier);
        if (!allowed)
        {
            Console.WriteLine($"RESULT: error={reason} pack={Path.GetFileName(zip)}");
            return 1;
        }
    }
    return null;
}

(int Applied, int Unmatched) Configure(string gameDir)
{
    var mapping = ComfortConfigMapper.LoadMap(ComfortMapPath());
    var mapper = new ComfortConfigMapper();
    var entries = mapper.BuildCfgEntries(ProfilePath(), mapping);
    using var mapDoc = JsonDocument.Parse(File.ReadAllText(ComfortMapPath()));
    var cfgPath = Path.Combine(gameDir, mapDoc.RootElement.GetProperty("cfg_file").GetString()!);
    if (!File.Exists(cfgPath))
        return (0, entries.Count); // mod cfg appears after the first modded launch
    var (newText, unmatched) = mapper.ApplyToCfg(File.ReadAllText(cfgPath), entries);
    File.WriteAllText(cfgPath, newText);
    return (entries.Count - unmatched.Count, unmatched.Count);
}

// Game-scoped verbs require an onboarded game (its modpack must exist). Catalog
// verbs (games/add-game/check-game) and status are handled without this guard.
var gameScopedVerbs = new HashSet<string>
{
    "safety", "resolve", "verify", "install", "configure",
    "verify-comfort-map", "convert", "launch", "uninstall", "doctor"
};
if (gameScopedVerbs.Contains(verb) && !File.Exists(ModpackPath()))
{
    Console.WriteLine($"RESULT: error=unknown-game slug={slug}");
    return 2;
}

try
{
    switch (verb)
    {
        case "cyberpunk-install-vr":
        case "cyberpunk-remove-vr":
        {
            var gameDir = GetOption("--game-dir") ?? new VrClient.Core.Discovery.SteamLibraryScanner()
                .FindGame(CyberpunkNativePreflight.SteamAppId, Path.Combine("bin", "x64", "Cyberpunk2077.exe"),
                    GetOption("--steam-root"))?.InstallDir;
            if (gameDir is null)
            {
                Console.WriteLine($"RESULT: {verb} error=cyberpunk_not_found (pass --game-dir <dir>)");
                return 2;
            }
            var controller = new AppController(repoRoot);
            ActionOutcome outcome;
            if (verb == "cyberpunk-remove-vr")
                outcome = controller.UninstallCyberpunkVr(gameDir, HasFlag("--with-frameworks"));
            else if (HasFlag("--dry-run"))
                outcome = controller.PlanCyberpunkVrInstall(gameDir);
            else if (GetOption("--archive-dir") is { } archiveDir)
                // Offline install: the framework archives were fetched beforehand.
                // They still have to match the pinned hashes.
                outcome = await controller.InstallCyberpunkVrAsync(gameDir, new LocalArchiveDownloader(archiveDir));
            else
            {
                using var backendHttp = NewHttpClient();
                outcome = await controller.InstallCyberpunkVrAsync(gameDir, new CliHttpDownloader(backendHttp));
            }
            Console.WriteLine($"RESULT: {verb} ok={outcome.Ok.ToString().ToLowerInvariant()} game={gameDir} - {outcome.Message}");
            return outcome.Ok ? 0 : 1;
        }

        case "cyberpunk-preflight":
        case "cyberpunk-launch":
        {
            var steamRoot = GetOption("--steam-root");
            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner()
                .FindGame(CyberpunkNativePreflight.SteamAppId, Path.Combine("bin", "x64", "Cyberpunk2077.exe"), steamRoot);
            var gameDir = GetOption("--game-dir") ?? discovered?.InstallDir;
            var manifestPath = GetOption("--steam-manifest") ?? discovered?.ManifestPath;
            if (manifestPath is null && gameDir is not null)
            {
                var steamApps = Directory.GetParent(gameDir)?.Parent?.FullName;
                if (steamApps is not null)
                    manifestPath = Path.Combine(steamApps, $"appmanifest_{CyberpunkNativePreflight.SteamAppId}.acf");
            }

            var profilePath = Path.Combine(
                repoRoot, "config", "redengine", "native", "cyberpunk-2077.json");
            var preflight = new CyberpunkNativePreflight().Evaluate(
                profilePath, gameDir, manifestPath);
            Console.WriteLine($"CYBERPUNK-PREFLIGHT: status={preflight.Status} game={preflight.GameRoot ?? "(not found)"}");
            Console.WriteLine($"CYBERPUNK-PREFLIGHT: build expected={preflight.ExpectedBuildId ?? "(unpinned)"} actual={preflight.ActualBuildId ?? "(unknown)"}");
            Console.WriteLine($"CYBERPUNK-PREFLIGHT: sha256 expected={preflight.ExpectedSha256 ?? "(unpinned)"} actual={preflight.ActualSha256 ?? "(unknown)"}");
            if (preflight.MissingDependencies.Count > 0)
                Console.WriteLine($"CYBERPUNK-PREFLIGHT: missing={string.Join(',', preflight.MissingDependencies)}");
            if (preflight.Conflicts.Count > 0)
                Console.WriteLine($"CYBERPUNK-PREFLIGHT: conflicts={string.Join(',', preflight.Conflicts)}");
            if (verb == "cyberpunk-preflight" || !preflight.Ready)
            {
                Console.WriteLine($"RESULT: cyberpunk-preflight status={preflight.Status}");
                return preflight.ExitCode;
            }

            if (System.Diagnostics.Process.GetProcessesByName("Cyberpunk2077").Length > 0)
                throw new InvalidOperationException("cyberpunk_launch_refused - Cyberpunk2077 is already running");

            var launchMode = (GetOption("--mode") ?? "vr").ToLowerInvariant() switch
            {
                "vr" => CyberpunkLaunchMode.Vr,
                "flat" => CyberpunkLaunchMode.Flat,
                var invalid => throw new InvalidOperationException(
                    $"cyberpunk_launch_refused - invalid launch mode '{invalid}' (expected vr or flat)")
            };
            var withVortex = HasFlag("--with-vortex");
            // --with-mods is the manager-agnostic route: it passes REDmod's -modded
            // for whatever the player deployed, without requiring Vortex.
            var withMods = withVortex || HasFlag("--with-mods");
            if (withVortex && !new ModManagerDiscovery().FindInstalled()
                    .Any(manager => manager.Kind == ModManagerKind.Vortex))
                throw new InvalidOperationException(
                    "cyberpunk_launch_refused - Vortex was not found; open Manage Cyberpunk 2077 mods in Vortex to reconnect or select it");
            var requestedXr = (GetOption("--xr-runtime") ?? "auto").ToLowerInvariant();
            XrRuntimeChoice? xrRuntime = null;
            if (launchMode is CyberpunkLaunchMode.Vr)
            {
                var availableXr = OpenXrRuntimeSelector.DiscoverAvailableRuntimes(steamRoot);
                xrRuntime = requestedXr switch
                {
                    "system" => null,
                    "steamvr" => availableXr.FirstOrDefault(r => r.Name == OpenXrRuntimeSelector.SteamVrName)
                        ?? throw new InvalidOperationException("xr_runtime_not_found - SteamVR manifest was not found"),
                    "vd" or "virtualdesktop" => availableXr.FirstOrDefault(r => r.Name == OpenXrRuntimeSelector.VirtualDesktopName)
                        ?? throw new InvalidOperationException("xr_runtime_not_found - Virtual Desktop manifest was not found"),
                    _ => OpenXrRuntimeSelector.Select(OpenXrRuntimeSelector.GetRunningVrProcessNames(), availableXr)
                };
            }
            var plan = new LaunchPlan(
                preflight.ExecutablePath!,
                ModPresent: true,
                new SafetyVerdict(Verdict.Allow, "cyberpunk_preflight_ready", "", "Pinned REDengine backend is ready"));
            var dryRun = HasFlag("--dry-run");
            var session = new CyberpunkLaunchSession();
            var pid = new GameLauncher().Launch(
                plan, acknowledgeWarn: true, dryRun, xrRuntime,
                session.BuildEnvironment(launchMode), session.BuildLaunchArguments(withMods));
            var activationSource = GetOption("--activation-source") ?? "vrclient-cli";
            var diagnostics = session.WriteDiagnostics(
                Path.Combine(repoRoot, "artifacts"), launchMode, activationSource, xrRuntime, pid, withMods);
            Console.WriteLine(
                $"RESULT: cyberpunk-launch mode={launchMode.ToString().ToLowerInvariant()} pid={pid} " +
                $"xr={xrRuntime?.Name ?? "none"} mods={withMods} vortex={withVortex} backend=CyberpunkVR_Stereo diagnostics={diagnostics}");
            return 0;
        }
        case "rdr2-preflight":
        {
            var steamRoot = GetOption("--steam-root");
            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner().FindGame("1174180", "RDR2.exe", steamRoot);
            var gameDir = GetOption("--game-dir") ?? discovered?.InstallDir;
            var manifest = GetOption("--steam-manifest") ?? discovered?.ManifestPath;
            if (gameDir is not null)
            {
                discovered = new VrClient.Core.Discovery.SteamGame
                {
                    AppId = "1174180", InstallDir = gameDir, ExeName = "RDR2.exe",
                    ExecutablePath = Path.Combine(gameDir, "RDR2.exe"),
                    ManifestPath = manifest ?? string.Empty
                };
            }
            var mode = GetOption("--mode");
            var preflight = new Rdr2Preflight().Evaluate(Path.Combine(repoRoot, "config", "rage", "rdr2.json"), discovered, mode);
            Console.WriteLine($"RDR2-PREFLIGHT: mode={preflight.RequestedMode ?? "(none)"} install={preflight.InstallRoot ?? "(not found)"}");
            Console.WriteLine($"RDR2-PREFLIGHT: build expected={preflight.ExpectedBuildId ?? "(unpinned)"} actual={preflight.ActualBuildId ?? "(unknown)"}");
            Console.WriteLine($"RDR2-PREFLIGHT: sha256 expected={preflight.ExpectedSha256 ?? "(unpinned)"} actual={preflight.ActualSha256 ?? "(not read)"}");
            Console.WriteLine($"RESULT: rdr2-preflight status={preflight.Status}");
            return preflight.ExitCode;
        }
        case "rdr2-diagnostics":
        {
            var mode = GetOption("--mode");
            if (!string.Equals(mode, "story", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("RESULT: rdr2-diagnostics status=OnlineModeBlocked");
                return 19;
            }

            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner().FindGame("1174180", "RDR2.exe", GetOption("--steam-root"));
            var preflight = new Rdr2Preflight().Evaluate(Path.Combine(repoRoot, "config", "rage", "rdr2.json"), discovered, mode);
            if (discovered is null || preflight.ActualBuildId is null || preflight.ExecutablePath is null || !File.Exists(preflight.ExecutablePath))
            {
                Console.WriteLine($"RESULT: rdr2-diagnostics status={preflight.Status}");
                return preflight.ExitCode;
            }

            var evidenceRoot = GetOption("--evidence-root") ?? DefaultRdr2EvidenceRoot();
            var runId = GetOption("--run-id") ?? $"candidate-{DateTimeOffset.UtcNow:yyyyMMddHHmmss}";
            var evidence = Rdr2Diagnostics.CreateCandidate(
                runId,
                preflight.ActualBuildId,
                Hashing.Sha256OfFile(preflight.ExecutablePath),
                Rdr2Diagnostics.ObserveRendererImports(preflight.ExecutablePath));
            var path = Rdr2Diagnostics.Write(evidenceRoot, evidence);
            Console.WriteLine($"RESULT: rdr2-diagnostics status=candidate path={path}");
            return 0;
        }
        case "rdr2-live-diagnostics":
        {
            var mode = GetOption("--mode");
            if (!string.Equals(mode, "story", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("RESULT: rdr2-live-diagnostics status=OnlineModeBlocked");
                return 19;
            }

            var process = System.Diagnostics.Process.GetProcessesByName("RDR2").FirstOrDefault();
            if (process is null)
            {
                Console.WriteLine("RESULT: rdr2-live-diagnostics status=game_not_running");
                return 10;
            }

            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner().FindGame("1174180", "RDR2.exe", GetOption("--steam-root"));
            var preflight = new Rdr2Preflight().Evaluate(Path.Combine(repoRoot, "config", "rage", "rdr2.json"), discovered, mode);
            if (preflight.ActualBuildId is null || preflight.ExecutablePath is null || !File.Exists(preflight.ExecutablePath))
            {
                Console.WriteLine($"RESULT: rdr2-live-diagnostics status={preflight.Status}");
                return preflight.ExitCode;
            }

            string? liveExecutablePath;
            try
            {
                liveExecutablePath = process.MainModule?.FileName;
            }
            catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
            {
                Console.WriteLine($"RESULT: rdr2-live-diagnostics status=process_identity_unavailable detail={ex.GetType().Name}");
                return 22;
            }
            if (!Rdr2Diagnostics.ProcessMatchesExpectedExecutable(preflight.ExecutablePath, liveExecutablePath))
            {
                Console.WriteLine("RESULT: rdr2-live-diagnostics status=process_identity_mismatch");
                return 22;
            }

            var modules = process.Modules.Cast<System.Diagnostics.ProcessModule>()
                .Select(module => module.ModuleName);
            var bridgeLog = Rdr2Diagnostics.BridgeLogPath(preflight.InstallRoot!, process.Id);
            var cameraObservation = "unknown";
            if (File.Exists(bridgeLog))
            {
                var log = File.ReadAllText(bridgeLog);
                cameraObservation = log.Contains("stereo_submit", StringComparison.Ordinal)
                    && log.Contains("camera_ready", StringComparison.Ordinal)
                    ? "native_bridge_verified"
                    : log.Contains("scripthook_bound", StringComparison.Ordinal)
                        ? "native_bridge_candidate"
                        : "unknown";
            }
            var evidenceRoot = GetOption("--evidence-root") ?? DefaultRdr2EvidenceRoot();
            var runId = GetOption("--run-id") ?? $"live-{DateTimeOffset.UtcNow:yyyyMMddHHmmss}";
            var evidence = Rdr2Diagnostics.CreateCandidate(
                runId, preflight.ActualBuildId, Hashing.Sha256OfFile(preflight.ExecutablePath),
                Rdr2Diagnostics.ClassifyRendererModules(modules), cameraObservation);
            var path = Rdr2Diagnostics.Write(evidenceRoot, evidence);
            Console.WriteLine($"RESULT: rdr2-live-diagnostics status=candidate path={path}");
            return 0;
        }
        case "rdr2-readiness":
        {
            var mode = GetOption("--mode");
            if (!string.Equals(mode, "story", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("RESULT: rdr2-readiness status=online_not_supported");
                return 19;
            }

            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner().FindGame(
                "1174180", "RDR2.exe", GetOption("--steam-root"));
            var preflight = new Rdr2Preflight().Evaluate(
                Path.Combine(repoRoot, "config", "rage", "rdr2.json"), discovered, mode);
            Console.WriteLine($"RDR2-READINESS: preflight={preflight.Status}");
            if (!preflight.Ready)
            {
                Console.WriteLine("RDR2-READINESS: blocker=no_ready_story_preflight");
                Console.WriteLine($"RESULT: rdr2-readiness status=blocked_preflight");
                return preflight.ExitCode;
            }

            var adapter = Rdr2NativeAdapterContract.Evaluate(
                repoRoot, preflight.ActualBuildId!, preflight.InstallRoot);
            Console.WriteLine($"RDR2-READINESS: adapter={adapter.Status} id={adapter.AdapterId ?? "(missing)"} build={adapter.RequiredBuild ?? "(unknown)"}");
            if (!adapter.Ready)
            {
                Console.WriteLine($"RDR2-READINESS: blocker=native_bridge_services_required detail={adapter.Detail}");
                Console.WriteLine("RESULT: rdr2-readiness status=blocked_native_bridge_services");
                return 22;
            }

            var evidenceRoot = GetOption("--evidence-root") ?? DefaultRdr2EvidenceRoot();
            if (!Directory.Exists(evidenceRoot))
            {
                Console.WriteLine("RDR2-READINESS: blocker=evidence_root_missing");
                Console.WriteLine("RESULT: rdr2-readiness status=blocked_no_evidence_root");
                return 7;
            }

            var requestedRun = GetOption("--run-id");
            var evidenceFiles = Directory.GetFiles(evidenceRoot, "rdr2-*.json");
            if (evidenceFiles.Length == 0)
            {
                Console.WriteLine("RDR2-READINESS: blocker=no_evidence_files");
                Console.WriteLine("RESULT: rdr2-readiness status=blocked_no_evidence");
                return 7;
            }

            var evidencePath = requestedRun is null
                ? evidenceFiles.Select(path => new FileInfo(path))
                    .OrderByDescending(file => file.LastWriteTimeUtc)
                    .First()
                    .FullName
                : Path.Combine(evidenceRoot, $"rdr2-{requestedRun}.json");

            if (!File.Exists(evidencePath))
            {
                Console.WriteLine($"RDR2-READINESS: blocker=evidence_missing run={requestedRun}");
                Console.WriteLine("RESULT: rdr2-readiness status=blocked_evidence_missing");
                return 7;
            }

            var evidenceResult = Rdr2Diagnostics.EvaluateReadinessEvidence(
                evidencePath, preflight.ActualBuildId!, preflight.ActualSha256!);
            var evidence = evidenceResult.Evidence;
            Console.WriteLine($"RDR2-READINESS: evidence={Path.GetFileName(evidencePath)}");
            Console.WriteLine(
                $"RDR2-READINESS: compatibility={evidence?.Compatibility ?? "invalid"} " +
                $"renderer={evidence?.RendererObservation ?? "invalid"} " +
                $"camera={evidence?.CameraObservation ?? "invalid"}");
            if (!evidenceResult.Ready)
            {
                Console.WriteLine($"RDR2-READINESS: blocker={evidenceResult.Status} detail={evidenceResult.Detail ?? "none"}");
                Console.WriteLine($"RESULT: rdr2-readiness status=blocked_{evidenceResult.Status}");
                return 22;
            }
            Console.WriteLine("RESULT: rdr2-readiness status=ready_for_smoke");
            return 0;
        }
        case "rdr2-launch":
        {
            var mode = GetOption("--mode");
            if (!string.Equals(mode, "story", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine("RESULT: rdr2-launch status=OnlineModeBlocked");
                return 19;
            }
            var renderer = (GetOption("--renderer") ?? "dx12").ToLowerInvariant();
            if (renderer != "dx12")
            {
                Console.WriteLine("RDR2-LAUNCH: renderer=dx12_required_for_rdr2_bridge");
                Console.WriteLine("RESULT: rdr2-launch status=unsupported_renderer");
                return 22;
            }
            var steamRoot = GetOption("--steam-root");
            var discovered = new VrClient.Core.Discovery.SteamLibraryScanner()
                .FindGame(SteamOwnedLauncher.Rdr2AppId, "RDR2.exe", steamRoot);
            var gameDir = GetOption("--game-dir") ?? discovered?.InstallDir;
            var manifest = GetOption("--steam-manifest") ?? discovered?.ManifestPath;
            if (gameDir is not null)
            {
                discovered = new VrClient.Core.Discovery.SteamGame
                {
                    AppId = SteamOwnedLauncher.Rdr2AppId,
                    InstallDir = gameDir,
                    ExeName = "RDR2.exe",
                    ExecutablePath = Path.Combine(gameDir, "RDR2.exe"),
                    ManifestPath = manifest ?? string.Empty
                };
            }
            var preflight = new Rdr2Preflight().Evaluate(
                Path.Combine(repoRoot, "config", "rage", "rdr2.json"), discovered, mode);
            Console.WriteLine($"RDR2-LAUNCH: preflight={preflight.Status}");
            if (!preflight.Ready)
            {
                Console.WriteLine($"RESULT: rdr2-launch status={preflight.Status}");
                return preflight.ExitCode;
            }
            if (System.Diagnostics.Process.GetProcessesByName("RDR2").Length > 0 ||
                System.Diagnostics.Process.GetProcessesByName("PlayRDR2").Length > 0)
            {
                Console.WriteLine("RESULT: rdr2-launch status=game_running");
                return 22;
            }
            var profileId = GetOption("--profile");
            var profileAppliedByLaunch = false;
            if (profileId is not null)
            {
                if (gameDir is null)
                {
                    Console.WriteLine("RESULT: rdr2-launch status=game_root_required_for_profile");
                    return 10;
                }
                var stateRoot = GetOption("--state-root") ?? DefaultRdr2StateRoot();
                var profiles = new Rdr2ModProfileService(gameDir, stateRoot);
                var apply = profiles.ApplyProfile(profileId, preflight.ActualBuildId!);
                if (!apply.Success && apply.Status != "profile_already_active")
                {
                    Console.WriteLine($"RDR2-LAUNCH: profile={apply.Status}");
                    Console.WriteLine($"RESULT: rdr2-launch status={apply.Status}");
                    return 22;
                }
                profileAppliedByLaunch = apply.Success;
            }
            var adapter = Rdr2NativeAdapterContract.Evaluate(
                repoRoot, preflight.ActualBuildId!, gameDir);
            if (!adapter.Ready)
            {
                if (profileAppliedByLaunch && profileId is not null)
                    new Rdr2ModProfileService(gameDir!, GetOption("--state-root") ?? DefaultRdr2StateRoot())
                        .DisableProfile(profileId);
                Console.WriteLine($"RDR2-LAUNCH: adapter={adapter.Status} detail={adapter.Detail}");
                Console.WriteLine("RESULT: rdr2-launch status=native_bridge_services_required");
                return 22;
            }
            var launch = new SteamOwnedLauncher().Launch(
                SteamOwnedLauncher.Rdr2AppId,
                steamRoot,
                HasFlag("--dry-run"),
                launchArguments: Rdr2LaunchPolicy.SteamArguments);
            if ((!launch.Success || HasFlag("--dry-run")) &&
                profileAppliedByLaunch && profileId is not null)
            {
                var restore = new Rdr2ModProfileService(
                    gameDir!, GetOption("--state-root") ?? DefaultRdr2StateRoot())
                    .DisableProfile(profileId);
                Console.WriteLine($"RDR2-LAUNCH: profile_restore={restore.Status}");
            }
            Console.WriteLine($"RDR2-LAUNCH: status={launch.Status} steam={launch.SteamExecutable ?? "(not found)"}");
            Console.WriteLine("RDR2-LAUNCH: renderer=dx12");
            Console.WriteLine($"RESULT: rdr2-launch status={(launch.Success ? "started" : launch.Status)} pid={launch.ProcessId?.ToString() ?? "(dry-run)"}");
            return launch.Success ? 0 : 5;
        }
        case "rdr2-profile":
        {
            if (args.Length < 2) throw new InvalidOperationException("missing RDR2 profile subcommand");
            var command = args[1].ToLowerInvariant();
            var profileId = args.Length >= 3 && !args[2].StartsWith("-", StringComparison.Ordinal) ? args[2] : null;
            var gameDir = GetOption("--game-dir") ?? DiscoverRdr2()?.InstallDir;
            var stateRoot = GetOption("--state-root") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VRClient", "games", "red-dead-redemption-2");
            if (string.IsNullOrWhiteSpace(gameDir) && command is not ("list" or "import"))
                throw new InvalidOperationException("RDR2 was not found. Pass --game-dir <dir> or --steam-root <Steam root>. ");
            var service = new Rdr2ModProfileService(gameDir ?? Path.Combine(stateRoot, "game-placeholder"), stateRoot);
            if (command is "validate" or "apply" or "enable")
            {
                if (gameDir is null) throw new InvalidOperationException("RDR2 game directory is required for this operation");
                var discovered = DiscoverRdr2();
                var identity = new Rdr2Preflight().Evaluate(
                    Path.Combine(repoRoot, "config", "rage", "rdr2.json"),
                    new VrClient.Core.Discovery.SteamGame
                    {
                        AppId = "1174180", InstallDir = gameDir, ExeName = "RDR2.exe",
                        ExecutablePath = Path.Combine(gameDir, "RDR2.exe"),
                        ManifestPath = GetOption("--steam-manifest") ?? discovered?.ManifestPath ?? string.Empty
                    }, "story");
                if (!identity.Ready)
                {
                    Console.WriteLine($"RESULT: rdr2-profile status={identity.Status}");
                    return identity.ExitCode;
                }
            }
            var build = command is "stage" or "stage-bridge" or "validate" or "apply" or "enable"
                ? GetOption("--steam-build") ??
                  (gameDir is not null ? DiscoverRdr2Build(gameDir) : null) ??
                  throw new InvalidOperationException("missing Steam build ID; pass --steam-build <id> or use a discovered Steam install")
                : string.Empty;
            Rdr2ProfileResult result = command switch
            {
                "list" => PrintProfiles(service.ListProfiles()),
                "create" when profileId is not null => service.CreateProfile(profileId),
                "clone" when profileId is not null && args.Length >= 4 => service.CloneProfile(profileId, args[3]),
                "delete" when profileId is not null => service.DeleteProfile(profileId),
                "stage" when profileId is not null => service.StageDirectory(profileId, GetOption("--source") ?? string.Empty, build),
                "stage-bridge" when profileId is not null => StageRdr2Bridge(service, profileId, build),
                "import" when profileId is not null => service.ImportProfile(profileId, GetOption("--source") ?? string.Empty),
                "export" when profileId is not null => service.ExportProfile(profileId, GetOption("--destination") ?? string.Empty),
                "validate" when profileId is not null => service.ValidateStaging(profileId, build),
                "apply" or "enable" when profileId is not null => service.ApplyProfile(profileId, build),
                "disable" when profileId is not null => service.DisableProfile(profileId),
                "recover" => service.RecoverIncompleteTransaction(),
                _ => Rdr2ProfileResult.Refuse("invalid_rdr2_profile_command")
            };
            Console.WriteLine($"RESULT: rdr2-profile status={result.Status}");
            return result.Success ? 0 : 4;
        }
        case "games":
        {
            var games = new AppController(repoRoot).ListGames();
            foreach (var game in games)
                Console.WriteLine(
                    $"GAME: {game.Slug} name=\"{game.DisplayName}\" engine={game.Engine} " +
                    $"steamAppId={game.SteamAppId} readiness={game.Readiness} " +
                    $"safety={game.SafetyVerdict}:{game.SafetyReason}");
            Console.WriteLine($"RESULT: games={games.Count}");
            return 0;
        }
        case "gta-sa-launch":
        {
            var gameDir = RequireGameDir();
            var profilePath = Path.Combine(repoRoot, "config", "legacy", "gta-san-andreas.json");
            var bridgePath = Path.Combine(gameDir, "vrclient_gtasa_theater.asi");
            var loaderPath = Path.Combine(gameDir, "openxr_loader.dll");
            var nativeConfigPath = Path.Combine(gameDir, GtaSanAndreasNativeConfigFileName);
            var pinnedNativeConfigPath = Path.Combine(
                repoRoot, "config", "legacy", "gta-san-andreas-native.json");
            var preflight = new GtaSanAndreasPreflight().Evaluate(profilePath, gameDir, bridgePath);
            if (!preflight.Ready)
            {
                Console.WriteLine($"RESULT: gta-sa-launch status={preflight.Status}");
                return preflight.ExitCode;
            }
            if (!File.Exists(loaderPath))
            {
                Console.WriteLine("RESULT: gta-sa-launch status=OpenXrLoaderNotFound");
                return 27;
            }
            if (GtaSanAndreasPreflight.ReadPeArchitecture(loaderPath) != "x86")
            {
                Console.WriteLine("RESULT: gta-sa-launch status=OpenXrLoaderArchitectureMismatch");
                return 28;
            }
            if (!File.Exists(nativeConfigPath))
            {
                Console.WriteLine($"RESULT: gta-sa-launch status=NativeHookProfileNotFound path={nativeConfigPath}");
                return 29;
            }
            if (!File.Exists(pinnedNativeConfigPath) ||
                !string.Equals(Hashing.Sha256OfFile(nativeConfigPath),
                    Hashing.Sha256OfFile(pinnedNativeConfigPath), StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine($"RESULT: gta-sa-launch status=NativeHookProfileHashMismatch path={nativeConfigPath}");
                return 30;
            }

            var safety = new SafetyVerdict(
                Verdict.Allow,
                "offline_only_ok",
                "safety.allow.offline",
                "classic GTA San Andreas experimental stereo launch is restricted to the local offline install");
            var launcher = new GameLauncher();
            var plan = launcher.Plan(gameDir, safety, modInstalled: true, exeName: "gta_sa.exe");
            var dryRun = HasFlag("--dry-run");
            var childEnvironment = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["VRCLIENT_GTASA_OPENXR_LOADER"] = loaderPath,
                ["VRCLIENT_GTASA_HOOK_CONFIG"] = nativeConfigPath,
                ["VRCLIENT_GTASA_STEREO"] = "1",
                ["VRCLIENT_GTASA_INPUT"] = "1"
            };
            foreach (var (key, value) in GtaSanAndreasTurnSettings.LaunchEnvironment(
                new GtaSanAndreasTurnSettings().Load()))
                childEnvironment[key] = value;
            var gtaXrRuntime = SelectXrRuntime(for32BitProcess: true);
            var pid = launcher.Launch(
                plan,
                acknowledgeWarn: true,
                dryRun: dryRun,
                xrRuntime: gtaXrRuntime,
                childEnvironment: childEnvironment);
            Console.WriteLine(dryRun
                ? "RESULT: gta-sa-launch status=dry_run"
                : $"RESULT: gta-sa-launch status=launched pid={pid}");
            return 0;
        }
        case "gta-sa-preflight":
        case "gta-sa-convert":
        {
            var profilePath = Path.Combine(repoRoot, "config", "legacy", "gta-san-andreas.json");
            var gameDir = GetOption("--game-dir") ?? GtaSanAndreasPreflight.FindDefaultInstall();
            var bridgePath = GetOption("--bridge");
            var nativeConfigSource = Path.Combine(repoRoot, "config", "legacy", "gta-san-andreas-native.json");
            if (bridgePath is null && verb == "gta-sa-preflight" && gameDir is not null)
            {
                var installedBridge = Path.Combine(gameDir, "vrclient_gtasa_theater.asi");
                if (File.Exists(installedBridge))
                    bridgePath = installedBridge;
            }
            if (bridgePath is null && verb == "gta-sa-convert")
            {
                var builtBridge = Path.Combine(
                    repoRoot, "build", "gtasa-x86", "vrclient_gtasa_theater.asi");
                if (File.Exists(builtBridge))
                    bridgePath = builtBridge;
            }
            var preflight = new GtaSanAndreasPreflight().Evaluate(profilePath, gameDir, bridgePath);

            Console.WriteLine($"GTA-SA-PREFLIGHT: install={preflight.InstallRoot ?? "(not found)"}");
            Console.WriteLine($"GTA-SA-PREFLIGHT: executable={preflight.ExecutablePath ?? "(not found)"}");
            Console.WriteLine($"GTA-SA-PREFLIGHT: architecture expected={preflight.ExpectedArchitecture ?? "(missing)"} actual={preflight.ActualArchitecture ?? "(unknown)"}");
            Console.WriteLine($"GTA-SA-PREFLIGHT: sha256 expected={preflight.ExpectedSha256 ?? "(unpinned)"} actual={preflight.ActualSha256 ?? "(not computed)"}");
            Console.WriteLine($"GTA-SA-PREFLIGHT: bridge={preflight.BridgePath ?? "(required; none supplied)"} bridgeArchitecture={preflight.ActualBridgeArchitecture ?? "(not checked)"}");
            Console.WriteLine($"GTA-SA-PREFLIGHT: modArtifacts={(preflight.ModArtifacts.Count == 0 ? "(none observed)" : string.Join(',', preflight.ModArtifacts))}");
            var installed = false;
            if (verb == "gta-sa-convert")
            {
                if (preflight.BridgePath is not null && File.Exists(preflight.BridgePath))
                    Console.WriteLine($"GTA-SA-CONVERT: bridge-artifact={preflight.BridgePath}");
                if (HasFlag("--install"))
                {
                    if (gameDir is null || !Directory.Exists(gameDir))
                        throw new InvalidOperationException("gta-sa-install refused: game directory was not found");
                    if (preflight.BridgePath is null || !File.Exists(preflight.BridgePath))
                        throw new InvalidOperationException("gta-sa-install refused: build or supply an x86 bridge with --bridge");
                    if (!preflight.Ready)
                        throw new InvalidOperationException(
                            $"gta-sa-install refused: bridge preflight is {preflight.Status}; only the catalog-pinned x86 bridge may be installed");
                    var loaderSource = GetOption("--openxr-loader") ??
                        Path.Combine(repoRoot, "build", "gtasa-x86", "openxr_loader.dll");
                    if (!File.Exists(loaderSource))
                        throw new InvalidOperationException($"gta-sa-install refused: x86 OpenXR loader was not found ({loaderSource})");
                    if (GtaSanAndreasPreflight.ReadPeArchitecture(loaderSource) != "x86")
                        throw new InvalidOperationException("gta-sa-install refused: OpenXR loader is not PE32/x86");
                    var destination = Path.Combine(gameDir, "vrclient_gtasa_theater.asi");
                    if (!File.Exists(nativeConfigSource))
                        throw new InvalidOperationException($"gta-sa-install refused: native hook profile was not found ({nativeConfigSource})");
                    var nativeConfigDestination = Path.Combine(gameDir, GtaSanAndreasNativeConfigFileName);
                    var updateBridge = HasFlag("--update-bridge");
                    var updateNativeConfig = false;
                    if (File.Exists(nativeConfigDestination) &&
                        !string.Equals(Hashing.Sha256OfFile(nativeConfigDestination),
                            Hashing.Sha256OfFile(nativeConfigSource), StringComparison.OrdinalIgnoreCase))
                    {
                        if (!updateBridge)
                            throw new InvalidOperationException($"gta-sa-install refused: existing native hook profile differs ({nativeConfigDestination}); review it manually");
                        var expectedNativeHash = GetOption("--from-native-config-sha256");
                        if (string.IsNullOrWhiteSpace(expectedNativeHash))
                            throw new InvalidOperationException("gta-sa-install refused: a changed native profile requires --from-native-config-sha256 <sha256>");
                        var existingNativeHash = Hashing.Sha256OfFile(nativeConfigDestination);
                        if (!string.Equals(existingNativeHash, expectedNativeHash,
                                StringComparison.OrdinalIgnoreCase))
                            throw new InvalidOperationException($"gta-sa-install refused: existing native profile hash {existingNativeHash} did not match --from-native-config-sha256");
                        updateNativeConfig = true;
                    }
                    if (File.Exists(destination))
                    {
                        if (!updateBridge)
                            throw new InvalidOperationException($"gta-sa-install refused: destination already exists ({destination}); use --update-bridge with --from-bridge-sha256 only after review");
                        var expectedExistingHash = GetOption("--from-bridge-sha256");
                        if (string.IsNullOrWhiteSpace(expectedExistingHash))
                            throw new InvalidOperationException("gta-sa-install refused: --update-bridge requires --from-bridge-sha256 <sha256>");
                        var existingHash = Hashing.Sha256OfFile(destination);
                        if (!string.Equals(existingHash, expectedExistingHash, StringComparison.OrdinalIgnoreCase))
                            throw new InvalidOperationException($"gta-sa-install refused: existing bridge hash {existingHash} did not match --from-bridge-sha256");
                    }
                    var loaderDestination = Path.Combine(gameDir, "openxr_loader.dll");
                    if (File.Exists(loaderDestination) && !updateBridge)
                        throw new InvalidOperationException($"gta-sa-install refused: destination already exists ({loaderDestination}); remove it manually only after review");
                    if (File.Exists(loaderDestination) && GtaSanAndreasPreflight.ReadPeArchitecture(loaderDestination) != "x86")
                        throw new InvalidOperationException($"gta-sa-install refused: existing {loaderDestination} is not PE32/x86");
                    var bridgeBackup = updateBridge
                        ? destination + ".vrclient-update-backup"
                        : null;
                    var nativeConfigBackup = updateNativeConfig
                        ? nativeConfigDestination + ".vrclient-update-backup"
                        : null;
                    if (bridgeBackup is not null && File.Exists(bridgeBackup))
                        throw new InvalidOperationException($"gta-sa-install refused: update backup already exists ({bridgeBackup}); review it manually");
                    if (nativeConfigBackup is not null && File.Exists(nativeConfigBackup))
                        throw new InvalidOperationException($"gta-sa-install refused: update backup already exists ({nativeConfigBackup}); review it manually");
                    var bridgeCopied = false;
                    var loaderCopied = false;
                    var nativeConfigCopied = false;
                    try
                    {
                        if (bridgeBackup is not null)
                            File.Copy(destination, bridgeBackup);
                        if (nativeConfigBackup is not null)
                            File.Copy(nativeConfigDestination, nativeConfigBackup);
                        File.Copy(preflight.BridgePath, destination, overwrite: updateBridge);
                        bridgeCopied = true;
                        if (!File.Exists(loaderDestination))
                        {
                            File.Copy(loaderSource, loaderDestination);
                            loaderCopied = true;
                        }
                        if (!File.Exists(nativeConfigDestination) || updateNativeConfig)
                        {
                            File.Copy(nativeConfigSource, nativeConfigDestination,
                                overwrite: updateNativeConfig);
                            nativeConfigCopied = true;
                        }
                        if (bridgeBackup is not null && File.Exists(bridgeBackup))
                            File.Delete(bridgeBackup);
                        if (nativeConfigBackup is not null && File.Exists(nativeConfigBackup))
                            File.Delete(nativeConfigBackup);
                    }
                    catch
                    {
                        if (bridgeBackup is not null && File.Exists(bridgeBackup))
                        {
                            File.Copy(bridgeBackup, destination, overwrite: true);
                            File.Delete(bridgeBackup);
                        }
                        else if (!updateBridge && bridgeCopied && File.Exists(destination))
                            File.Delete(destination);
                        if (loaderCopied && File.Exists(loaderDestination))
                            File.Delete(loaderDestination);
                        if (nativeConfigBackup is not null && File.Exists(nativeConfigBackup))
                        {
                            File.Copy(nativeConfigBackup, nativeConfigDestination, overwrite: true);
                            File.Delete(nativeConfigBackup);
                        }
                        else if (nativeConfigCopied && File.Exists(nativeConfigDestination))
                            File.Delete(nativeConfigDestination);
                        throw;
                    }
                    Console.WriteLine($"GTA-SA-CONVERT: installed={destination}");
                    Console.WriteLine($"GTA-SA-CONVERT: installed={loaderDestination}");
                    Console.WriteLine($"GTA-SA-CONVERT: installed={nativeConfigDestination}");
                    Console.WriteLine(updateBridge
                        ? "GTA-SA-CONVERT: updated only the hash-guarded VRClient bridge/profile transaction; unrelated mod/config files and the game executable were not replaced."
                        : "GTA-SA-CONVERT: existing ASI/config files were not replaced; launch uses explicit experimental stereo mode.");
                    installed = true;
                    preflight = new GtaSanAndreasPreflight().Evaluate(profilePath, gameDir, destination);
                }
                else
                {
                    Console.WriteLine("GTA-SA-CONVERT: no game files changed; pass --install only after reviewing the x86 bridge artifact.");
                }
            }
            Console.WriteLine($"RESULT: {verb} status={preflight.Status}");
            return installed ? 0 : preflight.ExitCode;
        }
        case "native-preflight":
        {
            var nativeProfilePath = Path.Combine(
                repoRoot, "config", "unreal", "native", $"{slug}.json");
            var preflight = new NativeUnrealPreflight().Evaluate(
                nativeProfilePath, GetOption("--steam-root"));

            Console.WriteLine($"NATIVE-PREFLIGHT: profile={preflight.ProfilePath}");
            Console.WriteLine(
                $"NATIVE-PREFLIGHT: install={preflight.InstallDir ?? "(not found)"}");
            Console.WriteLine(
                $"NATIVE-PREFLIGHT: executable={preflight.ExecutablePath ?? "(not found)"}");
            Console.WriteLine(
                $"NATIVE-PREFLIGHT: manifest={preflight.ManifestPath ?? "(not found)"}");
            Console.WriteLine(
                $"NATIVE-PREFLIGHT: build expected={preflight.ExpectedBuildId ?? "(missing)"} " +
                $"actual={preflight.ActualBuildId ?? "(missing)"}");
            if (preflight.ExpectedSha256 is not null ||
                preflight.ActualSha256 is not null)
            {
                Console.WriteLine(
                    $"NATIVE-PREFLIGHT: sha256 expected={preflight.ExpectedSha256 ?? "(unpinned)"} " +
                    $"actual={preflight.ActualSha256 ?? "(not computed)"}");
            }
            Console.WriteLine($"NATIVE-PREFLIGHT: detail={preflight.Detail}");
            Console.WriteLine(
                $"RESULT: native-preflight status={preflight.Status} " +
                $"game={preflight.GameSlug} app={preflight.SteamAppId}");
            return preflight.ExitCode;
        }
        case "add-game":
        {
            var appId = GetOption("--app-id") ?? throw new InvalidOperationException("missing required option --app-id <id>");
            var community = GetOption("--community") ?? throw new InvalidOperationException("missing required option --community <slug>");
            var modsRaw = GetOption("--mods") ?? throw new InvalidOperationException("missing required option --mods <ns/name,ns/name,...>");
            var mods = new List<(string Namespace, string Name)>();
            foreach (var token in modsRaw.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                var slash = token.IndexOf('/');
                if (slash <= 0 || slash == token.Length - 1)
                    throw new InvalidOperationException($"invalid --mods entry '{token}' (expected namespace/name)");
                mods.Add((token[..slash], token[(slash + 1)..]));
            }
            var result = new VrClient.Core.Onboarding.GameScaffolder().Scaffold(
                new VrClient.Core.Onboarding.ScaffoldRequest(slug, appId, community, mods),
                Path.Combine(repoRoot, "config", "modpacks"),
                Path.Combine(repoRoot, "config", "games"),
                Path.Combine(repoRoot, "config", "profiles"),
                Path.Combine(repoRoot, "config", "controller-maps"));
            if (!result.Created)
            {
                Console.WriteLine($"RESULT: refused reason={result.RefusalReason}");
                return 4;
            }
            foreach (var file in result.WrittenFiles)
                Console.WriteLine($"WROTE: {file}");
            Console.WriteLine($"RESULT: added={slug}");
            return 0;
        }
        case "check-game":
        {
            var report = new VrClient.Core.Onboarding.OnboardingChecker().Check(
                slug,
                Path.Combine(repoRoot, "config", "modpacks"),
                Path.Combine(repoRoot, "config", "games"),
                Path.Combine(repoRoot, "config", "profiles"),
                Path.Combine(repoRoot, "config", "safety", "default-rules.json"));
            foreach (var item in report.Items)
                Console.WriteLine($"CHECK: {item.Id} {(item.Pass ? "PASS" : "FAIL")} {item.Detail}");
            Console.WriteLine($"RESULT: check={(report.AllPass ? "pass" : "fail")} slug={slug}");
            return report.AllPass ? 0 : 5;
        }
        case "doctor":
        {
            var spec = ModpackResolver.LoadSpec(ModpackPath());
            var scanner = new VrClient.Core.Discovery.SteamLibraryScanner();
            var game = scanner.FindGame(spec.SteamAppId,
                ExecutableNames()?.FirstOrDefault() ?? "REPO.exe", GetOption("--steam-root"));
            var gameDir = GetOption("--game-dir") ?? game?.InstallDir;
            var offlineLibrary = gameDir is null
                ? scanner.FindOfflineLibrary(spec.SteamAppId, GetOption("--steam-root")) : null;
            var modInstalled = gameDir is not null && new ModInstaller().IsInstalled(gameDir);
            var verdict = EvaluateSafety();
            var running = OpenXrRuntimeSelector.GetRunningVrProcessNames();
            var steamVrUp = OpenXrRuntimeSelector.IsSteamVrRunning(running);
            var vdUp = OpenXrRuntimeSelector.IsVirtualDesktopRunning(running);
            var availableRt = OpenXrRuntimeSelector.DiscoverAvailableRuntimes();
            var pick = OpenXrRuntimeSelector.Select(running, availableRt);
            var lockPackages = File.Exists(LockfilePath())
                ? LockfileIo.Read(LockfilePath()).Packages
                : [];

            Console.WriteLine($"VRClient doctor - game '{spec.GameSlug}' (Steam app {spec.SteamAppId})");
            Console.WriteLine($"  game install:     {(gameDir is not null ? $"FOUND      {gameDir}" : offlineLibrary is not null ? $"DISCONNECTED LIBRARY  {offlineLibrary}" : "NOT FOUND  (install through Steam or reconnect its library)")}");
            Console.WriteLine($"  vr mod:           {(modInstalled ? "INSTALLED  (BepInEx loader + VR plugin present)" : "NOT INSTALLED  (run convert)")}");
            Console.WriteLine($"  mod lockfile:     {(lockPackages.Count > 0 ? $"{lockPackages.Count} packages pinned: " + string.Join(", ", lockPackages.Select(p => $"{p.Name} {p.Version}")) : "none  (run resolve)")}");
            Console.WriteLine($"  safety verdict:   {verdict.Verdict} ({verdict.ReasonCode})");
            Console.WriteLine($"  SteamVR:          {(steamVrUp ? "RUNNING" : "not running")}");
            Console.WriteLine($"  Virtual Desktop:  {(vdUp ? "RUNNING" : "not running")}");
            Console.WriteLine($"  runtimes found:   {(availableRt.Count > 0 ? string.Join(", ", availableRt.Select(r => r.Name)) : "none")}");
            Console.WriteLine($"  launch would use: {pick?.Name ?? "system default runtime"}");
            Console.WriteLine($"RESULT: doctor game={(gameDir is not null ? "found" : offlineLibrary is not null ? "offline" : "missing")} mod={(modInstalled ? "installed" : "missing")} steamvr={(steamVrUp ? "running" : "stopped")} vd={(vdUp ? "running" : "stopped")} xr={pick?.Name ?? "system-default"}");
            return 0;
        }
        case "status":
        {
            var installed = new ModInstaller().IsInstalled(RequireGameDir());
            Console.WriteLine($"RESULT: installed={installed.ToString().ToLowerInvariant()}");
            return 0;
        }
        case "safety":
        {
            var verdict = EvaluateSafety();
            // Non-blocking native rules-level cross-check (M5); the .NET gate above stays
            // authoritative. `mode` = the game's online_risk; unavailable when no native CLI.
            var gameCfg = Path.Combine(repoRoot, "config", "games", $"{slug}.json");
            var rulesPath = Path.Combine(repoRoot, "config", "safety", "default-rules.json");
            var mode = "offline";
            if (File.Exists(gameCfg))
            {
                using var gd = JsonDocument.Parse(File.ReadAllText(gameCfg));
                if (gd.RootElement.TryGetProperty("support_policy", out var sp) &&
                    sp.TryGetProperty("online_risk", out var orisk) && orisk.ValueKind == JsonValueKind.String)
                    mode = orisk.GetString()!;
            }
            var xcheck = NativeSafetyCrossCheck.Run(NativeSafetyCrossCheck.LocateCli(), gameCfg, rulesPath, mode);
            var crosscheck = xcheck.Available
                ? $"available:permitted={xcheck.Permitted.ToString().ToLowerInvariant()}"
                : "unavailable";
            Console.WriteLine($"RESULT: verdict={verdict.Verdict} reason={verdict.ReasonCode} crosscheck={crosscheck}");
            return verdict.Verdict is Verdict.Allow or Verdict.Warn ? 0 : 3;
        }
        case "resolve":
        {
            using var http = NewHttpClient();
            var lockfile = await LiveResolveAndWriteLockfileAsync(http);
            Console.WriteLine($"RESULT: locked={lockfile.Packages.Count} lockfile=config/modpacks/{slug}.lock.json");
            return 0;
        }
        case "verify":
        {
            var lockfile = LockfileIo.Read(LockfilePath());
            using var http = NewHttpClient();
            var verified = await DownloadVerifiedAsync(http, lockfile);
            Console.WriteLine($"RESULT: verified={verified.Count}");
            return 0;
        }
        case "install":
        {
            var gameDir = RequireGameDir();
            if (RefuseUnlessSafe(EvaluateSafety()) is int refusal)
                return refusal;
            var lockfile = LockfileIo.Read(LockfilePath());
            LockfileIo.RequireHashesFilled(lockfile);
            using var http = NewHttpClient();
            var zips = await DownloadVerifiedAsync(http, lockfile);
            if (RefuseUnsignedPacks(zips) is int packRefusal)
                return packRefusal;
            var result = new ModInstaller().Install(zips, gameDir, ExecutableNames());
            if (!result.Installed)
            {
                Console.WriteLine($"RESULT: error={result.RefusalReason}");
                return 1;
            }
            Console.WriteLine($"RESULT: installed={lockfile.Packages.Count} files={result.InstalledFiles.Count}");
            return 0;
        }
        case "configure":
        {
            var (applied, unmatched) = Configure(RequireGameDir());
            Console.WriteLine($"RESULT: applied={applied} unmatched={unmatched}");
            return 0;
        }
        case "verify-comfort-map":
        {
            var gameDir = RequireGameDir();
            var mapping = ComfortConfigMapper.LoadMap(ComfortMapPath());
            using var mapDoc = JsonDocument.Parse(File.ReadAllText(ComfortMapPath()));
            var cfgRelative = mapDoc.RootElement.GetProperty("cfg_file").GetString()!;
            var cfgPath = Path.Combine(gameDir, cfgRelative);
            if (!File.Exists(cfgPath))
            {
                Console.WriteLine($"RESULT: error=cfg-not-found ({cfgRelative} appears after the first modded launch)");
                return 1;
            }
            var available = ParseCfgSectionKeys(File.ReadAllText(cfgPath));
            var matched = 0;
            var suggested = new Dictionary<string, string>();
            foreach (var (field, target) in mapping.CfgKeyByProfileField)
            {
                if (available.Contains(target))
                {
                    matched++;
                    suggested[field] = target;
                    Console.WriteLine($"matched: {field} -> {target}");
                }
                else
                {
                    var key = target[(target.IndexOf('/') + 1)..];
                    var candidate = available.FirstOrDefault(
                        a => a[(a.IndexOf('/') + 1)..].Contains(key, StringComparison.OrdinalIgnoreCase) ||
                             key.Contains(a[(a.IndexOf('/') + 1)..], StringComparison.OrdinalIgnoreCase));
                    suggested[field] = candidate ?? target;
                    Console.WriteLine($"unmatched: {field} -> {target}" +
                                      (candidate is null ? "" : $" (suggest: {candidate})"));
                }
            }
            Console.WriteLine("suggested corrected map:");
            Console.WriteLine(JsonSerializer.Serialize(suggested, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine($"RESULT: matched={matched} unmatched={mapping.CfgKeyByProfileField.Count - matched}");
            return 0;
        }
        case "convert":
        {
            var gameDir = RequireGameDir();
            if (RefuseUnlessSafe(EvaluateSafety()) is int refusal)
                return refusal;
            using var http = NewHttpClient();
            var lockfile = File.Exists(LockfilePath())
                ? LockfileIo.Read(LockfilePath())
                : await LiveResolveAndWriteLockfileAsync(http);
            LockfileIo.RequireHashesFilled(lockfile);
            var zips = await DownloadVerifiedAsync(http, lockfile);
            if (RefuseUnsignedPacks(zips) is int packRefusal)
                return packRefusal;
            var result = new ModInstaller().Install(zips, gameDir, ExecutableNames());
            if (!result.Installed)
            {
                Console.WriteLine($"RESULT: error={result.RefusalReason}");
                return 1;
            }
            Configure(gameDir);
            // Per-game opt-in (user directive 2026-07-10): converting THIS game from
            // the library also installs its Steam launch option - exactly one app id,
            // guarded numeric, sourced from this slug's own modpack. Best-effort: a
            // launch-option failure never fails the conversion.
            if (!HasFlag("--no-launch-option"))
            {
                var launchOptionResult = SteamLaunchOptionInstaller.Install(
                    ModpackResolver.LoadSpec(ModpackPath()).SteamAppId,
                    Environment.ProcessPath ?? "vrclient",
                    remove: false, dryRun: false,
                    line => Console.WriteLine($"WRAP-INSTALL: {line}"));
                Console.WriteLine($"WRAP-INSTALL: {(launchOptionResult.Ok ? "ok" : "warning")} {launchOptionResult.Message}");
            }
            Console.WriteLine($"RESULT: converted={slug}");
            return 0;
        }
        case "launch":
        {
            var gameDir = RequireGameDir();
            var verdict = EvaluateSafety();
            var launcher = new GameLauncher();
            var launchExe = ExecutableNames()?.FirstOrDefault();
            var plan = launcher.Plan(gameDir, verdict, new ModInstaller().IsInstalled(gameDir), launchExe);
            var dryRun = HasFlag("--dry-run");
            var xrRuntime = SelectXrRuntime();
            // RepoXR's bundled OpenXR loader runs its own runtime enumeration and does not
            // honor XR_RUNTIME_JSON; it DOES honor its own [Internal] OpenXRRuntimeFile cfg
            // setting, so pin the decision there too (fresh on every launch; empty = let the
            // mod pick). Skipped on --dry-run: dry runs must not mutate anything.
            if (!dryRun)
            {
                using var launchMapDoc = JsonDocument.Parse(File.ReadAllText(ComfortMapPath()));
                var modCfgPath = Path.Combine(gameDir, launchMapDoc.RootElement.GetProperty("cfg_file").GetString()!);
                if (File.Exists(modCfgPath) && !ModRuntimePin.Apply(modCfgPath, xrRuntime?.JsonPath))
                    Console.WriteLine("XR: warning=mod cfg has no Internal/OpenXRRuntimeFile key; runtime pin skipped");
            }
            // M1.4 (2026-07-09): source-verified (docs/troubleshooting/repoxr-runtime-selection.md)
            // that RepoXR honors [Internal] OpenXRRuntimeFile FIRST; exp-cfgpin proved the pin
            // flips the attempt order to SteamVR-first. The pin above is the fix (Branch A).
            if (OpenXrRuntimeSelector.IsSteamVrRunning(OpenXrRuntimeSelector.GetRunningVrProcessNames()))
                Console.WriteLine($"XR: pin=applied runtime={xrRuntime?.Name ?? "system-default"}");
            var pid = launcher.Launch(plan, HasFlag("--acknowledge"), dryRun, xrRuntime);
            if (!dryRun)
            {
                var lockSha = File.Exists(LockfilePath()) ? Hashing.Sha256OfFile(LockfilePath()) : "";
                launcher.WriteEvidence(Path.Combine(repoRoot, "artifacts"), new SessionEvidence(
                    "repo", lockSha, DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
                    UserConfirmedVrStereo: false, UserConfirmedHeadTracking: false,
                    Notes: "launched via vrclient; VR stereo/head-tracking confirmation pending (see Milestone 1 headset protocol)"));
            }
            Console.WriteLine($"RESULT: launched pid={pid} xr={xrRuntime?.Name ?? "system-default"}");
            return 0;
        }
        case "uninstall":
        {
            new ModInstaller().Uninstall(RequireGameDir());
            // Symmetric with convert: uninstalling also removes THIS game's Steam
            // launch option (best-effort; --keep-launch-option to skip).
            if (!HasFlag("--keep-launch-option"))
            {
                var launchOptionResult = SteamLaunchOptionInstaller.Install(
                    ModpackResolver.LoadSpec(ModpackPath()).SteamAppId,
                    Environment.ProcessPath ?? "vrclient",
                    remove: true, dryRun: false,
                    line => Console.WriteLine($"WRAP-INSTALL: {line}"));
                Console.WriteLine($"WRAP-INSTALL: {(launchOptionResult.Ok ? "ok" : "warning")} {launchOptionResult.Message}");
            }
            Console.WriteLine("RESULT: uninstalled");
            return 0;
        }
        case "uevr-prepare":
        {
            var gamePath = Path.Combine(repoRoot, "config", "unreal", $"{slug}.uevr.json");
            if (!File.Exists(gamePath))
            {
                Console.WriteLine($"RESULT: error=uevr_game_not_in_catalog - no config/unreal/{slug}.uevr.json");
                return 1;
            }
            var game = UevrTools.LoadGame(gamePath);
            var pin = UevrTools.LoadReleasePin(
                Path.Combine(repoRoot, "config", "unreal", "uevr-release.json"), game.Channel);
            var dotNetPin = UevrTools.LoadDotNetDesktopPin(
                Path.Combine(repoRoot, "config", "unreal", "dotnet-desktop-runtime.json"));
            using var prepareHttp = NewHttpClient();
            var prepareDownloader = new CliHttpDownloader(prepareHttp);
            Console.WriteLine($"UEVR-PREPARE: fetching verified UEVR {pin.Tag} and app-local .NET Desktop {dotNetPin.Version} as needed...");
            await Task.WhenAll(
                UevrTools.EnsureFetchedAsync(pin, prepareDownloader, HasFlag("--force")),
                UevrTools.EnsureDotNetFetchedAsync(dotNetPin, prepareDownloader, HasFlag("--force")));
            Console.WriteLine($"RESULT: uevr-prepare ok game={game.Slug} uevr={pin.Tag} dotnet={dotNetPin.Version}");
            return 0;
        }
        case "uevr-fetch":
        {
            // ECO-2: download the pinned UEVR release (stable or nightly) from the
            // OFFICIAL praydog GitHub release, verify against our pinned sha256,
            // unpack to the user-local tools dir. License constraint: never bundled
            // or redistributed - this runs on the user's machine only.
            var channel = GetOption("--channel") ?? "stable";
            var pin = UevrTools.LoadReleasePin(
                Path.Combine(repoRoot, "config", "unreal", "uevr-release.json"), channel);
            using (var uevrHttp = NewHttpClient())
            {
                await UevrTools.EnsureFetchedAsync(
                    pin, new CliHttpDownloader(uevrHttp), HasFlag("--force"));
                Console.WriteLine($"RESULT: uevr-fetch ok tag={pin.Tag} sha256=verified dir={UevrTools.ToolsDir(pin.Tag)}");
            }
            return 0;
        }
        case "uevr-launch":
        {
            // ECO-2: T-Unreal launch flow - safety gate -> ensure UEVR fetched ->
            // start the game via Steam -> wait for the SHIPPING process (not the
            // launcher stub) -> attach the UEVR injector to it.
            var uevrGamePath = Path.Combine(repoRoot, "config", "unreal", $"{slug}.uevr.json");
            if (!File.Exists(uevrGamePath))
            {
                Console.WriteLine($"RESULT: error=uevr_game_not_in_catalog - no config/unreal/{slug}.uevr.json");
                return 1;
            }
            var uevrGame = UevrTools.LoadGame(uevrGamePath);
            var uevrVerdict = new SafetyGate().Evaluate(uevrGamePath,
                Path.Combine(repoRoot, "config", "safety", "default-rules.json"));
            if (RefuseUnlessSafe(uevrVerdict) is int uevrRefusal)
                return uevrRefusal;

            var uevrPin = UevrTools.LoadReleasePin(
                Path.Combine(repoRoot, "config", "unreal", "uevr-release.json"), uevrGame.Channel);
            var dotNetPin = UevrTools.LoadDotNetDesktopPin(
                Path.Combine(repoRoot, "config", "unreal", "dotnet-desktop-runtime.json"));
            var attachProcess = UevrTools.ShippingProcessName(uevrGame);
            var attachTimeout = int.TryParse(GetOption("--attach-timeout"), out var at) ? at : 120;
            var attachDelay = int.TryParse(GetOption("--attach-delay"), out var ad) ? ad : 10;

            if (HasFlag("--dry-run"))
            {
                Console.WriteLine($"RESULT: uevr-launch-dry-run game={uevrGame.Slug} app={uevrGame.SteamAppId} channel={uevrPin.Channel} tag={uevrPin.Tag} fetched={UevrTools.IsFetched(uevrPin)} attach={attachProcess} injector={UevrTools.InjectorPath(uevrPin)}");
                return 0;
            }

            using (var fetchHttp = NewHttpClient())
            {
                var fetcher = new CliHttpDownloader(fetchHttp);
                await Task.WhenAll(
                    UevrTools.EnsureFetchedAsync(uevrPin, fetcher),
                    UevrTools.EnsureDotNetFetchedAsync(dotNetPin, fetcher));
            }

            // Deploy the CATALOG UEVR profile (first-person camera, controller aim,
            // scripts) into UEVR's per-game dir. Copy-if-missing by default so a
            // user's in-headset tuning is never clobbered; --push-profile forces the
            // catalog version.
            var catalogProfile = Path.Combine(repoRoot, "config", "unreal", "profiles", uevrGame.Slug);
            var uevrGameProfileDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "UnrealVRMod", attachProcess);
            if (Directory.Exists(catalogProfile) &&
                (!Directory.Exists(uevrGameProfileDir) || HasFlag("--push-profile")))
            {
                CopyDirectoryRecursive(catalogProfile, uevrGameProfileDir);
                Console.WriteLine($"UEVR-LAUNCH: catalog profile deployed -> {uevrGameProfileDir}");
            }

            var controllerGuide = ControllerReferenceCatalog.Load(repoRoot, uevrGame.Slug);
            if (controllerGuide is not null)
                UevrControllerGuide.Install(controllerGuide, uevrGameProfileDir);

            // The RV hand driver is opt-in and must be installed only after
            // first-launch profile deployment, so its data directory cannot
            // accidentally make the profile appear already installed.
            if (uevrGame.Slug == "rv-there-yet")
                new RvTrackedHands(profileDirectory: uevrGameProfileDir).PrepareForLaunch();

            // Steam is found the same way as every other launch (registry first), so a
            // Steam installed outside Program Files works too.
            var steamRoot = GetOption("--steam-root");
            if (SteamOwnedLauncher.FindSteamExecutable(steamRoot) is null)
                throw new InvalidOperationException("steam_not_found");
            // Pre-flight: UEVR's backend can only ENGAGE when a VR runtime session is
            // live - inject without one and it "connects" but never renders VR (the
            // #1 cause of "it launched but no headset"). Fail fast with a clear ask,
            // unless the user forces past it.
            var vrProcs = OpenXrRuntimeSelector.GetRunningVrProcessNames();
            var requestedRuntime = (GetOption("--runtime") ?? string.Empty).ToLowerInvariant() switch
            {
                "vdxr" or "virtualdesktopxr" => OpenXrRuntimeSelector.VirtualDesktopName,
                "steamvr" => OpenXrRuntimeSelector.SteamVrName,
                "" => null,
                var invalid => throw new ArgumentException($"unknown UEVR runtime '{invalid}'; use vdxr or steamvr")
            };
            var runtimeStatus = OpenXrRuntimeSelector.EvaluateForUevr(
                vrProcs, OpenXrRuntimeSelector.DiscoverAvailableRuntimes(), requestedRuntime);
            Console.WriteLine($"UEVR-LAUNCH: runtime={runtimeStatus.SelectedRuntime ?? "none"} state={runtimeStatus.State} detail={runtimeStatus.Detail}");
            if (!runtimeStatus.Ready && !HasFlag("--no-vr-check"))
            {
                Console.WriteLine($"RESULT: error=uevr_runtime_not_ready state={runtimeStatus.State} - {runtimeStatus.Detail} (--no-vr-check to override.)");
                return 2;
            }

            Console.WriteLine($"UEVR-LAUNCH: starting app {uevrGame.SteamAppId} via Steam...");
            var steamStart = new SteamOwnedLauncher().Launch(uevrGame.SteamAppId, steamRoot);
            if (!steamStart.Success)
            {
                Console.WriteLine($"RESULT: error=uevr_steam_launch_failed - {steamStart.FailureText}");
                return 1;
            }

            Console.WriteLine($"UEVR-LAUNCH: waiting up to {attachTimeout}s for process '{attachProcess}'...");
            System.Diagnostics.Process? gameProc = null;
            for (var waited = 0; waited < attachTimeout; waited++)
            {
                var found = System.Diagnostics.Process.GetProcessesByName(attachProcess);
                if (found.Length > 0) { gameProc = found[0]; break; }
                Thread.Sleep(1000);
            }
            if (gameProc is null)
            {
                Console.WriteLine($"RESULT: error=uevr_game_process_not_found - '{attachProcess}' did not appear within {attachTimeout}s");
                return 1;
            }
            // The game process blocks module enumeration from a non-elevated caller
            // (verified: 0 modules readable). UEVR's injector uses that same module
            // read to decide a process is injectable AND to inject - so it MUST run
            // elevated or it silently never hooks (root cause of "opens but doesn't
            // hook"). We can't read the game's D3D either, so gate readiness on window
            // title + a settle, not modules.
            Console.WriteLine($"UEVR-LAUNCH: game pid={gameProc.Id}; waiting for window + settle...");
            var minSettle = Math.Max(6, attachDelay);
            for (var waited = 0; waited < attachTimeout; waited++)
            {
                gameProc.Refresh();
                if (gameProc.HasExited) break;
                if (!string.IsNullOrEmpty(gameProc.MainWindowTitle) && waited >= minSettle) break;
                Thread.Sleep(1000);
            }

            // Success signal: a completed hook makes UEVR's frontend flood config.txt
            // with its OWN keys (Frontend_RequestedRuntime + engine defaults). Our seed
            // is small; a real hook makes the file much larger. Baseline the size now so
            // growth = hooked.
            var uevrConfigTxt = Path.Combine(uevrGameProfileDir, "config.txt");
            static UevrConfigSnapshot ConfigSnapshot(string path)
            {
                if (!File.Exists(path)) return new UevrConfigSnapshot(0, DateTime.MinValue, false);
                var info = new FileInfo(path);
                var text = File.ReadAllText(path);
                return new UevrConfigSnapshot(info.Length, info.LastWriteTimeUtc,
                    text.Contains("Frontend_RequestedRuntime", StringComparison.OrdinalIgnoreCase));
            }
            var beforeConfig = ConfigSnapshot(uevrConfigTxt);

            // Launch the injector NON-ELEVATED - the game runs at normal integrity
            // (verified), so injection needs no admin, and the proven-working manual
            // path was never elevated. Elevation (reverted 2026-07-11) added a UAC
            // prompt, left unkillable orphans, and an elevated-injector-into-normal-game
            // mismatch that connected but never fully engaged.
            Console.WriteLine("UEVR-LAUNCH: launching UEVR injector (auto-attach)...");
            var injectorPsi = new System.Diagnostics.ProcessStartInfo
            {
                FileName = UevrTools.InjectorPath(uevrPin),
                WorkingDirectory = UevrTools.ToolsDir(uevrPin.Tag),
                Arguments = $"--attach={attachProcess}.exe",
                UseShellExecute = false,
            };
            // UEVRInjector targets .NET 6. Fetch a supported app-local .NET 8
            // Desktop runtime and opt into Microsoft's documented major-version
            // roll-forward instead of installing end-of-life .NET 6 system-wide.
            injectorPsi.Environment["DOTNET_ROOT"] = UevrTools.DotNetToolsDir(dotNetPin);
            injectorPsi.Environment["DOTNET_ROOT_X64"] = UevrTools.DotNetToolsDir(dotNetPin);
            injectorPsi.Environment["DOTNET_ROLL_FORWARD"] = "Major";
            var injectorProc = System.Diagnostics.Process.Start(injectorPsi);

            // Verify the hook landed: config.txt grows past its seeded size OR gains the
            // frontend runtime key. (Frontend writes these right after a successful
            // InjectDll, before the in-headset render handshake.)
            Console.WriteLine("UEVR-LAUNCH: verifying the VR backend hooked the game...");
            var injected = false;
            for (var waited = 0; waited < 45; waited++)
            {
                gameProc.Refresh();
                if (gameProc.HasExited) break;
                if (File.Exists(uevrConfigTxt))
                {
                    if (UevrAttachEvidence.IsFreshHook(beforeConfig, ConfigSnapshot(uevrConfigTxt)))
                    { injected = true; break; }
                }
                Thread.Sleep(1000);
            }
            if (injected)
            {
                // Keep the injector available for troubleshooting, but let the
                // game own the desktop once the backend has demonstrably hooked.
                // Never minimize on a failed or still-pending attachment.
                if (UevrInjectorWindow.TryMinimize(injectorProc))
                    Console.WriteLine("UEVR-LAUNCH: injector minimized after successful attach (still running)");
                Console.WriteLine($"UEVR-LAUNCH: switch to the headset - VR is active");
                Console.WriteLine($"RESULT: uevr-launch ok game={uevrGame.Slug} pid={gameProc.Id} attach={attachProcess}.exe tag={uevrPin.Tag} backend=hooked");
                return 0;
            }
            // No config handshake yet - but the handshake only completes once the game
            // renders a VR frame in-headset. If the injector is resident and the game is
            // alive, injection has landed and is waiting on the headset - not a failure.
            gameProc.Refresh();
            var injectorResident = System.Diagnostics.Process.GetProcessesByName("UEVRInjector").Length > 0;
            if (!gameProc.HasExited && injectorResident)
            {
                Console.WriteLine("UEVR-LAUNCH: injector attached and resident - PUT THE HEADSET ON NOW; VR engages when the game renders in-headset.");
                Console.WriteLine("UEVR-LAUNCH: (a 'SnippingTool/other is not an Unreal Engine title' popup from UEVR is harmless - just close it.)");
                Console.WriteLine($"RESULT: uevr-launch injected game={uevrGame.Slug} pid={gameProc.Id} attach={attachProcess}.exe tag={uevrPin.Tag} backend=pending-headset");
                return 0;
            }
            Console.WriteLine($"RESULT: error=uevr_backend_not_loaded game={uevrGame.Slug} pid={gameProc.Id} - the injector is not resident and no hook signal appeared. Rerun with the headset connected; if a UAC prompt appeared, approve it.");
            return 5;
        }
        case "uevr-profile-import":
        {
            var importGamePath = Path.Combine(repoRoot, "config", "unreal", $"{slug}.uevr.json");
            var archive = GetOption("--archive");
            if (!File.Exists(importGamePath) || string.IsNullOrWhiteSpace(archive))
            {
                Console.WriteLine("RESULT: error=uevr_profile_import_usage - use uevr-profile-import <slug> --archive <reviewed-profile.zip>");
                return 1;
            }
            var importGame = UevrTools.LoadGame(importGamePath);
            var importDestination = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "UnrealVRMod", UevrTools.ShippingProcessName(importGame));
            var imported = UevrTools.ImportProfileArchive(importGame, archive, importDestination);
            Console.WriteLine($"RESULT: uevr-profile-import ok game={slug} files={imported.FileCount} destination={imported.Destination} backup={imported.BackupDirectory ?? "none"}");
            return 0;
        }
        case "uevr-profile-rollback":
        {
            var rollbackGamePath = Path.Combine(repoRoot, "config", "unreal", $"{slug}.uevr.json");
            if (!File.Exists(rollbackGamePath))
            {
                Console.WriteLine($"RESULT: error=uevr_game_not_in_catalog - no config/unreal/{slug}.uevr.json");
                return 1;
            }
            var rollbackGame = UevrTools.LoadGame(rollbackGamePath);
            var rollbackDestination = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "UnrealVRMod", UevrTools.ShippingProcessName(rollbackGame));
            var restored = UevrTools.RollbackImportedProfile(rollbackDestination);
            Console.WriteLine($"RESULT: uevr-profile-rollback ok game={slug} restored={restored} destination={rollbackDestination}");
            return 0;
        }
        case "uevr-profile-export":
        {
            // Capture the user's in-headset UEVR tuning (camera/first-person, aim
            // method, UObjectHook attachments, Lua scripts) from UEVR's per-game
            // dir back into the catalog, where uevr-launch deploys it from - the
            // tune-once, ship-as-data loop.
            var exportGamePath = Path.Combine(repoRoot, "config", "unreal", $"{slug}.uevr.json");
            if (!File.Exists(exportGamePath))
            {
                Console.WriteLine($"RESULT: error=uevr_game_not_in_catalog - no config/unreal/{slug}.uevr.json");
                return 1;
            }
            var exportGame = UevrTools.LoadGame(exportGamePath);
            var exportProc = UevrTools.ShippingProcessName(exportGame);
            var exportSrc = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "UnrealVRMod", exportProc);
            if (!Directory.Exists(exportSrc))
            {
                Console.WriteLine($"RESULT: error=uevr_profile_not_found - {exportSrc} does not exist (tune in-headset first)");
                return 1;
            }
            var exportDst = Path.Combine(repoRoot, "config", "unreal", "profiles", slug);
            CopyDirectoryRecursive(exportSrc, exportDst);
            var fileCount = Directory.GetFiles(exportDst, "*", SearchOption.AllDirectories).Length;
            Console.WriteLine($"RESULT: uevr-profile-export ok game={slug} files={fileCount} from={exportSrc} to={exportDst}");
            return 0;
        }
        case "nexus-test":
        {
            // ECO-1 feasibility harness: proves the Nexus ecosystem works with this
            // client. READ-ONLY against the official API; the key comes from
            // NEXUS_API_KEY or %LOCALAPPDATA%\vrclient\nexus-api-key.txt - never the repo.
            var apiKey = NexusApi.ResolveApiKey();
            if (apiKey is null)
            {
                Console.WriteLine("RESULT: error=nexus_key_missing - set NEXUS_API_KEY or %LOCALAPPDATA%\\vrclient\\nexus-api-key.txt");
                return 1;
            }
            var domain = GetOption("--domain") ?? "fallout4";
            using var nexusHttp = NewHttpClient();
            var nexus = new NexusApi(nexusHttp, apiKey);

            var user = await nexus.ValidateAsync();
            Console.WriteLine($"NEXUS: key=valid user={user.Name} premium={user.IsPremium}");
            var game = await nexus.GetGameAsync(domain);
            Console.WriteLine($"NEXUS: game={game.Name} domain={game.DomainName} id={game.Id}");

            if (GetOption("--mod-id") is { } modIdText && long.TryParse(modIdText, out var modId))
            {
                var files = await nexus.GetModFilesAsync(domain, modId);
                Console.WriteLine($"NEXUS: mod={modId} files={files.Count}");
                foreach (var f in files.Take(5))
                    Console.WriteLine($"  file_id={f.FileId} v={f.Version} size_kb={f.SizeKb} md5={f.Md5 ?? "(none)"} name={f.FileName}");
                if (files.Count > 0)
                {
                    var probe = await nexus.ProbeDownloadLinkAsync(domain, modId, files[0].FileId);
                    Console.WriteLine($"NEXUS: download-link {(probe.Allowed ? "ALLOWED" : "gated")} - {probe.Detail}");
                }
            }
            Console.WriteLine("RESULT: nexus-test ok");
            return 0;
        }
        case "wrap-install":
        {
            // LI-1b: install (or --remove) the wrap launch option into Steam itself, so
            // the user never edits Steam settings by hand. Steam rewrites localconfig.vdf
            // from memory on exit, so a running Steam is shut down first (and relaunched
            // after). A timestamped backup is taken before every edit.
            var appId = ModpackResolver.LoadSpec(ModpackPath()).SteamAppId;
            var installResult = SteamLaunchOptionInstaller.Install(
                appId,
                Environment.ProcessPath
                    ?? throw new InvalidOperationException("cannot determine the vrclient executable path"),
                remove: HasFlag("--remove"),
                dryRun: HasFlag("--dry-run"),
                line => Console.WriteLine($"WRAP-INSTALL: {line}"),
                GetOption("--steam-root"));
            Console.WriteLine($"RESULT: {(installResult.Ok ? "wrap-install" : "error=wrap-install")} app={appId} {installResult.Message}");
            return installResult.Ok ? 0 : 1;
        }
        case "wrap":
        {
            // LI-1 Steam launch-option wrapper. Paste into Steam > Properties > Launch Options:
            //   "<vrclient.exe>" wrap -- %command%
            // FAIL-OPEN CONTRACT: any wrapper-internal error still launches the original
            // command unchanged; the wrapper must never block a flat launch. It waits on
            // the game process because Steam derives running-state/playtime from it.
            var sep = Array.IndexOf(args, "--");
            if (sep < 0 || sep == args.Length - 1)
            {
                var self = Environment.ProcessPath ?? "vrclient";
                Console.WriteLine("RESULT: error=wrap-usage (expected: wrap [--vr on|off|auto] [--dry-run] -- <game command>)");
                Console.WriteLine($"Steam launch option: \"{self}\" wrap -- %command%");
                return 1;
            }
            var wrapCommand = args[(sep + 1)..];
            var pre = args[..sep];
            // Wrap-local option parsing: only tokens BEFORE the -- separator are ours;
            // everything after belongs to the game (mod-manager doorstop args included).
            string? PreOption(string name)
            {
                for (var i = 1; i < pre.Length - 1; i++)
                    if (pre[i] == name)
                        return pre[i + 1];
                return null;
            }
            var wrapDryRun = pre.Contains("--dry-run");
            var wrapMode = WrapPlanner.ParseMode(PreOption("--vr"));

            // Telemetry: Steam swallows the wrapper's console output, so every launch
            // also appends to %LOCALAPPDATA%\vrclient\wrap.log (fail-open, best effort).
            void WrapLog(string line)
            {
                try
                {
                    var dir = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "vrclient");
                    Directory.CreateDirectory(dir);
                    File.AppendAllText(Path.Combine(dir, "wrap.log"),
                        $"{DateTime.UtcNow:yyyy-MM-ddTHH:mm:ssZ} {line}{Environment.NewLine}");
                }
                catch { /* telemetry must never break a launch */ }
            }

            WrapPlan wrapPlan;
            try
            {
                wrapPlan = WrapPlanner.Plan(wrapCommand, wrapMode,
                    OpenXrRuntimeSelector.GetRunningVrProcessNames(),
                    OpenXrRuntimeSelector.DiscoverAvailableRuntimes());

                // Per-launch runtime pin (M1.4 Branch A), data-driven per game: match the
                // wrapped exe against the catalog's executable_names for the cfg location.
                // LI-3: when the command carries a mod-manager doorstop redirect
                // (r2modman/TMM/Gale profile), BepInEx loads from THAT profile, so the pin
                // goes into the profile's cfg — created if the mod hasn't written it yet.
                // No catalog match (or unconverted game dir with no doorstop) = silent
                // passthrough.
                var gameDir = Path.GetDirectoryName(Path.GetFullPath(wrapCommand[0]));
                var profileRoot = DoorstopProfile.ProfileRootFromCommand(wrapCommand);
                var wrapMatch = FindWrapCfgRelative(Path.GetFileName(wrapCommand[0]));
                string? wrapCfg = null;
                if (!DoorstopProfile.IsExplicitlyDisabled(wrapCommand) &&
                    wrapMatch is { } match && (profileRoot ?? gameDir) is { } pinRoot)
                {
                    var candidate = Path.Combine(pinRoot, match.CfgRelative);
                    if (File.Exists(candidate))
                        wrapCfg = candidate;
                    else if (profileRoot is not null && !wrapPlan.FlatMode && wrapPlan.PinRuntimeJsonPath is not null)
                    {
                        // Opt-in gate: only touch a profile whose owner installed the VR
                        // mod there themselves (via the mod manager). First VR run of such
                        // a profile has no cfg yet; BepInEx merges the orphaned entry.
                        var modFolders = ModpackResolver
                            .LoadSpec(Path.Combine(repoRoot, "config", "modpacks", $"{match.Slug}.modpack.json"))
                            .Mods.Select(m => $"{m.Namespace}-{m.Name}");
                        if (DoorstopProfile.HasVrMod(profileRoot, modFolders))
                        {
                            if (!wrapDryRun)
                            {
                                Directory.CreateDirectory(Path.GetDirectoryName(candidate)!);
                                File.WriteAllText(candidate, DoorstopProfile.MinimalPinCfg(wrapPlan.PinRuntimeJsonPath));
                            }
                            wrapCfg = candidate;
                        }
                        else
                            WrapLog("note=profile has no VR mod installed; profile left untouched (install the VR mod via the mod manager to opt in)");
                    }
                }
                else if (profileRoot is not null)
                    WrapLog("note=doorstop profile detected but game not in catalog; no pin");

                if (wrapCfg is not null && !wrapDryRun)
                    ModRuntimePin.Apply(wrapCfg, wrapPlan.FlatMode ? null : wrapPlan.PinRuntimeJsonPath);
                var summary = $"mode={wrapMode} flat={wrapPlan.FlatMode} pin={wrapPlan.PinRuntimeName ?? "none"} " +
                    $"cfg={wrapCfg ?? "none"} doorstop={(profileRoot is null ? "none" : profileRoot)} reason={wrapPlan.Reason}";
                Console.WriteLine($"WRAP: {summary}");
                WrapLog($"{summary} cmd={string.Join(' ', wrapCommand)}");
                if (profileRoot is not null)
                    WrapLog("note=profile-provided mods are NOT hash-verified by the safety engine yet (LI-3 trust boundary, see docs/roadmap/launch-integration.md)");
            }
            catch (Exception ex)
            {
                wrapPlan = new WrapPlan(wrapCommand, null, null, false,
                    "wrapper-error (fail-open passthrough)");
                Console.WriteLine($"WRAP: warning=fail-open error={ex.Message.ReplaceLineEndings(" ")}");
                WrapLog($"fail-open error={ex.Message.ReplaceLineEndings(" ")} cmd={string.Join(' ', wrapCommand)}");
            }

            if (wrapDryRun)
            {
                Console.WriteLine($"RESULT: wrap-dry-run command={string.Join(' ', wrapPlan.Command)}");
                return 0;
            }

            var psi = new System.Diagnostics.ProcessStartInfo
            {
                FileName = wrapPlan.Command[0],
                UseShellExecute = false,
            };
            foreach (var token in wrapPlan.Command.Skip(1))
                psi.ArgumentList.Add(token);
            try
            {
                if (Path.GetDirectoryName(Path.GetFullPath(wrapPlan.Command[0])) is { Length: > 0 } workDir)
                    psi.WorkingDirectory = workDir;
            }
            catch { /* fail-open: an unparseable path still gets handed to Process.Start */ }
            using (var gameProcess = System.Diagnostics.Process.Start(psi))
            {
                if (gameProcess is null)
                {
                    Console.WriteLine("RESULT: error=wrap-spawn-failed process did not start");
                    return 1;
                }
                gameProcess.WaitForExit();
                Console.WriteLine($"RESULT: wrapped exit={gameProcess.ExitCode}");
                WrapLog($"exit={gameProcess.ExitCode}");
                return gameProcess.ExitCode;
            }
        }
        default:
            Console.WriteLine($"RESULT: error=unknown-verb:{args[0]}");
            return 1;
    }
}
catch (Exception ex)
{
    Console.WriteLine($"RESULT: error={ex.Message.ReplaceLineEndings(" ")}");
    return 1;
}

// LI-1/LI-3: the matched game slug + RELATIVE mod-cfg path the wrapper should pin,
// data-driven per game (M2 contract): exe name -> config/games/*.json
// executable_names -> that slug's comfort-map cfg_file. The caller resolves it
// against the game dir OR a mod-manager profile root (doorstop redirect). Null
// (silent passthrough) when anything is missing.
(string Slug, string CfgRelative)? FindWrapCfgRelative(string exeName)
{
    var gamesDir = Path.Combine(repoRoot, "config", "games");
    if (!Directory.Exists(gamesDir))
        return null;
    foreach (var gameJson in Directory.EnumerateFiles(gamesDir, "*.json"))
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(gameJson));
        if (!doc.RootElement.TryGetProperty("executable_names", out var names) ||
            names.ValueKind != JsonValueKind.Array ||
            !names.EnumerateArray().Any(n => n.ValueKind == JsonValueKind.String &&
                string.Equals(n.GetString(), exeName, StringComparison.OrdinalIgnoreCase)))
            continue;
        var matchedSlug = Path.GetFileNameWithoutExtension(gameJson);
        var comfortMap = Path.Combine(repoRoot, "config", "modpacks", $"{matchedSlug}.comfort-map.json");
        if (!File.Exists(comfortMap))
            return null;
        using var mapDoc = JsonDocument.Parse(File.ReadAllText(comfortMap));
        return mapDoc.RootElement.TryGetProperty("cfg_file", out var cfgFile) &&
               cfgFile.GetString() is { Length: > 0 } relativeCfg
            ? (matchedSlug, relativeCfg)
            : null;
    }
    return null;
}

// UEVR profile deploy/export: plain recursive copy, overwriting destination files.
static void CopyDirectoryRecursive(string sourceDir, string destDir)
{
    Directory.CreateDirectory(destDir);
    foreach (var file in Directory.GetFiles(sourceDir, "*", SearchOption.AllDirectories))
    {
        var relative = Path.GetRelativePath(sourceDir, file);
        var target = Path.Combine(destDir, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Copy(file, target, overwrite: true);
    }
}

static string? FindRepoRoot() => VrClient.Core.RepoRoot.Find();

static HttpClient NewHttpClient()
{
    var http = new HttpClient();
    http.DefaultRequestHeaders.UserAgent.ParseAdd("vrclient/0.1");
    return http;
}

static HashSet<string> ParseCfgSectionKeys(string cfgText)
{
    var pairs = new HashSet<string>(StringComparer.Ordinal);
    string? section = null;
    foreach (var raw in cfgText.Replace("\r\n", "\n").Split('\n'))
    {
        var line = raw.Trim();
        if (line.StartsWith("[", StringComparison.Ordinal) && line.EndsWith("]", StringComparison.Ordinal))
        {
            section = line[1..^1];
            continue;
        }
        if (section is null ||
            line.StartsWith("#", StringComparison.Ordinal) ||
            line.StartsWith(";", StringComparison.Ordinal))
            continue;
        var equalsIndex = line.IndexOf('=');
        if (equalsIndex > 0)
            pairs.Add($"{section}/{line[..equalsIndex].Trim()}");
    }
    return pairs;
}

internal sealed class CliHttpDownloader(HttpClient http) : IHttpDownloader
{
    public async Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
        => await http.GetByteArrayAsync(url, ct);
}

/// Serves each download from a file of the same name in a local folder.
internal sealed class LocalArchiveDownloader(string directory) : IHttpDownloader
{
    public async Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
        => await File.ReadAllBytesAsync(
            Path.Combine(directory, Path.GetFileName(new Uri(url).LocalPath)), ct);
}

internal static class UevrInjectorWindow
{
    private const int SwMinimize = 6;

    [DllImport("user32.dll")]
    private static extern bool ShowWindowAsync(nint window, int command);

    internal static bool TryMinimize(System.Diagnostics.Process? process)
    {
        if (process is null || !OperatingSystem.IsWindows()) return false;
        try
        {
            process.Refresh();
            if (process.HasExited || process.MainWindowHandle == nint.Zero) return false;
            return ShowWindowAsync(process.MainWindowHandle, SwMinimize);
        }
        catch (InvalidOperationException)
        {
            return false; // injector exited before we reached its window
        }
    }
}
