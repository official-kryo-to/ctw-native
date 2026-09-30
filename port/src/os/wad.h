// Reader for the game's ROM.TOC / ROM.WAD virtual filesystem.
// TOC = array of little-endian {u32 nameHash, u32 offset, u32 size}, sorted by hash.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class Wad {
public:
    Wad() = default;
    Wad(const Wad&) = delete;
    Wad& operator=(const Wad&) = delete;
    bool open(const std::string& dataDir);   // loads ROM.TOC, opens ROM.WAD
    bool exists(const char* name) const;
    bool size(const char* name, uint32_t* out) const;
    bool read(const char* name, std::vector<uint8_t>& out) const;
    size_t count() const { return toc_.size(); }
    static uint32_t hashName(const char* name);   // case-insensitive
    ~Wad() { if (fp_) fclose(fp_); }

private:
    struct Entry { uint32_t hash, offset, size; };
    const Entry* find(const char* name) const;
    std::vector<Entry> toc_;
    mutable FILE* fp_ = nullptr;
};
