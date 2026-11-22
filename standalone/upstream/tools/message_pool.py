import struct
from formats import require, message_index

WIDTH = (0, 0, 0, 0, 1, 6, 1, 4, 3, 1, 1, 1, 1, 0, 1, 5, 0, 4, 2, 4, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1)

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
