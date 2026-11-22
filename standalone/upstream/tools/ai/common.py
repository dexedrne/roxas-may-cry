"""Rebuild the common script using the clean PC file and the reviewed VM edits."""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from formats import bdx_triggers, require
from project import ROOT, destination, digest, inventory

NAME = '00common.bdx'
PATCH = ROOT / 'patches/ai/00common.json'


def offset(pc):
    return 16 + 2 * pc


def helper(spec):
    # Bdxio word encodings. Frame offsets are bytes; push constants occupy two words.
    pop, load, push, deref, store, ret = 1, 0x30, 0, 3, 4, 0x89
    if spec['operation'] == 'clear_upper_bits_before_address':
        n = spec['preceding_bytes']
        require(n in (2, 3), 'unsupported preceding byte count')
        mask = (1 << (8 * n)) - 1
        words = [pop, 0, load, 0, push, n, 0, 0x46, pop, 0,
                 load, 0, deref, 0, push, mask & 0xffff, mask >> 16, 0x146,
                 pop, 4, load, 0, load, 4, store, ret]
    elif spec['operation'] == 'add_to_dword':
        words = [pop, 4, pop, 0, load, 0, deref, 0, load, 4, 6,
                 pop, 8, load, 0, load, 8, store, ret]
    else:
        raise ValueError(f'unknown routine {spec}')
    return struct.pack(f'<{len(words)}H', *words)


def rebuild(clean, spec):
    require(digest(clean) == spec['source_sha256'], 'common clean PC fingerprint')
    triggers = bdx_triggers(clean)
    data = bytearray(clean)
    struct.pack_into('<I', data, 16, spec['work_bytes'])
    for name, pcs in spec['canonicalize_opcode'].items():
        opcode, mask = {'push': (0, 0x3f), 'dup': (0x149, 0xffcf)}[name]
        for pc in pcs:
            word, = struct.unpack_from('<H', data, offset(pc))
            require(word != opcode and word & mask == opcode, f'expected noncanonical {name} at {pc}')
            struct.pack_into('<H', data, offset(pc), opcode)

    edit = spec['set_weapon']
    pc = edit['first_push_pc']
    for i, value in enumerate([edit['character'], *edit['from']]):
        at = offset(pc + i * 3)
        require(struct.unpack_from('<Hi', data, at) == (0, value), 'weapon argument mismatch')
        if i:
            struct.pack_into('<i', data, at + 2, edit['to'][i - 1])
    require(struct.unpack_from('<HH', data, offset(pc + 9)) == (0xa, 75), 'set_weapon trap mismatch')

    start = offset(spec['insert_before_constants_pc'])
    end = start + spec['constant_bytes']
    require(end <= len(data) and not any(data[end:]), 'unexpected sector tail')
    built = data[:start] + b''.join(map(helper, spec['append_routines'])) + data[start:end]
    require(bdx_triggers(built) == triggers, 'common trigger layout changed')
    require(digest(built) == inventory()[NAME]['sha256'], 'common target fingerprint')
    return built


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--clean', required=True, type=Path, help='parent of kh2_first')
    p.add_argument('--out', required=True, type=Path, help='new installed directory below private/')
    args = p.parse_args()
    spec = json.loads(PATCH.read_text())
    built = rebuild((args.clean / spec['source']).read_bytes(), spec)
    out = destination(args.out)
    out.mkdir(parents=True, exist_ok=False)
    (out / NAME).write_bytes(built)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.exit(f'common: {exc}')
