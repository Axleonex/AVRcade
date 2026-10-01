"""Static UI contract for San Andreas launch controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
VIEW = ROOT / "client" / "VrClient.App" / "Views" / "MainWindow.axaml"
CODE_BEHIND = ROOT / "client" / "VrClient.App" / "Views" / "MainWindow.axaml.cs"
MAIN_VM = ROOT / "client" / "VrClient.App" / "ViewModels" / "MainWindowViewModel.cs"
CONTROLLER = ROOT / "client" / "VrClient.Core" / "App" / "AppController.cs"
PLANNER = ROOT / "client" / "VrClient.Core" / "App" / "PlayModes.cs"


def main() -> None:
    view = VIEW.read_text(encoding="utf-8")
    code = CODE_BEHIND.read_text(encoding="utf-8")
    main_vm = MAIN_VM.read_text(encoding="utf-8")
    planner = PLANNER.read_text(encoding="utf-8")
    controller = CONTROLLER.read_text(encoding="utf-8")
    launch = main_vm[main_vm.index("private ActionOutcome LaunchGtaSanAndreas("):]

    # The four play modes map onto the four San Andreas launch routes.
    assert "IReadOnlyList<PlayModeState> GtaSanAndreas(GtaPlayFacts f)" in planner
    assert "GtaSanAndreasVrMods.Clean" in launch and "GtaSanAndreasVrMods.Existing" in launch
    assert "LaunchGtaSanAndreasDesktop(dir)" in launch
    assert "LaunchGtaSanAndreasVanilla(dir)" in launch
    # gta_sa.exe is 32-bit, so its VR launch takes the 32-bit runtime selection, in Core.
    assert "LaunchGtaSanAndreasVr(dir, " in launch
    vr_launch = controller[controller.index("public ActionOutcome LaunchGtaSanAndreasVr("):]
    assert "SelectLiveFor32BitProcess()" in vr_launch[:400]
    assert "PlayModeSetup.ChooseCleanFolder" in planner and "ChooseGtaVanillaFolderAsync" in code

    # One manager chooser: any installed manager .exe, no manager-specific buttons.
    san_andreas = view[view.index("San Andreas mods are installed into the game folder itself"):]
    assert san_andreas.count('Content="Choose your manager"') == 1
    assert 'Content="Select installed GGMM"' not in view
    assert 'Content="Select installed SAMI"' not in view
    assert "FileTypeFilter" in code and '["*.exe"]' in code
    assert 'File.Exists(Path.Combine(gameDir, "ggmm.exe"))' not in code
    assert "SetGtaTurnModeCommand" in view and "SetGtaSnapTurnAngleCommand" in view
    print("gtasa_launch_controls_contract_tests: passed")


if __name__ == "__main__":
    main()
