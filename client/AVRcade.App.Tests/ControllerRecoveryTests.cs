using VrClient.App.ViewModels;
using VrClient.Core.Config;
using Xunit;

namespace AVRcade.App.Tests;

public sealed class ControllerRecoveryTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-editor-" + Guid.NewGuid().ToString("N"));
    private readonly ControllerCustomization _store;
    private static ControllerDeviceReference Device => new("quest-touch", "Quest Touch",
        ControllerCustomization.Slots.Select(slot => new ControllerBindingReference(slot, slot, slot)).ToArray());
    public ControllerRecoveryTests() { Directory.CreateDirectory(_root); _store = new(_root); }
    public void Dispose() => Directory.Delete(_root, true);

    [Fact]
    public void InvalidAssignmentsLeaveTheEditorUsableAndResetRestoresSelectedDefaults()
    {
        var defaults = ControllerCustomization.Swap(ControllerCustomization.Identity(), "left_primary", "left_secondary");
        _store.SaveAsDefaults("repo", "quest-touch", defaults);
        var broken = _store.PathFor("repo", "quest-touch");
        File.WriteAllText(broken, "{\"left_primary\":\"left_primary\"}");
        var editor = new ControllerEditorViewModel("repo", Device, _store);
        Assert.Contains("Reset", editor.Status);
        Assert.Equal("{\"left_primary\":\"left_primary\"}", File.ReadAllText(broken));
        editor.ResetDefaultsCommand.Execute(null);
        Assert.Equal(defaults.OrderBy(pair => pair.Key), _store.Load("repo", "quest-touch").OrderBy(pair => pair.Key));
        Assert.Contains("restored", editor.Status);
    }

    [Fact]
    public void InvalidSavedDefaultsOfferOriginalBindingsWithoutChangingAnotherGame()
    {
        _store.SaveAsDefaults("repo", "quest-touch", ControllerCustomization.Identity());
        File.WriteAllText(Path.Combine(_root, "repo", "defaults", "quest-touch.json"), "{}");
        _store.Save("peak", "quest-touch", ControllerCustomization.Identity());
        var other = File.ReadAllBytes(_store.PathFor("peak", "quest-touch"));
        var editor = new ControllerEditorViewModel("repo", Device, _store);
        editor.ResetDefaultsCommand.Execute(null);
        Assert.Contains("Could not restore", editor.Status);
        editor.RestoreOriginalCommand.Execute(null);
        Assert.Equal(ControllerCustomization.Identity().OrderBy(pair => pair.Key), _store.Load("repo", "quest-touch").OrderBy(pair => pair.Key));
        Assert.Equal(other, File.ReadAllBytes(_store.PathFor("peak", "quest-touch")));
    }
}
