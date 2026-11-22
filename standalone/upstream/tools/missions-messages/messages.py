"""Rebuild the US message BARs from clean messages and selected local wording."""
import argparse
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import bar, directory_record, message_index, require, selected
from project import ROOT, checked, destination, digest, inventory

PATCH = ROOT / 'patches/missions-messages/messages'
# Operand widths of the KH2 international message controls, indexed by opcode.
WIDTH = (0, 0, 0, 0, 1, 6, 1, 4, 3, 1, 1, 1, 1, 0, 1, 5, 0, 4, 2, 4, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1)


def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter='\t'))


def read_messages(data):
    messages = {}
    for key, pos in message_index(data):
        require(key not in messages, f'duplicate message ID {key:#x}')
        end = pos
        while end < len(data) and data[end]:
            op = data[end]
            end += 1 + (WIDTH[op] if op < 32 else 0)
        require(end < len(data), f'unterminated message {key:#x}')
        messages[key] = data[pos:end + 1]
    return messages


def pack_messages(messages, intern):
    header = bytearray(struct.pack('<II', 1, len(messages)))
    pool_at = 8 + 8 * len(messages)
    pool = bytearray()
    offsets = {}
    for key, string in sorted(messages.items()):
        if not intern or string not in offsets:
            offsets[string] = pool_at + len(pool)
            pool.extend(string)
        header.extend(struct.pack('<II', key, offsets[string]))
    return header + pool


def word(name):
    mapping = {' ': 1, '-': 0x66, **{str(i): 0x90 + i for i in range(10)}, **{chr(65 + i): 0x2e + i for i in range(26)},
               **{chr(97 + i): 0x9a + i for i in range(26)}}
    return bytes(mapping[c] for c in name)


def edits(name):
    for row in rows(PATCH / (name + '.tsv')):
        for span in row['ids_hex'].split(','):
            ends = [int(n, 16) for n in span.split('-')]
            require(len(ends) in (1, 2) and ends[0] <= ends[-1], f'invalid ID span {span}')
            for key in range(ends[0], ends[-1] + 1):
                yield key, row['operation'], row['argument']


def prepare(reference, cache):
    ledger = inventory()
    for recipe in rows(PATCH / 'archives.tsv'):
        name = recipe['table']
        keys = sorted(k for k, op, _ in edits(name) if op == 'local-wording')
        if not keys:
            continue
        path = f'msg/us/{name}.bar'
        archive = checked(reference / path, ledger[path]['sha256'])
        # The empty md_m entry of sys and jm can point beyond the physical EOF.
        offset, size = struct.unpack_from('<II', archive, 24)
        messages = read_messages(archive[offset:offset + size])
        data = pack_messages({k: messages[k] for k in keys}, False)
        require(digest(data) == recipe['wording_sha256'], f'wording pin mismatch in {path}')
        dest = destination(cache / 'message-wording' / name)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)


def build(clean, cache, out):
    recipes = rows(PATCH / 'archives.tsv')
    ledger = inventory()
    tables, archives = {}, {}
    for recipe in recipes:
        name = recipe['table']
        path = f'msg/us/{name}.bar'
        data = checked(clean / recipe['package'] / 'original' / path, recipe['clean_sha256'])
        entries = bar(data)
        require(len(entries) == 2, f'unexpected clean directory in {name}')
        member = selected(entries, 0, 2, name)
        tables[name] = read_messages(member.data(data))
        archives[name] = data, member, selected(entries, 1, 24, 'md_m')
    for recipe in recipes:
        name = recipe['table']
        patched = tables[name].copy()
        pin = recipe['wording_sha256']
        wording = read_messages(checked(cache / 'message-wording' / name, pin)) if pin else {}
        aliases = {}
        seen = set()
        for key, op, value in edits(name):
            require(key not in seen, f'repeated message edit {name}/{key:#x}')
            seen.add(key)
            if op == 'clean-id':
                source, ident = value.split('/')
                patched[key] = tables[source][int(ident, 16)]
            elif op == 'rename':
                old, new = (word(w) for w in value.split(' -> '))
                require(old in patched[key], f'missing rename source {name}/{key:#x}')
                patched[key] = patched[key].replace(old, new)
            elif op == 'append-icon':
                patched[key] = patched[key][:-1] + bytes((1, 9, int(value, 16), 0))
            elif op == 'local-wording':
                patched[key] = wording[key]
            elif op in ('result-id', 'result-choice'):
                aliases[key] = op, value
            else:
                raise ValueError(f'unknown message operation {op}')
        for key, (op, value) in aliases.items():
            fields = value.split('/')
            source = int(fields[0], 16)
            require(source not in aliases, 'unresolved message alias chain')
            string = patched[source]
            if op == 'result-choice':
                # Select the second 0x15 control in the shared save prompt.
                positions = []
                pos = 0
                while pos < len(string):
                    code = string[pos]
                    if code == 0x15:
                        positions.append(pos + 1)
                    pos += 1 + (WIDTH[code] if code < 32 else 0)
                require(len(positions) == 2, 'save prompt choice count')
                pos = positions[1]
                string = string[:pos] + struct.pack('<H', int(fields[1], 16)) + string[pos + 2:]
            patched[key] = string
        base, member, image = archives[name]
        require(recipe['packing'] in ('intern-equal', 'sequential'), 'unknown message packing')
        packed = pack_messages(patched, recipe['packing'] == 'intern-equal')
        body = bytearray(base[:member.offset]) + packed
        directory_record(body, 0, member.offset, len(packed))
        aligned = (len(body) + 15) & ~15
        directory_record(body, 1, aligned, image.size)
        if image.size:
            body.extend(bytes(aligned - len(body)) + image.data(base))
        for leaf in [f'{name}.bar'] + ([recipe['also_emit']] if recipe['also_emit'] else []):
            path = 'msg/us/' + leaf
            require(digest(body) == ledger[path]['sha256'], f'rebuilt archive mismatch {path}')
            dest = destination(out / path)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(body)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    importer = actions.add_parser('import')
    importer.add_argument('--reference', required=True, type=Path)
    importer.add_argument('--cache', required=True, type=Path)
    builder = actions.add_parser('build')
    builder.add_argument('--clean', required=True, type=Path)
    builder.add_argument('--cache', required=True, type=Path)
    builder.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.action == 'import':
            prepare(args.reference, destination(args.cache))
        else:
            import fonts
            build(args.clean, args.cache, destination(args.out))
            fonts.build(args.clean, args.out)
    except (ValueError, OSError, KeyError) as err:
        parser.exit(1, f'messages: {err}\n')
