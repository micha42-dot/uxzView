"""Prepare verified Windows tools for deployment and reproducible releases.

The release snapshot lives in build/, independently of a developer's installed,
disabled or removed tools in dist/uxzView/tools. Never archive that personal state.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile

IDS = {'zxing-cpp', 'tesseract-ocr', *(f'tessdata-{code}' for code in ('deu', 'eng', 'fra', 'jpn', 'kor'))}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_path(name):
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_+./-]{1,180}', name):
        return False
    parts = name.split('/')
    return all(part and part not in {'.', '..'} and not part.endswith('.') and
               not re.fullmatch(r'(con|prn|aux|nul|com[1-9]|lpt[1-9])(\..*)?', part, re.I) for part in parts)


def unpack(package, expected, destination):
    if not 0 < package.stat().st_size <= 512 * 1024 * 1024 or package.stat().st_size != expected['bytes'] or digest(package) != expected['sha256']:
        raise RuntimeError('Tool package differs from the embedded catalog: ' + package.name)
    with package.open('rb') as stream:
        if stream.read(9) != b'UXZTOOL1\n':
            raise RuntimeError('Invalid tool package magic')
        length = struct.unpack('>I', stream.read(4))[0]
        if not 0 < length <= 1024 * 1024:
            raise RuntimeError('Invalid tool manifest length')
        manifest = json.loads(stream.read(length))
        for key in ('schema', 'id', 'kind', 'version', 'os', 'arch', 'protocol', 'language', 'entryPoint'):
            if manifest.get(key) != expected.get(key):
                raise RuntimeError('Tool package/catalog mismatch: ' + key)
        files = manifest['files']
        if not 0 < len(files) <= 256:
            raise RuntimeError('Invalid tool file count')
        seen = set()
        for entry in files:
            name = entry['path']
            if not safe_path(name) or name.casefold() in seen or name.casefold() == 'manifest.json':
                raise RuntimeError('Invalid or duplicate tool path: ' + name)
            seen.add(name.casefold())
            length = entry['bytes']
            if not isinstance(length, int) or not 0 < length <= 512 * 1024 * 1024:
                raise RuntimeError('Invalid tool payload size')
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            checksum = hashlib.sha256()
            with target.open('wb') as output:
                remaining = length
                while remaining:
                    chunk = stream.read(min(remaining, 1024 * 1024))
                    if not chunk:
                        raise RuntimeError('Truncated tool payload')
                    remaining -= len(chunk)
                    output.write(chunk)
                    checksum.update(chunk)
            if checksum.hexdigest() != entry['sha256']:
                raise RuntimeError('Invalid tool file checksum: ' + name)
        if stream.read(1):
            raise RuntimeError('Trailing tool package data')
        required = 'data/' + manifest['language'] + '.traineddata' if manifest['kind'] == 'ocr-language' else manifest['entryPoint']
        if not safe_path(required) or required.casefold() not in seen:
            raise RuntimeError('Required tool executable/model missing')
    return dict(manifest, enabled=True)


def prepare(workspace, runtime):
    workspace, runtime = Path(workspace), Path(runtime)
    catalog = json.loads((workspace / 'resources/tool-catalog.json').read_text())['packages']
    packages = {entry['id']: entry for entry in catalog if entry['id'] in IDS}
    if set(packages) != IDS or any(entry['os'] not in {'windows', 'any'} or
                                 entry.get('arch') not in {'x86_64', 'any'} for entry in packages.values()):
        raise RuntimeError('Build all Windows tool packs before deployment: scripts/build_tool_packs.py')
    build = workspace / 'build'
    target = build / 'bundled-tools'
    with tempfile.TemporaryDirectory(prefix='bundle-tools-', dir=build) as temporary:
        stage = Path(temporary)
        installed = {}
        for identifier, entry in sorted(packages.items()):
            if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', entry['version']):
                raise RuntimeError('Invalid bundled tool version')
            filename = entry['packageUrl']
            if not safe_path(filename) or '/' in filename:
                raise RuntimeError('Bundled tool must reference a local package filename')
            folder = stage / identifier / entry['version']
            manifest = unpack(workspace / 'dist/tools' / filename, entry, folder)
            (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            installed[identifier] = manifest
        (stage / 'tools.json').write_text(json.dumps(dict(schema=1, layout='flat', packages=installed), indent=2) + '\n', encoding='utf-8')
        if target.is_symlink() or (target.exists() and not target.resolve().is_relative_to(build.resolve())):
            raise RuntimeError('Invalid bundled-tools staging directory')
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(stage, target)
    files = {path.relative_to(target).as_posix(): dict(bytes=path.stat().st_size, sha256=digest(path))
             for path in sorted(target.rglob('*')) if path.is_file()}
    inventory = dict(schema=1, packages=[dict(id=entry['id'], version=entry['version'],
                     package_sha256=entry['sha256']) for entry in packages.values()], files=files)
    (runtime / 'bundled-tools.json').write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
    installed_root = runtime / 'tools'
    if installed_root.is_symlink() or (installed_root.exists() and not installed_root.resolve().is_relative_to(runtime.resolve())):
        raise RuntimeError('Installed tool directory resolves outside the application')
    # Existing state belongs to the user, including deliberate removals.
    if not (installed_root / 'tools.json').exists():
        if installed_root.exists() and any(installed_root.iterdir()):
            raise RuntimeError('Existing tools directory has no state; left unchanged')
        shutil.copytree(target, installed_root, dirs_exist_ok=True)
    return inventory


def release_tools(runtime):
    runtime = Path(runtime)
    inventory_file = runtime / 'bundled-tools.json'
    if not inventory_file.exists():
        return
    inventory = json.loads(inventory_file.read_text(encoding='utf-8'))
    if inventory.get('schema') != 1 or {entry['id'] for entry in inventory['packages']} != IDS:
        raise RuntimeError('Invalid bundled tool inventory; run deploy.py')
    stage = runtime.parent.parent / 'build/bundled-tools'
    expected = inventory['files']
    actual = {path.relative_to(stage).as_posix() for path in stage.rglob('*') if path.is_file()}
    if stage.is_symlink() or actual != set(expected):
        raise RuntimeError('Bundled tool snapshot is missing or changed; run deploy.py')
    for relative, entry in sorted(expected.items()):
        if not safe_path(relative):
            raise RuntimeError('Unsafe bundled tool path')
        source = stage / PurePosixPath(relative)
        if source.is_symlink() or not source.resolve().is_relative_to(stage.resolve()) or source.stat().st_size != entry['bytes'] or digest(source) != entry['sha256']:
            raise RuntimeError('Bundled tool snapshot changed: ' + relative)
        yield 'tools/' + relative, source
