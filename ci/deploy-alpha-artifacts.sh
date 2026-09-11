#!/usr/bin/env bash
# Adapted from xGRIB's reviewed, parameter- and approval-gated deployment.
set -euo pipefail
set +x

if [[ -z "${CLOUDSMITH_API_KEY:-}" ]]; then
  echo "CLOUDSMITH_API_KEY is not configured in xweather-routing-deployment." >&2
  exit 2
fi

python3 ci/prepare-alpha-artifacts.py artifacts alpha-publication \
  "$(git rev-parse HEAD)" "${CIRCLE_BUILD_NUM:?CircleCI build number required}"

# Fixed destination: never deploy to xGRIB's repo or the standard WR repo.
python3 - <<'PY'
import json
import subprocess
from pathlib import Path

uploads = json.loads(Path("alpha-publication/uploads.json").read_text())
for item in uploads:
    subprocess.run([
        "cloudsmith", "push", "raw", "--republish", "--no-wait-for-sync",
        "--name", item["name"], "--version", item["version"],
        "--summary", "xWeatherRouting OpenCPN Alpha preview",
        "pob220/xweather-routing-alpha", item["file"],
    ], check=True)
PY
