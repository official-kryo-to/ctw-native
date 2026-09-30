# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
import struct, collections
def decode_color_block(b, alpha_mode):
    c0,c1,bits=struct.unpack('<HHI',b)
    def rgb(c): return ((c>>11&31)*255//31,(c>>5&63)*255//63,(c&31)*255//31)
    p0,p1=rgb(c0),rgb(c1)
    if c0>c1 or alpha_mode!='dxt1':
        pal=[p0+(255,),p1+(255,),tuple((2*a+b_)//3 for a,b_ in zip(p0,p1))+(255,),tuple((a+2*b_)//3 for a,b_ in zip(p0,p1))+(255,)]
    else:
        pal=[p0+(255,),p1+(255,),tuple((a+b_)//2 for a,b_ in zip(p0,p1))+(255,),(0,0,0,0)]
    return [pal[(bits>>(2*i))&3] for i in range(16)]
def decode_dxt(data,w,h,fmt):
    out=bytearray(w*h*4); pos=0
    for by in range(0,h,4):
        for bx in range(0,w,4):
            a=[255]*16
            if fmt=='dxt3':
                ab=data[pos:pos+8]; pos+=8
                a=[((ab[i//2]>>(4*(i&1)))&15)*17 for i in range(16)]
            elif fmt=='dxt5':
                a0,a1=data[pos],data[pos+1]; bits=int.from_bytes(data[pos+2:pos+8],'little'); pos+=8
                pal=[a0,a1]+([ (( (6-i)*a0+(1+i)*a1)//7) for i in range(6)] if a0>a1 else [((4-i)*a0+(1+i)*a1)//5 for i in range(4)]+[0,255])
                a=[pal[(bits>>(3*i))&7] for i in range(16)]
            px=decode_color_block(data[pos:pos+8],fmt); pos+=8
            for i in range(16):
                x,y=bx+(i&3),by+(i>>2)
                if x<w and y<h:
                    r,g,b,al=px[i]; o=(y*w+x)*4; out[o:o+4]=bytes((r,g,b,min(al,a[i]) if fmt=='dxt1' else a[i]))
    return bytes(out)
GLFMT={0x83f0:'dxt1',0x83f1:'dxt1',0x83f2:'dxt3',0x83f3:'dxt5'}
def entries(path):
    d=open(path,'rb').read()
    offs=list(struct.unpack('<8192I',d[:0x8000]))
    return d,offs
if __name__=='__main__':
    d,offs=entries('port/data/dxt.bin')
    ids=[i for i,o in enumerate(offs) if o]
    print(len(ids),'textures; formats:',collections.Counter(struct.unpack_from('<H',d,offs[i]+4)[0] for i in ids))
    srt=sorted(ids,key=lambda i:offs[i]); bad=0
    for a,b in zip(srt,srt[1:]+[None]):
        w,h,f=struct.unpack_from('<HHH',d,offs[a]); sz=w*h if f==0x83f3 else w*h//2
        nxt=offs[b] if b is not None else len(d)
        if 12+sz>nxt-offs[a]: bad+=1
    print('entries whose (12+size) exceeds gap to next:',bad)
    from PIL import Image
    import os; os.makedirs('analysis/exports/tex',exist_ok=True)
    for i in ids[:6]+ids[len(ids)//2:len(ids)//2+3]:
        w,h,f=struct.unpack_from('<HHH',d,offs[i]); sz=w*h if f==0x83f3 else w*h//2
        img=decode_dxt(d[offs[i]+12:offs[i]+12+sz],w,h,GLFMT[f]); Image.frombytes('RGBA',(w,h),img).save(f'analysis/exports/tex/{i}_{w}x{h}_{f:x}.png'); print(i,w,h,hex(f))
