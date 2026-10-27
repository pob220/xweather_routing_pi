"""Show the real Windows watcher failing before, and passing after, the fix."""
from pathlib import Path
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.request

assert sys.platform == 'win32' and os.environ.get('CIRCLECI') == 'true'
ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'core-source'
OUT = ROOT / 'shutdown-artifacts'
OUT.mkdir(exist_ok=True)
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
subprocess.run([sys.executable, ROOT / 'ci/prepare-core-fix.py'], check=True)
baseline = ROOT / 'usb-baseline'
(baseline / 'model').mkdir(parents=True)
for repo_path, output in [('model/include/model/win_usb_watch.h', baseline / 'model/win_usb_watch.h'),
                          ('model/src/win_usb_watch.cpp', baseline / 'win_usb_watch.cpp')]:
    text = subprocess.check_output(['git', '-C', CORE, 'show', 'HEAD:' + repo_path], text=True)
    # Remove the same unused include as the patch; no watcher logic is changed.
    output.write_text(text.replace('#include "model/gui.h"\n', ''))
project = ROOT / 'usb-regression'
project.mkdir()
(project / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(UsbWatcherRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_definitions(__WXMSW__ WXUSINGDLL UNICODE _UNICODE NOMINMAX)
include_directories("${WX}/include" "${WX}/lib/vc14x_x64_dll/mswu")
add_subdirectory("${CORE}/libs/gtest" gtest)
foreach(variant baseline fixed)
  if(variant STREQUAL "baseline")
    set(watcher "${BASELINE}/win_usb_watch.cpp")
  else()
    set(watcher "${CORE}/model/src/win_usb_watch.cpp")
  endif()
  add_executable(usb_${variant} "${CORE}/test/win_usb_watch_tests.cpp"
                 "${CORE}/libs/observable/src/observable.cpp" "${watcher}")
  if(variant STREQUAL "baseline")
    target_include_directories(usb_${variant} BEFORE PRIVATE "${BASELINE}")
  endif()
  target_include_directories(usb_${variant} PRIVATE "${CORE}/model/include"
    "${CORE}/libs/observable/include" "${CORE}/libs/observable/include/observable")
  target_link_libraries(usb_${variant} PRIVATE ocpn::gtest
    "${WX}/lib/vc14x_x64_dll/wxmsw${FAMILY}u_core.lib"
    "${WX}/lib/vc14x_x64_dll/wxbase${FAMILY}u.lib" ws2_32)
endforeach()
''')
results = []
for version, filename in [('3.2.9', 'windows64-sdk.json'), ('3.3.4', 'windows64-wx334-sdk.json')]:
    pin = json.loads((ROOT / 'ci' / filename).read_text())
    wx = ROOT / ('wx-' + version)
    for name, checksum in pin['wx_archives'].items():
        path = ROOT / name
        with urllib.request.urlopen('https://github.com/wxWidgets/wxWidgets/releases/download/v' + version + '/' + name,
                                    timeout=120) as response, path.open('wb') as target:
            shutil.copyfileobj(response, target)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == checksum
        subprocess.run(['7z', 'x', '-y', '-o' + str(wx), path], check=True, stdout=subprocess.DEVNULL)
    family = '32' if version == '3.2.9' else '334'
    build = ROOT / ('usb-build-' + family)
    subprocess.run(['cmake', '-S', project, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
                    '-DCORE=' + str(CORE), '-DBASELINE=' + str(baseline), '-DWX=' + str(wx),
                    '-DFAMILY=' + family], check=True)
    subprocess.run(['cmake', '--build', build, '--config', 'Release', '--parallel', '4'], check=True)
    env = dict(os.environ, PATH=str(wx / 'lib/vc14x_x64_dll') + os.pathsep + os.environ['PATH'])
    for variant, test_filter, expected_success in [('baseline', 'WinUsbWatch.RepeatedStartAndStop', True),
            ('baseline', 'WinUsbWatch.ParentDestructionBeforeStopAndRestart', False),
            ('fixed', 'WinUsbWatch.*', True)]:
        label = version + '-' + variant + '-' + str(expected_success)
        run = subprocess.run([build / 'Release' / ('usb_' + variant + '.exe'),
                              '--gtest_filter=' + test_filter,
                              '--gtest_output=xml:' + str(OUT / (label + '.xml'))],
                             text=True, capture_output=True, env=env, timeout=30)
        (OUT / (label + '.log')).write_text(run.stdout + run.stderr)
        print(run.stdout + run.stderr, flush=True)
        results.append(dict(wx_version=version, variant=variant, test_filter=test_filter,
                            exit=run.returncode, expected_success=expected_success))
        (OUT / 'usb-regression.json').write_text(json.dumps(results, indent=2))
        assert (run.returncode == 0) == expected_success, results[-1]
print('The old lifetime bug is reproduced; the fixed watcher passes both wxWidgets versions', flush=True)
