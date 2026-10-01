using System.Collections.ObjectModel;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using VrClient.Core.App;
using VrClient.Core.ModManagers;
using VrClient.Core.Modpack;

namespace VrClient.App.ViewModels;

/// Mod-manager state for a community-converted Unity game: which manager the
/// player uses, and the two profiles AVRcade remembers for it (one with the VR
/// mod for "VR + mods", one without for "Mods, no VR"). Managers own their
/// profiles; AVRcade only reads them and passes their loader to Steam.
public sealed partial class ModdedGameViewModel : ObservableObject, IDisposable
{
    private readonly ModdedVrPreferences _preferences;
    private readonly Action _selectionChanged;
    private readonly string _gameSlug;
    private readonly bool _gameInstalled;
    private readonly bool _launchAllowed;
    private readonly bool _offlineSteamLibrary;
    private readonly bool _runtimeDetected;
    private readonly bool _steamVrRunning;
    private readonly bool _gameDirectoryConversionPresent;
    private readonly bool _conversionIsVrOnly;
    private readonly string? _gameDirectory;
    private readonly string? _iconUrl;
    private readonly CancellationTokenSource _previewCancellation = new();
    private bool _disposed;

    public CommunityVrRoute Route { get; }
    public ObservableCollection<ModManagerProfile> Profiles { get; }
    public IReadOnlyList<ModManagerInstall> InstalledManagers { get; }
    public IReadOnlyList<ModManagerInstall> ManagersWithDataRoots { get; }
    public bool CanFindMovedProfiles => ManagersWithDataRoots.Count > 0;
    public bool HasInstalledManagers => InstalledManagers.Count > 0;
    // Setup prompts are shown only while their step is still open.
    public bool NoInstalledManagers => !HasInstalledManagers;
    public string LocateManagerText => HasInstalledManagers ? "Locate another manager" : "Locate manager .exe";
    public bool NeedsVrProfileHelp => SelectedProfile is not { VrModPresent: true, LoaderPresent: not false };
    public bool NeedsFlatProfileHelp => SelectedFlatProfile is not { VrModPresent: false, LoaderPresent: not false };
    public string ManagedInstallLocationText => _gameDirectory is null
        ? "The VR mod installs into the Steam game folder once the game is found."
        : $"Installs into {_gameDirectory}. Manager profiles are never touched.";
    public bool HasProfiles => Profiles.Count > 0;
    public bool NoProfiles => !HasProfiles;
    public bool HasSelectedProfile => SelectedProfile is not null;
    public bool HasSelectedFlatProfile => SelectedFlatProfile is not null;
    public bool CanPrepareManaged => _gameInstalled && _launchAllowed && !_gameDirectoryConversionPresent;

    /// The facts the four play modes are decided from.
    public UnityPlayFacts PlayFacts() => new(
        _gameInstalled, _launchAllowed, _gameDirectoryConversionPresent, _conversionIsVrOnly,
        Describe(SelectedProfile, ModdedLaunchMode.VrWithMods, SavedR2LaunchArguments),
        Describe(SelectedFlatProfile, ModdedLaunchMode.DesktopWithMods, null),
        HasInstalledManagers, _runtimeDetected, Route.RequiresSteamVr, _steamVrRunning);

    private PlayModeProfile? Describe(ModManagerProfile? profile, ModdedLaunchMode mode, string? savedArguments) =>
        profile is null ? null : new PlayModeProfile(
            profile.DisplayName, profile.Manager.DisplayName, profile.VrModPresent, profile.LoaderPresent,
            profile.HasOtherMods,
            ModdedVrLauncher.TryResolveProfileArguments(profile, savedArguments, mode, out _),
            ModdedVrLauncher.IsLoaderLinked(_gameDirectory, profile));

    public string InstalledManagersText { get; }
    public string DependenciesText { get; }
    public string CreatorText => $"{Route.Package} by {Route.Creator}";
    public string VrModIdentityText =>
        $"VR mod to add to your VR profile: {Route.Package} by {Route.Namespace} (package ID {ModManagerVrSetupGuide.PackageIdentity(Route)}).";
    public string VrWithModsManagerInstructions => SelectedManager is { } manager
        ? ModManagerVrSetupGuide.Instructions(Route, manager.Kind)
        : "Install a mod manager, then choose it here to see how to add this VR mod.";
    public bool HasSteamVrNotice => Route.RequiresSteamVr && !_steamVrRunning;
    public string SteamVrNoticeText =>
        "This VR mod needs SteamVR. Start SteamVR and connect your headset before launching; Virtual Desktop can provide the wireless link. If you still see a desktop screen, choose Switch to VR in the headset.";
    public string FlatModeInstructions => SelectedManager is { } manager
        ? $"In {manager.DisplayName}, keep a second profile with the mod loader and your mods but without {Route.PackageKey}, then pick it below."
        : $"Keep a second profile with the mod loader and your mods but without {Route.PackageKey}.";
    public string Summary => Route.Summary;
    public string Warning => Route.Warning + " Private, consenting modded multiplayer sessions only.";
    public bool HasGameDirectoryConflict => _gameDirectoryConversionPresent &&
        (SelectedProfile is not null || SelectedFlatProfile is not null);
    public string GameDirectoryConflictText =>
        "AVRcade's own VR mod is also installed in the game folder. A profile launch redirects the mod loader to the profile, so the game-folder copy stays unused for that launch. AVRcade does not alter either install.";
    public string OriginalPageUrl => Route.PageUrl;
    public bool HasPreviewImage => PreviewImage is not null;

    public string VrProfileStatusText => SelectedProfile switch
    {
        null => _gameDirectoryConversionPresent && !_conversionIsVrOnly
            ? "No profile chosen. VR + mods uses the mods already in the game folder."
            : "No profile chosen yet.",
        { VrModPresent: false } => $"This profile does not include {Route.PackageKey}. Add it in the manager, then reread profiles.",
        { LoaderPresent: false } => "This profile has no mod loader. Check its BepInEx dependency in the manager.",
        { VrModPresent: null } => "AVRcade cannot read this manager's mod list. Confirm the VR mod and loader there; the game starts from the manager.",
        { } profile when !ModdedVrLauncher.IsLoaderLinked(_gameDirectory, profile) =>
            "Profile looks right. Press Start modded once in the manager so it adds its mod loader to the game folder.",
        _ => "Profile looks right. Game build and mod combinations are not verified by AVRcade."
    };

    public string FlatProfileStatusText => SelectedFlatProfile switch
    {
        null => "No profile chosen yet.",
        { VrModPresent: true } => $"This profile includes {Route.PackageKey}. Pick one without it for monitor play.",
        { VrModPresent: null } => "AVRcade cannot read this manager's mod list. The game starts from the manager.",
        { LoaderPresent: false } => "This profile has no mod loader. Check its BepInEx dependency in the manager.",
        _ => "Profile looks right for monitor play."
    };

    public string ReadinessText => !_gameInstalled
        ? _offlineSteamLibrary ? "Reconnect the Steam library that contains this game, then refresh."
            : "Install the game in Steam, or point AVRcade at it if it is already on a drive."
        : !_gameDirectoryConversionPresent
            ? "The game is installed. Install the VR mod to play in VR, or use a mod manager profile."
            : !_runtimeDetected
                ? "The VR mod is installed. Start SteamVR or Virtual Desktop and connect your headset, then refresh."
                : Route.RequiresSteamVr && !_steamVrRunning
                    ? "The VR mod is installed. Start SteamVR before playing this game in VR."
                    : "The VR mod is installed and a headset runtime is running.";

    public bool IsR2Profile => SelectedProfile?.Manager.Kind == ModManagerKind.R2Modman;
    public string? SavedR2LaunchArguments => SelectedProfile is { } profile &&
        _preferences.ForGame(_gameSlug) is { } saved && saved.ProfileKey == profile.Key
            ? saved.R2LaunchArguments : null;
    public string VrModsText => ModList(SelectedProfile);
    public string FlatModsText => ModList(SelectedFlatProfile);
    private static string ModList(ModManagerProfile? profile) => profile switch
    {
        null => "No profile chosen.",
        { Mods.Count: > 0 } => string.Join(", ", profile.Mods),
        _ => "No readable mod list for this profile."
    };

    [ObservableProperty] private ModManagerProfile? _selectedProfile;
    [ObservableProperty] private ModManagerProfile? _selectedFlatProfile;
    [ObservableProperty] private ModManagerInstall? _selectedManager;
    [ObservableProperty] private string _r2LaunchArguments = string.Empty;
    [ObservableProperty] private Bitmap? _previewImage;
    [ObservableProperty] private ModManagerInstall? _managerForMovedProfiles;
    [ObservableProperty] private string _movedProfileRoot = string.Empty;

    /// installedManagers and dataRoots are looked up once per catalogue refresh and
    /// shared by every game's view model.
    public ModdedGameViewModel(string repoRoot, CommunityVrRoute route, string? gameDirectory,
        bool gameInstalled, bool launchAllowed,
        bool offlineSteamLibrary, bool runtimeDetected, bool steamVrRunning,
        bool gameDirectoryConversionPresent,
        ModManagerDiscovery discovery, IReadOnlyList<ModManagerInstall> installedManagers,
        IReadOnlyDictionary<ModManagerKind, string> dataRoots,
        ModdedVrPreferences preferences, Action selectionChanged)
    {
        Route = route;
        _gameSlug = route.Slug;
        _gameInstalled = gameInstalled;
        _launchAllowed = launchAllowed;
        _offlineSteamLibrary = offlineSteamLibrary;
        _runtimeDetected = runtimeDetected;
        _steamVrRunning = steamVrRunning;
        _gameDirectoryConversionPresent = gameDirectoryConversionPresent;
        _gameDirectory = gameDirectory;
        _conversionIsVrOnly = gameDirectoryConversionPresent && gameDirectory is not null &&
            ManagedModIsolation.IsVrOnly(gameDirectory);
        _preferences = preferences;
        _selectionChanged = selectionChanged;
        InstalledManagers = installedManagers;
        ManagersWithDataRoots = installedManagers.Where(m => m.Kind != ModManagerKind.Vortex).ToArray();
        Profiles = new(discovery.Discover(route.Slug, dataRoots, installedManagers));
        InstalledManagersText = InstalledManagers.Count == 0
            ? "No mod manager found on this PC yet."
            : "Found: " + string.Join(", ", InstalledManagers.Select(m => m.DisplayName));
        var lockfile = Path.Combine(repoRoot, "config", "modpacks", $"{route.Slug}.lock.json");
        var packages = File.Exists(lockfile) ? LockfileIo.Read(lockfile).Packages : [];
        DependenciesText = packages.Count > 0
            ? string.Join(", ", packages
                .Where(p => $"{p.Namespace}-{p.Name}" != route.PackageKey)
                .Select(p => $"{p.Namespace}-{p.Name}"))
            : "See the mod page for current dependencies.";
        var vrPackage = packages.FirstOrDefault(p => $"{p.Namespace}-{p.Name}" == route.PackageKey);
        if (vrPackage is not null)
        {
            var cdn = route.Slug == "big-walk" ? "gcdn" : "ccdn";
            _iconUrl = $"https://{cdn}.thunderstore.io/live/repository/icons/" +
                $"{route.Namespace}-{route.Package}-{vrPackage.Version}.png";
        }
        var saved = preferences.ForGame(route.Slug);
        _selectedProfile = Profiles.FirstOrDefault(p => p.Key == saved?.ProfileKey);
        var savedFlat = preferences.FlatProfileKey(route.Slug);
        _selectedFlatProfile = Profiles.FirstOrDefault(p => p.Key == savedFlat);
        _selectedManager = _selectedProfile?.Manager ?? _selectedFlatProfile?.Manager
            ?? InstalledManagers.FirstOrDefault();
        _r2LaunchArguments = _selectedProfile is null ? string.Empty
            : saved?.R2LaunchArguments ?? string.Empty;
        if ((saved is not null && _selectedProfile is null) || (savedFlat is not null && _selectedFlatProfile is null))
            InstalledManagersText += " A saved profile was not found; reconnect its manager data folder or pick it again.";
    }

    partial void OnSelectedProfileChanged(ModManagerProfile? value)
    {
        if (value is not null)
        {
            _preferences.Select(_gameSlug, value.Key);
            R2LaunchArguments = _preferences.ForGame(_gameSlug)?.R2LaunchArguments ?? string.Empty;
        }
        else _preferences.UseManaged(_gameSlug);
        RaiseState();
    }

    partial void OnSelectedFlatProfileChanged(ModManagerProfile? value)
    {
        _preferences.SelectFlat(_gameSlug, value?.Key);
        RaiseState();
    }

    // The manager choice only decides which manager opens and whose steps are shown;
    // the two profile pickers list every manager's profiles.
    partial void OnSelectedManagerChanged(ModManagerInstall? value)
    {
        OnPropertyChanged(nameof(VrWithModsManagerInstructions));
        OnPropertyChanged(nameof(FlatModeInstructions));
    }
    partial void OnPreviewImageChanged(Bitmap? value) => OnPropertyChanged(nameof(HasPreviewImage));
    partial void OnManagerForMovedProfilesChanged(ModManagerInstall? value) =>
        MovedProfileRoot = value?.DataRoot ?? string.Empty;

    public string SaveMovedProfileRoot()
    {
        if (ManagerForMovedProfiles is not { } manager || manager.Kind == ModManagerKind.Vortex)
            return "Choose r2modman or Thunderstore Mod Manager first.";
        try
        {
            _preferences.SetDataRoot(manager.Kind, MovedProfileRoot);
            return "Saved the manager's data folder. Profiles are being refreshed.";
        }
        catch (Exception ex) when (ex is IOException or ArgumentException or UnauthorizedAccessException)
        {
            return ex.Message;
        }
    }

    public async Task LoadPreviewAsync()
    {
        if (PreviewImage is not null || _iconUrl is null || _disposed)
            return;
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };
            using var response = await http.GetAsync(_iconUrl, HttpCompletionOption.ResponseHeadersRead,
                _previewCancellation.Token);
            response.EnsureSuccessStatusCode();
            if (response.Content.Headers.ContentLength is > 1_000_000) return;
            var bytes = await response.Content.ReadAsByteArrayAsync(_previewCancellation.Token);
            if (bytes.Length > 1_000_000 || _disposed) return;
            using var stream = new MemoryStream(bytes);
            PreviewImage = new Bitmap(stream);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or
                                   InvalidOperationException or ArgumentException or IOException or
                                   ObjectDisposedException)
        {
            // The mod page link and text stay available offline.
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _previewCancellation.Cancel();
        PreviewImage?.Dispose();
        _previewCancellation.Dispose();
    }

    private void RaiseState()
    {
        OnPropertyChanged(nameof(IsR2Profile));
        OnPropertyChanged(nameof(HasSelectedProfile));
        OnPropertyChanged(nameof(HasSelectedFlatProfile));
        OnPropertyChanged(nameof(VrProfileStatusText));
        OnPropertyChanged(nameof(FlatProfileStatusText));
        OnPropertyChanged(nameof(NeedsVrProfileHelp));
        OnPropertyChanged(nameof(NeedsFlatProfileHelp));
        OnPropertyChanged(nameof(HasGameDirectoryConflict));
        OnPropertyChanged(nameof(VrModsText));
        OnPropertyChanged(nameof(FlatModsText));
        _selectionChanged();
    }

    /// Forget the VR profile: "VR + mods" then falls back to mods in the game folder.
    public void ClearVrProfile() => SelectedProfile = null;

    public void ClearFlatProfile() => SelectedFlatProfile = null;

    public string SaveR2Arguments()
    {
        if (SelectedProfile is not { Manager.Kind: ModManagerKind.R2Modman } profile)
            return "Select an r2modman VR profile first.";
        if (!string.IsNullOrWhiteSpace(R2LaunchArguments) &&
            !ModdedVrLauncher.TryParseR2Arguments(profile, R2LaunchArguments, out _, out var reason))
            return reason;
        _preferences.SetR2LaunchArguments(_gameSlug, profile.Key, R2LaunchArguments);
        RaiseState();
        return string.IsNullOrWhiteSpace(R2LaunchArguments)
            ? "Automatic profile launch enabled. AVRcade checks the loader before each launch."
            : "Saved custom launch arguments. AVRcade rechecks them before each launch.";
    }
}
