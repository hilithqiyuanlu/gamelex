#!/usr/bin/env python3
"""Reversibly remove only GameLex executables. Keep settings and user data."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import shutil
import sys
from deploy import MODULE, safe_destination

FILES = (
    MODULE + "/flags/boot2.flag",  # Remove startup first; preserve it in backup.
    MODULE + "/exefs.nsp",
    "switch/.overlays/gamelex-probe.ovl",
    "switch/gamelex-device-probe/gamelex-device-probe.nro",
)


def uninstall(sd, *, apply=False):
    if sd.is_symlink() or not (sd / "atmosphere").is_dir() or not (sd / "switch").is_dir():
        raise ValueError("目标不是 Switch SD 卡根目录。")
    files = []
    for relative in FILES:
        source = safe_destination(sd, relative)
        if source.exists():
            if not source.is_file():
                raise ValueError("程序文件类型不符，已停止卸载。")
            files.append((source, relative))
    if not files:
        print("没有发现 GameLex 程序文件；用户数据保持原样。")
        return
    for _, relative in files:
        print("移出程序：" + relative)
    if not apply:
        print("只读预览。添加 --apply 执行；配置和用户数据会保留。")
        return
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    relative_backup = "config/gamelex/backups/uninstall-" + stamp
    backup = safe_destination(sd, relative_backup)
    # Copy and verify every original before removing any executable.
    for source, relative in files:
        saved = backup / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, saved)
        if saved.read_bytes() != source.read_bytes():
            raise OSError("卸载备份校验失败，尚未移除程序。")
    removed = []
    try:
        for source, relative in files:
            source.unlink()
            removed.append((source, relative))
    except OSError:
        for source, relative in reversed(removed):
            shutil.copy2(backup / relative, source)
        raise
    print("已移出 GameLex 程序。请完整重启；设置和数据已保留。")
    print("可恢复的备份：" + relative_backup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        uninstall(args.sd.absolute(), apply=args.apply)
    except (OSError, ValueError) as error:
        print("停止：" + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
