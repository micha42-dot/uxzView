"""Populate the verified native source cache; CMake can also fetch these itself."""
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def fetch():
    sources = json.loads((ROOT / 'scripts/native-sources-lock.json').read_text(encoding='utf-8'))['sources']
    cache = ROOT / '.deps/cache'
    cache.mkdir(parents=True, exist_ok=True)
    for entry in sources.values():
        target = cache / entry['file']
        def valid(path):
            if not path.is_file():
                return False
            with path.open('rb') as stream:
                return hashlib.file_digest(stream, 'sha256').hexdigest() == entry['sha256']
        if valid(target):
            continue
        part = target.with_suffix(target.suffix + '.part')
        with urllib.request.urlopen(entry['url'], timeout=120) as response, part.open('wb') as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if not valid(part):
            raise RuntimeError('SHA-256 mismatch: ' + entry['file'])
        part.replace(target)


if __name__ == '__main__':
    fetch()
