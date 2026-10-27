"""Temporary investigation in a disposable Windows CI VM, never a user profile."""
from pathlib import Path
import ctypes
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request

assert sys.platform == 'win32' and os.environ.get('CIRCLECI') == 'true'
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'shutdown-artifacts'
OUT.mkdir()
HOST = ROOT / '.shutdown-host'
PIN = json.loads((ROOT / 'ci/windows64-wx334-sdk.json').read_text())
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)


def download(url, destination):
    with urllib.request.urlopen(url, timeout=120) as response, destination.open('wb') as output:
        shutil.copyfileobj(response, output)


archive = ROOT / 'shutdown-host.exe'
download(PIN['host_url'], archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == PIN['host_sha256']
subprocess.run(['7z', 'x', '-y', '-o' + str(HOST), str(archive)], check=True,
               stdout=subprocess.DEVNULL)
PACKAGE = ROOT / 'shutdown-package.tar.gz'
download(json.loads((ROOT / 'ci/shutdown-package.json').read_text())['url'], PACKAGE)
with tarfile.open(PACKAGE) as source:
    for member in source.getmembers():
        parts = Path(member.name).parts
        if member.isfile() and 'plugins' in parts:
            path = Path(*parts[parts.index('plugins'):])
            assert '..' not in path.parts
            target = HOST / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.extractfile(member).read())

# Record any native debugging tools already supplied by the executor.
tools = subprocess.run(['powershell', '-NoProfile', '-Command',
    "Get-ChildItem 'C:/Program Files (x86)/Windows Kits' -Filter cdb.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName"],
    capture_output=True, text=True)
(OUT / 'debug-tools.log').write_text(tools.stdout + tools.stderr)
env = dict(os.environ, PATH=str(HOST) + os.pathsep + os.environ['PATH'])
for key in list(env):
    if key.startswith('WR_HEADLESS_'):
        del env[key]
config_dir = Path(os.environ['PROGRAMDATA']) / 'opencpn'
assert not (config_dir / 'opencpn.ini').exists(), 'CI VM unexpectedly contains a user profile'
binary = (HOST / 'opencpn.exe').read_bytes()
version = max(re.findall(rb'5\.15\.0[^\x00]*\x00', binary), key=len)[:-1].decode()
date = max(re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary))[:-1].decode()
results = []
for portable, weather, grib in [(False, False, False), (False, False, True),
                                (False, True, False), (False, True, True),
                                (True, False, False), (True, True, True)]:
    label = ('portable' if portable else 'normal') + '-weather' + str(int(weather)) + '-grib' + str(int(grib))
    case = OUT / label
    case.mkdir()
    location = HOST if portable else config_dir
    location.mkdir(exist_ok=True)
    config = location / 'opencpn.ini'
    header = ('[Settings]\nConfigVersionString=Version ' + version + ' Build ' + date +
              '\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n')
    for dll in (HOST / 'plugins').glob('*.dll'):
        enabled = weather if dll.name == 'weather_routing_pi.dll' else grib if dll.name == 'grib_pi.dll' else False
        header += '[PlugIns/' + dll.name + ']\nbEnabled=' + str(int(enabled)) + '\n'
    header += '[Plugins/WeatherRouting]\nConfigVersion=135\n[PlugIns/WeatherRouting]\nConfigVersion=135\n'
    config.write_text(header)
    args = ['-p'] if portable else []
    with (case / 'stdout.log').open('w') as stream:
        process = subprocess.Popen([str(HOST / 'opencpn.exe')] + args + ['--no_opengl'],
                                   cwd=HOST, env=env, stdout=stream, stderr=subprocess.STDOUT)
        time.sleep(10)
        quit_result = subprocess.run([str(HOST / 'opencpn.exe')] + args + ['--remote', '--quit'],
                                    cwd=HOST, env=env, capture_output=True, text=True, timeout=30)
        (case / 'quit.log').write_text(quit_result.stdout + quit_result.stderr)
        try:
            code = process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = 'timeout'
    for filename in ('opencpn.ini', 'opencpn.log'):
        path = location / filename
        if path.exists():
            shutil.copy2(path, case / filename)
    record = dict(case=label, exit_code=code, quit_exit_code=quit_result.returncode)
    results.append(record)
    print(json.dumps(record), flush=True)
    (OUT / 'controls.json').write_text(json.dumps(results, indent=2))
    if config_dir.exists():
        shutil.rmtree(config_dir)
    (HOST / 'opencpn.ini').unlink(missing_ok=True)
    (HOST / 'opencpn.log').unlink(missing_ok=True)

# Run the exact normal-install acceptance harness with the already-built native DLL.
try:
    run = subprocess.run([sys.executable, ROOT / 'ci/test-installed-host.py', HOST / 'opencpn.exe',
                          HOST / 'opencpn-cmd.exe', PACKAGE, OUT / 'normal-route',
                          '--host-version', '5.15.0'], env=dict(env, WR_HEADLESS_LIFECYCLE_CONTRACT='1'),
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    (OUT / 'route-harness.log').write_text(run.stdout)
    print(run.stdout[-5000:], flush=True)
except subprocess.TimeoutExpired as error:
    (OUT / 'route-harness.log').write_text(str(error))
events = subprocess.run(['powershell', '-NoProfile', '-Command',
    "Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000,1001; StartTime=(Get-Date).AddHours(-1)} -ErrorAction SilentlyContinue | Select-Object TimeCreated,Id,Message | ConvertTo-Json -Depth 4"],
    text=True, capture_output=True)
(OUT / 'windows-events.json').write_text(events.stdout + events.stderr)
print(events.stdout[-15000:], flush=True)
