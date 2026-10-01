from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    cli = (ROOT / "client/VrClient.Cli/Program.cs").read_text(encoding="utf-8")
    controller = (ROOT / "client/VrClient.Core/App/AppController.cs").read_text(encoding="utf-8")
    planner = (ROOT / "client/VrClient.Core/App/PlayModes.cs").read_text(encoding="utf-8")
    main_vm = (ROOT / "client/VrClient.App/ViewModels/MainWindowViewModel.cs").read_text(encoding="utf-8")
    view = (ROOT / "client/VrClient.App/Views/MainWindow.axaml").read_text(encoding="utf-8")

    require('GetOption("--mode")' in cli, "Cyberpunk CLI must accept an explicit launch mode")
    require("CyberpunkLaunchMode.Flat" in cli and "CyberpunkLaunchMode.Vr" in cli, "CLI must expose flat and VR modes")
    require("BuildEnvironment(launchMode)" in cli, "CLI must pass a per-process activation environment")
    require("WriteDiagnostics(" in cli, "CLI must persist local launch diagnostics")
    require('ArgumentList.Add("--mode")' in controller, "app controller must call the explicit CLI route")
    require('ArgumentList.Add(mode is CyberpunkLaunchMode.Vr ? "vr" : "flat")' in controller, "app controller must preserve the chosen mode")

    # Modded launches: any manager may have deployed the mods, so REDmod's -modded
    # must not depend on Vortex being installed.
    require('HasFlag("--with-mods")' in cli and "session.BuildLaunchArguments(withMods)" in cli,
            "Cyberpunk CLI must give modded launches the REDmod argument")
    require('HasFlag("--with-vortex")' in cli and "ModManagerKind.Vortex" in cli and "Vortex was not found" in cli,
            "the Vortex-specific launch must still refuse when Vortex cannot be found")
    require('ArgumentList.Add("--with-mods")' in controller and 'ArgumentList.Add("--with-vortex")' in controller,
            "app controller must preserve the modded launch choice")

    # All four play modes reach a launch route.
    require("IReadOnlyList<PlayModeState> Cyberpunk(CyberpunkPlayFacts f)" in planner,
            "Cyberpunk must have rules for the four play modes")
    require("LaunchRedengineAsync(CyberpunkLaunchMode.Vr, withMods: withMods)" in main_vm and
            "LaunchRedengineAsync(CyberpunkLaunchMode.Flat, withMods: withMods)" in main_vm,
            "VR and monitor launches must each carry the modded choice")
    require("LaunchCyberpunkFlatViaSteam(withMods)" in main_vm and
            "public ActionOutcome LaunchCyberpunkFlatViaSteam(" in controller,
            "monitor play must stay available through Steam when the VR backend is not installed")
    require('Click="OpenCyberpunkVortex_Click"' in view and "SelectGameModManager_Click" in view,
            "Cyberpunk mods must be manageable in Vortex or any manager the player chooses")
    require("SetCyberpunkTurnModeCommand" in view and "SetHudModeCommand" in view,
            "Cyberpunk turning and HUD settings must remain reachable")

    print("Cyberpunk explicit launch UI checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
