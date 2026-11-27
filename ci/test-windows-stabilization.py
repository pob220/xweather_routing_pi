"""Exercise the packaged 2.x planner with saved data in a native Windows host."""
import argparse
import configparser
from contextlib import closing
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET


def make_waypoint_database(source, destination):
    # Start from the schema of the tested host. Seed a populated profile
    # before launch so its legacy GPX migration cannot overlap the UI check.
    with closing(sqlite3.connect(source.as_uri() + '?mode=ro', uri=True)) as host_db:
        with closing(sqlite3.connect(destination)) as db:
            host_db.backup(db)
            with db:
                db.executemany("""
                    INSERT INTO routepoints
                      (guid, lat, lon, Name, Symbol, description, TideStation,
                       Type, Time, RangeRingsColour, visibility, viz_name, isolated)
                    VALUES (?, ?, ?, ?, 'diamond', '', '', 'type', '',
                            '#FFFFFF', 0, 0, 1)
                    """, (
                        (str(uuid.UUID(int=index + 1)), 20 + index / 10000,
                         -40, f'Saved mark {index + 1}')
                        for index in range(6000)
                    ))


def test(host, output, forecast):
    profile = host.with_name(host.name + '-responsiveness')
    private = profile / 'routing-data'
    output.mkdir(parents=True, exist_ok=True)
    private.mkdir(parents=True)
    # With --configdir, OpenCPN discovers plug-ins in that profile's plugins
    # directory. Install the already extracted package there as well.
    shutil.copytree(host / 'plugins', profile / 'plugins')
    make_waypoint_database(host / 'navobj.db', profile / 'navobj.db')
    config = configparser.RawConfigParser(strict=False)
    config.optionxform = str
    config.read(host / 'opencpn.ini', encoding='utf-8-sig')
    section = 'PlugIns/xWeatherRouting2'
    if not config.has_section(section):
        config.add_section(section)
    config.set(section, 'PassageGribFile', forecast.as_posix())
    with (profile / 'opencpn.conf').open('w', encoding='utf-8') as stream:
        config.write(stream, space_around_delimiters=False)
    ET.ElementTree(ET.Element('OpenCPNWeatherRoutingConfiguration',
                              version='1.10', creator='Responsiveness test')).write(
        private / 'WeatherRoutingConfiguration.xml',
        encoding='utf-8', xml_declaration=True)

    user32 = ctypes.WinDLL('user32', use_last_error=True)
    w = wintypes
    callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user32.EnumWindows.argtypes = (callback, w.LPARAM)
    user32.EnumChildWindows.argtypes = (w.HWND, callback, w.LPARAM)
    user32.GetWindowTextW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    user32.GetClassNameW.argtypes = (w.HWND, w.LPWSTR, ctypes.c_int)
    user32.GetWindowThreadProcessId.argtypes = (w.HWND, ctypes.POINTER(w.DWORD))
    user32.IsWindowVisible.argtypes = (w.HWND,)
    user32.IsWindowEnabled.argtypes = (w.HWND,)
    user32.SetForegroundWindow.argtypes = (w.HWND,)
    user32.PostMessageW.argtypes = (w.HWND, w.UINT, w.WPARAM, w.LPARAM)
    user32.SendMessageTimeoutW.argtypes = (
        w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT,
        ctypes.POINTER(ctypes.c_size_t))
    user32.SendMessageTimeoutW.restype = w.LPARAM

    def label(hwnd):
        text = ctypes.create_unicode_buffer(4096)
        user32.GetWindowTextW(hwnd, text, len(text))
        return text.value.replace('&', '')

    def windows():
        found = []

        @callback
        def visit(hwnd, _):
            pid = w.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == process.pid and user32.IsWindowVisible(hwnd):
                found.append((hwnd, label(hwnd)))
            return True

        user32.EnumWindows(visit, 0)
        return found

    def control(hwnd, label_text):
        found = []

        @callback
        def visit(child, _):
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(child, name, len(name))
            if (name.value.lower() == 'button' and user32.IsWindowVisible(child)
                    and user32.IsWindowEnabled(child) and label(child) == label_text):
                found.append(child)
            return True

        user32.EnumChildWindows(hwnd, visit, 0)
        return found[0] if found else None

    def wait_for(description, condition, seconds):
        started = time.monotonic()
        while time.monotonic() - started < seconds and process.poll() is None:
            result = condition()
            if result:
                timings[description] = round(time.monotonic() - started, 3)
                return result
            time.sleep(0.05)
        raise RuntimeError(f'{description} did not respond within {seconds} s: {windows()}')

    def responsive(hwnd):
        result = ctypes.c_size_t()
        # WM_NULL is handled by the GUI thread. Bound the wait even if the
        # window exists but is stuck in work that prevents handling input.
        return user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 0x0002, 1000,
                                          ctypes.byref(result))

    def click(owner, caption):
        button = control(owner, caption)
        if not button:
            raise RuntimeError(f'Missing enabled {caption!r} in {label(owner)!r}')
        user32.SetForegroundWindow(owner)
        if not user32.PostMessageW(button, 0x00F5, 0, 0):
            raise ctypes.WinError(ctypes.get_last_error())

    timings = {}
    env = {key: value for key, value in os.environ.items()
           if not key.startswith('WR_HEADLESS_')}
    env.update(WR_OPEN_PASSAGE_PLANNER='1', WR_HEADLESS_DATA_DIR=str(private))
    host_log = profile / 'opencpn.log'
    with (output / 'stdout.log').open('w') as stream:
        process = subprocess.Popen([str(host / 'opencpn.exe'), '-p',
                                    '--configdir=' + str(profile)],
                                   cwd=host, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT)
        try:
            planner = wait_for('planner startup', lambda: next(
                (hwnd for hwnd, _ in windows() if control(hwnd, 'New passage')), None), 45)
            if not responsive(planner):
                raise RuntimeError('The planner window did not process messages after startup')
            click(planner, 'New passage')
            editor = wait_for('new passage editor', lambda: next(
                (hwnd for hwnd, title in windows() if title == 'Define passage'), None), 5)
            # Repeatedly ping during waypoint population, then close while
            # the UI is still accepting input.
            for _ in range(12):
                if not responsive(editor):
                    raise RuntimeError('New passage stopped processing messages while filling waypoints')
                time.sleep(0.12)
            click(editor, 'Cancel')
            wait_for('cancel new passage', lambda: user32.IsWindowEnabled(planner)
                     and not user32.IsWindowVisible(editor), 5)
            click(planner, 'Detailed report')
            results = wait_for('results workspace', lambda: next(
                (hwnd for hwnd, title in windows() if title == 'Passage results'), None), 5)
            if not responsive(results):
                raise RuntimeError('The results window did not process messages after opening')
            wait_for('saved forecast restore', lambda: host_log.exists() and
                     'WR_SAVED_FORECAST restored' in host_log.read_text(
                         encoding='utf-8', errors='replace'), 15)
        finally:
            main = next((hwnd for hwnd, title in windows()
                         if title.startswith('OpenCPN ')), None)
            if main:
                user32.PostMessageW(main, 0x0010, 0, 0)  # WM_CLOSE
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            if host_log.exists():
                shutil.copy2(host_log, output / 'opencpn.log')
    with closing(sqlite3.connect((profile / 'navobj.db').as_uri() + '?mode=ro',
                                 uri=True)) as db:
        mark_count = db.execute(
            "SELECT count(*) FROM routepoints WHERE name LIKE 'Saved mark %'"
        ).fetchone()[0]
    if mark_count != 6000:
        raise RuntimeError(f'Expected 6000 saved waypoints, found {mark_count}')
    (output / 'responsiveness.json').write_text(
        json.dumps({'profile_waypoints': mark_count, 'seconds': timings}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('forecast', type=Path)
    args = parser.parse_args()
    test(args.host.resolve(), args.output.resolve(), args.forecast.resolve())
