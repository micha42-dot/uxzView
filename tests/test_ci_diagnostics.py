import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from report_test_logs import report_test_logs


class CiDiagnosticsTests(unittest.TestCase):
    def test_timeout_checkpoint_is_printed_before_long_build_output(self):
        with tempfile.TemporaryDirectory(prefix='uxzview-ci-') as directory:
            build = Path(directory)
            (build / 'build.log').write_text('compiler output\n', encoding='utf-8')
            (build / 'workflow-test-results.txt').write_text(
                'PASS   : WorkflowTests::previousCase()\n'
                'INFO   : WorkflowTests::pendingDialog() entering\n'
                'QWARN  : WorkflowTests::pendingDialog() layout warning\n', encoding='utf-8')
            output = io.StringIO()
            report_test_logs(build, output)
            text = output.getvalue()
            self.assertIn('workflow-test-results.txt: INFO   : WorkflowTests::pendingDialog() entering', text)
            self.assertLess(text.index('pendingDialog() entering'), text.index('compiler output'))
            self.assertIn('layout warning', text)

    def test_all_suites_and_partial_crash_reports_are_printed(self):
        with tempfile.TemporaryDirectory(prefix='uxzview-ci-') as directory:
            build = Path(directory)
            names = ('configure.log', 'build.log', 'install.log', 'package.log',
                     'installed-version.log', 'installed-image-probe.log',
                     'installed-modules.log',
                     'test-results.txt', 'workflow-test-results.txt',
                     'quality-test-results.txt', 'storage-test-results.txt',
                     'extension-test-results.txt', 'Testing/Temporary/LastTest.log')
            for name in names:
                path = build / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(('failure in ' + name + '\n').encode() + b'partial \xff')
            (build / 'private.ini').write_text('personal state')
            output = io.StringIO()
            report_test_logs(build, output)
            text = output.getvalue()
            for name in names:
                self.assertIn('failure in ' + name, text)
            self.assertEqual(text.count('partial \ufffd'), len(names))
            self.assertNotIn('personal state', text)

    def test_failure_before_build_creates_no_reports_or_directories(self):
        with tempfile.TemporaryDirectory(prefix='uxzview-ci-') as directory:
            build = Path(directory) / 'not-configured'
            output = io.StringIO()
            report_test_logs(build, output)
            self.assertIn('No test reports found', output.getvalue())
            self.assertFalse(build.exists())


if __name__ == '__main__':
    unittest.main()
