"""Qualify the exact Win32 v2 archive and its separate AMD64 GRIB helper."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'artifacts/windows-x86'


def main():
    package_dir = OUTPUT / 'package'
    archives = list(package_dir.glob('xweather_routing_pi-2.3.3-*.tar.gz'))
    if len(archives) != 1:
        raise RuntimeError('Expected exactly one xWeatherRouting 2.3.3 Win32 archive')
    archive = archives[0]
    subprocess.run([sys.executable, str(ROOT / 'ci/verify-package-abi.py'),
                    str(package_dir), '--target', 'windows-x86'], check=True)
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    acceptance = json.loads((OUTPUT / 'normal-runtime/acceptance.json').read_text())
    if acceptance['package_sha256'] != digest or acceptance['identity'] != 'xweather_routing_pi':
        raise RuntimeError('Normal installed-host receipt does not match this archive')
    for name in ['normal_profile', 'imported_by_stock_host', 'orderly_host_exit',
                 'data_lookup_checked', 'reinstall_preserves_personal_data']:
        if not acceptance[name]:
            raise RuntimeError('Installed host check failed: ' + name)
    if len(acceptance['routes']) < 7 or any(
            case['status'] != 'complete' or case['exit_code'] != 0 for case in acceptance['routes']):
        raise RuntimeError('Installed host route controls did not all pass')
    tests = ET.parse(OUTPUT / 'tests/ctest.xml').getroot()
    enabled = int(tests.attrib['tests']) - int(tests.attrib.get('disabled', '0'))
    if enabled < 723 or int(tests.attrib['failures']) or int(tests.attrib.get('skipped', '0')):
        raise RuntimeError('Win32 unit-test qualification failed')
    helper = ROOT / '.windows32-packaged-helper'
    if helper.exists():
        shutil.rmtree(helper)
    with tarfile.open(archive, 'r:gz') as bundle:
        if bundle.getnames().count('metadata.xml') != 1:
            raise RuntimeError('Package needs one root-level metadata.xml')
        metadata = ET.fromstring(bundle.extractfile('metadata.xml').read())
        if metadata.findtext('name').strip() != 'xWeatherRouting' or metadata.findtext('version').strip() != '2.3.3':
            raise RuntimeError('Incorrect package identity or version')
        plugin = [item for item in bundle if item.isfile() and
                  PurePosixPath(item.name).name == 'xweather_routing_pi.dll']
        if len(plugin) != 1:
            raise RuntimeError('Archive needs exactly one Win32 plugin DLL')
        marker = '/plugins/xweather_routing_pi/'
        for item in bundle:
            if not item.isfile():
                continue
            if PurePosixPath(item.name).name.lower() == 'opencpn.exe':
                raise RuntimeError('This must be a plugin-only archive')
            if item.name.lower().endswith('.dll') and item not in plugin and marker + 'bin/' not in item.name:
                raise RuntimeError('A helper DLL escaped its private directory: ' + item.name)
            if marker not in item.name:
                continue
            relative = PurePosixPath(item.name.split(marker, 1)[1])
            if relative.is_absolute() or '..' in relative.parts:
                raise RuntimeError('Unsafe package path')
            if relative.parts[0] in ['bin', 'runtime']:
                destination = helper.joinpath(*relative.parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(bundle.extractfile(item).read())
    subprocess.run([sys.executable, str(ROOT / 'ci/test-windows-generator.py'),
                    str(helper), str(OUTPUT / 'packaged-helper-runtime')], check=True)
    sums = []
    for path in sorted(package_dir.iterdir()):
        if path.suffix == '.xml' or path.name.endswith('.tar.gz'):
            with path.open('rb') as stream:
                sums.append(hashlib.file_digest(stream, 'sha256').hexdigest() + '  ' + path.name)
    (package_dir / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
    record = dict(version='2.3.3', target='windows-x86', plugin_architecture='x86',
                  helper_architecture='x86_64', requires_64bit_windows=True,
                  package=archive.name, package_sha256=digest, unit_tests_passed=enabled,
                  embedded_metadata=True, plugin_only=True, normal_import_and_reinstallation=True,
                  routing_controls_passed=len(acceptance['routes']), offline_grib_generation=True,
                  private_helper_runtime_checked=True)
    (OUTPUT / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2), flush=True)


if __name__ == '__main__':
    main()
