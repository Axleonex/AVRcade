using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using VrClient.App.Services;
using VrClient.Core.FalloutNewVegas;

namespace VrClient.App.ViewModels;

public sealed record FalloutNewVegasCheckViewModel(string State, string Component, string Message)
{
    public string StateText => State switch
    {
        "pass" => "PASS",
        "warning" => "CHECK",
        _ => "NEEDED"
    };
}

public partial class FalloutNewVegasSetupViewModel : ObservableObject
{
    private readonly FalloutNewVegasDesktopService _service;
    private FnvVortexSyncPlan? _vortexPlan;

    [ObservableProperty] private string _gameDirectory;
    [ObservableProperty] private FnvStorefront _storefront;
    [ObservableProperty] private string _modOrganizerExecutablePath;
    [ObservableProperty] private string _modOrganizerInstanceDirectory;
    [ObservableProperty] private string _sourceProfileName;
    [ObservableProperty] private string _vrProfileName;
    [ObservableProperty] private string _trackerExecutablePath;
    [ObservableProperty] private string _nativeAdapterExecutablePath;
    [ObservableProperty] private string _steamVrDirectory;
    [ObservableProperty] private bool _useVirtualDesktop;
    [ObservableProperty] private string _virtualDesktopExecutablePath;
    [ObservableProperty] private string _statusText = "Native stereo is experimental and not headset-verified. It does not require vorpX; AVRcade prepares an isolated MO2 profile.";
    [ObservableProperty] private bool _busy;
    [ObservableProperty] private bool _preparationPreviewed;
    [ObservableProperty] private bool _readyToLaunch;
    [ObservableProperty] private bool _sessionActive;
    [ObservableProperty] private bool _useVortexSync;
    [ObservableProperty] private string _vortexSyncStatus = "For VR with Vortex: deploy New Vegas mods in Vortex, then preview and apply the one-way plugin sync. Vortex remains in charge of mod files.";

    public FalloutNewVegasSetupViewModel(FalloutNewVegasDesktopService service)
    {
        _service = service;
        var settings = service.LoadAndDetect();
        _gameDirectory = settings.GameDirectory;
        _storefront = settings.Storefront;
        _modOrganizerExecutablePath = settings.ModOrganizerExecutablePath;
        _modOrganizerInstanceDirectory = settings.ModOrganizerInstanceDirectory;
        _sourceProfileName = settings.SourceProfileName;
        _vrProfileName = settings.VrProfileName;
        _trackerExecutablePath = settings.TrackerExecutablePath;
        _nativeAdapterExecutablePath = settings.NativeAdapterExecutablePath;
        _steamVrDirectory = settings.SteamVrDirectory;
        _useVirtualDesktop = settings.UseVirtualDesktop;
        _virtualDesktopExecutablePath = settings.VirtualDesktopExecutablePath;
        _sessionActive = service.SessionActive;
        _useVortexSync = settings.UseVortexSync;
    }

    public ObservableCollection<FalloutNewVegasCheckViewModel> Checks { get; } = new();
    public IReadOnlyList<FnvStorefront> Storefronts { get; } = Enum.GetValues<FnvStorefront>();
    public bool CanApplyPreparation => PreparationPreviewed && !Busy;
    public bool CanLaunchWithVortex => UseVortexSync && ReadyToLaunch && !Busy && !SessionActive;
    public bool CanStopSession => SessionActive && !Busy;
    public bool CanPrepare => !Busy && !SessionActive;
    public bool CanPreviewVortexSync => !Busy && !SessionActive;
    public bool CanApplyVortexSync => _vortexPlan is not null && !Busy && !SessionActive;

    [RelayCommand]
    private void StartNativeTest()
    {
        if (Busy) return;
        try
        {
            _service.StartNativeTest(CaptureSettings());
            StatusText = "Native VR test opened. Keep Steam running; New Vegas starts directly with its Steam game ID. Wait for the main menu before attaching. No Vortex deployment or extra physical plugin was detected, but manually installed loose files cannot be ruled out. F8 recenters.";
        }
        catch (Exception error) when (error is IOException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            StatusText = error.Message;
        }
    }

    [RelayCommand]
    private void StopNativeTest()
    {
        try { _service.StartNativeTest(CaptureSettings(), stop: true); StatusText = "Native stop launcher opened. Close the game normally to unload the adapter."; }
        catch (Exception error) when (error is IOException or InvalidOperationException or System.ComponentModel.Win32Exception)
        { StatusText = error.Message; }
    }

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (e.PropertyName is nameof(GameDirectory) or nameof(Storefront) or nameof(ModOrganizerExecutablePath)
            or nameof(ModOrganizerInstanceDirectory) or nameof(SourceProfileName) or nameof(VrProfileName)
            or nameof(TrackerExecutablePath) or nameof(NativeAdapterExecutablePath) or nameof(SteamVrDirectory)
            or nameof(UseVirtualDesktop) or nameof(VirtualDesktopExecutablePath))
        {
            ReadyToLaunch = false;
            PreparationPreviewed = false;
            _vortexPlan = null;
            OnPropertyChanged(nameof(CanApplyVortexSync));
        }
    }

    partial void OnBusyChanged(bool value) => NotifyActions();
    partial void OnPreparationPreviewedChanged(bool value) => OnPropertyChanged(nameof(CanApplyPreparation));
    partial void OnReadyToLaunchChanged(bool value) => OnPropertyChanged(nameof(CanLaunchWithVortex));
    partial void OnSessionActiveChanged(bool value)
    {
        OnPropertyChanged(nameof(CanLaunchWithVortex));
        OnPropertyChanged(nameof(CanStopSession));
        OnPropertyChanged(nameof(CanPrepare));
        OnPropertyChanged(nameof(CanPreviewVortexSync));
        OnPropertyChanged(nameof(CanApplyVortexSync));
    }

    partial void OnUseVortexSyncChanged(bool value)
    {
        ReadyToLaunch = false;
        OnPropertyChanged(nameof(CanLaunchWithVortex));
    }

    [RelayCommand]
    private async Task PreviewVortexSyncAsync()
    {
        if (!CanPreviewVortexSync) return;
        Busy = true;
        _vortexPlan = null;
        OnPropertyChanged(nameof(CanApplyVortexSync));
        try
        {
            var plan = await Task.Run(() => _service.PreviewVortexSync(CaptureSettings()));
            _vortexPlan = plan;
            VortexSyncStatus = $"Preview: {plan.VortexPlugins.Count} active deployed Vortex plugin(s); {plan.TargetPlugins.Count} total in VR profile. Apply to update only this profile's plugins.txt and loadorder.txt. A backup is kept in the VR profile.";
            OnPropertyChanged(nameof(CanApplyVortexSync));
        }
        catch (Exception error)
        {
            VortexSyncStatus = $"Cannot preview sync: {error.Message}";
        }
        finally { Busy = false; }
    }

    [RelayCommand]
    private async Task ApplyVortexSyncAsync()
    {
        if (!CanApplyVortexSync || _vortexPlan is null) return;
        Busy = true;
        try
        {
            var settings = CaptureSettings();
            var result = await Task.Run(() => _service.ApplyVortexSync(settings, _vortexPlan));
            UseVortexSync = true;
            VortexSyncStatus = result.Changed
                ? $"Synced {result.ImportedCount} Vortex plugin(s). Re-sync after changing or redeploying Vortex mods."
                : "Vortex plugins are already in sync.";
            _vortexPlan = null;
            var report = await Task.Run(() => _service.Check(settings));
            ShowChecks(report);
        }
        catch (Exception error)
        {
            VortexSyncStatus = $"Sync stopped safely: {error.Message}";
            _vortexPlan = null;
        }
        finally { Busy = false; OnPropertyChanged(nameof(CanApplyVortexSync)); }
    }

    public void InferMo2InstanceFromExecutable()
    {
        if (File.Exists(ModOrganizerExecutablePath) &&
            File.Exists(Path.Combine(Path.GetDirectoryName(ModOrganizerExecutablePath)!, "portable.txt")))
            ModOrganizerInstanceDirectory = Path.GetDirectoryName(ModOrganizerExecutablePath) ?? string.Empty;
    }

    [RelayCommand]
    private async Task CheckReadinessAsync()
    {
        if (Busy) return;
        Busy = true;
        StatusText = "Checking the game and user-owned VR components…";
        try
        {
            var settings = CaptureSettings();
            _service.Save(settings);
            var report = await Task.Run(() => _service.Check(settings));
            ShowChecks(report);
            StatusText = !settings.UseVortexSync
                ? "For VR with Vortex, preview and apply the one-way Vortex sync below before launching."
                : report.Ready
                    ? "Everything detectable is ready. Connect the headset, then launch VR with Vortex."
                    : "Setup is not ready yet. Resolve each NEEDED item, then check again.";
        }
        catch (Exception error)
        {
            ReadyToLaunch = false;
            StatusText = $"Readiness check failed: {error.Message}";
        }
        finally { Busy = false; }
    }

    [RelayCommand]
    private async Task PreviewPreparationAsync()
    {
        if (!CanPrepare) return;
        Busy = true;
        PreparationPreviewed = false;
        StatusText = "Reviewing the isolated-profile changes…";
        try
        {
            var settings = CaptureSettings();
            var plan = await Task.Run(() => _service.PreviewPreparation(settings));
            PreparationPreviewed = true;
            StatusText = $"Review complete: {plan.Operations.Count} safe profile operation(s). " +
                         "Existing VR-profile files will not be overwritten, mods will not be reordered, and saves are not copied. " +
                         "Choose Apply reviewed preparation when ready.";
        }
        catch (Exception error)
        {
            StatusText = $"Could not preview preparation: {error.Message}";
        }
        finally { Busy = false; }
    }

    [RelayCommand]
    private async Task ApplyPreparationAsync()
    {
        if (!CanPrepare) return;
        Busy = true;
        StatusText = "Creating or repairing the isolated New Vegas VR profile…";
        try
        {
            var settings = CaptureSettings();
            var result = await Task.Run(() => _service.ApplyPreparation(settings));
            ModOrganizerInstanceDirectory = settings.ModOrganizerInstanceDirectory;
            VrProfileName = settings.VrProfileName;
            PreparationPreviewed = false;
            var report = await Task.Run(() => _service.Check(settings));
            ShowChecks(report);
            var missing = report.Checks.Count(check => check.Severity == FnvCheckSeverity.Failure);
            StatusText = report.Ready
                ? "Profile prepared and detectable checks passed. Headset verification is still required."
                : $"Profile prepared automatically. Not headset-ready: {missing} required checks still need attention below. Profile preparation does not install the VR components.";
        }
        catch (Exception error)
        {
            StatusText = $"Preparation failed safely: {error.Message}";
        }
        finally { Busy = false; }
    }

    [RelayCommand]
    private async Task LaunchHeadsetTestAsync()
    {
        if (!CanLaunchWithVortex) return;
        Busy = true;
        StatusText = "Starting Virtual Desktop (if selected), SteamVR, FNVR Tracker, New Vegas, and the native OpenXR adapter…";
        try
        {
            var settings = CaptureSettings();
            var report = await Task.Run(() => _service.Launch(settings));
            ShowChecks(report);
            if (!report.Ready)
            {
                StatusText = "Launch was refused because one or more required checks are not ready.";
                return;
            }
            SessionActive = true;
            StatusText = "Headset test started. Verify stereo, tracking, controls, HUD, saves, and clean exit. Return here and end the session afterward.";
        }
        catch (Exception error)
        {
            SessionActive = false;
            StatusText = $"Launch failed: {error.Message}";
        }
        finally { Busy = false; }
    }

    [RelayCommand]
    private void StopSession()
    {
        _service.StopSession();
        SessionActive = false;
        StatusText = "AVRcade stopped the FNVR Tracker and native adapter processes that it started. SteamVR and Virtual Desktop remain under your control.";
    }

    private FalloutNewVegasDesktopSettings CaptureSettings() => new()
    {
        GameDirectory = GameDirectory.Trim(),
        Storefront = Storefront,
        ModOrganizerExecutablePath = ModOrganizerExecutablePath.Trim(),
        ModOrganizerInstanceDirectory = ModOrganizerInstanceDirectory.Trim(),
        SourceProfileName = SourceProfileName.Trim(),
        VrProfileName = VrProfileName.Trim(),
        TrackerExecutablePath = TrackerExecutablePath.Trim(),
        NativeAdapterExecutablePath = NativeAdapterExecutablePath.Trim(),
        SteamVrDirectory = SteamVrDirectory.Trim(),
        UseVirtualDesktop = UseVirtualDesktop,
        VirtualDesktopExecutablePath = VirtualDesktopExecutablePath.Trim(),
        UseVortexSync = UseVortexSync
    };

    private void ShowChecks(FnvReadinessReport report)
    {
        Checks.Clear();
        foreach (var check in report.Checks)
        {
            Checks.Add(new FalloutNewVegasCheckViewModel(
                check.Severity.ToString().ToLowerInvariant(),
                FriendlyComponent(check.Component), check.Message));
        }
        ReadyToLaunch = report.Ready;
    }

    private void NotifyActions()
    {
        OnPropertyChanged(nameof(CanPrepare));
        OnPropertyChanged(nameof(CanApplyPreparation));
        OnPropertyChanged(nameof(CanLaunchWithVortex));
        OnPropertyChanged(nameof(CanStopSession));
        OnPropertyChanged(nameof(CanPreviewVortexSync));
        OnPropertyChanged(nameof(CanApplyVortexSync));
    }

    private static string FriendlyComponent(string component) => component.Replace('_', ' ') switch
    {
        var value when value.Length == 0 => "Setup",
        var value => char.ToUpperInvariant(value[0]) + value[1..]
    };
}
