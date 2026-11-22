"""Reword messages inside a KH2 system message archive."""
import hashlib
import struct

# Operand bytes that follow each control code below 32.
WIDTH = (0, 0, 0, 0, 1, 6, 1, 4, 3, 1, 1, 1, 1, 0, 1, 5, 0, 4, 2, 4, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1)


def reword(data, edits):
    """Replace message[start:start+length] with new bytes for each (id, pin, start, length, hex) and repack the pool."""
    at, size = struct.unpack_from('<II', data, 24)
    if (struct.unpack_from('<4sI', data) != (b'BAR\x01', 2) or data[20:24] != b'sys\0'
            or at + size != len(data) or struct.unpack_from('<I', data, at)[0] != 1):
        raise ValueError('unexpected message archive')
    pool = data[at:]
    count = struct.unpack_from('<I', pool, 4)[0]
    messages = {}
    for key, pos in struct.iter_unpack('<II', pool[8:8 + 8 * count]):
        end = pos
        while pool[end]:
            end += 1 + (WIDTH[pool[end]] if pool[end] < 32 else 0)
        if key in messages:
            raise ValueError('duplicate message')
        messages[key] = pool[pos:end + 1]
    for key, pin, start, length, new in edits:
        old = messages[key]
        if hashlib.sha256(old).hexdigest() != pin:
            raise ValueError(f'message {key} differs')
        messages[key] = old[:start] + bytes.fromhex(new) + old[start + length:]
    index = bytearray(struct.pack('<II', 1, len(messages)))
    body = bytearray()
    for key, message in sorted(messages.items()):
        index += struct.pack('<II', key, 8 + 8 * len(messages) + len(body))
        body += message
    result = bytearray(data[:at]) + index + body
    struct.pack_into('<II', result, 24, at, len(index) + len(body))
    struct.pack_into('<II', result, 40, (len(result) + 15) & ~15, 0)
    return bytes(result)
