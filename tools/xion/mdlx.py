"""KH2 entity BAR codec for the packet formats used by the two milestone controls."""
import math
import struct

import numpy as np

R = struct.unpack_from
P = struct.pack_into


def directory(blob):
    assert blob[:4] == b'BAR\1'
    entries = [R('<HH4sII', blob, 16 + 16*i) for i in range(R('<I', blob, 4)[0])]
    assert all(not size or 16+16*len(entries) <= off <= off+size <= len(blob)
               for _, _, _, off, size in entries)
    return entries


class Archive:
    def __init__(self, blob):
        entries = directory(blob)
        self.header = blob[:16]
        self.members = {}
        self.rows = []
        cursor = 16+16*len(entries)
        for kind, link, name, off, size in entries:
            assert off >= cursor and kind not in self.members
            self.rows.append((kind, link, name, blob[cursor:off]))
            self.members[kind] = blob[off:off+size]
            cursor = off+size
        self.tail = blob[cursor:]

    def write(self):
        out = bytearray(self.header + bytes(16*len(self.rows)))
        for i,(kind,link,name,pad) in enumerate(self.rows):
            out.extend(pad)
            out.extend(bytes(-len(out) % 16))
            data = self.members[kind]
            P('<HH4sII', out, 16+16*i, kind, link, name, len(out), len(data))
            out.extend(data)
        return bytes(out) + self.tail


def world(srt, parents):
    matrices = []
    for (scale, rotation, translation), parent in zip(srt, parents):
        x,y,z = rotation[:3]
        a,b,c = map(math.cos,(x,y,z))
        d,e,f = map(math.sin,(x,y,z))
        m = np.eye(4)
        m[:3,:3] = np.array([[c*b,c*e*d-f*a,c*e*a+f*d],
                               [f*b,f*e*d+c*a,f*e*a-c*d],[-e,b*d,b*a]]) * scale[:3]
        m[:3,3] = translation[:3]
        matrices.append(m if parent < 0 else matrices[parent] @ m)
    return np.array(matrices)


class Packet:
    def __init__(self, data, offset, end, palette, shadow):
        mem = np.zeros((1024,4),dtype='<i4')
        locations = {}
        strip_fields = {}
        p = offset
        mask = 0
        column = (0,)*4
        while p < end:
            imm,n,cmd = R('<HBB', data, p)
            p += 4
            if cmd == 0: continue
            if cmd == 1:
                assert imm == 0x101
                continue
            if cmd == 0x20:
                mask, = R('<I',data,p)
                p += 4
                continue
            if cmd == 0x31:
                column = R('<4i',data,p)
                p += 16
                continue
            assert cmd in (0x65,0x72,0x6c,0x78)
            dst = imm & 1023
            lanes = ((cmd >> 2) & 3)+1
            size = 4 >> (cmd & 3)
            dtype = ('<u' if imm & 0x4000 else '<i') + str(size)
            values = np.frombuffer(data,dtype,n*lanes,p).reshape(n,lanes)
            assert dst+n<=1024 and n
            locations[(cmd,dst)] = p
            if cmd == 0x72:
                assert mask in (0xcfcfcfcf,0x3f3f3f3f)
                strip_fields[2 if mask==0xcfcfcfcf else 3] = p
            for k in range(4):
                mode = (mask >> (2*k)) & 3 if cmd & 16 else 0
                if mode == 0:
                    mem[dst:dst+n,k] = values[:,k % lanes]
                elif mode == 2:
                    assert len(set(column)) == 1
                    mem[dst:dst+n,k] = column[0]
                else: assert mode == 3
            p += (n*lanes*size+3)&~3
        assert p == end
        h = mem.ravel()[:16]
        if shadow:
            # Type 2 shadow packets omit color and weight fields.
            verts,coord,_,nm = h[8:12]
            groups = 0
        else:
            verts,coord,_,nm = h[12:16]
            groups = h[10]
        assert nm == len(palette) and h[7]+4*nm <= 1024
        self.nodes = mem[h[5]:h[5]+h[4]].copy()
        self.local = mem[coord:coord+verts].view('<f4').astype(float)
        self.source_w = self.local[:,3].copy()
        self.xyz_offset = locations.get((0x78,coord))
        self.coord_offset = self.xyz_offset or locations[(0x6c,coord)]
        self.strip_fields = strip_fields
        self.uv_offset = locations.get((0x65,h[5]))
        self.palette = tuple(palette)
        counts = mem[h[6]:].ravel()[:nm]
        self.joints = np.repeat(palette, counts)
        assert len(self.joints) == verts
        self.mix = []
        if groups:
            counts = mem[h[11]:].ravel()[:groups]
            cursor = h[14]*4
            for n,count in enumerate(counts,1):
                for _ in range(count):
                    self.mix.append(mem.ravel()[cursor:cursor+n].copy())
                    cursor += n
                cursor = (cursor+3)&~3
            for indices in self.mix:
                assert min(indices)>=0 and max(indices)<verts
                if len(indices)==1: self.local[indices,3]=1
                assert abs(self.local[indices,3].sum()-1)<.001
        else: self.local[:,3]=1
        assert np.isfinite(self.local).all()
        assert np.all((self.nodes[:,2]>=0)&(self.nodes[:,2]<(len(self.mix) or verts)))
        assert set(self.nodes[:,3]) <= {0,16,32,48}

    def write(self, data):
        # The rigid stream omits W. Four-lane streams can store zero for a singleton.
        coords = self.local.copy()
        if self.xyz_offset:
            coords = coords[:,:3]
        else:
            implicit = self.source_w==0
            coords[implicit,3] = self.source_w[implicit]
        encoded = coords.astype('<f4').tobytes()
        data[self.coord_offset:self.coord_offset+len(encoded)] = encoded
        if self.uv_offset is not None:
            encoded = self.nodes[:,:2].astype('<i2').tobytes()
            data[self.uv_offset:self.uv_offset+len(encoded)] = encoded
        for lane, offset in self.strip_fields.items():
            data[offset:offset+len(self.nodes)] = self.nodes[:,lane].astype('u1').tobytes()
        for joint, (tag, listing) in zip(self.palette,self.edits):
            for offset in (tag,listing): P('<I',data,offset,joint)

    def points(self, matrices):
        pts = np.einsum('nij,nj->ni', matrices[self.joints], self.local)[:,:3]
        if self.mix: pts = np.array([pts[idx].sum(axis=0) for idx in self.mix])
        return pts[self.nodes[:,2]]


class Model:
    def __init__(self, data):
        self.template = data
        h = 0x90
        assert R('<I',data,h)[0] == 3
        self.count,self.textures,self.bo,self.aux,ng = R('<HHIII',data,h+16)
        self.bones = data[h+self.bo:h+self.bo+64*self.count]
        records = list(struct.iter_unpack('<4hii12f', self.bones))
        self.parents = [r[2] for r in records]
        assert all(r[0]==i and -1<=r[2]<i for i,r in enumerate(records))
        self.srt = np.array([r[6:] for r in records]).reshape(-1,3,4)
        self.rest = world(self.srt,self.parents)
        self.groups = []
        self.shadow = []
        nxt, = R('<I',data,h+12)
        for section in ([h,h+nxt] if nxt else [h]):
            shadow = section != h
            for i in range(R('<I',data,section+28)[0]):
                _,mat,_,_,dma,pal,num,_ = R('<8I',data,section+32+32*i)
                cursor = section+pal+4
                limit = cursor+4*R('<I',data,cursor-4)[0]
                tag = section+dma
                packets = []
                while tag < section+dma+16*num:
                    first = tag
                    word,addr = R('<II',data,tag)
                    qwc = word & 65535
                    tag += 16
                    ids, edits = [], []
                    while R('<H',data,tag)[0]:
                        j, = R('<I',data,tag+4)
                        assert j == R('<i',data,cursor)[0] and j < self.count
                        ids.append(j)
                        edits.append((tag+4,cursor))
                        tag += 16
                        cursor += 4
                    tag += 16
                    if cursor < limit:
                        assert R('<i',data,cursor)[0] == -1
                        cursor += 4
                    packet = Packet(data,section+addr,section+addr+16*qwc,ids,shadow)
                    packet.edits = edits
                    packet.first = first
                    packets.append(packet)
                assert cursor==limit and tag==section+dma+16*num
                (self.shadow if shadow else self.groups).append((mat,packets))

    def write(self):
        data = bytearray(self.template)
        for i, parent in enumerate(self.parents):
            off = 144+self.bo+64*i
            P('<h',data,off+4,parent)
            P('<12f',data,off+16,*self.srt[i].ravel())
        for _,packets in self.groups+self.shadow:
            for packet in packets: packet.write(data)
        return bytes(data)

    def mesh(self,matrices):
        points,uv,faces,mats = [],[],[],[]
        for mat,packets in self.groups:
            for packet in packets:
                start=len(points)
                points.extend(packet.points(matrices))
                uv.extend(packet.nodes[:,:2]/4096)
                for i,flag in enumerate(packet.nodes[:,3]):
                    if flag==16: continue
                    assert i>=2
                    idx=start+i
                    faces.append((idx-2,idx-1,idx) if flag!=48 else (idx-1,idx-2,idx))
                    mats.append(mat)
        return tuple(map(np.array,(points,uv,faces,mats)))
