"""Extract only hash-verified locked SDK packages, repairing changed SDK files."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def same_contents(archive, member, target):
    if not target.is_file() or (member.isfile() and target.stat().st_size != member.size):
        return False
    with archive.extractfile(member) as source, target.open("rb") as installed:
        while block := source.read(1024 * 1024):
            if installed.read(len(block)) != block:
                return False
        return not installed.read(1)


def extract_sdk(root=ROOT):
    root = Path(root).resolve()
    cache, sdk = root / ".deps/cache", root / ".deps/sdk"
    locked = json.loads((root / "scripts/sdk-lock.json").read_text(encoding="utf-8"))["packages"]
    manifest = []
    # Check the complete input before writing any SDK file. This also protects
    # direct invocations, which do not pass through bootstrap_sdk.py.
    for package in locked:
        name = package["file"]
        if Path(name).name != name or "\\" in name or ":" in name:
            raise ValueError(f"Invalid SDK package filename: {name}")
        with (cache / name).open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != package["sha256"]:
            raise ValueError(f"SHA-256 mismatch: {name}")
        manifest.append({"file": name, "sha256": digest})
    if not sdk.resolve().is_relative_to(root):
        raise ValueError("SDK target must stay inside the project")
    sdk.mkdir(parents=True, exist_ok=True)
    resolved_sdk = sdk.resolve()
    for index, package in enumerate(locked, 1):
        print(f"{index}/{len(locked)} {package['file']}", flush=True)
        with tarfile.open(cache / package["file"]) as archive:
            links = []
            for member in archive:
                name = member.name.removeprefix("./")
                path = PurePosixPath(name)
                if not name.startswith("ucrt64/"):
                    continue
                if ".." in path.parts or "\\" in name or ":" in name:
                    raise ValueError(f"Invalid SDK member path: {name}")
                target = sdk.joinpath(*path.parts)
                if not target.resolve().is_relative_to(resolved_sdk):
                    raise ValueError(f"SDK member escapes installation: {name}")
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    if same_contents(archive, member, target):
                        continue
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
                elif member.issym() or member.islnk():
                    links.append((member, target))
            for member, target in links:
                # Materialize archive links as files; no Windows symlink rights needed.
                if same_contents(archive, member, target):
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
    (root / ".deps/packages.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("SDK ready", flush=True)


if __name__ == "__main__":
    extract_sdk()
