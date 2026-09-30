#include "spriteset.h"
#include <cctype>
#include <cstring>

bool SpriteSet::parse(const std::vector<uint8_t>& bin) {
    defs_.clear();
    if (bin.size() < 4) return false;
    uint32_t n;
    memcpy(&n, bin.data(), 4);
    if (bin.size() != 4 + (size_t)n * 16) return false;   // exact size is part of the format
    defs_.resize(n);
    memcpy(defs_.data(), bin.data() + 4, (size_t)n * 16);
    return true;
}

int SpriteSet::pngScaleFor(const std::string& fileName) {
    std::string n = fileName;
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    if (n.find("ss_") != std::string::npos) return 2;
    if (n.compare(0, 3, "jp/") == 0 || n.find("gtactwjapanese") != std::string::npos ||
        n.find("iphone_hel_20x20_lrg") != std::string::npos)
        return 1;
    return 2;
}
