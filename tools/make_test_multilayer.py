"""Writes a small multi-layer EXR sequence with a Cryptomatte layer, for testing.
Layers: RGBA (beauty), diffuse.RGB, Z (float), CryptoObject00/01 (2 groups = 4 ranks).
Objects: "ball" (moving, anti-aliased edge), "floor", "sky".
Usage: python make_test_multilayer.py <out_dir> [frames] [width] [height]"""
import json
import math
import os
import struct
import sys

HALF, FLOAT = 1, 2


def attr(name, typ, data):
    return name.encode() + b"\0" + typ.encode() + b"\0" + struct.pack("<i", len(data)) + data


def fbits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]


OBJECTS = {"ball": 0.1234567, "floor": 0.7654321, "sky": 1.3579246}   # ids as float32 values
COLORS = {"ball": (0.8, 0.2, 0.1), "floor": (0.18, 0.18, 0.2), "sky": (0.3, 0.5, 0.9)}


def write_frame(path, w, h, f, frames):
    cx = w * (0.2 + 0.6 * f / max(1, frames - 1))
    cy = h * 0.45
    rad = h * 0.18
    horizon = h * 0.6
    chans = {}   # name -> (type, list of rows)
    names = ["A", "B", "G", "R", "Z", "diffuse.B", "diffuse.G", "diffuse.R"]
    for g in range(2):
        for c in "ABGR":
            names.append("CryptoObject%02d.%s" % (g, c))
    names.sort()
    types = {n: (FLOAT if n == "Z" or n.startswith("Crypto") else HALF) for n in names}
    rows = {n: [] for n in names}
    for y in range(h):
        line = {n: [] for n in names}
        for x in range(w):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            ball_cov = min(1.0, max(0.0, rad - d + 0.5))
            bg = "floor" if y >= horizon else "sky"
            covs = sorted([("ball", ball_cov), (bg, 1.0 - ball_cov)], key=lambda t: -t[1])
            col = [sum(COLORS[o][i] * c for o, c in covs) for i in range(3)]
            light = 1.0 + 3.0 * ball_cov * max(0.0, 1.0 - d / rad)   # HDR highlight
            line["R"].append(col[0] * light); line["G"].append(col[1] * light); line["B"].append(col[2] * light)
            line["A"].append(1.0)
            line["diffuse.R"].append(col[0]); line["diffuse.G"].append(col[1]); line["diffuse.B"].append(col[2])
            line["Z"].append(5.0 + (y / h) * 20.0 - ball_cov * 3.0)
            ranks = covs + [(None, 0.0), (None, 0.0)]
            for g in range(2):
                a, b = ranks[g * 2], ranks[g * 2 + 1]
                line["CryptoObject%02d.R" % g].append(OBJECTS[a[0]] if a[0] and a[1] > 0 else 0.0)
                line["CryptoObject%02d.G" % g].append(a[1])
                line["CryptoObject%02d.B" % g].append(OBJECTS[b[0]] if b[0] and b[1] > 0 else 0.0)
                line["CryptoObject%02d.A" % g].append(b[1])
        for n in names:
            fmt = "<%d%s" % (w, "f" if types[n] == FLOAT else "e")
            rows[n].append(struct.pack(fmt, *line[n]))

    chlist = b"".join(n.encode() + b"\0" + struct.pack("<iB3xii", types[n], 0, 1, 1) for n in names) + b"\0"
    manifest = json.dumps({k: "%08x" % fbits(v) for k, v in OBJECTS.items()})
    header = b"\x76\x2f\x31\x01" + struct.pack("<i", 2)
    header += attr("channels", "chlist", chlist)
    header += attr("compression", "compression", b"\0")
    header += attr("dataWindow", "box2i", struct.pack("<iiii", 0, 0, w - 1, h - 1))
    header += attr("displayWindow", "box2i", struct.pack("<iiii", 0, 0, w - 1, h - 1))
    header += attr("lineOrder", "lineOrder", b"\0")
    header += attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    header += attr("screenWindowCenter", "v2f", struct.pack("<ff", 0, 0))
    header += attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    header += attr("cryptomatte/a1b2c3d/name", "string", b"CryptoObject")
    header += attr("cryptomatte/a1b2c3d/hash", "string", b"MurmurHash3_32")
    header += attr("cryptomatte/a1b2c3d/conversion", "string", b"uint32_to_float32")
    header += attr("cryptomatte/a1b2c3d/manifest", "string", manifest.encode())
    header += b"\0"

    line_bytes = sum(w * (4 if types[n] == FLOAT else 2) for n in names)
    first = len(header) + 8 * h
    table = b"".join(struct.pack("<Q", first + y * (8 + line_bytes)) for y in range(h))
    blocks = b"".join(struct.pack("<ii", y, line_bytes) + b"".join(rows[n][y] for n in names) for y in range(h))
    with open(path, "wb") as fo:
        fo.write(header + table + blocks)


def main():
    out = sys.argv[1]
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 480
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 270
    os.makedirs(out, exist_ok=True)
    for f in range(frames):
        write_frame(os.path.join(out, "crypto_shot.%04d.exr" % (1001 + f)), w, h, f, frames)
    print("ok", frames)


if __name__ == "__main__":
    main()
