"""Print retained configure/build and QtTest/CTest reports after a CI failure."""
import argparse
from pathlib import Path
import sys


def first_failure(lines):
    """Return the first FAIL!/QFATAL line, or None.

    A suite that dies mid-run has no Totals: line, and its last checkpoint is
    an ordinary PASS. Reporting only that made a crashed suite look healthy,
    which is exactly the wrong impression for CI triage.
    """
    return next((line for line in lines if line.startswith(('FAIL!', 'QFATAL'))), None)


def last_checkpoint(lines):
    """Return the last progress line: where a hang or crash stopped."""
    return next((line for line in reversed(lines)
                 if line.startswith(('INFO ', 'PASS ', 'FAIL!', 'QFATAL', 'Totals:'))),
                'No test checkpoint recorded.')


def report_test_logs(build: Path, stream):
    suites = sorted(build.glob('*test-results.txt'))
    if suites:
        print('\n===== Last recorded suite checkpoints =====', file=stream, flush=True)
        for path in suites:
            lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
            failure = first_failure(lines)
            if failure:
                print(f'{path.name}: FAILED {failure}', file=stream, flush=True)
            print(f'{path.name}: {last_checkpoint(lines)}', file=stream, flush=True)
    reports = [build / name for name in ('configure.log', 'build.log', 'install.log', 'package.log',
                                       'installed-version.log', 'installed-image-probe.log',
                                       'installed-modules.log')
               if (build / name).is_file()]
    reports.extend(suites)
    ctest = build / 'Testing/Temporary/LastTest.log'
    if ctest.is_file():
        reports.append(ctest)
    for path in reports:
        print(f'\n===== {path.as_posix()} =====', file=stream, flush=True)
        print(path.read_text(encoding='utf-8', errors='replace'), file=stream, flush=True)
    if not reports:
        print(f'No test reports found in {build.as_posix()}.', file=stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    args = parser.parse_args()
    sys.stdout.reconfigure(errors='replace')
    report_test_logs(args.build_dir, sys.stdout)


if __name__ == '__main__':
    main()
