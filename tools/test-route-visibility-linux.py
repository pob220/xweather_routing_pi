#!/usr/bin/env python3
"""Render real route endpoint marks in a disposable OpenCPN GTK/X11 host.

Accepts the same --build/--prefix/--host/--xvfb/--output arguments as the settings
reset host test. Add --opengl to check a private Mesa OpenGL canvas as well as
wxDC. The probe is never installed or distributed with the plugin.
"""
from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, str(Path(__file__).with_name("test-settings-reset-linux.py")),
                "--probe", "route-visibility", *sys.argv[1:]], check=True)
