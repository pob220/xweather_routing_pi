#!/usr/bin/env python3
"""Validate the complete matrix before staging any Cloudsmith uploads.

Uses xGRIB's same-version XML pairing (including Flatpak's different basename)
and public raw-package URL convention. Does not upload or access credentials.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import xml.etree.ElementTree as ET


TARGETS = {
    "trixie", "bookworm", "jammy", "noble", "bookworm-arm64",
    "flatpak-x86_64", "flatpak-aarch64", "windows-x86", "macos-arm64",
}
PACKAGE = "xweather_routing_pi"
REPOSITORY = "pob220/xweather-routing-alpha"


def value(root, field):
    text = (root.findtext(field) or "").strip()
    if not text:
        raise ValueError(f"Missing metadata field: {field}")
    return text


def inspect_pair(directory):
    archives = list(directory.glob("*.tar.gz"))
    if len(archives) != 1:
        raise ValueError(f"Expected exactly one archive: {directory}")
    archive = archives[0]
    if not archive.name.startswith(PACKAGE + "-"):
        raise ValueError(f"Wrong archive identity: {archive.name}")
    metadata = archive.with_name(archive.name[:-7] + ".xml")
    if not metadata.is_file():
        # Flatpak metadata deliberately has a different basename.
        match = re.match(r"xweather_routing_pi-(\d+\.\d+\.\d+\.\d+)-", archive.name)
        candidates = list(directory.glob(f"{PACKAGE}-{match[1]}-*.xml")) if match else []
        if len(candidates) != 1:
            raise ValueError(f"No unique same-version XML: {archive}")
        metadata = candidates[0]
    root = ET.parse(metadata).getroot()
    if root.tag != "plugin" or value(root, "name") != "xWeatherRouting":
        raise ValueError(f"Wrong plugin name: {metadata}")
    if value(root, "api-version") != "1.21":
        raise ValueError(f"Unexpected stock API requirement: {metadata}")
    if value(root, "source") != "https://github.com/pob220/xweather_routing_pi":
        raise ValueError(f"Wrong source repository: {metadata}")
    version = value(root, "version")
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", version) or not archive.name.startswith(f"{PACKAGE}-{version}-"):
        raise ValueError(f"Archive/XML version mismatch: {archive}")
    target = tuple(value(root, key) for key in ("target", "target-version", "target-arch"))
    if any(not re.fullmatch(r"[\w.+-]+", item) for item in target):
        raise ValueError(f"Invalid target: {target}")
    if target[0].startswith("flatpak-") and target[0] not in {"flatpak-x86_64", "flatpak-aarch64"}:
        raise ValueError(f"Invalid Flatpak catalogue target: {target[0]}")
    if target[1] == "22.04" and target[0] != "ubuntu-wx32-x86_64":
        raise ValueError(f"Missing Ubuntu 22.04 wxWidgets ABI marker: {target[0]}")
    library_names = {f"lib{PACKAGE}.so", f"lib{PACKAGE}.dylib", f"{PACKAGE}.dll"}
    with tarfile.open(archive, "r:gz") as package:
        members = package.getmembers()
        if any(PurePosixPath(m.name).is_absolute() or ".." in PurePosixPath(m.name).parts for m in members):
            raise ValueError(f"Unsafe archive path: {archive}")
        if sum(m.isfile() and PurePosixPath(m.name).name in library_names for m in members) != 1:
            raise ValueError(f"Missing/duplicate standalone library: {archive}")
        if any(re.match(r"(?:lib)?(?:weather_routing_pi\.(?:so|dylib|dll)|gtest|gmock)", PurePosixPath(m.name).name) for m in members):
            raise ValueError(f"Standard plugin or test library included: {archive}")
    return archive, root, version, target, metadata.name


def prepare(artifact_root, output, revision, build_number):
    if not re.fullmatch(r"[0-9a-f]{7,40}", revision) or not re.fullmatch(r"\d+", build_number):
        raise ValueError("Expected commit SHA and numeric CircleCI build number")
    directories = {p.parent.name: p for p in artifact_root.glob("*/package")}
    if set(directories) != TARGETS:
        raise ValueError(f"Incomplete/unexpected matrix: {sorted(directories)}")
    # Finish all validation before writing a manifest or allowing any upload.
    pairs = [inspect_pair(directories[name]) for name in sorted(TARGETS)]
    if len({p[2] for p in pairs}) != 1 or len({p[3] for p in pairs}) != len(TARGETS):
        raise ValueError("Mixed versions or duplicate platform metadata")
    output.mkdir(parents=True, exist_ok=False)
    uploads = []
    for archive, root, version, target, metadata_name in pairs:
        cloud_version = f"{version}+{build_number}.{revision[:7]}"
        base = f"{PACKAGE}-{version}-{'-'.join(target)}"
        url = f"https://dl.cloudsmith.io/public/{REPOSITORY}/raw/names/{base}-tarball/versions/{cloud_version}/{archive.name}"
        root.find("tarball-url").text = "\n    " + url + "\n  "
        staged = output / metadata_name
        if staged.exists():
            raise ValueError(f"Duplicate metadata filename: {metadata_name}")
        ET.ElementTree(root).write(staged, encoding="utf-8", xml_declaration=True)
        for name, file in ((base + "-metadata", staged), (base + "-tarball", archive)):
            with file.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            uploads.append({"name": name, "version": cloud_version,
                            "file": str(file.resolve()),
                            "sha256": digest})
    (output / "uploads.json").write_text(json.dumps(uploads, indent=2) + "\n")
    print(f"Validated {len(pairs)} xWeatherRouting platform pairs; {len(uploads)} uploads staged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("revision")
    parser.add_argument("build_number")
    args = parser.parse_args()
    prepare(args.artifact_root, args.output, args.revision, args.build_number)
