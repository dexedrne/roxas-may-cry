"""Read the nine PS2 NMC donors used by the Xemnas I costume and reward icon."""
import hashlib
import struct


def extent(stream,name):
    stream.seek(16*2048)
    volume=stream.read(2048)
    if volume[1:6]!=b'CD001':
        raise ValueError('expected an ISO9660 PS2 disc')
    sector,size=struct.unpack_from('<IxxxxI',volume,158)
    if size>8*1024*1024:
        raise ValueError('invalid ISO root directory')
    stream.seek(sector*2048)
    rows=stream.read(size)
    i=0
    while i<len(rows):
        length=rows[i]
        if not length:
            i=(i//2048+1)*2048
            continue
        if length<34 or i+length>len(rows):
            raise ValueError('invalid ISO directory record')
        if rows[i+33:i+33+rows[i+32]].split(b';')[0]==name:
            sector,size=struct.unpack_from('<IxxxxI',rows,i+2)
            return sector*2048,size
        i+=length
    raise ValueError(f'missing ISO member {name!r}')


def kh_hash(name):
    value=0xffffffff
    for b in name.encode():
        value ^= b<<24
        for _ in range(8):
            value=((value<<1)^(0x04c11db7 if value&0x80000000 else 0))&0xffffffff
    return ~value&0xffffffff


def unpack(data,size):
    data=data.rstrip(b'\0')
    if len(data)<5 or int.from_bytes(data[-5:-1],'big')!=size:
        raise ValueError('invalid KH2 compressed size')
    key=data[-1]
    i=len(data)-6
    out=bytearray(size)
    j=size
    while j and i>=0:
        v=data[i]
        i-=1
        if v==key:
            if i<0:
                raise ValueError('truncated KH2 escape')
            distance=data[i]
            i-=1
            if distance:
                if i<0 or j+distance>size:
                    raise ValueError('invalid KH2 backreference')
                count=data[i]+3
                i-=1
                for _ in range(min(j,count)):
                    j-=1
                    out[j]=out[j+distance]
                continue
        j-=1
        out[j]=v
    if j:
        raise ValueError('short KH2 payload')
    return bytes(out)


def read(path,name,pin):
    if path.is_dir():
        blob=(path/name).read_bytes()
    else:
        with path.open('rb') as stream:
            offset,length=extent(stream,b'KH2.IDX')
            if not 4<=length<=8*1024*1024:
                raise ValueError('invalid KH2 index length')
            stream.seek(offset)
            index=stream.read(length)
            count,=struct.unpack_from('<I',index)
            if count>(len(index)-4)//16:
                raise ValueError('truncated KH2 index')
            key=kh_hash(name)
            entries=[row for row in struct.iter_unpack('<IHHII',index[4:4+count*16]) if row[0]==key]
            if len(entries)!=1:
                raise ValueError(f'missing or ambiguous PS2 donor: {name}')
            _,_,flags,sector,size=entries[0]
            offset,length=extent(stream,b'KH2.IMG')
            stored=((flags&0x3fff)+1)*2048 if flags&0x4000 else size
            if size>16*1024*1024 or stored>length-sector*2048:
                raise ValueError('invalid PS2 asset extent')
            stream.seek(offset+sector*2048)
            blob=stream.read(stored)
            if flags&0x4000:
                blob=unpack(blob,size)
    if hashlib.sha256(blob).hexdigest()!=pin:
        raise ValueError(f'unsupported PS2 donor: {name}')
    return blob


def members(data):
    count,=struct.unpack_from('<I',data,4)
    if data[:4]!=b'BAR\1' or count>(len(data)-16)//16:
        raise ValueError('invalid model archive')
    return {kind:(16+16*i,off,size) for i,(kind,_,off,size) in
            enumerate(struct.iter_unpack('<I4sII',data[16:16+16*count])) if kind in (4,7)}


def costume(pc,ps2):
    out=bytearray(pc)
    slots=members(pc)
    for kind,(_,offset,size) in members(ps2).items():
        row,dest,capacity=slots[kind]
        if size>capacity:
            out.extend(bytes(-len(out)%16))
            dest=len(out)
        out[dest:dest+size]=ps2[offset:offset+size]
        struct.pack_into('<II',out,row+8,dest,size)
    return bytes(out)
