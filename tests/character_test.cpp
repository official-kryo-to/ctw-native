// SPDX-License-Identifier: GPL-3.0-only
#include "gfx/character.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using Data = std::vector<uint8_t>;
static int failures;
static void check(bool ok, const char* why) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", why);
        ++failures;
    }
}
static void word(Data& data, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        data.push_back((uint8_t)(value >> (8 * i)));
}
static void real(Data& data, float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    word(data, bits);
}
static void put(Data& data, size_t at, uint32_t value) { memcpy(data.data() + at, &value, 4); }
static void append(Data& a, const Data& b) { a.insert(a.end(), b.begin(), b.end()); }
static Data rw(uint32_t kind, const Data& body) {
    Data out;
    word(out, kind);
    word(out, (uint32_t)body.size());
    word(out, 0x310);
    append(out, body);
    return out;
}
static Data ifp(const char* kind, const Data& body) {
    Data out(kind, kind + 4);
    word(out, (uint32_t)body.size());
    append(out, body);
    while (out.size() % 4)
        out.push_back(0);
    return out;
}
static Data identity() {
    Data data;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            real(data, col == row ? 1.f : 0.f);
    return data;
}
static Data model() {
    Data frame;
    word(frame, 1);
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 3; ++row)
            real(frame, col == row ? 1.f : 0.f);
    word(frame, UINT32_MAX);
    word(frame, 0);
    Data hierarchy;
    for (uint32_t v : {256u, 0u, 1u, 0u, 0u, 0u, 0u, 0u})
        word(hierarchy, v);
    Data frames = rw(1, frame);
    append(frames, rw(3, rw(0x11e, hierarchy)));
    Data packet(16);
    Data tag(20);
    put(tag, 0, 0x6c018000);
    put(tag, 16, 3);
    append(packet, tag);
    auto setup = [&]() {
        word(packet, 0x20000000);
        word(packet, 0);
        word(packet, 0x30000000);
        for (int i = 0; i < 4; ++i)
            word(packet, 0);
    };
    setup();
    word(packet, 0x79038000);
    for (int v = 0; v < 3; ++v)
        for (int c = 0; c < 3; ++c) {
            uint16_t x = (v == c) ? 32767 : 0;
            packet.push_back(x & 255);
            packet.push_back(x >> 8);
        }
    while (packet.size() % 4)
        packet.push_back(0);
    setup();
    word(packet, 0x7603c000);
    for (int v = 0; v < 3; ++v) {
        packet.push_back(0);
        packet.push_back(128);
    }
    while (packet.size() % 4)
        packet.push_back(0);
    word(packet, 0x6a038000);
    for (int v = 0; v < 3; ++v) {
        packet.push_back(0);
        packet.push_back(0);
        packet.push_back(127);
    }
    while (packet.size() % 4)
        packet.push_back(0);
    word(packet, 0x6c038000);
    for (int v = 0; v < 3; ++v) {
        real(packet, 1);
        for (int j = 0; j < 3; ++j)
            word(packet, 0);
    }
    word(packet, 0x14000006);
    while (packet.size() % 16)
        word(packet, 0);
    put(packet, 0, (uint32_t)packet.size() / 16 - 1);
    Data geometry(64 + 48);
    put(geometry, 16, (1 << 20) | (uint32_t)(geometry.size() + packet.size()));
    put(geometry, 20, 23);
    for (int i = 0; i < 3; ++i)
        put(geometry, 40 + i * 4, 0x3f800000);
    put(geometry, 64 + 16, 0x3f800000);
    put(geometry, 64 + 20, 0x3f800000);
    append(geometry, packet);
    Data tex = rw(1, Data(4));
    append(tex, rw(2, Data{'t', 'e', 's', 't', 0}));
    append(tex, rw(2, Data{0}));
    Data material = rw(1, Data(28));
    append(material, rw(6, tex));
    Data ml;
    word(ml, 1);
    word(ml, UINT32_MAX);
    ml = rw(1, ml);
    append(ml, rw(7, material));
    append(geometry, rw(8, ml));
    Data skin;
    word(skin, 1);
    append(skin, identity());
    append(geometry, rw(3, rw(0x116, skin)));
    Data atomic = rw(1, Data(16));
    append(atomic, rw(15, geometry));
    Data clump = rw(14, frames);
    append(clump, rw(20, atomic));
    return rw(16, clump);
}
static Data animation() {
    Data header(44);
    put(header, 28, 2);
    Data keys;
    for (int i = 0; i < 2; ++i) {
        for (float q : {0.f, 0.f, 0.f, 1.f})
            real(keys, q);
        real(keys, (float)i);
    }
    Data track = ifp("ANIM", header);
    append(track, ifp("KR00", keys));
    Data gi;
    word(gi, 1);
    word(gi, 0);
    Data group = ifp("INFO", gi);
    append(group, ifp("CPAN", track));
    Data body;
    word(body, 1);
    word(body, 0);
    body = ifp("INFO", body);
    append(body, ifp("NAME", Data{'i', 'd', 'l', 'e', 0}));
    append(body, ifp("DGAN", group));
    return ifp("ANPK", body);
}
int main() {
    auto dff = model(), bank = animation();
    CharacterModel m;
    std::string error;
    check(m.parse(dff, bank, &error), error.c_str());
    check(m.meshes.size() == 1 && m.meshes[0].vertices.size() == 3,
          "mobile VIF geometry becomes one triangle");
    check(m.materials.size() == 1 && m.materials[0].texture == "test", "material texture names are read");
    std::vector<CharacterModel::Matrix> pose;
    check(m.pose("idle", .5f, true, pose) && pose.size() == 1 && std::abs(pose[0][0] - 1) < 1e-5,
          "bind pose and skin matrix agree");
    check(!m.pose("missing", 0, false, pose) &&
              !m.pose("idle", std::numeric_limits<float>::quiet_NaN(), true, pose),
          "missing animations and invalid time fail safely");
    for (size_t n = 0; n < dff.size(); ++n) {
        Data truncated(dff.begin(), dff.begin() + n);
        CharacterModel invalid;
        check(!invalid.parse(truncated, bank), "truncated model is rejected");
    }
    auto corrupt = dff;
    put(corrupt, 4, UINT32_MAX);
    check(!m.parse(corrupt, bank), "overflowing chunk length is rejected");
    auto old = m.meshes.size();
    check(!m.parse({}, bank) && m.meshes.size() == old, "failed parse preserves the loaded model");
    // Animated translation preserves vertical movement but simulation owns horizontal root motion.
    auto& track = m.animations[0].tracks[0];
    track.translates = true;
    track.keys[0].translation[0] = 0;
    track.keys[1].translation[0] = 10;
    track.keys[0].translation[2] = 0;
    track.keys[1].translation[2] = 2;
    check(m.pose("idle", .5f, false, pose) && pose[0][12] == 0 && pose[0][14] == 1,
          "root motion does not move the simulated character");
    float lo[3], hi[3], duration = -1;
    check(m.bounds("idle", .5f, lo, hi, &duration) && duration == 1 &&
              std::abs(lo[2] - 1) < 1e-5 && std::abs(hi[2] - 2) < 1e-4,
          "bounds include skinning and vertical motion for fitting and ground contact");
    check(m.bounds("idle", 100, lo, hi) && std::abs(lo[2] - 2) < 1e-5,
          "a completed timed one-shot holds its final pose");
    check(!m.bounds("missing", 0, lo, hi) && !m.bounds("idle", NAN, lo, hi),
          "invalid bounds queries fail safely");
    const float basis[9] = {0,1,0, 0,0,1, 1,0,0}, anchor[3] = {3,4,5};
    CharacterModel::Matrix attachment;
    check(m.attachmentTransform("idle",.5f,false,2,0,basis,anchor,attachment) &&
          attachment[0] == 0 && attachment[1] == 2 && attachment[6] == 2 &&
          attachment[8] == 2 && attachment[12] == 1 && attachment[13] == 4 && attachment[14] == 5,
          "animated bone anchoring rotates pitch/roll and places the joint at the native anchor");
    float invalidBasis[9]{};
    check(!m.attachmentTransform("idle",0,false,2,99,basis,anchor,attachment) &&
          !m.attachmentTransform("idle",0,false,2,0,invalidBasis,anchor,attachment) &&
          !m.attachmentTransform("idle",NAN,false,2,0,basis,anchor,attachment),
          "missing bones and invalid attachment frames fail safely");
    check(m.attachmentTransform("idle",.5f,false,2,-1,basis,anchor,attachment) &&
          attachment[12] == 3 && attachment[13] == 4 && attachment[14] == 5,
          "floor attachments preserve vertical animation rather than pinning a moving root bone");
    append(bank, animation());
    check(m.parse(dff, bank) && m.animations.size() == 2,
          "concatenated pedestrian and bike animation banks load");
    return failures ? 1 : 0;
}
