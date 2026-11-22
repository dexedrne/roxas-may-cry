"""Streaming Python port of OpenKh.Egs (Hed, EgsEncryption, EgsHdAsset, EgsTools).

OpenKH - Open Kingdom Hearts Research and Development
Copyright 2019-2022 OpenKH Team and Contributors
Modified for this distribution to validate the retail package indexes and record portable source filenames.
This product includes software developed at
The OpenKH GitHub Repository (https://git.openkh.dev/).
Licensed under the Apache License, Version 2.0 (see docs/LICENSE-OpenKH).
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib

# OpenKh.Egs/EgsEncryption.cs, MasterKey and GenerateKey.
MASTER_KEY = bytes.fromhex(
    "7e8897550b06f108ebbb141cd87aec41"
    "34b2a346ef6bfee1cf53a50512d28e52"
    "4a80e981b0f0b49cff0f1513da734e77"
    "bed730e5f65a113767bc836f2776d0cd"
    "690d2e514290b8b64cadce5b1a1ff5af"
    "01f85e3a6e688be89fc9d9269229c833"
    "983254d4442566ac5f9921e48f1dc2d5"
    "a462f90261de59e7079afa2f953f86d3"
    "78a775edd62d6487bdc7c1aaf28c17cb"
    "318ac3cc04ee6aab5c2270ca9e716d85"
    "455db9a9a61047fb827d847bc6e238fc"
    "2b0e209dc5f339a8a06558437ce33618"
    "724979aed17440c4914f2463bfba2396"
    "50b357df1e03487f354d3ee6a1dd093c"
    "3d3b568d932a9b4b0c28b1e0608919db"
    "2cf76cb51b94c0dceab70af416fda200"
)


def egs_mask(seed):
    """Fold the eleven XOR round keys into one repeated 16-byte mask."""
    if len(seed) != 16:
        raise ValueError("invalid EGS seed length")
    key = bytearray(value or i for i, value in enumerate(seed))
    mask = key.copy()
    for constant in (1, 2, 4, 8, 16, 32, 64, 128, 0x1B, 0x36):
        word = bytearray(MASTER_KEY[key[i]] for i in (13, 14, 15, 12))
        word[0] ^= constant
        for i in range(16):
            key[i] ^= word[i % 4]
            word[i % 4] = key[i]
            mask[i] ^= key[i]
    return bytes(mask)


def decrypt_prefix(data, mask):
    """Decrypt the first chunk of an original or HD payload."""
    prefix = bytes(value ^ mask[i % 16] for i, value in enumerate(data[:256]))
    return prefix + data[256:]


HED = struct.Struct("<16sqii")
HD = struct.Struct("<32s4i")
BLOCK = 256 * 1024


def checked_path(name):
    # Both WSL and native Linux must keep archive names inside the output root.
    if (any(c in name for c in '\\:<>|"?*') or any(ord(c) < 32 for c in name)
            or any(p in ("", ".", "..") or p.endswith((" ", "."))
                   for p in name.split("/"))):
        raise ValueError(f"unsafe asset path {name!r}")
    return name


def read_names(file, package):
    names = {}
    with file.open(encoding="utf-8-sig") as src:
        for line_number, line in enumerate(src, 1):
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) == 1:
                owner = fields[0]
            elif len(fields) == 6:
                if fields[1] != package:
                    continue
                owner = fields[3]
            else:
                raise ValueError(f"{file}:{line_number}: expected one name or six ledger fields")
            names[checked_path(owner)] = None
    return names


def read_exact(stream, n):
    data = stream.read(n)
    if len(data) != n:
        raise ValueError(f"short read at {stream.tell() - len(data):#x}, expected {n} bytes")
    return data


def read_index(stream):
    entries = {}
    for row in iter(lambda: stream.read(HED.size), b""):
        if len(row) != HED.size:
            raise ValueError(f"truncated HED entry at {stream.tell() - len(row):#x}")
        md5, offset, stored, actual = HED.unpack(row)
        if md5 in entries:
            raise ValueError(f"duplicate HED digest {md5.hex()}")
        entries[md5] = offset, stored, actual
    return entries


def write_asset(pkg, end, out, rel, size, mode, mask, hd):
    if size < 0 or mode < -2:
        raise ValueError(f"{rel}: invalid length {size} or compression mode {mode}")
    length = size if mode < 0 else mode
    if hd:
        pkg.seek((-pkg.tell()) % 16, os.SEEK_CUR)
        length = (length + 15) & ~15
    if pkg.tell() + length > end:
        raise ValueError(f"{rel}: payload extends past HED record")
    path = out / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    decoder = zlib.decompressobj(-15) if mode >= 0 else None
    remaining = length
    digest = hashlib.sha256()
    written = 0
    with path.open("xb") as dest:
        while remaining:
            block = read_exact(pkg, min(BLOCK, remaining))
            if remaining == length:
                if mode > -2:
                    block = decrypt_prefix(block, mask)
                if decoder:
                    block = block[2:]  # OpenKH skips the zlib wrapper before DEFLATE.
            remaining -= min(BLOCK, remaining)
            while block:
                if decoder:
                    if decoder.eof:
                        break  # checksum and alignment padding
                    try:
                        data = decoder.decompress(block, BLOCK)
                    except zlib.error as exc:
                        raise ValueError(f"{rel}: DEFLATE error: {exc}") from exc
                    block = decoder.unconsumed_tail
                else:
                    data, block = block, b""
                written += len(data)
                # OpenKH uses the declared size as MemoryStream capacity. Some
                # retail DDS streams expand past it. MemoryStream itself is int32 bounded.
                if written > 0x7fffffff:
                    raise ValueError(f"{rel}: decoded member exceeds the format size limit")
                dest.write(data)
                digest.update(data)
        if decoder and not decoder.eof:
            raise ValueError(f"{rel}: truncated DEFLATE stream")
    return rel, written, digest.hexdigest()


def extract_record(pkg, pkg_size, key, entry, name, out, manifest, source):
    offset, length, actual = entry
    end = offset + length
    if offset < 0 or length < 16 or end > pkg_size:
        raise ValueError("record outside package")
    pkg.seek(offset)
    seed = read_exact(pkg, 16)
    size, count, mode, _ = struct.unpack("<4i", seed)
    if size != actual or count < 0 or count * HD.size > length - 16:
        raise ValueError(f"inconsistent record header: HED size {actual}, original {size}, HD count {count}")
    header_hash = hashlib.sha256(HED.pack(key, *entry) + seed)
    assets = [(f"original/{name}", size, mode, False)]
    for _ in range(count):
        header = read_exact(pkg, HD.size)
        header_hash.update(header)
        raw, _, _, leaf_size, leaf_mode = HD.unpack(header)
        leaf = checked_path(raw.split(b"\0", 1)[0].decode("utf-8").replace("\\", "/"))
        assets.append((f"remastered/{name}/{leaf}", leaf_size, leaf_mode, True))
    mask = egs_mask(seed)
    for rel, size, mode, hd in assets:
        row = write_asset(pkg, end, out, rel, size, mode, mask, hd)
        manifest.writerow((*row, *source, header_hash.hexdigest()))


def extract(args):
    names = read_names(args.names, args.hed.stem)
    with args.hed.open("rb") as hed:
        pins = json.loads((Path(__file__).resolve().parents[1]/"config/package-index.json").read_text())
        if args.hed.name in pins:
            if hashlib.sha256(hed.read()).hexdigest() != pins[args.hed.name]:
                raise ValueError(f"unrecognized package revision: {args.hed.name}")
            hed.seek(0)
        entries = read_index(hed)
    # Publish the manifest only after every matched record has decoded.
    args.out.mkdir(exist_ok=True, parents=True)
    if any(args.out.iterdir()):
        raise ValueError(f"output directory must be empty: {args.out}")
    source = (args.hed.name, args.pkg.name)
    with tempfile.TemporaryDirectory(dir=args.out) as tmp, args.pkg.open("rb") as pkg:
        stage = Path(tmp)
        pkg_size = os.fstat(pkg.fileno()).st_size
        missing = []
        with (stage / "manifest.tsv").open("w", encoding="utf-8", newline="") as file:
            manifest = csv.writer(file, delimiter="\t", lineterminator="\n")
            manifest.writerow(("path", "size", "sha256", "hed", "pkg", "record_sha256"))
            for name in names:
                key = hashlib.md5(name.encode("utf-8")).digest()
                if key not in entries and re.fullmatch(r"[0-9A-F]{32}\.dat", name):
                    key = bytes.fromhex(name[:-4])  # OpenKH's unknown-name output
                if key not in entries:
                    missing.append(name)
                    print(f"NOT FOUND {args.hed}: {name}", file=sys.stderr)
                    continue
                if args.list_found:
                    print(name)
                try:
                    extract_record(pkg, pkg_size, key, entries[key], name, stage, manifest, source)
                except (ValueError, OSError) as exc:
                    raise ValueError(f"{args.pkg}: {name} at {entries[key][0]:#x}: {exc}") from exc
        for folder in ("original", "remastered", "manifest.tsv"):
            if (stage / folder).exists():
                (stage / folder).rename(args.out / folder)
    if missing:
        raise ValueError(f"{len(missing)} requested names missing; manifest covers {len(names)-len(missing)} matched records")
    print(f"{args.hed.name}: extracted {len(names)} records", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for opt in ("hed", "pkg", "names", "out"):
        parser.add_argument("--" + opt, type=Path, required=True)
    parser.add_argument("--list-found", action="store_true", help="print matched record names during extraction")
    args = parser.parse_args()
    try:
        extract(args)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"ERROR {args.hed}: {exc}\n")


if __name__ == "__main__":
    main()
