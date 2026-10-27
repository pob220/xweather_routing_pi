"""Exercise the rebuilt host with ordinary GUI/IPC exits and the native plugin."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import importlib.util
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request

assert sys.platform == 'win32' and os.environ.get('CIRCLECI') == 'true'
ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'core-source'
OUT = ROOT / 'shutdown-artifacts'
OUT.mkdir(exist_ok=True)
HOST = ROOT / '.fixed-host'
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
archives = list((CORE / 'build').glob('*setup_x64.exe'))
assert len(archives) == 1, archives
shutil.copy2(archives[0], OUT / archives[0].name)
for symbols in (CORE / 'build').glob('*.pdb.tar.gz'):
    shutil.copy2(symbols, OUT / symbols.name)
subprocess.run(['7z', 'x', '-y', '-o' + str(HOST), archives[0]], check=True,
               stdout=subprocess.DEVNULL)
shutil.copy2(CORE / 'build/Release/opencpn.pdb', HOST / 'opencpn.pdb')
spec = importlib.util.spec_from_file_location('installed_host', ROOT / 'ci/test-installed-host.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
version, date = helper.host_build((HOST / 'opencpn.exe').read_bytes(), '5.15.0')
environment = dict(os.environ, PATH=str(HOST) + os.pathsep + os.environ['PATH'])
for key in list(environment):
    if key.startswith('WR_HEADLESS_'):
        del environment[key]
config_dir = Path(os.environ['PROGRAMDATA']) / 'opencpn'
assert not (config_dir / 'opencpn.ini').exists(), 'CI VM contains an existing profile'
receipts = []
patch_hash = hashlib.sha256((ROOT / 'ci/core-shutdown.patch').read_bytes()).hexdigest()
(OUT / 'host-provenance.json').write_text(json.dumps(dict(
    base_commit='02cf53340029c291b2903a5eeaa5a6b9443f9d21', patch_sha256=patch_hash,
    host=version, build_date=date, rebuilt_host_sha256=helper.digest(HOST / 'opencpn.exe'),
    original_installer_was_not_modified=True), indent=2))


def close_window(pid):
    api = ctypes.windll.user32
    api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    api.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    api.IsWindowVisible.argtypes = [wintypes.HWND]
    api.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    windows = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def collect(window, _):
        owner = wintypes.DWORD()
        api.GetWindowThreadProcessId(window, ctypes.byref(owner))
        title = ctypes.create_unicode_buffer(1024)
        api.GetWindowTextW(window, title, len(title))
        if owner.value == pid and api.IsWindowVisible(window) and title.value.startswith('OpenCPN'):
            windows.append((window, title.value))
        return True

    api.EnumWindows(collect, 0)
    assert len(windows) == 1, windows
    assert api.PostMessageW(windows[0][0], 0x0010, 0, 0), 'WM_CLOSE failed'
    return windows[0][1]


try:
    tests = CORE / 'build/test/Release/win_usb_watch_tests.exe'
    run = subprocess.run([tests, '--gtest_output=xml:' + str(OUT / 'watcher-unit.xml')],
                         env=environment, capture_output=True, text=True, timeout=30)
    (OUT / 'watcher-unit.log').write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, flush=True)
    assert run.returncode == 0, 'The core-integrated watcher regression failed'
    for mechanism in ('remote', 'window-close'):
        for grib in (False, True):
            label = mechanism + '-grib' + str(int(grib))
            config_dir.mkdir(exist_ok=True)
            header = ('[Settings]\nConfigVersionString=Version ' + version + ' Build ' + date +
                      '\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n')
            for dll in (HOST / 'plugins').glob('*.dll'):
                enabled = grib and dll.name == 'grib_pi.dll'
                header += '[PlugIns/' + dll.name + ']\nbEnabled=' + str(int(enabled)) + '\n'
            (config_dir / 'opencpn.ini').write_text(header)
            case = OUT / label
            case.mkdir()
            with (case / 'stdout.log').open('w') as stdout:
                process = subprocess.Popen([HOST / 'opencpn.exe', '--no_opengl'], cwd=HOST,
                                           env=environment, stdout=stdout, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 90
                    while time.monotonic() < deadline and process.poll() is None:
                        log = config_dir / 'opencpn.log'
                        if log.is_file() and 'Finalize Canvases' in log.read_text(errors='replace'):
                            break
                        time.sleep(0.2)
                    assert process.poll() is None, 'Host failed during startup'
                    time.sleep(1)
                    if mechanism == 'remote':
                        quit_result = subprocess.run([HOST / 'opencpn.exe', '--remote', '--quit'],
                            cwd=HOST, env=environment, capture_output=True, text=True, timeout=30)
                        (case / 'quit.log').write_text(quit_result.stdout + quit_result.stderr)
                        assert quit_result.returncode == 0
                    else:
                        (case / 'window-close.json').write_text(json.dumps(dict(title=close_window(process.pid))))
                    code = process.wait(timeout=30)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    for name in ('opencpn.ini', 'opencpn.log'):
                        if (config_dir / name).is_file():
                            shutil.copy2(config_dir / name, case / name)
            receipts.append(dict(case=label, exit_code=code, weather_dll_present=False))
            (OUT / 'fixed-shutdown-controls.json').write_text(json.dumps(receipts, indent=2))
            print(json.dumps(receipts[-1]), flush=True)
            assert code == 0, 'Rebuilt host still fails shutdown: ' + label
            shutil.rmtree(config_dir)

    package = ROOT / 'weather-routing-native.tar.gz'
    url = json.loads((ROOT / 'ci/shutdown-package.json').read_text())['url']
    with urllib.request.urlopen(url, timeout=120) as response, package.open('wb') as target:
        shutil.copyfileobj(response, target)
    run = subprocess.run([sys.executable, ROOT / 'ci/test-installed-host.py', HOST / 'opencpn.exe',
                          HOST / 'opencpn-cmd.exe', package, OUT / 'normal-route',
                          '--host-version', '5.15.0'], env=dict(environment, WR_HEADLESS_LIFECYCLE_CONTRACT='1'),
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    (OUT / 'route-harness.log').write_text(run.stdout)
    print(run.stdout, flush=True)
    assert run.returncode == 0, 'The normal installed plugin qualification still fails'
    print('Rebuilt core: window-close, remote quit, normal routing and reinstall all pass', flush=True)
except Exception:
    # Preserve a second fault, if one remains, while the disposable VM is alive.
    if config_dir.exists():
        for name in ('opencpn.ini', 'opencpn.log'):
            if (config_dir / name).is_file():
                shutil.copy2(config_dir / name, OUT / ('failure-' + name))
        shutil.rmtree(config_dir)
    subprocess.run([sys.executable, ROOT / 'ci/debug-host-shutdown.py'], env=dict(environment,
        WR_DEBUG_EXISTING_HOST=str(HOST), WR_DEBUG_OUTPUT=str(OUT / 'remaining-fault')), timeout=600)
    raise
