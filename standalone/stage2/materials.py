"""Decode KH2 retail materials and render recipe-selected mip surfaces."""
import struct
from functools import lru_cache
from PIL import Image
import numpy as np
R = Image.Resampling

@lru_cache(maxsize=12)
def upload_order(bw,bh):
    # GS PSMCT32 pages are 64x32 words, arranged in 8x8 blocks.
    y,x = np.indices((bh*32,bw*64),dtype='i4')
    page = (y//32*bw+x//64)*2048
    block = (x>>3&1) + 4*(x>>4&1) + 16*(x>>5&1) + 2*(y>>3&1) + 8*(y>>4&1)
    word = 64*block+16*(y//2%4)+(x&1)+4*(x>>1&1)+8*(x>>2&1)+2*(y&1)
    return np.argsort((page+word).ravel())


def encode32(data,bw,bh):
    return np.frombuffer(data,'<u4')[upload_order(bw,bh)].tobytes()


@lru_cache(maxsize=12)
def permutation(w,h):
    y,x = np.indices((h,w),dtype='i4')
    block = (x>>4&1)+4*(x>>5&1)+16*(x>>6&1)+2*(y>>4&1)+8*(y>>5&1)
    byte = ((x&1)*4 ^ (x>>1&1)*16 ^ (x>>2&1)*32 ^ (x>>3&1)*2 ^
            (y&1)*8 ^ (y>>1&1)*33 ^ (y>>2&1)*32)
    address = ((y//64*(w//128)+x//128)*8192+256*block+64*(y//4%4)+byte).ravel()
    return 4*upload_order(w//128,h//64)[address//4]+address%4


@lru_cache(maxsize=12)
def nibble_order(w,h):
    # GS PSMT4 uses 128x128 pages, 32x16 blocks and 32x4 columns.
    y,x = np.indices((h,w),dtype='i4')
    block = ((y//16&1) + 2*(x//32&1) + 4*(y//32&1) +
             8*(x//64&1) + 16*(y//64&1))
    addr = ((x&1)*8 ^ (x>>1&1)*32 ^ (x>>2&1)*64 ^ (x>>3&1)*2 ^
            (x>>4&1)*4 ^ (y&1)*16 ^ (y>>1&1)*65 ^ (y>>2&1)*64)
    return ((y//128*(w//128)+x//128)*16384 + block*512 + (y//4%4)*128 + addr).ravel()


def effect_images(raw, offsets=None):
    if raw[:4]!=b'PAX_':
        return
    def u(off):
        return struct.unpack_from('<I',raw,off)[0]
    dx = u(12)
    count_pos = dx+16+32*u(dx+12)
    for bank in range(u(count_pos)):
        base = dx+u(count_pos+4+bank*4)
        if u(base)!=0x96:
            continue
        directory = base+8+4*u(base+4)
        for j in range(u(directory)):
            pos = base+u(directory+4+j*4)
            _,_,_,psm,_,_,w,h = struct.unpack_from('<8H',raw,pos)
            length = u(pos+24)
            if psm!=19 or w*h!=length or pos+32+length+1024>len(raw):
                continue
            start = pos+32
            palette = np.frombuffer(raw,'u1',1024,start+length).reshape(256,4)
            ix = np.arange(256)
            palette = palette[(ix&~24)+(ix&8)*2+(ix&16)//2]
            palette[:,3] = np.minimum(255,palette[:,3].astype('u2')*2)
            indexes = np.frombuffer(raw,'u1',length,start).reshape(h,w)
            material=f'{bank}.{j}'
            if offsets is not None:
                offsets[material]=start
            yield material,Image.fromarray(palette[indexes])


def materials(blob, skipped, bindings=None):
    count, = struct.unpack_from('<I', blob, 4)
    for slot, (kind, link, _, off, size) in enumerate(struct.iter_unpack('<HH4sII', blob[16:16+count*16])):
        if kind == 18:
            dpd_offsets={}
            for material, im in effect_images(memoryview(blob)[off:off+size],dpd_offsets):
                if bindings is not None:
                    bindings[slot,material]=0x20000000+off+dpd_offsets[material]
                yield slot, material, im
        if kind != 7 or size < 36:
            continue
        raw = memoryview(blob)[off:off+size]
        flag, colors, sets, nm, ids, xf, gs, pixels, pal = struct.unpack_from('<9I', raw)
        if flag or gs+nm*160 > size or pal+colors*4 > size:
            skipped['unsupported member header'] += 1
            continue
        base = struct.unpack_from('<Q', raw, xf+32)[0] >> 32 & 16383
        count8=sum((struct.unpack_from('<Q',raw,gs+j*160+112)[0]>>20&63)!=20 for j in range(nm))
        p4=p8=0
        for i in range(nm):
            reg = struct.unpack_from('<Q', raw, gs+i*160+112)[0]
            w, h = 1 << (reg>>26&15), 1 << (reg>>30&15)
            j = raw[ids+i]
            if j >= sets:
                raise ValueError(f'invalid texture transfer {j}/{sets}')
            transfer = xf+(j+1)*144
            upload = struct.unpack_from('<Q', raw, transfer+32)[0] >> 56 & 63
            start = struct.unpack_from('<I', raw, transfer+116)[0]
            psm = reg>>20&63
            if bindings is not None and j<128:
                delta=p4+count8*16 if psm==20 else p8*16
                bindings[slot,i]=0x20000000+off+start+delta
            if psm==20:
                p4+=1
            else:
                p8+=1
            if psm not in (19,20) or upload not in (0,psm) or (upload == 0 and (w%128 or h%(128 if psm==20 else 64))):
                skipped[f'PSM {psm} upload {upload} or partial page'] += 1
                continue
            cbp = (reg>>37&16383)-base
            csa = reg>>56&31
            ix = np.arange(16 if psm==20 else 256)
            ci = ((ix&7)+(ix&8)*8+(ix&16)//2+(ix&~31)*4+(cbp&7)*4+
                  (cbp&8)*128+(cbp&16)*2+(cbp&~31)*64+(csa&1)*8+(csa&14)*64)
            length = w*h//(2 if psm==20 else 1)
            if cbp < 0 or max(ci) >= colors or not pixels <= start <= pal-length:
                skipped['unsupported pixel or palette bounds'] += 1
                continue
            palette = np.frombuffer(raw, 'u1', colors*4, pal).reshape(-1,4)[ci]
            palette[:,3] = np.minimum(255, 2*palette[:,3].astype('u2'))
            indices = np.frombuffer(raw, 'u1', length, start)
            if psm==20:
                if upload==0:
                    gs_bytes = np.frombuffer(encode32(indices.tobytes(),w//128,h//128),'u1')
                    order = nibble_order(w,h)
                    indices = (gs_bytes[order//2] >> (4*(order%2))) & 15
                else:
                    indices = np.stack((indices&15,indices>>4),axis=1).ravel()
            elif upload == 0:
                indices = indices[permutation(w,h)]
            yield slot, i, Image.fromarray(palette[indices.reshape(h,w)])


def scale2x(im):
    a = np.asarray(im).view("<u4").reshape(im.height, im.width)
    p = np.pad(a, 1, mode="edge")
    top, left, right, bottom = p[:-2, 1:-1], p[1:-1, :-2], p[1:-1, 2:], p[2:, 1:-1]
    edge = (top != bottom) & (left != right)
    out = np.empty((im.height*2, im.width*2), dtype="<u4")
    for y, v in enumerate((top, bottom)):
        for x, u in enumerate((left, right)):
            out[y::2, x::2] = np.where(edge & (u == v), u, a)
    return Image.fromarray(out.view("u1").reshape(*out.shape, 4))


def straight(im, size, kernel):
    return Image.merge("RGBA", [c.resize(size, kernel) for c in im.split()])


def render(src, size, method):
    """method = KERNEL[-wrap][-straight] | pad | native | scale2x.  Pillow resizes RGBA premultiplied; -straight does not."""
    if method == "native":
        if src.size != size:
            raise ValueError("native size mismatch")
        return src
    if method == "pad":
        im = Image.new("RGBA", size)
        im.paste(src)
        return im
    if method == "scale2x":
        return scale2x(src).resize(size, R.LANCZOS)
    parts = method.split("-")
    kernel = R[parts[0]]
    fn = straight if "straight" in parts else (lambda im, s, k: im.resize(s, k))
    if "wrap" in parts:
        k = 8
        fx, fy = size[0] // src.width, size[1] // src.height
        if fx * src.width != size[0] or fy * src.height != size[1]:
            raise ValueError("wrap needs an integer factor")
        a = np.pad(np.asarray(src), ((k, k), (k, k), (0, 0)), mode="wrap")
        big = fn(Image.fromarray(a), ((src.width+2*k)*fx, (src.height+2*k)*fy), kernel)
        return big.crop((k*fx, k*fy, k*fx+size[0], k*fy+size[1]))
    return fn(src, size, kernel)


