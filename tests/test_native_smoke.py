"""Execute the installed-runtime CI shell with isolated app/display stand-ins."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]


def smoke_script(workflow):
    step = workflow.split('      - name: Installed runtime smoke check\n', 1)[1]
    return textwrap.dedent(step.split('        run: |\n', 1)[1].split('      - name:', 1)[0])


def workflow_script():
    return smoke_script((ROOT / '.github/workflows/desktop.yml').read_text(encoding='utf-8'))


class InstalledRuntimeSmokeTests(unittest.TestCase):
    def test_backend_display_isolation_and_failure_reports(self):
        if os.name == 'nt':
            bash = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/bin/bash.exe'
            if not bash.is_file():
                self.skipTest('Git Bash is required for the Unix CI shell check on Windows')
        else:
            bash = shutil.which('bash')
            self.assertIsNotNone(bash, 'Unix build checks require Bash')
        script = 'set -e -o pipefail\n' + workflow_script()
        cases = [('macOS', '', 0), ('Linux', '', 0), ('macOS', 'version', 134),
                 ('Linux', 'version', 134), ('Linux', 'probe', 2),
                 ('macOS', 'modules', 7), ('Linux', 'modules', 7), ('Linux', 'json', 1)]
        for platform, failure, expected in cases:
            with self.subTest(platform=platform, failure=failure), \
                    tempfile.TemporaryDirectory(prefix='uxzview-smoke-') as temporary:
                stage = Path(temporary)
                (stage / 'build/native').mkdir(parents=True)
                (stage / 'bin').mkdir()

                def executable(relative, body):
                    path = stage / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(body, encoding='utf-8', newline='\n')
                    path.chmod(0o755)

                (stage / 'step.sh').write_text(script, encoding='utf-8', newline='\n')
                app = ('dist/native/uxzView.app/Contents/MacOS/uxzView' if platform == 'macOS'
                       else 'dist/native/bin/uxzView')
                executable(app, '''#!/bin/bash
set -e
[[ "$QT_QPA_PLATFORM" == "$EXPECTED_QPA" ]] || exit 90
for key in QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH LD_LIBRARY_PATH DYLD_LIBRARY_PATH QML2_IMPORT_PATH; do
    [[ -z "${!key+x}" ]] || exit 91
done
if [[ "$RUNNER_OS" == Linux ]]; then [[ "$DISPLAY" == :smoke-fixture ]] || exit 92; fi
case "$1" in
--version)
    if [[ "$FAKE_FAILURE" == version ]]; then echo 'version failure' >&2; exit 134; fi
    echo 'uxzView smoke';;
--probe-image)
    if [[ "$FAKE_FAILURE" == probe ]]; then echo 'probe failure' >&2; exit 2; fi
    if [[ "$FAKE_FAILURE" == json ]]; then echo '{}'; else
      echo '{"width":4,"height":3,"pages":1,"format":"png"}'
    fi;;
*) exit 93;;
esac
''')
                executable('bin/xvfb-run', '''#!/bin/bash
[[ "$1" == -a ]] || exit 94
echo display >> build/native/display-calls.txt
shift
DISPLAY=:smoke-fixture exec "$@"
''')
                executable('bin/desktop-file-validate', '#!/bin/sh\nexit 0\n')
                executable('bin/python3', '''#!/bin/bash
if [[ "$1" == scripts/verify_native_modules.py ]]; then
    [[ "$2" == --stage && "$3" == dist/native ]] || exit 95
    if [[ "$FAKE_FAILURE" == modules ]]; then echo 'module failure' >&2; exit 7; fi
    echo 'module probe passed'
else
    exec ''' + shlex.quote(Path(sys.executable).as_posix()) + ''' "$@"
fi
''')
                env = os.environ | {'RUNNER_OS': platform, 'FAKE_FAILURE': failure,
                    'EXPECTED_QPA': 'cocoa' if platform == 'macOS' else 'xcb',
                    'PATH': str(stage / 'bin') + os.pathsep + os.environ['PATH']}
                for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'LD_LIBRARY_PATH',
                            'DYLD_LIBRARY_PATH', 'QML2_IMPORT_PATH'):
                    env[key] = 'must-be-removed'
                result = subprocess.run([str(bash), 'step.sh'], cwd=stage, env=env,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                logs = stage / 'build/native'
                self.assertIn('version failure' if failure == 'version' else 'uxzView smoke',
                              (logs / 'installed-version.log').read_text())
                calls = logs / 'display-calls.txt'
                if platform == 'Linux':
                    self.assertEqual(len(calls.read_text().splitlines()), 1 if failure == 'version' else 2)
                else:
                    self.assertFalse(calls.exists())
                if failure == 'version':
                    self.assertFalse((logs / 'installed-image-probe.json').exists())
                    continue
                probe = (logs / 'installed-image-probe.log').read_text()
                if failure == 'probe':
                    self.assertIn('probe failure', probe)
                else:
                    self.assertFalse(probe)
                modules = logs / 'installed-modules.log'
                if failure in ('probe', 'json'):
                    self.assertFalse(modules.exists())
                elif failure == 'modules':
                    self.assertIn('module failure', modules.read_text())
                else:
                    self.assertIn('module probe passed', modules.read_text())
                    self.assertEqual(json.loads((logs / 'installed-image-probe.json').read_text()),
                                     {'width': 4, 'height': 3, 'pages': 1, 'format': 'png'})


if __name__ == '__main__':
    unittest.main()
