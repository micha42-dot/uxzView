"""Select public project sources; exclude SDKs, local notes and generated output."""
from pathlib import Path
import zipfile
from runtime_files import private_part

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = ("src", "resources", "reference", "scripts", "docs", "tests", "cmake", ".github")
ROOT_FILES = ("CMakeLists.txt", "CMakePresets.json", ".clang-format", ".gitignore",
              ".gitattributes", ".editorconfig", "LICENSE", "README.md", "PROJECT_STATUS.md",
              "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md")
PRIVATE = {"docs/REQUIREMENTS.de.txt", "docs/WORK_IN_PROGRESS.md"}


def source_files(root=ROOT):
    for name in ROOT_FILES:
        path = root / name
        if not path.is_file():
            raise FileNotFoundError(f"Required public source missing: {name}")
        yield path
    for folder in FOLDERS:
        for path in sorted((root / folder).rglob("*")):
            relative = path.relative_to(root)
            if (not path.is_file() or path.is_symlink() or relative.as_posix() in PRIVATE
                    or relative.parts[:2] == ("tests", "output")
                    or any(p in {"__pycache__", ".git"} or p.startswith("run-") or private_part(p) for p in relative.parts)
                    or path.suffix in {".pyc", ".pyo", ".part"}):
                continue
            yield path


def documentation_files(root=ROOT):
    """Use the same privacy rules for shipped documentation and source archives."""
    for path in source_files(root):
        relative = path.relative_to(root)
        if (len(relative.parts) == 1 and (path.suffix == '.md' or path.name == 'LICENSE')) or (
                relative.parts[0] == 'docs' and path.suffix == '.md'):
            yield path


def create_source_archive(destination: Path, prefix="uxzView-source/"):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in source_files():
            archive.write(path, prefix + path.relative_to(ROOT).as_posix())
    return destination
