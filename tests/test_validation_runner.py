import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import validate


class ValidationRunnerTests(unittest.TestCase):
    def setUp(self):
        output = Path(__file__).resolve().parent / 'output'
        output.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='run-validator-', dir=output)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').touch()
        self.addCleanup(patch.stopall)
        patch.object(validate, 'ROOT', self.root).start()
        patch.object(validate.shutil, 'which', return_value='ctest').start()
        patch.object(sys, 'argv', ['validate.py', '--suite', 'all', '--require-assets']).start()
        patch.object(sys, 'platform', 'win32').start()
        patch.object(validate.platform, 'platform', return_value='Test Windows').start()

    def report(self):
        return json.loads((self.build / 'validation-summary.json').read_text())

    def test_failure_stops_later_suites_and_replaces_old_success(self):
        (self.build / 'validation-summary.json').write_text('{"success":true}')
        with patch.object(validate.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)) as run:
            self.assertEqual(validate.main(), 1)
        self.assertEqual(run.call_count, 1)
        self.assertFalse(self.report()['success'])
        self.assertEqual(self.report()['steps'][0]['returncode'], 1)

    def test_timeout_is_reported_as_failure(self):
        with patch.object(validate.subprocess, 'run', side_effect=subprocess.TimeoutExpired('fixture', 300)):
            self.assertEqual(validate.main(), 1)
        self.assertFalse(self.report()['success'])
        self.assertIn('timed out', self.report()['error'])

    def test_native_plan_uses_required_assets_and_explicit_scales(self):
        with patch.dict(validate.os.environ, {'QT_QPA_PLATFORM': 'offscreen', 'QT_SCALE_FACTOR': '3'}), \
                patch.object(validate.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertEqual(validate.main(), 0)
        self.assertTrue(self.report()['success'])
        calls = run.call_args_list
        self.assertEqual(len(self.report()['steps']), len(calls))
        self.assertNotIn('QT_QPA_PLATFORM', calls[0].kwargs['env'])
        self.assertNotIn('QT_SCALE_FACTOR', calls[0].kwargs['env'])
        for call in calls:
            self.assertEqual(call.kwargs['env']['IX_REQUIRE_RAW'], '1')
            self.assertEqual(call.kwargs['env']['IX_REQUIRE_TOOLS'], '1')
        native = [call for call in calls if '-platform' in call.args[0]]
        scales = {call.kwargs['env'].get('QT_SCALE_FACTOR') for call in native}
        self.assertEqual(scales, {None, '1', '1.25', '1.5', '2'})
        for call in native:
            self.assertIn('windows', call.args[0])
            self.assertTrue(any(arg.endswith(',junitxml') for arg in call.args[0]))


if __name__ == '__main__':
    unittest.main()
