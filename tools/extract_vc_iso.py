#!/usr/bin/env python3
"""Extract a user-owned GTA Vice City ISO into a local asset root.

This intentionally implements the small ISO 9660/Joliet subset needed for
retail game data discs so the project workflow does not depend on 7z/bsdtar.
It does not bypass copy protection and it does not ship any game assets.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys
from dataclasses import dataclass


SECTOR_SIZE = 2048
PVD_SECTOR = 16
REQUIRED_FILES = (
    "MODELS/GTA3.IMG",
    "DATA/DEFAULT.DAT",
    "DATA/ANIMVIEWER.DAT",
)


@dataclass(frozen=True)
class IsoEntry:
    path: str
    extent: int
    size: int
    is_dir: bool


def read_sector(f, sector: int) -> bytes:
    f.seek(sector * SECTOR_SIZE)
    data = f.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE:
        raise ValueError(f"could not read ISO sector {sector}")
    return data


def parse_record(data: bytes, offset: int) -> tuple[IsoEntry | None, int]:
    length = data[offset]
    if length == 0:
        return None, offset + 1
    record = data[offset : offset + length]
    if len(record) < 34:
        raise ValueError("bad ISO directory record")

    extent = struct.unpack_from("<I", record, 2)[0]
    size = struct.unpack_from("<I", record, 10)[0]
    flags = record[25]
    name_len = record[32]
    raw_name = record[33 : 33 + name_len]
    try:
        name = raw_name.decode("utf-8")
    except UnicodeDecodeError:
        name = raw_name.decode("latin-1")
    if name == "\x00":
        name = "."
    elif name == "\x01":
        name = ".."
    else:
        if ";" in name:
            name = name.split(";", 1)[0]
        name = name.rstrip(".")

    entry = IsoEntry(name, extent, size, (flags & 0x02) != 0)
    return entry, offset + length


def parse_root_record(volume_descriptor: bytes) -> IsoEntry:
    entry, _ = parse_record(volume_descriptor, 156)
    if entry is None or not entry.is_dir:
        raise ValueError("ISO has no readable root directory record")
    return entry


def read_directory(f, entry: IsoEntry) -> list[IsoEntry]:
    f.seek(entry.extent * SECTOR_SIZE)
    data = f.read(entry.size)
    children: list[IsoEntry] = []
    offset = 0
    while offset < len(data):
        if data[offset] == 0:
            offset = ((offset // SECTOR_SIZE) + 1) * SECTOR_SIZE
            continue
        child, offset = parse_record(data, offset)
        if child is None or child.path in (".", ".."):
            continue
        children.append(child)
    return children


def walk_iso(f, root: IsoEntry) -> list[IsoEntry]:
    entries: list[IsoEntry] = []
    visited_dirs: set[tuple[int, int]] = set()

    def walk(parent_path: str, directory: IsoEntry) -> None:
        key = (directory.extent, directory.size)
        if key in visited_dirs:
            return
        visited_dirs.add(key)
        for child in read_directory(f, directory):
            if child.path in (".", "..", ""):
                continue
            rel = child.path if not parent_path else f"{parent_path}/{child.path}"
            full = IsoEntry(rel, child.extent, child.size, child.is_dir)
            entries.append(full)
            if full.is_dir:
                walk(rel, full)

    walk("", root)
    return entries


def safe_output_path(output_root: pathlib.Path, iso_path: str) -> pathlib.Path:
    parts = []
    for part in iso_path.replace("\\", "/").split("/"):
        if not part or part in (".", ".."):
            continue
        parts.append(part)
    if not parts:
        raise ValueError(f"bad ISO path {iso_path!r}")
    result = output_root.joinpath(*parts)
    resolved_root = output_root.resolve()
    resolved_parent = result.parent.resolve()
    if resolved_root != resolved_parent and resolved_root not in resolved_parent.parents:
        raise ValueError(f"refusing to write outside output root: {iso_path}")
    return result


def extract_entries(iso_file: pathlib.Path, output_root: pathlib.Path, entries: list[IsoEntry]) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    with iso_file.open("rb") as f:
        for entry in entries:
            out_path = safe_output_path(output_root, entry.path)
            if entry.is_dir:
                out_path.mkdir(parents=True, exist_ok=True)
                continue
            out_path.parent.mkdir(parents=True, exist_ok=True)
            f.seek(entry.extent * SECTOR_SIZE)
            remaining = entry.size
            with out_path.open("wb") as out:
                while remaining:
                    chunk = f.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise ValueError(f"unexpected EOF while extracting {entry.path}")
                    out.write(chunk)
                    remaining -= len(chunk)


def validate_asset_root(asset_root: pathlib.Path) -> list[str]:
    missing = [path for path in REQUIRED_FILES if not asset_root.joinpath(*path.split("/")).exists()]
    anim = asset_root / "ANIM"
    # Retail PC installs can store ped TXDs in GTA3.IMG and may not include
    # DATA/SPECIAL.TXT. The baker validates per-model TXD availability later.
    if not anim.is_dir() or not any(p.suffix.upper() == ".IFP" for p in anim.iterdir()):
        missing.append("ANIM/*.IFP")
    return sorted(missing)


def has_installer_cabs(asset_root: pathlib.Path) -> bool:
    return any(path.name.upper().startswith("DATA") and path.suffix.upper() == ".CAB" for path in asset_root.iterdir())


def write_local_config(repo_root: pathlib.Path, asset_root: pathlib.Path, sprite_output: pathlib.Path) -> None:
    config = repo_root / "vc-assets.local.properties"
    config.write_text(
        "# Local user-owned GTA VC data. Do not commit.\n"
        f"asset.root={asset_root}\n"
        f"sprite.output={sprite_output}\n",
        encoding="utf-8",
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=pathlib.Path, help="Path to the user-owned GTA Vice City ISO")
    parser.add_argument("--output", required=True, type=pathlib.Path, help="Local extracted asset root")
    parser.add_argument("--write-local-config", action="store_true", help="Write vc-assets.local.properties for the baker")
    parser.add_argument("--sprite-output", type=pathlib.Path, default=pathlib.Path("generated-sprites"), help="Generated sprite output root for local config")
    args = parser.parse_args(argv)

    iso_file = args.iso
    output_root = args.output
    if not iso_file.is_file():
        print(f"ISO not found: {iso_file}", file=sys.stderr)
        return 2

    with iso_file.open("rb") as f:
        descriptor = read_sector(f, PVD_SECTOR)
        if descriptor[1:6] != b"CD001":
            print(f"Not an ISO 9660 image: {iso_file}", file=sys.stderr)
            return 2
        root = parse_root_record(descriptor)
        entries = walk_iso(f, root)

    print(f"Extracting {len(entries)} ISO entries to {output_root}", flush=True)
    extract_entries(iso_file, output_root, entries)

    missing = validate_asset_root(output_root)
    if missing:
        print(f"Extracted ISO, but asset root is incomplete: {output_root}", file=sys.stderr)
        if has_installer_cabs(output_root):
            print("This disc stores installed game data inside InstallShield DATA*.CAB archives.", file=sys.stderr)
            print("Install an extractor such as unshield, then extract those CABs into the same asset root.", file=sys.stderr)
        print("Missing required asset inputs:", file=sys.stderr)
        for item in missing:
            print(f"  {item}", file=sys.stderr)
        return 1

    if args.write_local_config:
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        write_local_config(repo_root, output_root.resolve(), args.sprite_output.resolve())
        print(f"Wrote {repo_root / 'vc-assets.local.properties'}")

    print(f"Asset root ready: {output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
