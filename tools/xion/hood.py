from types import SimpleNamespace
from looks import serialize, np, R, P, struct
from accessory import hood
from mdlx import world
def header(data):
    n,_,bo,aux,ng=R('<HHIII',data,160)
    bones=data[144+bo:144+bo+64*n]
    records=list(struct.iter_unpack('<4hii12f',bones))
    rest=world(np.array([r[6:] for r in records]).reshape(-1,3,4),[r[2] for r in records])
    return SimpleNamespace(template=data,count=n,bo=bo,aux=aux,bones=bones,rest=rest,parents=[r[2] for r in records]),ng

def relocate(data,start,groups,delta):
    for i in range(groups):
        off=start+32*i
        dma,pal,num=R('<III',data,off+16)
        P('<II',data,off+16,dma+delta,pal+delta)
        cursor=144+dma+delta;end=cursor+16*num
        while cursor<end:
            tag,addr=R('<II',data,cursor)
            assert tag>>28==3
            if start==176 and i<8:
                # ADC on every strip vertex suppresses the enclosed head on every animation.
                pos=144+addr+delta
                marker=struct.pack('<III',0x20000000,0x3f3f3f3f,0x01000101)
                flag=data.index(marker,pos,pos+(tag&65535)*16)+12
                imm,n,cmd=R('<HBB',data,flag)
                assert (imm,cmd)==(0xc004,0x72) and data[flag+4:flag+4+n]==bytes([16,16,32])*(n//3)
                data[flag+4:flag+4+n]=bytes([16])*n
            P('<I',data,cursor+4,addr+delta)
            cursor+=16
            while R('<I',data,cursor)[0]==0x30000004:cursor+=16
            assert R('<I',data,cursor)[0]==0x10000000
            cursor+=16
        assert cursor==end
def hooded(data, name, body):
    orig,ng=header(body)
    single,_=serialize(data,orig,name,[hood()]);shell,_=header(single)
    cut=144+shell.bo+64*shell.count
    d=bytearray(body);start=176+ng*32
    d[start:start]=single[176:208]
    P('<III',d,164,orig.bo+32,orig.aux+32,ng+1)
    relocate(d,176,ng,32)
    delta=len(d)-cut;d.extend(single[cut:])
    relocate(d,start,1,delta)
    bounds=np.frombuffer(d,'<f4',8,144+orig.aux+32).reshape(2,4)
    pts=hood()[1]
    bounds[0,:3]=np.minimum(bounds[0,:3],pts.min(0));bounds[1,:3]=np.maximum(bounds[1,:3],pts.max(0))
    return bytes(d)
