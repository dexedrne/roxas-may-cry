"""Build or import the rooms subset. Only the import operation opens installed assets."""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import bar, require
from project import ROOT, catalog, checked, check_manifest, destination, digest, inventory
from records import EVENT_FIELDS, SCHEMA, OPS, arguments, encode, label, name, pack, tokens, unpack

PATCH = ROOT/'patches/rooms'


def owned():
    return {p:r for p,r in catalog('docs/inventory/baseline.tsv').items() if r['writer']=='rooms'}


def pathof(room):
    return f'ard/us/{room}.ard'


class Baseline:
    def __init__(self, root):
        self.root = root
        self.ledger = owned()

    @lru_cache(None)
    def archive(self, room):
        path = pathof(room)
        row = self.ledger[path]
        require(row['decision']=='base', f'no clean room: {room}')
        data = checked(self.root/('kh2_'+row['package'])/'original'/path, row['base_sha256'])
        return [(e.kind,name(e.name),e.link,e.data(data)) for e in bar(data)]

    def member(self, key):
        room,i = key.split(':')
        return self.archive(room)[int(i)]

    @lru_cache(None)
    def records(self, key):
        kind,_,_,data = self.member(key)
        return tokens(kind,data)


def stub(base, music):
    require(base.member('bb03:27')[:3]==(22,'209',0),'wrong stub project donor')
    project = base.records('bb03:27')[0]
    require(project[0]=='event0', 'missing bb03/209 project')
    rows = [project, ('event15',b''), ('event16',pack('I',0))]
    if music:
        rows.append(('event46',pack('HHBBBB',0,0,0,1,1,0)))
    return encode(22,rows+[('event8',pack('HH',50,18)),('event18',pack('3H',1,0,1))])


def load_recipe(room):
    return json.loads((PATCH/(room+'.json')).read_text())


def materialize(record, source, base):
    tag, *v = record
    if tag == 'record':
        return base.records(v[0])[v[1]]
    if tag in EVENT_FIELDS:
        fmt,fields = EVENT_FIELDS[tag]
        return tag,pack(fmt,*(v[0][f] for f in fields.split()))
    if tag in OPS:
        return tag,arguments(tag,v)
    fmt, fields = SCHEMA[tag]
    i, changes = v
    raw = bytes(struct.calcsize('<'+fmt)) if i is None else source[i][1]
    require(i is None or source[i][0]==tag,'record selector mismatch')
    fields = fields.split()
    vals = list(unpack(fmt,raw))
    for f,value in changes.items():
        vals[fields.index(f)] = value
    return tag,pack(fmt,*vals)


def payload(row, base, cache, previous):
    op = row['op']
    if op == 'clean':
        return base.member(row['from'])[3]
    if op == 'reuse':
        return previous[row['from']]
    if op.startswith('stub-'):
        require(op in ('stub-plain','stub-volume'),'unknown stub')
        return stub(base,op=='stub-volume')
    if op == 'asset':
        return checked(cache/row['sha256'],row['sha256'])
    require(op=='records','unknown room operation')
    source = base.records(row['from']) if 'from' in row else []
    out = []
    h = row.get('performance_sha256')
    performance = tokens(22,checked(cache/h,h)) if h else []
    cursor = 0
    last = 0
    for start, remove, insert in row['splices']:
        require(last <= start <= start+remove <= len(source),'bad record splice')
        out.extend(source[last:start])
        for r in insert:
            if r[0]=='performance':
                _,ordinal,count = r
                require(len(out)==ordinal and count>0 and cursor+count<=len(performance),'performance selector')
                out.extend(performance[cursor:cursor+count])
                cursor += count
            else:
                out.append(materialize(r,source,base))
        last = start+remove
    require(cursor==len(performance),'unused performance commands')
    return encode(row['type'],out+source[last:])


def build(clean, cache, out):
    base = Baseline(clean)
    pins = inventory()
    previous = {}
    require(not out.exists(), f'choose fresh output: {out}')
    out.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=out.parent) as tmp:
        stage = Path(tmp)/'installed'
        (stage/'ard/us').mkdir(parents=True)
        for path in sorted(base.ledger):
            room = Path(path).stem
            if base.ledger[path]['decision']=='base':
                base.archive(room)
            rows = load_recipe(room)
            b = bytearray(b'BAR\x01'+pack('III',len(rows),0,0)+bytes(16*len(rows)))
            for i,row in enumerate(rows):
                data = payload(row,base,cache,previous)
                require(digest(data)==row['sha256'],f'{room}:{i} member pin')
                b.extend(bytes(row['gap']))
                struct.pack_into('<HH4sII',b,16+16*i,row['type'],row['link'],label(row['name']),len(b),len(data))
                b.extend(data)
                previous[f'{room}:{i}'] = data
            require(digest(b)==pins[path]['sha256'],f'{path} outer pin')
            (stage/path).write_bytes(b)
        stage.rename(out)


def imports(reference, cache):
    check_manifest(reference)
    pins = inventory()
    cache.mkdir(parents=True, exist_ok=True)
    for path in owned():
        room = Path(path).stem
        b = None
        for i,row in enumerate(load_recipe(room)):
            h = row.get('performance_sha256')
            if row['op']!='asset' and not h:
                continue
            if b is None:
                b = checked(reference/path,pins[path]['sha256'])
                entries = bar(b)
            data = entries[i].data(b)
            if h:
                records = tokens(22,data)
                selected = []
                for _,_,insert in row['splices']:
                    for rec in insert:
                        if rec[0]=='performance':
                            _,start,count = rec
                            selected.extend(records[start:start+count])
                data = encode(22,selected)
            else:
                h = row['sha256']
            require(digest(data)==h,f'wrong donor {room}:{i}')
            with TemporaryDirectory(dir=cache) as tmp:
                staged = Path(tmp)/h
                staged.write_bytes(data)
                staged.replace(cache/h)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='op',required=True)
    imp = subs.add_parser('import')
    imp.add_argument('--reference',type=Path,required=True)
    build_cmd = subs.add_parser('build')
    build_cmd.add_argument('--clean',type=Path,required=True)
    build_cmd.add_argument('--out',type=Path,required=True)
    for command in (imp,build_cmd):
        command.add_argument('--cache',type=Path,required=True)
    args = parser.parse_args()
    try:
        cache = destination(args.cache) if args.op == 'import' else args.cache
        if args.op=='import':
            imports(args.reference,cache)
        else:
            build(args.clean,cache,destination(args.out))
    except (OSError,ValueError) as exc:
        parser.exit(1,f'rooms: {exc}\n')
