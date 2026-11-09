"""Ship and verify notices for source-built, statically linked dependencies."""
import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def install_notices(workspace, runtime):
    workspace, runtime = Path(workspace), Path(runtime)
    lock = workspace / 'scripts/native-sources-lock.json'
    built = workspace / 'build/third-party-notices'
    if (built / 'native-sources-lock.json').read_bytes() != lock.read_bytes():
        raise RuntimeError('Native source lock changed; reconfigure and rebuild before deployment')
    destination = runtime / 'licenses/native'
    shutil.copytree(built, destination, dirs_exist_ok=True)
    sources = json.loads(lock.read_text(encoding='utf-8'))['sources']
    notices = {}
    for name, entry in sources.items():
        for filename in entry['license_files']:
            relative = f'licenses/native/{name}/{filename}'
            notices[relative] = digest(runtime / relative)
    manifest = {'schema': 1, 'source_lock_sha256': digest(lock),
                'application_sha256': digest(runtime / 'uxzView.exe'),
                'components': sources, 'notices': notices}
    (runtime / 'native-dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    validate_notices(workspace, runtime)
    return manifest


def validate_notices(workspace, runtime):
    workspace, runtime = Path(workspace), Path(runtime)
    lock = workspace / 'scripts/native-sources-lock.json'
    manifest = json.loads((runtime / 'native-dependencies.json').read_text(encoding='utf-8'))
    sources = json.loads(lock.read_text(encoding='utf-8'))['sources']
    expected = {f'licenses/native/{name}/{filename}' for name, entry in sources.items()
                for filename in entry['license_files']}
    if (manifest['source_lock_sha256'] != digest(lock) or manifest['components'] != sources
            or manifest['application_sha256'] != digest(runtime / 'uxzView.exe')
            or set(manifest['notices']) != expected):
        raise RuntimeError('Native dependency inventory differs from the current build/lock')
    for relative, checksum in manifest['notices'].items():
        path = runtime / relative
        if not path.is_file() or digest(path) != checksum:
            raise RuntimeError('Missing or changed native dependency notice: ' + relative)
    return manifest
