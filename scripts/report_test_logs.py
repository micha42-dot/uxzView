"""Print retained QtTest/CTest reports when a CI build or test step fails."""
import argparse
from pathlib import Path
import sys


def report_test_logs(build: Path, stream):
    reports = sorted(build.glob('*test-results.txt'))
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
