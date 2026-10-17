using System.Diagnostics;
using System.Text.Json;
using VrClient.Core.Launch;
using VrClient.Core.ModManagers;

namespace VrClient.Core.App;

public sealed partial class AppController
{
    private void AddYapyapCatalogueEntry(List<GameView> views)
    {
        var path = Path.Combine(_repoRoot, "config", "catalogue", "yapyap.json");
        if (!File.Exists(path)) return;
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var config = document.RootElement;
        if (config.GetProperty("schema").GetString() != "flat-catalogue-game/1" ||
            config.GetProperty("game_id").GetString() != "yapyap") return;
        var install = _locator("3834090", "yapyap.exe");
        var vr = YapyapVrInstall.Inspect(install);
        views.Add(new GameView("yapyap", "YAPYAP", "3834090", install, vr.Installed,
            "Warn", "yapyap_experimental_vr", GameEngine.Unity,
            install is null ? GameReadiness.NeedsGame : vr.Installed
                ? GameReadiness.ProfileUnverified : GameReadiness.NeedsVrMod,
            vr.Detail));
    }

    public async Task<ActionOutcome> LaunchYapyapVrAsync(PlayMode mode, bool dryRun = false,
        string? steamRoot = null, string? managerExecutable = null, CancellationToken ct = default)
    {
        if (mode is not (PlayMode.VrOnly or PlayMode.VrWithMods))
            return new(false, "Choose a YAPYAP VR mode.");
        if (!dryRun && Process.GetProcessesByName("yapyap").Length != 0)
            return new(false, "YAPYAP is already running. Close it before switching launch modes.");
        var installDir = _locator("3834090", "yapyap.exe");
        var vr = YapyapVrInstall.Inspect(installDir);
        if (!vr.Installed)
            return new(false, vr.Detail);
        if (mode == PlayMode.VrOnly && vr.OtherPluginFiles != 0)
            return new(false, "Other plugins are present. Use VR with mods after checking their compatibility.");
        // Validate the requested handoff before starting runtime processes.
        if (mode == PlayMode.VrWithMods)
        {
            if (managerExecutable is not null && GameModManagerSettings.Validate(managerExecutable) is { } error)
                return new(false, error);
            if (managerExecutable is null && vr.OtherPluginFiles == 0)
                return new(false, "No extra plugin is installed; choose a mod manager for this mode.");
        }
        if (ct.IsCancellationRequested)
            return new(false, "YAPYAP launch was cancelled.");
        // Profile selection needs the desktop; do not take the headset into Home first.
        if (mode == PlayMode.VrWithMods && managerExecutable is not null)
            return OpenYapyapManager(managerExecutable, dryRun,
                "Choose a YAPYAP profile containing UUVR and the AVRcade bridge, then Start Modded. " +
                "SteamVR is required when the game enters VR. An extra game mod is optional.");
        // Without the server the game can open on the monitor while VR appears to have launched.
        if (!dryRun || !SteamVrStartup.IsServerRunning())
        {
            var runtime = await new SteamVrStartup().EnsureRunningAsync(steamRoot, dryRun, ct).ConfigureAwait(false);
            if (!runtime.Success) return new(false, runtime.Detail);
        }
        if (ct.IsCancellationRequested)
            return new(false, "YAPYAP launch was cancelled.");
        var launch = new SteamOwnedLauncher().Launch("3834090", steamRoot, dryRun);
        return new(launch.Success, launch.Success
            ? dryRun ? "YAPYAP VR Steam launch plan: 3834090"
                : mode == PlayMode.VrWithMods
                    ? "Steam is starting YAPYAP with the VR package and game-folder mod. Confirm both load in the headset."
                    : "Steam is starting YAPYAP with the VR package. Switch to the SteamVR headset view."
            : launch.FailureText);
    }

    public ActionOutcome LaunchYapyapDesktop(PlayMode mode, bool dryRun = false,
        string? steamRoot = null, string? managerExecutable = null)
    {
        if (mode is not (PlayMode.Vanilla or PlayMode.ModsNoVr))
            return new(false, "Choose a YAPYAP desktop mode.");
        if (_locator("3834090", "yapyap.exe") is null)
            return new(false, "YAPYAP is not installed.");
        if (!dryRun && Process.GetProcessesByName("yapyap").Length != 0)
            return new(false, "YAPYAP is already running. Close it before switching launch modes.");
        if (mode == PlayMode.ModsNoVr)
            return OpenYapyapManager(managerExecutable, dryRun,
                "Select a YAPYAP desktop mod profile without UUVR or the AVRcade VR bridge, then Start Modded.");

        string[] args = ["--doorstop-enabled", "false", "--doorstop-enable", "false", "--disable-vr"];
        var launch = new SteamOwnedLauncher().Launch("3834090", steamRoot, dryRun,
            launchArguments: args);
        return new(launch.Success, launch.Success
            ? dryRun ? $"YAPYAP vanilla launch plan: {launch.Detail}"
                : "Steam is starting YAPYAP with Doorstop and BepInEx disabled for this launch."
            : launch.FailureText);
    }

    private static ActionOutcome OpenYapyapManager(string? managerExecutable, bool dryRun,
        string nextStep)
    {
        if (GameModManagerSettings.Validate(managerExecutable) is { } managerError)
            return new(false, managerError);
        if (dryRun)
            return new(true, $"YAPYAP mod-manager handoff plan: {managerExecutable}. {nextStep}");
        try
        {
            var start = new ProcessStartInfo(managerExecutable!)
            {
                UseShellExecute = true,
                WorkingDirectory = Path.GetDirectoryName(managerExecutable)!
            };
            return Process.Start(start) is null
                ? new(false, "The mod manager did not start.")
                : new(true, $"Mod manager opened. {nextStep}");
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or IOException or
                                   UnauthorizedAccessException or ArgumentException)
        {
            return new(false, $"Could not open the mod manager: {ex.Message}");
        }
    }

}
