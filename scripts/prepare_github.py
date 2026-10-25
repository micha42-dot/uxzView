"""Audit and export the public repository without local SDKs or generated files."""
import hashlib
import json
from pathlib import Path
import re
from source_archive import ROOT, source_files


def audit():
    files = list(source_files())
    forbidden = re.compile(rb"(?:ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)")
    private_paths = [str(ROOT).encode(), str(ROOT).replace("\\", "/").encode()]
    manifest = {}
    for file in files:
        relative = file.relative_to(ROOT).as_posix()
        data = file.read_bytes()
        if (file.suffix.lower() in {".exe", ".dll", ".zip", ".pyc", ".pyo", ".part"}
                or file.name.startswith(".env") or forbidden.search(data)
                or any(p.lower() in data.lower() for p in private_paths)):
            raise RuntimeError(f"Review non-public content before exporting: {relative}")
        manifest[relative] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    return manifest


if __name__ == "__main__":
    manifest = audit()
    destination = ROOT / "build/github-manifest.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Public repository audit: {len(manifest)} files; report: {destination}")
    print("Use the Git working tree for GitHub; the common source ZIP is dist/uxzView-source.zip.")
