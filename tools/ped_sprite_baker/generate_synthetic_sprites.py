#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path

STATES = [
    "idle",
    "walk",
    "run",
    "sprint",
    "crouch",
    "attack",
    "firearm",
    "hit",
    "death",
    "car_sit",
    "bike_ride",
    "enter_exit",
]


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path):
    width = 1
    height = 1
    raw = b"\x00\xff\xff\xff\xff"
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(raw))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description="Generate synthetic ped sprite assets for CI build validation only.")
    parser.add_argument("--output", required=True)
    parser.add_argument("--model", type=int, default=0)
    parser.add_argument("--name", default="synthetic_player")
    args = parser.parse_args()

    root = Path(args.output)
    ped_dir = root / "sprites" / "peds"
    ped_dir.mkdir(parents=True, exist_ok=True)
    write_png(ped_dir / "synthetic.png")

    lines = [
        f"model {args.model} {args.name}",
        "atlas synthetic sprites/peds/synthetic.png 1 1",
    ]
    for state in STATES:
        for direction in range(8):
            lines.append(f"frame {args.model} {state} {direction} 0 100 synthetic 0 0 1 1 0.5 1.0 1.8")
    (ped_dir / "manifest.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
