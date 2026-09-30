// Reader for DXT.bin: 8192 u32 offsets indexed by resource id, followed by texture blobs.
// Blob = { u16 width, u16 height, u16 glFormat (0x83F0..0x83F3), u8, u8, u32 } + compressed data.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct DxtTexture {
    int id = 0, width = 0, height = 0;
    uint16_t glFormat = 0;            // 0x83F0/0x83F1 = DXT1, 0x83F2 = DXT3, 0x83F3 = DXT5
    std::vector<uint8_t> data;        // compressed blocks
};

class DxtBin {
public:
    bool open(const std::string& path);
    DxtBin() = default;
    DxtBin(const DxtBin&) = delete;
    DxtBin& operator=(const DxtBin&) = delete;
    ~DxtBin() { if (fp_) fclose(fp_); }
    const std::vector<int>& ids() const { return ids_; }
    bool get(int id, DxtTexture& out) const;
private:
    mutable FILE* fp_ = nullptr;
    size_t fileSize_ = 0;
    std::vector<uint32_t> offs_;
    std::vector<int> ids_;
};
