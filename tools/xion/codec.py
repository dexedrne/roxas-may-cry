import struct
import numpy as np

B=struct.pack
def pad(data,n):
    return data+b'\0'*(-len(data)%n)
def ints(values):return np.asarray(values,'<i4').tobytes()
def vif(cmd,dst,data,count):
    assert 0<count<256
    return B('<I',0x01000101)+B('<HBB',dst,count,cmd)+pad(data,4)
def encode(pos,uv,joints,w,inverse):
    # A mix row references the local coordinates of a vertex in each influencing joint.
    v,rev=np.unique(np.column_stack([pos,joints,w]),axis=0,return_inverse=True)
    pp=v[:,:3];jj=v[:,3:7].astype(int);ww=v[:,7:]
    ii,kk=np.where(ww>0); order=np.argsort(jj[ii,kk],kind='stable');ii,kk=ii[order],kk[order]
    pal,counts=np.unique(jj[ii,kk],return_counts=True)
    loc=np.einsum('nij,nj->ni',inverse[jj[ii,kk]],np.column_stack((pp[ii],np.ones(len(ii)))))*ww[ii,kk,None]
    ninf=(ww>0).sum(1);seq=np.argsort(ninf,kind='stable'); lookup=np.argsort(seq)[rev].astype('u1')
    groups=np.bincount(ninf,minlength=2)[1:]
    coord=4+len(pos);cnt=coord+len(ii);gc=cnt+(len(pal)+3)//4;ix=gc+(len(groups)+3)//4
    lanes=np.empty(jj.shape,int);lanes[ii,kk]=np.arange(len(ii))
    lists=b''.join(pad(ints([lanes[a,b] for a in seq[ninf[seq]==n] for b in np.flatnonzero(ww[a])]),16) for n in range(1,len(groups)+1))
    mat=ix+len(lists)//16
    if mat+4*len(pal)>224:return None
    h=[1,0,0,0,len(pos),4,cnt,mat,0,0,len(groups),gc,len(loc),coord,ix,len(pal)]
    flags=np.tile(np.array([16,16,32],'u1'),len(pos)//3)
    data=vif(0x6c,0x8000,ints(h),4)+vif(0x65,0x8004,np.rint(uv*4096).astype('<i2').tobytes(),len(pos))
    for mask,column in [(0xcfcfcfcf,lookup),(0x3f3f3f3f,flags)]:
        data+=B('<II',0x20000000,mask)+vif(0x72,0xc004,column.tobytes(),len(pos))
    metadata=pad(ints(counts),16)+pad(ints(groups),16)+lists
    data+=vif(0x6c,0x8000+coord,loc.astype('<f4').tobytes(),len(loc))+vif(0x6c,0x8000+cnt,metadata,len(metadata)//16)
    return pad(data,16),pal,mat
def mapping(name):
    mp=np.arange(229)
    if name.startswith(('P_EX110','H_EX510')):
        for j,k in [(24,16),(53,31),(70,50),(129,102),(131,104),(133,106),(135,108),(2,2),(11,7),(12,8),(15,11),(18,14),(40,22),(41,23),(44,26),(47,29),(65,41),(66,44),(67,45),(137,110),(140,113)]:mp[j]=k
        for j,k in [(221,211),(223,213),(225,215),(227,217)]:mp[j]=k+(70 if name.startswith("H_") else 0)
        for j in [75,78,79,85,88,91,92,94,95,96,98,99,100,102,103,104,106,107,108]:
            mp[j]=j-23;mp[j+92]=j-23+109+(70 if name.startswith('H_') else 0)
    elif name=='H_EX500':mp[141:]+=56
    return mp
