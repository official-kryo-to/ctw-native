# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
import struct
from pak import load_index, page
class Pak:
    def __init__(s,path='port/data/game.pak'):
        s.f=open(path,'rb'); s.ix=load_index(path); s.t=s.ix['tab']
    def raw(s,i):
        sz=((s.t[i+1]-s.t[i])&0xffff)*4096; s.f.seek(page(s.ix,i)*4096); return s.f.read(sz)
def parse_model(b):
    assert b[:2]==b'MG'
    A=struct.unpack_from('<H',b,2)[0]; B=b[4]; C=b[5]; D=struct.unpack_from('<H',b,6)[0]
    size=D*16+B*32+C*12+A*192+16
    recs=[]
    ro=16+B*32+D*16   # NOTE: block 0 sits at 0x10, vertices at 0x30, the other B-1 node blocks follow the vertices
    for k in range(C):
        tex,n,f14,f15,f16,f17,mask=struct.unpack_from('<HHBBBBI',b,ro+12*k); recs.append(dict(tex=tex,n=n,f14=f14,f15=f15,f16=f16,f17=f17,mask=mask))
    return dict(A=A,B=B,C=C,D=D,size=size,recs=recs,ro=ro)
