"""Current release contract; the 0.1.2 patch/installed-DLL tests are historical."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    package = json.loads((ROOT / "config/redengine/native/cyberpunk-2077-backend.json").read_text())
    backend = package["backend"]
    assert backend["version"] == "0.1.7"
    assert backend["creator"] == "dariulone"
    assert backend["project_url"] == "https://www.nexusmods.com/cyberpunk2077/mods/30093"
    files = {f["path"].replace("\\", "/"): f["sha256"] for f in backend["files"]}
    assert len(files) == len(backend["files"]) == 257
    assert not any("CyberpunkVR_Hands.dll" in name for name in files)
    assert "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_Stereo.dll" in files
    assert "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_VisionCs.dxil" in files
    for item in package["nexus_requirements"]:
        assert item["install_scope"] == "cyberpunk-2077-vr-only"
        assert item["cost"] == "free"
        assert item["file_id"] == 157829
        assert item["acquisition"] == "official-browser-auto-import"
    release = backend["upstream_release"]
    assert release["download_url"].startswith("https://github.com/dariulone/cyberpunk-vr-port/releases/download/0.1.7/")
    assert release["archive_sha256"].lower() == "a77b211d4db4e75b61b08951762e9034f60511731f3a441f21b11d1f723c7c84"
    assert any("CyberpunkVR_Hands.dll" in f["path"] for f in release["legacy_files"])
    archive = ROOT / "artifacts/cyberpunk-upstream-0.1.7/CyberpunkVRPort-0.1.7.zip"
    if archive.exists():
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == release["archive_sha256"].lower()
        with zipfile.ZipFile(archive) as z:
            for name, expected in files.items():
                assert hashlib.sha256(z.read(name)).hexdigest() == expected.lower(), name
    credit = json.loads((ROOT / "config/controller-maps/cyberpunk-2077.json").read_text())
    assert credit["conversion_credit"]["creator"] == "dariulone"
    assert credit["verification_state"] == "source_verified"
    print("Cyberpunk 0.1.7 upstream integrity, consolidated hands, attribution and Cyberpunk-only dependency scope validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
