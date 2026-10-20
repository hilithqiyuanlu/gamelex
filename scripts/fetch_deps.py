#!/usr/bin/env python3
"""Fetch hash-locked dependencies into this project, never install system-wide."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def extract_checked(archive, destination, strip=0):
    # Validate all archive names before invoking the platform's tar (including
    # zstd support on macOS). No extraction may escape the project tool folder.
    listing = subprocess.check_output(["tar", "-tf", str(archive)], text=True)
    for name in listing.splitlines():
        path = Path(name)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("Unsafe path in dependency archive")
    destination.mkdir(parents=True, exist_ok=True)
    command = ["tar", "-xf", str(archive), "-C", str(destination)]
    if strip:
        command += [f"--strip-components={strip}"]
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", action="store_true")
    parser.add_argument("--mac-sdk", action="store_true")
    args = parser.parse_args()
    if not (args.sources or args.mac_sdk):
        parser.error("Choose --sources or --mac-sdk")
    if args.mac_sdk and (platform.system() != "Darwin" or platform.machine() != "arm64"):
        parser.error("--mac-sdk is only for Apple Silicon macOS; use the pinned Docker image on other hosts")
    lock = json.loads((ROOT / "deps.lock.json").read_text())
    cache = ROOT / ".toolchain/downloads"
    cache.mkdir(parents=True, exist_ok=True)
    for entry in lock["archives"]:
        if entry["kind"] == "mac-sdk" and not args.mac_sdk:
            continue
        dest = ROOT / entry["destination"] if entry["kind"] == "source" else ROOT / ".toolchain/switch"
        marker = ROOT / ".toolchain/stamps" / entry["name"]
        if entry["kind"] == "source" and dest.is_dir():
            # Packaged source distributions already include these trees.
            continue
        if marker.is_file() and marker.read_text() == entry["sha256"] and dest.is_dir():
            continue
        filename = entry["url"].rsplit("/", 1)[1]
        archive = cache / filename
        if not archive.exists() or sha(archive) != entry["sha256"]:
            temp = archive.with_name(archive.name + ".part")
            command = ["curl", "-fsSL", "--retry", "2", "--connect-timeout", "15", "--max-time", "300"]
            if entry.get("referer"):
                command += ["--referer", entry["referer"]]
            command += [entry["url"], "-o", str(temp)]
            subprocess.run(command, check=True)
            if sha(temp) != entry["sha256"]:
                temp.unlink()
                raise ValueError(f"Dependency hash mismatch: {entry['name']}")
            temp.replace(archive)
        extract_checked(archive, dest, entry.get("strip_components", 0))
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(entry["sha256"])
        print("Prepared", entry["name"])


if __name__ == "__main__":
    main()
