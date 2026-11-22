"""Build the tables projection using only authenticated clean inputs and public recipes."""
import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import bar, selected, directory_record, require
from project import ROOT, catalog, checked, destination, digest, inventory, recipe
from rebuild import battle, worldpoint
from records import SCHEMAS
from scripts import progress, talk

PATCHES = ROOT / 'patches/tables'


def table(data, section, members):
    codec, fields = SCHEMAS[section['schema']]
    start, count = section['offset'], section['clean_rows']
    end = start + count * codec.size
    require(0 <= start <= end <= len(data), 'record range outside member')
    rows = list(codec.iter_unpack(data[start:end]))
    size = section.get('output_rows', count)
    output = (rows + [None] * max(0, size-count))[:size]
    for target, source, length in section.get('copy_rows', []):
        require(length > 0 and 0 <= source <= count-length and 0 <= target <= size-length,
                'invalid clean row span')
        output[target:target+length] = rows[source:source+length]
    for at, edit in section['edits'].items():
        index = int(at)
        require(0 <= index < size, 'invalid output row ordinal')
        for field, expected in edit.get('clean_key', {}).items():
            require(index < count and rows[index][fields.index(field)] == expected, 'clean record key mismatch')
        if 'copy_member_row' in edit:
            ordinal, source = edit['copy_member_row']
            row = list(codec.unpack_from(members[ordinal], 4 + source * codec.size))
        elif 'copy_row' in edit:
            row = list(rows[edit['copy_row']])
        else:
            row = list(rows[index])
        for name, value in edit.get('set', {}).items():
            pos = fields.index(name)
            row[pos] = value.encode('ascii') if isinstance(row[pos], bytes) else value
        output[index] = row
    require(None not in output, 'incomplete table resize')
    return b''.join(codec.pack(*row) for row in output), end


def apply(data, patch, members=()):
    if 'reuse' in patch:
        name = patch['reuse']
        return {'battle': battle, 'worldpoint': worldpoint}[name](data, recipe(name))
    if 'flags' in patch:
        return progress(data, patch)
    if 'talk' in patch:
        return talk(data, patch)
    if 'members' in patch:
        entries = bar(data)
        sources = [e.data(data) for e in entries]
        result = bytearray(data[:16 + 16 * len(entries)])
        modified = {entry['ordinal']:entry for entry in patch['members']}
        require(len(modified) == len(patch['members']), 'duplicate member')
        for e in entries:
            result.extend(bytes(-len(result) % patch['alignment']))
            spec = modified.pop(e.ordinal, None)
            payload = sources[e.ordinal]
            declared = len(payload)
            if spec:
                selected(entries, e.ordinal, spec['kind'], spec['name'], spec['link'])
                payload = apply(payload, spec, sources)
                declared = spec.get('declared_size', len(payload))
            directory_record(result, e.ordinal, len(result), declared)
            result.extend(payload)
        require(not modified, 'unknown member ordinal')
        result.extend(bytes(patch.get('end_padding', 0)))
        return bytes(result)
    out = bytearray()
    pos = 0
    for section in patch['sections']:
        require(pos <= section['offset'], 'overlapping table sections')
        out.extend(data[pos:section['offset']])
        encoded, pos = table(data, section, members)
        out.extend(encoded)
    out.extend(data[pos:patch.get('clean_end')])
    return bytes(out)


def build(clean, out):
    ledger = inventory()
    names = [r for r in catalog('docs/inventory/baseline.tsv').values() if r['writer'] == 'tables']
    require(not out.exists(), 'choose a fresh private output directory')
    outputs = {}
    for row in names:
        path = row['path']
        spec = json.loads((PATCHES/(Path(path).name+'.json')).read_text())
        pin = spec['base_sha256']
        require(not row['base_sha256'] or pin == row['base_sha256'], 'D1 base pin mismatch')
        data = checked(clean/('kh2_'+row['package'])/'original'/path, pin)
        result = apply(data, spec)
        require(digest(result) == ledger[path]['sha256'], f'{path}: output fingerprint mismatch')
        outputs[path] = result
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out.parent) as staging:
        staged = Path(staging) / 'installed'
        for path, result in outputs.items():
            target = staged / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(result)
        staged.rename(out)
    print(f'Built {len(outputs)} tables in {out}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clean', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    try:
        build(args.clean, destination(args.out))
    except (KeyError, IndexError, OSError, struct.error, ValueError) as error:
        parser.exit(1, f'tables: {error}\n')
