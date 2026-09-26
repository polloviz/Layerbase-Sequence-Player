"""Generates res/THIRD_PARTY_NOTICES.txt from the vcpkg copyright files of the
libraries linked into SequencePlayer.exe, plus a few manual attributions.

Usage: python tools/make_notices.py [vcpkg_root]   (default: $VCPKG_ROOT or ~/vcpkg)"""
import os
import sys

PORTS = [
    ("OpenColorIO", "opencolorio"),
    ("OpenEXR", "openexr"),
    ("Imath", "imath"),
    ("OpenJPH", "openjph"),
    ("libdeflate", "libdeflate"),
    ("LibTIFF", "tiff"),
    ("libjpeg-turbo", "libjpeg-turbo"),
    ("XZ Utils (liblzma)", "liblzma"),
    ("zlib", "zlib"),
    ("Zstandard", "zstd"),
    ("bzip2", "bzip2"),
    ("Expat", "expat"),
    ("yaml-cpp", "yaml-cpp"),
    ("pystring", "pystring"),
    ("minizip-ng", "minizip-ng"),
    ("GLEW", "glew"),
    ("Dear ImGui", "imgui"),
    ("stb", "stb"),
]

MANUAL = """
================================================================================
AgX view transform (built-in)
================================================================================
The built-in AgX rendering uses the matrices, log range and polynomial sigmoid
published by Troy Sobotka (AgX), as adapted by the Blender project and by
Google Filament (Apache License 2.0) and three.js (MIT License).

three.js - Copyright 2010-2025 three.js authors - MIT License
Filament - Copyright (C) 2015 The Android Open Source Project - Apache License 2.0
  https://www.apache.org/licenses/LICENSE-2.0

================================================================================
ACES / OpenColorIO built-in configs
================================================================================
The ACES Studio and CG configs are embedded in OpenColorIO and are
Copyright Contributors to the OpenColorIO Project (BSD-3-Clause).
ACES is a trademark of the Academy of Motion Picture Arts and Sciences.

================================================================================
Cryptomatte
================================================================================
Cryptomatte decoding follows the public Cryptomatte specification
(Psyop, BSD-3-Clause): https://github.com/Psyop/Cryptomatte

================================================================================
FFmpeg
================================================================================
FFmpeg is a separate program, not part of Layerbase Sequence Player. The
ffmpeg.exe distributed with it (FFmpeg n8.1.3, GPL build including x264 and
x265) is licensed under the GNU General Public License version 3: see
FFMPEG_LICENSE.txt and FFMPEG_README.txt (version, source code and build
scripts) in the program folder. https://ffmpeg.org/legal.html

================================================================================
Trademarks
================================================================================
ACES is a trademark of the Academy of Motion Picture Arts and Sciences.
Apple and ProRes are trademarks of Apple Inc. OpenEXR and OpenColorIO are
projects of the Academy Software Foundation. FFmpeg is a trademark of Fabrice
Bellard. All other trademarks belong to their respective owners; their use does
not imply endorsement.
"""


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("VCPKG_ROOT", os.path.expanduser("~/vcpkg"))
    share = os.path.join(root, "installed", "x64-windows-static", "share")
    out = []
    out.append("Layerbase Sequence Player - Third-party notices\n")
    out.append("Layerbase Sequence Player (c) Layerbase Luxury Vision - https://layerbase.it\n")
    out.append("This program includes the following third-party software, used under the\n"
               "license terms reproduced below.\n")
    for title, port in PORTS:
        path = os.path.join(share, port, "copyright")
        if not os.path.exists(path):
            print("warning: missing", path)
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read().strip()
        out.append("\n" + "=" * 80 + "\n" + title + "\n" + "=" * 80 + "\n" + text + "\n")
    out.append(MANUAL)
    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "THIRD_PARTY_NOTICES.txt")
    with open(dst, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("".join(out))
    print("wrote", os.path.abspath(dst), sum(len(s) for s in out), "chars")


if __name__ == "__main__":
    main()
