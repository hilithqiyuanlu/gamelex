#!/usr/bin/env python3
"""Create a probe-only SD package; reject missing/invalid Switch binaries."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile
from release_info import release_version, protocol_version

ROOT = Path(__file__).resolve().parents[1]
PROGRAM_ID = 0x42000000474C4558
DIST = ROOT / "dist"
RELEASE = release_version()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_nro(path):
    data = path.read_bytes()
    if len(data) < 0x80 or data[0x10:0x14] != b"NRO0":
        raise ValueError(f"Not an NRO/OVL: {path}")
    declared = struct.unpack_from("<I", data, 0x18)[0]
    if declared > len(data) or declared < 0x80:
        raise ValueError(f"Invalid NRO size: {path}")
    if declared + 0x38 > len(data) or data[declared:declared+4] != b"ASET":
        raise ValueError(f"Missing NRO assets: {path}")
    offset, size = struct.unpack_from("<QQ", data, declared + 24)
    if size < 0x3070 or declared + offset + size > len(data):
        raise ValueError(f"Invalid NACP: {path}")
    version = data[declared+offset+0x3060:declared+offset+0x3070].split(b"\0",1)[0].decode("ascii")
    if version != RELEASE:
        raise ValueError(f"NRO version {version} does not match release {RELEASE}: {path}")


def check_nsp(path):
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"PFS0":
        raise ValueError(f"Not an ExeFS PFS0: {path}")
    count, strings_length = struct.unpack_from("<II", data, 4)
    strings_start = 16 + 24 * count
    content_start = strings_start + strings_length
    files = {}
    for i in range(count):
        offset, size, name_offset = struct.unpack_from("<QQI", data, 16 + 24 * i)
        end = data.index(0, strings_start + name_offset, content_start)
        name = data[strings_start + name_offset:end].decode("utf-8")
        files[name] = data[content_start + offset:content_start + offset + size]
        if len(files[name]) != size:
            raise ValueError("Truncated ExeFS member")
    if set(files) != {"main", "main.npdm"} or files["main"][:4] != b"NSO0":
        raise ValueError("ExeFS is missing the actual NSO/NPDM")
    meta = files["main.npdm"]
    if meta[:4] != b"META":
        raise ValueError("Missing NPDM metadata")
    aci = struct.unpack_from("<I", meta, 0x70)[0]
    if meta[aci:aci + 4] != b"ACI0" or struct.unpack_from("<Q", meta, aci + 16)[0] != PROGRAM_ID:
        raise ValueError("NPDM program ID does not match installation directory")
    # Validate screenshot and TLS service permissions made it into the binary.
    for service in (b"caps:sc", b"ssl", b"pm:info", b"csrng"):
        if service not in meta:
            raise ValueError(f"Missing service permission: {service!r}")


def main():
    overlay = ROOT / "switch/overlay/gamelex-probe.ovl"
    app = ROOT / "switch/diagnostics/gamelex-device-probe.nro"
    module = ROOT / "switch/sysmodule/gamelex-sys.nsp"
    check_nro(overlay)
    check_nro(app)
    check_nsp(module)
    target = DIST / "phase1-sd"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    members = {
        "switch/.overlays/gamelex-probe.ovl": overlay,
        "switch/gamelex-device-probe/gamelex-device-probe.nro": app,
        "atmosphere/contents/42000000474C4558/exefs.nsp": module,
        "config/gamelex/config.example.json": ROOT / "config/config.example.json",
        "config/gamelex/cacert.pem": ROOT / "config/cacert.pem",
        "GAMELEX-PROBE-README.md": ROOT / "docs/实机测试.md",
    }
    for relative, source in members.items():
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    # Kept outside the package until the installer is explicitly given
    # --enable-module. Merely copying this package cannot enable a boot module.
    manifest = {
        "schema_version": 1,
        "release": RELEASE,
        "hardware_validation": "pending",
        "program_id": f"{PROGRAM_ID:016X}",
        "auto_start_included": False,
        "protocol_version": protocol_version(),
        "files": {name: digest(target / name) for name in members},
    }
    licenses = target / "config/gamelex/licenses"
    licenses.mkdir(parents=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "GameLex-GPL-2.0.txt")
    shutil.copy2(ROOT / "THIRD_PARTY.md", licenses / "THIRD_PARTY.md")
    shutil.copy2(ROOT / "vendor/JSON-LICENSE.MIT", licenses / "JSON-MIT.txt")
    shutil.copy2(ROOT / "vendor/libultrahand/SUB_LICENSE", licenses / "libultrahand-SUB_LICENSE.txt")
    shutil.copy2(ROOT / "vendor/libultrahand/LICENSE", licenses / "libultrahand-GPL-2.0.txt")
    for name in ["CURL-COPYING", "LIBNX-LICENSE", "Switch-OCR-LICENSE", "LIBJPEG-LICENSE", "ZLIB-LICENSE", "MBEDTLS-LICENSE"]:
        shutil.copy2(ROOT / "vendor" / name, licenses / name)
    # Archive copies of every linked library's license from the pinned SDK.
    sdk = ROOT / ".toolchain/switch/opt/devkitpro"
    for parent in (sdk / "portlibs/switch/licenses", sdk / "portlibs/switch/share/licenses"):
        if parent.is_dir():
            shutil.copytree(parent, licenses / parent.name, dirs_exist_ok=True)
    manifest["files"] = {p.relative_to(target).as_posix(): digest(p) for p in sorted(target.rglob("*")) if p.is_file()}
    (target / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    DIST.mkdir(exist_ok=True)
    with zipfile.ZipFile(DIST / f"gamelex-{RELEASE}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(target.rglob("*")):
            if p.is_file():
                archive.write(p, p.relative_to(target))
    print(f"M1 package: {DIST / f'gamelex-{RELEASE}.zip'}")
    print("Compiled and format-checked. Hardware gate: PENDING. No boot2.flag included.")


if __name__ == "__main__":
    main()
