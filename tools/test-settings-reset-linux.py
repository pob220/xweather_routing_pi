#!/usr/bin/env python3
"""Exercise reset races in a disposable GTK/X11 host. Never install the probe.

Requires a completed Ninja plugin build, its installed prefix, an OpenCPN host,
Xvfb, GTK development headers, objcopy and the normal plugin build dependencies.
The production objects are copied/relinked with a test-only factory wrapper;
this does not modify the build's production library or any existing profile.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import selectors
import shlex
import signal
import subprocess
import time
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
for flag in ("build", "prefix", "host", "xvfb", "output"):
    parser.add_argument("--" + flag, type=Path, required=True)
parser.add_argument("--forecast", type=Path, default=SOURCE / "ci/testdata/irish-sea-uniform-wind.grb")
parser.add_argument("--departure", default="2026-09-28T00:00:00")
parser.add_argument("--probe", choices=("settings-reset", "route-visibility"), default="settings-reset")
parser.add_argument("--opengl", action="store_true", help="Also exercise route visibility in a private OpenGL canvas")
args = parser.parse_args()
build, prefix, host, xvfb, output, forecast = (
    getattr(args, key).resolve() for key in ("build", "prefix", "host", "xvfb", "output", "forecast")
)
assert not output.exists(), "Choose a new output directory; previous runs are retained"
output.mkdir(parents=True, mode=0o700)


def run(command, log, **kwargs):
    with (output / log).open("w") as stream:
        subprocess.run(command, check=True, stdout=stream, stderr=subprocess.STDOUT, **kwargs)


rules = subprocess.check_output(["ninja", "-C", str(build), "-t", "rules"], text=True).splitlines()
rule = next(r for r in rules if r.startswith("CXX_COMPILER__xweather_routing_pi_unscanned_"))
db = json.loads(subprocess.check_output(["ninja", "-C", str(build), "-t", "compdb", rule], text=True))
command = shlex.split(next(c for c in db if c["file"].endswith("src/WeatherRouting.cpp"))["command"])
flags = []
i = 1
while i < len(command):
    word = command[i]
    if word in ("-I", "-isystem"):
        flags.extend(command[i:i + 2])
        i += 2
        continue
    if word.startswith(("-I", "-D", "-std=")):
        flags.append(word)
    i += 1
probe = "RouteVisibilityHostProbe.cpp" if args.probe == "route-visibility" else "SettingsResetHostProbe.cpp"
run([command[0], "-c", "-fPIC", "-O1", "-fno-access-control", *flags,
     str(SOURCE / "test/gui" / probe), "-o", str(output / "probe.o")], "compile.log")
link = shlex.split(subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "libxweather_routing_pi.so"], text=True).splitlines()[-1])
assert link[:2] == [":", "&&"] and link[-2:] == ["&&", ":"]
link = link[2:-2]
factory = next(w for w in link if w.endswith("/src/weather_routing_pi.cpp.o"))
run(["objcopy", "--redefine-sym", "create_pi=production_create_pi",
     str(build / factory), str(output / "factory.o")], "factory.log")
link[link.index(factory)] = str(output / "factory.o")
link[link.index("-o") + 1] = str(output / "libxweather_routing_pi.so")
link.append(str(output / "probe.o"))
if args.probe == "route-visibility":
    link.extend(shlex.split(subprocess.check_output(["wx-config", "--libs", "gl"], text=True)))
run(link, "link.log", cwd=build)

profile, data = output / "profile", output / "routing"
plugins = profile / "plugins/lib"
plugins.mkdir(parents=True)
data.mkdir()
(plugins / "libxweather_routing_pi.so").symlink_to(output / "libxweather_routing_pi.so")
(profile / "plugins/xweather_routing_pi").symlink_to(prefix / "share/opencpn/plugins/xweather_routing_pi", target_is_directory=True)
boat = output / "Boat.xml"
polar = prefix / "share/opencpn/plugins/xweather_routing_pi/data/polars/Example/Test-TWS-0-20+60.pol"
assert polar.is_file() and forecast.is_file() and host.is_file()
boat.write_text('<OpenCPNWeatherRoutingBoat><Polar FileName="' + str(polar) + '"/></OpenCPNWeatherRoutingBoat>')
root = ET.Element("OpenCPNWeatherRoutingConfiguration", version="1.10")
for name, longitude in (("QA start", -5.5), ("QA end", -4.8)):
    ET.SubElement(root, "Position", Name=name, Latitude="53.4", Longitude=str(longitude))
date, clock = args.departure.split("T")
ET.SubElement(root, "Configuration", Start="QA start", End="QA end", Boat=str(boat),
              StartType="0", EndType="0", dt="3600", UseCurrentTime="0", StartDate=date,
              StartTime=clock, UseGrib="1", ClimatologyType="0", DetectBoundary="0", Currents="0",
              MaxSwellMeters="0", AutomaticAdaptiveSettings="1", RoutingEngine="auto",
              AutoEngineMask="63", AllEngineMask="63")
ET.ElementTree(root).write(data / "WeatherRoutingConfiguration.xml", encoding="utf-8", xml_declaration=True)
(profile / "opencpn.conf").write_text(
    "[Settings]\nConfigVersionString=Version 5.15.0 Build 2026-10-07\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\n"
    "[Settings/GlobalState]\nAllowArbitrarySystemPlugins=1\n"
    "[PlugIns/libxweather_routing_pi.so]\nbEnabled=1\n"
    "[PlugIns/libgrib_pi.so]\nbEnabled=0\n[PlugIns/libxgrib_pi.so]\nbEnabled=0\n"
    "[PlugIns/xWeatherRouting2]\nConfigVersion=203\nPassageGribFile=" + str(forecast) + "\n")
env = {k: v for k, v in os.environ.items() if not k.startswith(("WR_HEADLESS_", "XGRIB_TEST_"))
       and k not in ("OPENCPN_PLUGIN_DIRS", "WR_OPEN_PASSAGE_PLANNER")}
env.update(TZ="UTC", OPENCPN_PLUGIN_DIRS=str(plugins), XDG_DATA_DIRS=str(prefix / "share") + ":/usr/local/share:/usr/share",
           XDG_CONFIG_HOME=str(profile / "xdg-config"), XDG_DATA_HOME=str(profile / "xdg-data"),
           XDG_CACHE_HOME=str(profile / "xdg-cache"), WR_HEADLESS_DATA_DIR=str(data),
           WR_OPEN_PASSAGE_PLANNER="1", WORKSPACE_QA_DIR=str(output))
if args.opengl:
    env.update(WR_VISIBILITY_GL="1", LIBGL_ALWAYS_SOFTWARE="1")


def close_host():
    class Data(C.Union):
        _fields_ = [("l", C.c_long * 5)]
    class Client(C.Structure):
        _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                    ("display", C.c_void_p), ("window", C.c_ulong), ("message_type", C.c_ulong),
                    ("format", C.c_int), ("data", Data)]
    class Event(C.Union):
        _fields_ = [("client", Client), ("pad", C.c_long * 24)]
    x = C.CDLL("libX11.so.6")
    x.XOpenDisplay.argtypes = [C.c_char_p]
    x.XOpenDisplay.restype = C.c_void_p
    display = x.XOpenDisplay(env["DISPLAY"].encode())
    assert display
    x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
    x.XInternAtom.restype = C.c_ulong
    window = int((output / "host-window.txt").read_text())
    event = Event()
    event.client.type, event.client.send_event, event.client.format = 33, 1, 32
    event.client.display, event.client.window = display, window
    event.client.message_type = x.XInternAtom(display, b"WM_PROTOCOLS", 0)
    event.client.data.l[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
    x.XSendEvent.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_long, C.POINTER(Event)]
    assert x.XSendEvent(display, window, 0, 0, C.byref(event))
    x.XFlush.argtypes = x.XCloseDisplay.argtypes = [C.c_void_p]
    x.XFlush(display)
    x.XCloseDisplay(display)


server = process = None
read_fd, write_fd = os.pipe()
try:
    server = subprocess.Popen([str(xvfb), "-displayfd", str(write_fd), "-screen", "0", "1450x1000x24", "-nolisten", "tcp"],
                              pass_fds=(write_fd,), stdout=(output / "xvfb.log").open("w"), stderr=subprocess.STDOUT, start_new_session=True)
    os.close(write_fd)
    with selectors.DefaultSelector() as selector:
        selector.register(read_fd, selectors.EVENT_READ)
        assert selector.select(10), "Xvfb did not start"
        number = os.read(read_fd, 128).decode().strip()
        assert number.isdigit()
    os.close(read_fd)
    env["DISPLAY"] = ":" + number
    process = subprocess.Popen([str(host), "--portable", "--configdir", str(profile), "--no_opengl"], cwd=profile,
                               env=env, stdout=(output / "stdout.log").open("w"), stderr=subprocess.STDOUT, start_new_session=True)
    deadline = time.monotonic() + 120
    while process.poll() is None and time.monotonic() < deadline and not (output / "result.json").exists():
        time.sleep(.1)
    assert (output / "result.json").is_file(), "No result; inspect profile/opencpn.log and stdout.log"
    result = json.loads((output / "result.json").read_text())
    close_host()
    process.wait(timeout=15)
    assert process.returncode == 0, process.returncode
    assert result["passed"], result
    print("PASS: " + ("route endpoint visibility before/during/after computation, selection/eye states and normal shutdown"
                       if args.probe == "route-visibility" else
                       "reset confirmation race, completed reset/recompute, cancellation, queued edit guards and normal shutdown"))
finally:
    for child in (process, server):
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
