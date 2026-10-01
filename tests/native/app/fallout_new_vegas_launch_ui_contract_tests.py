from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[3]
VIEW = ROOT / "client/VrClient.App/Views/MainWindow.axaml"
MAIN_VM = ROOT / "client/VrClient.App/ViewModels/MainWindowViewModel.cs"
GAME_VM = ROOT / "client/VrClient.App/ViewModels/GameItemViewModel.cs"


def main() -> int:
    ET.parse(VIEW)
    view = VIEW.read_text(encoding="utf-8")
    main_vm = MAIN_VM.read_text(encoding="utf-8")
    game_vm = GAME_VM.read_text(encoding="utf-8")
    assert 'Text="Fallout: New Vegas launch options"' in view
    assert 'Content="Native VR — experimental"' in view
    assert 'Content="VR with Vortex mods — experimental"' in view
    assert "LaunchFalloutNewVegasNativeCommand" in view
    assert "LaunchFalloutNewVegasVortexCommand" in view
    assert "LaunchFalloutNewVegasNative()" in main_vm
    assert "LaunchFalloutNewVegasVortexAsync()" in main_vm
    assert "CanLaunchFalloutNewVegas" in game_vm
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
