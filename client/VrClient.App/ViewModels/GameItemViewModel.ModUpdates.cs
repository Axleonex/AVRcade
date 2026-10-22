using VrClient.Core.Modpack;

namespace VrClient.App.ViewModels;

public sealed partial class GameItemViewModel
{
    private bool _modUpdateBusy;
    private string? _modUpdateMessage;
    private bool _modUpdateFailed;
    public bool HasPublicModUpdate => HasModCredits;
    public string StreamButtonLabel => HasPublicModUpdate ? "OBS Stream" : "Stream";
    public bool CanUpdateMods => HasPublicModUpdate && IsInstalled && !_modUpdateBusy;
    public bool ModUpdateBusy => _modUpdateBusy;
    public bool ModUpdateFailed => _modUpdateFailed;
    public string ModUpdateButtonText => _modUpdateBusy ? "Updating mods..." : "Update mods";
    public bool HasModUpdateMessage => !string.IsNullOrWhiteSpace(_modUpdateMessage);
    public string ModUpdateMessage => _modUpdateMessage ?? string.Empty;
    public string ModUpdateButtonHint => !IsInstalled ? "Install or locate the game first."
        : SetupBlocked ? "Check for compatible VR releases. Incompatible packages will not be installed."
        : "Check upstream releases and update AVRcade's game-folder VR components. Mod-manager profiles keep their own versions.";

    public void SetModUpdateProgress(string message, bool busy, bool failed = false)
    {
        _modUpdateMessage = message;
        _modUpdateBusy = busy;
        _modUpdateFailed = failed;
        foreach (var property in new[] { nameof(CanUpdateMods), nameof(ModUpdateBusy), nameof(ModUpdateButtonText),
            nameof(HasModUpdateMessage), nameof(ModUpdateMessage), nameof(ModUpdateFailed) }) OnPropertyChanged(property);
        NotifySetupChanged();
    }
}
