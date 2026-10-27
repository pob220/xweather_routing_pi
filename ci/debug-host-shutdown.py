"""Capture the stock host's shutdown fault in a disposable Windows CI VM."""
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
import threading
import time
import urllib.request

assert sys.platform == 'win32' and os.environ.get('CIRCLECI') == 'true'
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('WR_DEBUG_OUTPUT', str(ROOT / 'shutdown-artifacts')))
OUT.mkdir(parents=True, exist_ok=True)
HOST = Path(os.environ.get('WR_DEBUG_EXISTING_HOST', str(ROOT / '.debug-host')))
PIN = json.loads((ROOT / 'ci/windows64-wx334-sdk.json').read_text())
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)


def download(url, path, checksum):
    with urllib.request.urlopen(url, timeout=120) as response, path.open('wb') as output:
        shutil.copyfileobj(response, output)
    assert hashlib.sha256(path.read_bytes()).hexdigest() == checksum, str(path)


if not os.environ.get('WR_DEBUG_EXISTING_HOST'):
    archive = ROOT / 'host.exe'
    download(PIN['host_url'], archive, PIN['host_sha256'])
    subprocess.run(['7z', 'x', '-y', '-o' + str(HOST), archive], check=True,
                   stdout=subprocess.DEVNULL)
    # No third-party plugin is downloaded or installed in this control.
    assert not (HOST / 'plugins/weather_routing_pi.dll').exists()
    host_symbols = ROOT / 'host-symbols.tar.gz'
    download('https://dl.cloudsmith.io/public/david-register/opencpn-unstable/raw/files/opencpn+79.02cf5334.pdb.tar.gz',
             host_symbols, 'd67ff467945c3fd98a8fceb9c233571a8f5966b354ff7938934049f65f07e5b7')
    with tarfile.open(host_symbols) as archive:
        matches = [entry for entry in archive.getmembers() if Path(entry.name).name == 'opencpn.pdb']
        assert len(matches) == 1
        (HOST / 'opencpn.pdb').write_bytes(archive.extractfile(matches[0]).read())
wx_symbols = ROOT / 'wx-symbols.7z'
download('https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.4/wxMSW-3.3.4_vc14x_x64_ReleasePDB.7z',
         wx_symbols, 'a91c3b8a71d963c6a16417dfd89e96f3dfc8fcffebd8da9916ace45d1f7d366f')
symbol_root = ROOT / 'wx-symbols'
subprocess.run(['7z', 'x', '-y', '-o' + str(symbol_root), wx_symbols], check=True,
               stdout=subprocess.DEVNULL)
sdk_setup = ROOT / 'winsdksetup.exe'
download('https://download.microsoft.com/download/466df871-c7b6-43b5-a1c3-dab7816e38fc/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe',
         sdk_setup, '6a879a51866dad34a36957160600a65951294a8aa495bd608661599756290864')
print('Installing only the Microsoft debugging tools in the disposable VM', flush=True)
install = subprocess.run([sdk_setup, '/features', 'OptionId.WindowsDesktopDebuggers',
                          '/quiet', '/norestart', '/log', OUT / 'debugger-install.log'], timeout=600)
assert install.returncode in (0, 3010), install.returncode
cdb = Path('C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe')
assert cdb.is_file(), 'Debugger installation did not provide cdb.exe'
environment = dict(os.environ, PATH=str(HOST) + os.pathsep + os.environ['PATH'])
for key in list(environment):
    if key.startswith('WR_HEADLESS_'):
        del environment[key]
config_dir = Path(os.environ['PROGRAMDATA']) / 'opencpn'
assert not (config_dir / 'opencpn.ini').exists(), 'CI VM contains an existing profile'
config_dir.mkdir(exist_ok=True)
binary = (HOST / 'opencpn.exe').read_bytes()
version = max(re.findall(rb'5\.15\.0[^\x00]*\x00', binary), key=len)[:-1].decode()
date = max(re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary))[:-1].decode()
header = ('[Settings]\nConfigVersionString=Version ' + version + ' Build ' + date +
          '\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n')
for dll in (HOST / 'plugins').glob('*.dll'):
    header += '[PlugIns/' + dll.name + ']\nbEnabled=0\n'
(config_dir / 'opencpn.ini').write_text(header)
symbols = ';'.join([str(HOST)] + sorted({str(p.parent) for p in symbol_root.rglob('*.pdb')}) +
                   ['srv*' + str(ROOT / 'ms-symbols') + '*https://msdl.microsoft.com/download/symbols'])


def quit_when_ready():
    try:
        log = config_dir / 'opencpn.log'
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if log.is_file() and 'Finalize Canvases' in log.read_text(errors='replace'):
                break
            time.sleep(1)
        time.sleep(5)
        result = subprocess.run([HOST / 'opencpn.exe', '--remote', '--quit'], cwd=HOST,
                                env=environment, capture_output=True, text=True, timeout=30)
        (OUT / 'debug-quit.log').write_text(result.stdout + result.stderr)
        print('Requested stock remote quit; client exit=' + str(result.returncode), flush=True)
    except Exception as error:
        (OUT / 'debug-quit-error.log').write_text(repr(error))


worker = threading.Thread(target=quit_when_ready, daemon=True)
worker.start()
dump = str(OUT / 'host-shutdown.dmp')
commands = '.exr -1; .ecxr; kv; dps @rsp L80; lm; .dump /m "' + dump + '"; q\n'
print('Launching the genuine host under CDB with exact host and wxWidgets symbols', flush=True)
with (OUT / 'cdb-shutdown.log').open('w') as stream:
    process = subprocess.Popen([cdb, '-G', '-y', symbols, '-lines', '-c', 'g',
                                HOST / 'opencpn.exe', '--no_opengl'], cwd=HOST, env=environment,
                               stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT,
                               text=True)
    try:
        process.communicate(commands, timeout=240)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise
worker.join(timeout=5)
for filename in ('opencpn.ini', 'opencpn.log'):
    if (config_dir / filename).is_file():
        shutil.copy2(config_dir / filename, OUT / filename)
log = (OUT / 'cdb-shutdown.log').read_text(errors='replace')
print(log[-22000:], flush=True)
(OUT / 'debug-result.json').write_text(json.dumps(dict(
    host_revision=PIN['core_revision'], weather_dll_present=False,
    debugger_exit=process.returncode, crash_dump_created=Path(dump).is_file()), indent=2))
