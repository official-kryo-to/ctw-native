#include "pak.h"
#include <cstring>

#ifdef _WIN32
#define SEEK64 _fseeki64
#else
#define SEEK64 fseeko
#endif

bool Pak::open(const std::string& path) {
    fp_ = fopen(path.c_str(), "rb");
    if (!fp_) return false;
    std::vector<uint8_t> head(0x1000);
    if (fread(head.data(), 1, head.size(), fp_) != head.size()) return false;
    uint32_t v[6];
    memcpy(v, head.data(), sizeof v);
    seg1_ = v[1]; seg2_ = v[2]; seg3_ = v[3]; count_ = v[4]; endPage_ = v[5];
    if (count_ == 0 || count_ > 0x10000) return false;
    // Header buffer size as computed by cResourceManager::Init; everything past the first page is at the file tail.
    size_t hdr = ((size_t)count_ * 2 + 0x1017) & ~(size_t)0xFFF;
    // The game asks for the whole rounded-up buffer but the file tail is shorter (a short read); only the bytes
    // that actually hold table entries are required.
    size_t need = 0x18 + (size_t)count_ * 2;
    if (hdr > 0x1000) {
        head.resize(hdr, 0);
        if (SEEK64(fp_, (int64_t)endPage_ * 4096, SEEK_SET) != 0) return false;
        size_t got = fread(head.data() + 0x1000, 1, hdr - 0x1000, fp_);
        if (0x1000 + got < need) return false;
    }
    if (need > head.size()) return false;
    table_.resize(count_ + 1);
    memcpy(table_.data(), head.data() + 0x18, (size_t)count_ * 2);
    table_[count_] = (uint16_t)endPage_;   // the game stores the end page as the final entry too
    return true;
}

uint64_t Pak::pageOf(uint32_t id) const {
    uint64_t seg = id < seg1_ ? 0 : id < seg2_ ? 1 : id < seg3_ ? 2 : 3;
    return table_[id] + 0x10000ull * seg;
}

bool Pak::sizeBytes(uint32_t id, uint32_t* out) const {
    if (id >= count_ || table_[id] == 0xFFFF || table_[id + 1] == 0xFFFF) return false;   // 0xFFFF = no resource
    *out = (uint32_t)((table_[id + 1] - table_[id]) & 0xFFFF) * 4096u;
    return true;
}

bool Pak::readPrefix(uint32_t id, size_t maxBytes, std::vector<uint8_t>& out) const {
    uint32_t sz;
    if (!sizeBytes(id, &sz) || sz == 0) return false;
    size_t n = sz < maxBytes ? sz : maxBytes;
    out.resize(n);
    if (SEEK64(fp_, (int64_t)(pageOf(id) * 4096u), SEEK_SET) != 0) return false;
    return fread(out.data(), 1, n, fp_) == n;
}

bool Pak::read(uint32_t id, std::vector<uint8_t>& out) const { return readPrefix(id, (size_t)-1, out); }
