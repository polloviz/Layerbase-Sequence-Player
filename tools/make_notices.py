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
Intel Open Image Denoise
================================================================================
Intel Open Image Denoise is not part of Layerbase Sequence Player and is not
distributed with it. The first time its denoiser is used, the program asks
before downloading the official release (version 2.5.1) from
https://github.com/RenderKit/oidn into the user's local application data
folder, together with its LICENSE.txt and third-party notices. It is loaded
only while the denoiser is in use.
Copyright 2018 Intel Corporation - Apache License 2.0
  https://www.apache.org/licenses/LICENSE-2.0

================================================================================
NVIDIA OptiX
================================================================================
The OptiX denoiser support is built with the NVIDIA OptiX SDK 8.0 headers
(https://github.com/NVIDIA/optix-dev), Copyright (c) 2023 NVIDIA Corporation,
used under the NVIDIA SDK license; they are not part of this program's source
code. OptiX itself is provided by the NVIDIA display driver installed on the
user's computer and is subject to the NVIDIA driver license.

================================================================================
NVIDIA Optical Flow SDK headers
================================================================================
The motion estimation of the OptiX temporal denoiser is built with the NVIDIA
Optical Flow SDK interface headers
(https://github.com/NVIDIA/NVIDIAOpticalFlowSDK). NVIDIA Optical Flow itself is
provided by the NVIDIA display driver installed on the user's computer.

Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Trademarks
================================================================================
ACES is a trademark of the Academy of Motion Picture Arts and Sciences. Apple
and ProRes are trademarks of Apple Inc. OpenEXR and OpenColorIO are projects of
the Academy Software Foundation. FFmpeg is a trademark of Fabrice Bellard. Intel
is a trademark of Intel Corporation. NVIDIA and OptiX are trademarks of NVIDIA
Corporation. All other trademarks belong to their respective owners; their use
does not imply endorsement.
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
