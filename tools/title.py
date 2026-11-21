"""Compose both title textures from owned atlases and the typeset subtitle."""
import json
import math
from pathlib import Path
import struct
import zlib

ART = Path(__file__).resolve().parents[1] / 'art'


def paeth(a,b,c):
    p = a+b-c
    x,y,z = abs(p-a),abs(p-b),abs(p-c)
    return a if x <= y and x <= z else b if y <= z else c


def decode(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('expected title PNG')
    w,h,depth,kind,_,_,interlace = struct.unpack_from('>IIBBBBB',data,16)
    if (depth,kind,interlace) != (8,6,0):
        raise ValueError('expected 8-bit RGBA title texture')
    payload = bytearray()
    k = 8
    while k < len(data):
        n,tag = struct.unpack_from('>I4s',data,k)
        if tag == b'IDAT':
            payload.extend(data[k+8:k+8+n])
        k += 12+n
    raw = zlib.decompress(payload)
    stride = w*4
    if len(raw) != (stride+1)*h:
        raise ValueError('invalid title texture dimensions')
    prev = bytes(stride)
    rows = []
    for start in range(0,len(raw),stride+1):
        mode = raw[start]
        row = bytearray(raw[start+1:start+stride+1])
        if mode > 4:
            raise ValueError('unknown PNG filter')
        for i in range(stride) if mode else ():
            a = row[i-4] if i >= 4 else 0
            b = prev[i]
            c = prev[i-4] if i >= 4 else 0
            pred = a if mode==1 else b if mode==2 else (a+b)//2 if mode==3 else paeth(a,b,c)
            row[i] = (row[i]+pred)&255
        rows.append(row)
        prev = row
    return rows


def encode(rows,filters):
    def chunk(name,data):
        return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data))
    prev = bytes(len(rows[0]))
    compressor = zlib.compressobj(6,zlib.DEFLATED,15,9,zlib.Z_FILTERED)
    blocks = []
    for mode,row in zip(filters,rows):
        left = b'\0'*4+row[:-4]
        if mode==0:
            data=row
        elif mode==2:
            data=bytes((a-b)&255 for a,b in zip(row,prev))
        elif mode==1:
            data=bytes((a-b)&255 for a,b in zip(row,left))
        else:
            data=bytes((v-paeth(a,b,c))&255 for v,a,b,c in zip(row,left,prev,b'\0'*4+prev[:-4]))
        blocks.append(compressor.compress(bytes([mode])+data))
        prev=row
    payload=b''.join(blocks)+compressor.flush()
    header=struct.pack('>IIBBBBB',len(prev)//4,len(rows),8,6,0,0,0)
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+
            b''.join(chunk(b'IDAT',payload[i:i+65536]) for i in range(0,len(payload),65536))+chunk(b'IEND',b''))


def transpose(rows):
    channels=[list(zip(*(r[c::4] for r in rows))) for c in range(4)]
    result=[]
    for column in zip(*channels):
        line=bytearray(4*len(rows))
        for c,values in enumerate(column):
            line[c::4]=bytes(values)
        result.append(line)
    return result


def resample(rows,count):
    # Each 32-bit lane holds one channel's 22-bit fixed-point convolution.
    # Python integers apply the same filter to the entire scanline in C.
    length=len(rows[0])
    lanes=[]
    for row in rows:
        packed=bytearray(length*4)
        packed[::4]=row
        lanes.append(int.from_bytes(packed,'little'))
    unit=int.from_bytes(b'\x01\0\0\0'*length,'little')
    bias=unit*((1<<30)+(1<<21))
    scale=len(rows)/count
    factor=max(1.,scale)
    result=[]
    def sinc(x):
        return math.sin(math.pi*x)/(math.pi*x) if x else 1.
    for i in range(count):
        middle=(i+.5)*scale
        left=max(0,int(middle-3*factor+.5))
        right=min(len(rows),int(middle+3*factor+.5))
        x=[(j-middle+.5)/factor for j in range(left,right)]
        weights=[sinc(v)*sinc(v/3) if -3 <= v < 3 else 0 for v in x]
        total=sum(weights)
        acc=bias
        for j,w in enumerate(weights,left):
            q=int(w/total*(1<<22)+(.5 if w>=0 else -.5))
            acc+=lanes[j]*q
        raw=acc.to_bytes(4*length,'little')
        result.append(bytes(max(0,min(255,((hi<<2)|(lo>>6))-256)) for hi,lo in zip(raw[3::4],raw[2::4])))
    return result


def alpha(rows,inverse=False):
    for i,row in enumerate(rows):
        line=bytearray(row)
        for c in range(3):
            if inverse:
                line[c::4]=bytes(min(255,255*v//a) if 0<a<255 else v for v,a in zip(row[c::4],row[3::4]))
            else:
                line[c::4]=bytes((v*a+127)//255 for v,a in zip(row[c::4],row[3::4]))
        rows[i]=line
    return rows


def resize(rows,w,h):
    src=alpha(list(rows))
    return alpha(resample(transpose(resample(transpose(src),w)),h),True)


def native(layout,atlas,name,logo_size,plan):
    d=bytearray(layout)
    sprites,_,parts=struct.unpack_from('<3I',d,0x7f4c+12)
    w,h=logo_size
    factor=min(350/w,188/h)
    w,h=round(w*factor),round(h*factor)
    struct.pack_into('<4i',d,0x7f4c+sprites+92,4,68,508,508)
    pos=0x7f4c+parts
    d[pos:pos+100]=b''.join(bytes(16)+d[pos+i*20+16:pos+i*20+20] for i in range(5))
    struct.pack_into('<4i',d,pos+100,-w//2,-h//2+13,w-w//2,h-h//2+13)
    off=0x128bd0
    pixels,_,palette,_=struct.unpack_from('<4I',d,off+8)
    def address(x,y):
        return (y&~15)*512+(x&~15)*2+(((y&~3)//2+(y&1))%8)*1024+((x+(((y+2)//4)&1)*4)&7)*4+((y//2)&1)+((x//4)&2)
    keep={d[off+pixels+address(x,y)] for y in range(33) for x in range(316,512)}
    free=sorted(set(range(256))-keep)
    keys={int(k):v for k,v in plan['keys'].items()}
    sums=[[0]*5 for _ in free]
    for y,row in enumerate(resize(atlas,512,512)):
        for x,(r,g,b,a) in enumerate(struct.iter_unpack('4B',row)):
            idx=keys[((r<<24|g<<16|b<<8|a)&plan['mask'])]
            s=sums[idx]
            for c,v in enumerate((r,g,b,a,1)):
                s[c]+=v
            if y>=33 or x<316:
                d[off+pixels+address(x,y)]=free[idx]
    for idx,(r,g,b,a,n) in zip(free,sums):
        slot=(idx&0xe7)|((idx&8)*2)|((idx&16)//2)
        if n:
            struct.pack_into('4B',d,off+palette+4*slot,r//n,g//n,b//n,(a//n+1)//2)
        else:
            struct.pack_into('4B',d,off+palette+4*slot,0,0,0,0)
    tag=('roxas','xion','xkb').index(name)+1
    return d+bytes(tag)+b'T80L'+bytes([tag])


def compose(name,retail,mod_atlas,layout):
    plan=json.loads((ART/(name+'.json')).read_text())
    sub=decode((ART/(name+'-name.png')).read_bytes())
    w,h=634,193
    logo=[bytearray(w*4) for _ in range(h-30+len(sub))]
    for y,row in enumerate(sub,h-30):
        start=(w-len(row)//4)//2*4
        logo[y][start:start+len(row)]=row
    for y,row in enumerate(retail[551:744]):
        for x in range(w):
            pixel=row[(68+x)*4:(69+x)*4]
            if pixel[3]:
                logo[y][4*x:4*x+4]=pixel
    for row in logo:
        row[3::4]=bytes(255 if a>250 else a for a in row[3::4])
    atlas=[bytearray(8192) for _ in range(2048)]
    for y,row in enumerate(mod_atlas[:66]):
        credit=b''.join(bytes(pixel)*2 for pixel in struct.iter_unpack('4B',row[632*4:]))
        for dy in range(2):
            atlas[2*y+dy][1264*4:]=credit
    for y,row in enumerate(resize(logo,2016,1760),272):
        atlas[y][64:64+len(row)]=row
    return encode(atlas,plan['filters']),native(layout,atlas,name,(w,len(logo)),plan)
