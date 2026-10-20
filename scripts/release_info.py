"""The native components and all distribution scripts share this version."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def release_version():
    header = (ROOT / "core/include/gamelex/release.hpp").read_text()
    match = re.search(r'ReleaseVersion\[\] = "(\d+\.\d+\.\d+)";', header)
    if not match:
        raise ValueError("Missing release version")
    return match[1]


def protocol_version():
    header = (ROOT / "core/include/gamelex/release.hpp").read_text()
    match = re.search(r'ProtocolVersion = (\d+);', header)
    if not match:
        raise ValueError("Missing protocol version")
    return int(match[1])
