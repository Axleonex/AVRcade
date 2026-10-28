"""Verify and repin the official 0.1.9 runtime archive; retain framework pins."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.9'
DIGEST = 'adc255885f24ede400c1510a9dd34ae5e32a3a61b3cd55bef3a017f802d48bf0'
COMMIT = 'e73fab025dd6cbc2840d95f5a2baeec870a359f3'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='Official CyberpunkVRPort-0.1.9.zip')
    args = parser.parse_args()
    if hashlib.sha256(args.archive.read_bytes()).hexdigest() != DIGEST:
        raise ValueError('Archive does not match the official release digest.')
    path = ROOT / 'config/redengine/native/cyberpunk-2077-backend.json'
    package = json.loads(path.read_text(encoding='utf-8-sig'))
    backend = package['backend']
    legacy = backend.get('upstream_release', {}).get('legacy_files', []).copy()
    if backend['version'] != VERSION:
        legacy += backend['files']
    with zipfile.ZipFile(args.archive) as archive:
        files = [dict(path=i.filename.replace('/', '\\'),
                      sha256=hashlib.sha256(archive.read(i)).hexdigest().upper())
                 for i in archive.infolist() if not i.is_dir() and
                 i.filename.split('/')[0] in {'bin', 'r6', 'red4ext', 'archive', 'engine'}]
    backend.update(version=VERSION, files=files, directories=[],
        source=f'https://github.com/dariulone/cyberpunk-vr-port/commit/{COMMIT}',
        upstream_release=dict(
            download_url=f'https://github.com/dariulone/cyberpunk-vr-port/releases/download/{VERSION}/CyberpunkVRPort-{VERSION}.zip',
            archive_sha256=DIGEST.upper(),
            legacy_files=list({(f['path'].lower(), f['sha256'].lower()): f for f in legacy}.values())))
    path.write_text(json.dumps(package, indent=2) + '\n', encoding='utf-8')
    print(f'Pinned {VERSION}: {len(files)} official runtime files. Framework pins retained.')


if __name__ == '__main__':
    main()
