#!/usr/bin/env python3
"""build_mission.py --out DIR : NMC-free msn/us/EH_NOKILL.bar from STOCK data + the decomposition's metadata recipe.

member 0 (type 2, mission metadata): stock msn/us/AL14_MS_M.bar#0 with the recipe's field edits (patches/missions-messages/
          mission-edits.json key EH11_NOKILL#0)  -> digest checked against the NMC member (byte-identical).
member 1 (type 3, mission script): the decomposition copies NMC's 1,020-byte script (internal name tt32_ms302) from its cache.
          Here the STOCK script of the donor archive (AL14_MS_M.bar#1, al14_fakejafar, 594 bytes) is used instead.
No file or cache entry of the NMC copy is opened."""
import argparse, hashlib, json, struct, sys
from pathlib import Path
D = Path(__file__).resolve().parents[1]/'upstream'
sys.path[:0] = [str(D/'tools'), str(D/'tools/ai'), str(D/'tools/missions-messages')]
import missions as M
from formats import bar
from project import checked
sha = lambda b: hashlib.sha256(b).hexdigest()
p = argparse.ArgumentParser(); p.add_argument('--clean', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
ms = M.Missions(); path = 'msn/us/EH_NOKILL.bar'; layout = ms.layout[path]
rule = ms.rules[layout[0]['source_or_rule']]; src, ordinal = rule['from'].rsplit('#', 1)
rec = ms.inputs[src]; stock = checked(a.clean/rec['package']/'original'/src, rec['sha256']); ent = bar(stock)
meta = M.assemble_metadata(ent[int(ordinal)].data(stock), rule)
script = ent[1].data(stock); assert ent[1].kind == 3
out = bytearray(struct.pack('<4sIII', b'BAR\x01', 2, 0, 0) + bytes(32))
for i, (row, body) in enumerate(zip(layout, (meta, script))):
    struct.pack_into('<HH4sII', out, 16+16*i, int(row['type']), int(row['link']), row['name'].encode(), len(out), len(body)); out.extend(body); out.extend(bytes(-len(out) % 4))
t = a.out/path; t.parent.mkdir(parents=True, exist_ok=True); t.write_bytes(out)
print('%s: %d bytes sha256 %s; NMC file pin %s' % (t, len(out), sha(out)[:16], ms.ledger[path]['sha256'][:16]))
print('member 0 metadata %d bytes sha256 %s (stock %s#%s + fields %s)' % (len(meta), sha(meta)[:16], src, ordinal, json.dumps(rule.get('fields'))))
print('member 1 script   %d bytes sha256 %s (stock %s#1 unchanged; NMC script pin %s, 1020 bytes, NOT used)' % (len(script), sha(script)[:16], src, ms.ai[path, 1]['sha256'][:16]))
