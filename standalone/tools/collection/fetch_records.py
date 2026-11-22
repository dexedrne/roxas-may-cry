#!/usr/bin/env python3
"""Extract exactly the listed records/members from KINGDOM HEARTS -HD 1.5+2.5 ReMIX- package pairs.

Standard library only; runs on Windows, Linux and macOS (Python 3.8+).

    python fetch_records.py --image "C:\\Program Files (x86)\\Steam\\steamapps\\common\\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\\Image\\dt" --out fetched

Input : the folder that holds kh1_first.hed / kh1_first.pkg ... (the Steam build calls it Image\\dt; other
        store builds use Image\\en).  Only the package pairs named in the list are opened, read-only.
List  : fetch-list.tsv beside this script (columns: sha256 package record_md5 member size), or --list FILE.
        One sha256 may have several witnesses (rows); the first one that is present and verifies is used.
Output: OUT/<sha256>           one file per wanted record or member, bytes verified against the sha256
        OUT/manifest.tsv       sha256 size package record_md5 member hed_sha256 status
Exit status 0 only when every listed sha256 was written and verified.  Nothing else is copied.

The record reader is a small port of OpenKh.Egs (Hed, EgsEncryption, EgsHdAsset):
OpenKH - Open Kingdom Hearts Research and Development, Copyright 2019-2022 OpenKH Team and Contributors,
Apache License 2.0 (https://github.com/OpenKH/OpenKh).
"""
import argparse
import csv
import hashlib
import os
import struct
import sys
import zlib

MASTER_KEY = bytes.fromhex(
    "7e8897550b06f108ebbb141cd87aec41" "34b2a346ef6bfee1cf53a50512d28e52"
    "4a80e981b0f0b49cff0f1513da734e77" "bed730e5f65a113767bc836f2776d0cd"
    "690d2e514290b8b64cadce5b1a1ff5af" "01f85e3a6e688be89fc9d9269229c833"
    "983254d4442566ac5f9921e48f1dc2d5" "a462f90261de59e7079afa2f953f86d3"
    "78a775edd62d6487bdc7c1aaf28c17cb" "318ac3cc04ee6aab5c2270ca9e716d85"
    "455db9a9a61047fb827d847bc6e238fc" "2b0e209dc5f339a8a06558437ce33618"
    "724979aed17440c4914f2463bfba2396" "50b357df1e03487f354d3ee6a1dd093c"
    "3d3b568d932a9b4b0c28b1e0608919db" "2cf76cb51b94c0dceab70af416fda200")
HED = struct.Struct("<16sqii")      # name MD5, offset, stored length, original size
HD = struct.Struct("<32s4i")        # leaf name, 2 unused, size, mode
MAX_MEMBER = 1 << 30


def egs_mask(seed):
    key = bytearray(v or i for i, v in enumerate(seed))
    mask = bytearray(key)
    for constant in (1, 2, 4, 8, 16, 32, 64, 128, 0x1B, 0x36):
        word = bytearray(MASTER_KEY[key[i]] for i in (13, 14, 15, 12))
        word[0] ^= constant
        for i in range(16):
            key[i] ^= word[i % 4]
            word[i % 4] = key[i]
            mask[i] ^= key[i]
    return bytes(mask)


def read_exact(stream, n):
    data = stream.read(n)
    if len(data) != n:
        raise ValueError("short read in package (truncated .pkg?)")
    return data


def read_index(path):
    entries = {}
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        blob = stream.read()
    digest.update(blob)
    if len(blob) % HED.size:
        raise ValueError("truncated HED index")
    for md5, offset, stored, actual in HED.iter_unpack(blob):
        entries[md5] = (offset, stored, actual)
    return entries, digest.hexdigest()


def decode(pkg, length, mode, mask):
    """Read one stored asset at the current position and return its decoded bytes."""
    if length > MAX_MEMBER:
        raise ValueError("member too large")
    raw = read_exact(pkg, length)
    if mode > -2:                                   # -2 = stored plain, -1 = stored encrypted, >=0 = deflated
        head = bytes(v ^ mask[i % 16] for i, v in enumerate(raw[:256]))
        raw = head + raw[256:]
    if mode >= 0:
        inflater = zlib.decompressobj(-15)
        raw = inflater.decompress(raw[2:])          # OpenKH skips the 2-byte zlib wrapper
        if not inflater.eof:
            raise ValueError("truncated DEFLATE stream")
    return raw


def members(pkg, pkg_size, key, entry, wanted):
    """Yield (member, bytes) for the wanted member names of one HED record ('' = the original file)."""
    offset, stored, actual = entry
    end = offset + stored
    if offset < 0 or stored < 16 or end > pkg_size:
        raise ValueError("record outside package")
    pkg.seek(offset)
    seed = read_exact(pkg, 16)
    size, count, mode, _ = struct.unpack("<4i", seed)
    if size != actual or count < 0 or count * HD.size > stored - 16:
        raise ValueError("inconsistent record header")
    assets = [("", size, mode, False)]
    for _ in range(count):
        raw, _, _, leaf_size, leaf_mode = HD.unpack(read_exact(pkg, HD.size))
        assets.append((raw.split(b"\0", 1)[0].decode("utf-8").replace("\\", "/"), leaf_size, leaf_mode, True))
    mask = egs_mask(seed)
    left = set(wanted)
    for name, a_size, a_mode, hd in assets:
        if a_size < 0 or a_mode < -2:
            raise ValueError("invalid member header")
        length = a_size if a_mode < 0 else a_mode
        if hd:
            pkg.seek((-pkg.tell()) % 16, os.SEEK_CUR)
            length = (length + 15) & ~15
        if pkg.tell() + length > end:
            raise ValueError("member extends past its record")
        if name in left:
            left.discard(name)
            yield name, decode(pkg, length, a_mode, mask)
            if not left:
                return
        else:
            pkg.seek(length, os.SEEK_CUR)


def fit(data, size, sha):
    """Return the bytes whose SHA-256 is `sha` (HD leaves may carry their final 16-byte alignment), else None."""
    if hashlib.sha256(data).hexdigest() == sha:
        return data
    if len(data) == ((size + 15) & ~15) and hashlib.sha256(data[:size]).hexdigest() == sha:
        return data[:size]
    return None


def run(image, out, listing, log=print):
    with open(listing, newline="", encoding="utf-8") as stream:
        wanted = list(csv.DictReader(stream, delimiter="\t"))
    for row in wanted:
        if (len(row["sha256"]) != 64 or len(row["record_md5"]) != 32 or
                any(c in row["package"] for c in '/\\:.') or not row["size"].isdigit()):
            raise ValueError("bad row in list: %r" % (row,))
    os.makedirs(out, exist_ok=True)
    done, notes = {}, {}
    by_package = {}
    for row in wanted:
        by_package.setdefault(row["package"], []).append(row)
    for package in sorted(by_package):
        hed, pkg_path = (os.path.join(image, package + ext) for ext in (".hed", ".pkg"))
        if not (os.path.isfile(hed) and os.path.isfile(pkg_path)):
            log("package pair not found, skipped: %s.hed / %s.pkg" % (package, package))
            for row in by_package[package]:
                notes.setdefault(row["sha256"], []).append("%s: package pair not found" % package)
            continue
        index, hed_sha = read_index(hed)
        records = {}
        for row in by_package[package]:
            if row["sha256"] not in done:
                records.setdefault(row["record_md5"], []).append(row)
        pkg_size = os.path.getsize(pkg_path)
        with open(pkg_path, "rb") as pkg:
            for md5 in sorted(records, key=lambda m: index.get(bytes.fromhex(m), (0,))[0]):
                rows = [r for r in records[md5] if r["sha256"] not in done]
                key = bytes.fromhex(md5)
                if not rows:
                    continue
                if key not in index:
                    for r in rows:
                        notes.setdefault(r["sha256"], []).append("%s/%s: record not in HED" % (package, md5))
                    continue
                try:
                    found = dict(members(pkg, pkg_size, key, index[key], {r["member"] for r in rows}))
                except (ValueError, OSError, zlib.error, struct.error) as exc:
                    for r in rows:
                        notes.setdefault(r["sha256"], []).append("%s/%s: %s" % (package, md5, exc))
                    continue
                for r in rows:
                    sha = r["sha256"]
                    if sha in done:
                        continue
                    if r["member"] not in found:
                        notes.setdefault(sha, []).append("%s/%s/%s: member not in record" % (package, md5, r["member"]))
                        continue
                    data = fit(found[r["member"]], int(r["size"]), sha)
                    if data is None:
                        notes.setdefault(sha, []).append("%s/%s/%s: SHA-256 mismatch (other game revision?)"
                                                         % (package, md5, r["member"]))
                        continue
                    tmp = os.path.join(out, sha + ".part")
                    with open(tmp, "wb") as dest:
                        dest.write(data)
                    os.replace(tmp, os.path.join(out, sha))
                    done[sha] = (sha, len(data), package, md5, r["member"], hed_sha, "verified")
    order = list(dict.fromkeys(r["sha256"] for r in wanted))
    with open(os.path.join(out, "manifest.tsv"), "w", newline="", encoding="utf-8") as stream:
        table = csv.writer(stream, delimiter="\t", lineterminator="\n")
        table.writerow(("sha256", "size", "package", "record_md5", "member", "hed_sha256", "status"))
        for sha in order:
            table.writerow(done.get(sha) or (sha, "", "", "", "", "", "MISSING: " + "; ".join(notes.get(sha, ["?"]))))
    missing = [s for s in order if s not in done]
    log("%d of %d listed files written and verified to %s; %d missing (see manifest.tsv)"
        % (len(done), len(order), out, len(missing)))
    return len(missing)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    cli = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    cli.add_argument("--image", required=True, help=r"folder with the .hed/.pkg pairs (Steam: ...\Image\dt)")
    cli.add_argument("--out", required=True, help="destination folder (created; existing verified files are rewritten)")
    cli.add_argument("--list", default=os.path.join(here, "fetch-list.tsv"), help="TSV list (default: fetch-list.tsv beside the script)")
    args = cli.parse_args()
    if os.path.abspath(args.out).startswith(os.path.abspath(args.image) + os.sep) or \
            os.path.abspath(args.out) == os.path.abspath(args.image):
        cli.error("choose an output folder outside the game folder")
    try:
        sys.exit(1 if run(args.image, args.out, args.list) else 0)
    except (OSError, ValueError, KeyError) as exc:
        cli.exit(2, "ERROR: %s\n" % exc)


if __name__ == "__main__":
    main()
