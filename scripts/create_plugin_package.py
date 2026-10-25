"""Create a local IxView image-format package without loading native code."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--manifest', required=True, type=Path)
parser.add_argument('--plugin', required=True, type=Path)
parser.add_argument('--dependency', action='append', default=[], type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
required = {'schema', 'kind', 'id', 'version', 'os', 'arch', 'qtMajor', 'qtMinor'}
if not required <= manifest.keys() or manifest['schema'] != 1 or manifest['kind'] != 'qt-imageformats':
    parser.error('Expected a schema-1 qt-imageformats manifest with platform and Qt version fields.')
if not re.fullmatch(r'[a-z0-9][a-z0-9.-]{1,63}', manifest['id']) or '..' in manifest['id']:
    parser.error('Invalid package id')
if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', manifest['version']):
    parser.error('Invalid package version')
files, names, total = [], set(), 0
for index, path in enumerate([args.plugin, *args.dependency]):
    if not path.is_file() or path.is_symlink():
        parser.error(f'Not a regular binary: {path}')
    if path.name.casefold() in names or path.name.startswith('.') or ':' in path.name:
        parser.error(f'Invalid or duplicate filename: {path.name}')
    names.add(path.name.casefold())
    if path.stat().st_size > 48 * 1024 * 1024:
        parser.error('Binary is too large')
    data = path.read_bytes()
    total += len(data)
    if not data or total > 48 * 1024 * 1024:
        parser.error('Maximum total binary size is 48 MiB')
    files.append({'name': path.name, 'role': 'plugin' if index == 0 else 'dependency',
                  'sha256': hashlib.sha256(data).hexdigest(),
                  'data': base64.b64encode(data).decode('ascii')})
if len(files) > 32:
    parser.error('Maximum file count is 32')
manifest['files'] = files
encoded = json.dumps(manifest, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
if len(encoded) > 64 * 1024 * 1024:
    parser.error('Package exceeds 64 MiB JSON limit')
# Never silently replace an existing package.
with args.output.open('xb') as stream:
    stream.write(encoded)
print(json.dumps({'package': str(args.output), 'bytes': len(encoded),
                  'sha256': hashlib.sha256(encoded).hexdigest()}, indent=2))
