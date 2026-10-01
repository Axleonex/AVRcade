using Avalonia.Media;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using VrClient.App.Services;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Discovery;
using VrClient.Core.Legacy;
using VrClient.Core.Redengine;
using VrClient.Core.Unreal;

namespace VrClient.App.ViewModels;

/// One numbered line of the "VR setup" checklist on a game's page.
public sealed record SetupStepViewModel(int Number, string Title, string Detail, bool Done)
{
    public string Mark => Done ? "✓" : Number.ToString();
    public IBrush MarkBrush => LauncherThemeService.Brush(Done ? "GoodBrush" : "AccentBrush");
}

public sealed class GameItemViewModel : ObservableObject, IDisposable
{
    private enum LibraryStatus { Blocked, LibraryOffline, NotInstalled, VrReady, VrReadyExperimental, SetUpVr }

    private CyberpunkHudMode? _hudMode;
    private GtaTurnPreference _gtaTurnPreference;
    private CyberpunkTurnPreference _cyberpunkTurnPreference;
    private bool _rvHandsEnabled;
    private Func<GameItemViewModel, IReadOnlyList<PlayModeState>>? _playModeSource;
    private IReadOnlyList<PlayModeViewModel> _playModes = [];
    private bool _runtimeDetected;
    private UnrealModInventory _unrealMods = new(string.Empty, []);

    /// coverImage is owned by the window's cover cache, which outlives a catalogue refresh.
    public GameItemViewModel(
        GameView view,
        ControllerReference? controllerReference,
        CyberpunkHudMode? hudMode,
        GtaTurnPreference gtaTurnPreference,
        CyberpunkTurnPreference cyberpunkTurnPreference,
        Bitmap? coverImage)
    {
        View = view;
        ControllerReference = controllerReference;
        _hudMode = hudMode;
        _gtaTurnPreference = gtaTurnPreference;
        _cyberpunkTurnPreference = cyberpunkTurnPreference;
        if (IsRvThereYet)
            _rvHandsEnabled = new RvTrackedHands().Enabled;
        CoverImage = coverImage;
    }

    public GameView View { get; }
    public ModdedGameViewModel? Modded { get; private set; }
    public bool HasModded => Modded is not null;
    public void SetModded(ModdedGameViewModel value) => Modded = value;

    // ---- The four ways to play -------------------------------------------------

    /// The window's view model supplies the rules input, because some of it (mod
    /// manager choice, clean folder, detected mods) lives outside this game row.
    public void SetPlayModeSource(Func<GameItemViewModel, IReadOnlyList<PlayModeState>> source,
        bool runtimeDetected, UnrealModInventory unrealMods)
    {
        _playModeSource = source;
        _runtimeDetected = runtimeDetected;
        _unrealMods = unrealMods;
        RefreshPlayModes();
    }

    public void RefreshPlayModes()
    {
        if (_playModeSource is null) return;
        _playModes = _playModeSource(this).Select(state => new PlayModeViewModel(state)).ToArray();
        OnPropertyChanged(nameof(PlayModes));
        OnPropertyChanged(nameof(HasPlayModes));
        OnPropertyChanged(nameof(IsVrReady));
        OnPropertyChanged(nameof(LibraryStatusText));
        OnPropertyChanged(nameof(LibraryStatusBrush));
    }

    public IReadOnlyList<PlayModeViewModel> PlayModes => _playModes;
    public bool HasPlayModes => _playModes.Count > 0;
    public PlayModeViewModel? PlayModeFor(PlayMode mode) => _playModes.FirstOrDefault(m => m.Mode == mode);
    public bool IsInstalled => View.InstallDir is not null;
    public bool NotInstalled => !IsInstalled;
    public bool IsVrReady => _playModes.Any(mode => mode.IsVr && mode.IsLaunch);
    public UnrealModInventory UnrealMods => _unrealMods;
    public string UnrealModsText => !IsUnreal ? string.Empty
        : _unrealMods.HasMods
            ? "Found in the game folder: " + string.Join(", ", _unrealMods.Files)
            : "No mods found in the game folder.";

    // For Cyberpunk and San Andreas, "Blocked" only means the game build rules out
    // VR; their monitor modes still work, so the library does not call them blocked.
    private LibraryStatus Status =>
        View.Readiness is GameReadiness.Blocked && !IsCyberpunk && !IsGtaSanAndreas ? LibraryStatus.Blocked
        : !IsInstalled ? View.OfflineSteamLibrary is not null ? LibraryStatus.LibraryOffline : LibraryStatus.NotInstalled
        : !IsVrReady ? LibraryStatus.SetUpVr
        : View.Readiness is GameReadiness.Ready ? LibraryStatus.VrReady : LibraryStatus.VrReadyExperimental;
    public string LibraryStatusText => Status switch
    {
        LibraryStatus.Blocked => "Blocked",
        LibraryStatus.LibraryOffline => "Library offline",
        LibraryStatus.NotInstalled => "Not installed",
        LibraryStatus.VrReady => "Ready for VR",
        LibraryStatus.VrReadyExperimental => "VR ready · experimental",
        _ => "Set up VR"
    };
    public IBrush LibraryStatusBrush => LauncherThemeService.Brush(Status switch
    {
        LibraryStatus.VrReady or LibraryStatus.VrReadyExperimental => "GoodBrush",
        LibraryStatus.SetUpVr => "AccentBrush",
        LibraryStatus.Blocked => "DangerBrush",
        _ => "InkSecondaryBrush"
    });
    public double CoverOpacity => IsInstalled ? 1 : 0.55;
    public string SteamStoreUrl => SteamLinks.StorePage(View.SteamAppId);
    /// The player can point AVRcade at a copy it did not find by itself.
    public bool CanLocateGame => HasModded || IsGtaSanAndreas;
    /// Classic San Andreas is no longer sold on Steam, so there is no store page to open.
    public bool HasStorePage => !IsGtaSanAndreas;
    public string NotInstalledText => IsGtaSanAndreas
        ? "AVRcade plays the classic Windows version you already own. Point it at the folder that contains gta_sa.exe."
        : "AVRcade plays the copy you own on Steam; it never downloads games. Install it in Steam, then press Refresh.";

    public IReadOnlyList<SetupStepViewModel> SetupSteps =>
    [
        new(1, IsInstalled ? "Game installed" : "Install the game",
            IsInstalled ? "Found in your Steam library."
                : "AVRcade does not download games. Install it in Steam, then refresh.", IsInstalled),
        VrPieceStep,
        new(3, _runtimeDetected ? "Headset software running" : "Start your headset software",
            _runtimeDetected ? "A VR runtime is running on this PC."
                : "Start SteamVR or Virtual Desktop and connect your headset, then refresh.", _runtimeDetected)
    ];

    private SetupStepViewModel VrPieceStep => View.Engine switch
    {
        GameEngine.Unreal => View.ModInstalled && (!View.ProfileImportRequired || View.ProfileImported)
            ? new(2, "VR tools ready", "UEVR, its runtime and the game's VR profile are downloaded and checked.", true)
            : new(2, "Download the VR setup", "One click below. AVRcade fetches UEVR and this game's VR profile and checks their hashes.", false),
        GameEngine.Redengine => View.ModInstalled
            ? new(2, "VR backend installed", "The RED4ext VR plugins are in the game folder.", true)
            : new(2, "Install the VR backend", "One click below. AVRcade copies its VR plugins into the game folder and downloads the modding frameworks they need (RED4ext, Cyber Engine Tweaks and four others), checking every hash. Frameworks you already have are left alone.", false),
        GameEngine.LegacyD3D9 => View.ModInstalled
            ? new(2, "VR bridge installed", "The stereo bridge and its OpenXR loader sit beside gta_sa.exe.", true)
            : new(2, "Install the VR bridge", "One click below. AVRcade copies its stereo bridge beside gta_sa.exe without replacing game files.", false),
        _ => View.ModInstalled
            ? new(2, "VR mod installed", "AVRcade downloaded the community VR mod and checked its hashes.", true)
            : new(2, "Install the VR mod", "One click below. AVRcade downloads the community VR mod and checks its hashes.", false)
    };

    // ---- Controls, comfort and per-game extras ---------------------------------

    public ControllerReference? ControllerReference { get; }
    public IReadOnlyList<ControllerDeviceReference> ControllerDevices =>
        ControllerReference?.Devices ?? [];
    public bool HasControllerReference => ControllerDevices.Count > 0;
    public bool NoControllerReference => !HasControllerReference;
    public string ControllerReferenceTitle => ControllerReference?.Title ?? "Controller reference";
    public string ControllerGuidance => ControllerReference?.Guidance ?? string.Empty;
    /// Unity games carry their runtime notice on the mod itself; only the UEVR route needs this one.
    public bool ShowStandaloneRuntimeNotice => IsRvThereYet;
    public string RuntimeNoticeTitle => "OpenXR runtime required";
    public string RuntimeNoticeText =>
        "Experimental, unverified UEVR route for the flat game. Connect a headset through VDXR or SteamVR/OpenXR. Controller-driven local hands are opt-in and have not passed a headset test.";
    public bool HasConversionCredit => ControllerReference?.ConversionCredit is not null;
    public string ConversionCreditText => ControllerReference?.ConversionCredit is { } credit
        ? $"{credit.Project} by {credit.Creator}"
        : string.Empty;
    public string? ConversionProjectUrl => ControllerReference?.ConversionCredit?.ProjectUrl;
    public string? ControllerGuideUrl => ControllerReference?.ConversionCredit?.ControlsUrl;
    public string ConversionCreditDescription => HasModded
        ? "AVRcade installs this upstream conversion for VR only; in a mod manager profile you install and update it there. Credit stays with its creator."
        : "AVRcade installs and launches this upstream conversion; credit remains with its creator.";
    public bool HasHudModes => IsCyberpunk && IsInstalled;
    public CyberpunkTurnPreference CyberpunkTurnPreference => _cyberpunkTurnPreference;
    public bool IsCyberpunkSmoothTurn => _cyberpunkTurnPreference.Mode is CyberpunkTurnMode.Smooth;
    public bool IsCyberpunkSnapTurn => !IsCyberpunkSmoothTurn;
    public string CyberpunkSnapAngleText => $"Snap turn angle: {_cyberpunkTurnPreference.SnapDegrees}°";

    public void SetCyberpunkTurnPreference(CyberpunkTurnPreference preference)
    {
        _cyberpunkTurnPreference = preference;
        OnPropertyChanged(nameof(CyberpunkTurnPreference));
        OnPropertyChanged(nameof(IsCyberpunkSmoothTurn));
        OnPropertyChanged(nameof(IsCyberpunkSnapTurn));
        OnPropertyChanged(nameof(CyberpunkSnapAngleText));
    }
    public bool IsFalloutNewVegas => View.Engine is GameEngine.FalloutNewVegas;
    public bool IsFallout3 => View.Engine is GameEngine.Fallout3;
    public bool IsUnreal => View.Engine is GameEngine.Unreal;
    public bool IsCyberpunk => View.Engine is GameEngine.Redengine;
    public bool IsGtaSanAndreas => View.Engine is GameEngine.LegacyD3D9;
    public bool IsRvThereYet => Slug == "rv-there-yet";
    /// Games whose mods are deployed into the game folder by any manager the player likes.
    public bool UsesFolderMods => IsCyberpunk || IsUnreal;
    public GtaTurnPreference GtaTurnPreference => _gtaTurnPreference;
    public bool IsGtaSmoothTurn => _gtaTurnPreference.Mode is GtaTurnMode.Smooth;
    public bool IsGtaSnapTurn => !IsGtaSmoothTurn;
    public string GtaSnapAngleText => $"Snap turn angle: {_gtaTurnPreference.SnapDegrees}°";
    public string GtaSmoothSpeedText => $"Smooth turn speed: {_gtaTurnPreference.SmoothDegreesPerSecond}°/second";

    public void SetGtaTurnPreference(GtaTurnPreference preference)
    {
        _gtaTurnPreference = preference;
        OnPropertyChanged(nameof(GtaTurnPreference));
        OnPropertyChanged(nameof(IsGtaSmoothTurn));
        OnPropertyChanged(nameof(IsGtaSnapTurn));
        OnPropertyChanged(nameof(GtaSnapAngleText));
        OnPropertyChanged(nameof(GtaSmoothSpeedText));
    }
    public bool CanImportUevrProfile => View.ProfileImportRequired && IsInstalled;
    public string RvHandsButtonText => _rvHandsEnabled
        ? "Turn off experimental tracked hands"
        : "Turn on experimental tracked hands";
    public string RvHandsStatusText => _rvHandsEnabled
        ? "Enabled for the next RV VR launch. Only your local first-person hands are affected; controller alignment and game-build compatibility still need headset testing."
        : "Off by default. Normal game hand animation is unchanged.";

    public void SetRvHandsEnabled(bool enabled)
    {
        _rvHandsEnabled = enabled;
        OnPropertyChanged(nameof(RvHandsButtonText));
        OnPropertyChanged(nameof(RvHandsStatusText));
    }
    public string? UevrProfileSourceUrl => View.ProfileSourceUrl;
    public string UevrProfileImportText => View.ProfileImported
        ? "Replace VR profile ZIP"
        : "Import VR profile ZIP";
    public string UevrProfileStatusText => View.ProfileImported
        ? "The reviewed profile is installed and its hashes match."
        : "Download VR setup fetches the reviewed profile straight from its source. ZIP import is an optional fallback.";
    public string HudModeText => _hudMode is { } mode
        ? CyberpunkHudSettings.DisplayName(mode)
        : "Unavailable";

    public void SetHudMode(CyberpunkHudMode mode)
    {
        _hudMode = mode;
        OnPropertyChanged(nameof(HudModeText));
    }

    // ---- Identity, artwork and status ------------------------------------------

    public string Slug => View.Slug;
    public string DisplayName => View.DisplayName;
    public Bitmap? CoverImage { get; }
    public bool HasCoverImage => CoverImage is not null;
    public string CoverMark => DisplayName
        .Split([' ', '-', ':'], StringSplitOptions.RemoveEmptyEntries)
        .Take(2)
        .Aggregate(string.Empty, (mark, word) => mark + char.ToUpperInvariant(word[0]));
    public IBrush CoverBrush => new SolidColorBrush(Color.Parse(Slug switch
    {
        "cyberpunk-2077" => "#8B6B16",
        "rdr2" => "#7D342F",
        "gta-san-andreas" => "#356D61",
        "fallout-new-vegas" => "#66704C",
        "fallout-3" => "#596947",
        "lethal-company" => "#5D557E",
        "repo" => "#39677C",
        "meccha-chameleon" => "#9A5D3F",
        _ => "#426779"
    }));
    public string EngineText => View.Engine switch
    {
        GameEngine.Unity => "UNITY MOD",
        GameEngine.Unreal => "UEVR",
        GameEngine.Redengine => "REDENGINE VR",
        GameEngine.Rage => "RAGE NATIVE",
        GameEngine.LegacyD3D9 => "CLASSIC D3D9",
        GameEngine.FalloutNewVegas => "NATIVE VR EXPERIMENT",
        GameEngine.Fallout3 => "NATIVE VR EXPERIMENT",
        _ => "VR"
    };
    public string RouteText => View.Engine switch
    {
        GameEngine.Unity => "Unity game · community VR mod through BepInEx",
        GameEngine.Unreal when IsRvThereYet => "Unreal game · experimental UEVR profile, not headset-verified",
        GameEngine.Unreal => "Unreal game · UEVR injection",
        GameEngine.Redengine => "REDengine 4 · RED4ext VR backend",
        GameEngine.Rage => "RAGE · native adapter route",
        GameEngine.LegacyD3D9 => "RenderWare / Direct3D 9 · AVRcade's x86 stereo bridge, experimental",
        GameEngine.FalloutNewVegas => "Gamebryo · experimental native stereo; not headset-verified",
        GameEngine.Fallout3 => "Gamebryo · exact-build native stereo; headset proof pending",
        _ => "Unknown engine route"
    };
    public string BadgeText => View.Readiness switch
    {
        GameReadiness.NeedsGame => View.OfflineSteamLibrary is not null ? "Library offline" : "Needs game",
        GameReadiness.NeedsVrMod => "Needs VR mod",
        GameReadiness.NeedsUevr => "Needs UEVR",
        GameReadiness.NeedsHeadset => "Needs headset",
        GameReadiness.ProfileUnverified => "Not headset-verified",
        GameReadiness.Ready => "Ready",
        GameReadiness.Blocked => "Blocked",
        _ => "Unknown"
    };
    public IBrush BadgeBrush => LauncherThemeService.Brush(View.Readiness switch
    {
        GameReadiness.Ready => "GoodBrush",
        GameReadiness.NeedsVrMod or GameReadiness.NeedsUevr => "AccentBrush",
        GameReadiness.NeedsHeadset or GameReadiness.ProfileUnverified => "WarningBrush",
        GameReadiness.Blocked => "DangerBrush",
        _ => "InkSecondaryBrush"
    });
    public string ReadinessDetail => HasModded ? Modded!.ReadinessText : View.ReadinessDetail;
    public string SafetyText => View.SafetyReason switch
    {
        "private_modded_coop_requires_acknowledgement" =>
            "Modded play is for private sessions with friends who agreed to it. AVRcade asks you to confirm before a modded launch.",
        "approved" => "Approved for this launch.",
        "anti_cheat_detected" => "Blocked: anti-cheat detected.",
        "unknown_game" => "Blocked: this game is not in the supported catalogue.",
        "offline_only_ok" => "Single-player, offline only.",
        "cyberpunk_preflight_ready" => "Single-player. The game build matches the supported VR backend.",
        _ => $"{View.SafetyVerdict}: {View.SafetyReason.Replace('_', ' ')}."
    };
    public string InstallText => View.InstallDir ??
        (View.OfflineSteamLibrary is { } library
            ? $"Steam library disconnected: {library}"
            : IsGtaSanAndreas ? "Not found on this PC" : "Not installed on a connected Steam library");
    public string PrepareText => View.Engine switch
    {
        GameEngine.Unreal when IsRvThereYet => "Download VR setup",
        GameEngine.Unreal => "Prepare UEVR",
        GameEngine.Redengine => "Install VR backend",
        GameEngine.LegacyD3D9 => View.ModInstalled ? "Verify VR bridge" : "Install VR bridge",
        GameEngine.FalloutNewVegas => "Set up headset test",
        _ => "Install VR mod"
    };
    public bool IsCyberpunkRedmodMissing => IsCyberpunk && View.InstallDir is { } installDir &&
        !File.Exists(Path.Combine(installDir, "tools", "redmod", "bin", "redmod.exe"));
    public bool CanPrepare => IsInstalled && (HasModded
        ? Modded!.CanPrepareManaged
        : (IsRvThereYet && View.Readiness is not GameReadiness.Blocked) ||
          View.Readiness is GameReadiness.NeedsVrMod or GameReadiness.NeedsUevr ||
          (IsGtaSanAndreas && View.Readiness is GameReadiness.ProfileUnverified));
    public bool CanConfigureFalloutNewVegas => IsFalloutNewVegas && IsInstalled;
    public bool CanLaunchFalloutNewVegas => IsFalloutNewVegas && IsInstalled &&
        View.Readiness is not GameReadiness.Blocked;
    public bool CanLaunchFallout3 => IsFallout3 && IsInstalled &&
        View.Readiness is GameReadiness.ProfileUnverified;
    public bool CanLaunchFallout3Desktop => IsFallout3 && IsInstalled &&
        View.Readiness is not GameReadiness.Blocked;
    /// A modded or VR launch of a Warn-rated game needs the player's explicit
    /// acknowledgement; vanilla play has nothing modded to acknowledge.
    public bool RequiresLaunchAcknowledgement =>
        View.SafetyVerdict.Equals("Warn", StringComparison.OrdinalIgnoreCase);
    public bool CanUninstall => View.Engine is GameEngine.Unity or GameEngine.Redengine && View.ModInstalled;

    public void Dispose() => Modded?.Dispose();
}
