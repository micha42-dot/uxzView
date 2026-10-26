"""Run first-image diagnostics in fresh, bounded processes and retain failures."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
METRICS = ('open_return_ms', 'document_install_ms', 'paint_completed_ms', 'max_gui_heartbeat_gap_ms')


def run_sample(command, result_path, log_path, env, timeout=40):
    result_path.unlink(missing_ok=True)
    entry = {'command': [str(item) for item in command], 'report': str(result_path),
             'log': str(log_path), 'success': False}
    started = time.monotonic()
    try:
        with log_path.open('w', encoding='utf-8') as log:
            process = subprocess.run(entry['command'], stdout=log, stderr=subprocess.STDOUT,
                                     env=env, cwd=ROOT, timeout=timeout)
        entry['returncode'] = process.returncode
        if process.returncode:
            raise RuntimeError(f'Benchmark exited with code {process.returncode}')
        data = json.loads(result_path.read_text(encoding='utf-8'))
        if not isinstance(data, dict) or data.get('error'):
            raise RuntimeError(str(data.get('error', 'Invalid benchmark report')) if isinstance(data, dict)
                               else 'Invalid benchmark report')
        samples = data.get('samples')
        if not isinstance(samples, list) or not samples:
            raise RuntimeError('Benchmark produced no timing samples')
        for sample in samples:
            for metric in METRICS:
                value = sample.get(metric) if isinstance(sample, dict) else None
                if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                    raise RuntimeError(f'Invalid or missing metric: {metric}')
        entry['samples'] = samples
        entry['success'] = True
    except subprocess.TimeoutExpired:
        entry['error'] = f'Benchmark timed out after {timeout} seconds'
    except (OSError, ValueError, RuntimeError) as error:
        entry['error'] = str(error)
    finally:
        entry['seconds'] = round(time.monotonic() - started, 3)
        if log_path.is_file():
            lines = log_path.read_text(encoding='utf-8', errors='replace').splitlines()
            checkpoints = [line for line in lines if 'ix-first-open ' in line or 'ix-load ' in line]
            entry['last_checkpoint'] = checkpoints[-1] if checkpoints else None
    return entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--image', type=Path, action='append')
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--platform', default='offscreen')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.samples <= 10:
        parser.error('--samples must be between 1 and 10')
    build = args.build_dir.resolve()
    executable = build / ('ixview_benchmarks.exe' if os.name == 'nt' else 'ixview_benchmarks')
    images = [path.resolve() for path in (args.image or [ROOT / 'tests/fixtures/smoke.png', ROOT / 'example.jpg'])]
    if not executable.is_file() or any(not path.is_file() for path in images):
        parser.error('Build ixview_benchmarks and provide existing image files')
    output = (args.output or build / 'first-open-diagnostics.json').resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    runs = Path(tempfile.mkdtemp(prefix=output.stem + '-', dir=output.parent))
    env = os.environ.copy()
    sdk = ROOT / '.deps/sdk/ucrt64/bin'
    if os.name == 'nt' and sdk.is_dir():
        env['PATH'] = str(sdk) + os.pathsep + env.get('PATH', '')
    env.update(IX_PROFILE_IMAGE_LOAD='1', QT_FORCE_STDERR_LOGGING='1')
    report = {'success': False, 'platform': args.platform, 'runs': []}
    for image_index, image in enumerate(images):
        for sample in range(args.samples):
            result = runs / f'{image_index}-{sample}.json'
            log = runs / f'{image_index}-{sample}.log'
            command = [executable, result, '--profile-first-open', image, '-platform', args.platform]
            entry = run_sample(command, result, log, env)
            report['runs'].append(entry)
            output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            if not entry['success']:
                print(f"First-open diagnosis failed: {entry['error']}; last stage: {entry.get('last_checkpoint')}")
                print(f'Logs and partial results retained in {runs}')
                return 1
    report['success'] = True
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'{len(report["runs"])} fresh benchmark processes passed; report: {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
