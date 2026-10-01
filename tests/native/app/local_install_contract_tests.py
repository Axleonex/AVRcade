from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    script_path = ROOT / "scripts/install-local-vrclient.ps1"
    script = script_path.read_text(encoding="utf-8")

    require("Programs\\VRClient" in script and "vrclient-app.exe" in script,
            "publisher must define one canonical per-user executable")
    require("VRClient.staging" in script and "VRClient.rollback" in script,
            "publisher must use explicit staging and rollback directories")
    require("Assert-CanonicalInstallPath" in script,
            "every install directory mutation must pass an exact-path guard")
    require(script.index("Test-AppWindow $LocalStagedExe") < script.index("$hadPreviousInstall"),
            "staged app smoke must occur before the installed copy is replaced")
    require(script.count("assert-gtasa-bridge-sync.ps1") >= 2,
            "local install must verify source and staged GTA bridge profiles")
    require("Test-AppWindow $InstalledExe" in script and "catch {" in script,
            "installed app smoke must have a rollback path")
    require("--self-contained" in script and "PublishSingleFile=true" in script,
            "canonical app must not depend on a developer runtime")
    require("vrclient_safety_cli.exe" in script and "vrclient_sign_cli.exe" in script,
            "canonical install must retain the native safety and signature helpers")
    require("Set-Shortcut $DesktopShortcut $InstalledExe" in script,
            "Desktop shortcut must target the canonical executable")
    require("Set-Shortcut $StartMenuShortcut $InstalledExe" in script,
            "Start-menu shortcut must target the same canonical executable")
    require(script.count("Assert-ShortcutTarget") >= 3,
            "both shortcuts must be resolved and verified after creation")
    require("Remove-Item -LiteralPath $StatusHelper" in script,
            "obsolete Desktop status helper must be removed after convergence")
    require("Remove-Item -LiteralPath $ProgramsRoot" not in script,
            "publisher must never delete the Programs directory")
    require("Remove-Item -LiteralPath $env:LOCALAPPDATA" not in script,
            "publisher must never delete a broad LocalAppData target")

    print("VRClient canonical local-install contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
