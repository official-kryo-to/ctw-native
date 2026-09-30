// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "datafile.h"
#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#define SEEK64 _fseeki64
#else
#define SEEK64 fseeko
#endif

namespace fs = std::filesystem;

namespace {
FILE* openReal(const std::string& path) {
#ifdef _WIN32
    return _wfopen(fs::u8path(path).wstring().c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}
}  // namespace

bool DataFile::open(const std::string& path) {
    close();
    if (!(fp_ = openReal(path))) return false;
    SEEK64(fp_, 0, SEEK_END);
#ifdef _WIN32
    size_ = (uint64_t)_ftelli64(fp_);
#else
    size_ = (uint64_t)ftello(fp_);
#endif
    base_ = 0;
    pos_ = 0;
    SEEK64(fp_, (int64_t)base_, SEEK_SET);
    return true;
}

void DataFile::close() {
    if (fp_) std::fclose(fp_);
    fp_ = nullptr;
    base_ = size_ = pos_ = 0;
}

bool DataFile::seek(uint64_t p) {
    if (!fp_ || p > size_) return false;
    if (SEEK64(fp_, (int64_t)(base_ + p), SEEK_SET) != 0) return false;
    pos_ = p;
    return true;
}

size_t DataFile::read(void* out, size_t bytes) {
    if (!fp_ || pos_ >= size_) return 0;
    bytes = (size_t)std::min<uint64_t>(bytes, size_ - pos_);
    size_t n = std::fread(out, 1, bytes, fp_);
    pos_ += n;
    return n;
}

bool Data_Exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec);
}

bool Data_ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    DataFile f;
    if (!f.open(path)) return false;
    out.resize((size_t)f.size());
    return f.read(out.data(), out.size()) == out.size();
}

std::vector<std::string> Data_List(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    const fs::path root = fs::u8path(dir);
    for (auto& e : fs::recursive_directory_iterator(root, ec))
        if (e.is_regular_file(ec)) files.push_back(fs::relative(e.path(), root, ec).generic_u8string());
    return files;
}
