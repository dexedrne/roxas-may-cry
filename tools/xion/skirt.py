"""Set the lower coat's bend heights around the back of the knee and the ankle."""
import numpy as np
def fit(v,j,w,knee,ankle,width,offset=0,panels=None):
    x,y,z=v.T
    left=np.clip(.5+(x+offset)/width,0,1)
    if panels is not None:
        l,r=panels.T
        left=l+(1-l-r)*left
    # The coat is several cm behind the leg. Its rear folds above the knee as the leg bends.
    shin=np.clip((knee-y)/18+.5,0,1)
    foot=np.clip((ankle-y)/7+.5,0,1)
    hip=np.clip((y-78)/18,0,1)
    rows=np.stack((hip,(1-hip)*left*(1-shin),(1-hip)*left*shin*(1-foot),(1-hip)*left*shin*foot,
                   (1-hip)*(1-left)*(1-shin),(1-hip)*(1-left)*shin*(1-foot),(1-hip)*(1-left)*shin*foot),axis=1)
    select=(y<99)&(abs(x)<26)
    pick=np.argsort(rows,axis=1)[:,-4:];wt=np.take_along_axis(rows,pick,axis=1);wt/=wt.sum(1)[:,None]
    return np.where(select[:,None],np.array([2,11,12,15,40,41,44])[pick],j),np.where(select[:,None],wt,w)

def release(v,j,w,panels):
    j,w=fit(v,j,w,80,16,16,panels=panels)
    x,y,z=v.T
    right=(w*np.isin(j,[40,41,44])).sum(1)
    front=right*np.clip((z-2)/4,0,1)*np.clip((y-20)/12,0,1)*np.clip((84-y)/12,0,1)*np.clip((14-abs(x))/6,0,1)
    back=.4*np.clip((-z-16)/7,0,1)*np.clip((49-y)/14,0,1)*np.clip((15-abs(x))/9,0,1)
    inner=np.clip((9-abs(x))/7,0,1)
    thigh=front*(.65+.12*(1-inner));foot=front*.14*(1-inner);other=front*.25*inner+back
    rows=w*(1-thigh-foot-other)[:,None]
    ids=np.column_stack((j,np.tile([40,44,15],(len(v),1))))
    vals=np.column_stack((rows,thigh,foot,other))
    # Combine repeated joint references before enforcing the native four-weight limit.
    for i in (4,5,6):
        for k in range(i):
            shared=ids[:,i]==ids[:,k]
            vals[shared,k]+=vals[shared,i];vals[shared,i]=0
    pick=np.argsort(vals,axis=1)[:,-4:]
    ww=np.take_along_axis(vals,pick,1)
    return np.take_along_axis(ids,pick,1),ww/ww.sum(1)[:,None]

def cuffs(v,j,w):
    # Let the flared sleeve follow forearm and wrist roll while the shoulder supports its outer edge.
    for wrist in (85,177):
        active=(abs(v[:,0])>23)&(v[:,1]>79)&((j==wrist)*w).any(1)
        if not active.any():continue
        ids=j[active];vals=w[active].copy()
        distal=(vals*(ids==wrist)).sum(1)
        vals[ids==wrist]*=.35
        ids=np.column_stack((ids,np.full(len(ids),wrist-8),np.full(len(ids),wrist-3)))
        vals=np.column_stack((vals,.2*distal,.45*distal))
        pick=np.argsort(vals,axis=1)[:,-4:];vals=np.take_along_axis(vals,pick,1)
        j[active]=np.take_along_axis(ids,pick,1);w[active]=vals/vals.sum(1)[:,None]
    return j,w

def source_panels(g,srcj,srcw):
    names=[g.d['nodes'][i]['name'] for i in g.d['skins'][0]['joints']]
    return np.column_stack([ (srcw*np.array([n.startswith(side+'_skt') for n in names])[srcj]).sum(1) for side in ('L','R')])
