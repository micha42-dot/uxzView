"""Enumerate distributable runtime files without personal portable profiles."""
import os
from pathlib import Path
import shutil


def private_part(name):
    name = name.casefold()
    return (name in {'ixview-data', 'portable.mode', '.ixview-portable.lock'}
            or name.startswith('.ixview-profile-'))


def runtime_files(root):
    root = Path(root)
    for folder, directories, files in os.walk(root, followlinks=False):
        directories[:] = sorted(name for name in directories if not private_part(name))
        if Path(folder) == root:
            directories[:] = [name for name in directories if name.casefold() != 'tools']
        for name in sorted(files):
            if not private_part(name):
                yield Path(folder) / name


def release_files(root):
    """Use verified bundle defaults, never a local user's installed-tool state."""
    from bundle_tools import release_tools
    root = Path(root)
    yield from ((path.relative_to(root).as_posix(), path) for path in runtime_files(root))
    yield from release_tools(root)


def clean_runtime(root, workspace):
    root, workspace = Path(root), Path(workspace)
    if root.is_symlink() or not root.resolve().is_relative_to(workspace.resolve()) or root.resolve() == workspace.resolve():
        raise RuntimeError('Deployment target must stay inside the project')
    if root.exists():
        for path in root.iterdir():
            if private_part(path.name) or path.name.casefold() == 'tools':
                continue
            if path.is_dir() and not path.is_symlink():
                if not path.resolve().is_relative_to(root.resolve()):
                    raise RuntimeError('Runtime directory resolves outside deployment: ' + str(path))
                shutil.rmtree(path)
            else:
                path.unlink()
    root.mkdir(parents=True, exist_ok=True)
