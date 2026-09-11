#!/usr/bin/env python3
"""Recover only the nine reviewed 1.17.2 CI packages; never build or upload.

This recovery workflow is deliberately pinned, not a general artifact importer.
Public artifact URLs are used only in memory, never printed (they may be signed).
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import time
import urllib.request

PROJECT = "circleci/K9DC7cYmiJNgi5mJSq9gdA/GBpPDRQdh2Wv6WHqtEcfyn"
WORKFLOW = "24046c17-4209-4dc8-af63-7e3d7a7a38d9"
REVISION = "6c4d5b2a4870bf50e75dae7aa3bcfd6be34419fe"
JOBS = {
    "trixie": 368, "bookworm": 371, "jammy": 367, "noble": 370,
    "bookworm-arm64": 365, "flatpak-x86_64": 369,
    "flatpak-aarch64": 364, "windows-x86": 363, "macos-arm64": 366,
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


def check_source(job, revision=REVISION, workflow=WORKFLOW):
    if job.get("status") != "success":
        raise ValueError("Source job has not succeeded")
    if job.get("vcs_revision") != revision:
        raise ValueError("Source revision mismatch")
    if job.get("workflows", {}).get("workflow_id") != workflow:
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


def wait_for_builds(timeout, source_jobs=None):
    source_jobs = JOBS if source_jobs is None else source_jobs
    deadline = time.monotonic() + timeout
    while True:
        data = get_json(f"https://circleci.com/api/v2/workflow/{WORKFLOW}/job")
        jobs = {j.get("job_number"): j for j in data["items"]}
        states = {target: jobs.get(number, {}).get("status", "missing")
                  for target, number in source_jobs.items()}
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


def replacement_jammy(jobs, revision, workflow):
    if not re.fullmatch(r"[0-9a-f]{40}", revision) or not workflow:
        raise ValueError("Missing immutable recovery revision/workflow")
    matches = [j for j in jobs if j.get("name") == "ubuntu22.04-x86_64-recovery"]
    if len(matches) != 1 or matches[0].get("status") != "success":
        raise ValueError("Expected one successful Ubuntu 22.04 recovery job")
    number = matches[0].get("job_number")
    if not isinstance(number, int) or number <= 0:
        raise ValueError("Invalid recovery job number")
    return number, revision, workflow


def collect(output, timeout, recover_jammy=False):
    sources = {t: (n, REVISION, WORKFLOW) for t, n in JOBS.items()}
    retained = dict(JOBS)
    if recover_jammy:
        workflow = os.environ["CIRCLE_WORKFLOW_ID"]
        revision = os.environ["CIRCLE_SHA1"]
        data = get_json(f"https://circleci.com/api/v2/workflow/{workflow}/job")
        sources["jammy"] = replacement_jammy(data["items"], revision, workflow)
        del retained["jammy"]
    wait_for_builds(timeout, retained)
    # Check provenance of the entire matrix before downloading any binaries.
    for number, revision, workflow in sources.values():
        check_source(get_json(f"https://circleci.com/api/v1.1/project/{PROJECT}/{number}"),
                     revision, workflow)
    output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("prepare", Path(__file__).with_name("prepare-alpha-artifacts.py"))
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    records = []
    for target, (number, revision, workflow) in sources.items():
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
            records.append({"path": str(path), "sha256": digest, "job": number,
                            "revision": revision, "workflow": workflow})
        _, _, version, abi, _ = prepare.inspect_pair(directory)
        if version != "1.17.2.0":
            raise ValueError("Expected version 1.17.2.0")
        print(f"Verified {target}: {version}, {abi}", flush=True)
    (output / "source-provenance.json").write_text(json.dumps({
        "workflow": WORKFLOW, "revision": REVISION, "files": records}, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--wait-seconds", type=int, default=2700)
    parser.add_argument("--current-workflow-jammy", action="store_true")
    args = parser.parse_args()
    collect(args.output, args.wait_seconds, args.current_workflow_jammy)
