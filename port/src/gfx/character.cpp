// SPDX-License-Identifier: GPL-3.0-only
#include "character.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glad/gl.h>
#include <stdexcept>

namespace {
using Matrix = CharacterModel::Matrix;
Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix c{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                c[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    return c;
}
struct Bytes {
    const std::vector<uint8_t>& data;
    size_t begin, end;
    void check(size_t at, size_t n) const {
        if (at < begin || at > end || n > end - at)
            throw std::runtime_error("truncated character resource");
    }
    uint32_t u32(size_t at) const {
        check(at, 4);
        uint32_t v;
        memcpy(&v, &data[at], 4);
        return v;
    }
    uint16_t u16(size_t at) const {
        check(at, 2);
        uint16_t v;
        memcpy(&v, &data[at], 2);
        return v;
    }
    float real(size_t at) const {
        uint32_t v = u32(at);
        float f;
        memcpy(&f, &v, 4);
        if (!std::isfinite(f))
            throw std::runtime_error("non-finite character value");
        return f;
    }
    std::string text(size_t at, size_t n) const {
        check(at, n);
        size_t len = 0;
        while (len < n && data[at + len])
            ++len;
        return std::string((const char*)&data[at], len);
    }
};
struct Chunk {
    uint32_t type;
    Bytes bytes;
    size_t next;
};
Chunk chunk(const Bytes& b, size_t at) {
    b.check(at, 12);
    size_t n = b.u32(at + 4);
    b.check(at + 12, n);
    return {b.u32(at), {b.data, at + 12, at + 12 + n}, at + 12 + n};
}
Chunk ifp(const Bytes& b, size_t at) {
    b.check(at, 8);
    size_t n = b.u32(at + 4);
    b.check(at + 8, (n + 3) & ~size_t(3));
    return {b.u32(at), {b.data, at + 8, at + 8 + n}, at + 8 + ((n + 3) & ~size_t(3))};
}
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::vector<uint8_t> read(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    auto n = f.tellg();
    if (n < 0 || n > (32 << 20))
        return {};
    std::vector<uint8_t> b((size_t)n);
    f.seekg(0);
    f.read((char*)b.data(), n);
    return f ? b : std::vector<uint8_t>{};
}
void parseFrames(CharacterModel& m, const Bytes& b) {
    auto s = chunk(b, b.begin);
    require(s.type == 1, "missing frame structure");
    size_t n = s.bytes.u32(s.bytes.begin);
    require(n > 0 && n <= 128, "invalid skeleton size");
    s.bytes.check(s.bytes.begin + 4, n * 56);
    size_t at = s.next;
    for (size_t i = 0; i < n; ++i) {
        CharacterModel::Frame f;
        f.local = identity();
        size_t p = s.bytes.begin + 4 + i * 56;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 3; ++row)
                f.local[col * 4 + row] = s.bytes.real(p + (col * 3 + row) * 4);
        f.parent = (int)s.bytes.u32(p + 48);
        require(f.parent >= -1 && f.parent < (int)i, "invalid skeleton parent");
        auto ex = chunk(b, at);
        require(ex.type == 3, "missing frame extensions");
        at = ex.next;
        for (size_t q = ex.bytes.begin; q < ex.bytes.end;) {
            auto c = chunk(ex.bytes, q);
            q = c.next;
            if (c.type != 0x11e)
                continue;
            f.id = (int)c.bytes.u32(c.bytes.begin + 4);
            size_t bones = c.bytes.u32(c.bytes.begin + 8);
            if (bones) {
                require(bones <= 128 && m.boneFrames.empty(), "invalid bone hierarchy");
                c.bytes.check(c.bytes.begin + 20, bones * 12);
                m.boneFrames.resize(bones, -1);
                for (size_t j = 0; j < bones; ++j) {
                    int id = (int)c.bytes.u32(c.bytes.begin + 20 + j * 12),
                        idx = (int)c.bytes.u32(c.bytes.begin + 24 + j * 12);
                    require(idx >= 0 && idx < (int)bones && m.boneFrames[idx] == -1, "invalid bone index");
                    m.boneFrames[idx] = id;
                }
            }
        }
        m.frames.push_back(f);
    }
    for (int& bone : m.boneFrames) {
        int idx = -1;
        for (size_t i = 0; i < m.frames.size(); ++i)
            if (m.frames[i].id == bone) {
                idx = (int)i;
                break;
            }
        require(idx >= 0, "bone missing from skeleton");
        bone = idx;
    }
}
void parseMaterials(CharacterModel& m, const Bytes& b) {
    auto s = chunk(b, b.begin);
    require(s.type == 1, "missing material structure");
    size_t n = s.bytes.u32(s.bytes.begin);
    require(n <= 128, "too many materials");
    size_t at = s.next;
    for (size_t i = 0; i < n; ++i) {
        auto mat = chunk(b, at);
        require(mat.type == 7, "invalid material");
        at = mat.next;
        CharacterModel::Material out;
        for (size_t p = mat.bytes.begin; p < mat.bytes.end;) {
            auto c = chunk(mat.bytes, p);
            p = c.next;
            if (c.type != 6)
                continue;
            for (size_t q = c.bytes.begin; q < c.bytes.end;) {
                auto t = chunk(c.bytes, q);
                q = t.next;
                if (t.type == 2 && out.texture.empty())
                    out.texture = t.bytes.text(t.bytes.begin, t.bytes.end - t.bytes.begin);
            }
        }
        for (char& c : out.texture)
            c = (char)std::tolower((unsigned char)c);
        require(!out.texture.empty() && out.texture.find_first_not_of(
                                            "abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos,
                "unsafe or missing material texture");
        m.materials.push_back(out);
    }
}
void parseGeometry(CharacterModel& m, const Bytes& b) {
    const size_t base = b.begin;
    uint32_t packed = b.u32(base + 16), flags = b.u32(base + 20);
    size_t n = packed >> 20, size = packed & 0xfffff;
    require(n > 0 && n <= 128 && size >= 64 + n * 48, "invalid mobile geometry");
    b.check(base, size);
    require((flags & ~31u) == 0 && (flags & 0x15) == 0x15, "unsupported character vertex layout");
    float scale[3], origin[3];
    for (int j = 0; j < 3; ++j) {
        scale[j] = b.real(base + 40 + j * 4);
        origin[j] = b.real(base + 52 + j * 4);
    }
    size_t data = base + 64 + n * 48;
    for (size_t mesh = 0; mesh < n; ++mesh) {
        size_t d = base + 64 + mesh * 48;
        CharacterModel::Mesh out;
        out.material = (int16_t)b.u16(d + 34);
        float uvscale[2] = {b.real(d + 16), b.real(d + 20)};
        size_t p = data + b.u32(d + 28);
        b.check(p, 16);
        size_t end = p + ((b.u32(p) & 0xffff) + 1) * 16;
        b.check(p, end - p);
        p += 16;
        while (p < end) {
            require(b.u32(p) == 0x6c018000, "invalid character strip header");
            size_t vertices = b.u32(p + 16) & 0x7fff;
            require(vertices >= 3 && vertices <= 256, "invalid character strip size");
            p += 20;
            size_t pos = 0, uv = 0, norm = 0, skin = 0;
            // VIF streams: positions (STMASK/STROW + V3-16), UVs (V2-8), optional
            // colour, normal, skin.
            if (flags & 1) {
                require(b.u32(p) == 0x20000000 && b.u32(p + 8) == 0x30000000, "invalid position stream");
                p += 28;
                require((b.u32(p) & 0xff004000) == 0x79000000, "invalid position unpack");
                pos = p + 4;
                b.check(pos, vertices * 6);
                p = pos + ((vertices * 6 + 3) & ~size_t(3));
            }
            if (flags & 4) {
                require(b.u32(p) == 0x20000000 && b.u32(p + 8) == 0x30000000, "invalid UV stream");
                p += 28;
                require((b.u32(p) & 0xff004000) == 0x76004000, "invalid UV unpack");
                uv = p + 4;
                b.check(uv, vertices * 2);
                p = uv + ((vertices * 2 + 3) & ~size_t(3));
            }
            if (flags & 8) {
                require((b.u32(p) & 0xff004000) == 0x6f000000, "invalid colour unpack");
                p += 4 + ((vertices * 2 + 3) & ~size_t(3));
            }
            if (flags & 2) {
                require((b.u32(p) & 0xff004000) == 0x6a000000, "invalid normal unpack");
                norm = p + 4;
                b.check(norm, vertices * 3);
                p = norm + ((vertices * 3 + 3) & ~size_t(3));
            }
            if (flags & 16) {
                require((b.u32(p) & 0xff004000) == 0x6c000000, "invalid skin unpack");
                skin = p + 4;
                b.check(skin, vertices * 16);
                p = skin + vertices * 16;
            }
            require(b.u32(p) == 0x14000006, "invalid character draw command");
            p += 4;
            while (p < end && b.u32(p) == 0)
                p += 4;
            std::vector<CharacterModel::Vertex> strip(vertices);
            for (size_t i = 0; i < vertices; ++i) {
                auto& v = strip[i];
                float sum = 0;
                for (int j = 0; j < 3; ++j) {
                    v.pos[j] = (int16_t)b.u16(pos + i * 6 + j * 2) / 32767.f * scale[j] + origin[j];
                    v.normal[j] = norm ? (int8_t)b.data[norm + i * 3 + j] / 127.f : 0;
                }
                for (int j = 0; j < 2; ++j)
                    v.uv[j] = b.data[uv + i * 2 + j] / 255.f * uvscale[j] * 2.f;
                for (int j = 0; j < 4; ++j) {
                    uint32_t word = b.u32(skin + i * 16 + j * 4), weight = word & 0xffffff00;
                    memcpy(&v.weights[j], &weight, 4);
                    v.bones[j] = (word >> 2) & 63;
                    require(std::isfinite(v.weights[j]) && v.weights[j] >= 0 && v.weights[j] <= 1.01f,
                            "invalid bone weight");
                    sum += v.weights[j];
                }
                require(sum > 0.99f && sum < 1.01f, "invalid skin weight sum");
                for (float& w : v.weights)
                    w /= sum;
            }
            for (size_t i = 2; i < vertices; ++i) {
                out.vertices.push_back(strip[i % 2 ? i - 1 : i - 2]);
                out.vertices.push_back(strip[i % 2 ? i - 2 : i - 1]);
                out.vertices.push_back(strip[i]);
            }
        }
        require(out.vertices.size() <= 65536, "character mesh too large");
        m.meshes.push_back(std::move(out));
    }
    size_t at = base + size;
    auto ml = chunk(b, at);
    require(ml.type == 8, "missing materials");
    parseMaterials(m, ml.bytes);
    at = ml.next;
    auto ex = chunk(b, at);
    require(ex.type == 3, "missing skin extension");
    for (size_t p = ex.bytes.begin; p < ex.bytes.end;) {
        auto c = chunk(ex.bytes, p);
        p = c.next;
        if (c.type != 0x116)
            continue;
        uint32_t info = c.bytes.u32(c.bytes.begin);
        size_t bones = info & 255;
        require(bones == m.boneFrames.size() && (info >> 16 & 255) == 0, "unsupported skin palette");
        c.bytes.check(c.bytes.begin + 4, bones * 64);
        for (size_t i = 0; i < bones; ++i) {
            Matrix mat = identity();
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 3; ++row)
                    mat[col * 4 + row] = c.bytes.real(c.bytes.begin + 4 + i * 64 + (col * 4 + row) * 4);
            m.inverse.push_back(mat);
        }
    }
}
void parseAnimations(CharacterModel& m, const Bytes& b) {
    size_t bank = 0;
    while (bank < b.end) {
        auto root = ifp(b, bank);
        bank = root.next;
        require(root.type == 0x4b504e41, "unsupported animation bank");
        auto info = ifp(root.bytes, root.bytes.begin);
        require(info.type == 0x4f464e49, "missing animation info");
        size_t n = info.bytes.u32(info.bytes.begin);
        require(n <= 1024 && m.animations.size() + n <= 1024, "too many animations");
        size_t at = info.next;
        for (size_t i = 0; i < n; ++i) {
            auto name = ifp(root.bytes, at);
            require(name.type == 0x454d414e, "missing animation name");
            CharacterModel::Animation a;
            a.name = name.bytes.text(name.bytes.begin, name.bytes.end - name.bytes.begin);
            at = name.next;
            auto group = ifp(root.bytes, at);
            require(group.type == 0x4e414744, "missing animation group");
            at = group.next;
            auto gi = ifp(group.bytes, group.bytes.begin);
            size_t tracks = gi.bytes.u32(gi.bytes.begin);
            require(tracks <= 128, "too many animation tracks");
            size_t p = gi.next;
            for (size_t j = 0; j < tracks; ++j) {
                auto cp = ifp(group.bytes, p);
                require(cp.type == 0x4e415043, "missing animation track");
                p = cp.next;
                auto header = ifp(cp.bytes, cp.bytes.begin);
                require(header.type == 0x4d494e41, "missing animation track header");
                CharacterModel::Track t;
                size_t count = header.bytes.u32(header.bytes.begin + 28);
                t.id = (int)header.bytes.u32(header.bytes.begin + 40);
                require(count <= 65536, "too many animation keys");
                t.translates = false;
                if (count) {
                    auto keys = ifp(cp.bytes, header.next);
                    require(keys.type == 0x3030524b || keys.type == 0x3054524b || keys.type == 0x5354524b,
                            "unsupported animation keys");
                    t.translates = keys.type != 0x3030524b;
                    size_t stride = keys.type == 0x3030524b ? 20 : keys.type == 0x3054524b ? 32 : 44;
                    require(keys.bytes.end - keys.bytes.begin == count * stride, "invalid key stream");
                    for (size_t k = 0; k < count; ++k) {
                        CharacterModel::Key key{};
                        size_t q = keys.bytes.begin + k * stride;
                        for (int x = 0; x < 4; ++x)
                            key.rotation[x] = keys.bytes.real(q + x * 4) * (x == 3 ? 1 : -1);
                        if (t.translates)
                            for (int x = 0; x < 3; ++x)
                                key.translation[x] = keys.bytes.real(q + 16 + x * 4);
                        key.time = keys.bytes.real(q + stride - 4);
                        require(key.time >= 0 && (t.keys.empty() || key.time >= t.keys.back().time),
                                "invalid animation time");
                        a.duration = std::max(a.duration, key.time);
                        t.keys.push_back(key);
                    }
                }
                a.tracks.push_back(std::move(t));
            }
            m.animations.push_back(std::move(a));
        }
    }
}
// PVR v2 ETC1. Decode in software: GL 2.1 desktops need not support compressed
// ETC uploads.
bool texture(const std::vector<uint8_t>& data, unsigned& gl) {
    try {
        Bytes b{data, 0, data.size()};
        uint32_t h = b.u32(4), w = b.u32(8);
        require(b.u32(0) == 52 && b.u32(44) == 0x21525650 && (b.u32(16) & 255) == 0x36 && h > 0 && w > 0 &&
                    h <= 2048 && w <= 2048,
                "unsupported PVR texture");
        size_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        b.check(52, bw * bh * 8);
        std::vector<uint8_t> rgba((size_t)w * h * 4, 255);
        static const int modifiers[8][4] = {{2, 8, -2, -8},       {5, 17, -5, -17},    {9, 29, -9, -29},
                                            {13, 42, -13, -42},   {18, 60, -18, -60},  {24, 80, -24, -80},
                                            {33, 106, -33, -106}, {47, 183, -47, -183}};
        for (size_t y = 0; y < bh; ++y)
            for (size_t x = 0; x < bw; ++x) {
                size_t p = 52 + (y * bw + x) * 8;
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v = (v << 8) | data[p + i];
                uint32_t high = (uint32_t)(v >> 32), low = (uint32_t)v;
                bool diff = high & 2, flip = high & 1;
                int colours[2][3];
                for (int c = 0; c < 3; ++c) {
                    int shift = 24 - c * 8;
                    if (diff) {
                        int a = (high >> (shift + 3)) & 31, d = (high >> shift) & 7;
                        if (d & 4)
                            d -= 8;
                        int z = a + d;
                        require(z >= 0 && z < 32, "invalid ETC1 differential");
                        colours[0][c] = (a << 3) | (a >> 2);
                        colours[1][c] = (z << 3) | (z >> 2);
                    } else {
                        int a = (high >> (shift + 4)) & 15, z = (high >> shift) & 15;
                        colours[0][c] = a * 17;
                        colours[1][c] = z * 17;
                    }
                }
                for (int py = 0; py < 4; ++py)
                    for (int px = 0; px < 4; ++px) {
                        size_t xx = x * 4 + px, yy = y * 4 + py;
                        if (xx >= w || yy >= h)
                            continue;
                        int side = (flip ? py : px) >= 2, bit = px * 4 + py,
                            index = ((low >> bit) & 1) | (((low >> (bit + 16)) & 1) << 1),
                            table = (high >> (side ? 2 : 5)) & 7;
                        for (int c = 0; c < 3; ++c)
                            rgba[(yy * w + xx) * 4 + c] =
                                (uint8_t)std::clamp(colours[side][c] + modifiers[table][index], 0, 255);
                    }
            }
        glGenTextures(1, &gl);
        glBindTexture(GL_TEXTURE_2D, gl);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace

bool CharacterModel::parse(const std::vector<uint8_t>& model, const std::vector<uint8_t>& animation,
                           std::string* error) {
    CharacterModel parsed;
    try {
        require(model.size() <= (8 << 20) && animation.size() <= (32 << 20), "character resource too large");
        Bytes b{model, 0, model.size()};
        auto clump = chunk(b, 0);
        require(clump.type == 16, "missing character clump");
        for (size_t p = clump.bytes.begin; p < clump.bytes.end;) {
            auto c = chunk(clump.bytes, p);
            p = c.next;
            if (c.type == 14)
                parseFrames(parsed, c.bytes);
            if (c.type == 20) {
                auto s = chunk(c.bytes, c.bytes.begin);
                auto g = chunk(c.bytes, s.next);
                require(g.type == 15, "missing mobile geometry");
                parseGeometry(parsed, g.bytes);
            }
        }
        require(!parsed.meshes.empty() && parsed.inverse.size() == parsed.boneFrames.size(),
                "incomplete character model");
        parsed.floor = 1e9f;
        for (const auto& mesh : parsed.meshes) {
            require(mesh.material >= 0 && mesh.material < (int)parsed.materials.size(),
                    "invalid material index");
            for (const auto& v : mesh.vertices) {
                parsed.floor = std::min(parsed.floor, v.pos[2]);
                for (int j = 0; j < 4; ++j)
                    if (v.weights[j] > 0)
                        require(v.bones[j] < parsed.inverse.size(), "invalid vertex bone");
            }
        }
        parseAnimations(parsed, {animation, 0, animation.size()});
        *this = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    }
}
bool CharacterModel::load(const std::string& model, const std::string& animation, const std::string& textures,
                          std::string* error) {
    if (!parse(read(model), read(animation), error))
        return false;
    for (auto& m : materials)
        if (!texture(read((std::filesystem::u8path(textures) / (m.texture + ".pvr")).u8string()), m.gl)) {
            if (error)
                *error = "missing or unsupported texture: " + m.texture;
            release();
            return false;
        }
    return true;
}
void CharacterModel::release() {
    for (auto& m : materials)
        if (m.gl) {
            glDeleteTextures(1, &m.gl);
            m.gl = 0;
        }
}
bool CharacterModel::pose(const char* name, float seconds, bool loop, std::vector<Matrix>& out,
                          std::vector<Matrix>* frameWorld) const {
    if (!name || !std::isfinite(seconds))
        return false;
    const Animation* animation = nullptr;
    for (const auto& a : animations)
        if (a.name == name) {
            animation = &a;
            break;
        }
    if (!animation)
        return false;
    float t = std::max(0.f, seconds);
    if (animation->duration > 0)
        t = loop ? std::fmod(t, animation->duration) : std::min(t, animation->duration);
    std::vector<Matrix> local;
    for (const auto& f : frames)
        local.push_back(f.local);
    for (const auto& track : animation->tracks) {
        if (track.keys.empty())
            continue;
        int index = -1;
        for (size_t i = 0; i < frames.size(); ++i)
            if (frames[i].id == track.id) {
                index = (int)i;
                break;
            }
        if (index < 0)
            continue;
        auto it = std::upper_bound(track.keys.begin(), track.keys.end(), t,
                                   [](float v, const Key& key) { return v < key.time; });
        const Key& a = it == track.keys.begin() ? *it : *(it - 1);
        const Key& b = it == track.keys.end() ? track.keys.back() : *it;
        float u = b.time > a.time ? std::clamp((t - a.time) / (b.time - a.time), 0.f, 1.f) : 0, q[4], dot = 0,
              norm = 0;
        for (int j = 0; j < 4; ++j)
            dot += a.rotation[j] * b.rotation[j];
        for (int j = 0; j < 4; ++j) {
            q[j] = a.rotation[j] * (1 - u) + b.rotation[j] * (dot < 0 ? -u : u);
            norm += q[j] * q[j];
        }
        if (norm < 1e-12f)
            return false;
        for (float& x : q)
            x /= std::sqrt(norm);
        float x = q[0], y = q[1], z = q[2], w = q[3];
        Matrix m = identity();
        m[0] = 1 - 2 * (y * y + z * z);
        m[1] = 2 * (x * y + z * w);
        m[2] = 2 * (x * z - y * w);
        m[4] = 2 * (x * y - z * w);
        m[5] = 1 - 2 * (x * x + z * z);
        m[6] = 2 * (y * z + x * w);
        m[8] = 2 * (x * z + y * w);
        m[9] = 2 * (y * z - x * w);
        m[10] = 1 - 2 * (x * x + y * y);
        for (int j = 0; j < 3; ++j)
            m[12 + j] =
                track.translates ? a.translation[j] * (1 - u) + b.translation[j] * u : local[index][12 + j];
        // Root motion belongs to the simulation; preserve the animation's vertical
        // movement only.
        if (track.id == 0) {
            m[12] = local[index][12];
            m[13] = local[index][13];
        }
        local[index] = m;
    }
    std::vector<Matrix> world(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        int p = frames[i].parent;
        world[i] = p < 0 || frames[p].id < 0 ? local[i] : multiply(world[p], local[i]);
    }
    out.resize(inverse.size());
    for (size_t i = 0; i < inverse.size(); ++i)
        out[i] = multiply(world[boneFrames[i]], inverse[i]);
    if (frameWorld) *frameWorld = std::move(world);
    return true;
}
bool CharacterModel::attachmentTransform(const char* clip, float seconds, bool loop, float scale, int boneId,
                                         const float basis[9], const float anchor[3], Matrix& out) const {
    if (!basis || !anchor || !std::isfinite(scale) || scale <= 0 || scale > 8) return false;
    for (int k = 0; k < 9; ++k) if (!std::isfinite(basis[k]) || std::abs(basis[k]) > 2) return false;
    for (int k = 0; k < 3; ++k) if (!std::isfinite(anchor[k]) || std::abs(anchor[k]) > 32768) return false;
    int frame = -1;
    for (size_t i = 0; i < frames.size(); ++i) if (frames[i].id == boneId) { frame = (int)i; break; }
    std::vector<Matrix> skin, world;
    if ((frame < 0 && boneId != -1) || !pose(clip, seconds, loop, skin, &world)) return false;
    const float origin[3] = {frame >= 0 ? world[frame][12] : 0,
                             frame >= 0 ? world[frame][13] : 0,
                             frame >= 0 ? world[frame][14] : floor};
    // Reject degenerate/skew frames; callers supply a unit basis, scale separately.
    for (int i = 0; i < 3; ++i) for (int j = i; j < 3; ++j) {
        float dot = 0;
        for (int k = 0; k < 3; ++k) dot += basis[3*i+k]*basis[3*j+k];
        if (std::abs(dot - (i == j ? 1.f : 0.f)) > .01f) return false;
    }
    Matrix result = identity();
    for (int k = 0; k < 3; ++k) {
        result[12+k] = anchor[k];
        for (int i = 0; i < 3; ++i) {
            result[4*i+k] = scale*basis[3*i+k];
            result[12+k] -= result[4*i+k]*origin[i];
        }
    }
    out = result;
    return true;
}
namespace {
void skinnedBounds(const std::vector<CharacterModel::Mesh>& meshes,
                   const std::vector<CharacterModel::Matrix>& bones, float lo[3], float hi[3]) {
    std::fill(lo, lo + 3, INFINITY);
    std::fill(hi, hi + 3, -INFINITY);
    for (const auto& mesh : meshes)
        for (const auto& v : mesh.vertices) {
            float p[3]{};
            for (int j = 0; j < 4; ++j)
                if (v.weights[j] > 0) {
                    const auto& b = bones[v.bones[j]];
                    for (int k = 0; k < 3; ++k)
                        p[k] += v.weights[j] * (b[k] * v.pos[0] + b[4+k] * v.pos[1] +
                                                b[8+k] * v.pos[2] + b[12+k]);
                }
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], p[k]);
                hi[k] = std::max(hi[k], p[k]);
            }
        }
}
}
bool CharacterModel::bounds(const char* animation, float seconds, float lo[3], float hi[3],
                            float* duration) const {
    if (!lo || !hi) return false;
    std::vector<Matrix> bones;
    if (!pose(animation, seconds, false, bones)) return false;
    skinnedBounds(meshes, bones, lo, hi);
    if (duration)
        for (const auto& a : animations)
            if (a.name == animation) { *duration = a.duration; break; }
    return std::isfinite(lo[2]) && std::isfinite(hi[2]);
}
bool CharacterModel::draw(const float p[3], float heading, float scale, const char* animation, float seconds,
                          bool loop, bool groundPose, const Matrix* attachment) const {
    std::vector<Matrix> bones;
    if (!pose(animation, seconds, loop, bones))
        return false;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT |
                 GL_LIGHTING_BIT);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    if (glIsEnabled(GL_LIGHT0)) {
        glEnable(GL_LIGHTING);
        glEnable(GL_NORMALIZE);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    } else
        glDisable(GL_LIGHTING);
    glColor4f(1, 1, 1, 1);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    GLint previousMatrixMode;
    glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    if (attachment) glMultMatrixf(attachment->data());
    else {
        glTranslatef(p[0], p[1], p[2]);
        glRotatef(heading, 0, 0, 1);
        glScalef(scale, scale, scale);
        float origin = floor;
        if (groundPose) {
            float lo[3], hi[3];
            skinnedBounds(meshes, bones, lo, hi);
            origin = lo[2];
        }
        glTranslatef(0, 0, -origin);
    }
    for (const auto& mesh : meshes) {
        glBindTexture(GL_TEXTURE_2D, materials[mesh.material].gl);
        glBegin(GL_TRIANGLES);
        for (const auto& v : mesh.vertices) {
            float vertex[3] = {}, normal[3] = {};
            for (int j = 0; j < 4; ++j)
                if (v.weights[j] > 0) {
                    const auto& m = bones[v.bones[j]];
                    for (int k = 0; k < 3; ++k) {
                        vertex[k] += v.weights[j] * (m[k] * v.pos[0] + m[4 + k] * v.pos[1] +
                                                     m[8 + k] * v.pos[2] + m[12 + k]);
                        normal[k] += v.weights[j] *
                                     (m[k] * v.normal[0] + m[4 + k] * v.normal[1] + m[8 + k] * v.normal[2]);
                    }
                }
            glNormal3fv(normal);
            glTexCoord2fv(v.uv);
            glVertex3fv(vertex);
        }
        glEnd();
    }
    glPopMatrix();
    glMatrixMode(previousMatrixMode);
    glPopAttrib();
    return true;
}
