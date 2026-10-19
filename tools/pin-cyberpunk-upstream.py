"""Pin the author's 0.1.7 archive without bundling or rebuilding its binaries."""
import hashlib
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERSION = "0.1.7"
DIGEST = "a77b211d4db4e75b61b08951762e9034f60511731f3a441f21b11d1f723c7c84"
COMMIT = "a0c23fe110530bb74a73b3adf1ffa7d83050c43d"
archive = ROOT / "artifacts/cyberpunk-upstream-0.1.7/CyberpunkVRPort-0.1.7.zip"
assert hashlib.sha256(archive.read_bytes()).hexdigest() == DIGEST
path = ROOT / "config/redengine/native/cyberpunk-2077-backend.json"
package = json.loads(path.read_text(encoding="utf-8-sig"))
old = package["backend"]
legacy = old.get("upstream_release", {}).get("legacy_files", old["files"]).copy()
historical = json.loads((path.parent / "cyberpunk-2077-installed-files.json").read_text())
for item in historical["packages"]:
    if item["name"].startswith("CyberpunkVR Port"):
        legacy.extend(item["files"])
legacy.append(dict(path=r"bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_Stereo\bridge\.gitkeep",
    sha256="04679A195263DEB63AB0D02DF17F0A3DB09709B186950DE77695D71C16D87DED"))
unique = {(f["path"].lower(), f["sha256"].lower()): f for f in legacy}
with zipfile.ZipFile(archive) as release:
    files = [dict(path=i.filename.replace("/", "\\"), sha256=hashlib.sha256(release.read(i)).hexdigest().upper())
             for i in release.infolist() if not i.is_dir() and
             i.filename.split("/")[0] in {"bin", "red4ext", "archive", "r6", "engine"}]
package["backend"] = dict(name="CyberpunkVR Port by dariulone", version=VERSION,
    creator="dariulone", source=f"https://github.com/dariulone/cyberpunk-vr-port/commit/{COMMIT}",
    project_url="https://www.nexusmods.com/cyberpunk2077/mods/30093", license="MIT",
    payload_dir="native/cyberpunk-2077", files=files, directories=[],
    upstream_release=dict(download_url=f"https://github.com/dariulone/cyberpunk-vr-port/releases/download/{VERSION}/CyberpunkVRPort-{VERSION}.zip",
        archive_sha256=DIGEST.upper(), legacy_files=list(unique.values())))
package.pop("manual_requirements", None)
package["nexus_requirements"] = [dict(name="More Occluders (Scripted v0.3)", creator="sosuine",
    url="https://www.nexusmods.com/cyberpunk2077/mods/33136",
    install_scope="cyberpunk-2077-vr-only", cost="free",
    file_id=157829, acquisition="official-browser-auto-import",
    reason="Required by CyberpunkVR Port 0.1.7; the free Nexus download is automatically imported by AVRcade.")]
path.write_text(json.dumps(package, indent=2) + "\n", encoding="utf-8")
profile_path = path.parent / "cyberpunk-2077.json"
profile = json.loads(profile_path.read_text())
profile["dependencies"] = [d for d in profile["dependencies"] if not any(s in d["path"] for s in
    ["CyberpunkVR_Hands", "CyberpunkVRPort_HUD", "hud_layout.ini"])]
for d in profile["dependencies"]:
    if "CyberpunkVRPort_Stereo" in d["path"]:
        d["reason"] = "Official CyberpunkVR Port 0.1.7 camera selector by dariulone"
    if "CyberpunkVRPort_VRIK" in d["path"]:
        d["reason"] = "Official upstream avatar and controller pose bridge"
for relative in [r"red4ext\plugins\CyberpunkVR_Stereo\CyberpunkVR_VisionCs.dxil",
                 r"r6\scripts\CyberpunkVRPort_Holster\vrport_holster.reds"]:
    if not any(d["path"] == relative for d in profile["dependencies"]):
        profile["dependencies"].append(dict(path=relative, reason="Required by the official 0.1.7 release"))
profile["conflicts"].append(dict(path=r"red4ext\plugins\CyberpunkVR_Hands\CyberpunkVR_Hands.dll",
    reason="0.1.7 incorporates hands into Stereo; the old DLL would detour the same hooks twice"))
profile["conflicts"] = list({d["path"]: d for d in profile["conflicts"]}.values())
profile["loading"]["activation_mode"] = "managed_session"
profile["loading"].pop("activation_environment", None)
profile["loading"]["state"] = "planned"
profile["conversion"]["capabilities"] = {k: "planned" for k in profile["conversion"]["capabilities"]}
profile["verification"] = dict(status="preflight_ready", evidence=[
    f"2026-10-18: Official 0.1.7 tag {COMMIT}; archive SHA-256 {DIGEST.upper()} verified against the author's GitHub release asset digest.",
    "The earlier 0.1.2 Quest 2 headset evidence remains historical; it does not verify 0.1.7.",
    "Unmodified official package is downloaded directly from dariulone. AVRcade parks its files outside mod scan paths between VR sessions.",
    "More Occluders (Scripted v0.3) is a separate upstream requirement; no local 0.1.7 headset acceptance has been recorded."])
profile_path.write_text(json.dumps(profile, indent=2) + "\n", encoding="utf-8")
schema_path = ROOT / "config/schemas/redengine-native-profile.schema.json"
schema = json.loads(schema_path.read_text())
modes = schema["properties"]["loading"]["properties"]["activation_mode"]["enum"]
if "managed_session" not in modes:
    modes.append("managed_session")
schema_path.write_text(json.dumps(schema, indent=2) + "\n", encoding="utf-8")
print(f"Pinned {len(files)} official release files; {len(unique)} known legacy file hashes.")
