// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Read-only access to the game's data files (one place for the readers to open files, with 64-bit offsets and
// UTF-8 paths on Windows).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class DataFile {
public:
    DataFile() = default;
    DataFile(const DataFile&) = delete;
    DataFile& operator=(const DataFile&) = delete;
    ~DataFile() { close(); }
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return fp_ != nullptr; }
    uint64_t size() const { return size_; }
    bool seek(uint64_t position);
    uint64_t tell() const { return pos_; }
    size_t read(void* out, size_t bytes);         // bytes actually read
private:
    FILE* fp_ = nullptr;
    uint64_t base_ = 0, size_ = 0, pos_ = 0;
};

bool Data_Exists(const std::string& path);
bool Data_ReadAll(const std::string& path, std::vector<uint8_t>& out);
// Every file below dir (recursively), as paths relative to dir with '/' separators.
std::vector<std::string> Data_List(const std::string& dir);
