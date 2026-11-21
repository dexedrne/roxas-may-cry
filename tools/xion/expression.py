from mdlx import Archive, Model, np
from functools import lru_cache

@lru_cache()
def native_face(data, name):
    m=Model(Archive((data/'obj'/(name+'.mdlx')).read_bytes()).members[4])
    group=m.groups[1][1];points=[];weights=[]
    for packet in group:
        points.extend(packet.points(m.rest))
        for n in packet.nodes[:,2]:
            influences=packet.mix[n] if packet.mix else [n]
            row=np.zeros(m.count)
            for i in influences:row[packet.joints[i]]+=packet.local[i,3]
            weights.append(row)
    points,idx=np.unique(np.round(points,4),axis=0,return_index=True)
    return points,np.array(weights)[idx]

def expression(data,name,p,j,w,slot):
    # Eyelashes follow skin, whereas the two iris meshes move with the native eyeballs.
    points,weights=native_face(data,name);unique,idx=np.unique(p,axis=0,return_inverse=True)
    dist=((unique[:,None]-points)**2).sum(2)
    near=np.argpartition(dist,3,axis=1)[:,:4];a=1/np.maximum(np.take_along_axis(dist,near,axis=1),.01)**1.5
    blend=np.einsum('nk,nkj->nj',a/a.sum(1)[:,None],weights[near])
    joints=np.argsort(blend,axis=1)[:,-4:];wt=np.take_along_axis(blend,joints,axis=1)
    return joints[idx],(wt/wt.sum(1)[:,None])[idx]
