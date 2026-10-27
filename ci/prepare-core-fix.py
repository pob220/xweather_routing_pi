"""Prepare the exact upstream source plus the small USB watcher fix."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'core-source'
PIN = json.loads((ROOT / 'ci/windows64-wx334-sdk.json').read_text())
if not (CORE / '.git').exists():
    subprocess.run(['git', 'clone', '-c', 'core.autocrlf=false', '--depth', '1',
                    'https://github.com/OpenCPN/OpenCPN.git', CORE], check=True)
revision = subprocess.check_output(['git', '-C', CORE, 'rev-parse', 'HEAD'], text=True).strip()
assert revision == PIN['core_revision'], (revision, PIN['core_revision'])
patch = ROOT / 'ci/core-shutdown.patch'
subprocess.run(['git', '-C', CORE, 'apply', '--check', patch], check=True)
subprocess.run(['git', '-C', CORE, 'apply', patch], check=True)
print('Prepared pinned source and USB watcher patch ' + hashlib.sha256(patch.read_bytes()).hexdigest(), flush=True)
