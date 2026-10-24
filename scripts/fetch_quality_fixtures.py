"""Fetch the pinned CC0 camera RAW fixtures into build/quality-fixtures.

No downloads occur during CMake configuration or ordinary offline tests.
Files stay out of the source and application packages.
"""
import hashlib
import json
from pathlib import Path
import urllib.request
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

def fetch():
    manifest = json.loads((ROOT/'tests/fixtures/raw-manifest.json').read_text(encoding='utf-8'))
    output = ROOT/'build/quality-fixtures'
    output.mkdir(parents=True,exist_ok=True)
    for fixture in manifest['fixtures']:
        name, url = fixture['file'], fixture['url']
        parsed = urlparse(url)
        if (Path(name).name != name or '\\' in name or ':' in name or name.startswith('.')
                or parsed.scheme != 'https' or parsed.hostname != 'raw.pixls.us'
                or fixture['license'] != 'CC0-1.0'):
            raise RuntimeError('Invalid fixture provenance or path')
        path = output/name
        if path.is_file() and digest(path) == fixture['sha256']:
            print('Verified cached fixture:', name,flush=True)
            continue
        temporary = path.with_suffix(path.suffix+'.part')
        try:
            request=urllib.request.Request(url,headers={'User-Agent':'IxView-quality-tests/0.4'})
            total=0
            with urllib.request.urlopen(request,timeout=60) as response, temporary.open('wb') as stream:
                while block:=response.read(1024*1024):
                    total+=len(block)
                    if total>128*1024*1024: raise RuntimeError('Fixture exceeds 128 MiB')
                    stream.write(block)
            if digest(temporary)!=fixture['sha256']: raise RuntimeError('Fixture SHA-256 mismatch: '+name)
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
        print('Downloaded and verified:',name,path.stat().st_size,flush=True)
    return output

if __name__=='__main__':
    print(fetch())
