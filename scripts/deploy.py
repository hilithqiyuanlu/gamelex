#!/usr/bin/env python3
"""Inspect an SD root and copy only manifest-listed GameLex files.

Default is a read-only dry run. No raw-disk access or partition operations.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
MODULE = "atmosphere/contents/42000000474C4558"
ALLOWED_FILES = {
    "switch/.overlays/gamelex-probe.ovl",
    "switch/gamelex-device-probe/gamelex-device-probe.nro",
    MODULE + "/exefs.nsp", MODULE + "/flags/boot2.flag",
    "config/gamelex/config.example.json", "config/gamelex/cacert.pem",
    "GAMELEX-PROBE-README.md",
}


def safe_destination(sd, relative):
    p = PurePosixPath(relative)
    if p.is_absolute() or ".." in p.parts or "\\" in relative:
        raise ValueError("路径超出安装范围。")
    destination = sd / relative
    for parent in [destination, *destination.parents]:
        if parent == sd:
            break
        if parent.is_symlink():
            raise ValueError(f"目标路径包含符号链接：{relative}")
    return destination


def plan(sd, package):
    if not sd.is_dir() or not (sd / "atmosphere").is_dir() or not (sd / "switch").is_dir():
        raise ValueError("目标不是已识别的 Switch SD 卡根目录；不会修改。")
    if sd.is_symlink() or package.is_symlink():
        raise ValueError("不允许符号链接作为卡或安装包根目录。")
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("schema_version") != 1 or manifest.get("program_id") != "42000000474C4558":
        raise ValueError("不支持的安装清单。")
    operations = []
    for relative, expected in manifest["files"].items():
        p = PurePosixPath(relative)
        if p.is_absolute() or ".." in p.parts or "\\" in relative or not (
            relative in ALLOWED_FILES or relative.startswith("config/gamelex/licenses/")
        ):
            raise ValueError("清单包含范围外的路径。")
        source = package / relative
        if not source.is_file() or source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"安装包文件校验失败：{relative}")
        destination = safe_destination(sd, relative)
        if destination.exists() and not destination.is_file():
            raise ValueError(f"目标文件类型冲突：{relative}")
        action = "replace" if destination.exists() else "create"
        operations.append((source, destination, relative, action))
    # Keep existing credentials untouched. A fresh installation gets the
    # non-networked example so screenshot probes work before a key is entered.
    configuration = sd / "config/gamelex/config.json"
    if not configuration.exists():
        if configuration.is_symlink() or (sd / "config/gamelex").is_symlink() or (sd / "config").is_symlink():
            raise ValueError("配置路径包含符号链接。")
        operations.append((package / "config/gamelex/config.example.json", configuration,
                           "config/gamelex/config.json", "create"))
    return operations


def install(sd, package, *, apply=False, enable=False):
    operations = plan(sd, package)
    for _, _, relative, action in operations:
        print(f"{'更新' if action == 'replace' else '新增'} {relative}")
    if not apply:
        print("只读检查完成，尚未写入 SD 卡。")
        return
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    backup = sd / "config/gamelex/backups" / stamp
    safe_destination(sd, backup.relative_to(sd).as_posix())
    changed = []
    flag = sd / MODULE / "flags/boot2.flag"
    if flag.is_symlink() or flag.parent.is_symlink():
        raise ValueError("模块标记路径包含符号链接。")
    if flag.exists() and not flag.is_file():
        raise ValueError("模块标记不是普通文件。")
    prior_flag = flag.is_file()
    try:
        # Back up every overwritten file before copying any replacement.
        for source, destination, relative, action in operations:
            if action == "replace":
                old = backup / relative
                old.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(destination, old)
        for source, destination, relative, action in operations:
            destination.parent.mkdir(parents=True, exist_ok=True)
            temp = destination.with_name(destination.name + ".gamelex-tmp")
            if temp.exists() or temp.is_symlink():
                raise ValueError(f"发现遗留的安装临时文件，请先检查：{temp.name}")
            try:
                shutil.copyfile(source, temp)
                if hashlib.sha256(temp.read_bytes()).hexdigest() != hashlib.sha256(source.read_bytes()).hexdigest():
                    raise OSError("写入后校验失败")
                temp.replace(destination)
                changed.append((destination, relative, action))
            finally:
                if temp.exists():
                    temp.unlink()
        if enable and not flag.exists():
            flag.parent.mkdir(parents=True, exist_ok=True)
            flag.touch(exist_ok=False)
    except Exception:
        if not prior_flag and flag.is_file():
            flag.unlink()
        for destination, relative, action in reversed(changed):
            if action == "replace":
                shutil.copy2(backup / relative, destination)
            else:
                destination.unlink(missing_ok=True)
        raise
    print("复制和校验完成。已有文件已备份；未修改用户 config.json。")
    print("后台模块已启用，将在重启后运行。" if enable or prior_flag else "后台模块未自动启用；当前可先运行设备诊断应用。")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sd", type=Path, required=True)
    p.add_argument("--package", type=Path, default=ROOT / "sd" if (ROOT / "sd").is_dir() else ROOT / "dist/phase1-sd")
    p.add_argument("--apply", action="store_true")
    p.add_argument("--enable-module", action="store_true")
    args = p.parse_args()
    try:
        install(args.sd.absolute(), args.package.absolute(), apply=args.apply, enable=args.enable_module)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(f"停止：{e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
