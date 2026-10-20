#!/usr/bin/env python3
"""Assemble a self-contained trial: SD files, tools, instructions and source."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile
from release_info import ROOT, release_version, protocol_version
from deploy import ALLOWED_FILES

MODULE = "atmosphere/contents/42000000474C4558"
SECRETS = ("api_key", "ocr_api_key", "ocr_secret", "translate_appid", "translate_key")


def private_values():
    values = []
    for name in ("config/config.local.json", "config/config.json"):
        path = ROOT / name
        if path.is_file():
            try:
                config = json.loads(path.read_text())
                values += [config[k].encode() for k in SECRETS if isinstance(config.get(k), str) and config[k]]
            except (ValueError, OSError, AttributeError):
                raise ValueError("私有配置无法读取，停止发布检查。") from None
    return values


def audit(name, data, secrets):
    parts = PurePosixPath(name).parts
    if any(part in {"runtime", "reports", "backups", ".backups", "__MACOSX", ".DS_Store"} or part.startswith("._") for part in parts):
        raise ValueError("发布包混入运行记录或个人文件：" + name)
    if PurePosixPath(name).name == "config.json" or ".local." in name or name.endswith(("last-capture.jpg", "last-ocr.json")):
        raise ValueError("发布包混入私有配置或截图：" + name)
    if any(value in data for value in secrets):
        raise ValueError("发布包发现私有密钥，已停止。")


def build_trial(sd_source, source_archive, output):
    version = release_version()
    manifest = json.loads((sd_source / "manifest.json").read_text())
    if manifest.get("release") != version or manifest.get("protocol_version") != protocol_version() or manifest.get("program_id") != "42000000474C4558":
        raise ValueError("安装包版本或模块编号不匹配。")
    secrets = private_values()
    members = {}
    for name, expected in manifest["files"].items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or not (
            name in ALLOWED_FILES or name.startswith("config/gamelex/licenses/")
        ):
            raise ValueError("安装清单路径无效。")
        data = (sd_source / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("安装包校验失败：" + name)
        audit(name, data, secrets)
        members["sd/" + name] = data
    # The trial explicitly uses the same boot-start arrangement as the tested
    # device. Do not silently depend on unverified on-demand startup elsewhere.
    flag = MODULE + "/flags/boot2.flag"
    members["sd/" + flag] = b""
    manifest["files"][flag] = hashlib.sha256(b"").hexdigest()
    manifest["auto_start_included"] = True
    manifest["distribution"] = "friend-trial"
    guide = (ROOT / "docs/同学试用.md").read_bytes()
    members["先读我.md"] = guide
    members["反馈模板.md"] = (ROOT / "docs/试用反馈.md").read_bytes()
    members["sd/GAMELEX-PROBE-README.md"] = guide
    manifest["files"]["GAMELEX-PROBE-README.md"] = hashlib.sha256(guide).hexdigest()
    for script in ("deploy.py", "uninstall.py"):
        members["tools/" + script] = (ROOT / "scripts" / script).read_bytes()
    source = source_archive.read_bytes()
    with zipfile.ZipFile(io.BytesIO(source)) as archive:
        for name in archive.namelist():
            audit(name, archive.read(name), secrets)
    members[f"gamelex-{version}-source.zip"] = source
    members["sd/manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()
    sums = []
    for name, data in sorted(members.items()):
        audit(name, data, secrets)
        sums.append(hashlib.sha256(data).hexdigest() + "  " + name)
    members["SHA256SUMS.txt"] = ("\n".join(sums) + "\n").encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    pending = output.with_name(output.name + ".tmp")
    try:
        with zipfile.ZipFile(pending, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(members.items()):
                archive.writestr(name, data)
        pending.replace(output)
    finally:
        pending.unlink(missing_ok=True)
    checksum = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".zip.sha256").write_text(checksum + "  " + output.name + "\n")
    return manifest


def main():
    version = release_version()
    output = ROOT / "dist" / f"gamelex-{version}-trial.zip"
    manifest = build_trial(ROOT / "dist/phase1-sd", ROOT / "dist" / f"gamelex-{version}-source.zip", output)
    print(output)
    print(f"PASS {len(manifest['files'])} SD files; source, tools, checksums and blank credentials included.")
    print("GameLex backend autostart is included; no Ultrahand or loader files are replaced. NS 0.3.0 validation is pending.")


if __name__ == "__main__":
    main()
