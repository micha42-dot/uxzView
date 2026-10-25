"""Build optional native tools and pinned OCR models, separate from the viewer.

Requires CMake/Ninja and a C/C++ toolchain. No Python is needed by end users.
All downloads are pinned by tool-sources-lock.json; generated packs are uncompressed
streaming containers so the viewer needs no archive/installer runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.deps/tool-sources'
WORK = ROOT / 'build/optional-tools'
LOCK = json.loads((ROOT / 'scripts/tool-sources-lock.json').read_text())


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(item):
    path = CACHE / item['file']
    CACHE.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        temporary = path.with_suffix(path.suffix + '.part')
        try:
            with urllib.request.urlopen(item['url'], timeout=60) as response, temporary.open('wb') as output:
                shutil.copyfileobj(response, output)
            if temporary.stat().st_size != item['bytes'] or digest(temporary) != item['sha256']:
                raise RuntimeError('Source checksum mismatch: ' + item['file'])
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    if path.stat().st_size != item['bytes'] or digest(path) != item['sha256']:
        raise RuntimeError('Source checksum mismatch: ' + str(path))
    return path


def source(key):
    archive = fetch(LOCK['sources'][key])
    destination = WORK / 'sources' / key
    marker = destination / '.source-sha256'
    if not marker.exists() or marker.read_text() != digest(archive):
        if marker.exists():
            raise RuntimeError('Remove the stale build source directory manually: ' + str(destination))
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as stream:
            members = stream.getmembers()
            for member in members:
                # Drop the archive's top directory. Reject links and special files.
                parts = Path(member.name).parts
                if len(parts) < 2:
                    continue
                if member.issym() or member.islnk():
                    continue  # Upstream example links are not build inputs.
                if not (member.isfile() or member.isdir()):
                    raise RuntimeError('Unexpected archive special file')
                member.name = Path(*parts[1:]).as_posix()
                stream.extract(member, destination, filter='data')
        marker.write_text(digest(archive))
    return destination


def run(*args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def compiler_runtime():
    """Pin the three Windows runtime DLLs to their locked SDK source packages."""
    from dependency_sources import fields
    sdk = ROOT / '.deps/sdk/ucrt64'
    if not (sdk / 'bin/g++.exe').is_file():
        raise RuntimeError('Windows tool packs require scripts/bootstrap_sdk.py first')
    os.environ['PATH'] = str(sdk / 'bin') + os.pathsep + os.environ.get('PATH', '')
    needed = {'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll'}
    packages = []
    binaries = {}
    locked = json.loads((ROOT / 'scripts/sdk-lock.json').read_text())['packages']
    for item in locked:
        if not any('-' + name + '-' in item['file'] for name in ['libgcc', 'libstdc++', 'libwinpthread']):
            continue
        cached = ROOT / '.deps/cache' / item['file']
        if digest(cached) != item['sha256']:
            raise RuntimeError('Invalid compiler runtime source package: ' + str(cached))
        metadata = None
        with tarfile.open(cached, mode='r|*') as archive_file:
            for member in archive_file:
                name = member.name.removeprefix('./')
                if name == '.PKGINFO':
                    metadata = fields(archive_file.extractfile(member).read())
                elif Path(name).name in needed and member.isfile():
                    filename = Path(name).name
                    path = sdk / 'bin' / filename
                    expected = hashlib.file_digest(archive_file.extractfile(member), 'sha256').hexdigest()
                    if digest(path) != expected:
                        raise RuntimeError('Compiler runtime differs from SDK lock: ' + filename)
                    binaries[filename] = dict(sha256=expected, package=item['file'])
        if metadata:
            packages.append(dict(binary_url=item['url'], binary_sha256=item['sha256'],
                source_url='https://repo.msys2.org/mingw/sources/' + metadata['pkgbase'][0] + '-' + metadata['pkgver'][0] + '.src.tar.zst',
                license=metadata.get('license', [])))
    if set(binaries) != needed:
        raise RuntimeError('Compiler runtime source mapping is incomplete')
    origin = WORK / 'compiler-runtime-sources.json'
    origin.write_text(json.dumps(dict(packages=packages, binaries=binaries), indent=2) + '\n')
    return sdk, origin


def build(name, src, prefix, options, jobs):
    directory = WORK / name
    run('cmake', '-S', src, '-B', directory, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_INSTALL_PREFIX=' + str(prefix), *options)
    run('cmake', '--build', directory, '--parallel', str(jobs))
    run('cmake', '--install', directory)


def pack(output, manifest, files):
    entries = []
    for relative, path in sorted(files.items()):
        entries.append(dict(path=relative, bytes=path.stat().st_size, sha256=digest(path),
                            executable=relative.startswith('bin/')))
    manifest = dict(schema=1, **manifest, files=entries)
    header = json.dumps(manifest, separators=(',', ':'), ensure_ascii=True).encode()
    destination = output / (manifest['id'] + '-' + manifest['version'] + '-' + manifest['os'] + '.ixtool')
    with destination.open('wb') as stream:
        stream.write(b'UXZTOOL1\n' + struct.pack('>I', len(header)) + header)
        for item in entries:
            with files[item['path']].open('rb') as source_file:
                shutil.copyfileobj(source_file, stream, 1024 * 1024)
    return dict(manifest, files=None, packageUrl=destination.name, bytes=destination.stat().st_size,
                installedBytes=sum(item['bytes'] for item in entries), sha256=digest(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--only', choices=['qr', 'ocr', 'all'], default='all')
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    runtime_sdk, runtime_origin = compiler_runtime() if os.name == 'nt' else (None, None)
    output = ROOT / 'dist/tools'
    output.mkdir(parents=True, exist_ok=True)
    prefix = WORK / 'install'
    os_name = {'Windows': 'windows', 'Linux': 'linux', 'Darwin': 'macos'}[platform.system()]
    arch = {'amd64': 'x86_64', 'x86_64': 'x86_64', 'arm64': 'arm64', 'aarch64': 'arm64'}[platform.machine().lower()]
    extension = '.exe' if os.name == 'nt' else ''
    catalog_path = output / 'uxzView-tools.json'
    old = json.loads(catalog_path.read_text())['packages'] if catalog_path.exists() else []
    packages = {item['id']: item for item in old if item.get('os') in [os_name, 'any']}

    def files_for(executable, licenses):
        files = {'bin/' + executable.name: executable, 'licenses/uxzView-GPL-3.0': ROOT / 'LICENSE'}
        for name, path in licenses.items():
            files['licenses/' + name] = path
        files['licenses/tool-sources-lock.json'] = ROOT / 'scripts/tool-sources-lock.json'
        if os.name == 'nt':
            # The tools are statically linked to their engines. Only the compiler
            # runtimes remain shared; never pull unrelated SDK image/network DLLs.
            for dll in ['libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll']:
                found = runtime_sdk / 'bin' / dll
                if not found:
                    raise RuntimeError('Compiler runtime missing: ' + dll)
                files['bin/' + dll] = found
            for name in ['COPYING.RUNTIME', 'COPYING3']:
                runtime_license = runtime_sdk / 'share/licenses/gcc' / name
                if not runtime_license.exists():
                    raise RuntimeError('Compiler runtime license missing: ' + str(runtime_license))
                files['licenses/gcc-' + name] = runtime_license
            files['licenses/winpthreads-COPYING'] = runtime_sdk / 'share/licenses/winpthreads/COPYING'
            files['licenses/compiler-runtime-sources.json'] = runtime_origin
        return files

    if args.only in ['qr', 'all']:
        zxing = source('zxing')
        build('qr', ROOT / 'src/tools', prefix, ['-DZXING_SOURCE=' + str(zxing)], args.jobs)
        packages['zxing-cpp'] = pack(output, dict(id='zxing-cpp', name='QR / Barcode (ZXing-C++)',
            version='3.0.0', kind='image-tool', os=os_name, arch=arch, protocol='zxing-json-v1',
            entryPoint='bin/uxz-code-reader' + extension),
            files_for(prefix / ('bin/uxz-code-reader' + extension), {'zxing-LICENSE': zxing / 'LICENSE'}))
    if args.only in ['ocr', 'all']:
        lept = source('leptonica')
        build('leptonica', lept, prefix, ['-DBUILD_SHARED_LIBS=OFF', '-DSW_BUILD=OFF', '-DBUILD_PROG=OFF'] +
              ['-DENABLE_' + key + '=OFF' for key in ['ZLIB', 'PNG', 'GIF', 'JPEG', 'TIFF', 'WEBP', 'OPENJPEG']], args.jobs)
        tess = source('tesseract')
        build('tesseract', tess, prefix, ['-DCMAKE_PREFIX_PATH=' + str(prefix), '-DBUILD_SHARED_LIBS=OFF',
              '-DSW_BUILD=OFF', '-DOPENMP_BUILD=OFF', '-DGRAPHICS_DISABLED=ON', '-DDISABLED_LEGACY_ENGINE=ON',
              '-DBUILD_TRAINING_TOOLS=OFF', '-DBUILD_TESTS=OFF', '-DUSE_SYSTEM_ICU=OFF', '-DDISABLE_TIFF=ON',
              '-DDISABLE_ARCHIVE=ON', '-DDISABLE_CURL=ON', '-DENABLE_NATIVE=OFF'], args.jobs)
        packages['tesseract-ocr'] = pack(output, dict(id='tesseract-ocr', name='Text / OCR (Tesseract)',
            version='5.5.3', kind='image-tool', os=os_name, arch=arch, protocol='tesseract-text-v1',
            entryPoint='bin/tesseract' + extension), files_for(prefix / ('bin/tesseract' + extension),
            {'tesseract-LICENSE': tess / 'LICENSE', 'leptonica-LICENSE': lept / 'leptonica-license.txt'}))
        license_path = fetch(LOCK['model_license'])
        for code, item in LOCK['models'].items():
            packages['tessdata-' + code] = pack(output, dict(id='tessdata-' + code, name='OCR: ' + code,
                version='4.1.0', kind='ocr-language', os='any', arch='any', language=code),
                {'data/' + code + '.traineddata': fetch(item), 'licenses/tessdata-LICENSE': license_path})
    ordered = sorted(packages.values(), key=lambda item: (item['id'] != 'zxing-cpp', item['id'] != 'tesseract-ocr', item['id']))
    catalog = dict(schema=1, packages=ordered)
    catalog_path.write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')
    assets = [catalog_path] + [output / item['packageUrl'] for item in ordered]
    (output / 'uxzView-tools-SHA256SUMS.txt').write_text(
        ''.join(digest(path) + '  ' + path.name + '\n' for path in assets), encoding='utf-8')
    # Resource contains metadata only, never executables or trained models.
    (ROOT / 'resources/tool-catalog.json').write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')
    print('Optional packs:', output)


if __name__ == '__main__':
    main()
