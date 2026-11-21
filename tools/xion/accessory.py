"""A separate coat-fabric hood with a recessed, dark front lining. Units are centimetres."""
import numpy as np

def hood():
    # Height, width radius, back and front hems, opening half-angle.
    sections=[(115,9,-9,9,0),(119,13,-12,11,0),(125,16,-16,11,0),
              (130,15,-17,14,15),(136,15,-17,16,34),(143,15,-17,17,42),
              (149,14.5,-16,17,38),(154,12,-14,15,24),(156.6,8,-11,10,0),
              (158,3,-7,2,0)]
    rings=[];faces=[];tex=[]
    # Distinct rim/lining vertices form a lip that projects in front of the face.
    for y,r,back,front,opening in sections:
        angle=max(2,opening)
        exterior=np.linspace(angle,360-angle,49)
        interior=np.linspace(-angle,angle,17)[1:-1]
        theta=np.deg2rad(np.r_[exterior,interior])
        x=r*np.sin(theta)
        z=(front+back)/2+(front-back)/2*np.cos(theta)
        if opening:
            z[49:]=np.interp(abs(interior),[0,angle*.86,angle],[front-4,front-4.3,z[0]])
        rings.append(np.column_stack((x,np.full(len(x),y),z)))
    def triangle(points,uv):
        faces.extend(points);tex.extend(uv)
    n=len(rings[0])
    for i,(lo,hi) in enumerate(zip(rings,rings[1:])):
        for a in range(n):
            b=(a+1)%n
            if a<48:
                u,v=.375+.14*a/48,.13+.26*i/(len(rings)-1)
                du,dv=.14/48,.26/(len(rings)-1)
            else:
                u,v=.02,.02
                du=dv=.003
            triangle([lo[a],lo[b],hi[a]],[(u,v),(u+du,v),(u,v+dv)])
            triangle([hi[a],lo[b],hi[b]],[(u,v+dv),(u+du,v),(u+du,v+dv)])
    for ring,top in [(rings[0],False),(rings[-1],True)]:
        for a in range(n):
            pair=[ring[a],ring[(a+1)%n]]
            triangle([ring.mean(0),*(pair if top else pair[::-1])],[(.4,.22),(.4,.24),(.43,.24)])
    points=np.array(faces)
    head=np.clip((points[:,1]-117)/14,0,1)
    joints=np.tile([140,137,0,0],(len(head),1))
    return 10,points,np.array(tex),joints,np.column_stack((head,1-head,np.zeros((len(head),2))))
