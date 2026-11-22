#!/usr/bin/env python3
"""build_rooms.py --clean DIR --out DIR [--rooms tt01,eh02] [--report FILE]

Build NMC-free opening room archives (ard/us/<room>.ard) from STOCK KH2 PC data plus the published decomposition recipes
(/home/alex/work/nmc-decomp/patches/rooms/<room>.json, read-only).  No file, member or cache entry of the NMC copy is opened:
  * op clean / records / stub-*  -> built exactly as the decomposition does (clean member + declarative record edits);
    the member digest must equal the recipe's pin (= the NMC member), so these members are byte-identical to NMC's.
  * op asset (type 3 BDX the decomposition copies from the NMC cache)  -> LEFT OUT.  Every `MapScript <ordinal>` command that
    selected it is removed and the remaining MapScript ordinals are renumbered.
  * op records with performance_sha256 (type 22 timeline whose added commands come from the NMC cache) -> the CLEAN member
    named by `from` is used unchanged (the stock timeline).
The result is therefore not byte-identical to NMC's archive; the report lists every member and what was done.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path
D = Path(__file__).resolve().parents[1]/'upstream'
sys.path[:0] = [str(D/'tools'), str(D/'tools/rooms')]
import rooms as R                                    # decomposition library (not modified)
from records import tokens, encode, label, pack, unpack

class NoCache:                                       # any cache access is a bug here
    def __truediv__(self, other): raise RuntimeError('NMC cache access attempted: '+str(other))

def sha(b): return hashlib.sha256(b).hexdigest()

def build_room(base, room, rep):
    rows = R.load_recipe(room)
    keep = [i for i, r in enumerate(rows) if r['op'] != 'asset']
    remap = {old: new for new, old in enumerate(keep)}
    previous, members = {}, []
    for i, row in enumerate(rows):
        if row['op'] == 'asset':
            rep.append((room, i, row['type'], row['name'], 'DROPPED', 'NMC-authored script (decomposition op asset)', row['sha256'], ''))
            continue
        if row.get('performance_sha256'):
            data = base.member(row['from'])[3]; how = 'stock member '+row['from']+' unchanged (NMC timeline additions left out)'
        else:
            data = R.payload(row, base, NoCache(), previous)
            how = {'clean': 'stock member '+row.get('from', ''), 'records': 'recipe: record edits on '+row.get('from', 'empty member')}.get(row['op'], 'recipe: '+row['op'])
        if row['type'] == 13:                         # area programs: drop / renumber MapScript
            out, changed = [], 0
            for tag, a in tokens(13, data):
                if tag == 'MapScript':
                    n, = unpack('I', a)
                    if n not in remap: changed += 1; how += '; MapScript %d removed' % n; continue
                    if remap[n] != n: changed += 1; how += '; MapScript %d -> %d' % (n, remap[n]); a = pack('I', remap[n])
                out.append((tag, a))
            if changed: data = encode(13, out)
        previous['%s:%d' % (room, i)] = data
        state = 'same-as-NMC' if sha(data) == row['sha256'] else 'DIFFERS'
        rep.append((room, i, row['type'], row['name'], state, how, row['sha256'], sha(data)))
        members.append((row, data))
    b = bytearray(b'BAR\x01' + pack('III', len(members), 0, 0) + bytes(16*len(members)))
    for i, (row, data) in enumerate(members):
        b.extend(bytes(-len(b) % 4))
        struct.pack_into('<HH4sII', b, 16+16*i, row['type'], row['link'], label(row['name']), len(b), len(data))
        b.extend(data)
    return bytes(b)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--clean', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True); p.add_argument('--rooms', default='tt01,eh02'); p.add_argument('--report', type=Path)
    a = p.parse_args()
    base = R.Baseline(a.clean); rep = []
    for room in a.rooms.split(','):
        data = build_room(base, room, rep)
        t = a.out/('ard/us/%s.ard' % room); t.parent.mkdir(parents=True, exist_ok=True); t.write_bytes(data)
        print('%s: %d bytes sha256 %s (NMC pin %s)' % (t, len(data), sha(data)[:16], R.inventory()['ard/us/%s.ard' % room]['sha256'][:16]))
    lines = ['room\tordinal\ttype\tname\tstate\thow\tnmc_member_sha256\tbuilt_sha256'] + ['\t'.join(map(str, r)) for r in rep]
    if a.report: a.report.write_text('\n'.join(lines)+'\n')
    for l in lines: print(l[:230])
