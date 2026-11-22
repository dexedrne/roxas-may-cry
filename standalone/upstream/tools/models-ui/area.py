"""Build the models-ui overlay from authenticated clean sources and local member donors."""
import argparse
import os
import csv
import struct
import sys
from collections import defaultdict
from functools import lru_cache
from pathlib import Path
from tempfile import TemporaryDirectory, NamedTemporaryFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from collision import apply as edit_collision
from formats import Member, require
from project import ROOT, PRIVATE, package_manifest, catalog, checked, check_manifest, destination, digest, inventory

PATCH = ROOT/'patches/models-ui'
WORK = PRIVATE/'models-ui'
def input_path(variable):
    value = os.environ.get(variable)
    return Path(value) if value else None


CLEAN = input_path('CLEAN')
REFERENCE = input_path('REFERENCE')
AI = PRIVATE/'ai/members/out'
ENTRY = struct.Struct('<HH4sII')


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def directory(data):
    require(data[:4] == b'BAR\1', 'invalid BAR')
    count, = struct.unpack_from('<I', data, 4)
    end = 16 + count * ENTRY.size
    require(end <= len(data), 'truncated directory')
    entries = [Member(i, *fields) for i, fields in enumerate(ENTRY.iter_unpack(data[16:end]))]
    # Empty MSET slots can carry a sentinel outside the archive.
    require(all(not e.size or end <= e.offset <= len(data)-e.size for e in entries), 'invalid member range')
    return entries


def owned():
    return {p:r for p,r in catalog('docs/inventory/baseline.tsv').items() if r['writer']=='models-ui'}


def clean_reader(root):
    pins = {}
    for row in rows(PATCH/'packages.tsv'):
        pkg = row['package']
        manifest = root/pkg/'manifest.tsv'
        records, sha = package_manifest(manifest)
        require(sha == row['sha256'], f'SHA-256 mismatch: {manifest}')
        for item in records:
            pins[pkg+'/'+item['path']] = item['sha256']

    @lru_cache(maxsize=4)
    def read(key):
        return checked(root/key, pins[key])
    return read


def collect(cache, sha, data):
    path = destination(cache/sha)
    require(digest(data)==sha, f'donor slice mismatch {sha}')
    if path.exists():
        checked(path, sha)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        with NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temp = Path(stream.name)
            try:
                stream.write(data)
                stream.flush()
                temp.replace(path)
            finally:
                temp.unlink(missing_ok=True)


def run(args):
    ledger = inventory()
    files = rows(PATCH/'files.tsv')
    require(len(files)==len(owned()) and {r['path'] for r in files}==set(owned()), 'incomplete models-ui recipes')
    collisions = defaultdict(list)
    for edit in rows(PATCH/'collision.tsv'):
        collisions[edit['sha256']].append(edit)
    motions = {r['slot_sha256']:r for r in rows(PATCH/'motions.tsv')}
    placements = defaultdict(list)
    for row in rows(PATCH/'placements.tsv'):
        placements[row['path']].append(row)
    if args.mode=='import':
        check_manifest(args.reference)
    else:
        base = clean_reader(args.clean)
        output = destination(args.output)
        require(not output.exists(), 'output already exists')
        output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=WORK) as tmp:
        stage = Path(tmp)/'installed'
        for file in files:
            p = file['path']
            sha = ledger[p]['sha256']
            size = int(ledger[p]['bytes'])
            if args.mode=='import':
                target = checked(args.reference/p, sha)
                if file['operation']=='donor':
                    collect(args.cache, sha, target)
                for row in placements[p]:
                    pos, n = int(row['offset']), int(row['size'])
                    if row['source']=='motion':
                        m = motions[row['sha256']]
                        left, clip = int(m['prefix_bytes']), int(m['clip_bytes'])
                        collect(args.cache, m['prefix_sha256'], target[pos:pos+left])
                        collect(args.cache, m['suffix_sha256'], target[pos+left+clip:pos+n])
                    elif row['source']=='donor':
                        collect(args.cache, row['sha256'], target[pos:pos+n])
                continue
            op = file['operation']
            same = base(file['baseline']) if file['baseline'] else b''
            if op=='clean':
                data = base(file['source'])[:size]
            elif op=='donor':
                data = checked(args.cache/sha, sha)
            elif op=='bar':
                count, reserved, motion = map(int, file['source'].split(','))
                data = bytearray(size)
                struct.pack_into('<4sIII', data, 0, b'BAR\1', count, reserved, motion)
                next_slot = 0
                for r in placements[p]:
                    pos, n = int(r['offset']), int(r['size'])
                    if r['slot']!='gap':
                        require(int(r['slot'])==next_slot, f'directory order {p}')
                        name = bytes.fromhex(r['raw_name'])
                        for i in range(int(r['repeat'])):
                            ENTRY.pack_into(data, 16+16*(next_slot+i), int(r['kind']), int(r['link']), name, pos, n)
                        next_slot += int(r['repeat'])
                    if not n:
                        continue
                    src = r['source']
                    if src=='ai':
                        payload = checked(destination(args.ai/p/(r['slot']+'.bdx')), r['sha256'])
                    elif src=='donor':
                        payload = checked(args.cache/r['sha256'], r['sha256'])
                    elif src=='collision':
                        edits = collisions[r['sha256']]
                        first = edits[0]
                        start, length = int(first['offset']), int(first['size'])
                        payload = edit_collision(base(first['clean_archive'])[start:start+length], edits)
                    elif src=='motion':
                        m = motions[r['sha256']]
                        start, length = int(m['clean_offset']), int(m['clip_bytes'])
                        clip = base(m['clean_archive'])[start:start+length]
                        require(digest(clip)==m['clip_sha256'], f'clean animation mismatch {p}')
                        prefix = checked(args.cache/m['prefix_sha256'], m['prefix_sha256'])
                        suffix = checked(args.cache/m['suffix_sha256'], m['suffix_sha256'])
                        require(len(prefix)==int(m['prefix_bytes']), f'motion header size {p}')
                        payload = prefix+clip+suffix
                    elif src=='alias':
                        payload = data[pos:pos+n]
                    elif src.startswith('fill '):
                        payload = bytes([int(src[5:])])*n
                    else:
                        key, start = src.rsplit('@',1)
                        original = same if key=='base' else base(key)
                        payload = original[int(start):int(start)+n]
                    require(len(payload)==n and digest(payload)==r['sha256'], f'member mismatch {p} {r["slot"]}')
                    require(16+16*count <= pos <= size-n, f'placement bounds {p}')
                    data[pos:pos+n] = payload
                require(next_slot==count, f'missing slots {p}')
            else:
                raise ValueError(f'unknown operation {op}')
            require(len(data)==size and digest(data)==sha, f'outer mismatch {p}')
            dest = stage/p
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        if args.mode=='build':
            stage.rename(output)
    print(f'{args.mode} completed for {len(files)} paths')


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['import','build'])
    for key, default in [('reference',REFERENCE),('clean',CLEAN),('cache',WORK/'exact-cache'),('ai',AI),('output',WORK/'installed')]:
        parser.add_argument('--'+key, type=Path, default=default)
    try:
        WORK.mkdir(parents=True, exist_ok=True)
        args = parser.parse_args()
        if args.mode=='import' and args.reference is None:
            parser.error('import requires --reference or REFERENCE')
        if args.mode=='build' and args.clean is None:
            parser.error('build requires --clean or CLEAN')
        run(args)
    except (KeyError, OSError, ValueError, struct.error) as exc:
        parser.exit(1, f'models-ui: {exc}\n')
