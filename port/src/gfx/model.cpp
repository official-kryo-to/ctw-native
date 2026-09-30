#include "model.h"
#include <cstring>

bool Model::parse(const std::vector<uint8_t>& b) {
    verts.clear();
    batches.clear();
    nodes.clear();
    if (b.size() < 0x30 || b[0] != 'M' || b[1] != 'G') return false;
    uint16_t A, D;
    memcpy(&A, &b[2], 2);
    memcpy(&D, &b[6], 2);
    uint8_t B = b[4], C = b[5];
    size_t need = 16 + (size_t)B * 32 + (size_t)D * 16 + (size_t)C * 12 + (size_t)A * 192;
    if (b.size() < need || B == 0) return false;

    int32_t bb[6];
    memcpy(bb, &b[0x10], sizeof bb);
    for (int i = 0; i < 3; ++i) { bboxMin[i] = bb[i] / 4096.f; bboxMax[i] = bb[3 + i] / 4096.f; }
    memcpy(&scale, &b[0x10 + 24], 4);

    verts.resize(D);
    memcpy(verts.data(), &b[0x30], (size_t)D * 16);

    size_t no = 0x30 + (size_t)D * 16;
    NodeMatrix ident{};
    ident.r[0][0] = ident.r[1][1] = ident.r[2][2] = 1.f;
    nodes.push_back(ident);
    for (uint8_t k = 0; k + 1 < B; ++k) {
        const uint8_t* n = &b[no + 32 * (size_t)k];
        int16_t rot[9];
        int32_t tr[3];
        memcpy(rot, n, 18);
        memcpy(tr, n + 20, 12);
        NodeMatrix m;
        m.parent = (int8_t)n[18];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m.r[r][c] = rot[r * 3 + c] / 4096.f;
        for (int i = 0; i < 3; ++i) m.t[i] = tr[i] / 4096.f;
        nodes.push_back(m);
    }

    // Resolve the hierarchy in order (RefreshMatrices walks the nodes sequentially, so parents come first).
    world.resize(nodes.size());
    for (size_t k = 0; k < nodes.size(); ++k) {
        const NodeMatrix& L = nodes[k];
        int p = L.parent;
        if (p <= 0 || (size_t)(p - 1) >= k) { world[k] = L; continue; }   // no (earlier) parent: local is final
        const NodeMatrix& P = world[p - 1];
        NodeMatrix W = L;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                W.r[r][c] = P.r[r][0] * L.r[0][c] + P.r[r][1] * L.r[1][c] + P.r[r][2] * L.r[2][c];
            W.t[r] = P.r[r][0] * L.t[0] + P.r[r][1] * L.t[1] + P.r[r][2] * L.t[2] + P.t[r];
        }
        world[k] = W;
    }

    size_t ro = no + 32 * (size_t)(B - 1);
    uint32_t first = 0;
    for (uint8_t k = 0; k < C; ++k) {
        const uint8_t* r = &b[ro + 12 * (size_t)k];
        ModelBatch mb;
        memcpy(&mb.texture, r, 2);
        memcpy(&mb.count, r + 2, 2);
        mb.flags = r[4]; mb.node = r[5]; mb.alpha = r[6]; mb.unk = r[7];
        memcpy(&mb.mask, r + 8, 4);
        mb.firstVertex = first;
        first += mb.count;
        if (first > D || mb.node >= nodes.size()) return false;   // counts must partition the vertex array
        batches.push_back(mb);
    }
    return first == D;
}
