"""Package an installed Linux tree/macOS bundle with matching IxView source."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import tarfile
from source_archive import create_source_archive, ROOT
from runtime_files import private_part

def archive_stage(stage, source, output):
    def public(member):
        parts = tuple(part.casefold() for part in Path(member.name).parts)
        if any(private_part(part) for part in parts):
            return None
        # Native builds have no factory tool bundle yet. Local tool stores are
        # personal data, including activation choices and running-job leases.
        relative = parts[1:]
        for prefix in (('tools',), ('bin', 'tools'), ('uxzview.app', 'contents', 'macos', 'tools')):
            if relative[:len(prefix)] == prefix:
                return None
        return member

    with tarfile.open(output, 'w:gz') as archive:
        archive.add(stage, arcname='uxzView', filter=public)
        archive.add(source, arcname='uxzView-source.zip')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=ROOT / 'dist/native')
    parser.add_argument('--name', default=f'uxzView-{platform.system().lower()}-{platform.machine()}')
    args = parser.parse_args()
    if not args.name or Path(args.name).name != args.name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.name):
        parser.error('--name must be a simple package name')
    stage = args.stage.resolve()
    if not (stage / 'bin/uxzView').is_file() and not (stage / 'uxzView.app/Contents/MacOS/uxzView').is_file():
        parser.error('No installed IxView binary found in --stage')
    source = create_source_archive(ROOT / 'dist/uxzView-source.zip')
    output = ROOT / 'dist' / (args.name + '.tar.gz')
    archive_stage(stage, source, output)

    digest = hashlib.sha256()
    with output.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    manifest = {output.name: {'bytes': output.stat().st_size, 'sha256': digest.hexdigest()}}
    (ROOT / 'dist' / (args.name + '.sha256.json')).write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
