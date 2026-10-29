"""Build the single Windows/source package pair directly in dist; --release validates source origins."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
import shutil
import zipfile
from prepare_github import audit
from source_archive import ROOT, create_source_archive
from runtime_files import release_files
from dependency_sources import OWN_DLLS


def validate_own_runtime(runtime):
    for name in ('uxzView.exe', 'ixrawpreview.dll', 'ixextensions.dll', 'imageformats/ixarchive.dll'):
        built = ROOT / 'build' / name
        deployed = runtime / name
        if name == 'uxzView.exe' and not built.is_file():
            raise RuntimeError('Build IxView before packaging')
        if built.is_file() != deployed.is_file():
            raise RuntimeError(f'Runtime/build artifact set differs: {name}; run deploy.py again')
        if built.is_file():
            with built.open('rb') as source, deployed.open('rb') as target:
                if hashlib.file_digest(source, 'sha256').digest() != hashlib.file_digest(target, 'sha256').digest():
                    raise RuntimeError(f'Runtime differs from current build: {name}; run deploy.py again')


def validate_runtime_inventory(runtime):
    validate_own_runtime(runtime)
    dependencies = json.loads((runtime / "dependency-sources.json").read_text(encoding="utf-8"))
    if json.loads((runtime / "runtime-manifest.json").read_text())["unresolved"]:
        raise RuntimeError("Unresolved runtime dependencies")
    files = dict(release_files(runtime))
    actual = {relative for relative, p in files.items()
              if p.suffix.lower() == '.dll' and p.name.lower() not in OWN_DLLS}
    if actual != set(dependencies["runtime_files"]):
        raise RuntimeError("DLL set changed after source inventory")
    with (ROOT / "scripts/sdk-lock.json").open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != dependencies["sdk_lock_sha256"]:
            raise RuntimeError("SDK lock changed after source inventory")
    for relative, entry in dependencies["runtime_files"].items():
        with files[relative].open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != entry["sha256"]:
                raise RuntimeError(f"Runtime changed after source inventory: {relative}")
    for package in dependencies['packages']:
        custom = package.get('custom_build')
        if custom:
            recipe = ROOT / custom['recipe']
            with recipe.open('rb') as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest() != custom['recipe_sha256']:
                    raise RuntimeError('Custom dependency build recipe changed after deployment')
            with (ROOT/custom['builder']).open('rb') as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest() != custom['builder_sha256']:
                    raise RuntimeError('Custom dependency builder changed after deployment')
    return dependencies


def validate_release(runtime):
    dependencies = validate_runtime_inventory(runtime)
    checked = dependencies.get("source_urls_checked_at")
    if not checked or not 0 <= (datetime.now(timezone.utc) - datetime.fromisoformat(checked)).total_seconds() < 86400:
        raise RuntimeError("Run dependency_sources.py --refresh-urls (unchanged runtime) or --check-urls; source availability check older than 24 hours")


def package(release=False):
    audit()
    version = re.search(r"project\(IxView VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    runtime = ROOT / "dist/uxzView"
    if not (runtime / "uxzView.exe").is_file():
        raise RuntimeError("Run deploy.py before packaging")
    files = list(release_files(runtime))
    runtime_bytes = sum(path.stat().st_size for _, path in files)
    viewer_bytes = sum(path.stat().st_size for relative, path in files if not relative.startswith("tools/"))
    tool_bytes = runtime_bytes - viewer_bytes
    if viewer_bytes > 160_000_000 or tool_bytes > 25_000_000:
        raise RuntimeError(f'Windows size budget exceeded (viewer 160 MB, tools 25 MB): {viewer_bytes}, {tool_bytes} bytes')
    if release:
        validate_release(runtime)
    else:
        validate_own_runtime(runtime)
    destination = ROOT / "dist"
    source = create_source_archive(destination / "uxzView-source.zip")
    binary = destination / "uxzView-win64.zip"
    with zipfile.ZipFile(binary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for relative, path in sorted(files):
            archive.write(path, "uxzView/" + relative)
        archive.write(source, source.name)
    assets = [binary, source]
    if release:
        for name in ["SOURCES.md", "dependency-sources.json"]:
            target = destination / name
            shutil.copy2(runtime / name, target)
            assets.append(target)
    sums = []
    manifest = {}
    for path in assets:
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        sums.append(f"{digest}  {path.name}")
        manifest[path.name] = {"bytes": path.stat().st_size, "sha256": digest}
    (destination / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (destination / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
    if not release:
        print(f"Packages: {binary.name}, {source.name}; version {version}")
        return
    notes = f"""# üxzView {version}

An image viewer inspired by IrfanView and nomacs. Previously named IxView; existing profiles remain compatible.

## Downloads

- **Windows x64:** `uxzView-win64.zip`. Ready-to-use runtime: {runtime_bytes/1e6:.2f} MB, including {tool_bytes/1e6:.2f} MB of offline recognition tools and five OCR languages. Targets Windows 10 version 1809 and newer; tested locally on Windows 11. This development build is unsigned.
- **Source:** `uxzView-source.zip`, with Windows/Linux/macOS build instructions in `docs/PLATFORMS.md`. Prebuilt releases currently target Windows only.
- **Third-party sources:** `SOURCES.md` links the corresponding source packages; `dependency-sources.json` records DLL hashes, package versions and custom codec builds.
- **Included tools:** QR/barcode reading and Tesseract OCR are ready to use from `uxzView/tools`, with English, German, French, Japanese and Korean. All tools/languages remain removable. Separate `.ixtool` assets support reinstallation.
- **Integrity:** `SHA256SUMS.txt` covers the four base download files; `uxzView-tools-SHA256SUMS.txt` covers the optional tool assets.

## Changes and validation

See `CHANGELOG.md` for the included changes. `docs/PERFORMANCE.md` records measured performance changes and their scope; `docs/QUALITY.md` and the review reports record test evidence and remaining gaps. Packaging validates runtime/source integrity; it does not itself establish successful tests on every supported platform.

Native Windows 10, Linux and macOS validation remains outstanding. Complete camera metadata preservation, calibrated monitor profiles and broad desktop/DPI testing are not established. Format availability does not guarantee every variant. GPL-3.0-or-later; third-party libraries retain their licenses.
"""
    (destination / "release-notes.md").write_text(notes, encoding="utf-8")
    print(f"Windows release assets: {destination}; version {version}; runtime {runtime_bytes/1e6:.2f} MB")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", action="store_true", help="Validate runtime/source origins and generate release notes")
    package(parser.parse_args().release)
