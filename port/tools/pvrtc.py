"""PVRTC1 decoder (2bpp and 4bpp), numpy. Written to compare game.pak textures with their dxt.bin replacements.
Follows the public PVRTC1 description: 64-bit blocks (u32 modulation, u32 colours) in Morton order,
two low-res colour images (A, B) bilinearly upscaled from block centres, per-pixel modulation weight 0..8."""
import numpy as np

def _twiddle(bx, by, nbx, nby):
    mn = min(nbx, nby); t = 0; bit = 1; dst = 1; shift = 0
    while bit < mn:
        if by & bit: t |= dst
        if bx & bit: t |= dst << 1
        bit <<= 1; dst <<= 2; shift += 1
    t |= (max(bx, by) >> shift) << (2 * shift)
    return t

def _colours(cw):
    """cw: uint32 array of colour words -> (A, B) as float arrays [...,4] RGBA in 0..255"""
    cw = cw.astype(np.uint32)
    def ext(v, bits):   # expand n-bit to 8-bit
        v = v.astype(np.uint32); return ((v << (8 - bits)) | (v >> (2 * bits - 8 if bits > 4 else 0) if bits > 4 else (v << (8 - bits)) >> bits)).astype(np.float64) if False else (v * 255.0 / ((1 << bits) - 1))
    a = cw & 0xFFFF; b = cw >> 16
    A = np.zeros(cw.shape + (4,)); B = np.zeros(cw.shape + (4,))
    op = (a & 0x8000) != 0   # colour A: opaque RGB554 (blue has 4 bits incl. mode bit slot), else ARGB3443
    A[..., 0] = np.where(op, ext((a >> 10) & 31, 5), ext((a >> 8) & 15, 4))
    A[..., 1] = np.where(op, ext((a >> 5) & 31, 5), ext((a >> 4) & 15, 4))
    A[..., 2] = np.where(op, ext((a >> 1) & 15, 4), ext((a >> 1) & 7, 3))
    A[..., 3] = np.where(op, 255, ext((a >> 12) & 7, 3))
    op = (b & 0x8000) != 0   # colour B: opaque RGB555, else ARGB3444
    B[..., 0] = np.where(op, ext((b >> 10) & 31, 5), ext((b >> 8) & 15, 4))
    B[..., 1] = np.where(op, ext((b >> 5) & 31, 5), ext((b >> 4) & 15, 4))
    B[..., 2] = np.where(op, ext(b & 31, 5), ext(b & 15, 4))
    B[..., 3] = np.where(op, 255, ext((b >> 12) & 7, 3))
    return A, B

def decode(data, w, h, bpp):
    bw = 8 if bpp == 2 else 4; bh = 4
    nbx, nby = w // bw, h // bh
    words = np.frombuffer(data[: nbx * nby * 8], dtype='<u4').reshape(-1, 2)
    order = np.array([[_twiddle(x, y, nbx, nby) for x in range(nbx)] for y in range(nby)])
    mod = words[order, 0]; col = words[order, 1]          # [nby, nbx]
    A, B = _colours(col)
    # upscale colour images: block (bx,by) colour sits at the block centre; wrap at edges
    ys = (np.arange(h) - bh / 2) / bh; xs = (np.arange(w) - bw / 2) / bw
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int); fy = (ys - y0)[:, None, None]; fx = (xs - x0)[None, :, None]
    def up(C):
        c00 = C[y0 % nby][:, x0 % nbx]; c01 = C[y0 % nby][:, (x0 + 1) % nbx]
        c10 = C[(y0 + 1) % nby][:, x0 % nbx]; c11 = C[(y0 + 1) % nby][:, (x0 + 1) % nbx]
        return (c00 * (1 - fx) + c01 * fx) * (1 - fy) + (c10 * (1 - fx) + c11 * fx) * fy
    Au, Bu = up(A), up(B)
    # modulation weights (0..8)
    W = np.zeros((h, w))
    if bpp == 4:
        rep = np.array([0, 3, 5, 8])
        for py in range(4):
            for px in range(4):
                v = (mod >> (2 * (py * 4 + px))) & 3
                W[py::4, px::4] = rep[v]
    else:
        modeflag = (col & 1) != 0
        direct = np.zeros((nby, nbx, 4, 8)); vals = np.full((nby, nbx, 4, 8), -1.0); mmode = np.zeros((nby, nbx), int)
        for py in range(4):
            for px in range(8):
                direct[:, :, py, px] = ((mod >> (py * 8 + px)) & 1) * 8
        m = mod.astype(np.uint64).copy()
        sub = np.ones((nby, nbx), int)                       # 1 = average of 4, 2 = horizontal, 3 = vertical
        has = (m & 1) != 0
        sub = np.where(has & ((m & (1 << 20)) != 0), 3, np.where(has, 2, 1))
        m = np.where(has, np.where((m & (1 << 21)) != 0, m | (1 << 20), m & ~np.uint64(1 << 20)), m)
        m = np.where((m & 2) != 0, m | 1, m & ~np.uint64(1))
        rep = np.array([0, 3, 5, 8])
        k = 0
        for py in range(4):
            for px in range(8):
                if ((px ^ py) & 1) == 0:
                    vals[:, :, py, px] = rep[((m >> np.uint64(2 * k)) & np.uint64(3)).astype(int)]; k += 1
        full = vals.transpose(0, 2, 1, 3).reshape(h, w)            # stored checkerboard values, -1 elsewhere
        pad = np.pad(full, 1, mode='wrap')
        up_, dn, lf, rt = pad[:-2, 1:-1], pad[2:, 1:-1], pad[1:-1, :-2], pad[1:-1, 2:]
        subf = np.repeat(np.repeat(sub, 4, 0), 8, 1)
        interp = np.where(subf == 1, (up_ + dn + lf + rt + 2) // 4, np.where(subf == 2, (lf + rt + 1) // 2, (up_ + dn + 1) // 2))
        W2 = np.where(full >= 0, full, interp)
        Wd = direct.transpose(0, 2, 1, 3).reshape(h, w)
        mf = np.repeat(np.repeat(modeflag, 4, 0), 8, 1)
        W = np.where(mf, W2, Wd)
    Wn = (W / 8.0)[..., None]
    out = Au * (1 - Wn) + Bu * Wn
    return np.clip(out, 0, 255).astype(np.uint8)
