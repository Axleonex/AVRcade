using VrClient.Core.App;
using VrClient.Core.Compatibility;

namespace VrClient.App.ViewModels;

public sealed record InstallationChoice(string Label, string? Root);

public sealed partial class GameItemViewModel
{
    private GameCompatibilityService? _compatibility;
    private Action<InstallationChoice>? _selectInstallation;
    private InstallationChoice? _installation;
    private bool _automaticUpdates;
    public GameCompatibilityState? CompatibilityState { get; private set; }
    public GameVersionRecovery? VersionRecovery { get; private set; }
    public bool CanRecoverGameVersion => VersionRecovery is not null;
    public IReadOnlyList<InstallationChoice> InstallationChoices { get; private set; } = [];
    public string CompatibilityText => CompatibilityState?.Message ?? "Compatibility checks are unavailable.";
    public string CompatibilityQuickText => VersionRecovery is { } version
        ? $"Installed: {version.InstalledBuild}. VR needs: {version.RequiredBuild}."
        : CompatibilityState is { CanTryVr: true } ? "Build check complete. Headset testing is still separate."
        : "A compatibility check needs attention. Open Troubleshooting.";
    public string SaveCompatibilityWarning => GameCompatibilityService.SaveWarning;
    public bool CanUseCompatibility => IsInstalled;
    public bool HasParkedMods => CompatibilityState?.ModsParked == true;
    public bool HasPackageBackup => View.InstallDir is { } root && PackageRecovery.Exists(root);
    public string BuildIdentityText => CompatibilityState?.Identity is { } identity
        ? $"Game: {identity.Version ?? "version unavailable"} · Steam build: {identity.SteamBuild ?? "separate/local copy"}"
        : "No readable game build in the selected installation.";
    public bool AutomaticCompatibilityUpdates
    {
        get => _automaticUpdates;
        set
        {
            if (_automaticUpdates == value) return;
            _compatibility?.SetAutomaticUpdates(View, value);
            _automaticUpdates = value;
            OnPropertyChanged();
        }
    }
    public InstallationChoice? SelectedInstallation
    {
        get => _installation;
        set
        {
            if (value is null || value == _installation) return;
            _selectInstallation?.Invoke(value);
        }
    }
    public void SetCompatibility(GameCompatibilityService compatibility, Action<InstallationChoice> select)
    {
        _compatibility = compatibility;
        _selectInstallation = select;
        CompatibilityState = compatibility.Inspect(View);
        VersionRecovery = compatibility.VersionRecoveryFor(View, CompatibilityState);
        GameInstallations saved;
        try { saved = compatibility.Installations.ForGame(Slug); }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException or UnauthorizedAccessException)
        { saved = new(null, []); }
        InstallationChoices = new[] { new InstallationChoice("Store / discovered installation", null) }
            .Concat(saved.Profiles.Select(p => new InstallationChoice(p.Label + " — " + p.Root, p.Root))).ToArray();
        _installation = InstallationChoices.FirstOrDefault(c => c.Root is not null &&
            GameInstallationStore.SameRoot(c.Root, saved.SelectedRoot)) ?? InstallationChoices[0];
        _automaticUpdates = CompatibilityState.Profile?.AutomaticUpdates ?? true;
        foreach (var name in new[] { nameof(CompatibilityState), nameof(CompatibilityText), nameof(BuildIdentityText),
            nameof(InstallationChoices), nameof(SelectedInstallation), nameof(AutomaticCompatibilityUpdates),
            nameof(HasParkedMods), nameof(HasPackageBackup), nameof(CanUseCompatibility),
            nameof(VersionRecovery), nameof(CanRecoverGameVersion), nameof(CompatibilityQuickText), nameof(SetupQuickStatus) }) OnPropertyChanged(name);
    }
}
