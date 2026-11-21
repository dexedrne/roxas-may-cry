import struct
import numpy as np
from functools import lru_cache
from PIL import Image
from codec import encode, ints, pad, mapping
from expression import expression
from glb import Glb
from mdlx import Archive, R, P
from swizzle import decode8, encode32

@lru_cache()
def permutation(w,h):
    a=np.arange(w*h,dtype='<u4').view('u1').reshape(w*h,4)
    cols=[decode8(encode32(col.tobytes(),w//128,h//64),w//128,h//64) for col in a.T]
    return np.stack([np.frombuffer(c,'u1') for c in cols],1).copy().view('<u4').ravel()
@lru_cache
def sheets(textures):
    def image(name):
        paths = sorted(textures.rglob(name + '.png'))
        if not paths:
            raise ValueError(f'missing KH3 export texture: {name}.png')
        with Image.open(paths[0]) as im:
            return im.convert('RGBA')
    hair=image('hairlinemask').getchannel('R')
    hair=Image.fromarray(np.uint8([19,22,30]+np.asarray(hair)[:,:,None]/255*[28,30,37])).convert('RGBA')
    strands=hair.copy()
    strands.putalpha(image('hairlineendmask2').getchannel('A'))
    mask=np.asarray(image('EyeG_ColorMask'))[:,:,:3].astype(float)/255
    iris=Image.fromarray(np.uint8([10,18,28]+mask*[48,109,156])).convert('RGBA')
    return [hair,strands,image('n_ex033_F_0d00').convert('RGB').convert('RGBA'),Image.new('RGBA',(16,16),(255,255,255,6)),
            image('n_ex033_E_0d00'),image('Mouth_M_0d00').convert('RGB').convert('RGBA'),image('EyeWhiteBaseColor'),
            iris,image('e_ex304_0d02'),image('n_ex033_0d03'),
            image('n_ex033_0d00'),image('n_ex033_0d01')]

def textures_native(data,textures):
    t=bytearray(Archive((data/'obj/H_ZZ010.mdlx').read_bytes()).members[7])
    _,colors,count,_,_,tx,gs,_,pal=R('<9I',t,0)
    assert count==12
    clut_base=R('<Q',t,tx+32)[0]>>32&16383
    for i,img in enumerate(sheets(textures)):
        v=R('<Q',t,gs+i*160+112)[0]
        w,h=1<<(v>>26&15),1<<(v>>30&15)
        cb=(v>>37&16383)-clut_base
        off=R('<I',t,tx+(i+1)*144+116)[0]
        small=img.resize((w,h),Image.Resampling.LANCZOS)
        if i==4:
            # Eyelash colour is black. Spend all 256 palette entries on coverage.
            q=small.getchannel('A')
            rgba=np.zeros((256,4),'u1');rgba[:,3]=np.arange(256)
        else:
            q=small.quantize(256,Image.Quantize.FASTOCTREE)
            rgba=np.array(q.getpalette('RGBA'),'u1').reshape(-1,4)
        rgba[:,3]=(rgba[:,3].astype(int)+1)//2
        j=np.arange(len(rgba)); loc=(j&7)+(j&8)*8+(j&16)//2+(j&~31)*4+(cb&7)*4+(cb&8)*128+(cb&16)*2+(cb&~31)*64
        np.frombuffer(t,'u1',offset=pal,count=colors*4).reshape(-1,4)[loc]=rgba
        np.frombuffer(t,'u1',offset=off,count=w*h)[permutation(w,h)]=np.asarray(q).ravel()
    return bytes(t)

def jointmap(g,sleeves=False):
    nodes=g.d['nodes'];skin=g.d['skins'][0]['joints']
    ids={'koshi':2,'center':2,'hara1':65,'hara2':66,'mune':67,'kubi':137,'atama':140}
    for side,arm,leg in [('L',0,0),('R',92,29)]:
        for key,bone in zip(['momo','sune','ashi1','ashi2','kata','ude1','ude2','te'],[11+leg,12+leg,15+leg,18+leg,75+arm,78+arm,79+arm,85+arm]):
            ids[side+'_'+key]=bone
        if sleeves:
            for panel in 'ABCDEFGH':
                for segment,bone in [(1,79),(2,85),(3,85)]:
                    ids[f'{side}_sode{panel}{segment}']=arm+bone
            ids[side+'_hiji_spo']=arm+79
        for digit,base in zip('ABCDE',[88,94,98,102,106]):
            for n,offset in enumerate(([0,3,4] if digit=='A' else [0,1,2]),1):
                ids[f'{side}_yubi{digit}{n}']=arm+base+offset
    parents={c:p for p,node in enumerate(nodes) for c in node.get('children',[])}
    out=[]
    for node in skin:
        while nodes[node]['name'] not in ids and node in parents:node=parents[node]
        out.append(ids.get(nodes[node]['name'],2))
    return np.array(out)

def parts(stage, hooded=False, cloth_fit="release"):
    s=Glb(stage/'static.glb'); g=Glb(stage/'skin.glb'); jm=jointmap(g,cloth_fit=='release'); result=[]
    for k,(p,q) in enumerate(zip(s.d['meshes'][0]['primitives'],g.d['meshes'][0]['primitives'])):
        si=s.get(p['indices']).ravel();gi=g.get(q['indices']).ravel()
        idx=si
        a=p['attributes'];b=q['attributes'];pos=s.get(a['POSITION'])
        assert np.max(abs(pos[si]-g.get(b['POSITION'])[gi]))<.00001
        # A static vertex can be shared at a UV seam. Check every source corner before assigning weights.
        original=np.zeros(len(pos),int);original[si]=gi
        weights=g.get(b['WEIGHTS_0']);joints=jm[g.get(b['JOINTS_0'])]
        assert np.array_equal(joints[gi],joints[original[si]]) and np.array_equal(weights[gi],weights[original[si]])
        j=joints[original[idx]].copy();w=weights[original[idx]].astype(float);v=pos[idx].astype(float)*100
        if k in (8,9,10) and cloth_fit is not None:
            from skirt import fit,release,source_panels
            panels=source_panels(g,g.get(b['JOINTS_0'])[original[idx]],weights[original[idx]])
            j,w=release(v,j,w,panels) if cloth_fit=="release" else fit(v,j,w,*cloth_fit,panels=panels)
        elif k in (8,9,10):
            x,y,z=v.T
            cloth=(y<100)&(abs(x)<26)
            left=np.clip(.5+x/16,0,1);hip=np.clip((y-76)/22,0,1);shin=np.clip((66-y)/20,0,1)
            allw=np.column_stack([hip,left*(1-shin)*(1-hip),(1-left)*(1-shin)*(1-hip),left*shin*(1-hip),(1-left)*shin*(1-hip)])
            pick=np.argsort(allw,axis=1)[:,-4:]
            j[cloth]=np.array([2,11,40,12,41])[pick[cloth]]; w[cloth]=np.take_along_axis(allw,pick,axis=1)[cloth]
        if k==10 and cloth_fit=='release':
            from skirt import cuffs
            j,w=cuffs(v,j,w)
        w/=w.sum(1)[:,None]
        result.append((k,v,s.get(a['TEXCOORD_0'])[idx],j,w))
    if hooded:
        from accessory import hood
        result.append(hood())
    return result

def serialize(data,old,name,groups):
    h=144
    raw=bytearray(old.template[:h+32]);raw.extend(bytes(32*len(groups)));aux=len(raw)-h
    raw.extend(old.template[h+old.aux:h+old.bo]);bo=len(raw)-h;raw.extend(old.bones)
    P('<I',raw,h+12,0);P('<HHIII',raw,h+16,old.count,12,bo,aux,len(groups))
    inv=np.linalg.inv(old.rest);mp=mapping(name);peak=palette=packets=0
    for side in (0,92):
        mp[77+side]=old.parents[mp[78+side]]
        wrist=mp[85+side]
        for _ in range(3):wrist=old.parents[wrist]
        mp[82+side]=wrist
    for gi,(slot,p,u,j,w) in enumerate(groups):
        j=mp[j]
        if name.startswith('H_') and 2<=slot<=7:
            j,w=expression(data,name,p,j,w,6)
            if slot==7:
                eyes=sorted(([182,187] if name=='H_EX500' else [162,164]),key=lambda b:old.rest[b,0,3])
                j[:,0]=np.where(p[:,0]<0,*eyes);w[:]=[1,0,0,0]
        tags=[];listing=[];s=0
        while s<len(p):
            n=min(60,len(p)-s)
            while True:
                sl=slice(s,s+n);packet=encode(p[sl],u[sl],j[sl],w[sl],inv)
                if packet:break
                n=n//6*3
                assert n>=3
            blob,pal,mat=packet;off=len(raw)-h;raw.extend(blob)
            tags.append(struct.pack('<4I',0x30000000+len(blob)//16,off,0,0))
            for k,bone in enumerate(pal):tags.append(struct.pack('<4I',0x30000004,bone,0x01000101,0x6c048000+mat+4*k))
            tags.append(struct.pack('<4I',0x10000000,0,0x17000000,0))
            listing.extend(pal);listing.append(-1);s+=n
            peak=max(peak,mat+4*len(pal));palette=max(palette,len(pal));packets+=1
        off=len(raw)-h;raw.extend(b''.join(tags));lp=len(raw)-h;raw.extend(pad(ints([len(listing)-1]+listing[:-1]),16))
        P('<8I',raw,h+32+gi*32,0,slot,0,0,off,lp,len(tags),0)
    bounds=np.frombuffer(raw,'<f4',count=8,offset=h+aux).reshape(2,4)
    points=np.concatenate([row[1] for row in groups])
    bounds[:,:3]=[points.min(0),points.max(0)]
    return bytes(raw),(packets,palette,peak)
