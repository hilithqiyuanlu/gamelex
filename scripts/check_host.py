#!/usr/bin/env python3
"""Build and test portable core; HTTPS tests only contact localhost."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    cmake = shutil.which("cmake")
    bundled = ROOT / ".toolchain/host/bin/cmake"
    if bundled.is_file():
        cmake = str(bundled)
    if not cmake:
        venv.create(ROOT / ".toolchain/host", with_pip=True)
        run(ROOT / ".toolchain/host/bin/python", "-m", "pip", "install", "cmake==3.31.6")
        cmake = str(bundled)
    if not (ROOT / "vendor/libjpeg-turbo/CMakeLists.txt").is_file():
        run(sys.executable, "scripts/fetch_deps.py", "--sources")
    jpeg_build = ROOT / ".toolchain/jpeg-build"
    run(cmake, "-S", ROOT / "vendor/libjpeg-turbo", "-B", jpeg_build,
        "-DENABLE_SHARED=OFF", "-DENABLE_STATIC=ON", "-DWITH_TURBOJPEG=OFF", "-DWITH_SIMD=OFF", "-DCMAKE_BUILD_TYPE=Release")
    run(cmake, "--build", jpeg_build, "--target", "jpeg-static", "--parallel", "4")
    run(cmake, "-S", ROOT, "-B", ROOT / "build/host",
        f"-DJPEG_INCLUDE_DIR={ROOT / 'vendor/libjpeg-turbo/src'};{jpeg_build}", f"-DJPEG_LIBRARY={jpeg_build / 'libjpeg.a'}")
    run(cmake, "--build", ROOT / "build/host", "--parallel", "4")
    ctest = str(Path(cmake).with_name("ctest")) if Path(cmake).is_absolute() else "ctest"
    run(ctest, "--test-dir", ROOT / "build/host", "--output-on-failure")


if __name__ == "__main__":
    main()
