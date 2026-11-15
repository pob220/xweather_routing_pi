#!/usr/bin/env python3
"""Exercise a relocated Unix helper with its packaged libraries and data."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def test(plugin, output):
    output.mkdir(parents=True, exist_ok=True)
    relocated = output / 'relocated-plugin'
    shutil.copytree(plugin, relocated, symlinks=True)
    helper = relocated / 'bin/environmental-grib'
    runtime = relocated / 'runtime/share'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('ECCODES_', 'PROJ_', 'DYLD_', 'LD_'))}
    env.update(PATH='/usr/bin:/bin', ECCODES_DEFINITION_PATH=str(runtime / 'eccodes/definitions'),
               ECCODES_SAMPLES_PATH=str(runtime / 'eccodes/samples'), PROJ_DATA=str(runtime / 'proj'))
    assert (runtime / 'eccodes/definitions/grib1/boot.def').is_file()
    assert (runtime / 'eccodes/samples/regular_ll_sfc_grib1.tmpl').is_file()
    assert (runtime / 'proj/proj.db').is_file()
    if sys.platform == 'darwin':
        images = [helper] + list((relocated / 'Frameworks').glob('*.dylib'))
        assert len(images) > 1, 'No private helper dependencies'
        for image in images:
            subprocess.run(['codesign', '--verify', '--strict', str(image)], check=True, capture_output=True)
            dependencies = subprocess.check_output(['otool', '-L', str(image)], text=True)
            assert '/opt/homebrew/' not in dependencies and '/usr/local/' not in dependencies, dependencies
    else:
        binary = relocated / 'libexec/environmental-grib.bin'
        libraries = dict(env, LD_LIBRARY_PATH=str(relocated / 'runtime/lib'))
        dependencies = subprocess.check_output(['ldd', str(binary)], env=libraries, text=True)
        assert 'not found' not in dependencies, dependencies
        (output / 'dependencies.log').write_text(dependencies)

    def run(name, *args):
        result = subprocess.run([str(helper), *map(str, args)], cwd=output, env=env,
                                capture_output=True, text=True, timeout=90)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.returncode}: {result.stderr}')
        return result

    run('capabilities', 'capabilities')
    run('inspect-wind', 'inspect-grib', ROOT / 'ci/testdata/irish-sea-uniform-wind.grb')
    # Exercises actual GRIB writes and the ecCodes samples without network access.
    subprocess.run([sys.executable, str(ROOT / 'third_party/environmental-grib-generator/tests/preflight_cli_tests.py'), str(helper)],
                   cwd=output, env=env, check=True, timeout=90)
    (output / 'acceptance.json').write_text(json.dumps({
        'relocated': True, 'capabilities': True, 'read_grib': True,
        'generate_grib_offline': True, 'packaged_data_checked': True,
        'mac_dependency_signatures_checked': sys.platform == 'darwin',
    }, indent=2) + '\n')
    print('Relocated packaged Unix helper: capabilities, GRIB read/write and runtime checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.plugin.resolve(), args.output.resolve())
