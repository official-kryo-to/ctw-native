#include "dxtbin.h"
#include <cstdio>
#include <cstring>

// Only the 32 KB offset table is kept in memory; texture blobs are read on demand (the file is ~50 MB).
bool DxtBin::open(const std::string& path) {
    if (fp_) fclose(fp_);
    fp_ = fopen(path.c_str(), "rb");
    if (!fp_) return false;
    fseek(fp_, 0, SEEK_END);
    fileSize_ = (size_t)ftell(fp_);
    fseek(fp_, 0, SEEK_SET);
    offs_.resize(0x2000);
    if (fileSize_ < 0x8000 || fread(offs_.data(), 4, 0x2000, fp_) != 0x2000) return false;
    ids_.clear();
    for (int i = 0; i < 0x2000; ++i)
        if (offs_[i]) ids_.push_back(i);
    return true;
}

bool DxtBin::get(int id, DxtTexture& t) const {
    if (!fp_ || id < 0 || id >= 0x2000 || !offs_[id]) return false;
    size_t o = offs_[id];
    if (o + 12 > fileSize_) return false;
    uint8_t hdr[12];
    if (fseek(fp_, (long)o, SEEK_SET) != 0 || fread(hdr, 1, 12, fp_) != 12) return false;
    uint16_t w, h, fmt;
    memcpy(&w, hdr, 2);
    memcpy(&h, hdr + 2, 2);
    memcpy(&fmt, hdr + 4, 2);
    // Same size rule as the original GetDXTData: DXT5 (0x83F3) is 8 bpp, everything else 4 bpp.
    size_t sz = fmt == 0x83f3 ? (size_t)w * h : (size_t)w * h / 2;
    if (o + 12 + sz > fileSize_) return false;
    t.id = id; t.width = w; t.height = h; t.glFormat = fmt;
    t.data.resize(sz);
    return fread(t.data.data(), 1, sz, fp_) == sz;
}
