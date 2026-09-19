"""Build both desktop identities for the native OpenCPN Windows x64 Preview.

Use the recorded host import library, its matching zlib, and wxWidgets 3.2.8.
The disposable source overlay matches the Preview's installed-data lookup and
Windows test temporary paths; routing implementation files are not altered.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

from windows64_pe import verify

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.windows64'
ARTIFACTS = ROOT / 'artifacts/windows-x64'


def run(*args, cwd=ROOT):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def download(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_suffix(path.suffix + '.part')
        with urllib.request.urlopen(url, timeout=120) as response, partial.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(partial) != expected:
            raise RuntimeError(f'Archive checksum mismatch: {path.name}')
        partial.replace(path)
    if digest(path) != expected:
        raise RuntimeError(f'Archive checksum mismatch: {path.name}')


def prepare_sdk():
    pin = json.loads((ROOT / 'ci/windows64-sdk.json').read_text())
    archive = ROOT / pin['archive']
    if digest(archive) != pin['sha256']:
        raise RuntimeError('Native host SDK archive checksum mismatch')
    sdk = WORK / 'sdk'
    with zipfile.ZipFile(archive) as package:
        for member in package.namelist():
            if Path(member).is_absolute() or '..' in Path(member).parts:
                raise RuntimeError('Unsafe SDK archive path')
        package.extractall(sdk)
    manifest = json.loads((sdk / 'manifest.json').read_text())
    if manifest['architecture'] != 'AMD64' or manifest['core_revision'] != pin['core_revision']:
        raise RuntimeError('Native host SDK provenance mismatch')
    for relative, expected in manifest['files'].items():
        if digest(sdk / relative) != expected:
            raise RuntimeError(f'Native SDK file checksum mismatch: {relative}')
    verify(sdk)
    wx = WORK / 'cache/wxWidgets-3.2.8'
    for filename, expected in pin['wx_archives'].items():
        path = WORK / 'cache' / filename
        download('https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.8/' + filename,
                 path, expected)
        run('7z', 'x', '-y', '-o' + str(wx), path)
    wxlib = wx / 'lib/vc14x_x64_dll'
    verify(wxlib)
    os.environ['PATH'] = str(wxlib) + os.pathsep + str(sdk / 'bin') + os.pathsep + os.environ['PATH']
    os.environ['WX_VER'] = '32'
    os.environ['OCPN_TARGET'] = 'MSVC-x64'
    return sdk, wx, wxlib


def prepare_source(revision):
    source = WORK / 'source'
    if source.exists():
        raise RuntimeError('Remove the disposable .windows64/source before rebuilding')
    run('git', 'clone', '--shared', '--no-checkout', ROOT, source)
    run('git', '-c', 'core.autocrlf=false', 'checkout', '--detach', revision, cwd=source)
    run('git', 'remote', 'set-url', 'origin', 'https://github.com/pob220/xweather_routing_pi.git', cwd=source)
    run('git', '-c', 'core.autocrlf=false', 'submodule', 'update', '--init', 'opencpn-libs', cwd=source)
    icons = source / 'src/icons.cpp'
    text = icons.read_text(encoding='utf-8')
    if text.count('GetPluginDataDir("weather_routing_pi")') != 2:
        raise RuntimeError('Review the Preview icon/data overlay against the new source')
    text = text.replace('#include "icons.h"', '#include "icons.h"\n#include "version.h"')
    icons.write_text(text.replace('GetPluginDataDir("weather_routing_pi")',
                                  'GetPluginDataDir(PLUGIN_PACKAGE_NAME)'), encoding='utf-8')
    names = {
        'StabilityCorridor_tests.cpp': ['weather-routing-stability-test.geojson'],
        'RoutingScenarioJson_tests.cpp': ['weather-routing-climatology-on.json',
            'weather-routing-climatology-absent.json', 'weather-routing-utc-scenario.json',
            'weather-routing-stability-result.json'],
    }
    for filename, paths in names.items():
        path = source / 'test' / filename
        text = path.read_text(encoding='utf-8')
        if '#include <wx/filename.h>' not in text:
            text = text.replace('#include <wx/wx.h>', '#include <wx/wx.h>\n#include <wx/filename.h>')
        for temporary in paths:
            needle = '"/tmp/' + temporary + '"'
            if text.count(needle) != 1:
                raise RuntimeError('Review the Windows temporary-path test overlay')
            text = text.replace(needle, 'wxFileName::CreateTempFileName("' + temporary + '-")')
        path.write_text(text, encoding='utf-8')
    patch = subprocess.check_output(['git', 'diff', '--', 'src', 'test'], cwd=source)
    (ARTIFACTS / 'windows-native-overlays.patch').write_bytes(patch)
    return source


def build(identity, source, sdk, wx, wxlib, revision):
    enabled = identity == 'xweather'
    package = 'xweather_routing_pi' if enabled else 'weather_routing_pi'
    work = WORK / identity
    output = ARTIFACTS / identity
    output.mkdir(parents=True, exist_ok=True)
    run('cmake', '-S', source, '-B', work, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_BUILD_TYPE=Release', '-DOCPN_BUILD_TEST=ON',
        '-DWEATHER_ROUTING_STANDALONE_API=ON',
        '-DWEATHER_ROUTING_XWEATHER_IDENTITY=' + ('ON' if enabled else 'OFF'),
        '-DWEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY=' + str(sdk / 'lib/opencpn.lib'),
        '-DZLIB_ROOT=' + str(sdk),
        '-DZLIB_LIBRARY_RELEASE=' + str(sdk / 'lib/z.lib'),
        '-DwxWidgets_ROOT_DIR=' + str(wx), '-DwxWidgets_LIB_DIR=' + str(wxlib),
        '-DwxWidgets_CONFIGURATION=mswu', '-DINSTALL_GTEST=OFF',
        '-DCMAKE_INSTALL_PREFIX=' + str(work / 'stage'))
    header = (work / 'CMakeFiles/include/version.h').read_text()
    version = '.'.join(re.search(r'#define PLUGIN_VERSION_' + part + r'\s+(\d+)', header)[1]
                       for part in ('MAJOR', 'MINOR', 'PATCH', 'TWEAK'))
    if re.search(r'#define PLUGIN_PACKAGE_NAME\s+"([^"]+)"', header)[1] != package:
        raise RuntimeError('Compiled plugin identity differs from the requested package')
    run('cmake', '--build', work, '--config', 'Release', '--parallel', '4')
    run('ctest', '--test-dir', work, '-C', 'Release', '--output-on-failure',
        '--no-tests=error', '--timeout', '180', '--output-junit', output / 'ctest.xml')
    run('cmake', '--install', work, '--config', 'Release', '--prefix', work / 'stage')
    run('cpack', '-G', 'TGZ', '-C', 'Release', '--config', 'CPackConfig.cmake', cwd=work)
    archives = list(work.glob(package + '-*.tar.gz'))
    metadata = list(work.glob(package + '-*.xml'))
    if len(archives) != 1 or len(metadata) != 1:
        raise RuntimeError('Expected exactly one native archive and metadata pair')
    root = ET.parse(metadata[0]).getroot()
    values = [(root.findtext(key) or '').strip() for key in ('target', 'target-version', 'target-arch', 'version')]
    if values != ['msvc-wx32-x64', '10', 'x86_64', version]:
        raise RuntimeError(f'Incorrect Windows x64 metadata: {values}')
    with tempfile.TemporaryDirectory() as directory, tarfile.open(archives[0]) as archive:
        members = archive.getmembers()
        if sum(Path(m.name).name == package + '.dll' for m in members) != 1:
            raise RuntimeError('The native package must contain exactly its selected plugin DLL')
        if any('gtest' in m.name.lower() or 'gmock' in m.name.lower() for m in members):
            raise RuntimeError('Test development files entered the native plugin package')
        archive.extractall(directory, filter='data')
        architecture = verify(Path(directory))
        if len(architecture) != 1:
            raise RuntimeError('Unexpected extra native images in the plugin package')
    run(sys.executable, source / 'ci/verify-shoreline-package.py', archives[0])
    for path in (archives[0], metadata[0], work / 'CMakeCache.txt'):
        shutil.copy2(path, output)
    provenance = {'source_revision': revision, 'version': version,
        'sdk': json.loads((sdk / 'manifest.json').read_text()),
        'architecture': architecture,
        'sha256': {p.name: digest(p) for p in (archives[0], metadata[0])}}
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('Verified native Windows x64 package:', archives[0].name, flush=True)


def main():
    if sys.platform != 'win32':
        raise SystemExit('Use a native Windows x64 MSVC environment')
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    sdk, wx, wxlib = prepare_sdk()
    source = prepare_source(revision)
    failures = []
    for identity in ('xweather', 'weather'):
        try:
            build(identity, source, sdk, wx, wxlib, revision)
        except (subprocess.CalledProcessError, RuntimeError, OSError) as error:
            failures.append(identity)
            print(f'Native build failed: {identity}: {error}', flush=True)
        finally:
            output = ARTIFACTS / identity
            output.mkdir(exist_ok=True)
            if (output / 'ctest.xml').is_file():
                results = ROOT / 'test-results/windows-x64'
                results.mkdir(parents=True, exist_ok=True)
                shutil.copy2(output / 'ctest.xml', results / (identity + '.xml'))
            for relative in ('CMakeCache.txt', 'CMakeFiles/CMakeConfigureLog.yaml',
                             'Testing/Temporary/LastTest.log'):
                path = WORK / identity / relative
                if path.is_file():
                    shutil.copy2(path, output / path.name)
    if failures:
        raise SystemExit('Windows x64 qualification failed: ' + ', '.join(failures))


if __name__ == '__main__':
    main()
