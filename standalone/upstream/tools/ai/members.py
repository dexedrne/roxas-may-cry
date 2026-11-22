"""Import local script donors, rebuild type-3 members, or compare them at their target ordinals."""
import argparse
import csv
import json
import struct
import sys
from collections import Counter
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import bdx_triggers, require
from project import PRIVATE, ROOT, check_manifest, checked, destination, digest, inventory, recipe

PATCHES = ROOT / 'patches/ai'
WORK = PRIVATE / 'ai/members'


def extract(data, ordinal):
    require(data[:4] == b'BAR\x01' and len(data) >= 16, 'invalid BAR')
    count, = struct.unpack_from('<I', data, 4)
    end = 16 + count * 16
    require(0 <= ordinal < count and end <= len(data), f'invalid BAR ordinal {ordinal}')
    kind, link, name, pos, size = struct.unpack_from('<HH4sII', data, 16 + ordinal * 16)
    require(kind == 3 and end <= pos and pos + size <= len(data), f'invalid type-3 range at {ordinal}')
    return name.hex(), link, pos, data[pos:pos + size]


def edit(data, change):
    if 'pool_start_byte' in change:
        pos = change['pool_start_byte']
        require(0 <= pos < len(data), 'invalid BDX symbol-pool start')
        strings = []
        while pos < len(data):
            end = data.index(0, pos)
            size = (end - pos + 2) // 2 * 2
            require(end > pos and pos + size <= len(data) and not any(data[end:pos+size]),
                    'invalid BDX aligned string')
            strings.append(data[pos:pos+size])
            pos += size
        order = change['pool_order']
        require(all(0 <= index < len(strings) for index in order), 'invalid BDX symbol index')
        return data[:change['pool_start_byte']] + b''.join(strings[i] for i in order)
    out = bytearray(data)
    for group in change['word_edits']:
        for pc in group['at_word_pc']:
            pos = 16 + pc * 2
            require(0 <= pc and pos + 2 <= len(out) and struct.unpack_from('<H', out, pos)[0] == group['expect'],
                    f'BDX word guard at PC {pc}')
            struct.pack_into('<H', out, pos, group['write'])
    for push in change.get('push_float', []):
        pos = 16 + push['pc'] * 2
        op, value = struct.unpack_from('<Hf', out, pos)
        require(op == 16 and value == push['from'], f"BDX float guard at PC {push['pc']}")
        struct.pack_into('<f', out, pos + 2, push['to'])
    return bytes(out)


def run(args):
    with (PATCHES/'members.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    keys = {(r['path'], r['ordinal']) for r in rows}
    require(len(keys) == len(rows), 'duplicate member recipe')
    ledger = inventory()
    if args.mode != 'build':
        check_manifest(args.reference)
    changes = json.loads((PATCHES/'members-edits.json').read_text())
    changes['D1-RAW'] = recipe('M_EX760_RAW')
    clean = {}
    if args.mode == 'build':
        for manifest in args.clean.glob('kh2_*/manifest.tsv'):
            with manifest.open() as stream:
                for m in csv.DictReader(stream, delimiter='\t'):
                    clean[manifest.parent.name, m['path']] = m['sha256']

    @lru_cache(maxsize=2)
    def reference(path):
        return checked(args.reference/path, ledger[path]['sha256'])

    @lru_cache(maxsize=2)
    def baseline(package, name):
        return checked(args.clean/package/name, clean[package, name])

    routes = Counter()
    for row in rows:
        path = row['path']
        require(path in ledger, f'unknown installation path {path}')
        require(row['kind'] == '3', f'unexpected member type for {path}')
        ordinal = int(row['ordinal'])
        sha = row['sha256']
        cache = destination(args.cache/sha) if args.mode == 'import' else args.cache/sha
        output = destination(args.output/path/f'{ordinal}.bdx')
        route = row['operation']
        if args.mode != 'build':
            name, link, pos, target = extract(reference(path), ordinal)
            require((name, link, pos, len(target), digest(target)) ==
                    (row['raw_name'], int(row['link']), int(row['offset']), int(row['size']), sha),
                    f'target member mismatch {path}#{ordinal}')
        if args.mode == 'import':
            if route == 'local-script':
                cache.parent.mkdir(parents=True, exist_ok=True)
                cache.write_bytes(target)
        elif args.mode == 'verify':
            require(checked(output, sha) == target, f'member bytes differ {path}#{ordinal}')
        else:
            if route == 'local-script':
                result = checked(cache, sha)
            else:
                require(route in ('retain', 'transplant', 'edit'), f'unknown member operation {route}')
                archive = baseline(row['package'], 'original/'+row['source_path'])
                name, link, _, result = extract(archive, int(row['source_ordinal']))
                require((name, link, digest(result)) ==
                        (row['source_raw_name'], int(row['source_link']), row['source_sha256']),
                        f'clean member mismatch {path}#{ordinal}')
                if route == 'edit':
                    result = edit(result, changes[row['edit']])
            require(digest(result) == sha and len(result) == int(row['size']), f'output mismatch {path}#{ordinal}')
            bdx_triggers(result)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(result)
        routes[route] += 1
    if args.mode == 'verify':
        with (ROOT/'docs/ai/catalog-paths.tsv').open() as stream:
            all_scripts = csv.DictReader(stream, delimiter='\t')
            require(keys == {(r['path'], r['ordinal']) for r in all_scripts if r['ordinal'] != 'root'},
                    'member selection differs from recursive script catalog')
        expected = {args.output/path/f'{ordinal}.bdx' for path, ordinal in keys}
        require({p for p in args.output.rglob('*') if p.is_file()} == expected, 'unexpected member output paths')
    print(f'{args.mode}: {len(rows)} member records; {dict(sorted(routes.items()))}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['import', 'build', 'verify'])
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--clean', type=Path)
    parser.add_argument('--cache', type=Path, default=WORK/'cache')
    parser.add_argument('--output', type=Path, default=WORK/'out')
    try:
        args = parser.parse_args()
        if args.mode == 'build' and args.clean is None:
            parser.error('build requires --clean')
        if args.mode != 'build' and args.reference is None:
            parser.error(f'{args.mode} requires --reference')
        run(args)
    except (ValueError, OSError, KeyError, struct.error) as exc:
        parser.exit(1, f'members: {exc}\n')
