import struct,json,numpy as np
R=struct.unpack_from
class Glb:
    def __init__(self,path):
        data=path.read_bytes()
        size=R('<I',data,12)[0]
        self.d=json.loads(data[20:20+size])
        self.bin=data[28+size:]
    def get(self,i):
        a=self.d['accessors'][i]
        v=self.d['bufferViews'][a['bufferView']]
        typ={5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
        n={'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16,'SCALAR':1}[a['type']]
        return np.ndarray((a['count'],n),typ,self.bin,v.get('byteOffset',0)+a.get('byteOffset',0),
                          (v.get('byteStride',np.dtype(typ).itemsize*n),np.dtype(typ).itemsize))
