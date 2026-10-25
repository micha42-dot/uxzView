"""Record exact MSYS2 origins and corresponding source URLs for shipped DLLs.

Python 3.14+ is required to read .tar.zst SDK packages. No source code is
executed and source archives are not downloaded. --check-urls verifies their
presence in the upstream source index before preparing a public release.
"""
import argparse
from datetime import datetime, timezone
import hashlib
from html.parser import HTMLParser
import json
import re
from pathlib import Path
from runtime_files import release_files
import tarfile
import urllib.request
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
SOURCE_BASE = "https://repo.msys2.org/mingw/sources/"
OWN_DLLS = {"ixrawpreview.dll", "ixextensions.dll"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fields(raw):
    result = {}
    for line in raw.decode("utf-8").splitlines():
        if " = " in line and not line.startswith("#"):
            key, value = line.split(" = ", 1)
            result.setdefault(key, []).append(value)
    return result


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.names = set()

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.names.update(unquote(value) for key, value in attrs if key == "href" and value)


def check_source_urls(packages):
    packages = list(packages)
    for package in packages:
        url = package['source_url']
        name = url.rsplit('/', 1)[-1]
        if url != SOURCE_BASE + name or not name.endswith('.src.tar.zst'):
            raise RuntimeError('Unexpected corresponding-source URL: ' + url)
    with urllib.request.urlopen(SOURCE_BASE, timeout=60) as response:
        index = response.read(20 * 1024 * 1024).decode('utf-8')
    links = Links()
    links.feed(index)
    absent = [p['source_url'] for p in packages if p['source_url'].rsplit('/', 1)[1] not in links.names]
    if absent:
        raise RuntimeError(f'Corresponding sources absent from upstream index: {absent}')
    return datetime.now(timezone.utc).isoformat()


def refresh_urls():
    """Recheck a previously mapped runtime only when all artifact/lock hashes still match."""
    from package import validate_runtime_inventory
    runtime = ROOT / 'dist/uxzView'
    manifest = validate_runtime_inventory(runtime)
    if manifest.get('source_index') != SOURCE_BASE:
        raise RuntimeError('Unexpected source index; regenerate the complete source inventory')
    notes = (runtime / 'SOURCES.md').read_text(encoding='utf-8')
    checked = check_source_urls(manifest['packages'])
    notes, count = re.subn(r'^Source-index availability check: .*$',
                          'Source-index availability check: ' + checked, notes, flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError('Source notes missing the availability record; regenerate the source inventory')
    manifest['source_urls_checked_at'] = checked
    (runtime / 'dependency-sources.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (runtime / 'SOURCES.md').write_text(notes, encoding='utf-8')
    print(f"Rechecked {len(manifest['packages'])} source URLs; runtime and SDK hashes unchanged")


def collect(check_urls=False):
    runtime = ROOT / "dist/uxzView"
    if not (runtime / "uxzView.exe").is_file():
        raise RuntimeError("Run deploy.py before dependency_sources.py")
    shipped = {}
    custom_path = runtime / 'custom-builds.json'
    custom = json.loads(custom_path.read_text()) if custom_path.exists() else {}
    if custom:
        import build_heif, build_avif
        recipes = {'libheif.dll':build_heif,'libavif-16.dll':build_avif}
        if set(custom) != set(recipes):
            raise RuntimeError('Unknown custom runtime build')
        for filename, recipe in recipes.items():
            entry = custom[filename]
            if (entry['sha256'] != digest(runtime/filename)
                    or entry['recipe_sha256'] != digest(Path(recipe.__file__))
                    or entry['builder_sha256'] != digest(ROOT/'scripts/build_heif.py')
                    or entry['sdk_lock_sha256'] != digest(ROOT/'scripts/sdk-lock.json')
                    or entry['source_url'] != recipe.SOURCE_URL or entry['source_sha256'] != recipe.SOURCE_SHA256
                    or entry['upstream_sha256'] != recipe.UPSTREAM_SHA256 or entry['version'] != recipe.VERSION
                    or entry['cmake_options'] != recipe.OPTIONS):
                raise RuntimeError('Custom codec build/provenance changed: '+filename)
    for relative, path in release_files(runtime):
        if path.suffix.lower() != '.dll':
            continue
        if path.name.lower() not in OWN_DLLS:
            shipped.setdefault(path.name.lower(), []).append((relative, path))
    owners = {}
    packages = {}
    lock = ROOT / "scripts/sdk-lock.json"
    locked_packages = json.loads(lock.read_text(encoding="utf-8"))["packages"]
    for index, package in enumerate(locked_packages, 1):
        if index % 20 == 0:
            print(f"Checking SDK origins: {index}/{len(locked_packages)}", flush=True)
        cached = ROOT / ".deps/cache" / package["file"]
        if not cached.is_file() or digest(cached) != package["sha256"]:
            raise RuntimeError(f"Missing or invalid locked SDK package: {package['file']}")
        metadata = buildinfo = None
        matched = []
        with tarfile.open(cached, mode="r|*") as archive:
            for member in archive:
                name = member.name.removeprefix("./")
                if name in {".PKGINFO", ".BUILDINFO"}:
                    data = fields(archive.extractfile(member).read())
                    if name == ".PKGINFO":
                        metadata = data
                    else:
                        buildinfo = data
                elif Path(name).name.lower() in shipped and name.startswith("ucrt64/"):
                    if not member.isfile():
                        continue
                    with archive.extractfile(member) as stream:
                        sdk_hash = hashlib.file_digest(stream, "sha256").hexdigest()
                    for relative, deployed in shipped[Path(name).name.lower()]:
                        if digest(deployed) == sdk_hash or (deployed.name.lower() in custom and
                                package['file'].startswith('mingw-w64-ucrt-x86_64-'+custom[deployed.name.lower()]['name']+'-')):
                            matched.append((relative, deployed))
        if not matched:
            continue
        if not metadata or not buildinfo:
            raise RuntimeError(f"Package metadata missing: {package['file']}")
        pkgname = metadata["pkgname"][0]
        source = f"{metadata['pkgbase'][0]}-{metadata['pkgver'][0]}.src.tar.zst"
        packages[pkgname] = {
            "name": pkgname, "version": metadata["pkgver"][0],
            "license": metadata.get("license", []),
            "upstream": metadata.get("url", [""])[0],
            "binary_url": package["url"], "binary_sha256": package["sha256"],
            "source_url": SOURCE_BASE + source,
            "pkgbuild_sha256": buildinfo["pkgbuild_sha256sum"][0],
        }
        for entry in custom.values():
            if entry['name'] == metadata['pkgbase'][0].removeprefix('mingw-w64-'):
                packages[pkgname]['custom_build'] = entry
        for relative, deployed in matched:
            if relative in owners and owners[relative]["package"] != pkgname:
                raise RuntimeError(f"Ambiguous SDK origin: {relative}")
            owners[relative] = {"package": pkgname, "sha256": digest(deployed)}
    missing = [relative for paths in shipped.values()
               for relative, _ in paths if relative not in owners]
    if missing:
        raise RuntimeError(f"Unmapped runtime DLLs: {missing}")
    checked = check_source_urls(packages.values()) if check_urls else None
    manifest = {"schema": 1, "source_index": SOURCE_BASE, "source_urls_checked_at": checked,
                "sdk_lock_sha256": digest(lock), "runtime_files": dict(sorted(owners.items())),
                "packages": sorted(packages.values(), key=lambda p: p["name"])}
    (runtime / "dependency-sources.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    lines = ["# Corresponding third-party sources / Drittanbieter-Quellcode", "",
             "üxzView sources and build instructions are included in the accompanying source ZIP.",
             "The libraries below have exact MSYS2 source packages, including build recipes and patches.",
             "The smaller libheif/libavif are rebuilt by scripts/build_heif.py and scripts/build_avif.py",
             "in the accompanying uxzView source ZIP.",
             "Its source hashes, SDK lock, build options and output hash are recorded in custom-builds.json",
             "and in dependency-sources.json. Other listed libraries are unmodified MSYS2 binaries.",
             "dependency-sources.json maps each shipped DLL hash to its original binary package and source URL.",
             "Download the matching .src.tar.zst and unpack with a Zstandard-capable tar tool; see its PKGBUILD.",
             "The distributor must keep these sources accessible while distributing the binaries; archive a mirror",
             "before upstream removes versions and update these directions if hosting changes.", "",
             "Source-index availability check: " + (checked or "NOT RUN; required before release"), "",
             "| Package | Version | Corresponding source |", "| --- | --- | --- |"]
    for p in manifest["packages"]:
        lines.append(f"| {p['name']} | {p['version']} | [source archive]({p['source_url']}) |")
    tool_lock = json.loads((ROOT / 'scripts/tool-sources-lock.json').read_text())
    lines += ['', '## Bundled recognition tools', '',
              'ZXing-C++, Tesseract, Leptonica and tessdata_fast models are included under tools/.',
              'Their pinned source/model URLs and SHA-256 are recorded in scripts/tool-sources-lock.json',
              'in the source ZIP and each tool package; licenses are alongside each installed package.',
              'Rebuild using scripts/build_tool_packs.py. Compiler DLLs are mapped in the table above.', '']
    for name, entry in tool_lock['sources'].items():
        lines.append(f"- {name}: [pinned source archive]({entry['url']}); SHA-256 `{entry['sha256']}`.")
    for name, entry in tool_lock['models'].items():
        lines.append(f"- tessdata_fast {name}: [model]({entry['url']}); SHA-256 `{entry['sha256']}`.")
    (runtime / "SOURCES.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Mapped {len(owners)} DLLs to {len(packages)} source packages. URLs checked: {bool(checked)}")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--check-urls", action="store_true")
    group.add_argument("--refresh-urls", action="store_true",
                       help="Recheck source availability for an unchanged, already mapped runtime")
    args = parser.parse_args()
    if args.refresh_urls:
        refresh_urls()
    else:
        collect(args.check_urls)
