#!/usr/bin/env python3
"""Collect only the nine reviewed 1.17.3 CI packages; never build or upload.

This recovery workflow is deliberately pinned, not a general artifact importer.
Public artifact URLs are used only in memory, never printed (they may be signed).
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import time
import urllib.request

PROJECT = "circleci/K9DC7cYmiJNgi5mJSq9gdA/GBpPDRQdh2Wv6WHqtEcfyn"
WORKFLOW = "03531061-68fa-4345-b646-2e96a8260d96"
REVISION = "ac541342b73191e065ead7354f113d80aae3b7b0"
JOBS = {
    "trixie": 573, "bookworm": 572, "jammy": 578, "noble": 575,
    "bookworm-arm64": 574, "flatpak-x86_64": 576,
    "flatpak-aarch64": 571, "windows-x86": 577, "macos-arm64": 579,
}


def get_json(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                return json.load(response)
        except (OSError, ValueError):
            if attempt == 2:
                raise RuntimeError("CircleCI API request failed after three attempts") from None
            time.sleep(3)


def check_source(job):
    if job.get("status") != "success":
        raise ValueError("Source job has not succeeded")
    if job.get("vcs_revision") != REVISION:
        raise ValueError("Source revision mismatch")
    if job.get("workflows", {}).get("workflow_id") != WORKFLOW:
        raise ValueError("Source workflow mismatch")


def select_artifacts(target, items):
    prefix = "windows-x86/" if target == "windows-x86" else f"artifacts/{target}/package/"
    selected = [a for a in items if a["path"].startswith(prefix)
                and "/" not in a["path"][len(prefix):]
                and a["path"].endswith((".tar.gz", ".xml"))]
    if (len(selected) != 2 or
            sum(a["path"].endswith(".tar.gz") for a in selected) != 1 or
            sum(a["path"].endswith(".xml") for a in selected) != 1):
        raise ValueError(f"{target}: expected exactly one archive and metadata pair")
    if any(not a["url"].startswith("https://") for a in selected):
        raise ValueError("Artifact URL is not HTTPS")
    return selected


def wait_for_builds(timeout):
    deadline = time.monotonic() + timeout
    while True:
        data = get_json(f"https://circleci.com/api/v2/workflow/{WORKFLOW}/job")
        jobs = {j.get("job_number"): j for j in data["items"]}
        states = {target: jobs.get(number, {}).get("status", "missing")
                  for target, number in JOBS.items()}
        if any(s in {"failed", "canceled", "not_run", "unauthorized", "missing"}
               for s in states.values()):
            raise RuntimeError(f"Source builds cannot be published: {states}")
        if all(s == "success" for s in states.values()):
            return
        print("Waiting for existing builds: " + ", ".join(
            f"{t}={s}" for t, s in states.items() if s != "success"), flush=True)
        if time.monotonic() >= deadline:
            raise RuntimeError("Timed out waiting for existing builds; no rebuild was started")
        time.sleep(30)


def collect(output, timeout):
    wait_for_builds(timeout)
    # Check provenance of the entire matrix before downloading any binaries.
    for number in JOBS.values():
        check_source(get_json(f"https://circleci.com/api/v1.1/project/{PROJECT}/{number}"))
    output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("prepare", Path(__file__).with_name("prepare-alpha-artifacts.py"))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    records = []
    for target, number in JOBS.items():
        items = get_json(f"https://circleci.com/api/v2/project/{PROJECT}/{number}/artifacts")["items"]
        directory = output / target / "package"
        directory.mkdir(parents=True)
        for item in select_artifacts(target, items):
            path = directory / Path(item["path"]).name
            for attempt in range(3):
                try:
                    with urllib.request.urlopen(item["url"], timeout=60) as response, path.open("wb") as destination:
                        while chunk := response.read(1024 * 1024):
                            destination.write(chunk)
                    break
                except OSError:
                    if attempt == 2:
                        raise RuntimeError(f"Artifact download failed: {target}/{path.name}") from None
                    time.sleep(3)
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            records.append({"path": str(path), "sha256": digest, "job": number})
        _, _, version, abi, _ = prepare.inspect_pair(directory)
        if version != "1.17.3.0":
            raise ValueError("Expected version 1.17.3.0")
        print(f"Verified {target}: {version}, {abi}", flush=True)
    (output / "source-provenance.json").write_text(json.dumps({
        "workflow": WORKFLOW, "revision": REVISION, "files": records}, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--wait-seconds", type=int, default=2700)
    args = parser.parse_args()
    collect(args.output, args.wait_seconds)
