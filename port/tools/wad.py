# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
import struct, os
M=0xFFFFFFFF
def name_hash(name):
    h=0
    for c in name.encode():
        if 0x61<=c<=0x7a: c-=0x20
        h=((h+c)*0x401)&M; h^=h>>6
    h=(h*9)&M
    return (((h^(h>>11))*0x8001)&M)
def load_toc(path):
    d=open(path,'rb').read()
    return {h:(o,s) for h,o,s in struct.iter_unpack('<III',d[:len(d)//12*12])}
if __name__=='__main__':
    toc=load_toc('port/data/rom.toc')
    tests=['SS_Portraits.bin','SS_Hud.bin','ss_icons.png','SS_Email.bin','ammunation_hel16x16.bin','ROM.WAD','newtextures.txt']
    for t in tests: print(t, toc.get(name_hash(t)))
    # names of loose files in data dir
    import glob
    hit=[f for f in os.listdir('port/data') if name_hash(f) in toc]
    print(len(toc),'entries; loose files also in TOC:',len(hit),hit[:10])
