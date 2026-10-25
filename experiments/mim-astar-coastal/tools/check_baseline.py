#!/usr/bin/env python3
"""Verify that neither the first experiment nor the shared engines changed."""
from pathlib import Path
import hashlib
import json

experiment = Path(__file__).resolve().parents[1]
root = experiment.parents[1]
baseline = json.loads((experiment / 'baseline.json').read_text())
failed = []
for name, expected in baseline['files'].items():
    file = root / name
    if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != expected:
        failed.append(name)
if failed:
    raise SystemExit('Protected source changed: ' + ', '.join(failed))
print(f"PASS: all {len(baseline['files'])} protected first-experiment/shared files are unchanged")
