using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using VrClient.Core;
using VrClient.Core.App;
using VrClient.Core.Config;
using VrClient.Core.Discovery;
using VrClient.Core.Legacy;
using VrClient.Core.Launch;
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
    public StreamingViewModel Streaming { get; }
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
    private readonly Dictionary<string, (string Message, bool Busy, bool Failed)> _modUpdateProgress = new(StringComparer.OrdinalIgnoreCase);
    private CancellationTokenSource? _modUpdateCancellation;
    private void ClearFinishedModUpdate(string slug, string root)
    {
        var key = SetupKey(slug, root);
        if (_modUpdateProgress.TryGetValue(key, out var active) && active.Busy) return;
        _modUpdateProgress.Remove(key);
        foreach (var item in Games.Where(item => item.Slug == slug && string.Equals(item.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)))
            item.SetModUpdateProgress(string.Empty, false);
    }
    private void ReportModUpdate(string slug, string root, string message, bool busy, bool failed = false)
    {
        _modUpdateProgress[SetupKey(slug, root)] = (message, busy, failed);
        foreach (var item in Games.Where(item => item.Slug == slug && string.Equals(item.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)))
            item.SetModUpdateProgress(message, busy, failed);
        if (SelectedGame?.Slug == slug && string.Equals(SelectedGame.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)) StatusText = message;
    }
    private readonly Dictionary<string, (string Message, bool Busy, bool Failed)> _gameSetupProgress = new(StringComparer.OrdinalIgnoreCase);
    private static string SetupKey(string slug, string root) => slug + "|" + Path.GetFullPath(root);
    private void ReportGameSetup(string slug, string root, string message, bool busy, bool failed = false)
    {
        _gameSetupProgress[SetupKey(slug, root)] = (message, busy, failed);
        foreach (var item in Games.Where(item => item.Slug == slug && string.Equals(item.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)))
            item.SetSetupProgress(message, busy, failed);
        if (SelectedGame?.Slug == slug && string.Equals(SelectedGame.View.InstallDir, root, StringComparison.OrdinalIgnoreCase))
            StatusText = message;
    }
    private readonly Dictionary<string, (string Message, bool Busy)> _cyberpunkSetupProgress = new(StringComparer.OrdinalIgnoreCase);
    private void ReportCyberpunkSetup(string root, string message, bool busy)
    {
        _cyberpunkSetupProgress[root] = (message, busy);
        foreach (var item in Games.Where(item => item.IsCyberpunk && string.Equals(item.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)))
        {
            item.SetCyberpunkSetupProgress(message, busy);
            if (item.ModUpdateBusy) ReportModUpdate(item.Slug, root, message, true);
        }
        if (string.Equals(SelectedGame?.View.InstallDir, root, StringComparison.OrdinalIgnoreCase)) StatusText = message;
    }
    private CancellationTokenSource? _cyberpunkRequirementsCancellation;
    [ObservableProperty] private bool _isInstallingCyberpunkRequirements;
    private IReadOnlyList<ModManagerInstall> _installedManagers = [];

    public ObservableCollection<GameItemViewModel> Games { get; } = new();
    public ObservableCollection<GameItemViewModel> FilteredGames { get; } = new();

    [ObservableProperty] private GameItemViewModel? _selectedGame;
    [ObservableProperty] private string _statusText = "Ready.";
    [ObservableProperty] private bool _statusSuccess;
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
    /// 0 = VR setup, 1 = Mods, 2 = Controls, 3 = About, 4 = Troubleshooting.
    [ObservableProperty] private int _detailTabIndex;
    // Facts about the selected game's mod tools, read from disk once per selection.
    [ObservableProperty] private string _gtaVanillaFolderText = string.Empty;
    [ObservableProperty] private string _gtaModManagerText = string.Empty;
    [ObservableProperty] private string _gtaVrGameFolderText = string.Empty;
    [ObservableProperty] private string _gameModManagerText = string.Empty;
    [ObservableProperty] private bool _canOpenGameModManager;
    // Chooser buttons read "Choose" until something is chosen; download links go away after.
    [ObservableProperty] private string _gameManagerChooserText = ChooseManager;
    [ObservableProperty] private string _gtaManagerChooserText = ChooseManager;
    [ObservableProperty] private string _gtaCleanFolderChooserText = "Choose clean game folder";
    [ObservableProperty] private bool _showGtaManagerDownloads = true;
    private const string ChooseManager = "Choose your manager";
    private const string ChangeManager = "Change manager";

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

    partial void OnSelectedGameChanged(GameItemViewModel? value)
    {
        OnPropertyChanged(nameof(CanUpdateSelectedMods));
        RefreshRepairAvailability();
        ShowSelectedTroubleshooting();
        Streaming.SetGameContext(ShowGameDetail ? value?.Slug : null);
        UpdateSelectedGameFacts();
    }

    public bool CanUpdateSelectedMods => !Busy && !IsInstallingCyberpunkRequirements && SelectedGame?.CanUpdateMods == true;
    public bool CanRepairSelectedGame => !Busy && !IsInstallingCyberpunkRequirements && SelectedGame?.View.InstallDir is not null;
    private void RefreshRepairAvailability()
    {
        OnPropertyChanged(nameof(CanRepairSelectedGame));
        VerifyGameFilesCommand.NotifyCanExecuteChanged();
        RecheckGameSetupCommand.NotifyCanExecuteChanged();
        RunGameDiagnosticsCommand.NotifyCanExecuteChanged();
        TroubleshootingActionCommand.NotifyCanExecuteChanged();
    }
    partial void OnBusyChanged(bool value)
    {
        if (value) StatusSuccess = false;
        OnPropertyChanged(nameof(CanUpdateSelectedMods));
        RefreshRepairAvailability();
    }
    partial void OnStatusTextChanged(string value) => StatusSuccess = false;
    partial void OnIsInstallingCyberpunkRequirementsChanged(bool value)
    {
        OnPropertyChanged(nameof(CanUpdateSelectedMods));
        RefreshRepairAvailability();
    }

    private void UpdateSelectedGameFacts()
    {
        var game = SelectedGame;
        if (game?.IsGtaSanAndreas == true)
        {
            GtaVrGameFolderText = game.View.InstallDir is { } folder
                ? $"VR game folder: {folder}" : "VR game folder has not been found.";
            GtaVanillaFolderText = DescribeGtaVanillaFolder(game);
            GtaModManagerText = DescribeGtaModManager();
            var manager = _gtaModManagerSettings.LoadSelection();
            var hasManager = manager is not null &&
                GtaSanAndreasModManagerSettings.Validate(manager.Executable, manager.Kind) is null;
            GtaManagerChooserText = hasManager ? ChangeManager : ChooseManager;
            ShowGtaManagerDownloads = !hasManager;
            GtaCleanFolderChooserText = GtaSanAndreasVanillaSettings.Validate(
                _controller?.GetGtaSanAndreasVanillaFolder(), game.View.InstallDir) is null
                ? "Change clean game folder" : "Choose clean game folder";
        }
        var executable = GameModManagerExecutable(game);
        CanOpenGameModManager = executable is not null;
        GameManagerChooserText = executable is not null ? ChangeManager : ChooseManager;
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
          (game.IsYapyap ? _installedManagers.FirstOrDefault(manager =>
              manager.Kind is ModManagerKind.R2Modman or ModManagerKind.Thunderstore)?.LaunchPath : null) ??
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
        Streaming.SetGameContext(value ? SelectedGame?.Slug : null);
        if (value)
            ShowSettings = false;
        RaiseSurface();
    }

    partial void OnShowSettingsChanged(bool value)
    {
        if (value) Streaming.SetGameContext(null);
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
    public MainWindowViewModel(Func<string, string?>? coverLocator = null, StreamingViewModel? streaming = null,
        AppController? controller = null)
    {
        Streaming = streaming ?? new();
        _coverLocator = coverLocator ?? (appId => SteamLibraryArtworkLocator.FindPortrait(appId));
        IsDarkTheme = LauncherThemeService.Current.IsDark;
        var root = controller?.RepoRoot ?? RepoRoot.Find();
        if (root is null)
        {
            StatusText = "Could not find the AVRcade configuration folder.";
            return;
        }
        _controller = controller ?? new AppController(root);
        _falloutNewVegasService = new FalloutNewVegasDesktopService(root);
        var releaseDataRoot = VrClient.Core.AppDataPaths.Local("release-foundation");
        _releasePackageStore = new ReleasePackageStore(releaseDataRoot);
        _releaseProfileStore = new ReleaseProfileStore(releaseDataRoot);
        RefreshCatalogue(announce: true);
    }

    public FalloutNewVegasSetupViewModel? CreateFalloutNewVegasSetupViewModel() =>
        _falloutNewVegasService is null ? null : new FalloutNewVegasSetupViewModel(_falloutNewVegasService);

    public async Task ImportUevrProfileAsync(string slug, string gameRoot, string archivePath)
    {
        if (_controller is null || Busy || IsInstallingCyberpunkRequirements ||
            !Games.Any(game => game.Slug == slug && game.View.InstallDir == gameRoot && game.CanImportUevrProfile))
            return;
        ClearFinishedModUpdate(slug, gameRoot);
        Busy = true;
        var message = "Profile installation did not complete.";
        var failed = true;
        ReportGameSetup(slug, gameRoot, "Checking and importing the selected VR profile ZIP...", true);
        try
        {
            var outcome = await Task.Run(() => _controller.ImportUnrealProfile(slug, archivePath));
            message = outcome.Message;
            failed = !outcome.Ok;
        }
        catch (Exception ex) { message = "Profile import failed: " + ex.Message; }
        finally
        {
            Busy = false;
            ReportGameSetup(slug, gameRoot, message, false, failed);
            RefreshGame(slug);
            StatusText = message;
        }
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
        if (!Busy && !IsInstallingCyberpunkRequirements)
        {
            RefreshGame(game.Slug);
            if (SelectedGame is not { } refreshed) return;
            game = refreshed;
        }
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

    // Session-only, one small result per installation. Nothing is written to disk.
    private readonly Dictionary<string, (GameTroubleshootingReport Report, string Action)> _troubleshooting = new(StringComparer.OrdinalIgnoreCase);
    [ObservableProperty] private IReadOnlyList<TroubleshootingFinding> _troubleshootingFindings = [];
    [ObservableProperty] private string _troubleshootingSummary = "Run diagnostics to check this game's installation.";
    [ObservableProperty] private string _troubleshootingActionStatus = string.Empty;
    public bool CanRunGameDiagnostics => _controller is not null && SelectedGame is not null && !Busy && !IsInstallingCyberpunkRequirements;
    private static string TroubleshootingKey(GameView game) => game.Slug + "|" +
        (game.InstallDir is { } root ? Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)) : "not-installed");
    private bool IsSelectedInstallation(GameView game) => SelectedGame is { } selected &&
        TroubleshootingKey(selected.View).Equals(TroubleshootingKey(game), StringComparison.OrdinalIgnoreCase);
    private void ShowSelectedTroubleshooting()
    {
        var selected = SelectedGame;
        if (selected is not null && _troubleshooting.TryGetValue(TroubleshootingKey(selected.View), out var state))
        {
            TroubleshootingFindings = state.Report.Findings;
            TroubleshootingSummary = state.Action.Length > 0
                ? $"{selected.DisplayName} · action at {state.Report.CheckedAt.LocalDateTime:t}. Review the result, then run diagnostics when finished."
                : $"{selected.DisplayName} · checked {state.Report.CheckedAt.LocalDateTime:t}. " +
                    (state.Report.NeedsAttention ? "Review the findings below." : "No problem found in the checks performed. Manual VR checks may still be needed.");
            TroubleshootingActionStatus = state.Action;
        }
        else
        {
            TroubleshootingFindings = [];
            TroubleshootingSummary = selected is null ? "Choose a game to troubleshoot."
                : $"Run diagnostics to check {selected.DisplayName}. Results stay in memory until AVRcade closes.";
            TroubleshootingActionStatus = string.Empty;
        }
        RefreshTroubleshootingGuide();
    }
    private void StoreTroubleshooting(GameView game, GameTroubleshootingReport report, string action = "")
    {
        var key = TroubleshootingKey(game);
        foreach (var old in _troubleshooting.Where(item =>
            !Games.Any(candidate => candidate.Slug == item.Value.Report.Slug) ||
            item.Value.Report.Slug == game.Slug && !item.Key.Equals(key, StringComparison.OrdinalIgnoreCase))
            .Select(item => item.Key).ToArray()) _troubleshooting.Remove(old);
        if (!_troubleshooting.ContainsKey(key) && _troubleshooting.Count >= 32)
            _troubleshooting.Remove(_troubleshooting.Keys.First());
        _troubleshooting[key] = (report, action.Length <= 2000 ? action : action[..2000]);
        if (IsSelectedInstallation(game)) ShowSelectedTroubleshooting();
    }
    private void RecordTroubleshootingAction(GameView game, string message, bool ok)
    {
        // Repairs/Steam requests invalidate previous findings; rerun explicitly when finished.
        StoreTroubleshooting(game, new(game.Slug, game.InstallDir, DateTimeOffset.Now,
            [new("Action result", ok ? "Instructions" : "Attention", ShortTroubleshootingText(message),
                "When the action finishes, run diagnostics again for this installation.")]), message);
    }
    private static string ShortTroubleshootingText(string text) => text.Length <= 1800 ? text : text[..1800] + "…";
    private GameTroubleshootingReport CheckCapturedGame(GameView captured)
    {
        var runtime = _controller!.GetRuntimeStatus();
        var current = _controller.GetGame(captured.Slug, runtime.SteamVrRunning || runtime.VirtualDesktopRunning);
        if (current is null || !TroubleshootingKey(current).Equals(TroubleshootingKey(captured), StringComparison.OrdinalIgnoreCase))
            return new(captured.Slug, captured.InstallDir, DateTimeOffset.Now,
                [new("Installation changed", "Attention", "This game's selected installation changed while checking.",
                    "Refresh this game, confirm its installed folder, then run diagnostics again.")]);
        return new GameTroubleshooting().Check(_controller.RepoRoot, current, runtime);
    }
    [RelayCommand(CanExecute = nameof(CanRunGameDiagnostics))]
    private async Task RunGameDiagnosticsAsync()
    {
        if (!CanRunGameDiagnostics || SelectedGame is not { } selected) return;
        var captured = selected.View;
        Busy = true;
        StatusText = $"Checking {captured.DisplayName}...";
        try
        {
            var report = await Task.Run(() => CheckCapturedGame(captured));
            RefreshGame(captured.Slug); // Reflect repaired/updated files in this game's play buttons.
            StoreTroubleshooting(captured, report);
            if (IsSelectedInstallation(captured))
            { DetailTabIndex = 4; StatusText = $"{captured.DisplayName}: diagnostics finished. See Troubleshooting at the bottom of this page."; }
        }
        catch (Exception ex)
        {
            StoreTroubleshooting(captured, new(captured.Slug, captured.InstallDir, DateTimeOffset.Now,
                [new("Check could not finish", "Attention", ex.Message.Length > 1800 ? ex.Message[..1800] : ex.Message,
                    "Close this game, check that its drive is available, and run diagnostics again.")]));
            if (IsSelectedInstallation(captured)) StatusText = $"{captured.DisplayName}: diagnostics could not finish. See Troubleshooting below.";
        }
        finally
        {
            Busy = false;
            if (!IsSelectedInstallation(captured))
                StatusText = $"{captured.DisplayName}: check finished. Open that game's Troubleshooting tab for results.";
        }
    }
    [RelayCommand(CanExecute = nameof(CanRunGameDiagnostics))]
    private async Task TroubleshootingActionAsync(TroubleshootingFinding? finding)
    {
        if (!CanRunGameDiagnostics || finding is null || !TroubleshootingFindings.Any(current => ReferenceEquals(current, finding))) return;
        switch (finding.Action)
        {
            case TroubleshootingAction.RepairSetup: await RecheckGameSetupAsync(); break;
            case TroubleshootingAction.VerifyFiles: await VerifyGameFilesAsync(); break;
            case TroubleshootingAction.VrSetup: DetailTabIndex = 0; break;
            case TroubleshootingAction.Mods: DetailTabIndex = 1; break;
            case TroubleshootingAction.Controls: DetailTabIndex = 2; break;
            case TroubleshootingAction.UpdateMods: await UpdateGameModsAsync(); break;
        }
    }

    [RelayCommand(CanExecute = nameof(CanRepairSelectedGame))]
    private async Task VerifyGameFilesAsync()
    {
        if (_controller is null || !CanRepairSelectedGame || SelectedGame is not { } game) return;
        Busy = true;
        StatusText = $"Preparing Steam verification for {game.DisplayName}...";
        try
        {
            var outcome = await Task.Run(() => new VrClient.Core.Launch.GameSetupRecovery().VerifyGameFiles(_controller.RepoRoot, game.View));
            RecordTroubleshootingAction(game.View, outcome.Message, outcome.Ok);
            if (IsSelectedInstallation(game.View)) StatusText = outcome.Message;
        }
        catch (Exception ex)
        {
            RecordTroubleshootingAction(game.View, "Steam verification could not start: " + ex.Message, false);
            if (IsSelectedInstallation(game.View)) StatusText = "Steam verification could not start: " + ex.Message;
        }
        finally
        {
            Busy = false;
            if (!IsSelectedInstallation(game.View))
                StatusText = $"{game.DisplayName}: see that game's Troubleshooting tab for the Steam verification request result.";
        }
    }

    [RelayCommand(CanExecute = nameof(CanRepairSelectedGame))]
    private async Task RecheckGameSetupAsync()
    {
        if (_controller is null || Busy || IsInstallingCyberpunkRequirements) return;
        if (SelectedGame is not { } selected) return;
        RefreshGame(selected.Slug);
        var game = Games.FirstOrDefault(item => item.Slug == selected.Slug);
        if (game is null) return;
        if (!TroubleshootingKey(game.View).Equals(TroubleshootingKey(selected.View), StringComparison.OrdinalIgnoreCase))
        { StatusText = "This game's installation changed. Confirm its folder, then recheck again."; return; }
        var root = game.View.InstallDir;
        if (root is null) { StatusText = "Reconnect this game's disk or choose Locate installed game, then Recheck setup."; return; }
        ClearFinishedModUpdate(game.Slug, root);
        Busy = true;
        ReportGameSetup(game.Slug, root, "Checking this game's installation and controller routes...", true);
        if (game.IsCyberpunk) ReportCyberpunkSetup(root, "Checking this game's installation and controller routes...", true);
        ActionOutcome outcome;
        try { outcome = await Task.Run(() => new VrClient.Core.Launch.GameSetupRecovery(_controller.Compatibility.ControllersFor(game.View)).Recheck(_controller.RepoRoot, game.View)); }
        catch (Exception ex) { outcome = new(false, "Setup check could not complete: " + ex.Message); }
        finally { Busy = false; }
        ReportGameSetup(game.Slug, root, outcome.Message, false, !outcome.Ok);
        if (game.IsCyberpunk) ReportCyberpunkSetup(root, outcome.Message, false);
        RefreshGame(game.Slug);
        StoreTroubleshooting(game.View, new(game.Slug, root, DateTimeOffset.Now,
            [new("Setup recheck", outcome.Ok ? "Check" : "Attention", ShortTroubleshootingText(outcome.Message),
                "Run diagnostics for this installation to review native files, VR requirements and runtime observations.")]), outcome.Message);
        if (IsSelectedInstallation(game.View))
        {
            StatusText = outcome.Message;
            if (!outcome.Ok) DetailTabIndex = 4;
        }
        else StatusText = $"{game.DisplayName}: setup recheck finished. Open that game's Troubleshooting tab for results.";
    }

    /// The page in view still asks the player for something: the headset software,
    /// a game to install, or a setup step on one of the four play modes.
    public bool HasPendingSetup => !RuntimeOnline ||
        (ShowGameDetail
            ? SelectedGame is { } game && (!game.IsInstalled || game.PlayModes.Any(mode => !mode.IsLaunch))
            : Games.Any(game => !game.IsInstalled));

    /// Recheck what is installed without replacing the status line, so a step the
    /// player just finished outside AVRcade stops being asked for.
    public void RedetectQuietly()
    {
        if (Busy || IsInstallingCyberpunkRequirements || _controller is null) return;
        if (ShowGameDetail && SelectedGame is { } game) RefreshGame(game.Slug);
        else UpdateRuntimeStatus(_controller.GetRuntimeStatus());
    }

    /// Remember where a game the scan missed is installed (Unity games and San Andreas).
    public void RememberGameFolder(string folder)
    {
        if (_controller is null || SelectedGame is not { CanLocateGame: true } game) return;
        var result = game.IsGtaSanAndreas
            ? _controller.RememberGtaSanAndreasFolder(folder)
            : _controller.RememberFriendslopGameFolder(game.Slug, folder);
        if (result.Ok) RefreshGame(game.Slug);
        StatusText = result.Message;
    }

    private void RefreshCatalogue(bool announce)
    {
        if (_controller is null)
            return;
        if (announce)
        {
            foreach (var key in _gameSetupProgress.Where(pair => !pair.Value.Busy).Select(pair => pair.Key).ToArray())
                _gameSetupProgress.Remove(key);
            foreach (var key in _modUpdateProgress.Where(pair => !pair.Value.Busy).Select(pair => pair.Key).ToArray())
                _modUpdateProgress.Remove(key);
        }
        var selectedSlug = SelectedGame?.Slug;
        var selectedManagerKind = SelectedGame?.Modded?.SelectedManager?.Kind;
        SelectedGame = null;
        foreach (var game in Games)
            game.Dispose();
        Games.Clear();

        // Sampled once per refresh and shared by every game below.
        var runtime = _controller.GetRuntimeStatus();
        var runtimeDetected = runtime.SteamVrRunning || runtime.VirtualDesktopRunning;
        var dataRoots = _moddedPreferences.LoadDataRoots();
        var setupGuides = GameSetupGuides.Load(_controller.RepoRoot);
        _installedManagers = _modManagerDiscovery.FindInstalled(dataRoots);
        foreach (var game in _controller.ListGames(runtimeDetected)
                     .OrderByDescending(game => game.InstallDir is not null)
                     .ThenBy(game => game.DisplayName, StringComparer.OrdinalIgnoreCase))
            Games.Add(CreateGameItem(game, runtime, dataRoots, setupGuides,
                game.Slug == selectedSlug ? selectedManagerKind : null));
        Streaming.SetCatalogue(Games.Select(game => new KeyValuePair<string, string>(game.Slug, game.DisplayName)));
        ApplyFilter();
        OnPropertyChanged(nameof(FilterAllText));
        OnPropertyChanged(nameof(FilterInstalledText));
        OnPropertyChanged(nameof(FilterReadyText));

        UpdateRuntimeStatus(runtime);
        UpdateReleaseTooling(runtimeDetected);
        if (announce)
            StatusText = CatalogueSummaryText;
        SelectedGame = Games.FirstOrDefault(game => game.Slug == selectedSlug)
            ?? (Games.Count > 0 ? Games[0] : null);
    }

    private GameItemViewModel CreateGameItem(GameView game, RuntimeStatusView runtime,
        IReadOnlyDictionary<ModManagerKind, string> dataRoots,
        IReadOnlyDictionary<string, GameSetupGuide> setupGuides, ModManagerKind? selectedManagerKind)
    {
        var controller = _controller ?? throw new InvalidOperationException("Catalogue configuration is unavailable.");
        var runtimeDetected = runtime.SteamVrRunning || runtime.VirtualDesktopRunning;
        var item = new GameItemViewModel(
            game,
            controller.GetControllerReference(game.Slug),
            controller.GetHudMode(game),
            game.Engine is GameEngine.LegacyD3D9
                ? controller.GetGtaSanAndreasTurnPreference() : GtaSanAndreasTurnSettings.Default,
            controller.GetCyberpunkTurnPreference(game),
            CoverFor(game.SteamAppId), controller.GetModCredits(game.Slug), controller.Compatibility.ControllersFor(game));
        if (CommunityVrRoutes.All.TryGetValue(game.Slug, out var route))
        {
            var modded = new ModdedGameViewModel(controller.RepoRoot, route, game.InstallDir,
                game.InstallDir is not null, game.Readiness is not GameReadiness.Blocked,
                game.OfflineSteamLibrary is not null,
                runtimeDetected, runtime.SteamVrRunning, game.ModInstalled,
                _modManagerDiscovery, _installedManagers, dataRoots, _moddedPreferences,
                item.RefreshPlayModes);
            item.SetModded(modded);
            if (selectedManagerKind is { } kind &&
                _installedManagers.FirstOrDefault(manager => manager.Kind == kind) is { } manager)
                modded.SelectedManager = manager;
        }
        item.SetCompatibility(controller.Compatibility, choice => SelectInstallationChoice(game, choice));
        item.SetPlayModeSource(
            source => BuildPlayModes(source, runtimeDetected), runtimeDetected, controller.GetUnrealMods(game), runtime.SteamVrRunning);
        if (item.IsCyberpunk && game.InstallDir is { } cpRoot && _cyberpunkSetupProgress.TryGetValue(cpRoot, out var cpProgress))
            item.SetCyberpunkSetupProgress(cpProgress.Message, cpProgress.Busy);
        item.SetSetupGuide(setupGuides.GetValueOrDefault(game.Slug));
        if (game.InstallDir is { } setupRoot && _gameSetupProgress.TryGetValue(SetupKey(game.Slug, setupRoot), out var setupProgress))
            item.SetSetupProgress(setupProgress.Message, setupProgress.Busy, setupProgress.Failed);
        if (game.InstallDir is { } updateRoot && _modUpdateProgress.TryGetValue(SetupKey(game.Slug, updateRoot), out var updateProgress))
            item.SetModUpdateProgress(updateProgress.Message, updateProgress.Busy, updateProgress.Failed);
        return item;
    }

    private void UpdateRuntimeStatus(RuntimeStatusView runtime)
    {
        var runtimeDetected = runtime.SteamVrRunning || runtime.VirtualDesktopRunning;
        RuntimeText = $"SteamVR: {(runtime.SteamVrRunning ? "running" : "off")}   " +
                      $"Virtual Desktop: {(runtime.VirtualDesktopRunning ? "running" : "off")}   " +
                      $"Launch uses: {runtime.SelectedRuntime}";
        RuntimeOnline = runtimeDetected;
        RuntimeIndicatorText = runtimeDetected
            ? $"VR ready · {runtime.SelectedRuntime}"
            : "Headset not connected";
        OnPropertyChanged(nameof(ShowHeadsetHint));
    }

    /// Update the action's game, preserving unrelated pages and the current selection.
    public void RefreshGame(string slug)
    {
        if (_controller is null) return;
        var old = Games.FirstOrDefault(item => item.Slug == slug);
        if (old is null) return;
        var runtime = _controller.GetRuntimeStatus();
        var view = _controller.GetGame(slug, runtime.SteamVrRunning || runtime.VirtualDesktopRunning);
        var selected = ReferenceEquals(SelectedGame, old);
        if (view is null)
        {
            Games.Remove(old);
            if (selected) SelectedGame = null;
        }
        else
        {
            var roots = _moddedPreferences.LoadDataRoots();
            _installedManagers = _modManagerDiscovery.FindInstalled(roots);
            var item = CreateGameItem(view, runtime, roots, GameSetupGuides.Load(_controller.RepoRoot),
                old.Modded?.SelectedManager?.Kind);
            Games[Games.IndexOf(old)] = item;
            if (selected) SelectedGame = item;
            var ordered = Games.OrderByDescending(game => game.IsInstalled)
                .ThenBy(game => game.DisplayName, StringComparer.OrdinalIgnoreCase).ToList();
            var position = ordered.IndexOf(item);
            if (Games.IndexOf(item) != position) Games.Move(Games.IndexOf(item), position);
        }
        old.Dispose();
        Streaming.SetCatalogue(Games.Select(game => new KeyValuePair<string, string>(game.Slug, game.DisplayName)));
        ApplyFilter();
        OnPropertyChanged(nameof(FilterAllText));
        OnPropertyChanged(nameof(FilterInstalledText));
        OnPropertyChanged(nameof(FilterReadyText));
        UpdateRuntimeStatus(runtime);
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
        if (item.IsYapyap)
            return PlayModePlanner.Yapyap(installed, YapyapVrInstall.Inspect(view.InstallDir),
                GameModManagerExecutable(item) is not null,
                VrClient.Core.Launch.SteamVrStartup.IsServerRunning());
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
                    allowed ? null : view.ReadinessDetail,
                    view.ModInstalled, runtimeDetected,
                    _controller?.CyberpunkBackendInstallable == true,
                    view.InstallDir is { } cyberpunkDir && CyberpunkNexusRequirements.Installed(cyberpunkDir)));
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
        http.DefaultRequestHeaders.UserAgent.ParseAdd("AVRcade/0.4");
        return http;
    }

    [RelayCommand]
    private async Task UpdateGameModsAsync()
    {
        if (_controller is null || SelectedGame is not { } previous || Busy || IsInstallingCyberpunkRequirements) return;
        RefreshGame(previous.Slug); // Steam may have updated the game since this page opened.
        if (SelectedGame is not { CanUpdateMods: true } game || !IsSelectedInstallation(previous.View) ||
            game.View.InstallDir is not { } root || Busy || IsInstallingCyberpunkRequirements) return;
        var slug = game.Slug;
        Busy = true;
        var message = "The mod update did not complete.";
        var failed = true;
        var acceptProgress = true;
        _modUpdateCancellation = new CancellationTokenSource();
        ReportModUpdate(slug, root, "Checking this game's upstream mod releases...", true);
        try
        {
            using var http = NewHttp();
            var progress = new Progress<string>(text => { if (acceptProgress) ReportModUpdate(slug, root, text, true); });
            var result = await Task.Run(() => new GameModUpdater(_controller.RepoRoot, http)
                .UpdateAsync(game.View, _controller, progress, _modUpdateCancellation.Token));
            acceptProgress = false;
            message = result.Message;
            failed = !result.Ok;
            if (result.Ok && game.IsCyberpunk && !CyberpunkNexusRequirements.Installed(root))
            {
                var requirement = await InstallCyberpunkRequirementsCoreAsync(root);
                failed = !requirement.Ok;
                message += "\n" + requirement.Message;
            }
        }
        catch (OperationCanceledException) { message = "Mod update cancelled. Completed file transactions are retained; unfinished transactions recover before the next update."; }
        catch (Exception ex) { message = "Mod update failed: " + ex.Message; }
        finally
        {
            acceptProgress = false;
            _modUpdateCancellation.Dispose();
            _modUpdateCancellation = null;
            Busy = false;
            ReportModUpdate(slug, root, message, false, failed);
            RefreshGame(slug);
            StatusText = message;
        }
    }

    [RelayCommand]
    private async Task PrepareAsync()
    {
        if (_controller is null || SelectedGame is not { } game || Busy || IsInstallingCyberpunkRequirements) return;
        var dir = game.View.InstallDir;
        if (string.IsNullOrEmpty(dir))
        {
            StatusText = "Game is not installed on this PC.";
            return;
        }

        var controller = _controller;
        var slug = game.Slug;
        ClearFinishedModUpdate(slug, dir);
        Busy = true;
        var finalStatus = $"Finished preparing {game.DisplayName}.";
        var setupFailed = true;
        StatusText = game.View.Engine switch
        {
            GameEngine.Unreal => $"Downloading and checking the VR setup for {game.DisplayName}…",
            GameEngine.Redengine => $"Downloading and installing the VR backend for {game.DisplayName}…",
            GameEngine.LegacyD3D9 => $"Checking the VR bridge for {game.DisplayName}…",
            _ => $"Installing the VR mod for {game.DisplayName}…"
        };
        ReportGameSetup(slug, dir, StatusText, true);
        try
        {
            using var http = NewHttp();
            ActionOutcome outcome;
            if (game.View.Engine is GameEngine.Unreal)
            {
                ReportGameSetup(slug, dir, "Downloading and checking UEVR and its runtime...", true);
                outcome = await controller.PrepareUnrealAsync(slug, new HttpDownloader(http));
                // A game whose VR profile is not redistributable has it fetched from its source.
                if (outcome.Ok && game.View.ProfileImportRequired)
                {
                    ReportGameSetup(slug, dir, "Downloading, checking and installing this game's reviewed VR profile...", true);
                    outcome = await controller.DownloadUnrealProfileAsync(slug, new HttpDownloader(http));
                }
            }
            else if (game.View.Engine is GameEngine.Redengine)
            {
                ReportCyberpunkSetup(dir, "Downloading and installing CyberpunkVR Port...", true);
                outcome = await controller.InstallCyberpunkVrAsync(dir, new HttpDownloader(http));
                if (outcome.Ok)
                {
                    var requirements = await InstallCyberpunkRequirementsCoreAsync(dir);
                    outcome = new ActionOutcome(requirements.Ok, requirements.Message);
                }
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
            setupFailed = !outcome.Ok;
        }
        catch (Exception ex)
        {
            finalStatus = $"Preparation failed: {ex.Message}";
        }
        finally
        {
            Busy = false;
            if (game.IsCyberpunk) ReportCyberpunkSetup(dir, finalStatus, false);
            ReportGameSetup(slug, dir, finalStatus, false, setupFailed);
            RefreshGame(slug);
            StatusText = finalStatus;
        }
    }

    private async Task<CyberpunkBackendOutcome> InstallCyberpunkRequirementsCoreAsync(string gameRoot)
    {
        if (IsInstallingCyberpunkRequirements)
            return new(false, "Cyberpunk requirements setup is already running.");
        ClearFinishedModUpdate("cyberpunk-2077", gameRoot);
        IsInstallingCyberpunkRequirements = true;
        ReportCyberpunkSetup(gameRoot, "Checking for the More Occluders download...", true);
        var acceptProgress = true;
        _cyberpunkRequirementsCancellation = new CancellationTokenSource();
        try
        {
            var result = await new CyberpunkNexusRequirements().InstallAsync(gameRoot,
                url => Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }),
                progress: new Progress<string>(message => { if (acceptProgress) ReportCyberpunkSetup(gameRoot, message, true); }),
                ct: _cyberpunkRequirementsCancellation.Token);
            acceptProgress = false;
            ReportCyberpunkSetup(gameRoot, result.Ok
                ? "Installation complete: VR port and More Occluders are installed. Choose a VR mode below; headset validation remains pending."
                : "Setup incomplete: " + result.Message, false);
            return result;
        }
        catch (OperationCanceledException)
        {
            acceptProgress = false;
            ReportCyberpunkSetup(gameRoot, "Setup paused: installation is incomplete. Resume Nexus download authorization to continue.", false);
            throw;
        }
        catch (Exception ex)
        {
            acceptProgress = false;
            ReportCyberpunkSetup(gameRoot, "Setup failed: " + ex.Message, false);
            throw;
        }
        finally
        {
            _cyberpunkRequirementsCancellation.Dispose();
            _cyberpunkRequirementsCancellation = null;
            IsInstallingCyberpunkRequirements = false;
        }
    }

    [RelayCommand]
    private async Task InstallCyberpunkRequirementsAsync()
    {
        // Capture this game's root before awaiting. Switching the selected title
        // while Nexus downloads cannot redirect the installation to another game.
        if (IsInstallingCyberpunkRequirements || Busy || SelectedGame?.IsCyberpunk != true || SelectedGame.View.InstallDir is not { } gameRoot) return;
        string message;
        try { message = (await InstallCyberpunkRequirementsCoreAsync(gameRoot)).Message; }
        catch (OperationCanceledException) { message = "Cyberpunk requirements download detection cancelled. Use Install requirements to resume."; }
        catch (Exception ex) { message = $"Cyberpunk requirements setup: {ex.Message}"; }
        RefreshGame("cyberpunk-2077");
        StatusText = message;
    }

    public async Task ImportCyberpunkRequirementZipAsync(string gameRoot, string archivePath)
    {
        // The root was captured before the file picker opened. Selection changes
        // cannot redirect this archive to another title.
        if (!Games.Any(game => game.IsCyberpunk && string.Equals(game.View.InstallDir, gameRoot, StringComparison.OrdinalIgnoreCase))) return;
        if (Busy || IsInstallingCyberpunkRequirements)
        { StatusText = "Pause automatic setup before importing a ZIP, then retry."; return; }
        ClearFinishedModUpdate("cyberpunk-2077", gameRoot);
        Busy = true;
        var message = "Installation did not complete.";
        ReportCyberpunkSetup(gameRoot, "Validating and installing the selected More Occluders ZIP...", true);
        try
        {
            if (CyberpunkUpstreamSession.ReadState(gameRoot) is null)
            { message = "Install VR at the top before importing More Occluders."; return; }
            var result = await Task.Run(() => CyberpunkNexusRequirements.Import(gameRoot, archivePath));
            message = result.Ok ? "Installation complete. VR port and More Occluders are installed; headset validation remains pending." : result.Message;
        }
        catch (Exception ex) { message = "ZIP import failed: " + ex.Message; }
        finally
        {
            Busy = false;
            ReportCyberpunkSetup(gameRoot, message, false);
            RefreshGame("cyberpunk-2077");
            StatusText = message;
        }
    }

    [RelayCommand]
    private void CancelCyberpunkRequirements() => _cyberpunkRequirementsCancellation?.Cancel();

    /// Start the selected game in one of the four play modes. `acknowledge` is the
    /// player's answer to the private-session warning; it is never assumed here.
    public async Task RunPlayModeAsync(PlayMode mode, bool acknowledge)
    {
        if (_controller is null || SelectedGame is not { } game || Busy) return;
        if (mode == PlayMode.Vanilla) { await RunCleanMonitorAsync(game.View); return; }
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
        var launchSucceeded = false;
        StatusText = $"Starting {game.DisplayName}…";
        try
        {
            if (mode == PlayMode.ModsNoVr && VrClient.Core.Compatibility.MonitorModSession.IsParked(dir))
            { finalStatus = "Mods are parked for clean monitor play. Close the game and restore parked mods before choosing play with mods."; return; }
            if (mode is PlayMode.VrOnly or PlayMode.VrWithMods)
            {
                var fresh = _controller.GetGame(game.Slug);
                if (fresh is null || !VrClient.Core.Compatibility.GameInstallationStore.SameRoot(fresh.InstallDir, dir))
                { finalStatus = "The selected installation changed. Refresh and choose the intended game copy before launching."; return; }
                var prepared = await PrepareCompatibleVrCoreAsync(fresh);
                if (!prepared.Ok) { finalStatus = prepared.Message; return; }
            }
            var native = new VrClient.Core.Launch.NativeDependencyHealth().Check(dir,
                VrClient.Core.Config.ControllerRuntimeBindings.Executables(_controller.RepoRoot, game.View));
            if (!native.Healthy) { finalStatus = native.Message; return; }
            if (mode is PlayMode.VrOnly or PlayMode.VrWithMods)
            {
                if (File.Exists(Path.Combine(dir, ".avrcade", "mod-updates", "pending.json")))
                { finalStatus = "An interrupted mod update needs recovery. Close the game and choose Recheck setup before launching VR."; return; }
                var modRoots = mode == PlayMode.VrWithMods && game.Modded?.SelectedProfile?.Directory is { } profileRoot
                    ? new[] { profileRoot } : null;
                var controls = new VrClient.Core.Config.ControllerRuntimeBindings(_controller.Compatibility.ControllersFor(game.View)).Prepare(_controller.RepoRoot, game.View, modRoots);
                if (!controls.Ok) { finalStatus = controls.Message; return; }
            }
            var outcome = game.View.Engine switch
            {
                _ when game.IsYapyap && mode is (PlayMode.VrOnly or PlayMode.VrWithMods) =>
                    await _controller.LaunchYapyapVrAsync(mode,
                        managerExecutable: mode == PlayMode.VrWithMods ? GameModManagerExecutable(game) : null),
                _ when game.IsYapyap => _controller.LaunchYapyapDesktop(mode,
                    managerExecutable: mode == PlayMode.ModsNoVr ? GameModManagerExecutable(game) : null),
                GameEngine.Unity => LaunchUnity(game, dir, mode, acknowledge),
                GameEngine.Unreal => mode is PlayMode.VrOnly or PlayMode.VrWithMods
                    ? await _controller.LaunchUnrealAsync(game.Slug, acknowledge, gameRoot: dir)
                    : _controller.Compatibility.CanUseStoreHandoff(game.View)
                        ? _controller.LaunchUnrealFlat(game.Slug)
                        : _controller.Compatibility.LaunchSelectedExecutable(game.View),
                GameEngine.Redengine => await LaunchCyberpunkAsync(game, mode),
                GameEngine.LegacyD3D9 => LaunchGtaSanAndreas(dir, mode),
                _ => new ActionOutcome(false, "This game has no launch route in AVRcade yet.")
            };
            var feedbackOutcome = outcome;
            if (outcome.Ok && outcome.LaunchedProcessId is > 0 && outcome.LaunchedExecutable is { } launchedExecutable)
            {
                StatusText = $"Checking {game.DisplayName} startup…";
                var startup = await VrClient.Core.Launch.GameStartupMonitor.ObserveAsync(
                    outcome.LaunchedProcessId.Value, launchedExecutable);
                feedbackOutcome = outcome with
                {
                    Ok = startup.Running,
                    Message = $"{game.DisplayName}: {startup.Message}"
                };
                if (mode is PlayMode.VrOnly or PlayMode.VrWithMods)
                    feedbackOutcome = feedbackOutcome with { Message = feedbackOutcome.Message +
                        _controller.Compatibility.CompleteStartup(game.View, startup.Running,
                            startup.ExitCode is not null and not 0) };
            }
            else if (mode is PlayMode.VrOnly or PlayMode.VrWithMods)
            {
                if (outcome.Ok)
                {
                    StatusText = $"Checking {game.DisplayName}'s selected process…";
                    var observed = await _controller.Compatibility.ObserveStartupAsync(game.View);
                    feedbackOutcome = outcome with { Ok = observed == VrClient.Core.Compatibility.StartupObservation.Running,
                        Message = outcome.Message + (observed == VrClient.Core.Compatibility.StartupObservation.Running
                            ? " Startup was observed; check stereo, tracking and controls in the headset."
                            : " Startup was not confirmed for the selected installation; no VR readiness claim was made.") +
                            _controller.Compatibility.CompleteStartup(game.View,
                                observed == VrClient.Core.Compatibility.StartupObservation.Running,
                                observed == VrClient.Core.Compatibility.StartupObservation.ExitedWithError) };
                }
                else if (outcome.Message.Contains("closed during startup (exit code 0x", StringComparison.Ordinal) &&
                    !outcome.Message.Contains("exit code 0x00000000", StringComparison.Ordinal))
                    feedbackOutcome = outcome with { Message = outcome.Message + _controller.Compatibility.CompleteStartup(game.View, false, true) };
            }
            (finalStatus, launchSucceeded) = DescribeLaunchFeedback(game.DisplayName, mode, feedbackOutcome);
        }
        catch (Exception ex)
        {
            finalStatus = $"Launch failed: {ex.Message}";
            launchSucceeded = false;
        }
        finally
        {
            Busy = false;
            RefreshGame(game.Slug);
            StatusText = finalStatus;
            StatusSuccess = launchSucceeded;
        }
    }

    private static (string Text, bool Success) DescribeLaunchFeedback(
        string gameName, PlayMode mode, ActionOutcome outcome)
    {
        if (!outcome.Ok || mode is not (PlayMode.VrOnly or PlayMode.VrWithMods))
            return (outcome.Message, false);

        // ActionOutcome.Ok also covers opening a manager or a test launcher.
        // Those handoffs, and an unconfirmed UEVR attachment, are not VR success.
        var message = outcome.Message;
        if (message.Contains("AVRcade has not launched the game", StringComparison.OrdinalIgnoreCase) ||
            message.StartsWith("Mod manager opened.", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("test opened", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("has not confirmed it attached", StringComparison.OrdinalIgnoreCase))
            return (message, false);

        if (message.StartsWith("RESULT: cyberpunk-launch ", StringComparison.Ordinal) &&
            message.Contains("started=true", StringComparison.Ordinal))
            return ($"VR launch succeeded: {gameName}. Put on your headset to check the image and tracking.", true);
        if (message.StartsWith("launched pid=", StringComparison.Ordinal))
            return ($"VR launch succeeded: {gameName}. Put on your headset to check the image and tracking.", true);
        if (message.StartsWith("Cyberpunk 2077 process is running in VR mode.", StringComparison.Ordinal) ||
            message.Contains("The game process is running.", StringComparison.Ordinal))
            return ($"VR launch succeeded: {gameName}. {message}", true);
        if (message.StartsWith("UEVR attached to the game.", StringComparison.Ordinal))
            return ($"VR connected: {gameName}. {message}", true);
        if (message.StartsWith("Started ", StringComparison.Ordinal))
            return ($"VR launch succeeded: {gameName}. {message}", true);

        // Steam and MO2 can accept a request before the game or headset is ready.
        return ($"VR launch request sent: {gameName}. {message}", true);
    }

    private void ReportLaunchOutcome(string gameName, PlayMode mode, ActionOutcome outcome)
    {
        var feedback = DescribeLaunchFeedback(gameName, mode, outcome);
        StatusText = feedback.Text;
        StatusSuccess = feedback.Success;
    }

    private ActionOutcome LaunchUnity(GameItemViewModel game, string dir, PlayMode mode, bool acknowledge)
    {
        Enum.TryParse<Verdict>(game.View.SafetyVerdict, out var safety);
        var modded = game.Modded;
        var launcher = new ModdedVrLauncher();
        if (mode is PlayMode.VrWithMods or PlayMode.ModsNoVr &&
            (modded?.SelectedProfile is not null || modded?.SelectedFlatProfile is not null) &&
            !_controller!.Compatibility.CanUseStoreHandoff(game.View))
            return new(false, "This mod manager's Steam handoff cannot guarantee the selected older copy. Use game-folder mods for this copy, or configure and launch that copy in the manager itself.");
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
            return await _controller!.LaunchRedengineAsync(CyberpunkLaunchMode.Vr, withMods: withMods, gameRoot: game.View.InstallDir);
        // With the VR backend installed AVRcade starts the game itself, VR switched
        // off; without it a normal Steam start is the only route.
        return game.View.ModInstalled && game.View.Readiness is not GameReadiness.Blocked
            ? await _controller!.LaunchRedengineAsync(CyberpunkLaunchMode.Flat, withMods: withMods, gameRoot: game.View.InstallDir)
            : _controller!.Compatibility.CanUseStoreHandoff(game.View)
                ? _controller.LaunchCyberpunkFlatViaSteam(withMods)
                : new(false, "This installation needs a compatible VR backend to disable VR while retaining mods. Use Clean monitor play, or prepare a compatible package for this copy first.");
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
            ReportLaunchOutcome("Fallout 3", requireVortex ? PlayMode.VrWithMods : PlayMode.VrOnly,
                _controller.LaunchFallout3Native(installDir, requireVortex));
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
        try
        {
            ReportLaunchOutcome("Fallout: New Vegas", PlayMode.VrWithMods,
                await LaunchFalloutNewVegasVortexCoreAsync());
        }
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
        if (SelectedGame is not { Modded: { } modded } game) return;
        var message = modded.SaveMovedProfileRoot();
        if (message.StartsWith("Saved", StringComparison.Ordinal))
            RefreshGame(game.Slug);
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
            RefreshGame(game.Slug);
            StatusText = finalStatus;
        }
    }

    public void Dispose()
    {
        _modUpdateCancellation?.Cancel();
        _cyberpunkRequirementsCancellation?.Cancel();
        Streaming.Dispose();
        foreach (var game in Games)
            game.Dispose();
        foreach (var cover in _covers.Values)
            cover.Dispose();
        _covers.Clear();
        _falloutNewVegasService?.Dispose();
    }
}
