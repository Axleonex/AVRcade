namespace VrClient.Core.App;
using System.Diagnostics;
using System.Text.Json;
using System.Security.Cryptography;
using VrClient.Core.Catalog;
using VrClient.Core.Config;
using VrClient.Core.Discovery;
using VrClient.Core.Launch;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using VrClient.Core.Safety;
using VrClient.Core.Redengine;
using VrClient.Core.Rage;
using VrClient.Core.Unreal;
using VrClient.Core.Legacy;
using VrClient.Core.FalloutNewVegas;
using VrClient.Core.Fallout3;
using VrClient.Core.ModManagers;

public enum GameEngine { Unity, Unreal, Redengine, Rage, LegacyD3D9, FalloutNewVegas, Fallout3 }
public enum GameReadiness
{
    NeedsGame,
    NeedsVrMod,
    NeedsUevr,
    NeedsHeadset,
    ProfileUnverified,
    Ready,
    Blocked
}

public sealed record GameView(
    string Slug, string DisplayName, string SteamAppId,
    string? InstallDir, bool ModInstalled,
    string SafetyVerdict, string SafetyReason,
    GameEngine Engine, GameReadiness Readiness, string ReadinessDetail,
    string? ProfileSourceUrl = null, bool ProfileImportRequired = false,
    bool ProfileImported = false, string? OfflineSteamLibrary = null);

public sealed record RuntimeStatusView(
    bool SteamVrRunning, bool VirtualDesktopRunning, string SelectedRuntime);

public sealed record ActionOutcome(bool Ok, string Message);

/// Locate a Steam game's install dir by app id + executable name; null if not found.
public delegate string? SteamInstallLocator(string appId, string exeName);

/// Headless facade the GUI binds to. No Avalonia dependency — fully unit-testable.
public sealed class AppController
{
    // Keep future Red Dead work in the repository without advertising it in AVRcade yet.
    private const bool RedDeadCatalogEnabled = false;
    // Keep unfinished Fallout integrations available for future development, but out of the client.
    private const bool FalloutCatalogEnabled = false;
    private static readonly HashSet<string> DeferredCatalogSlugs = new(StringComparer.OrdinalIgnoreCase)
    {
        "red-dead-redemption", "red-dead-redemption-1", "red-dead-redemption-2", "rdr1", "rdr2"
    };
    private static readonly HashSet<string> DeferredFalloutCatalogSlugs = new(StringComparer.OrdinalIgnoreCase)
    {
        "fallout-3", "fallout-new-vegas"
    };

    private readonly string _repoRoot;
    public string RepoRoot => _repoRoot;
    private readonly SteamInstallLocator _locator;
    private readonly Func<UevrReleasePin, bool> _uevrFetched;
    private readonly Func<DotNetDesktopPin, bool> _dotNetFetched;
    private readonly Func<bool> _headsetConnected;
    private readonly Func<string?> _gtaSanAndreasLocator;
    private readonly Func<FnvDiscoveryResult> _falloutNewVegasDiscovery;
    private readonly Func<Fallout3DiscoveryResult> _fallout3Discovery;
    private readonly GtaSanAndreasVanillaSettings _gtaVanillaSettings;
    private readonly string? _cyberpunkStateDir;
    private CyberpunkBackendInstaller? _cyberpunkBackend;

    public AppController(
        string repoRoot,
        SteamInstallLocator? locator = null,
        Func<UevrReleasePin, bool>? uevrFetched = null,
        Func<DotNetDesktopPin, bool>? dotNetFetched = null,
        Func<bool>? headsetConnected = null,
        Func<string?>? gtaSanAndreasLocator = null,
        Func<FnvDiscoveryResult>? falloutNewVegasDiscovery = null,
        Func<Fallout3DiscoveryResult>? fallout3Discovery = null,
        GtaSanAndreasVanillaSettings? gtaVanillaSettings = null,
        string? cyberpunkStateDir = null)
    {
        _cyberpunkStateDir = cyberpunkStateDir;
        _repoRoot = repoRoot;
        _locator = locator ?? DefaultLocator;
        _uevrFetched = uevrFetched ?? (pin => UevrTools.IsFetched(pin));
        _dotNetFetched = dotNetFetched ?? (pin => UevrTools.IsDotNetFetched(pin));
        _headsetConnected = headsetConnected ?? DefaultHeadsetConnected;
        _gtaSanAndreasLocator = gtaSanAndreasLocator ?? GtaSanAndreasPreflight.FindDefaultInstall;
        _falloutNewVegasDiscovery = falloutNewVegasDiscovery ?? (() => new FalloutNewVegasDiscovery().Discover());
        _fallout3Discovery = fallout3Discovery ?? (() => new Fallout3Discovery().Discover());
        _gtaVanillaSettings = gtaVanillaSettings ?? new GtaSanAndreasVanillaSettings();
    }

    private static string? DefaultLocator(string appId, string exeName)
        => new SteamLibraryScanner().FindGame(appId, exeName, null)?.InstallDir;

    public ActionOutcome RememberFriendslopGameFolder(string slug, string folder)
    {
        if (!CommunityVrRoutes.All.TryGetValue(slug, out var route))
            return new(false, "Manual Steam-library selection is available for Friendslop games only.");
        var (_, exeName) = ReadGameConfigBasics(slug);
        if (string.IsNullOrWhiteSpace(exeName))
            return new(false, "AVRcade has no executable identity for this game.");
        try
        {
            return new SteamLibraryScanner().RememberGameFolder(route.AppId, exeName, folder)
                ? new(true, "Steam game folder confirmed. AVRcade will remember its library and refresh discovery.")
                : new(false, "Choose this game's folder under a Steam library's steamapps\\common. The matching Steam app manifest and game executable must be present.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return new(false, $"Could not remember the Steam library: {ex.Message}");
        }
    }

    public ActionOutcome RememberGtaSanAndreasFolder(string folder)
    {
        try
        {
            return GtaSanAndreasPreflight.RememberInstall(folder)
                ? new(true, "San Andreas folder saved. AVRcade checks its game build next.")
                : new(false, "That folder has no gta_sa.exe. Choose the folder that contains the classic game's executable.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return new(false, $"Could not remember the San Andreas folder: {ex.Message}");
        }
    }

    private static bool DefaultHeadsetConnected()
    {
        var running = OpenXrRuntimeSelector.GetRunningVrProcessNames();
        return OpenXrRuntimeSelector.IsSteamVrRunning(running) ||
               OpenXrRuntimeSelector.IsVirtualDesktopRunning(running);
    }

    private string ModpacksDir => Path.Combine(_repoRoot, "config", "modpacks");
    private string GamesDir => Path.Combine(_repoRoot, "config", "games");
    private string ProfilesDir => Path.Combine(_repoRoot, "config", "profiles");
    private string UnrealDir => Path.Combine(_repoRoot, "config", "unreal");
    private string RedengineDir => Path.Combine(_repoRoot, "config", "redengine", "native");
    private string RageDir => Path.Combine(_repoRoot, "config", "rage");
    private string LegacyDir => Path.Combine(_repoRoot, "config", "legacy");
    private string GtaSanAndreasProfilePath => Path.Combine(LegacyDir, "gta-san-andreas.json");
    private string GtaSanAndreasNativeConfigPath => Path.Combine(LegacyDir, "gta-san-andreas-native.json");
    /// The prebuilt x86 bridge and OpenXR loader: shipped under native/ in an
    /// installed copy, taken from the build output in a source checkout.
    private string GtaSanAndreasPayloadDir
    {
        get
        {
            var shipped = Path.Combine(_repoRoot, "native", "gta-san-andreas");
            return Directory.Exists(shipped) ? shipped : Path.Combine(_repoRoot, "build", "gtasa-x86");
        }
    }
    /// The Cyberpunk VR backend files: shipped under native/ in an installed copy,
    /// staged by scripts/collect-cyberpunk-backend.ps1 in a source checkout.
    private CyberpunkBackendInstaller? CyberpunkBackend
    {
        get
        {
            if (_cyberpunkBackend is not null)
                return _cyberpunkBackend;
            var package = Path.Combine(RedengineDir, "cyberpunk-2077-backend.json");
            if (!File.Exists(package))
                return null;
            var shipped = Path.Combine(_repoRoot, "native", "cyberpunk-2077");
            return _cyberpunkBackend = new CyberpunkBackendInstaller(package,
                Directory.Exists(shipped) ? shipped : Path.Combine(_repoRoot, "build", "cyberpunk-backend"),
                _cyberpunkStateDir);
        }
    }
    private const string GtaSanAndreasBridgeFileName = "vrclient_gtasa_theater.asi";
    private const string GtaSanAndreasOpenXrLoaderFileName = "openxr_loader.dll";
    private const string GtaSanAndreasNativeConfigFileName = "vrclient_gtasa_theater.json";
    private string RulesPath => Path.Combine(_repoRoot, "config", "safety", "default-rules.json");
    private string GameConfigPath(string slug) => Path.Combine(GamesDir, $"{slug}.json");
    private string UevrGamePath(string slug) => Path.Combine(UnrealDir, $"{slug}.uevr.json");
    /// Where UEVR keeps a game's profile; it is keyed by the process UEVR attaches to.
    private static string UevrProfileDirectory(UevrGame game) => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "UnrealVRMod", UevrTools.ShippingProcessName(game));
    private string ModpackPath(string slug) => Path.Combine(ModpacksDir, $"{slug}.modpack.json");
    private string LockfilePath(string slug) => Path.Combine(ModpacksDir, $"{slug}.lock.json");
    private string ComfortMapPath(string slug) => Path.Combine(ModpacksDir, $"{slug}.comfort-map.json");
    private string ProfilePath(string slug) => Path.Combine(ProfilesDir, $"{slug}-game-profile.json");

    public ControllerReference? GetControllerReference(string slug) =>
        ControllerReferenceCatalog.Load(_repoRoot, slug);

    public CyberpunkHudMode? GetHudMode(GameView game) =>
        game.Engine is GameEngine.Redengine && !string.IsNullOrEmpty(game.InstallDir)
            ? CyberpunkHudSettings.ReadMode(game.InstallDir)
            : null;

    public CyberpunkTurnPreference GetCyberpunkTurnPreference(GameView game) =>
        game.Engine is GameEngine.Redengine && !string.IsNullOrEmpty(game.InstallDir)
            ? CyberpunkTurnSettings.Read(game.InstallDir)
            : CyberpunkTurnSettings.Default;

    public ActionOutcome SetCyberpunkTurnPreference(GameView game, CyberpunkTurnPreference preference)
    {
        if (game.Engine is not GameEngine.Redengine || string.IsNullOrEmpty(game.InstallDir))
            return new ActionOutcome(false, "Turning modes are available only for an installed Cyberpunk 2077 VR conversion.");
        try
        {
            CyberpunkTurnSettings.Write(game.InstallDir, preference);
            return new ActionOutcome(true, preference.Mode is CyberpunkTurnMode.Snap
                ? $"Cyberpunk VR snap turning set to {preference.SnapDegrees}° steps."
                : "Cyberpunk VR smooth turning enabled. Speed follows Cyberpunk's controller sensitivity.");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, $"Could not update Cyberpunk VR turning: {ex.Message}");
        }
    }

    public GtaTurnPreference GetGtaSanAndreasTurnPreference() =>
        new GtaSanAndreasTurnSettings().Load();

    public ActionOutcome SetGtaSanAndreasTurnPreference(GameView game, GtaTurnPreference preference)
    {
        if (game.Engine is not GameEngine.LegacyD3D9 || game.Slug != "gta-san-andreas")
            return new ActionOutcome(false, "Turning modes are available only for GTA San Andreas VR.");
        try
        {
            new GtaSanAndreasTurnSettings().Save(preference);
            return new ActionOutcome(true,
                $"San Andreas VR turning set to {preference.Mode}" +
                (preference.Mode is GtaTurnMode.Smooth
                    ? $" at {preference.SmoothDegreesPerSecond}°/s"
                    : $" ({preference.SnapDegrees}° steps)") +
                ". This applies on the next VR launch.");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, $"Could not save San Andreas turning: {ex.Message}");
        }
    }

    public ActionOutcome SetCyberpunkHudMode(GameView game, CyberpunkHudMode mode)
    {
        if (game.Engine is not GameEngine.Redengine || string.IsNullOrEmpty(game.InstallDir))
            return new ActionOutcome(false, "HUD modes are available only for an installed Cyberpunk 2077 conversion.");
        try
        {
            CyberpunkHudSettings.WriteMode(game.InstallDir, mode);
            return new ActionOutcome(true,
                $"Cyberpunk HUD set to {CyberpunkHudSettings.DisplayName(mode)}. The change applies live or on the next VR launch.");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, $"Could not update the Cyberpunk HUD: {ex.Message}");
        }
    }

    public static GameReadiness ComputeUnrealReadiness(
        bool installFound, bool safetyBlocked, bool uevrFetched,
        bool dotNetFetched, bool headsetConnected,
        bool injectionStable, bool headsetT1Verified, bool profileVerified)
    {
        if (safetyBlocked) return GameReadiness.Blocked;
        if (!installFound) return GameReadiness.NeedsGame;
        if (!uevrFetched || !dotNetFetched) return GameReadiness.NeedsUevr;
        if (!headsetConnected) return GameReadiness.NeedsHeadset;
        if (!injectionStable || !headsetT1Verified || !profileVerified)
            return GameReadiness.ProfileUnverified;
        return GameReadiness.Ready;
    }

    /// RAGE Phase 11 is foundation-only: no state reaches Ready, Converted, or NeedsVrMod.
    public static GameReadiness ComputeRageReadiness(bool safetyBlocked, bool installFound,
        bool identityReady, bool transactionIncomplete)
    {
        if (safetyBlocked) return GameReadiness.Blocked;
        if (!installFound) return GameReadiness.NeedsGame;
        if (!identityReady) return GameReadiness.ProfileUnverified;
        if (transactionIncomplete) return GameReadiness.ProfileUnverified;
        return GameReadiness.ProfileUnverified;
    }

    public static GameReadiness ComputeLegacyD3D9Readiness(
        GtaSanAndreasPreflightResult preflight)
    {
        if (preflight.Status is GtaSanAndreasPreflightStatus.ProfileMissing or
            GtaSanAndreasPreflightStatus.ProfileInvalid)
            return GameReadiness.Blocked;
        if (preflight.Status == GtaSanAndreasPreflightStatus.GameNotFound)
            return GameReadiness.NeedsGame;
        if (IsBlockedLegacyPreflight(preflight.Status))
            return GameReadiness.Blocked;

        // Even a pinned bridge is not promoted to Ready until live stereo and
        // headset evidence exists. This mirrors the native adapter policy.
        return GameReadiness.ProfileUnverified;
    }

    private static bool IsBlockedLegacyPreflight(GtaSanAndreasPreflightStatus status) => status is
        GtaSanAndreasPreflightStatus.ExecutableNotFound or
        GtaSanAndreasPreflightStatus.ArchitectureMismatch or
        GtaSanAndreasPreflightStatus.FingerprintUnpinned or
        GtaSanAndreasPreflightStatus.HashMismatch or
        GtaSanAndreasPreflightStatus.BridgeArchitectureMismatch or
        GtaSanAndreasPreflightStatus.BridgeHashMismatch;

    private static string LegacyD3D9BlockedDetail(GtaSanAndreasPreflightStatus status) => status switch
    {
        GtaSanAndreasPreflightStatus.ExecutableNotFound =>
            "The detected GTA San Andreas folder does not contain gta_sa.exe.",
        GtaSanAndreasPreflightStatus.ArchitectureMismatch =>
            "The detected GTA SA executable is not the supported x86 build.",
        GtaSanAndreasPreflightStatus.FingerprintUnpinned =>
            "The AVRcade GTA SA profile does not pin a supported executable identity.",
        GtaSanAndreasPreflightStatus.HashMismatch =>
            "The GTA SA executable does not match the supported x86 identity.",
        GtaSanAndreasPreflightStatus.BridgeArchitectureMismatch =>
            "The installed GTA SA VR bridge is not the required x86 build.",
        GtaSanAndreasPreflightStatus.BridgeHashMismatch =>
            "The installed GTA SA VR bridge does not match AVRcade's packaged bridge profile.",
        _ => $"GTA San Andreas identity validation failed ({status})."
    };

    private static string OfflineLibraryDetail(string library) =>
        $"Steam lists this game on {library}, but that library is disconnected. Reconnect the drive and refresh AVRcade.";

    /// headsetConnected lets a caller that already sampled the VR processes pass
    /// its answer in, so one refresh works from a single sample.
    public IReadOnlyList<GameView> ListGames(bool? headsetConnected = null)
    {
        var views = new List<GameView>();
        var headset = headsetConnected ?? _headsetConnected();
        foreach (var g in new GameCatalog().Discover(ModpacksDir, GamesDir, ProfilesDir))
        {
            if ((!RedDeadCatalogEnabled && DeferredCatalogSlugs.Contains(g.Slug)) ||
                (!FalloutCatalogEnabled && DeferredFalloutCatalogSlugs.Contains(g.Slug)))
                continue;
            var (displayName, firstExe) = ReadGameConfigBasics(g.Slug);
            var installDir = string.IsNullOrEmpty(firstExe) ? null : _locator(g.SteamAppId, firstExe);
            var offlineLibrary = installDir is null
                ? new SteamLibraryScanner().FindOfflineLibrary(g.SteamAppId) : null;
            var modInstalled = installDir is not null && new ModInstaller().IsInstalled(installDir);
            var verdict = EvaluateSafety(g.Slug);
            var headsetVerified = UnityHeadsetVerified(g.Slug);
            var readiness = verdict.Verdict.IsBlocked()
                ? GameReadiness.Blocked
                : installDir is null
                    ? GameReadiness.NeedsGame
                    : !modInstalled
                        ? GameReadiness.NeedsVrMod
                        : !headset
                            ? GameReadiness.NeedsHeadset
                            : headsetVerified ? GameReadiness.Ready : GameReadiness.ProfileUnverified;
            views.Add(new GameView(
                g.Slug, displayName, g.SteamAppId, installDir, modInstalled,
                verdict.Verdict.ToString(), verdict.ReasonCode,
                GameEngine.Unity, readiness,
                offlineLibrary is null ? ReadinessDetail(readiness, GameEngine.Unity)
                    : OfflineLibraryDetail(offlineLibrary),
                OfflineSteamLibrary: offlineLibrary));
        }

        // Fallout: New Vegas uses a dedicated desktop setup flow when its catalog entry is enabled.
        var fnvAdapterPath = Path.Combine(_repoRoot, "adapters", "fallout_new_vegas", "adapter.json");
        if (FalloutCatalogEnabled && File.Exists(fnvAdapterPath))
        {
            var discovery = _falloutNewVegasDiscovery();
            var install = discovery.Installs.FirstOrDefault(candidate =>
                candidate.Support is FnvInstallSupport.Supported or FnvInstallSupport.ManualUnverified);
            var readiness = install is null
                ? GameReadiness.NeedsGame
                : GameReadiness.ProfileUnverified;
            views.Add(new GameView(
                "fallout-new-vegas", "Fallout: New Vegas", FalloutNewVegasDiscovery.SteamAppId,
                install?.RootDirectory, ModInstalled: false,
                "Allow", "single_player_user_supplied_stack",
                GameEngine.FalloutNewVegas, readiness,
                install is null
                    ? "Install a supported Steam or GOG copy, then refresh."
                    : "Open headset-test setup to check FNVR, the native OpenXR adapter, MO2, and SteamVR components."));
        }

        var fallout3AdapterPath = Path.Combine(_repoRoot, "adapters", "fallout_3", "adapter.json");
        if (FalloutCatalogEnabled && File.Exists(fallout3AdapterPath))
        {
            var install = _fallout3Discovery().Installs.FirstOrDefault(candidate =>
                candidate.Support == Fallout3InstallSupport.Supported);
            var runtime = OpenXrRuntimeSelector.SelectFor32BitProcess(
                OpenXrRuntimeSelector.GetRunningVrProcessNames(),
                OpenXrRuntimeSelector.DiscoverAvailableRuntimes());
            var readiness = install is null ? GameReadiness.NeedsGame
                : runtime is null ? GameReadiness.NeedsHeadset
                : GameReadiness.ProfileUnverified;
            views.Add(new GameView(
                "fallout-3", "Fallout 3: Game of the Year Edition", Fallout3Discovery.SteamAppId,
                install?.RootDirectory, ModInstalled: false,
                "Allow", "single_player_native_experiment",
                GameEngine.Fallout3, readiness,
                install is null ? "Install a supported Steam/GOG build, then refresh."
                    : runtime is null ? "Start SteamVR or Virtual Desktop with a 32-bit OpenXR runtime, then refresh."
                    : "Native VR - Experimental. The exact-build FOSE hook is available; stereo and headset tracking still require a live test."));
        }

        var releasePath = Path.Combine(UnrealDir, "uevr-release.json");
        var dotNetPath = Path.Combine(UnrealDir, "dotnet-desktop-runtime.json");
        if (Directory.Exists(UnrealDir) && File.Exists(releasePath) && File.Exists(dotNetPath))
        {
            var dotNetPin = UevrTools.LoadDotNetDesktopPin(dotNetPath);
            foreach (var path in Directory.EnumerateFiles(UnrealDir, "*.uevr.json"))
            {
                var game = UevrTools.LoadGame(path);
                var installDir = _locator(game.SteamAppId, game.LauncherStub);
                var offlineLibrary = installDir is null
                    ? new SteamLibraryScanner().FindOfflineLibrary(game.SteamAppId) : null;
                var verdict = new SafetyGate().Evaluate(path, RulesPath);
                var safetyBlocked = verdict.Verdict.IsBlocked();
                var release = UevrTools.LoadReleasePin(releasePath, game.Channel);
                var fetched = _uevrFetched(release);
                var dotNetFetched = _dotNetFetched(dotNetPin);
                var profileImported = UevrTools.IsImportedProfileValid(game, UevrProfileDirectory(game));
                var readiness = ComputeUnrealReadiness(
                    installDir is not null, safetyBlocked, fetched, dotNetFetched,
                    headset, game.InjectionStable, game.HeadsetT1Verified,
                    game.ProfileVerified);
                views.Add(new GameView(
                    game.Slug, game.DisplayName, game.SteamAppId, installDir,
                    fetched && dotNetFetched, verdict.Verdict.ToString(), verdict.ReasonCode,
                    GameEngine.Unreal, readiness,
                    offlineLibrary is null ? UnrealReadinessDetail(readiness, game)
                        : OfflineLibraryDetail(offlineLibrary),
                    game.ProfileSource?.TreeUrl,
                    game.ProfileSource?.Redistribution.Equals(
                        "user_import_only", StringComparison.OrdinalIgnoreCase) == true,
                    profileImported, offlineLibrary));
            }
        }

        var cyberpunkProfile = Path.Combine(RedengineDir, "cyberpunk-2077.json");
        if (File.Exists(cyberpunkProfile))
        {
            using var document = JsonDocument.Parse(File.ReadAllText(cyberpunkProfile));
            var game = document.RootElement.GetProperty("game");
            var slug = game.GetProperty("slug").GetString()!;
            var displayName = game.GetProperty("display_name").GetString()!;
            var appId = game.GetProperty("steam_app_id").GetString()!;
            var shippingBinary = game.GetProperty("shipping_binary").GetString()!;
            var installDir = _locator(appId, shippingBinary);
            var manifest = installDir is null ? null : SteamManifestBeside(installDir, appId);
            var preflight = new CyberpunkNativePreflight().Evaluate(
                cyberpunkProfile, installDir, manifest);
            var installed = installDir is not null &&
                preflight.MissingDependencies.Count == 0;
            var blocked = preflight.Conflicts.Count > 0 || preflight.IdentityMismatch;
            var readiness = installDir is null ? GameReadiness.NeedsGame
                : blocked ? GameReadiness.Blocked
                : !installed ? GameReadiness.NeedsVrMod
                : !headset ? GameReadiness.NeedsHeadset
                : GameReadiness.ProfileUnverified;
            views.Add(new GameView(
                slug, displayName, appId, installDir, installed,
                blocked ? "Block" : "Allow",
                preflight.Conflicts.Count > 0 ? nameof(CyberpunkNativePreflightStatus.ConflictingHooks)
                    : preflight.IdentityMismatch ? nameof(CyberpunkNativePreflightStatus.BuildMismatch)
                    : "cyberpunk_preflight_ready",
                GameEngine.Redengine, readiness,
                ReadinessDetail(readiness, GameEngine.Redengine)));
        }

        var rdr2Profile = Path.Combine(RageDir, "rdr2.json");
        if (RedDeadCatalogEnabled && File.Exists(rdr2Profile))
        {
            using var document = JsonDocument.Parse(File.ReadAllText(rdr2Profile));
            var game = document.RootElement.GetProperty("game");
            var slug = game.GetProperty("slug").GetString()!;
            var displayName = game.GetProperty("display_name").GetString()!;
            var appId = game.GetProperty("steam_app_id").GetString()!;
            var executableName = game.GetProperty("launcher_executable").GetString()!;
            var installDir = _locator(appId, executableName);
            SteamGame? discovered = installDir is null ? null : new SteamGame
            {
                AppId = appId,
                InstallDir = installDir,
                ExeName = executableName,
                ExecutablePath = Path.Combine(installDir, executableName),
                ManifestPath = SteamManifestBeside(installDir, appId) ?? string.Empty
            };
            var preflight = new Rdr2Preflight().Evaluate(rdr2Profile, discovered, "story");
            var safetyBlocked = preflight.Status is Rdr2PreflightStatus.OnlineModeBlocked or Rdr2PreflightStatus.SafetyUnreviewed;
            var readiness = ComputeRageReadiness(safetyBlocked, installDir is not null, preflight.Ready, transactionIncomplete: false);
            views.Add(new GameView(
                slug, displayName, appId, installDir, ModInstalled: false,
                safetyBlocked ? "Block" : "Unverified", preflight.Status.ToString(),
                GameEngine.Rage, readiness,
                readiness is GameReadiness.NeedsGame
                    ? "Install the Steam-owned game, then refresh."
                    : safetyBlocked
                        ? "RDR2 is restricted to explicit Story Mode; Online or unreviewed intent is blocked."
                        : "RDR2 uses the native RAGE adapter contract; build identity, bridge services, and one smoke run are still required."));
        }

        if (File.Exists(GtaSanAndreasProfilePath))
        {
            try
            {
                var gtaProfile = new GtaSanAndreasPreflight().LoadProfile(GtaSanAndreasProfilePath);
                var installDir = _gtaSanAndreasLocator();
                var bridgePath = ResolveGtaSanAndreasBridge(installDir, includeBuildArtifact: false);
                var preflight = new GtaSanAndreasPreflight().Evaluate(
                    GtaSanAndreasProfilePath, installDir, bridgePath);
                var readiness = ComputeLegacyD3D9Readiness(preflight);
                var identityBlocked = IsBlockedLegacyPreflight(preflight.Status) ||
                    preflight.Status is GtaSanAndreasPreflightStatus.ProfileMissing or
                    GtaSanAndreasPreflightStatus.ProfileInvalid;
                var bridgeInstalled = bridgePath is not null && File.Exists(bridgePath);
                var loaderInstalled = installDir is not null &&
                    GtaSanAndreasPreflight.ReadPeArchitecture(
                        Path.Combine(installDir, GtaSanAndreasOpenXrLoaderFileName)) == "x86";
                var nativeConfigInstalled = installDir is not null &&
                    NativeConfigMatchesRepository(
                        Path.Combine(installDir, GtaSanAndreasNativeConfigFileName));
                var modInstalled = !identityBlocked && bridgeInstalled && loaderInstalled && nativeConfigInstalled;
                views.Add(new GameView(
                    gtaProfile.Slug, gtaProfile.DisplayName, GtaSanAndreasPreflight.SteamAppId, installDir,
                    ModInstalled: modInstalled,
                    identityBlocked ? "Block" : "Allow",
                    identityBlocked ? preflight.Status.ToString() : "offline_only_ok",
                    GameEngine.LegacyD3D9, readiness,
                    readiness is GameReadiness.NeedsGame
                        ? "AVRcade did not find the classic Windows version of San Andreas. Use Locate installed game to point at the folder that contains gta_sa.exe."
                        : identityBlocked
                            ? LegacyD3D9BlockedDetail(preflight.Status)
                            : !bridgeInstalled
                                ? "Game identity and existing mod surface are recognized; install the x86 stereo bridge and loader to enable experimental VR launch."
                                : !loaderInstalled
                                    ? "The x86 stereo bridge is present, but openxr_loader.dll is missing beside it."
                                    : "The x86 stereo bridge and loader are installed; experimental stereo launch is available but still headset-unverified."));
            }
            catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException)
            {
                views.Add(new GameView(
                    "gta-san-andreas", "Grand Theft Auto: San Andreas", GtaSanAndreasPreflight.SteamAppId, null,
                    false, "Block", "invalid_profile",
                    GameEngine.LegacyD3D9, GameReadiness.Blocked,
                    "The GTA San Andreas legacy profile is invalid; conversion is refused."));
            }
        }

        return views.Where(view => RedDeadCatalogEnabled || !DeferredCatalogSlugs.Contains(view.Slug))
            .OrderBy(view => view.DisplayName, StringComparer.OrdinalIgnoreCase).ToList();
    }

    public ActionOutcome LaunchFallout3Native(string installDir, bool requireVortex = false)
    {
        if (requireVortex && !new ModManagerDiscovery().FindInstalled()
                .Any(manager => manager.Kind == ModManagerKind.Vortex))
            return new ActionOutcome(false,
                "Vortex was not found. Open Manage Fallout 3 mods in Vortex to reconnect or select it before launching modded VR.");
        var install = new Fallout3Discovery().Inspect(installDir, Fallout3Storefront.Steam);
        var running = OpenXrRuntimeSelector.GetRunningVrProcessNames();
        var runtime = OpenXrRuntimeSelector.SelectFor32BitProcess(
            running, OpenXrRuntimeSelector.DiscoverAvailableRuntimes());
        if (runtime is null)
            return new ActionOutcome(false,
                "No running SteamVR/Virtual Desktop 32-bit OpenXR runtime was found. Connect the headset and refresh.");
        var plugins = Path.Combine(install.RootDirectory, "Data", "FOSE", "Plugins");
        var installedProfile = Path.Combine(plugins, "fallout3-native-profile.ini");
        var reviewedProfile = Path.Combine(_repoRoot, "adapters", "fallout_3",
            "fallout3-native-profile-steam-1.7.0.3.ini");
        if (!File.Exists(installedProfile) || !File.Exists(reviewedProfile) ||
            !SHA256.HashData(File.ReadAllBytes(installedProfile)).SequenceEqual(
                SHA256.HashData(File.ReadAllBytes(reviewedProfile))))
            return new ActionOutcome(false,
                "The installed Fallout 3 hook profile differs from AVRcade's reviewed exact-build profile. Native launch was refused.");
        var options = new Fallout3DependencyOptions(null, null, null, null, null,
            Path.Combine(plugins, "vrclient_fallout3_native.dll"),
            installedProfile, null,
            running, runtime.JsonPath);
        var readiness = new Fallout3DependencyValidator().ValidateNativeDirect(install, options);
        var failure = readiness.Checks.FirstOrDefault(check => check.Severity == Fallout3Severity.Failure);
        if (failure is not null)
            return new ActionOutcome(false, $"Fallout 3 native preflight: {failure.Message}");
        var openXrLoader = Path.Combine(plugins, "openxr_loader.dll");
        if (!File.Exists(openXrLoader))
            return new ActionOutcome(false, "The x86 OpenXR loader is missing from Fallout 3's FOSE plugin folder.");
        if (Process.GetProcessesByName("Fallout3").Length > 0)
            return new ActionOutcome(false, "Fallout 3 is already running. Close it before starting a new VR session.");

        var start = CreateFallout3DirectLaunchInfo(install.RootDirectory, runtime.JsonPath);
        using var process = Process.Start(start);
        return process is null
            ? new ActionOutcome(false, "FOSE did not start.")
            : new ActionOutcome(true,
                $"Started Fallout 3 via FOSE with {runtime.Name}. {(requireVortex ? "This uses any Vortex-deployed game files, but AVRcade cannot verify that mods are deployed, the active Vortex profile, or mod compatibility. " : "Installed game files, including any deployed mods, are shared with this launch. ")}Native VR - Experimental; verify independent stereo and tracking in the headset.");
    }

    public ActionOutcome LaunchFallout3Desktop(string installDir)
    {
        var install = new Fallout3Discovery().Inspect(installDir, Fallout3Storefront.Steam);
        if (install.Support != Fallout3InstallSupport.Supported)
            return new ActionOutcome(false, "A supported Steam Fallout 3 installation was not found.");
        if (install.FileVersion != "1.7.0.3")
            return new ActionOutcome(false, "FOSE desktop launch requires the supported Fallout 3 1.7.0.3 executable.");
        if (!File.Exists(Path.Combine(install.RootDirectory, "fose_loader.exe")))
            return new ActionOutcome(false, "FOSE is required to retain FOSE-based Vortex mods in desktop mode.");
        if (Process.GetProcessesByName("Fallout3").Length > 0)
            return new ActionOutcome(false, "Fallout 3 is already running. Close it before starting another session.");
        var start = CreateFallout3DirectLaunchInfo(install.RootDirectory, null);
        using var process = Process.Start(start);
        return process is null
            ? new ActionOutcome(false, "FOSE did not start.")
            : new ActionOutcome(true,
                "Started Fallout 3 via FOSE in desktop mode (VR hooks off). Vortex-deployed game files and compatible FOSE mods remain available; the active Vortex profile was not independently verified.");
    }

    public static ProcessStartInfo CreateFallout3DirectLaunchInfo(string installRoot, string? runtimeManifest)
    {
        var start = new ProcessStartInfo(Path.Combine(installRoot, "fose_loader.exe"))
        {
            WorkingDirectory = installRoot,
            UseShellExecute = false
        };
        start.Environment["VRCLIENT_FALLOUT3_NATIVE_VR"] = runtimeManifest is null ? "0" : "1";
        if (runtimeManifest is null)
            start.Environment.Remove("XR_RUNTIME_JSON");
        else
            start.Environment["XR_RUNTIME_JSON"] = runtimeManifest;
        return start;
    }

    private static string ReadinessDetail(GameReadiness readiness, GameEngine engine) => readiness switch
    {
        GameReadiness.NeedsGame => "Install the game in Steam, then refresh the library.",
        GameReadiness.NeedsVrMod => "The game is installed; its verified community VR mod is ready to install.",
        GameReadiness.NeedsUevr => ".NET Desktop and UEVR need to be fetched and hash-verified.",
        GameReadiness.NeedsHeadset => "Connect a headset through Virtual Desktop or SteamVR, then refresh.",
        GameReadiness.ProfileUnverified => engine is GameEngine.Rage
            ? "RDR2 requires a reviewed, build-pinned Story Mode profile and native RAGE bridge evidence."
            : engine is GameEngine.Redengine
            ? "The REDengine backend is installed; one local headset validation run is still required."
            : engine is GameEngine.FalloutNewVegas
            ? "The user-owned FNVR stack must pass desktop setup before the headset test begins."
            : engine is GameEngine.Unity
            ? "The community VR mod is installed; stereo, tracking, and controls still need a headset check for this game build."
            : "UEVR is prepared, but this game's injection and headset profile still need a private-room verification run.",
        GameReadiness.Ready => engine switch
        {
            GameEngine.Unreal => "UEVR, headset, injection, and the game profile are verified.",
            GameEngine.Redengine => "The pinned REDengine backend and headset route are verified.",
            GameEngine.Rage => "The native RAGE adapter contract is present; bridge services and a Story Mode smoke run are still required.",
            GameEngine.FalloutNewVegas => "The FNVR stack is prepared; final stereo, tracking, controls, saves, and restoration remain a human headset gate.",
            _ => "The VR mod is installed and a headset runtime is connected."
        },
        GameReadiness.Blocked => "AVRcade's safety policy blocks this conversion.",
        _ => "State unavailable."
    };

    private static string UnrealReadinessDetail(GameReadiness readiness, UevrGame game)
    {
        if (!game.Slug.Equals("rv-there-yet", StringComparison.OrdinalIgnoreCase))
            return ReadinessDetail(readiness, GameEngine.Unreal);

        return readiness switch
        {
            GameReadiness.NeedsGame => "Install RV There Yet? from Steam, then refresh.",
            GameReadiness.NeedsUevr =>
                "Headset + gamepad milestone: fetch the pinned UEVR nightly and .NET Desktop runtime. The community profile is a user-import-only artifact.",
            GameReadiness.NeedsHeadset =>
                "Connect your headset through a healthy Virtual Desktop/VDXR or SteamVR/OpenXR runtime, then refresh.",
            GameReadiness.ProfileUnverified =>
                "Experimental VR profile: VDXR has displayed the main menu in-headset and gameplay was reached, but menu height, performance, and controller actions still need validation. Tracked hands are not implemented. Private co-op is untested.",
            GameReadiness.Ready =>
                "The build-pinned headset + gamepad milestone is verified; motion controls and private co-op retain their separate capability states.",
            _ => ReadinessDetail(readiness, GameEngine.Unreal)
        };
    }

    public async Task<ActionOutcome> PrepareUnrealAsync(
        string slug, IHttpDownloader downloader, CancellationToken ct = default)
    {
        var gamePath = UevrGamePath(slug);
        if (!File.Exists(gamePath))
            return new ActionOutcome(false, $"{slug} is not in the UEVR catalog.");
        var verdict = new SafetyGate().Evaluate(gamePath, RulesPath);
        if (verdict.Verdict.IsBlocked())
            return new ActionOutcome(false, $"Blocked: {verdict.ReasonCode}.");

        var game = UevrTools.LoadGame(gamePath);
        var release = UevrTools.LoadReleasePin(
            Path.Combine(UnrealDir, "uevr-release.json"), game.Channel);
        var dotNet = UevrTools.LoadDotNetDesktopPin(
            Path.Combine(UnrealDir, "dotnet-desktop-runtime.json"));
        await Task.WhenAll(
            UevrTools.EnsureFetchedAsync(release, downloader, ct: ct),
            UevrTools.EnsureDotNetFetchedAsync(dotNet, downloader, ct: ct));
        return new ActionOutcome(true,
            $"Prepared {game.DisplayName}: UEVR {release.Tag} and .NET Desktop {dotNet.Version} verified.");
    }

    public ActionOutcome ImportUnrealProfile(string slug, string archivePath)
    {
        var gamePath = UevrGamePath(slug);
        if (!File.Exists(gamePath))
            return new ActionOutcome(false, $"{slug} is not in the UEVR catalog.");

        try
        {
            var game = UevrTools.LoadGame(gamePath);
            var imported = UevrTools.ImportProfileArchive(game, archivePath, UevrProfileDirectory(game));
            return new ActionOutcome(true,
                $"Imported and verified {imported.FileCount} profile files for {game.DisplayName}. AVRcade created a rollback manifest.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   InvalidDataException or InvalidOperationException)
        {
            return new ActionOutcome(false, $"Profile import refused: {ex.Message}");
        }
    }

    public async Task<ActionOutcome> DownloadUnrealProfileAsync(
        string slug, IHttpDownloader downloader, CancellationToken ct = default)
    {
        var gamePath = UevrGamePath(slug);
        if (!File.Exists(gamePath))
            return new ActionOutcome(false, $"{slug} is not in the UEVR catalog.");

        try
        {
            var game = UevrTools.LoadGame(gamePath);
            var destination = UevrProfileDirectory(game);
            if (UevrTools.IsImportedProfileValid(game, destination))
                return new ActionOutcome(true, $"The reviewed VR profile for {game.DisplayName} is already installed.");
            var imported = await UevrTools.DownloadAndImportProfileAsync(
                game, downloader, destination, ct);
            return new ActionOutcome(true,
                $"Downloaded and verified {imported.FileCount} profile files for {game.DisplayName}. Ready for a headset test.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   InvalidDataException or InvalidOperationException or HttpRequestException)
        {
            return new ActionOutcome(false, $"Profile download failed: {ex.Message}");
        }
    }

    public ActionOutcome CheckGtaSanAndreas(string gameDir)
    {
        var bridgePath = ResolveGtaSanAndreasBridge(gameDir, includeBuildArtifact: true);
        var preflight = new GtaSanAndreasPreflight().Evaluate(
            GtaSanAndreasProfilePath, gameDir, bridgePath);
        return new ActionOutcome(
            preflight.Ready,
            $"GTA San Andreas preflight: {preflight.Status}. " +
            (preflight.ModArtifacts.Count > 0
                ? $"Preserved mod artifacts observed: {preflight.ModArtifacts.Count}. "
                : "No managed mod artifacts were changed. ") +
            (preflight.Ready
                ? "Pinned external bridge found; headset validation is still required."
                : "No game files were changed."));
    }

    public ActionOutcome InstallGtaSanAndreasTheater(string gameDir)
    {
        var profilePath = GtaSanAndreasProfilePath;
        var sourceBridge = Path.Combine(GtaSanAndreasPayloadDir, GtaSanAndreasBridgeFileName);
        var sourceLoader = Path.Combine(GtaSanAndreasPayloadDir, GtaSanAndreasOpenXrLoaderFileName);
        var sourceNativeConfig = GtaSanAndreasNativeConfigPath;
        var preflight = new GtaSanAndreasPreflight().Evaluate(
            profilePath, gameDir,
            File.Exists(sourceBridge) ? sourceBridge : null);
        if (IsBlockedLegacyPreflight(preflight.Status) || !preflight.GameIdentityReady)
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install refused: preflight is {preflight.Status}.");
        if (!File.Exists(sourceBridge) || !File.Exists(sourceLoader))
            return new ActionOutcome(false,
                "GTA San Andreas stereo bridge install requires the x86 bridge and OpenXR loader to be built first.");
        if (!File.Exists(sourceNativeConfig))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install requires the native hook profile ({sourceNativeConfig}).");
        if (GtaSanAndreasPreflight.ReadPeArchitecture(sourceLoader) != "x86")
            return new ActionOutcome(false,
                "GTA San Andreas stereo bridge install refused: the staged OpenXR loader is not PE32/x86.");
        if (!preflight.Ready)
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install refused: bridge preflight is {preflight.Status}.");

        var destinationBridge = Path.Combine(gameDir, GtaSanAndreasBridgeFileName);
        var destinationLoader = Path.Combine(gameDir, GtaSanAndreasOpenXrLoaderFileName);
        var destinationNativeConfig = Path.Combine(gameDir, GtaSanAndreasNativeConfigFileName);
        if (File.Exists(destinationBridge))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install refused: destination already exists ({destinationBridge}).");
        if (File.Exists(destinationLoader))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install refused: destination already exists ({destinationLoader}).");
        if (File.Exists(destinationNativeConfig))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install refused: destination already exists ({destinationNativeConfig}).");

        var bridgeCopied = false;
        var loaderCopied = false;
        var nativeConfigCopied = false;
        try
        {
            File.Copy(sourceBridge, destinationBridge);
            bridgeCopied = true;
            File.Copy(sourceLoader, destinationLoader);
            loaderCopied = true;
            File.Copy(sourceNativeConfig, destinationNativeConfig);
            nativeConfigCopied = true;
            return new ActionOutcome(true,
                "GTA San Andreas stereo bridge, x86 OpenXR loader, and external native hook profile installed; existing ASI/config files and the game executable were not replaced.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (bridgeCopied && File.Exists(destinationBridge))
                File.Delete(destinationBridge);
            if (loaderCopied && File.Exists(destinationLoader))
                File.Delete(destinationLoader);
            if (nativeConfigCopied && File.Exists(destinationNativeConfig))
                File.Delete(destinationNativeConfig);
            return new ActionOutcome(false,
                $"GTA San Andreas stereo bridge install failed: {ex.Message}");
        }
    }

    public ActionOutcome LaunchGtaSanAndreasTheater(
        string gameDir,
        bool dryRun,
        XrRuntimeChoice? xrRuntime,
        GtaSanAndreasVrMods mods = GtaSanAndreasVrMods.Existing)
    {
        var bridgePath = ResolveGtaSanAndreasBridge(gameDir, includeBuildArtifact: false);
        var preflight = new GtaSanAndreasPreflight().Evaluate(
            GtaSanAndreasProfilePath, gameDir, bridgePath);
        if (!preflight.Ready)
            return new ActionOutcome(false,
                $"GTA San Andreas stereo launch refused: bridge preflight is {preflight.Status}.");

        var loaderPath = Path.Combine(gameDir, GtaSanAndreasOpenXrLoaderFileName);
        var nativeConfigPath = Path.Combine(gameDir, GtaSanAndreasNativeConfigFileName);
        if (!File.Exists(loaderPath))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo launch refused: x86 {GtaSanAndreasOpenXrLoaderFileName} is missing beside the bridge.");
        if (GtaSanAndreasPreflight.ReadPeArchitecture(loaderPath) != "x86")
            return new ActionOutcome(false,
                $"GTA San Andreas stereo launch refused: {GtaSanAndreasOpenXrLoaderFileName} is not PE32/x86.");
        if (!File.Exists(nativeConfigPath))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo launch refused: external native hook profile is missing ({nativeConfigPath}).");
        if (!NativeConfigMatchesRepository(nativeConfigPath))
            return new ActionOutcome(false,
                $"GTA San Andreas stereo launch refused: installed native hook profile differs from the pinned AVRcade profile ({nativeConfigPath}).");

        var safety = new SafetyVerdict(
            Verdict.Allow,
            "offline_only_ok",
            "safety.allow.offline",
            "classic GTA San Andreas experimental stereo launch is restricted to the local offline install");
        var plan = new GameLauncher().Plan(
            gameDir, safety, modInstalled: true, exeName: GtaSanAndreasPreflight.ExecutableName);
        xrRuntime = OpenXrRuntimeSelector.For32BitProcess(xrRuntime);
        var childEnvironment = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["VRCLIENT_GTASA_OPENXR_LOADER"] = loaderPath,
            ["VRCLIENT_GTASA_HOOK_CONFIG"] = nativeConfigPath,
            ["VRCLIENT_GTASA_STEREO"] = "1",
            ["VRCLIENT_GTASA_INPUT"] = "1"
        };
        foreach (var (key, value) in GtaSanAndreasTurnSettings.LaunchEnvironment(
            GetGtaSanAndreasTurnPreference()))
            childEnvironment[key] = value;
        try
        {
            var launchArguments = GtaSanAndreasModLoaderProfiles.PrepareArguments(
                gameDir, mods, dryRun);
            var pid = new GameLauncher().Launch(
                plan, acknowledgeWarn: true, dryRun, xrRuntime, childEnvironment, launchArguments);
            return new ActionOutcome(true,
                dryRun
                    ? "GTA San Andreas stereo launch plan ok (dry run)"
                    : $"launched GTA San Andreas experimental stereo bridge pid={pid} " +
                      $"(ModLoader mode: {mods}; GGMM and root-level mods are shared)");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, ex.Message.ReplaceLineEndings(" "));
        }
    }

    public ActionOutcome LaunchGtaSanAndreasDesktop(string gameDir, bool dryRun = false)
    {
        if (string.IsNullOrWhiteSpace(gameDir))
            return new ActionOutcome(false, "GTA San Andreas desktop launch refused: game folder is missing.");
        var executable = Path.Combine(gameDir, GtaSanAndreasPreflight.ExecutableName);
        if (GtaSanAndreasPreflight.ReadPeArchitecture(executable) != "x86")
            return new ActionOutcome(false,
                "GTA San Andreas desktop launch refused: a classic x86 gta_sa.exe was not found.");

        var safety = new SafetyVerdict(Verdict.Allow, "offline_only_ok",
            "safety.allow.offline", "classic GTA San Andreas local offline launch");
        var plan = new GameLauncher().Plan(gameDir, safety, modInstalled: true,
            exeName: GtaSanAndreasPreflight.ExecutableName);
        try
        {
            var pid = new GameLauncher().Launch(plan, acknowledgeWarn: true, dryRun,
                xrRuntime: null, childEnvironment: GtaSanAndreasDesktopEnvironment());
            return new ActionOutcome(true, dryRun
                ? "GTA San Andreas desktop launch plan ok (VR disabled)"
                : $"launched GTA San Andreas without VR pid={pid}; installed game mods remain active");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, ex.Message.ReplaceLineEndings(" "));
        }
    }

    public string? GetGtaSanAndreasVanillaFolder() => _gtaVanillaSettings.Load();

    public ActionOutcome SetGtaSanAndreasVanillaFolder(string vrGameDir, string cleanGameDir)
    {
        try
        {
            _gtaVanillaSettings.Save(cleanGameDir, vrGameDir);
            return new ActionOutcome(true, $"Clean San Andreas folder selected: {_gtaVanillaSettings.Load()}");
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            return new ActionOutcome(false, ex.Message.ReplaceLineEndings(" "));
        }
    }

    public ActionOutcome LaunchGtaSanAndreasVanilla(string vrGameDir, bool dryRun = false)
    {
        var cleanGameDir = _gtaVanillaSettings.Load();
        var error = GtaSanAndreasVanillaSettings.Validate(cleanGameDir, vrGameDir);
        if (error is not null) return new ActionOutcome(false, error);
        return LaunchGtaSanAndreasDesktop(cleanGameDir!, dryRun);
    }

    public static IReadOnlyDictionary<string, string> GtaSanAndreasDesktopEnvironment() =>
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["VRCLIENT_GTASA_DISABLE"] = "1",
            ["VRCLIENT_GTASA_STEREO"] = "0",
            ["VRCLIENT_GTASA_INPUT"] = "0"
        };

    public ActionOutcome OpenGtaSanAndreasGgmm(string gameDir)
    {
        if (string.IsNullOrWhiteSpace(gameDir))
            return new ActionOutcome(false, "GGMM was not opened: game folder is missing.");
        if (!File.Exists(Path.Combine(gameDir, GtaSanAndreasPreflight.ExecutableName)))
            return new ActionOutcome(false,
                "GGMM was not opened: gta_sa.exe was not found in the selected game folder.");

        var executable = Path.Combine(gameDir, "ggmm.exe");
        var interfaceDll = Path.Combine(gameDir, "gtainterface.dll");
        if (!File.Exists(executable) || !File.Exists(interfaceDll))
            return new ActionOutcome(false,
                "GGMM is not installed. Place your own ggmm.exe and gtainterface.dll beside gta_sa.exe, then try again. AVRcade does not bundle GGMM.");
        try
        {
            var process = Process.Start(new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = gameDir,
                UseShellExecute = false
            });
            return process is null
                ? new ActionOutcome(false, "GGMM did not start.")
                : new ActionOutcome(true, $"opened GGMM pid={process.Id}");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, $"GGMM did not start: {ex.Message.ReplaceLineEndings(" ")}");
        }
    }

    /// VR launch of San Andreas, pinned to the live 32-bit OpenXR runtime (gta_sa.exe is x86).
    public ActionOutcome LaunchGtaSanAndreasVr(string gameDir, GtaSanAndreasVrMods mods) =>
        LaunchGtaSanAndreasTheater(gameDir, dryRun: false,
            OpenXrRuntimeSelector.SelectLiveFor32BitProcess(), mods);

    public Task<ActionOutcome> LaunchUnrealAsync(
        string slug, bool acknowledge, CancellationToken ct = default) =>
        RunCliAsync(start =>
        {
            start.ArgumentList.Add("uevr-launch");
            start.ArgumentList.Add(slug);
            if (acknowledge) start.ArgumentList.Add("--acknowledge");
        }, ct);

    /// Mods an Unreal game would load on any launch (pak mods, UE4SS). Unreal has
    /// no per-launch off switch for them, so the play modes are gated on this.
    public UnrealModInventory GetUnrealMods(GameView game)
    {
        var gamePath = UevrGamePath(game.Slug);
        if (game.Engine is not GameEngine.Unreal || game.InstallDir is null || !File.Exists(gamePath))
            return new UnrealModInventory(string.Empty, []);
        return UnrealMods.Scan(game.InstallDir, UevrTools.LoadGame(gamePath).ShippingBinaryRelative);
    }

    /// Monitor launch of an Unreal game: a normal Steam start with no UEVR injection.
    public ActionOutcome LaunchUnrealFlat(string slug, bool dryRun = false, string? steamRoot = null)
    {
        var gamePath = UevrGamePath(slug);
        if (!File.Exists(gamePath))
            return new ActionOutcome(false, $"{slug} is not in the UEVR catalog.");
        var game = UevrTools.LoadGame(gamePath);
        var steam = new SteamOwnedLauncher().Launch(game.SteamAppId, steamRoot, dryRun);
        return steam.Success
            ? new ActionOutcome(true, dryRun
                ? $"Steam launch plan ok for {game.DisplayName} (no VR injection)."
                : $"Started {game.DisplayName} through Steam without VR.")
            : new ActionOutcome(false, steam.FailureText);
    }

    /// Whether this copy of AVRcade carries the complete Cyberpunk VR backend.
    public bool CyberpunkBackendInstallable => CyberpunkBackend?.PayloadReady == true;

    /// Installs the VR backend and the modding frameworks it needs into the game
    /// folder. Refused for any game build other than the one the backend is pinned to.
    public async Task<ActionOutcome> InstallCyberpunkVrAsync(
        string gameDir, IHttpDownloader downloader, CancellationToken ct = default)
    {
        if (CyberpunkBackend is not { } backend)
            return new ActionOutcome(false, "This copy of AVRcade has no Cyberpunk VR backend package.");
        var preflight = EvaluateCyberpunk(gameDir);
        if (preflight.ActualSha256 is null)
            return new ActionOutcome(false, "Cyberpunk2077.exe was not found in that folder. Nothing was installed.");
        if (preflight.IdentityMismatch)
            return new ActionOutcome(false,
                $"This Cyberpunk 2077 build is not the one the VR backend supports (Steam build {preflight.ExpectedBuildId}). Nothing was installed.");
        if (preflight.Conflicts.Count > 0)
            return new ActionOutcome(false,
                $"Remove the conflicting VR or graphics hook first: {string.Join(", ", preflight.Conflicts)}. Nothing was installed.");

        var outcome = await backend.InstallAsync(gameDir, downloader, ct);
        if (!outcome.Ok)
            return new ActionOutcome(false, outcome.Message);
        var missing = EvaluateCyberpunk(gameDir).MissingDependencies;
        return missing.Count == 0
            ? new ActionOutcome(true, outcome.Message)
            : new ActionOutcome(false,
                $"{outcome.Message} Still missing, likely from an incomplete framework you already had: {string.Join(", ", missing)}.");
    }

    /// What InstallCyberpunkVrAsync would do, without downloading or writing anything.
    public ActionOutcome PlanCyberpunkVrInstall(string gameDir)
    {
        if (CyberpunkBackend is not { } backend)
            return new ActionOutcome(false, "This copy of AVRcade has no Cyberpunk VR backend package.");
        var preflight = EvaluateCyberpunk(gameDir);
        var plan = backend.Plan(gameDir);
        var missingPayload = backend.MissingPayloadFiles();
        var ok = preflight.ActualSha256 is not null && !preflight.IdentityMismatch &&
                 preflight.Conflicts.Count == 0 && missingPayload.Count == 0;
        return new ActionOutcome(ok,
            $"build_match={!preflight.IdentityMismatch && preflight.ActualSha256 is not null} " +
            $"conflicts={preflight.Conflicts.Count} payload_missing={missingPayload.Count} " +
            $"download=[{string.Join(", ", plan.ToDownload.Select(f => $"{f.Name} {f.Version}"))}] " +
            $"keep=[{string.Join(", ", plan.AlreadyPresent.Select(f => f.Name))}] " +
            $"copy={plan.BackendFilesToCopy} replace={plan.BackendFilesToReplace}");
    }

    public ActionOutcome UninstallCyberpunkVr(string gameDir, bool removeFrameworks = false)
    {
        if (CyberpunkBackend is not { } backend)
            return new ActionOutcome(false, "This copy of AVRcade has no Cyberpunk VR backend package.");
        var outcome = backend.Uninstall(gameDir, removeFrameworks);
        return new ActionOutcome(outcome.Ok, outcome.Message);
    }

    private CyberpunkNativePreflightResult EvaluateCyberpunk(string gameDir) =>
        new CyberpunkNativePreflight().Evaluate(
            Path.Combine(RedengineDir, "cyberpunk-2077.json"), gameDir,
            SteamManifestBeside(gameDir, CyberpunkNativePreflight.SteamAppId));

    /// Monitor launch of Cyberpunk 2077 when AVRcade's VR backend is not installed:
    /// a normal Steam start, with REDmod's -modded only for the modded mode.
    public ActionOutcome LaunchCyberpunkFlatViaSteam(bool withMods, bool dryRun = false, string? steamRoot = null)
    {
        var steam = new SteamOwnedLauncher().Launch(CyberpunkNativePreflight.SteamAppId, steamRoot, dryRun,
            launchArguments: new CyberpunkLaunchSession().BuildLaunchArguments(withMods));
        return steam.Success
            ? new ActionOutcome(true, dryRun
                ? "Steam launch plan ok for Cyberpunk 2077 (VR off)."
                : withMods
                    ? "Started Cyberpunk 2077 through Steam with REDmod mods, VR off."
                    : "Started Cyberpunk 2077 through Steam, VR off.")
            : new ActionOutcome(false, steam.FailureText);
    }

    public Task<ActionOutcome> LaunchRedengineAsync(
        CyberpunkLaunchMode mode,
        CancellationToken ct = default,
        bool withVortex = false,
        bool withMods = false) =>
        RunCliAsync(start =>
        {
            start.ArgumentList.Add("cyberpunk-launch");
            start.ArgumentList.Add("--mode");
            start.ArgumentList.Add(mode is CyberpunkLaunchMode.Vr ? "vr" : "flat");
            if (withVortex)
                start.ArgumentList.Add("--with-vortex");
            else if (withMods)
                start.ArgumentList.Add("--with-mods");
            start.ArgumentList.Add("--activation-source");
            start.ArgumentList.Add("vrclient-app");
        }, ct);

    /// Run one verb of the vrclient command-line tool and report its RESULT line.
    /// These launches live in the CLI because they wait on, and attach to, the game process.
    private async Task<ActionOutcome> RunCliAsync(Action<ProcessStartInfo> addArguments, CancellationToken ct)
    {
        var cli = FindWrapperExe();
        if (cli is null)
            return new ActionOutcome(false, "vrclient.exe was not found beside the app.");

        using var process = new Process();
        process.StartInfo = new ProcessStartInfo
        {
            FileName = cli,
            WorkingDirectory = _repoRoot,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        addArguments(process.StartInfo);
        process.Start();
        var stdoutTask = process.StandardOutput.ReadToEndAsync(ct);
        var stderrTask = process.StandardError.ReadToEndAsync(ct);
        try
        {
            await process.WaitForExitAsync(ct);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            throw;
        }
        var output = $"{await stdoutTask}\n{await stderrTask}";
        var result = output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .LastOrDefault(line => line.StartsWith("RESULT:", StringComparison.Ordinal))
            ?.Trim() ?? output.ReplaceLineEndings(" ").Trim();
        return new ActionOutcome(process.ExitCode == 0, result);
    }

    public RuntimeStatusView GetRuntimeStatus(
        IReadOnlyCollection<string>? runningProcessNames = null,
        IReadOnlyList<XrRuntimeChoice>? runtimes = null)
    {
        var running = runningProcessNames ?? OpenXrRuntimeSelector.GetRunningVrProcessNames();
        var available = runtimes ?? OpenXrRuntimeSelector.DiscoverAvailableRuntimes();
        var pick = OpenXrRuntimeSelector.Select(running, available);
        return new RuntimeStatusView(
            OpenXrRuntimeSelector.IsSteamVrRunning(running),
            OpenXrRuntimeSelector.IsVirtualDesktopRunning(running),
            pick?.Name ?? "system default");
    }

    public SafetyVerdict EvaluateSafety(string slug)
        => new SafetyGate().Evaluate(GameConfigPath(slug), RulesPath);

    /// resolve (if no lockfile) -> download+verify -> install -> configure. Off the UI thread.
    public async Task<ActionOutcome> ConvertAsync(
        string slug, string gameDir, IThunderstoreApi api, IHttpDownloader downloader,
        string resolvedAtUtc, bool installLaunchOption = false, CancellationToken ct = default)
    {
        var verdict = EvaluateSafety(slug);
        if (verdict.Verdict.IsBlocked())
            return new ActionOutcome(false, $"blocked: {verdict.ReasonCode}");

        Lockfile lockfile;
        if (File.Exists(LockfilePath(slug)))
        {
            lockfile = LockfileIo.Read(LockfilePath(slug));
        }
        else
        {
            var spec = ModpackResolver.LoadSpec(ModpackPath(slug));
            var resolved = await new ModpackResolver(api).ResolveAsync(spec, resolvedAtUtc, ct);
            var filled = new List<LockedPackage>();
            foreach (var p in resolved.Packages)
            {
                var bytes = await downloader.GetBytesAsync(p.DownloadUrl, ct);
                filled.Add(p with { Sha256 = Hashing.Sha256OfBytes(bytes) });
            }
            lockfile = resolved with { Packages = filled };
            LockfileIo.Write(LockfilePath(slug), lockfile);
        }

        LockfileIo.RequireHashesFilled(lockfile);
        var cacheDir = Path.Combine(Path.GetTempPath(), "vrclient-cache", slug);
        var zips = await new PackageDownloader(downloader).DownloadAllAsync(lockfile, cacheDir, ct);
        var install = new ModInstaller().Install(zips, gameDir, ReadExecutableNames(slug));
        if (!install.Installed)
            return new ActionOutcome(false, install.RefusalReason ?? "install refused");

        ApplyComfort(slug, gameDir);

        // Per-game opt-in (user directive 2026-07-10): converting THIS game from the
        // library also installs its Steam launch option (wrap integration) - exactly
        // one numeric app id, from this slug's own modpack. Best-effort: a
        // launch-option failure never fails the conversion.
        var launchOptionNote = "";
        if (installLaunchOption)
        {
            launchOptionNote = FindWrapperExe() is { } wrapperExe
                ? "; steam launch option: " + SteamLaunchOptionInstaller.Install(
                    ModpackResolver.LoadSpec(ModpackPath(slug)).SteamAppId,
                    wrapperExe, remove: false, dryRun: false, log: _ => { }).Message
                : "; steam launch option skipped (vrclient.exe not found)";
        }

        return new ActionOutcome(true, $"converted {slug} ({install.InstalledFiles.Count} files){launchOptionNote}");
    }

    /// The wrap-capable CLI exe the Steam launch option must point at. Shipped
    /// layout: vrclient.exe sits beside the app exe; dev layout: the CLI build
    /// output under the repo root.
    private string? FindWrapperExe()
    {
        var beside = Path.Combine(AppContext.BaseDirectory, "vrclient.exe");
        if (File.Exists(beside))
            return beside;
        var dev = Path.Combine(_repoRoot, "client", "VrClient.Cli", "bin", "Release", "net8.0", "vrclient.exe");
        return File.Exists(dev) ? dev : null;
    }

    private static string? SteamManifestBeside(string gameRoot, string appId)
    {
        var steamApps = Directory.GetParent(gameRoot)?.Parent?.FullName;
        if (steamApps is null)
            return null;
        var manifest = Path.Combine(steamApps, $"appmanifest_{appId}.acf");
        return File.Exists(manifest) ? manifest : null;
    }

    /// VR launch of a managed Unity conversion. The catalog's Unity games are x64,
    /// so the runtime comes from the x64 selection rule, and the mod's own cfg pin
    /// is rewritten because RepoXR-family mods ignore XR_RUNTIME_JSON.
    public ActionOutcome LaunchUnityVr(string slug, string gameDir, bool acknowledge, bool dryRun = false)
    {
        var xr = OpenXrRuntimeSelector.SelectLive();
        var verdict = EvaluateSafety(slug);
        var launcher = new GameLauncher();
        var exe = ReadExecutableNames(slug)?.FirstOrDefault();
        var plan = launcher.Plan(gameDir, verdict, new ModInstaller().IsInstalled(gameDir), exe);
        try
        {
            if (!dryRun && plan.ModPresent && ModCfgPath(slug, gameDir) is { } modCfg)
                ModRuntimePin.Apply(modCfg, xr?.JsonPath);
            var pid = launcher.Launch(plan, acknowledge, dryRun, xr);
            return new ActionOutcome(true, dryRun ? "launch plan ok (dry run)" : $"launched pid={pid}");
        }
        catch (Exception ex)
        {
            return new ActionOutcome(false, ex.Message.ReplaceLineEndings(" "));
        }
    }

    private string? ResolveGtaSanAndreasBridge(string? gameDir, bool includeBuildArtifact)
    {
        if (!string.IsNullOrWhiteSpace(gameDir))
        {
            var installed = Path.Combine(gameDir, GtaSanAndreasBridgeFileName);
            if (File.Exists(installed))
                return Path.GetFullPath(installed);
        }

        if (!includeBuildArtifact)
            return null;
        var built = Path.Combine(GtaSanAndreasPayloadDir, GtaSanAndreasBridgeFileName);
        return File.Exists(built) ? Path.GetFullPath(built) : null;
    }

    private bool NativeConfigMatchesRepository(string installedPath)
    {
        try
        {
            return File.Exists(GtaSanAndreasNativeConfigPath) && File.Exists(installedPath) &&
                string.Equals(
                    Hashing.Sha256OfFile(GtaSanAndreasNativeConfigPath),
                    Hashing.Sha256OfFile(installedPath),
                    StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    public ActionOutcome Uninstall(string gameDir, string? slug = null, bool removeLaunchOption = false)
    {
        new ModInstaller().Uninstall(gameDir);

        // Symmetric with ConvertAsync (user directive 2026-07-10): disabling from
        // the library also removes THAT game's Steam launch option. Best-effort.
        var note = "";
        if (removeLaunchOption && slug is not null)
        {
            if (FindWrapperExe() is { } wrapperExe)
            {
                var lo = SteamLaunchOptionInstaller.Install(
                    ModpackResolver.LoadSpec(ModpackPath(slug)).SteamAppId,
                    wrapperExe, remove: true, dryRun: false, log: _ => { });
                note = $"; steam launch option: {lo.Message}";
            }
            else
                note = "; steam launch option untouched (vrclient.exe not found)";
        }
        return new ActionOutcome(true, $"uninstalled{note}");
    }

    private string? ModCfgPath(string slug, string gameDir)
    {
        if (!File.Exists(ComfortMapPath(slug)))
            return null;
        using var doc = JsonDocument.Parse(File.ReadAllText(ComfortMapPath(slug)));
        return doc.RootElement.TryGetProperty("cfg_file", out var cfg) &&
               cfg.GetString() is { Length: > 0 } relative
            ? Path.Combine(gameDir, relative)
            : null;
    }

    private void ApplyComfort(string slug, string gameDir)
    {
        if (!File.Exists(ComfortMapPath(slug)) || !File.Exists(ProfilePath(slug)))
            return;
        var mapping = ComfortConfigMapper.LoadMap(ComfortMapPath(slug));
        if (mapping.CfgKeyByProfileField.Count == 0)
            return;
        var mapper = new ComfortConfigMapper();
        var entries = mapper.BuildCfgEntries(ProfilePath(slug), mapping);
        using var doc = JsonDocument.Parse(File.ReadAllText(ComfortMapPath(slug)));
        var cfgPath = Path.Combine(gameDir, doc.RootElement.GetProperty("cfg_file").GetString()!);
        if (!File.Exists(cfgPath))
            return;
        var (newText, _) = mapper.ApplyToCfg(File.ReadAllText(cfgPath), entries);
        File.WriteAllText(cfgPath, newText);
    }

    private bool UnityHeadsetVerified(string slug)
    {
        var path = GameConfigPath(slug);
        if (!File.Exists(path))
            return false;
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        return !doc.RootElement.TryGetProperty("vr_mod", out var mod) ||
               !mod.TryGetProperty("headset_verified_by_vrclient", out var verified) ||
               verified.ValueKind == JsonValueKind.True;
    }

    /// The display name falls back to the slug when the game config has none.
    private (string DisplayName, string? FirstExe) ReadGameConfigBasics(string slug)
    {
        var path = GameConfigPath(slug);
        if (!File.Exists(path))
            return (slug, null);
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        var root = doc.RootElement;
        var name = root.TryGetProperty("display_name", out var d) && d.ValueKind == JsonValueKind.String
            ? d.GetString()! : slug;
        string? exe = null;
        if (root.TryGetProperty("executable_names", out var exes) && exes.ValueKind == JsonValueKind.Array)
            foreach (var e in exes.EnumerateArray())
                if (e.ValueKind == JsonValueKind.String) { exe = e.GetString(); break; }
        return (name, exe);
    }

    private IReadOnlyList<string>? ReadExecutableNames(string slug)
    {
        var path = GameConfigPath(slug);
        if (!File.Exists(path))
            return null;
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        if (!doc.RootElement.TryGetProperty("executable_names", out var exes) || exes.ValueKind != JsonValueKind.Array)
            return null;
        return exes.EnumerateArray().Where(e => e.ValueKind == JsonValueKind.String).Select(e => e.GetString()!).ToList();
    }
}
