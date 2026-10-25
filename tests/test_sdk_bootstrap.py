"""Offline regression checks for the SDK installer; no downloaded code is run."""
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from extract_sdk import extract_sdk


class SdkBootstrapTests(unittest.TestCase):
    def setUp(self):
        scratch_parent = Path(__file__).resolve().parents[1] / "build"
        scratch_parent.mkdir(exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="sdk-test-", dir=scratch_parent)
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.assertTrue(self.root.resolve().is_relative_to(scratch_parent.resolve()))
        (self.root / "scripts").mkdir()
        (self.root / ".deps/cache").mkdir(parents=True)
        self.packages = []

    def add_package(self, name="test.pkg.tar.gz", files=None, link=None):
        target = self.root / ".deps/cache" / name
        with tarfile.open(target, "w:gz") as archive:
            for path, contents in (files or {"ucrt64/bin/example.dll": b"correct"}).items():
                member = tarfile.TarInfo(path)
                member.size = len(contents)
                archive.addfile(member, io.BytesIO(contents))
            if link:
                member = tarfile.TarInfo("ucrt64/bin/alias.dll")
                member.type = tarfile.LNKTYPE
                member.linkname = link
                archive.addfile(member)
        self.packages.append({"file": name, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
        (self.root / "scripts/sdk-lock.json").write_text(json.dumps({"packages": self.packages}), encoding="utf-8")
        return target

    def install(self):
        with redirect_stdout(io.StringIO()):
            extract_sdk(self.root)

    def test_install_repairs_equal_length_corruption_without_rewriting_good_files(self):
        self.add_package(files={"ucrt64/bin/example.dll": b"correct", "ucrt64/bin/keep.dll": b"keep"})
        self.install()
        target = self.root / ".deps/sdk/ucrt64/bin/example.dll"
        good = target.with_name("keep.dll")
        original_time = good.stat().st_mtime_ns
        target.write_bytes(b"corrupt")
        self.install()
        self.assertEqual(target.read_bytes(), b"correct")
        self.assertEqual(good.stat().st_mtime_ns, original_time)

    def test_corrupt_later_package_does_not_write_any_sdk_files(self):
        self.add_package("first.pkg.tar.gz")
        second = self.add_package("second.pkg.tar.gz")
        second.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.install()
        self.assertFalse((self.root / ".deps/sdk").exists())

    def test_unlocked_cache_package_is_ignored(self):
        self.add_package()
        (self.root / ".deps/cache/extra.pkg.tar.gz").write_bytes(b"not an archive")
        self.install()
        self.assertEqual(json.loads((self.root / ".deps/packages.json").read_text()), self.packages)

    def test_parent_traversal_is_rejected(self):
        self.add_package(files={"ucrt64/../outside": b"bad"})
        with self.assertRaisesRegex(ValueError, "Invalid SDK member path"):
            self.install()
        self.assertFalse((self.root / ".deps/sdk/outside").exists())

    def test_archive_links_are_materialized_as_regular_files(self):
        self.add_package(link="ucrt64/bin/example.dll")
        self.install()
        alias = self.root / ".deps/sdk/ucrt64/bin/alias.dll"
        self.assertFalse(alias.is_symlink())
        self.assertEqual(alias.read_bytes(), b"correct")


if __name__ == "__main__":
    unittest.main()
