"""Select public project sources; exclude SDKs, local notes and generated output."""
from pathlib import Path
import fnmatch
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
POLICY = json.loads((ROOT / '.github/github-updater.json').read_text(encoding='utf-8'))
FOLDERS = tuple(POLICY['folders'])
ROOT_FILES = tuple(POLICY['files'])
PRIVATE = tuple(POLICY['exclude'])


def source_files(root=ROOT):
    for name in ROOT_FILES:
        path = root / name
        if not path.is_file():
            raise FileNotFoundError(f"Required public source missing: {name}")
        yield path
    for folder in FOLDERS:
        for path in sorted((root / folder).rglob("*")):
            relative = path.relative_to(root)
            if (not path.is_file() or path.is_symlink() or
                    any(fnmatch.fnmatchcase(relative.as_posix().casefold(), pattern.casefold())
                        for pattern in PRIVATE)):
                continue
            yield path


def documentation_files(root=ROOT):
    """Use the same privacy rules for shipped documentation and source archives."""
    for path in source_files(root):
        relative = path.relative_to(root)
        # Root image assets explicitly selected by the public policy accompany
        # README.md in both installed documentation and source archives.
        if (len(relative.parts) == 1 and (path.suffix.lower() in {
                '.md', '.png', '.jpg', '.jpeg', '.svg', '.webp', '.gif', '.bmp'} or path.name == 'LICENSE')) or (
                relative.parts[0] == 'docs' and path.suffix == '.md'):
            yield path


def create_source_archive(destination: Path, prefix="uxzView-source/"):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in source_files():
            archive.write(path, prefix + path.relative_to(ROOT).as_posix())
    return destination
