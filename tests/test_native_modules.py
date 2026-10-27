from pathlib import Path
import json
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from verify_native_modules import verify_modules, ENTRIES


class InstalledModuleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='uxzview-modules-')
        self.addCleanup(temporary.cleanup)
        self.stage = Path(temporary.name)
        self.folder = self.stage / 'lib/IxView'
        self.folder.mkdir(parents=True)
        for name in ('ixrawpreview', 'ixextensions'):
            (self.folder / (name + '.so')).touch()

    def test_missing_installed_module_is_rejected_before_loading(self):
        (self.folder / 'ixrawpreview.so').unlink()
        loader = Mock()
        with self.assertRaisesRegex(RuntimeError, 'Installed module missing'):
            verify_modules(self.stage, 'linux', loader)
        loader.assert_not_called()

    def test_unresolved_runtime_dependency_is_not_ignored(self):
        with self.assertRaisesRegex(RuntimeError, 'missing dependency'):
            verify_modules(self.stage, 'linux', Mock(side_effect=OSError('missing dependency')))

    def test_wrong_module_without_entry_point_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'ixReadRawPreviewSized'):
            verify_modules(self.stage, 'linux', lambda _: SimpleNamespace())

    def test_macos_layout_and_all_entry_points_are_checked_without_invoking_them(self):
        folder = self.stage / 'uxzView.app/Contents/PlugIns/IxView'
        folder.mkdir(parents=True)
        for path in self.folder.iterdir():
            (folder / path.name).touch()
        symbols = {name: Mock() for name in ('ixReadRawPreviewSized', 'ixOpenExtensionManager',
                                            'ixOpenFunctionExtensionManager')}
        loader = Mock(return_value=SimpleNamespace(**symbols))
        result = verify_modules(self.stage, 'darwin', loader)
        self.assertEqual(len(loader.call_args_list), 2)
        self.assertEqual(result['ixextensions']['path'],
                         'uxzView.app/Contents/PlugIns/IxView/ixextensions.so')
        for symbol in symbols.values():
            symbol.assert_not_called()

    def test_macos_uses_bundle_executable_context(self):
        result = {name: {'loaded': True, 'symbols': list(symbols)} for name, symbols in ENTRIES.items()}
        # macOS temp paths can alias /private/var through /var. Exercise a
        # noncanonical spelling on every host too, without symlink privileges.
        for stage in (self.stage, self.stage / 'lib' / '..'):
            with self.subTest(stage=stage):
                runner = Mock(return_value=subprocess.CompletedProcess([], 0, json.dumps(result), ''))
                self.assertEqual(verify_modules(stage, 'darwin', runner=runner), result)
                self.assertEqual(runner.call_args.args[0],
                                 [str(stage.resolve() / 'uxzView.app/Contents/MacOS/uxzView'), '--probe-modules'])

    def test_macos_probe_failure_or_incomplete_exports_are_rejected(self):
        for code, output in ((2, '{}'), (0, '{}')):
            with self.subTest(code=code), self.assertRaises(RuntimeError):
                verify_modules(self.stage, 'darwin',
                    runner=Mock(return_value=subprocess.CompletedProcess([], code, output, 'loader failure')))


if __name__ == '__main__':
    unittest.main()
