"""Load installed optional modules and check their entry points without opening UI.

Run after removing SDK library environment variables. Native desktop CI does so
before calling this script; loading succeeds or fails through the OS loader.
"""
import argparse
from contextlib import ExitStack
import ctypes
import json
import os
from pathlib import Path
import sys
import subprocess

ENTRIES = {'ixrawpreview': ('ixReadRawPreviewSized',),
           'ixextensions': ('ixOpenExtensionManager', 'ixOpenFunctionExtensionManager')}


def verify_modules(stage, system=sys.platform, loader=None, runner=subprocess.run):
    stage = Path(stage).resolve()
    if system == 'darwin' and loader is None:
        # macdeployqt may use @executable_path for copied non-Qt libraries.
        # Resolve them inside the bundle executable, with its actual dyld context.
        app = stage / 'uxzView.app/Contents/MacOS/uxzView'
        process = runner([str(app), '--probe-modules'], capture_output=True, text=True, timeout=30)
        if process.returncode:
            raise RuntimeError(f'Bundle module probe failed ({process.returncode}): {process.stderr}\n{process.stdout}')
        result = json.loads(process.stdout)
        for name, symbols in ENTRIES.items():
            module = result.get(name, {})
            if module.get('loaded') is not True or module.get('symbols') != list(symbols):
                raise RuntimeError('Bundle module missing or incomplete: ' + name)
        return result
    if loader is None:
        loader = ctypes.CDLL
    if system == 'darwin':
        folder = stage / 'uxzView.app/Contents/PlugIns/IxView'
        suffix = '.so'  # CMake MODULE libraries on macOS, not SHARED dylibs.
    elif system.startswith('linux'):
        folder, suffix = stage / 'lib/IxView', '.so'
    elif system == 'win32':
        folder = stage / 'bin' if (stage / 'bin/uxzView.exe').is_file() else stage
        suffix = '.dll'
    else:
        raise RuntimeError('Unsupported desktop platform: ' + system)
    result, libraries = {}, []
    with ExitStack() as scope:
        if system == 'win32':
            scope.enter_context(os.add_dll_directory(str(folder)))
        for name, symbols in ENTRIES.items():
            module = folder / (name + suffix)
            if not module.is_file():
                raise RuntimeError('Installed module missing: ' + str(module))
            try:
                library = loader(str(module))
                libraries.append(library)  # Retain all handles until verification finishes.
                for symbol in symbols:
                    getattr(library, symbol)
            except (OSError, AttributeError) as error:
                raise RuntimeError(f'Cannot load {module}: {error}') from error
            result[name] = {'path': module.relative_to(stage).as_posix(), 'symbols': list(symbols)}
    return result


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path,
                        default=root / 'dist' / ('uxzView' if sys.platform == 'win32' else 'native'))
    args = parser.parse_args()
    print(json.dumps(verify_modules(args.stage), indent=2))


if __name__ == '__main__':
    main()
