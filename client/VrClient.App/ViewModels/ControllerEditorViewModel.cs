using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using VrClient.Core.Config;

namespace VrClient.App.ViewModels;

public sealed record ControllerActionChoice(string Slot, string Display);

public partial class ControllerEditorViewModel : ObservableObject
{
    private readonly ControllerCustomization _store;
    private readonly string _game;
    private Dictionary<string, string> _map;
    public ControllerDeviceReference Defaults { get; }
    public string DisplayName => Defaults.DisplayName;
    [ObservableProperty] private ControllerDeviceReference _diagram;
    [ObservableProperty] private string _selectedSlot = "left_primary";
    [ObservableProperty] private ControllerActionChoice? _selectedAction;
    [ObservableProperty] private string _status = "Saved automatically. Changes take effect on the next VR launch.";
    public IReadOnlyList<ControllerActionChoice> Choices => ControllerCustomization.Slots
        .Where(s => ControllerCustomization.Group(s) == ControllerCustomization.Group(SelectedSlot))
        .Select(s => new ControllerActionChoice(s, ControllerCustomization.DefaultBinding(Defaults, s).Action +
            " (default: " + ControllerCustomization.Label(Defaults.Id, s) + ")")).ToArray();
    public string ResetButtonText => "Reset to my defaults";
    public bool HasSelectedDefaults => _store.HasSelectedDefaults(_game, Defaults.Id);
    public string SelectedControl => ControllerCustomization.Label(Defaults.Id, SelectedSlot);
    private bool _updating;

    public ControllerEditorViewModel(string game, ControllerDeviceReference defaults, ControllerCustomization? store = null)
    {
        _game = game; Defaults = ControllerCustomization.ForEditing(game, defaults); _store = store ?? new();
        try { _map = _store.Load(game, defaults.Id); }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException or UnauthorizedAccessException)
        { _map = ControllerCustomization.Identity(); Status = "Saved settings could not be read: " + ex.Message + " Reset to restore defaults."; }
        _diagram = ControllerCustomization.Apply(Defaults, _map);
        Select("left_primary");
    }
    public void Select(string slot)
    {
        if (!ControllerCustomization.Slots.Contains(slot)) return;
        _updating = true;
        SelectedSlot = slot;
        OnPropertyChanged(nameof(SelectedControl)); OnPropertyChanged(nameof(Choices));
        SelectedAction = Choices.Single(c => c.Slot == _map[slot]);
        _updating = false;
    }
    partial void OnSelectedActionChanged(ControllerActionChoice? value)
    {
        if (_updating || value == null) return;
        try
        {
            var map = ControllerCustomization.Swap(_map, SelectedSlot, value.Slot);
            _store.Save(_game, Defaults.Id, map);
            _map = map; Diagram = ControllerCustomization.Apply(Defaults, map);
            Status = "Saved for this game and controller. The previous assignment swapped places. Applies on the next VR launch.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        { Status = "Could not save assignments: " + ex.Message; Select(SelectedSlot); }
    }
    [RelayCommand]
    private void ResetDefaults()
    {
        try
        {
            _store.Reset(_game, Defaults.Id); _map = _store.Load(_game, Defaults.Id);
            Diagram = ControllerCustomization.Apply(Defaults, _map); Select(SelectedSlot);
            Status = "Selected defaults restored for this game and controller. Applies on the next VR launch.";
        }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException or UnauthorizedAccessException)
        { Status = "Could not restore defaults: " + ex.Message; }
    }
    [RelayCommand]
    private void SaveAsDefaults()
    {
        try
        {
            _store.SaveAsDefaults(_game, Defaults.Id, _map);
            OnPropertyChanged(nameof(ResetButtonText));
            OnPropertyChanged(nameof(HasSelectedDefaults));
            Status = "Current assignments saved as your defaults for this game and controller.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        { Status = "Could not save defaults: " + ex.Message; }
    }
    [RelayCommand]
    private void RestoreOriginal()
    {
        try
        {
            _store.RestoreOriginal(_game, Defaults.Id);
            _map = ControllerCustomization.Identity();
            Diagram = ControllerCustomization.Apply(Defaults, _map); Select(SelectedSlot);
            Status = "Original bindings restored. Your saved defaults are kept. Applies on the next VR launch.";
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        { Status = "Could not restore original bindings: " + ex.Message; }
    }

}
