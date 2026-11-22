"""Assemble mission BARs from clean PC members, field edits and private asset slices."""
import argparse
import csv
import json
import struct
import sys
from collections import defaultdict
from functools import lru_cache
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TOOLS), str(TOOLS/'ai')]
from formats import bar, bdx_triggers, event_commands, require
from project import ROOT, check_manifest, checked, destination, digest, inventory
from members import edit as edit_bdx

PATCH = ROOT/'patches/missions-messages'
HEADER = struct.Struct('<HHHHBBHBBBBiii')
FIELDS = ('version id flags information_id pause_controller reserved09 pause_information_id '
          'help_sequence bonus_reward antiform_multiplier reserved0f start_sound finish_sound fail_sound').split()
OPS = 'End CameraStart CameraComplete CameraFailed Timer Counter Gauge ComboCounter MissionScore Watch LimitCost DriveRefillRatio AddDrive CameraPrize'.split()
SECTIONS = ['sprites', 'parts', 'groups', 'animations', 'animation_groups']
STRIDES = [44, 20, 4, 144, 36]


def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter='\t'))


def read_rules():
    return json.loads((PATCH/'mission-edits.json').read_text())


def metadata_commands(data):
    pos = HEADER.size
    result = []
    while pos + 4 <= len(data):
        opcode, count = struct.unpack_from('<HH', data, pos)
        if opcode == 0:
            break
        require(0 < opcode < len(OPS), 'unknown mission opcode')
        pos += 4
        available = min(count, (len(data) - pos)//4)
        values = list(struct.unpack_from(f'<{available}I', data, pos))
        pos += available * 4
        result.append((OPS[opcode], count, values))
    require(not any(data[pos:]), 'nonzero mission terminator/trailer')
    return result, len(data)-pos


def assemble_metadata(base, change):
    header = dict(zip(FIELDS, HEADER.unpack_from(base)))
    for field, value in change.get('fields', {}).items():
        require(field in header, f'unknown mission field {field}')
        header[field] = value
    result = bytearray(HEADER.pack(*(header[key] for key in FIELDS)))
    commands, tail = metadata_commands(base)
    for index, remove, insert in reversed(change.get('splices', [])):
        require(0 <= index <= len(commands)-remove, 'invalid mission opcode splice')
        commands[index:index+remove] = [command_values(row) for row in insert]
    for name, count, values in commands:
        result.extend(struct.pack(f'<HH{len(values)}I', OPS.index(name), count, *values))
    return bytes(result) + bytes(change.get('zero_tail', tail))


def command_values(row):
    name, args = row
    if name.startswith('Camera'):
        values = [struct.unpack('<I', struct.pack('<BBH', *args))[0]]
    else:
        values = args['parameters']
    count = args.get('declared_count', len(values)) if isinstance(args, dict) else len(values)
    return name, count, values


def sequence(base, edits):
    result = bytearray(base)
    require(base[:4] == b'SEQD', 'invalid sequence member')
    for section, row, field_offset, old, new in edits:
        i = SECTIONS.index(section)
        count, start = struct.unpack_from('<II', base, 8+i*8)
        require(0 <= row < count and 0 <= field_offset <= STRIDES[i]-4 and field_offset%4==0,
                'sequence record/field out of bounds')
        field = start + row*STRIDES[i] + field_offset
        require(struct.unpack_from('<I', base, field)[0] == old, 'sequence field mismatch')
        struct.pack_into('<I', result, field, new)
    return result


class Missions:
    def __init__(self):
        self.ledger = inventory()
        self.layout = defaultdict(list)
        for row in rows(PATCH/'missions.tsv'):
            self.layout[row['path']].append(row)
        require(set(self.layout) == {p for p in self.ledger if p.startswith('msn/')}, 'mission roster mismatch')
        self.rules = read_rules()
        self.inputs = {r['path']:r for r in rows(PATCH/'mission-sources.tsv')}
        self.ai = {(r['path'], int(r['ordinal'])):r for r in rows(ROOT/'patches/ai/members.tsv')
                   if r['path'].startswith('msn/')}
        self.ai_edits = json.loads((ROOT/'patches/ai/members-edits.json').read_text())

    def prepare(self, reference, cache):
        check_manifest(reference)
        for path, layout in self.layout.items():
            data = checked(reference/path, self.ledger[path]['sha256'])
            entries = bar(data)
            for ordinal, row in enumerate(layout):
                operation, ref = row['operation'], row['source_or_rule']
                payload = entries[ordinal].data(data)
                assets = []
                if operation == 'asset':
                    assets.append((ref, payload))
                elif operation == 'ai' and self.ai[path, ordinal]['operation'] == 'local-script':
                    assets.append((self.ai[path, ordinal]['sha256'], payload))
                elif operation == 'event':
                    commands = list(event_commands(payload))
                    for i, item in enumerate(self.rules[ref]['commands']):
                        if 'camera_asset' in item:
                            opcode, track = commands[i]
                            require(opcode == 19, f'unexpected camera command {path}#{ordinal}/{i}')
                            assets.append((item['camera_asset'], track))
                for sha, body in assets:
                    require(digest(body) == sha, f'donor mismatch {path}#{ordinal}')
                    target = destination(cache/sha)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(body)

    def build(self, clean, cache, out):
        @lru_cache(maxsize=8)
        def archive(path):
            record = self.inputs[path]
            return checked(clean/record['package']/'original'/path, record['sha256'])

        def member(ref, kind):
            path, ordinal = ref.rsplit('#', 1)
            data = archive(path)
            entry = bar(data)[int(ordinal)]
            require(entry.kind == kind, f'clean type mismatch {ref}')
            return entry.data(data)

        def event(change):
            result = bytearray()
            for row in change['commands']:
                if 'camera_asset' in row:
                    op, args = 19, checked(cache/row['camera_asset'], row['camera_asset'])
                else:
                    source, index = row['from'].rsplit('/', 1)
                    op, args = list(event_commands(member(source, 22)))[int(index)]
                    if 'project' in row:
                        require(op == 0, 'expected project declaration')
                        args = args[:6] + row['project'].encode('ascii') + bytes(row['zero_tail'])
                    if 'set_u16' in row:
                        args = bytearray(args)
                        for word, old, new in row['set_u16']:
                            require(struct.unpack_from('<H', args, word*2)[0] == old, 'event operand mismatch')
                            struct.pack_into('<H', args, word*2, new)
                result.extend(struct.pack('<HH', len(args)+4, op))
                result.extend(args)
            return result + bytes(2)

        def script(path, ordinal):
            row = self.ai[path, ordinal]
            sha = row['sha256']
            if row['operation'] == 'local-script':
                body = checked(cache/sha, sha)
            else:
                require(row['operation'] in ('retain', 'transplant', 'edit'), 'unexpected AI recipe')
                body = member(f"{row['source_path']}#{row['source_ordinal']}", 3)
                require(digest(body) == row['source_sha256'], 'unexpected clean BDX')
                if row['operation'] == 'edit':
                    body = edit_bdx(body, self.ai_edits[row['edit']])
            require(digest(body) == sha, 'mission BDX fingerprint mismatch')
            bdx_triggers(body)
            return body

        for path, layout in self.layout.items():
            result = bytearray(struct.pack('<4sIII', b'BAR\x01', len(layout), 0, 0) + bytes(16*len(layout)))
            for i, row in enumerate(layout):
                op, ref, kind = row['operation'], row['source_or_rule'], int(row['type'])
                require(i == int(row['ordinal']), f'out-of-order mission ordinal {path}#{i}')
                if op == 'clean':
                    body = member(ref, kind)
                elif op == 'asset':
                    body = checked(cache/ref, ref)
                elif op == 'ai':
                    body = script(path, i)
                elif op == 'event':
                    body = event(self.rules[ref])
                elif op in ('metadata', 'seqd'):
                    change = self.rules[ref]
                    base = member(change['from'], kind)
                    body = assemble_metadata(base, change) if op == 'metadata' else sequence(base, change['fields'])
                else:
                    raise ValueError(f'unrecognized mission operation {op}')
                name = row['name'].encode('ascii')
                require(len(name) <= 4, 'invalid BAR name')
                result.extend(bytes(int(row['zero_gap'])))
                struct.pack_into('<HH4sII', result, 16 + i*16, kind, int(row['link']), name, len(result), len(body))
                result.extend(body)
            require(digest(result) == self.ledger[path]['sha256'], f'mission output fingerprint mismatch {path}')
            target = destination(out/path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    prepare = commands.add_parser('import')
    prepare.add_argument('--reference', type=Path, required=True)
    build = commands.add_parser('build')
    build.add_argument('--clean', type=Path, required=True)
    build.add_argument('--out', type=Path, required=True)
    for command in (prepare, build):
        command.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    try:
        missions = Missions()
        if args.action == 'import':
            missions.prepare(args.reference, args.cache)
        else:
            missions.build(args.clean, args.cache, args.out)
        print(f'{args.action}: {len(missions.layout)} missions')
    except (ValueError, OSError, KeyError, IndexError, struct.error) as exc:
        parser.exit(1, f'missions: {exc}\n')
