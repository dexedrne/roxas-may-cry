"""Small collision edits. Type 23 uses a 64-byte header and 20-byte attached shapes."""
import struct

SHAPE = struct.Struct('<BBhBB7h')
FIELDS = 'group part argument type shape bone x y z height radius extra_height'.split()


def shapes(data):
    if len(data)<64:
        return None
    count, = struct.unpack_from('<I',data)
    if 64+count*SHAPE.size != len(data):
        return None
    return list(SHAPE.iter_unpack(data[64:]))


def difference(before, after):
    old, new = shapes(before), shapes(after)
    if old is None or new is None or before[4:64]!=after[4:64] or not new:
        return
    if len(old)==len(new):
        edits = []
        for i,(a,b) in enumerate(zip(old,new)):
            for field,prev,value in zip(FIELDS,a,b):
                if prev!=value:
                    edits.append((i,field,prev,value))
                    if len(edits)>12:
                        return
        return edits
    if 0 < len(old)-len(new) <= 8:
        j = 0
        edits = []
        for i,row in enumerate(old):
            if j<len(new) and new[j]==row:
                j += 1
            else:
                edits.append((i,'remove','',''))
        if j==len(new):
            return edits


def apply(data, edits):
    records = shapes(data)
    if records is None:
        raise ValueError('invalid collision table')
    remove = set()
    for row in edits:
        i = int(row['row'])
        if not 0<=i<len(records):
            raise ValueError('invalid collision selector')
        field = row['field']
        if field=='remove':
            remove.add(i)
        else:
            j = FIELDS.index(field)
            if records[i][j]!=int(row['expect']):
                raise ValueError('collision field guard')
            fields = list(records[i])
            fields[j] = int(row['write'])
            records[i] = fields
    return struct.pack('<I',len(records)-len(remove))+data[4:64]+b''.join(
        SHAPE.pack(*r) for i,r in enumerate(records) if i not in remove)
