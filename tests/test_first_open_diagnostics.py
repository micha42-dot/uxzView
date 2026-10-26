import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from measure_first_open import METRICS, run_sample


class FirstOpenDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='uxzview-first-open-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.result = self.root / 'result.json'
        self.log = self.root / 'process.log'

    def run_child(self, code, timeout=5):
        return run_sample([sys.executable, '-c', code, str(self.result)],
                          self.result, self.log, os.environ.copy(), timeout)

    def test_valid_results_are_retained(self):
        payload = json.dumps({'samples': [dict.fromkeys(METRICS, 1.0)]})
        entry = self.run_child('import pathlib,sys; pathlib.Path(sys.argv[1]).write_text(' + repr(payload) + ')')
        self.assertTrue(entry['success'])
        self.assertEqual(entry['samples'][0]['paint_completed_ms'], 1.0)

    def test_success_without_new_report_cannot_reuse_stale_results(self):
        self.result.write_text(json.dumps({'samples': [dict.fromkeys(METRICS, 1.0)]}))
        entry = self.run_child('pass')
        self.assertFalse(entry['success'])
        self.assertFalse(self.result.exists())

    def test_failed_process_preserves_partial_report_and_checkpoint(self):
        entry = self.run_child("import pathlib,sys; pathlib.Path(sys.argv[1]).write_text('{partial'); "
                               "print('ix-first-open create-window', flush=True); sys.exit(3)")
        self.assertFalse(entry['success'])
        self.assertEqual(entry['returncode'], 3)
        self.assertEqual(self.result.read_text(), '{partial')
        self.assertEqual(entry['last_checkpoint'], 'ix-first-open create-window')

    def test_timeout_is_reported_and_log_is_retained(self):
        entry = self.run_child("import time; print('ix-load worker started', flush=True); time.sleep(10)", 0.5)
        self.assertFalse(entry['success'])
        self.assertIn('timed out', entry['error'])
        self.assertTrue(self.log.is_file())
        # A heavily loaded runner can time out before Python starts. Preserve
        # that distinction instead of inventing a checkpoint or adding waits.
        if 'ix-load worker started' in self.log.read_text():
            self.assertEqual(entry['last_checkpoint'], 'ix-load worker started')
        else:
            self.assertIsNone(entry['last_checkpoint'])

    def test_invalid_metrics_are_rejected(self):
        payload = json.dumps({'samples': [dict.fromkeys(METRICS, -1)]})
        entry = self.run_child('import pathlib,sys; pathlib.Path(sys.argv[1]).write_text(' + repr(payload) + ')')
        self.assertFalse(entry['success'])
        self.assertIn('metric', entry['error'])


if __name__ == '__main__':
    unittest.main()
