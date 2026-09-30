#include "wad.h"
#include <algorithm>

#ifdef _WIN32
#define SEEK64 _fseeki64
#else
#define SEEK64 fseeko
#endif

uint32_t Wad::hashName(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        h = (h + c) * 0x401u;
        h ^= h >> 6;
    }
    h *= 9u;
    return (h ^ (h >> 11)) * 0x8001u;
}

bool Wad::open(const std::string& dir) {
    std::string tocPath = dir + "/rom.toc", wadPath = dir + "/rom.wad";
    FILE* t = fopen(tocPath.c_str(), "rb");
    if (!t) return false;
    fseek(t, 0, SEEK_END);
    long n = ftell(t) / (long)sizeof(Entry);
    fseek(t, 0, SEEK_SET);
    toc_.resize((size_t)n);
    size_t got = fread(toc_.data(), sizeof(Entry), (size_t)n, t);
    fclose(t);
    if (got != (size_t)n) return false;
    fp_ = fopen(wadPath.c_str(), "rb");
    return fp_ != nullptr;
}

const Wad::Entry* Wad::find(const char* name) const {
    uint32_t h = hashName(name);
    auto it = std::lower_bound(toc_.begin(), toc_.end(), h,
                               [](const Entry& e, uint32_t v) { return e.hash < v; });
    return (it != toc_.end() && it->hash == h) ? &*it : nullptr;
}

bool Wad::exists(const char* name) const { return find(name) != nullptr; }

bool Wad::size(const char* name, uint32_t* out) const {
    const Entry* e = find(name);
    if (!e) return false;
    *out = e->size;
    return true;
}

bool Wad::read(const char* name, std::vector<uint8_t>& out) const {
    const Entry* e = find(name);
    if (!e || !fp_) return false;
    out.resize(e->size);
    if (SEEK64(fp_, (int64_t)e->offset, SEEK_SET) != 0) return false;
    return fread(out.data(), 1, e->size, fp_) == e->size;
}
