"""Two audited KH2 PSMT8 materials and six legacy BC3 leaves."""
import struct


def u64(data, pos):
    return struct.unpack_from('<Q', data, pos)[0]


def material(blob, slot, index):
    # The two pinned BARs use PSMT8 with a PSMCT32 GS upload.
    kind, _, _, offset, length = struct.unpack_from('<HH4sII', blob, 16 + slot*16)
    raw = memoryview(blob)[offset:offset+length]
    _, _, _, _, ids, xf, gs, _, pal = struct.unpack_from('<9I', raw)
    reg = u64(raw, gs+index*160+112)
    transfer = xf+(raw[ids+index]+1)*144
    if kind != 7 or (reg >> 20 & 63) != 19 or (u64(raw, transfer+32) >> 56 & 63):
        raise ValueError('unsupported material upload')
    start = struct.unpack_from('<I', raw, transfer+116)[0]
    w, h = 1 << (reg >> 26 & 15), 1 << (reg >> 30 & 15)
    cbp = (reg >> 37 & 16383) - (u64(raw, xf+32) >> 32 & 16383)
    csa = reg >> 56 & 31
    palette = []
    for i in range(256):
        k = ((i&7)+(i&8)*8+(i&16)//2+(i&~31)*4+(cbp&7)*4+
             (cbp&8)*128+(cbp&16)*2+(cbp&~31)*64+(csa&1)*8+(csa&14)*64)
        r,g,b,a = raw[pal+4*k:pal+4*k+4]
        palette.append(bytes((b,g,r,min(255,2*a))))
    words = [0]*(w*h//4)
    for y in range(h//2):
        for x in range(w//2):
            page = (y//32*(w//128)+x//64)*2048
            block = (x>>3&1)+4*(x>>4&1)+16*(x>>5&1)+2*(y>>3&1)+8*(y>>4&1)
            word = 64*block+16*(y//2%4)+(x&1)+4*(x>>1&1)+8*(x>>2&1)+2*(y&1)
            words[page+word] = y*(w//2)+x
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            block = (x>>4&1)+4*(x>>5&1)+16*(x>>6&1)+2*(y>>4&1)+8*(y>>5&1)
            byte = ((x&1)*4 ^ (x>>1&1)*16 ^ (x>>2&1)*32 ^ (x>>3&1)*2 ^
                    (y&1)*8 ^ (y>>1&1)*33 ^ (y>>2&1)*32)
            addr = (y//64*(w//128)+x//128)*8192+256*block+64*(y//4%4)+byte
            pixels.extend(palette[raw[start+4*words[addr//4]+addr%4]])
    return w, h, pixels


def bgra(blob, slot, index, levels):
    w,h,pixels = material(blob, slot, index)
    mip = levels > 1
    header = [124, 0x21007 if mip else 0x2100f, h,w,4*w,int(not mip),levels,
              *([0]*11),32,0x41,0,32,0xff0000,0xff00,0xff,0xff000000,
              0x401008 if mip else 0x1000,0,0,0,0]
    result = bytearray(b'DDS '+struct.pack('<31I', *header))
    result.extend(pixels)
    for level in range(1, levels):
        smaller = bytearray()
        for y in range(0,h,2):
            for x in range(0,w,2):
                off = 4*(y*w+x)
                for k in range(off,off+4):
                    smaller.append((pixels[k]+pixels[k+4]+pixels[k+4*w]+pixels[k+4*w+4])//4)
        result.extend(smaller)
        w,h,pixels = w//2,h//2,smaller
    return result


def bc3(blob):
    h,w = struct.unpack_from('<II',blob,12)
    header = [124,0x1007,h,w,0,1,1,*([0]*11),32,4,0x35545844,*([0]*5),0x1000,0,0,0,0]
    out = bytearray(b'DDS '+struct.pack('<31I',*header))
    if blob[84:88] != b'DXT5':
        if any(blob[128:]) or len(blob) != 128+4*w*h:
            raise ValueError('expected transparent BGRA source')
        return out + (bytes((0,255))+bytes(14))*(w*h//16)
    opaque = bytes((0,255)) + ((8**16-1)//7).to_bytes(6,'little')
    for i in range(128,128+w*h,16):
        color0,color1,indices = struct.unpack_from('<HHI',blob,i+8)
        if indices == 0:
            color1 = color0
        elif indices == 0x55555555:
            color0,indices = color1,0
        out.extend(opaque + struct.pack('<HHI',color0,color1,indices))
    return out
