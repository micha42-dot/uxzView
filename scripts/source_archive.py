"""Select public project sources; exclude SDKs, local notes and generated output."""
from pathlib import Path
from contextlib import contextmanager
import fnmatch
import json
import os
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
POLICY = json.loads((ROOT / '.github/github-updater.json').read_text(encoding='utf-8'))
FOLDERS = tuple(POLICY['folders'])
ROOT_FILES = tuple(POLICY['files'])
PRIVATE = tuple(POLICY['exclude'])


@contextmanager
def staged_packages(destination):
    """Finish archives and checksum files before replacing the published copies."""
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.package-', dir=destination) as temporary:
        stage = Path(temporary)
        yield stage
        # Only publication needs serialization. Keep one inode for all writers;
        # the operating system releases its lock when the descriptor closes.
        with (destination / '.package.lock').open('a+b') as lock:
            if os.name == 'nt':
                import msvcrt
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX)
            _publish_packages(stage, destination)


def _publish_packages(stage, destination):
    assets = list(stage.iterdir())
    previous = stage / 'previous'
    previous.mkdir()
    for asset in assets:
        target = destination / asset.name
        if target.exists():
            backup = previous / asset.name
            try:
                backup.hardlink_to(target)
            except OSError:
                shutil.copy2(target, backup)
    published = []
    try:
        for asset in assets:
            target = destination / asset.name
            asset.replace(target)
            published.append(target)
    except OSError:
        for target in reversed(published):
            backup = previous / target.name
            if backup.exists():
                backup.replace(target)
            else:
                target.unlink()
        raise


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
