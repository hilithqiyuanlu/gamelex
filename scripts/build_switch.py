#!/usr/bin/env python3
"""Build the three phase-one Switch components using an existing SDK."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    env = os.environ.copy()
    sdk = Path(env.get("DEVKITPRO", ROOT / ".toolchain/switch/opt/devkitpro"))
    if not (sdk / "devkitA64/bin/aarch64-none-elf-g++").is_file():
        raise SystemExit("Switch SDK missing. Run scripts/fetch_deps.py --mac-sdk, or use the pinned Docker image.")
    if not (ROOT / "vendor/libultrahand/ultrahand.mk").is_file():
        raise SystemExit("Run scripts/fetch_deps.py --sources first.")
    env["DEVKITPRO"] = str(sdk.resolve())
    for lib in ["curl", "jpeg", "minizip", "mbedtls", "mbedx509", "mbedcrypto", "z"]:
        if not (sdk / f"portlibs/switch/lib/lib{lib}.a").is_file():
            raise SystemExit(f"Missing Switch library: {lib}")
    for component in ["sysmodule", "overlay", "diagnostics"]:
        subprocess.run(["make", "-C", str(ROOT / "switch" / component), "-j", "4"], env=env, check=True)
    print("Three Switch binaries compiled. Hardware validation is still required.")


if __name__ == "__main__":
    main()
