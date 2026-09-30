// .gxt string table: "DS_GXT", u16 count, then count * { u16 length, u16 chars[length] } (UTF-16LE).
// Strings may contain in-band tags: 0xFEFE = insert number, 0xFEFF = insert string (script arguments).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class GxtFile {
public:
    bool parse(const std::vector<uint8_t>& bytes);
    bool load(const std::string& path);
    size_t count() const { return strings_.size(); }
    const std::u16string& get(size_t i) const { return strings_[i]; }
private:
    std::vector<std::u16string> strings_;
};
