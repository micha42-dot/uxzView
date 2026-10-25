"""Inspect generated .reg files; never import them or change registry defaults."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'nt', 'Windows PowerShell registry-file generator')
class WindowsRegistrationTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='registration-test-', dir=ROOT / 'build')
        self.folder = Path(self.temporary.name)

    def tearDown(self):
        assert self.folder.resolve().is_relative_to((ROOT / 'build').resolve())
        self.temporary.cleanup()

    def generate(self, *arguments):
        output = self.folder / 'IxView.reg'
        subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'scripts/windows/register-windows.ps1'), '-GenerateOnly',
                        '-RegFilePath', str(output), *arguments], check=True, capture_output=True)
        return output.read_text(encoding='utf-16')

    def test_quoted_unicode_paths_and_per_user_registration(self):
        folder = self.folder / 'Bilder & Fotos 日本語'
        folder.mkdir()
        executable = folder / 'uxzView.exe'
        executable.write_bytes(b'fixture, not executable')
        text = self.generate('-ExecutablePath', str(executable))
        self.assertIn('Software\\RegisteredApplications', text)
        self.assertIn('Capabilities\\FileAssociations', text)
        self.assertIn('Applications\\uxzView.exe', text)
        self.assertIn('"FriendlyAppName"="üxzView"', text)
        self.assertIn('"ApplicationName"="üxzView"', text)
        self.assertIn('\\"%1\\"', text)
        self.assertIn(str(executable).replace('\\', '\\\\'), text)
        self.assertNotIn('UserChoice', text)
        self.assertNotIn('HKEY_LOCAL_MACHINE', text)
        self.assertIn('"IxView.Image"=hex(0):', text)

    def test_unregister_only_removes_ixview_registration(self):
        text = self.generate('-Unregister')
        self.assertIn('[-HKEY_CURRENT_USER\\Software\\Classes\\IxView.Image]', text)
        self.assertIn('"IxView"=-', text)
        self.assertIn('"IxView.Image"=-', text)
        self.assertNotIn('UserChoice', text)
        self.assertNotIn('[-HKEY_CURRENT_USER\\Software\\Classes\\.jpg]', text)


if __name__ == '__main__':
    unittest.main()
