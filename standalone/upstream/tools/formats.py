"""Little-endian KH2 physical formats. Member identity includes raw name and linked index."""
import struct
from dataclasses import dataclass


def require(condition, message):
    if not condition:
        raise ValueError(message)


@dataclass(frozen=True)
class Member:
    ordinal: int
    kind: int
    link: int
    name: bytes
    offset: int
    size: int

    def data(self, archive):
        return archive[self.offset:self.offset + self.size]


def bar(data):
    require(len(data) >= 16 and data[:4] == b'BAR\x01', 'invalid BAR header')
    count, = struct.unpack_from('<I', data, 4)
    directory_end = 16 + 16 * count
    require(directory_end <= len(data), 'truncated BAR directory')
    entries = []
    for i in range(count):
        entry = Member(i, *struct.unpack_from('<HH4sII', data, 16 + i * 16))
        require((not entry.size or entry.offset >= directory_end)
                and entry.offset + entry.size <= len(data), f'BAR member {i} out of bounds')
        entries.append(entry)
    return entries


def selected(entries, ordinal, kind, name, link=0):
    require(0 <= ordinal < len(entries), 'unknown BAR ordinal')
    e = entries[ordinal]
    require((e.kind, e.name, e.link) == (kind, name.encode().ljust(4,b'\0'), link),
            f'BAR identity mismatch at ordinal {ordinal}')
    return e


def directory_record(data, ordinal, offset, size):
    struct.pack_into('<II', data, 24 + 16 * ordinal, offset, size)


def programs(data):
    result = []
    pos = 0
    while pos + 4 <= len(data):
        key, size = struct.unpack_from('<hH', data, pos)
        if key == -1:
            require(size == 0 and pos + 4 == len(data), 'bad ARD terminator')
            return result
        require(size >= 4 and size % 4 == 0 and pos + size <= len(data), 'bad ARD program size')
        result.append((key, data[pos + 4:pos + size]))
        pos += size
    raise ValueError('missing ARD terminator')


def event_commands(data):
    pos = 0
    while pos + 2 <= len(data):
        size, = struct.unpack_from('<H', data, pos)
        if size == 0:
            require(pos + 2 == len(data), 'unexpected event tail')
            return
        require(size >= 4 and pos + size <= len(data), 'bad event command length')
        op, = struct.unpack_from('<H', data, pos + 2)
        yield op, data[pos + 4:pos + size]
        pos += size
    raise ValueError('missing event terminator')


def bdx_triggers(data):
    require(len(data) >= 36 and len(data) % 2 == 0, 'invalid BDX size')
    result = []
    for pos in range(28, len(data) - 7, 8):
        event, pc = struct.unpack_from('<ii', data, pos)
        if (event, pc) == (0, 0):
            require(all(16 + pc * 2 >= pos + 8 for _, pc in result), 'BDX trigger inside header')
            return result
        require(0 <= pc and 16 + pc * 2 < len(data), 'BDX trigger outside body')
        result.append((event, pc))
    raise ValueError('missing BDX trigger terminator')


def message_index(data):
    require(len(data) >= 8, 'short message header')
    version, count = struct.unpack_from('<II', data)
    require(version == 1 and 8 + count * 8 <= len(data), 'invalid message directory')
    result = list(struct.iter_unpack('<II', data[8:8 + 8 * count]))
    require(all(8 + 8 * count <= p < len(data) for _, p in result), 'invalid message pointer')
    return result
