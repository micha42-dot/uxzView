"""Exercise actual ELF dependency deployment and relocation without requiring Qt."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not sys.platform.startswith('linux'):
        raise SystemExit('This integration check requires native Linux, CMake and a C compiler.')
    build_root = ROOT / 'build'
    build_root.mkdir(exist_ok=True)
    env = os.environ.copy()
    for name in ('LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'QT_PLUGIN_PATH',
                 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH'):
        env.pop(name, None)
    with tempfile.TemporaryDirectory(prefix='elf deployment ', dir=build_root) as temporary:
        folder = Path(temporary).resolve()
        build, stage = folder / 'build', folder / 'installed runtime'
        subprocess.run(['cmake', '-S', str(ROOT / 'tests/fixtures/linux_module_deployment'),
                        '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'],
                       env=env, check=True, timeout=60)
        subprocess.run(['cmake', '--build', str(build), '--parallel', '2'],
                       env=env, check=True, timeout=60)
        subprocess.run(['cmake', '--install', str(build), '--prefix', str(stage)],
                       env=env, check=True, timeout=60)
        assert (stage / 'lib/libixFixtureNetwork.so.6').is_file()
        sdk = (build / 'sdk').resolve()
        assert sdk.parent == build.resolve() and build.resolve().parent == folder
        sdk.rename(folder / 'sdk-unavailable')
        probe = ('import ctypes,sys; m=ctypes.CDLL(sys.argv[1]); '
                 'assert m.ix_module_probe() == 42')
        for runtime in (stage, folder / 'relocated runtime'):
            if runtime != stage:
                stage.rename(runtime)
            subprocess.run([sys.executable, '-c', probe, str(runtime / 'lib/IxView/ixextensions.so')],
                           cwd=folder, env=env, check=True, timeout=30)
    print('ELF regression: missing installed-module dependency reproduced; '
          'build-module deployment loads without SDK paths, including after relocation.')


if __name__ == '__main__':
    main()
