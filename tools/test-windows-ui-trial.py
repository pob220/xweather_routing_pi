"""Exercise the delivered ZIP/launcher in a disposable native Windows runner."""
import argparse
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test(archive, plugin, output):
    if sys.platform != 'win32' or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Use a disposable native Windows GitHub Actions runner')
    output.mkdir(parents=True, exist_ok=False)
    destination = output / 'relocated trial with spaces'
    with zipfile.ZipFile(archive) as bundle:
        for name in bundle.namelist():
            parts = PurePosixPath(name)
            if parts.is_absolute() or '..' in parts.parts or ':' in name or '\\' in name:
                raise ValueError('Unsafe trial ZIP entry: ' + name)
        bundle.extractall(destination)
    trial = destination
    host = trial / 'OpenCPN'
    manifest = json.loads((trial / 'trial-manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest['files_sha256'].items():
        if sha(trial / name) != expected:
            raise RuntimeError('Relocated trial checksum mismatch: ' + name)
    if any(p.name.lower().startswith('uninstall') for p in host.iterdir()):
        raise RuntimeError('Portable trial must not contain an uninstaller')

    # Simulate an existing normal profile only in this disposable CI VM.
    # Both launch and route acceptance must leave it byte-for-byte intact.
    sentinels = [Path(os.environ['ProgramData']) / 'opencpn/opencpn.ini',
                 Path(os.environ['LOCALAPPDATA']) / 'opencpn/plugins/trial-keep.txt']
    for path in sentinels:
        if path.exists():
            raise RuntimeError('Refusing to replace an existing profile: ' + str(path))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('[TrialSentinel]\nKeep=existing normal OpenCPN\n', encoding='ascii')
    before = {p: sha(p) for p in sentinels}
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    kernel32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel32.OpenProcess.restype = w.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
    kernel32.CloseHandle.argtypes = [w.HANDLE]
    user32.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user32.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user32.EnumChildWindows.argtypes = [w.HWND, callback_type, w.LPARAM]

    def text(hwnd):
        buffer = ctypes.create_unicode_buffer(4096)
        user32.GetWindowTextW(hwnd, buffer, len(buffer))
        return buffer.value

    def owned_windows():
        result = []
        @callback_type
        def visit(hwnd, _):
            pid = w.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            handle = kernel32.OpenProcess(0x1000, False, pid.value)
            if handle:
                buffer, length = ctypes.create_unicode_buffer(32768), w.DWORD(32768)
                try:
                    if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)):
                        if Path(buffer.value).resolve() == (host / 'opencpn.exe').resolve():
                            result.append((hwnd, text(hwnd)))
                finally:
                    kernel32.CloseHandle(handle)
            return True
        user32.EnumWindows(visit, 0)
        return result

    # Only app-local DLLs and the Windows system directory are available.
    environment = dict(os.environ, PATH=str(Path(os.environ['SystemRoot']) / 'System32'))
    # The launcher must discard inherited automation and data-directory overrides.
    environment.update(WR_HEADLESS_ROUTE_TEST='should-be-cleared',
                       WR_HEADLESS_SCENARIO='should-be-cleared',
                       WR_HEADLESS_DATA_DIR=str(output / 'must-not-be-used'))
    with (output / 'launcher.log').open('w') as log:
        process = subprocess.Popen(['cmd', '/d', '/c', str(trial / 'Start-Weather-Routing.cmd')],
                                   cwd=trial, env=environment, stdout=log, stderr=subprocess.STDOUT)
        labels = set()
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline and process.poll() is None:
                for hwnd, title in owned_windows():
                    labels.add(title)
                    @callback_type
                    def visit(child, _):
                        labels.add(text(child).replace('&', ''))
                        return True
                    user32.EnumChildWindows(hwnd, visit, 0)
                if {'Passage actions', '1. Route', '2. Boat and polars', '3. Weather', 'Calculate route'} <= labels:
                    break
                time.sleep(0.25)
            else:
                raise RuntimeError('The delivered launcher did not show the passage planner: ' + repr(sorted(labels)))
            mains = [(hwnd, title) for hwnd, title in owned_windows() if title.startswith('OpenCPN')]
            if not mains:
                raise RuntimeError('Missing owned portable OpenCPN main window')
            user32.PostMessageW(mains[0][0], 0x0010, 0, 0)  # WM_CLOSE, this host only.
            if process.wait(timeout=30) != 0:
                raise RuntimeError('Trial launcher did not exit cleanly')
        finally:
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=False,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                process.wait(timeout=10)
    if (output / 'must-not-be-used').exists():
        raise RuntimeError('Launcher inherited the external plugin data override')
    profile = host / 'plugins/xweather_routing_v2'
    if not (profile / 'boats/Example-40ft-monohull.xml').is_file():
        raise RuntimeError('The portable first launch did not populate its own boat library')
    # A copy extracted from the delivered archive also calculates real routes.
    subprocess.run([sys.executable, str(ROOT / 'ci/test-windows-host.py'), str(host),
                    str(plugin), str(output / 'routes'), '--host-version', '5.15.0'], check=True)
    for path, expected in before.items():
        if sha(path) != expected:
            raise RuntimeError('Trial changed the normal profile: ' + str(path))
    (output / 'acceptance.json').write_text(json.dumps({
        'delivered_launcher_tested': True, 'relocated_path_with_spaces': True,
        'normal_profile_preserved': True, 'external_overrides_cleared': True,
        'app_local_crt': True, 'passage_planner_visible': True,
        'forecast_climatology_and_fallback_routes': 'complete',
        'visible_labels': sorted(labels), 'zip_sha256': sha(archive),
    }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.archive.resolve(), args.plugin.resolve(), args.output.resolve())
