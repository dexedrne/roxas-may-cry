"""Optional exact Neverland textures from the supported European Days cartridge."""
import csv
from functools import lru_cache
import hashlib
import struct
from pathlib import Path
import numpy as np
from PIL import Image

ROM_SHA='1ecf5e7a41a2ae48e7c5fd3f678e5a79e854d843f1a1fa3a952a41b14e51ec4f'
MAX_OUTPUT_SIZE=16*1024*1024
def u16(b,p):return struct.unpack_from('<H',b,p)[0]
def u32(b,p):return struct.unpack_from('<I',b,p)[0]

def expected_size(data: bytes) -> int:
    if len(data) < 4:
        raise ValueError("Compressed stream is smaller than the 4-byte Nitro header.")

    size = int.from_bytes(data[1:4], "little")
    if size == 0:
        if len(data) < 8:
            raise ValueError("Extended-size Nitro stream is truncated.")
        size = int.from_bytes(data[4:8], "little")

    if size <= 0:
        raise ValueError("Decompressed size is zero.")
    if size > MAX_OUTPUT_SIZE:
        raise ValueError(f"Refusing to allocate {size} bytes; maximum is {MAX_OUTPUT_SIZE}.")
    return size


def decompress_lz10(data: bytes) -> bytes:
    if not data or data[0] != 0x10:
        raise ValueError("Not a Nintendo DS LZ10 stream.")

    output_size = expected_size(data)
    cursor = 4
    output = bytearray()

    while len(output) < output_size:
        if cursor >= len(data):
            raise ValueError("LZ10 stream ended before the output was complete.")

        flags = data[cursor]
        cursor += 1

        for bit in range(7, -1, -1):
            if len(output) >= output_size:
                break

            compressed = (flags >> bit) & 1
            if not compressed:
                if cursor >= len(data):
                    raise ValueError("LZ10 literal byte is truncated.")
                output.append(data[cursor])
                cursor += 1
                continue

            if cursor + 1 >= len(data):
                raise ValueError("LZ10 back-reference is truncated.")

            first = data[cursor]
            second = data[cursor + 1]
            cursor += 2

            length = (first >> 4) + 3
            displacement = (((first & 0x0F) << 8) | second) + 1

            if displacement > len(output):
                raise ValueError(f"Invalid LZ10 displacement {displacement} at output offset {len(output)}.")

            for _ in range(length):
                if len(output) >= output_size:
                    break
                output.append(output[-displacement])

    return bytes(output)


def dictionary(b, p):
    header = p+u16(b,p+6)
    names = header+u16(b,header+2)
    return {b[names+16*i:names+16*i+16].split(b'\0')[0].decode('ascii', 'backslashreplace'):
            header+4+i*u16(b,header) for i in range(b[p+1])}


def sections(b, kind=b'TEX0'):
    for i in range(u16(b, 14)):
        p = u32(b, 16+4*i)
        if b[p:p+4] == kind:
            yield p


def decoded(b, t, entry, pal):
    v = u32(b, entry)
    fmt, w, h = (v >> 26) & 7, 8 << ((v >> 20) & 7), 8 << ((v >> 23) & 7)
    p = t + u32(b, t+20) + 8*(v & 65535)
    n = w*h
    alpha = np.full(n, 255, dtype='u1')
    if fmt == 7:
        c = np.frombuffer(b, '<u2', n, p)
        alpha = ((c >> 15)*255).astype('u1')
    else:
        bits = {1:8, 2:2, 3:4, 4:8, 6:8}[fmt]
        raw = np.frombuffer(b, 'u1', n*bits//8, p)
        ids = raw
        if bits < 8:
            ids = np.stack([(raw>>k) & ((1<<bits)-1) for k in range(0,8,bits)], axis=1).ravel()
        if fmt in (1, 6):
            k = 5 if fmt == 1 else 3
            ids = raw & ((1 << k)-1)
            a = (raw >> k).astype('<u2')
            if k == 5:
                a = a*4 + a//2
            alpha = (a*255//31).astype('u1')
        elif (v >> 29) & 1:
            alpha[ids == 0] = 0
        off = t+u32(b,t+56)+8*u16(b,pal)
        c = np.frombuffer(b,'<u2',int(ids.max())+1,off)[ids]
    rgb = [(c>>k)&31 for k in (0,5,10)]
    rgba = np.stack([x*8+(x>>2) for x in rgb]+[alpha],axis=1).astype('u1').reshape(h,w,4)
    return Image.fromarray(rgba)


def gs_alpha(im, method):
    a = np.asarray(im).copy()
    a5 = (a[:,:,3].astype('u2')*31+127)//255
    a7 = (a5*128+15)//31
    if method == 'gs-alpha-double':
        a[:,:,3] = np.minimum(a7*2,255)
    elif method == 'gs-alpha-scale':
        a[:,:,3] = a7*255//128
    else:
        raise ValueError(method)
    return Image.fromarray(a)


def dds_bgra(im, flags):
    # Both layouts are legacy uncompressed BGRA8 with a single actual image.
    if flags not in (0x1007,0x2100f):
        raise ValueError(f'unsupported DDS flags {flags:#x}')
    pitch = flags == 0x2100f
    header = [
        124, flags, im.height, im.width, im.width*4 if pitch else 0, int(pitch), int(pitch),
        *([0]*11), 32, 0x41, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000,
        0x1000 if pitch else 0x1002, 0, 0, 0, 0,
    ]
    return b'DDS '+struct.pack('<31I',*header)+im.tobytes('raw','BGRA')


def build(rom):
    with rom.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=ROM_SHA:
            raise ValueError('unsupported Days ROM; expected European revision 0')
        # Pinned ROM FAT entry mi/wd/wd_pp.
        stream.seek(90854400)
        world=stream.read(619008)
    count=u16(world,2)&0x7fff
    @lru_cache(None)
    def container(index,section,item,pin):
        pos=u32(world,12)+512*u16(world,16+2*index)
        length=u32(world,16+4*((count+1)//2+index))
        if not length&0x80000000:raise ValueError('expected compressed Days archive')
        area=decompress_lz10(world[pos:pos+(length&0x7fffffff)])
        table=u32(area,8+4*section)
        n=u32(area,table)
        if item>=n:raise ValueError('missing Days model')
        off=u32(area,table+4+4*item)
        size=u32(area,table+4+4*(n+item))
        data=area[off:off+size]
        if hashlib.sha256(data).hexdigest()!=pin:raise ValueError('Days texture container differs')
        return data
    for r in csv.DictReader(Path(__file__).with_name('days.tsv').open(),delimiter='\t'):
        _,model,slot=r['days_member'].split('#')
        section,i=map(int,slot.split('/'))
        data=container(int(model.split('-')[0]),section,i,r['days_container_sha256'])
        tex,=sections(data)
        texname,palname=r['days_texture'].split('@')
        tx=dictionary(data,tex+u16(data,tex+14))
        pl=dictionary(data,tex+u16(data,tex+52))
        image=decoded(data,tex,tx[texname],pl[palname])
        size=int(r['width']),int(r['height'])
        if r['method']=='nearest-exact':
            image=image.resize(size,Image.Resampling.NEAREST)
        elif r['method']!='pixel-exact':
            image=gs_alpha(image,r['method'])
        if image.size!=size:raise ValueError('Days texture dimensions differ')
        yield r['nmc'],dds_bgra(image,int(r['dds_flags'],16)),r['output_sha256']
