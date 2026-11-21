"""Reconcile every physical BDX placement in the baked tree with the shipped recipes."""
import argparse
import csv
import hashlib
import mmap
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def members(data, start, size, prefix=""):
    if size < 16 or data[start:start + 4] != b"BAR\x01":
        return
    count = struct.unpack_from("<I", data, start + 4)[0]
    if 16 + 16 * count > size:
        raise ValueError(f"truncated BAR directory at {start}")
    for i in range(count):
        kind, link, name, offset, length = struct.unpack_from("<HH4sII", data, start + 16 + 16 * i)
        ordinal = f"{prefix}{i}"
        if not length:
            continue
        if offset < 16 + 16 * count or offset + length > size:
            if kind == 3:
                raise ValueError(f"invalid script range at {start}, ordinal {ordinal}")
            continue  # Nested motion archives can reference external non-script records.
        base = start + offset
        if kind == 3:
            yield ordinal, name.rstrip(b"\0").decode("ascii"), data[base:base + length]
        elif data[base:base + 4] == b"BAR\x01":
            yield from members(data, base, length, ordinal + "/")


def placement_rooms(root, public, inventory):
    """Rooms with explicit object spawn records. Party and scripted spawns can appear elsewhere."""
    sys.path[:0] = [str(public / "tools/rooms"), str(public / "tools")]
    from records import tokens
    from formats import bar

    rooms = defaultdict(set)
    for rel in sorted(inventory):
        if not rel.startswith("ard/us/") or not rel.endswith(".ard"):
            continue
        path = root / rel
        data = path.read_bytes()
        for entry in bar(data):
            if entry.kind == 12:
                for tag, raw in tokens(12, entry.data(data)):
                    if tag == "entity":
                        rooms[struct.unpack_from("<I", raw)[0]].add(path.stem)
    return rooms


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=Path, required=True)
    ap.add_argument("--public", type=Path, required=True)
    ap.add_argument("--docs", type=Path, required=True)
    ap.add_argument("--output", type=Path, default=Path("run-native/script-census.tsv"))
    args = ap.parse_args()
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(args.port / "tools"))
    import bdxscan

    bdxscan.find = lru_cache(maxsize=65536)(bdxscan.find)
    inventory = {r["path"] for r in rows(args.public / "INVENTORY-52.tsv")}
    scripts = {(r["path"], r["ordinal"]): r for r in rows(args.public / "docs/ai/catalog-paths.tsv")}
    expected = Counter((r["path"], r["ordinal"], int(r["word_pc"]), int(r["trap_id"]), r["reachability"])
                       for r in rows(args.docs / "ai/catalog-table3.tsv"))
    names = dict((int(i), n) for i, n in re.findall(r"ENTRY\((\d+),(\w+),", (Path(__file__).resolve().parents[1] / "script_entries.inc").read_text()))
    actual = Counter()
    seen = set()
    sites = defaultdict(list)
    totals = Counter()
    gummi_files = set()
    cache = {}
    root = args.port / "data-full"
    for parent, dirs, files in os.walk(root, followlinks=True, onerror=lambda e: sys.exit(str(e))):
        for file in files:
            path = Path(parent, file)
            rel = path.relative_to(root).as_posix()
            with path.open("rb") as stream:
                if stream.read(4) != b"BAR\x01" and path.suffix.lower() != ".bdx":
                    continue
                totals["containers"] += 1
                with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                    placements = [("root", "-", data[:])] if path.suffix.lower() == ".bdx" else members(data, 0, len(data))
                    for ordinal, member, payload in placements:
                        digest = hashlib.sha256(payload).hexdigest()
                        if digest not in cache:
                            script = bdxscan.parse_bdx(payload)
                            if script is None:
                                raise ValueError(f"invalid BDX at {rel}#{ordinal}")
                            cache[digest] = script["name"], [s for s in script["syscalls"] if s[0] == 3]
                        name, calls = cache[digest]
                        totals["scripts"] += 1
                        key = rel, ordinal
                        if rel in inventory:
                            row = scripts[key]
                            if (row["sha256"], row["script"], row["member"]) != (digest, name, member):
                                raise ValueError(f"recipe mismatch at {key}")
                            seen.add(key)
                        for _, idx, pc, reach in calls:
                            if rel.startswith("obj/G_") and rel not in inventory:
                                gummi_files.add(rel)
                                continue
                            actual[rel, ordinal, pc, idx, reach] += 1
                            context = scripts.get(key, {}).get("actor_or_context", path.stem)
                            sites[idx, rel, ordinal, member, name, context, reach].append(pc)
    if seen != set(scripts) or actual != expected:
        raise ValueError(f"script census mismatch. Missing scripts {set(scripts) - seen}, missing sites {expected - actual}, extra sites {actual - expected}")
    rooms = placement_rooms(root, args.public, inventory)
    with args.output.open("w") as out:
        writer = csv.writer(out, delimiter="\t", lineterminator="\n")
        writer.writerow(["index", "name", "file", "BAR_ordinal", "member", "script", "room_or_actor", "objentry_ids", "shipped_spawn_rooms", "reachability", "count", "word_PCs"])
        for (idx, rel, ordinal, member, name, context, reach), pcs in sorted(sites.items()):
            ids = scripts[rel, ordinal]["objentry_ids"]
            places = sorted({room for obj in ids.split() for room in rooms[int(obj)]})
            writer.writerow([idx, names[idx], rel, ordinal, member, name, context, ids,
                             " ".join(places), reach, len(pcs), " ".join(map(str, pcs))])
    print(f'{totals["containers"]} containers, {totals["scripts"]} scripts, {len(seen)} shipped placements, {len(gummi_files)} stock Gummi files calling their own table')
    print(f'{actual.total()} field call sites, {len({k[3] for k in actual})} indices, {len({k[0] for k in actual})} files')


if __name__ == "__main__":
    main()
