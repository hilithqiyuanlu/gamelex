#!/usr/bin/env python3
"""Ship corresponding source, including vendored libraries, with no credentials."""
from pathlib import Path
import shutil
import zipfile
from release_info import release_version

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {"build", "__pycache__", ".git", ".pics", ".DS_Store", "__MACOSX"}
BINARY_SUFFIXES = {".elf", ".ovl", ".nro", ".nsp", ".nso", ".npdm", ".nacp", ".map", ".lst", ".o", ".a", ".pyc"}
TOP = ["README.md", "CONTRIBUTING.md", "LICENSE", "THIRD_PARTY.md", "Makefile", "CMakeLists.txt", "deps.lock.json", ".gitignore"]


def main():
    dest = ROOT / "dist/gamelex-source.zip"
    dest.parent.mkdir(exist_ok=True)
    files = [ROOT / name for name in TOP]
    for directory in ["core", "switch", "scripts", "tests", "docs", "vendor", ".github"]:
        base = ROOT / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.is_symlink() or path.suffix in BINARY_SUFFIXES:
                continue
            if any(part in EXCLUDED for part in path.relative_to(base).parts):
                continue
            files.append(path)
    # Explicit allowlist: never glob config/ where a developer may have a key.
    files += [ROOT / "config/config.example.json", ROOT / "config/cacert.pem"]
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(set(files)):
            archive.write(path, "gamelex/" + path.relative_to(ROOT).as_posix())
    print(dest)
    versioned = dest.with_name(f"gamelex-{release_version()}-source.zip")
    shutil.copyfile(dest, versioned)
    print(versioned)


if __name__ == "__main__":
    main()
