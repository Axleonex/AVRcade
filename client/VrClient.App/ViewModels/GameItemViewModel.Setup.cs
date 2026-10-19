using Avalonia.Media;
using VrClient.App.Services;
using VrClient.Core.App;
using VrClient.Core.Config;

namespace VrClient.App.ViewModels;

public sealed partial class GameItemViewModel
{
    private bool _steamVrRunning;
    private GameSetupGuide? _setupGuide;
    private string? _setupMessage;
    private bool _setupBusy;
    private bool _setupFailed;

    public bool HasSetupCard => true; // Every current public catalogue entry has a VR route.
    public bool SetupBusy => IsCyberpunk ? CyberpunkSetupBusy : _setupBusy;
    public bool SetupBlocked => View.Readiness is GameReadiness.Blocked;
    public bool SetupPackagesInstalled => IsCyberpunk ? CyberpunkSetupComplete
        : IsInstalled && View.ModInstalled && (!View.ProfileImportRequired || View.ProfileImported);
    public string SetupTitle => IsCyberpunk ? CyberpunkSetupTitle
        : SetupBusy ? "VR setup in progress"
        : !IsInstalled ? "Game installation needed"
        : SetupBlocked ? "VR setup blocked"
        : _setupFailed ? "VR setup needs attention"
        : SetupPackagesInstalled ? "VR packages installed" : "VR setup incomplete";
    public IBrush SetupBrush => IsCyberpunk ? CyberpunkSetupBrush
        : LauncherThemeService.Brush(SetupBlocked || _setupFailed ? "WarningBrush"
            : SetupPackagesInstalled && !SetupBusy ? "GoodBrush" : "AccentBrush");
    public string SetupStatus => IsCyberpunk ? CyberpunkSetupStatus
        : _setupMessage ?? (!IsInstalled ? NotInstalledText
        : SetupBlocked ? View.ReadinessDetail
        : SetupPackagesInstalled ? "The required packages are present. Headset connection and each launch mode are checked separately below."
        : IsYapyap || IsFallout3 || IsFalloutNewVegas ? View.ReadinessDetail
        : SetupCanInstall ? "Install the required VR components here. AVRcade downloads, checks and places them for this game."
        : View.ReadinessDetail);
    public string SetupPackageStatus => IsCyberpunk ? CyberpunkBackendStatus
        : (IsUnreal ? "UEVR tools and runtime" : "VR package") + (View.ModInstalled ? ": installed" : ": not installed");
    public bool HasSetupProfileStatus => !IsCyberpunk && View.ProfileImportRequired;
    public string SetupProfileStatus => View.ProfileImported ? "Game VR profile: installed" : "Game VR profile: not installed";
    public string SetupRuntimeStatus => IsYapyap
        ? _steamVrRunning ? "SteamVR: running" : "SteamVR: not detected"
        : _runtimeDetected ? "Headset software: running" : "Headset software: not detected";
    public bool ShowSetupRuntimeStatus => !IsCyberpunk;
    public bool SetupCanInstall => !IsCyberpunk && IsInstalled && !SetupBlocked && !SetupBusy &&
        !IsYapyap && !IsFallout3 && !IsFalloutNewVegas &&
        PlayModes.Any(mode => mode.IsVr && mode.State.Setup is PlayModeSetup.InstallVr);
    public bool SetupCanImportProfile => CanImportUevrProfile && !SetupBusy && !SetupBlocked;
    public bool SetupNeedsProfileImport => SetupCanImportProfile && !View.ProfileImported && _setupFailed;
    public bool HasSetupHelp => !IsCyberpunk && HasSetupCard && (_setupGuide?.Steps.Count > 0 || _setupFailed);
    public bool SetupNeedsDetails => IsInstalled && !SetupBusy && !SetupPackagesInstalled && !SetupCanInstall && HasSetupHelp;
    public string? SetupSourceUrl => _setupGuide?.SourceUrl ?? UevrProfileSourceUrl ?? ModCredits.FirstOrDefault()?.Url;
    public bool HasSetupSource => !CanImportUevrProfile && !string.IsNullOrWhiteSpace(SetupSourceUrl);
    public IReadOnlyList<string> SetupRecoverySteps => _setupGuide?.Steps ??
        ["Close the game, check the reported error and retry the install action at the top. AVRcade installs the configured packages in the correct location.",
         "If you changed the installation or deployed mods in a manager, choose Recheck setup. Manager profiles are separate from the VR-only game-folder installation."];

    public void SetSetupGuide(GameSetupGuide? guide)
    {
        _setupGuide = guide;
        NotifySetupChanged();
    }

    public void SetSetupProgress(string message, bool busy, bool failed = false)
    {
        _setupMessage = message;
        _setupBusy = busy;
        _setupFailed = failed;
        NotifySetupChanged();
    }

    private void NotifySetupChanged()
    {
        foreach (var name in new[] { nameof(HasSetupCard), nameof(SetupBusy), nameof(SetupTitle), nameof(SetupBrush),
            nameof(SetupStatus), nameof(SetupPackageStatus), nameof(SetupProfileStatus), nameof(SetupRuntimeStatus),
            nameof(SetupCanInstall), nameof(SetupCanImportProfile), nameof(SetupNeedsProfileImport), nameof(HasSetupHelp), nameof(SetupNeedsDetails),
            nameof(SetupRecoverySteps), nameof(SetupSourceUrl), nameof(HasSetupSource) })
            OnPropertyChanged(name);
    }
}
