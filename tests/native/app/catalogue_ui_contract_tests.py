from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
AVALONIA = "{https://github.com/avaloniaui}"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    view_path = ROOT / "client/VrClient.App/Views/MainWindow.axaml"
    warning_path = ROOT / "client/VrClient.App/Views/ConfirmSafetyWarningDialog.axaml"
    main_vm_path = ROOT / "client/VrClient.App/ViewModels/MainWindowViewModel.cs"
    game_vm_path = ROOT / "client/VrClient.App/ViewModels/GameItemViewModel.cs"
    mode_vm_path = ROOT / "client/VrClient.App/ViewModels/PlayModeViewModel.cs"
    planner_path = ROOT / "client/VrClient.Core/App/PlayModes.cs"
    code_behind_path = ROOT / "client/VrClient.App/Views/MainWindow.axaml.cs"
    manager_dialog_path = ROOT / "client/VrClient.App/Views/FriendslopModManagerDialog.axaml"
    fallout_manager_dialog_path = ROOT / "client/VrClient.App/Views/Fallout3VortexDialog.axaml"
    embedded_host_path = ROOT / "client/VrClient.App/Views/VortexWindowHost.cs"

    # Parsing catches malformed markup before Avalonia's compiled-XAML build.
    parsed_view = ET.parse(view_path)
    ET.parse(warning_path)
    ET.parse(manager_dialog_path)
    ET.parse(fallout_manager_dialog_path)

    view = view_path.read_text(encoding="utf-8")
    warning = warning_path.read_text(encoding="utf-8")
    main_vm = main_vm_path.read_text(encoding="utf-8")
    game_vm = game_vm_path.read_text(encoding="utf-8")
    mode_vm = mode_vm_path.read_text(encoding="utf-8")
    planner = planner_path.read_text(encoding="utf-8")
    code_behind = code_behind_path.read_text(encoding="utf-8")
    manager_dialog = manager_dialog_path.read_text(encoding="utf-8")
    fallout_manager_dialog = fallout_manager_dialog_path.read_text(encoding="utf-8")
    embedded_host = embedded_host_path.read_text(encoding="utf-8")
    tabs = [tab.get("Header") for tab in parsed_view.findall(f".//{AVALONIA}TabItem")]

    # Library, game page and settings.
    require('ItemsSource="{Binding FilteredGames}"' in view, "library must render the filtered game catalogue")
    require('Classes="game-card"' in view and "OpenGameCommand" in view, "each catalogue card must open its game page")
    require("ShowLibrary" in view and "ShowGameDetail" in view and "ShowSettings" in view,
            "library, game page, and settings surfaces must be explicit")
    require("BackToLibraryCommand" in view and "SearchText" in view, "catalogue navigation and search must be reachable")
    require("SetLibraryFilterCommand" in view and "LibraryStatusText" in view,
            "the library must filter by installed/VR-ready and show one status per card")
    require("ShowHeadsetHint" in view, "a missing headset connection must be explained on the library")

    # The four ways to play: one shared block, on every game.
    require(re.search(r"enum PlayMode \{ VrOnly, VrWithMods, ModsNoVr, Vanilla \}", planner) is not None,
            "the four play modes must be the single shared definition")
    require(all(f"IReadOnlyList<PlayModeState> {engine}(" in planner
                for engine in ("Unity", "Unreal", "Cyberpunk", "GtaSanAndreas")),
            "every catalogue engine must have play-mode rules")
    require(all(f"PlayModePlanner.{engine}(" in main_vm
                for engine in ("Unity", "Unreal", "Cyberpunk", "GtaSanAndreas")),
            "the window must ask the shared rules for every catalogue engine")
    require(view.count('ItemsSource="{Binding PlayModes}"') == 1 and
            view.count('Click="PlayMode_Click"') == 1,
            "all games must share one four-mode block and one launch handler")
    require(all(title in mode_vm for title in ('"VR only"', '"VR + mods"', '"Mods, no VR"', '"Vanilla"')),
            "the four modes must keep their plain names")
    require('Content="{Binding ActionText}"' in view and 'Text="{Binding Status}"' in view and
            'IsEnabled="{Binding CanAct}"' in view,
            "each mode must show its state and one action, never an unexplained disabled button")
    require(tabs[:4] == ["VR setup", "Mods", "Controls", "About"],
            "setup details must sit in the four topic tabs below the play modes")
    require(view.index("CHOOSE HOW TO PLAY") < view.index('TabItem Header="VR setup"'),
            "the play choices must come before any setup detail")

    # Launch safety: only the confirmed dialog path may acknowledge a modded launch.
    require("PlayMode_Click" in code_behind and "ConfirmSafetyWarningDialog" in code_behind and
            "RunPlayModeAsync(mode.Mode, acknowledge)" in code_behind,
            "launches must pass through the warning acknowledgement gate")
    require("RunPlayModeAsync(PlayMode mode, bool acknowledge)" in main_vm,
            "acknowledgement must be an explicit launch input")
    require("acknowledge: true" not in main_vm,
            "the view model must never hard-code warning acknowledgement")
    require("RequiresLaunchAcknowledgement" in game_vm,
            "game presentation must say when a launch needs acknowledgement")
    require("Acknowledge and launch" in warning and "supported private or offline scenario" in warning,
            "warning dialog must state the commitment and supported scope")
    require("RefreshCatalogue(announce: false)" in main_vm,
            "background refresh must not replace operation outcomes")
    require("LaunchUnityVr(" in main_vm and "SelectFor32BitProcess" not in
            main_vm[main_vm.index("private ActionOutcome LaunchUnity("):main_vm.index("private async Task<ActionOutcome> LaunchCyberpunkAsync(")],
            "64-bit Unity games must use the 64-bit runtime selection with the mod's own runtime pin")

    # Mods: profiles belong to the manager; AVRcade remembers one per modded mode.
    require(view.count('SelectedItem="{Binding SelectedProfile, Mode=TwoWay}"') == 1 and
            view.count('SelectedItem="{Binding SelectedFlatProfile, Mode=TwoWay}"') == 1,
            "VR + mods and Mods, no VR must each keep their own manager profile")
    require("VrModIdentityText" in view and "VrWithModsManagerInstructions" in view and
            "FlatModeInstructions" in view and "OriginalPageUrl" in view,
            "mod setup must name the VR package, give manager-specific steps, and link its source page")
    require("LocateFriendslopGame_Click" in view and "LocateModManager_Click" in view and
            "Open in Steam" in view and 'Content="Get r2modman"' in view and 'Content="Get Vortex"' in view,
            "first-run setup must offer official downloads and validated manual locations")
    require("OpenFriendslopManager_Click" in view and "FriendslopModManagerDialog" in code_behind and
            "Reread profiles" in view,
            "the manager must open inside AVRcade and its profiles must be re-readable")
    require("SelectGameModManager_Click" in view and "SelectGtaModManager_Click" in view and
            "OpenGameModManagerCommand" in view,
            "games with game-folder mods must let the player choose any mod manager")
    require("OptionalModSearch" not in view and "SearchOptionalModsCommand" not in view and
            "AddOptionalModCommand" not in view,
            "mod discovery and optional downloads must remain in the manager's own pages")
    require("ManagerHost" in manager_dialog and "Detect again" in manager_dialog,
            "manager setup must include its embedded window host")
    require("Open VR mod page" in manager_dialog,
            "embedded VR setup must keep the named mod's source page reachable")
    require(all('Content="Focus manager typing" Click="Focus_Click"' in dialog
                for dialog in (manager_dialog, fallout_manager_dialog)) and
            "AttachThreadInput" in embedded_host and "SetFocus" in embedded_host,
            "both embedded manager panels must expose cross-thread keyboard focus recovery")

    # Existing behaviour that must stay reachable.
    require("ControllerReferenceTitle" in view and "VR HUD" in view and "ThemePalettes" in view,
            "controller, HUD, and appearance behavior must remain reachable")
    require("ReleaseToolingText" in view and "RuntimeText" in view,
            "release and runtime diagnostics must remain reachable")
    require("PrepareCommand" in view and "Uninstall_Click" in view and "ImportUevrProfile_Click" in view,
            "VR setup, removal, and profile import must remain reachable")
    require("RefreshCatalogue" not in main_vm[main_vm.index("private void ToggleTheme()"):main_vm.index("private void SetLibraryFilter(")],
            "a theme or palette change must recolour in place, not rescan the library")
    require("FilteredGames" in main_vm and "ApplyFilter" in main_vm and "OpenGame" in main_vm,
            "view model must own catalogue filtering and navigation")

    # Artwork stays local.
    require("CoverBrush" in game_vm and "CoverMark" in game_vm,
            "catalogue cards must have deterministic local artwork fallbacks")
    require('Source="{Binding CoverImage}"' in view and "HasCoverImage" in view,
            "catalogue and game-page posters must render optional local cover images")
    require('IsVisible="{Binding !HasCoverImage}"' in view,
            "typographic covers must remain available when local artwork is missing")
    require("SteamLibraryArtworkLocator.FindPortrait" in main_vm and "_covers" in main_vm,
            "covers must come from the bounded Steam cache locator and be decoded once, not per refresh")
    require("http://" not in game_vm.lower() and "https://" not in game_vm.lower(),
            "cover artwork must not introduce remote image requests")

    print("AVRcade catalogue UI contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
