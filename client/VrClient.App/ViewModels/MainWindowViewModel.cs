using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using VrClient.Core;
using VrClient.Core.App;
using VrClient.Core.Discovery;
using VrClient.Core.Legacy;
using VrClient.Core.Model;
using VrClient.Core.Modpack;
using VrClient.Core.ModManagers;
using VrClient.Core.Redengine;
using VrClient.Core.Distribution;
using VrClient.Core.Platform;
using VrClient.Core.ReleaseDiagnostics;
using VrClient.Core.ReleaseProfiles;
using VrClient.Core.Unreal;
using VrClient.App.Services;

namespace VrClient.App.ViewModels;

public partial class MainWindowViewModel : ObservableObject, IDisposable
{
    private readonly AppController? _controller;
    private readonly ReleasePackageStore? _releasePackageStore;
    private readonly ReleaseProfileStore? _releaseProfileStore;
    private readonly FalloutNewVegasDesktopService? _falloutNewVegasService;
    private readonly ModManagerDiscovery _modManagerDiscovery = new();
    private readonly ModdedVrPreferences _moddedPreferences = new();
    private readonly GtaSanAndreasModManagerSettings _gtaModManagerSettings = new();
    private readonly GameModManagerSettings _gameModManagers = new();
    private readonly Func<string, string?> _coverLocator;
    // Decoded cover art survives a catalogue refresh; only this view model disposes it.
    private readonly Dictionary<string, Bitmap> _covers = new();
    private IReadOnlyList<ModManagerInstall> _installedManagers = [];

    public ObservableCollection<GameItemViewModel> Games { get; } = new();
    public ObservableCollection<GameItemViewModel> FilteredGames { get; } = new();

    [ObservableProperty] private GameItemViewModel? _selectedGame;
    [ObservableProperty] private string _statusText = "Ready.";
    [ObservableProperty] private string _runtimeText = "Runtime: unknown";
    [ObservableProperty] private string _runtimeIndicatorText = "Checking for a headset";
    [ObservableProperty] private bool _runtimeOnline;
    [ObservableProperty] private string _releaseToolingText = "Release tooling: checking";
    [ObservableProperty] private string _releaseToolingDetail = "Checking local release prerequisites.";
    [ObservableProperty] private bool _busy;
    [ObservableProperty] private bool _isDarkTheme;
    [ObservableProperty] private string _searchText = string.Empty;
    [ObservableProperty] private bool _showGameDetail;
    [ObservableProperty] private bool _showSettings;
    [ObservableProperty] private string _libraryFilter = "all";
    /// 0 = VR setup, 1 = Mods, 2 = Controls, 3 = About.
    [ObservableProperty] private int _detailTabIndex;
    // Facts about the selected game's mod tools, read from disk once per selection.
    [ObservableProperty] private string _gtaVanillaFolderText = string.Empty;
    [ObservableProperty] private string _gtaModManagerText = string.Empty;
    [ObservableProperty] private string _gtaVrGameFolderText = string.Empty;
    [ObservableProperty] private string _gameModManagerText = string.Empty;
    [ObservableProperty] private bool _canOpenGameModManager;

    public IReadOnlyList<LauncherPalette> ThemePalettes => LauncherThemeService.Current.AvailablePalettes;
    public string ThemeModeText => IsDarkTheme ? "Switch to light" : "Switch to dark";
    public string ThemePaletteText => LauncherThemeService.Current.SelectedPalette.Name;

    public bool HasGames => Games.Count > 0;
    public bool HasFilteredGames => FilteredGames.Count > 0;
    public bool ShowLibrary => !ShowGameDetail && !ShowSettings;
    public bool ShowBack => !ShowLibrary;
    public bool RuntimeOffline => !RuntimeOnline;
    public bool ShowHeadsetHint => ShowLibrary && !RuntimeOnline && HasGames;
    public bool IsFilterAll => LibraryFilter == "all";
    public bool IsFilterInstalled => LibraryFilter == "installed";
    public bool IsFilterReady => LibraryFilter == "ready";
    public string FilterAllText => $"All {Games.Count}";
    public string FilterInstalledText => $"Installed {Games.Count(game => game.IsInstalled)}";
    public string FilterReadyText => $"VR ready {Games.Count(game => game.IsVrReady)}";
    public string EmptyLibraryTitle => Games.Count == 0 ? "No games in the catalogue"
        : string.IsNullOrWhiteSpace(SearchText) ? "Nothing here yet" : "No games match this search";
    public string EmptyLibraryText => Games.Count == 0
        ? "AVRcade could not read its game catalogue. Reinstall AVRcade or refresh."
        : IsFilterInstalled ? "None of the supported games are installed on this PC. Install one in Steam, then refresh."
        : IsFilterReady ? "No game is ready for VR yet. Open an installed game and follow its VR setup."
        : "Try another title, or show all games.";
    private string CatalogueSummaryText => Games.Count == 0
        ? "No supported games found."
        : $"{Games.Count} supported games. {Games.Count(game => game.IsInstalled)} installed on this PC.";

    partial void OnSelectedGameChanged(GameItemViewModel? value) => UpdateSelectedGameFacts();

    private void UpdateSelectedGameFacts()
    {
        var game = SelectedGame;
        if (game?.IsGtaSanAndreas == true)
        {
            GtaVrGameFolderText = game.View.InstallDir is { } folder
                ? $"VR game folder: {folder}" : "VR game folder has not been found.";
            GtaVanillaFolderText = DescribeGtaVanillaFolder(game);
            GtaModManagerText = DescribeGtaModManager();
        }
        var executable = GameModManagerExecutable(game);
        CanOpenGameModManager = executable is not null;
        GameModManagerText = game is not { UsesFolderMods: true } ? string.Empty
            : _gameModManagers.Load(game.Slug) is { } chosen
                ? $"Your mod manager: {Path.GetFileNameWithoutExtension(chosen)} ({chosen})"
            : executable is not null
                ? $"Using Vortex, found at {executable}. Choose another manager if you prefer."
            : "No mod manager chosen yet. Any manager that installs mods into the game folder works.";
    }

    private string DescribeGtaVanillaFolder(GameItemViewModel game)
    {
        var folder = _controller?.GetGtaSanAndreasVanillaFolder();
        if (string.IsNullOrWhiteSpace(folder)) return "No clean folder chosen yet.";
        var error = GtaSanAndreasVanillaSettings.Validate(folder, game.View.InstallDir);
        return error is null ? $"Clean game folder: {folder}" : $"Clean folder unavailable: {error}";
    }

    private string DescribeGtaModManager()
    {
        var selection = _gtaModManagerSettings.LoadSelection();
        if (selection is null) return "No mod manager chosen yet.";
        var error = GtaSanAndreasModManagerSettings.Validate(selection.Executable, selection.Kind);
        if (error is not null) return $"Chosen manager unavailable: {error}";
        var name = selection.Kind switch
        {
            GtaSanAndreasModManagerKind.Ggmm => "GGMM",
            GtaSanAndreasModManagerKind.Sami => "SAMI",
            _ => Path.GetFileNameWithoutExtension(selection.Executable)
        };
        return $"Your mod manager: {name} ({selection.Executable})";
    }

    /// The manager AVRcade opens for a game whose mods live in the game folder:
    /// the player's own choice, else a detected Vortex.
    private string? GameModManagerExecutable(GameItemViewModel? game) =>
        game is not { UsesFolderMods: true } ? null
        : _gameModManagers.Load(game.Slug) ??
          _installedManagers.FirstOrDefault(manager => manager.Kind == ModManagerKind.Vortex)?.LaunchPath;

    partial void OnSearchTextChanged(string value) => ApplyFilter();
    partial void OnLibraryFilterChanged(string value)
    {
        OnPropertyChanged(nameof(IsFilterAll));
        OnPropertyChanged(nameof(IsFilterInstalled));
        OnPropertyChanged(nameof(IsFilterReady));
        ApplyFilter();
    }

    partial void OnRuntimeOnlineChanged(bool value)
    {
        OnPropertyChanged(nameof(RuntimeOffline));
        OnPropertyChanged(nameof(ShowHeadsetHint));
    }

    partial void OnShowGameDetailChanged(bool value)
    {
        if (value)
            ShowSettings = false;
        RaiseSurface();
    }

    partial void OnShowSettingsChanged(bool value)
    {
        if (value)
            ShowGameDetail = false;
        RaiseSurface();
    }

    private void RaiseSurface()
    {
        OnPropertyChanged(nameof(ShowLibrary));
        OnPropertyChanged(nameof(ShowBack));
        OnPropertyChanged(nameof(ShowHeadsetHint));
    }

    /// coverLocator maps a Steam app id to a local cover image; tooling that must
    /// not show publisher artwork passes one that finds nothing.
    public MainWindowViewModel(Func<string, string?>? coverLocator = null)
    {
        _coverLocator = coverLocator ?? (appId => SteamLibraryArtworkLocator.FindPortrait(appId));
        IsDarkTheme = LauncherThemeService.Current.IsDark;
        var root = RepoRoot.Find();
        if (root is null)
        {
            StatusText = "Could not find the AVRcade configuration folder.";
            return;
        }
        _controller = new AppController(root);
        _falloutNewVegasService = new FalloutNewVegasDesktopService(root);
        var releaseDataRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "VRClient",
            "release-foundation");
        _releasePackageStore = new ReleasePackageStore(releaseDataRoot);
        _releaseProfileStore = new ReleaseProfileStore(releaseDataRoot);
        RefreshCatalogue(announce: true);
    }

    public FalloutNewVegasSetupViewModel? CreateFalloutNewVegasSetupViewModel() =>
        _falloutNewVegasService is null ? null : new FalloutNewVegasSetupViewModel(_falloutNewVegasService);

    public void ImportSelectedUevrProfile(string archivePath)
    {
        if (_controller is null || SelectedGame is null || !SelectedGame.CanImportUevrProfile)
            return;
        StatusText = $"Checking and importing the VR profile for {SelectedGame.DisplayName}…";
        var outcome = _controller.ImportUnrealProfile(SelectedGame.Slug, archivePath);
        RefreshCatalogue(announce: false);
        StatusText = outcome.Message;
    }

    // Theme changes recolour the shared brushes in place, so nothing is reloaded.
    [RelayCommand]
    private void ToggleTheme()
    {
        LauncherThemeService.Current.ToggleMode();
        IsDarkTheme = LauncherThemeService.Current.IsDark;
        OnPropertyChanged(nameof(ThemeModeText));
    }

    [RelayCommand]
    private void SelectPalette(string? paletteId)
    {
        LauncherThemeService.Current.SetPalette(paletteId);
        OnPropertyChanged(nameof(ThemePaletteText));
    }

    [RelayCommand]
    private void SetLibraryFilter(string? filter) =>
        LibraryFilter = filter is "installed" or "ready" ? filter : "all";

    [RelayCommand]
    private async Task OpenGame(GameItemViewModel? game)
    {
        if (game is null)
            return;
        SelectedGame = game;
        DetailTabIndex = 0;
        ShowGameDetail = true;
        if (game.Modded is not null)
            await game.Modded.LoadPreviewAsync();
    }

    [RelayCommand]
    private void BackToLibrary()
    {
        ShowGameDetail = false;
        ShowSettings = false;
    }

    [RelayCommand]
    private void ShowSettingsPage() => ShowSettings = true;

    [RelayCommand]
    private void Refresh() => RefreshCatalogue(announce: true);

    /// Remember where a game the scan missed is installed (Unity games and San Andreas).
    public void RememberGameFolder(string folder)
    {
        if (_controller is null || SelectedGame is not { CanLocateGame: true }) return;
        var result = SelectedGame.IsGtaSanAndreas
            ? _controller.RememberGtaSanAndreasFolder(folder)
            : _controller.RememberFriendslopGameFolder(SelectedGame.Slug, folder);
        if (result.Ok) RefreshCatalogue(announce: false);
        StatusText = result.Message;
    }

    private void RefreshCatalogue(bool announce)
    {
        if (_controller is null)
            return;
        var selectedSlug = SelectedGame?.Slug;
        var selectedManagerKind = SelectedGame?.Modded?.SelectedManager?.Kind;
        SelectedGame = null;
        foreach (var game in Games)
            game.Dispose();
        Games.Clear();

        // Sampled once per refresh and shared by every game below.
        var runtime = _controller.GetRuntimeStatus();
        var runtimeDetected = runtime.SteamVrRunning || runtime.VirtualDesktopRunning;
        var gtaTurnPreference = _controller.GetGtaSanAndreasTurnPreference();
        var dataRoots = _moddedPreferences.LoadDataRoots();
        _installedManagers = _modManagerDiscovery.FindInstalled(dataRoots);

        foreach (var game in _controller.ListGames(runtimeDetected)
                     .OrderByDescending(game => game.InstallDir is not null)
                     .ThenBy(game => game.DisplayName, StringComparer.OrdinalIgnoreCase))
        {
            var item = new GameItemViewModel(
                game,
                _controller.GetControllerReference(game.Slug),
                _controller.GetHudMode(game),
                gtaTurnPreference,
                _controller.GetCyberpunkTurnPreference(game),
                CoverFor(game.SteamAppId));
            if (CommunityVrRoutes.All.TryGetValue(game.Slug, out var route))
            {
                var modded = new ModdedGameViewModel(_controller.RepoRoot, route, game.InstallDir,
                    game.InstallDir is not null, game.Readiness is not GameReadiness.Blocked,
                    game.OfflineSteamLibrary is not null,
                    runtimeDetected, runtime.SteamVrRunning, game.ModInstalled,
                    _modManagerDiscovery, _installedManagers, dataRoots, _moddedPreferences,
                    item.RefreshPlayModes);
                item.SetModded(modded);
                if (game.Slug == selectedSlug && selectedManagerKind is { } kind &&
                    _installedManagers.FirstOrDefault(manager => manager.Kind == kind) is { } manager)
                    modded.SelectedManager = manager;
            }
            item.SetPlayModeSource(
                source => BuildPlayModes(source, runtimeDetected), runtimeDetected, _controller.GetUnrealMods(game));
            Games.Add(item);
        }
        ApplyFilter();
        OnPropertyChanged(nameof(FilterAllText));
        OnPropertyChanged(nameof(FilterInstalledText));
        OnPropertyChanged(nameof(FilterReadyText));

        RuntimeText = $"SteamVR: {(runtime.SteamVrRunning ? "running" : "off")}   " +
                      $"Virtual Desktop: {(runtime.VirtualDesktopRunning ? "running" : "off")}   " +
                      $"Launch uses: {runtime.SelectedRuntime}";
        RuntimeOnline = runtimeDetected;
        RuntimeIndicatorText = runtimeDetected
            ? $"VR ready · {runtime.SelectedRuntime}"
            : "Headset not connected";
        OnPropertyChanged(nameof(ShowHeadsetHint));
        UpdateReleaseTooling(runtimeDetected);
        if (announce)
            StatusText = CatalogueSummaryText;
        SelectedGame = Games.FirstOrDefault(game => game.Slug == selectedSlug)
            ?? (Games.Count > 0 ? Games[0] : null);
    }

    /// Steam's cached cover for a game, decoded once. A missing or unreadable file
    /// is looked for again on the next refresh; the typographic cover shows meanwhile.
    private Bitmap? CoverFor(string steamAppId)
    {
        if (_covers.TryGetValue(steamAppId, out var cached))
            return cached;
        if (_coverLocator(steamAppId) is not { } path)
            return null;
        try
        {
            return _covers[steamAppId] = new Bitmap(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
                                   ArgumentException or InvalidOperationException)
        {
            return null;
        }
    }

    /// Which of the four play modes can start right now for this game.
    private IReadOnlyList<PlayModeState> BuildPlayModes(GameItemViewModel item, bool runtimeDetected)
    {
        var view = item.View;
        var installed = view.InstallDir is not null;
        var allowed = view.Readiness is not GameReadiness.Blocked;
        switch (view.Engine)
        {
            case GameEngine.Unity:
                return PlayModePlanner.Unity(item.Modded?.PlayFacts() ?? new UnityPlayFacts(
                    installed, allowed, view.ModInstalled,
                    view.ModInstalled && installed && ManagedModIsolation.IsVrOnly(view.InstallDir!),
                    null, null, false, runtimeDetected, false, false));
            case GameEngine.Unreal:
                return PlayModePlanner.Unreal(new UnrealPlayFacts(
                    installed, allowed, view.ModInstalled,
                    !view.ProfileImportRequired || view.ProfileImported,
                    item.UnrealMods.Files.Count, runtimeDetected));
            case GameEngine.Redengine:
                return PlayModePlanner.Cyberpunk(new CyberpunkPlayFacts(
                    installed,
                    allowed ? null
                        : view.SafetyReason == nameof(CyberpunkNativePreflightStatus.ConflictingHooks)
                            ? "Another VR or graphics hook (dxgi.dll or d3d12.dll) is in the game folder. Remove it first"
                            : "This game build does not match the supported VR backend",
                    view.ModInstalled, runtimeDetected,
                    _controller?.CyberpunkBackendInstallable == true));
            case GameEngine.LegacyD3D9:
                return PlayModePlanner.GtaSanAndreas(new GtaPlayFacts(
                    installed,
                    allowed ? null : view.ReadinessDetail,
                    view.ModInstalled,
                    GtaSanAndreasVanillaSettings.Validate(
                        _controller?.GetGtaSanAndreasVanillaFolder(), view.InstallDir) is null,
                    runtimeDetected));
            default:
                // Deferred engines keep their own experimental panels.
                return [];
        }
    }

    private void ApplyFilter()
    {
        var query = SearchText.Trim();
        var matches = Games.Where(game =>
            (LibraryFilter != "installed" || game.IsInstalled) &&
            (LibraryFilter != "ready" || game.IsVrReady) &&
            (string.IsNullOrEmpty(query) ||
             game.DisplayName.Contains(query, StringComparison.OrdinalIgnoreCase) ||
             game.EngineText.Contains(query, StringComparison.OrdinalIgnoreCase) ||
             game.LibraryStatusText.Contains(query, StringComparison.OrdinalIgnoreCase))).ToArray();

        FilteredGames.Clear();
        foreach (var game in matches)
            FilteredGames.Add(game);
        OnPropertyChanged(nameof(HasFilteredGames));
        OnPropertyChanged(nameof(EmptyLibraryTitle));
        OnPropertyChanged(nameof(EmptyLibraryText));
    }

    private void UpdateReleaseTooling(bool runtimeAvailable)
    {
        if (_releasePackageStore is null || _releaseProfileStore is null)
            return;

        var packageId = "vrclient-shared-controls";
        var packageState = _releasePackageStore.ReadState(packageId);
        var health = _releasePackageStore.CheckHealth(packageId);
        var doctor = ReleaseDoctor.Evaluate(new ReleaseDoctorInput(
            runtimeAvailable,
            health,
            packageState.InstalledVersions.Count > 1,
            WriteLocationAvailable: true));
        var preferences = _releaseProfileStore.Load("global");
        var platform = ReleasePlatformCompatibility.Evaluate(
            new HostPlatformInfo(HostPlatform.Windows, IsSteamOs: false, runtimeAvailable),
            new PackagePlatformSupport(Windows: true, NativeLinux: false, Proton: false));

        ReleaseToolingText = doctor.CanLaunch ? "Release tooling ready" : "Release tooling local-only";
        ReleaseToolingDetail = $"{platform.ReasonCode}; input preference: {preferences.Controller}; " +
                               $"package state: {health.ToString().ToLowerInvariant()}.";
    }

    private sealed class HttpDownloader(HttpClient http) : IHttpDownloader
    {
        public async Task<byte[]> GetBytesAsync(string url, CancellationToken ct = default)
            => await http.GetByteArrayAsync(url, ct);
    }

    private static HttpClient NewHttp()
    {
        var http = new HttpClient();
        http.DefaultRequestHeaders.UserAgent.ParseAdd("vrclient-app/0.3");
        return http;
    }

    [RelayCommand]
    private async Task PrepareAsync()
    {
        if (_controller is null || SelectedGame is not { } game) return;
        var dir = game.View.InstallDir;
        if (string.IsNullOrEmpty(dir))
        {
            StatusText = "Game is not installed on this PC.";
            return;
        }

        var controller = _controller;
        var slug = game.Slug;
        Busy = true;
        var finalStatus = $"Finished preparing {game.DisplayName}.";
        StatusText = game.View.Engine switch
        {
            GameEngine.Unreal => $"Downloading and checking the VR setup for {game.DisplayName}…",
            GameEngine.Redengine => $"Downloading and installing the VR backend for {game.DisplayName}…",
            GameEngine.LegacyD3D9 => $"Checking the VR bridge for {game.DisplayName}…",
            _ => $"Installing the VR mod for {game.DisplayName}…"
        };
        try
        {
            using var http = NewHttp();
            ActionOutcome outcome;
            if (game.View.Engine is GameEngine.Unreal)
            {
                outcome = await controller.PrepareUnrealAsync(slug, new HttpDownloader(http));
                // A game whose VR profile is not redistributable has it fetched from its source.
                if (outcome.Ok && game.View.ProfileImportRequired)
                    outcome = await controller.DownloadUnrealProfileAsync(slug, new HttpDownloader(http));
            }
            else if (game.View.Engine is GameEngine.Redengine)
            {
                outcome = await controller.InstallCyberpunkVrAsync(dir, new HttpDownloader(http));
            }
            else if (game.View.Engine is GameEngine.LegacyD3D9)
            {
                outcome = game.View.ModInstalled
                    ? controller.CheckGtaSanAndreas(dir)
                    : controller.InstallGtaSanAndreasTheater(dir);
            }
            else
            {
                // Unpacking the mod and setting the Steam launch option can take a
                // while (Steam is restarted), so none of it runs on the UI thread.
                var stamp = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
                outcome = await Task.Run(() => controller.ConvertAsync(
                    slug, dir, new LiveThunderstoreApi(http), new HttpDownloader(http), stamp,
                    installLaunchOption: true));
            }
            finalStatus = outcome.Message;
        }
        catch (Exception ex)
        {
            finalStatus = $"Preparation failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
            RefreshCatalogue(announce: false);
            StatusText = finalStatus;
        }
    }

    /// Start the selected game in one of the four play modes. `acknowledge` is the
    /// player's answer to the private-session warning; it is never assumed here.
    public async Task RunPlayModeAsync(PlayMode mode, bool acknowledge)
    {
        if (_controller is null || SelectedGame is not { } game || Busy) return;
        if (game.PlayModeFor(mode) is not { IsLaunch: true })
        {
            StatusText = $"{game.DisplayName}: this way to play is not ready yet. {game.PlayModeFor(mode)?.Status}";
            return;
        }
        if (game.View.InstallDir is not { } dir)
        {
            StatusText = "Game is not installed on this PC.";
            return;
        }

        Busy = true;
        var finalStatus = $"Finished launching {game.DisplayName}.";
        StatusText = $"Starting {game.DisplayName}…";
        try
        {
            var outcome = game.View.Engine switch
            {
                GameEngine.Unity => LaunchUnity(game, dir, mode, acknowledge),
                GameEngine.Unreal => mode is PlayMode.VrOnly or PlayMode.VrWithMods
                    ? await _controller.LaunchUnrealAsync(game.Slug, acknowledge)
                    : _controller.LaunchUnrealFlat(game.Slug),
                GameEngine.Redengine => await LaunchCyberpunkAsync(game, mode),
                GameEngine.LegacyD3D9 => LaunchGtaSanAndreas(dir, mode),
                _ => new ActionOutcome(false, "This game has no launch route in AVRcade yet.")
            };
            finalStatus = outcome.Message;
        }
        catch (Exception ex)
        {
            finalStatus = $"Launch failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
            RefreshCatalogue(announce: false);
            StatusText = finalStatus;
        }
    }

    private ActionOutcome LaunchUnity(GameItemViewModel game, string dir, PlayMode mode, bool acknowledge)
    {
        Enum.TryParse<Verdict>(game.View.SafetyVerdict, out var safety);
        var modded = game.Modded;
        var launcher = new ModdedVrLauncher();
        static ActionOutcome Outcome(ModdedLaunchResult result) =>
            new(result.GameLaunchRequested || result.ManagerOpened, result.Message);

        switch (mode)
        {
            case PlayMode.VrOnly:
                // The folder is checked again here: a mod may have been added since the page was drawn.
                return ManagedModIsolation.IsVrOnly(dir)
                    ? _controller!.LaunchUnityVr(game.Slug, dir, acknowledge)
                    : new ActionOutcome(false,
                        "VR only refused: the game folder now contains other mods. Refresh, then use VR + mods.");
            case PlayMode.VrWithMods when modded?.SelectedProfile is { } profile:
                // Runtime detection only knows SteamVR and Virtual Desktop, so the
                // launch itself is not refused when neither is seen.
                return Outcome(launcher.Launch(profile, modded.SavedR2LaunchArguments,
                    safety, acknowledge, headsetConnected: true,
                    mode: ModdedLaunchMode.VrWithMods, gameDirectory: dir));
            case PlayMode.VrWithMods:
                return _controller!.LaunchUnityVr(game.Slug, dir, acknowledge);
            case PlayMode.ModsNoVr when modded?.SelectedFlatProfile is { } flatProfile:
                return Outcome(launcher.Launch(flatProfile, null,
                    safety, acknowledge, headsetConnected: false,
                    mode: ModdedLaunchMode.DesktopWithMods, gameDirectory: dir));
            case PlayMode.Vanilla:
                return Outcome(launcher.LaunchVanilla(game.Slug, safety, acknowledge));
            default:
                return new ActionOutcome(false, "Choose a mod profile for this way to play first.");
        }
    }

    private async Task<ActionOutcome> LaunchCyberpunkAsync(GameItemViewModel game, PlayMode mode)
    {
        var withMods = mode is PlayMode.VrWithMods or PlayMode.ModsNoVr;
        if (mode is PlayMode.VrOnly or PlayMode.VrWithMods)
            return await _controller!.LaunchRedengineAsync(CyberpunkLaunchMode.Vr, withMods: withMods);
        // With the VR backend installed AVRcade starts the game itself, VR switched
        // off; without it a normal Steam start is the only route.
        return game.View.ModInstalled && game.View.Readiness is not GameReadiness.Blocked
            ? await _controller!.LaunchRedengineAsync(CyberpunkLaunchMode.Flat, withMods: withMods)
            : _controller!.LaunchCyberpunkFlatViaSteam(withMods);
    }

    private ActionOutcome LaunchGtaSanAndreas(string dir, PlayMode mode) => mode switch
    {
        PlayMode.VrOnly => _controller!.LaunchGtaSanAndreasVr(dir, GtaSanAndreasVrMods.Clean),
        PlayMode.VrWithMods => _controller!.LaunchGtaSanAndreasVr(dir, GtaSanAndreasVrMods.Existing),
        PlayMode.ModsNoVr => _controller!.LaunchGtaSanAndreasDesktop(dir),
        _ => _controller!.LaunchGtaSanAndreasVanilla(dir)
    };

    [RelayCommand]
    private void ToggleRvHands()
    {
        if (SelectedGame?.IsRvThereYet != true || Busy) return;
        try
        {
            var hands = new RvTrackedHands();
            var enabled = !hands.Enabled;
            hands.SetEnabled(enabled);
            SelectedGame.SetRvHandsEnabled(enabled);
            StatusText = enabled
                ? "Experimental RV hands enabled for the next VR launch. They still need a headset test."
                : "Experimental RV hands disabled. Normal hand animation will be used.";
        }
        catch (Exception ex)
        {
            StatusText = $"Could not change RV hand tracking: {ex.Message}";
        }
    }

    [RelayCommand]
    private void LaunchFallout3Native() => LaunchFallout3Vr(requireVortex: false);

    [RelayCommand]
    private void LaunchFallout3Vortex() => LaunchFallout3Vr(requireVortex: true);

    private void LaunchFallout3Vr(bool requireVortex)
    {
        if (_controller is null || SelectedGame?.CanLaunchFallout3 != true ||
            SelectedGame.View.InstallDir is not { } installDir)
            return;
        Busy = true;
        try
        {
            StatusText = _controller.LaunchFallout3Native(installDir, requireVortex).Message;
        }
        catch (Exception ex)
        {
            StatusText = $"Fallout 3 native launch failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
        }
    }

    [RelayCommand]
    private void LaunchFallout3Desktop()
    {
        if (_controller is null || SelectedGame?.IsFallout3 != true ||
            SelectedGame.View.InstallDir is not { } installDir)
            return;
        Busy = true;
        try
        {
            StatusText = _controller.LaunchFallout3Desktop(installDir).Message;
        }
        catch (Exception ex)
        {
            StatusText = $"Fallout 3 desktop launch failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
        }
    }

    [RelayCommand]
    private void LaunchFalloutNewVegasNative()
    {
        if (_falloutNewVegasService is null || SelectedGame?.CanLaunchFalloutNewVegas != true)
            return;
        Busy = true;
        try
        {
            _falloutNewVegasService.StartNativeTest(_falloutNewVegasService.LoadAndDetect());
            StatusText = "New Vegas native VR test opened. Keep Steam running; wait for the game menu, then verify it in the headset.";
        }
        catch (Exception ex) { StatusText = $"New Vegas native VR could not start: {ex.Message}"; }
        finally { Busy = false; }
    }

    [RelayCommand]
    private async Task LaunchFalloutNewVegasVortexAsync()
    {
        if (_falloutNewVegasService is null || SelectedGame?.CanLaunchFalloutNewVegas != true)
            return;
        Busy = true;
        try { StatusText = (await LaunchFalloutNewVegasVortexCoreAsync()).Message; }
        catch (Exception ex) { StatusText = $"New Vegas VR with Vortex could not start: {ex.Message}"; }
        finally { Busy = false; }
    }

    private async Task<ActionOutcome> LaunchFalloutNewVegasVortexCoreAsync()
    {
        if (_falloutNewVegasService is null)
            throw new InvalidOperationException("Fallout: New Vegas setup is unavailable.");
        var settings = _falloutNewVegasService.LoadAndDetect();
        if (!settings.UseVortexSync)
            return new ActionOutcome(false,
                "Apply the one-way Vortex sync in Set up New Vegas VR and Vortex sync before this launch.");
        var report = await Task.Run(() => _falloutNewVegasService.Check(settings));
        if (!report.Ready)
        {
            var failures = report.Checks
                .Where(check => check.Severity == VrClient.Core.FalloutNewVegas.FnvCheckSeverity.Failure)
                .Select(check => check.Message);
            return new ActionOutcome(false,
                $"New Vegas VR with Vortex is not ready: {string.Join(" ", failures)} Open VR setup for the full checklist.");
        }
        report = await Task.Run(() => _falloutNewVegasService.Launch(settings));
        return new ActionOutcome(report.Ready,
            report.Ready
                ? "New Vegas VR with Vortex launch requested through MO2 and native OpenXR. Verify it in the headset."
                : "New Vegas readiness changed before launch; open VR setup and recheck.");
    }

    [RelayCommand]
    private void SetGtaTurnMode(string mode)
    {
        if (_controller is null || SelectedGame?.IsGtaSanAndreas != true) return;
        var selectedMode = mode.Equals("toggle", StringComparison.OrdinalIgnoreCase)
            ? SelectedGame.IsGtaSmoothTurn ? GtaTurnMode.Snap : GtaTurnMode.Smooth
            : mode.Equals("smooth", StringComparison.OrdinalIgnoreCase)
                ? GtaTurnMode.Smooth : GtaTurnMode.Snap;
        SaveGtaTurnPreference(SelectedGame.GtaTurnPreference with { Mode = selectedMode });
    }

    [RelayCommand]
    private void SetGtaSmoothTurnSpeed(string degreesPerSecond)
    {
        if (SelectedGame?.IsGtaSanAndreas != true ||
            !int.TryParse(degreesPerSecond, out var speed)) return;
        SaveGtaTurnPreference(SelectedGame.GtaTurnPreference with
        {
            SmoothDegreesPerSecond = speed
        });
    }

    [RelayCommand]
    private void SetGtaSnapTurnAngle(string degrees)
    {
        if (SelectedGame?.IsGtaSanAndreas != true ||
            !int.TryParse(degrees, out var angle)) return;
        SaveGtaTurnPreference(SelectedGame.GtaTurnPreference with { SnapDegrees = angle });
    }

    private void SaveGtaTurnPreference(GtaTurnPreference preference)
    {
        if (_controller is null || SelectedGame?.IsGtaSanAndreas != true) return;
        var outcome = _controller.SetGtaSanAndreasTurnPreference(SelectedGame.View, preference);
        StatusText = outcome.Message;
        if (outcome.Ok) SelectedGame.SetGtaTurnPreference(preference);
    }

    [RelayCommand]
    private void SetCyberpunkTurnMode(string mode)
    {
        if (SelectedGame?.IsCyberpunk != true) return;
        var selectedMode = mode.Equals("toggle", StringComparison.OrdinalIgnoreCase)
            ? SelectedGame.IsCyberpunkSmoothTurn ? CyberpunkTurnMode.Snap : CyberpunkTurnMode.Smooth
            : mode.Equals("smooth", StringComparison.OrdinalIgnoreCase)
                ? CyberpunkTurnMode.Smooth : CyberpunkTurnMode.Snap;
        SaveCyberpunkTurnPreference(SelectedGame.CyberpunkTurnPreference with { Mode = selectedMode });
    }

    [RelayCommand]
    private void SetCyberpunkSnapTurnAngle(string degrees)
    {
        if (SelectedGame?.IsCyberpunk != true || !int.TryParse(degrees, out var angle)) return;
        SaveCyberpunkTurnPreference(SelectedGame.CyberpunkTurnPreference with { SnapDegrees = angle });
    }

    private void SaveCyberpunkTurnPreference(CyberpunkTurnPreference preference)
    {
        if (_controller is null || SelectedGame?.IsCyberpunk != true) return;
        var outcome = _controller.SetCyberpunkTurnPreference(SelectedGame.View, preference);
        StatusText = outcome.Message;
        if (outcome.Ok) SelectedGame.SetCyberpunkTurnPreference(preference);
    }

    /// GGMM only works from the game folder, beside its companion DLL.
    private static string? GgmmPlacementError(string executable, string gameDir) =>
        Path.GetFullPath(Path.GetDirectoryName(executable) ?? "").Equals(
            Path.GetFullPath(gameDir), StringComparison.OrdinalIgnoreCase) &&
        File.Exists(Path.Combine(gameDir, "gtainterface.dll"))
            ? null
            : "Place ggmm.exe and gtainterface.dll beside gta_sa.exe in the VR game folder before using GGMM.";

    public void SelectGtaSanAndreasModManager(
        string executable, GtaSanAndreasModManagerKind kind)
    {
        if (SelectedGame?.IsGtaSanAndreas != true ||
            SelectedGame.View.InstallDir is not { } gameDir) return;
        try
        {
            var error = GtaSanAndreasModManagerSettings.Validate(executable, kind) ??
                (kind is GtaSanAndreasModManagerKind.Ggmm ? GgmmPlacementError(executable, gameDir) : null);
            if (error is not null)
                throw new InvalidOperationException(error);
            _gtaModManagerSettings.Save(executable, kind);
            StatusText = $"Mod manager selected: {executable}. Point it at {gameDir}; AVRcade cannot verify its active mods.";
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            StatusText = $"Could not select mod manager: {ex.Message}";
        }
        UpdateSelectedGameFacts();
    }

    [RelayCommand]
    private void OpenGtaSanAndreasModManager()
    {
        if (_controller is null || SelectedGame?.IsGtaSanAndreas != true ||
            SelectedGame.View.InstallDir is not { } gameDir) return;
        var selection = _gtaModManagerSettings.LoadSelection();
        var error = GtaSanAndreasModManagerSettings.Validate(
            selection?.Executable, selection?.Kind ?? GtaSanAndreasModManagerKind.Custom);
        if (error is not null) { StatusText = error; return; }
        // GGMM needs its companion DLL and the game's working directory. Other tools
        // get no guessed command-line arguments or automatic profile/deployment claims.
        if (selection!.Kind is GtaSanAndreasModManagerKind.Ggmm)
            StatusText = GgmmPlacementError(selection.Executable, gameDir)
                ?? _controller.OpenGtaSanAndreasGgmm(gameDir).Message;
        else
            StatusText = StartManager(selection.Executable,
                $"Set its game to {gameDir}, deploy mods, then return here to play.");
    }

    public void SelectGtaSanAndreasVanillaFolder(string folder)
    {
        if (_controller is null || SelectedGame?.IsGtaSanAndreas != true ||
            SelectedGame.View.InstallDir is not { } installDir) return;
        StatusText = _controller.SetGtaSanAndreasVanillaFolder(installDir, folder).Message;
        UpdateSelectedGameFacts();
        SelectedGame.RefreshPlayModes();
    }

    /// Remember the manager the player uses for a game with game-folder mods.
    public void SelectGameModManager(string executable)
    {
        if (SelectedGame is not { UsesFolderMods: true } game) return;
        try
        {
            _gameModManagers.Save(game.Slug, executable);
            StatusText = $"Mod manager selected: {executable}. AVRcade opens it for you; the manager installs the mods.";
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException or ArgumentException)
        {
            StatusText = $"Could not select mod manager: {ex.Message}";
        }
        UpdateSelectedGameFacts();
    }

    [RelayCommand]
    private void OpenGameModManager() =>
        StatusText = SelectedGame is { } game && GameModManagerExecutable(game) is { } executable
            ? StartManager(executable,
                $"Install or deploy your mods for {game.DisplayName}, then come back and refresh.")
            : "Choose your mod manager first.";

    /// Open a mod-manager application in its own window; returns the status line to show.
    private static string StartManager(string executable, string nextStep)
    {
        try
        {
            var process = Process.Start(new ProcessStartInfo(executable)
            {
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(executable)!
            });
            return process is null
                ? "The mod manager did not start."
                : $"Opened {Path.GetFileNameWithoutExtension(executable)}. {nextStep}";
        }
        catch (Exception ex) when (ex is Win32Exception or IOException or UnauthorizedAccessException or ArgumentException)
        {
            return $"Could not open mod manager: {ex.Message}";
        }
    }

    [RelayCommand]
    private void OpenGameFolder()
    {
        if (SelectedGame?.View.InstallDir is not { } folder || !Directory.Exists(folder))
        {
            StatusText = "Game is not installed on this PC.";
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo("explorer.exe") { ArgumentList = { folder } });
            StatusText = $"Opened {folder}.";
        }
        catch (Exception ex) when (ex is Win32Exception or IOException)
        {
            StatusText = $"Could not open the game folder: {ex.Message}";
        }
    }

    [RelayCommand]
    private void SaveManagerLaunchArguments()
    {
        if (SelectedGame?.Modded is { } modded)
            StatusText = modded.SaveR2Arguments();
    }

    [RelayCommand]
    private void ClearVrProfile()
    {
        SelectedGame?.Modded?.ClearVrProfile();
        StatusText = "VR profile cleared. Manager profiles themselves are untouched.";
    }

    [RelayCommand]
    private void ClearFlatProfile()
    {
        SelectedGame?.Modded?.ClearFlatProfile();
        StatusText = "Flat profile cleared. Manager profiles themselves are untouched.";
    }

    [RelayCommand]
    private void SaveMovedProfileRoot()
    {
        if (SelectedGame?.Modded is not { } modded) return;
        var message = modded.SaveMovedProfileRoot();
        if (message.StartsWith("Saved", StringComparison.Ordinal))
            RefreshCatalogue(announce: false);
        StatusText = message;
    }

    [RelayCommand]
    private void SetHudMode(string? modeName)
    {
        if (_controller is null || SelectedGame is null ||
            !Enum.TryParse<CyberpunkHudMode>(modeName, ignoreCase: true, out var mode))
            return;

        var outcome = _controller.SetCyberpunkHudMode(SelectedGame.View, mode);
        StatusText = outcome.Message;
        if (outcome.Ok)
            SelectedGame.SetHudMode(mode);
    }

    [RelayCommand]
    private async Task UninstallAsync()
    {
        if (_controller is null || SelectedGame is not { } game || Busy) return;
        var dir = game.View.InstallDir;
        if (string.IsNullOrEmpty(dir))
        {
            StatusText = "Game is not installed on this PC.";
            return;
        }
        // Removing the Steam launch option restarts Steam, which can take a while.
        var controller = _controller;
        Busy = true;
        StatusText = $"Removing the VR mod from {game.DisplayName}…";
        var finalStatus = "Removal failed.";
        try
        {
            finalStatus = (await Task.Run(() => game.IsCyberpunk
                ? controller.UninstallCyberpunkVr(dir)
                : controller.Uninstall(dir, game.Slug, removeLaunchOption: true))).Message;
        }
        catch (Exception ex)
        {
            finalStatus = $"Removal failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
            RefreshCatalogue(announce: false);
            StatusText = finalStatus;
        }
    }

    public void Dispose()
    {
        foreach (var game in Games)
            game.Dispose();
        foreach (var cover in _covers.Values)
            cover.Dispose();
        _covers.Clear();
        _falloutNewVegasService?.Dispose();
    }
}
