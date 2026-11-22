"""Decode and edit progress flag programs and ordered libretto talk definitions."""
import struct
from formats import require

OP = {0:'End', 1:'ChangeArdProgram', 2:'BlockArea', 3:'UnblockArea', 4:'AddWorldPoint',
      5:'RemoveWorldPoint', 6:'ChangeBGM', 7:'LowerProgress', 8:'RaiseMenu',
      9:'LowerMenu', 12:'RaiseProgress', 13:'WorldMapStatus'}
CODES = {v:k for k,v in OP.items()}


def read_flags(data):
    limit = len(data)
    pos = 0
    while pos < limit:
        pointer, = struct.unpack_from('<H', data, pos)
        if pointer:
            require(pos < pointer <= len(data) and pointer % 2 == 0, 'invalid progress pointer')
            limit = min(limit, pointer)
        pos += 2
    pointers = struct.unpack_from(f'<{limit//2}H', data)
    labels = {}
    flags = []
    bounds = sorted(set(pointers) - {0}) + [len(data)]
    ends = dict(zip(bounds, bounds[1:]))
    for pointer in pointers:
        if not pointer:
            flags.append(None)
            continue
        if pointer in labels:
            flags.append(labels[pointer])
            continue
        labels[pointer] = len(flags)
        pos = pointer
        ops = []
        while True:
            require(pos+2 <= ends[pointer], 'unterminated progress script')
            code, argc = struct.unpack_from('<BB', data, pos)
            pos += 2
            if not code:
                require(argc == 0 and pos == ends[pointer], 'invalid progress terminator')
                break
            require(pos+2*argc <= ends[pointer], 'progress operands cross next flag')
            args = struct.unpack_from(f'<{argc}h', data, pos)
            ops.append((OP.get(code, f'Opcode{code}'), *args))
            pos += 2 * argc
        flags.append(ops)
    return flags


def progress(data, patch):
    flags = read_flags(data)
    count = patch['flag_count']
    flags = (flags + [None] * count)[:count]
    for key, change in patch['flags'].items():
        i = int(key)
        if change is None or isinstance(change, int):
            flags[i] = change
        else:
            ops = list(flags[i] or [])
            for at, remove, insert in reversed(change):
                require(0 <= at <= at + remove <= len(ops), 'progress instruction range')
                if isinstance(insert, dict):
                    name, *args = ops[at]
                    require(remove == 1 and name == insert['operation'], 'progress opcode guard')
                    for index, value in insert['operands'].items():
                        args[int(index)] = value
                    insert = [[name, *args]]
                ops[at:at+remove] = insert
            flags[i] = ops
    result = bytearray(2*count)
    pointers = []
    for i, ops in enumerate(flags):
        if ops is None:
            pointer = 0
        elif isinstance(ops, int):
            require(0 <= ops < i and pointers[ops], 'invalid progress alias')
            pointer = pointers[ops]
        else:
            pointer = len(result)
            for name, *args in ops:
                code = int(name[6:]) if name.startswith('Opcode') else CODES[name]
                result.extend(struct.pack(f'<BB{len(args)}h', code, len(args), *args))
            result.extend(bytes(2))
        pointers.append(pointer)
        struct.pack_into('<H', result, 2*i, pointer)
    return bytes(result)


def read_talk(data):
    version, count = struct.unpack_from('<II', data)
    require(version == 3 and 8+8*count <= len(data), 'invalid talk header')
    defs = list(struct.iter_unpack('<HHI', data[8:8+8*count]))
    result = {}
    for i,(key, mode, start) in enumerate(defs):
        end = defs[i+1][2] if i+1 < count else len(data)
        require(key not in result and 8+8*count <= start <= end <= len(data), 'bad talk directory')
        result[key] = mode, [w for w, in struct.iter_unpack('<I', data[start:end])]
    return result


def talk(data, patch):
    defs = read_talk(data)
    for key, change in patch['talk'].items():
        key = int(key)
        mode, words = defs.get(key, (None, []))
        if 'append_textbox' in change:
            require(mode is None, 'duplicate appended TalkMessageId')
            mode, text = change['append_textbox']
            words = [65537, text, 0]
        elif 'clear_words' in change:
            require(len(words) == change['clear_words'], 'unexpected talk length')
            words = [0] * len(words)
        else:
            base = words[:]
            for at, remove, parts in reversed(change['edit_words']):
                require(0 <= at <= at+remove <= len(base), 'talk word range')
                output = []
                for part in parts:
                    if 'clean' in part:
                        pos, n = part['clean']
                        require(0 <= pos <= pos+n <= len(base), 'talk copy range')
                        output.extend(base[pos:pos+n])
                    elif 'zeros' in part:
                        output.extend([0]*part['zeros'])
                    else:
                        output.extend(part['words'])
                words[at:at+remove] = output
        defs[key] = mode, words
    result = bytearray(struct.pack('<II', 3, len(defs)) + bytes(8*len(defs)))
    for i,(key,(mode,words)) in enumerate(defs.items()):
        struct.pack_into('<HHI', result, 8+8*i, key, mode, len(result))
        result.extend(struct.pack(f'<{len(words)}I', *words))
    return bytes(result)
