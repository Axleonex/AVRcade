"""Current release contract; the 0.1.2 patch/installed-DLL tests are historical."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    package = json.loads((ROOT / "config/redengine/native/cyberpunk-2077-backend.json").read_text())
    backend = package["backend"]
    assert backend["version"] == "0.1.9"
    assert backend["creator"] == "dariulone"
    assert backend["project_url"] == "https://www.nexusmods.com/cyberpunk2077/mods/30093"
    files = {f["path"].replace("\\", "/"): f["sha256"] for f in backend["files"]}
    assert len(files) == len(backend["files"]) == 692
    assert not any("CyberpunkVR_Hands.dll" in name for name in files)
    assert "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_Stereo.dll" in files
    assert "red4ext/plugins/CyberpunkVR_Stereo/CyberpunkVR_VisionCs.dxil" in files
    for item in package["nexus_requirements"]:
        assert item["install_scope"] == "cyberpunk-2077-vr-only"
        assert item["cost"] == "free"
        assert item["file_id"] == 157829
        assert item["acquisition"] == "official-browser-auto-import"
    release = backend["upstream_release"]
    assert release["download_url"].startswith("https://github.com/dariulone/cyberpunk-vr-port/releases/download/0.1.9/")
    assert release["archive_sha256"].lower() == "adc255885f24ede400c1510a9dd34ae5e32a3a61b3cd55bef3a017f802d48bf0"
    assert any("CyberpunkVR_Hands.dll" in f["path"] for f in release["legacy_files"])
    archive = ROOT / "artifacts/cyberpunk-2.5-20261028/CyberpunkVRPort-0.1.9.zip"
    if archive.exists():
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == release["archive_sha256"].lower()
        with zipfile.ZipFile(archive) as z:
            for name, expected in files.items():
                assert hashlib.sha256(z.read(name)).hexdigest() == expected.lower(), name
    frameworks = {f['name']: f for f in package['frameworks']}
    for name, minimum in {'RED4ext': '1.31.0', 'Cyber Engine Tweaks': '1.38.0',
                          'redscript': '0.5.32', 'Codeware': '1.21.0',
                          'ArchiveXL': '1.28.0', 'TweakXL': '1.12.0', 'Input Loader': '0.2.6'}.items():
        f = frameworks[name]
        assert tuple(map(int, f['version'].split('.'))) >= tuple(map(int, minimum.split('.')))
        assert len(f['marker_sha256']) == 64
        local = archive.parent / f['download_url'].split('/')[-1]
        if local.exists():
            assert hashlib.sha256(local.read_bytes()).hexdigest() == f['archive_sha256'].lower(), name
    profile = json.loads((ROOT / 'config/redengine/native/cyberpunk-2077.json').read_text())
    assert profile['game']['steam_buildid_observed'] == '26161058'
    for dep in profile['dependencies']:
        relative = dep['path'].replace('\\', '/')
        assert relative in files or any(f['marker'].replace('\\', '/').lower() == relative.lower()
                                        for f in frameworks.values()), relative
    credit = json.loads((ROOT / "config/controller-maps/cyberpunk-2077.json").read_text())
    assert credit["conversion_credit"]["creator"] == "dariulone"
    assert credit["verification_state"] == "source_verified"
    print("Cyberpunk 0.1.9 integrity, 2.5 framework minimums, exact build pin, attribution and dependency scope validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
