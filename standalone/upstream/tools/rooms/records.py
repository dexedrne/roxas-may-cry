"""ARD records used as units of inheritance. All integers are little-endian."""
import struct
from formats import event_commands, programs, require

SCHEMA = {
    'spawn': ('II', 'version groups'),
    'group': ('BBh4hI3I4B3I', 'type flags id entities activators walks returns signals u10 u14 u18 area entrance direction u1f u20 u24 u28'),
    'entity': ('I6fBBhiihhhhBBhiii', 'object x y z rx ry rz spawn_type argument serial arg1 arg2 talk delay reaction range level medal u32 u34 u38 u3c'),
    'activator': ('hh9fI4h3I', 'shape option x y z sx sy sz rx ry rz flags type bg_on bg_off u32 u34 u38 u3c'),
    'walk': ('hhBBh', 'serial points flags id u06'),
    'point': ('3f', 'x y z'),
    'return': ('6Bhii', 'id type rate entry_type u04 u05 u06 u08 u0c'),
    'signal': ('hh4B', 'id argument action u05 u06 u07'),
    'program': ('h', 'id')
}
OPS = 'Spawn MapVisibility RandomSpawn CasualSpawn Capacity AllocEnemy EnemyHistoryDepth GimmickHistoryDepth Reserved08 SpawnAlt MapScript BarrierFlag AreaSettings Reserved0d NaviMap Party Bgm MsgWall AllocPacket Camera StatusFlag3 Mission Bar StatusFlag5 AllocEffect Progress VisibilityOn VisibilityOff If Recov BattleLevel Gacha'.split()


def pack(fmt, *v):
    return struct.pack('<' + fmt, *v)


def unpack(fmt, data):
    return struct.unpack('<' + fmt, data)


def name(d):
    return d.rstrip(b'\0').decode('ascii')


def label(s, n=4):
    b = s.encode('ascii')
    require(len(b) <= n, 'name too long')
    return b.ljust(n, b'\0')


def tokens(kind, d):
    if kind == 22:
        return [('event'+str(op), a) for op,a in event_commands(d)]
    if kind == 13:
        out = []
        for pid, body in programs(d):
            out.append(('program', pack('h', pid)))
            out.extend(commands(body))
        return out
    require(kind == 12, 'unexpected record kind')
    out = []
    pos = 0

    def take(tag):
        nonlocal pos
        fmt = SCHEMA[tag][0]
        end = pos + struct.calcsize('<' + fmt)
        raw = d[pos:end]
        values = unpack(fmt, raw)
        out.append((tag, raw))
        pos = end
        return values

    if d:
        version, count = take('spawn')
        require(version == 2, 'unknown spawn format')
        for _ in range(count):
            g = take('group')
            for tag, count in zip(('entity','activator','walk','return','signal'), g[3:8]):
                require(count >= 0, 'negative spawn count')
                for _ in range(count):
                    values = take(tag)
                    if tag == 'walk':
                        for _ in range(values[1]):
                            take('point')
    require(pos == len(d), 'spawn trailing data')
    return out


def commands(body):
    pos = 0
    while pos < len(body):
        op, n = struct.unpack_from('<HH', body, pos)
        end = pos + 4 + n * 4
        require(end <= len(body) and op < len(OPS), 'invalid area command')
        yield OPS[op], body[pos+4:end]
        pos = end


def cmd(tag, a):
    return pack('HH', OPS.index(tag), len(a)//4) + a


def encode(kind, rows):
    if kind == 12:
        return b''.join(a for _, a in rows)
    if kind == 22:
        return b''.join(pack('HH', len(a)+4, int(tag[5:])) + a for tag,a in rows) + bytes(2)
    out = bytearray()
    for tag, a in rows:
        if tag == 'program':
            pos = len(out)
            out.extend(a + bytes(2))
        else:
            out.extend(cmd(tag, a))
        struct.pack_into('<H', out, pos+2, len(out)-pos)
    return bytes(out) + pack('hH', -1, 0)


def settings(a):
    out = []
    pos = 4
    while pos < len(a):
        op, arg = struct.unpack_from('<hh', a, pos)
        pos += 4
        size = {0:4, 1:12, 6:4*arg}.get(op, 0)
        require(0 <= op <= 8 and size >= 0 and pos+size <= len(a), 'unknown area setting')
        payload = a[pos:pos+size]
        pos += size
        if op == 0:
            out.append(['event', arg, name(payload)])
        elif op == 1:
            out.append(['jump', arg, *unpack('6H', payload)])
        elif op == 6:
            out.append(['list', *unpack('I'*arg, payload)])
        else:
            out.append([['','','flag','menu','set4','set5','','set7','set8'][op], arg])
    return [*unpack('hh', a[:4]), *out]


def values(tag, a):
    if tag == 'AreaSettings':
        return settings(a)
    if tag == 'If':
        return [*unpack('I', a[:4]), *[[t,*values(t,b)] for t,b in commands(a[4:])]]
    if tag in ('Spawn','SpawnAlt','RandomSpawn','Gacha'):
        return [name(a[i:i+4]) for i in range(0,len(a),4)]
    if tag == 'CasualSpawn':
        return [*unpack('I', a[:4]),name(a[4:])]
    if tag == 'Mission':
        return [*unpack('I', a[:4]),name(a[4:]),len(a)-4]
    if tag == 'Bar':
        return [name(a),len(a)]
    if tag == 'Bgm':
        return list(unpack('HH', a))
    return list(unpack('I'*(len(a)//4), a))


def arguments(tag, v):
    if tag == 'AreaSettings':
        b = pack('hh', *v[:2])
        for sub, *args in v[2:]:
            op = ['event','jump','flag','menu','set4','set5','list','set7','set8'].index(sub)
            if op == 6:
                b += pack('hh'+'I'*len(args), op,len(args),*args)
            else:
                b += pack('hh', op,args[0])
                if op == 0:
                    b += label(args[1])
                elif op == 1:
                    b += pack('6H', *args[1:])
        return b
    if tag == 'If':
        return pack('I',v[0]) + b''.join(cmd(t,arguments(t,args)) for t,*args in v[1:])
    if tag in ('Spawn','SpawnAlt','RandomSpawn','Gacha'):
        return b''.join(label(s) for s in v)
    if tag == 'CasualSpawn':
        return pack('I',v[0])+label(v[1])
    if tag == 'Mission':
        return pack('I',v[0])+label(v[1],v[2])
    if tag == 'Bar':
        return label(*v)
    if tag == 'Bgm':
        return pack('HH',*v)
    return pack('I'*len(v), *v)

# Timeline fields with verified operand layouts (H50 event reader and D1 eh05).
EVENT_FIELDS = {
    'event8': ('HH','frame parameter'),
    'event18': ('HHH','frame duration mode'),
    'event21': ('4H','frame line message hide'),
    'event46': ('HHBBBB','frame flags level_a level_b time slot')
}
