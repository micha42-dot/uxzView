"""Build a smaller Windows libheif with the locked SDK and pinned upstream sources.

HEVC encoding uses kvazaar; AV1 encoding uses AOM. Redundant x265, SVT-AV1
and rav1e encoders are omitted. Decoder support remains enabled.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / '.deps/sdk/ucrt64'
VERSION = '1.23.6'
SOURCE_URL = 'https://repo.msys2.org/mingw/sources/mingw-w64-libheif-1.23.6-1.src.tar.zst'
SOURCE_SHA256 = '6ab1a041fee5ddf2ddd18de511296e1643c31f3731dad926bec0c2742a50fd56'
UPSTREAM_SHA256 = '4484346dc5995319dbc11e3a1c35d0a2ec46511ce370900869337fd2c7033125'
OPTIONS = {
    'BUILD_SHARED_LIBS': 'ON', 'BUILD_TESTING': 'OFF', 'BUILD_DOCUMENTATION': 'OFF',
    'WITH_EXAMPLES': 'OFF', 'WITH_GDK_PIXBUF': 'OFF', 'ENABLE_PLUGIN_LOADING': 'OFF',
    'WITH_HEADER_COMPRESSION': 'ON', 'WITH_LIBDE265': 'ON', 'WITH_DAV1D': 'ON',
    'WITH_KVAZAAR': 'ON', 'WITH_X265': 'OFF', 'WITH_SvtEnc': 'OFF', 'WITH_RAV1E': 'OFF',
    'WITH_X264': 'ON', 'WITH_OpenH264_DECODER': 'ON',
    'WITH_AOM_DECODER': 'ON', 'WITH_AOM_ENCODER': 'ON',
    'WITH_JPEG_DECODER': 'ON', 'WITH_JPEG_ENCODER': 'ON',
    'WITH_OpenJPEG_DECODER': 'ON', 'WITH_OpenJPEG_ENCODER': 'ON', 'WITH_OPENJPH_ENCODER': 'ON',
}

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def build_codec(*, name, version, source_url, source_sha256, upstream_sha256, options,
                library_path, target, recipe_path):
    output = ROOT / 'build' / (name.removeprefix('lib')+'-lite')
    library = output / library_path
    recipe = digest(Path(recipe_path))
    builder = digest(Path(__file__))
    lock_hash = digest(ROOT / 'scripts/sdk-lock.json')
    manifest_path = output / 'build-origin.json'
    if library.is_file() and manifest_path.is_file():
        previous = json.loads(manifest_path.read_text())
        if (previous.get('recipe_sha256') == recipe and previous.get('builder_sha256') == builder and previous.get('sdk_lock_sha256') == lock_hash
                and previous.get('sha256') == digest(library)):
            return library
    cached = ROOT / '.deps/cache' / source_url.rsplit('/',1)[1]
    cached.parent.mkdir(parents=True, exist_ok=True)
    if not cached.is_file() or digest(cached) != source_sha256:
        temporary = cached.with_suffix('.part')
        try:
            with urllib.request.urlopen(source_url, timeout=60) as response, temporary.open('wb') as stream:
                while block := response.read(1024*1024): stream.write(block)
            if digest(temporary) != source_sha256:
                raise RuntimeError('libheif source archive SHA-256 mismatch')
            temporary.replace(cached)
        finally:
            temporary.unlink(missing_ok=True)
    sources = ROOT / '.deps/sources'
    sources.mkdir(parents=True, exist_ok=True)
    upstream = sources / f'{name}-{version}.tar.gz'
    with tarfile.open(cached) as archive:
        upstream.write_bytes(archive.extractfile(f'mingw-w64-{name}/{name}-{version}.tar.gz').read())
    if digest(upstream) != upstream_sha256:
        raise RuntimeError('Upstream libheif source SHA-256 mismatch')
    # Always restore the verified source tree: local edits must not silently
    # enter a release while the provenance still describes unmodified sources.
    with tarfile.open(upstream) as archive:
        archive.extractall(sources, filter='data')
    env = os.environ.copy()
    env['PATH'] = str(SDK/'bin') + os.pathsep + env['PATH']
    cmake = str(SDK/'bin/cmake.exe')
    subprocess.run([cmake, '-S', str(sources/f'{name}-{version}'), '-B', str(output), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_PREFIX_PATH={SDK}',
                    '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++',
                    *[f'-D{key}={value}' for key,value in options.items()]], env=env, check=True)
    subprocess.run([cmake,'--build',str(output),'--target',target,'--parallel','3'], env=env, check=True)
    subprocess.run([str(SDK/'bin/strip.exe'),'--strip-unneeded',str(library)],env=env,check=True)
    manifest = {'name': name, 'version': version, 'sha256': digest(library),
                'source_url': source_url, 'source_sha256': source_sha256,
                'upstream_sha256': upstream_sha256, 'recipe': Path(recipe_path).relative_to(ROOT).as_posix(),
                'builder': 'scripts/build_heif.py', 'builder_sha256': builder,
                'recipe_sha256': recipe, 'sdk_lock_sha256': lock_hash, 'cmake_options': options}
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return library

def build():
    return build_codec(name='libheif',version=VERSION,source_url=SOURCE_URL,
                       source_sha256=SOURCE_SHA256,upstream_sha256=UPSTREAM_SHA256,options=OPTIONS,
                       library_path='libheif/libheif.dll',target='heif',recipe_path=__file__)

if __name__ == '__main__':
    print(build())
