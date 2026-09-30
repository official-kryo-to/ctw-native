#include "roads.h"
#include "os/gamefs.h"
#include <glad/gl.h>
#include <cmath>
#include <cstdio>
#include <cstring>

bool RoadNetwork::load(const std::string& dataDir) {
    GameFs fs;
    std::vector<uint8_t> d;
    if (!fs.open(dataDir) || !fs.read("ai.bin", d) || d.size() < 16) return false;
    uint32_t nl, nn, nv, off;
    memcpy(&nl, &d[0], 4); memcpy(&nn, &d[4], 4); memcpy(&nv, &d[8], 4); memcpy(&off, &d[12], 4);
    if (0x10 + (size_t)nl * 2 > d.size() || off + (size_t)nn * 10 > d.size()) return false;
    links_.resize(nl);
    memcpy(links_.data(), &d[0x10], (size_t)nl * 2);
    nodes_.resize(nn);
    for (uint32_t i = 0; i < nn; ++i) memcpy(&nodes_[i], &d[off + i * 10], 10);
    for (Node& n : nodes_)   // drop links that point outside the table
        if (n.first + (n.flags >> 2 & 7) > nl) n.flags &= ~0x1C;
    printf("-=[ AI node data loaded: %u nodes, %u links ]=-\n", nn, nl);
    return true;
}

void RoadNetwork::pos(int i, int32_t o[3]) const {
    const Node& n = nodes_[i];
    o[0] = n.x << 9; o[1] = n.y << 9; o[2] = n.z << 11;
}

void RoadNetwork::pos2d(int i, int32_t o[2]) const {
    const Node& n = nodes_[i];
    o[0] = n.x << 9; o[1] = n.y << 9;
}

RoadMeta RoadNetwork::meta(int i) const {   // cBaseNode::GetMetaData
    const Node& n = nodes_[i];
    RoadMeta m{};
    int two = n.flags >> 9 & 1;
    m.lanes = two + 1;
    m.flag5 = (n.flags >> 5 & 1) != 0;
    bool hwy = (n.flags >> 10 & 1) != 0;
    if (n.z < -4) {   // in the water (boats)
        m.halfWidth = hwy ? 0x16800 : 0x7800;   // VEHICLE_(HIGHWAY_)LANE_IN_WATER_HALF_WIDTH
        m.laneOffset = 0x1000;
        m.wobble = (m.halfWidth - 0x3000) >> 3;
    } else if (hwy) {
        m.halfWidth = 0x3C00;   // VEHICLE_HIGHWAY_LANE_HALF_WIDTH
        m.laneOffset = 0xC00;
        m.wobble = 0x140;
    } else {
        m.halfWidth = 0x2800;   // VEHICLE_LANE_HALF_WIDTH
        m.laneOffset = 0x800;
        m.wobble = 0x100;
    }
    m.lanesMinus1 = two;
    m.width = (int)(((int64_t)m.lanes * m.halfWidth) >> 12);
    return m;
}

void RoadNetwork::debugDraw(float x, float y, float range) const {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.f);
    glBegin(GL_LINES);
    for (int i = 0; i < count(); ++i) {
        const Node& n = nodes_[i];
        float ax = n.x / 8.f, ay = n.y / 8.f, az = n.z / 2.f + 0.2f;
        if (std::fabs(ax - x) > range || std::fabs(ay - y) > range) continue;
        for (int k = 0; k < linkCount(i); ++k) {
            const Node& m = nodes_[link(i, k)];
            float bx = m.x / 8.f, by = m.y / 8.f, bz = m.z / 2.f + 0.2f;
            glColor3f(0.2f, 1.f, 0.3f);
            glVertex3f(ax, ay, az); glVertex3f(bx, by, bz);
            float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
            if (l < 0.01f) continue;
            dx /= l; dy /= l;
            glColor3f(1.f, 0.3f, 0.2f);   // arrow head
            glVertex3f(bx, by, bz); glVertex3f(bx - dx * 1.5f - dy * 0.8f, by - dy * 1.5f + dx * 0.8f, bz);
            glVertex3f(bx, by, bz); glVertex3f(bx - dx * 1.5f + dy * 0.8f, by - dy * 1.5f - dx * 0.8f, bz);
        }
    }
    glEnd();
    glLineWidth(1.f);
    glEnable(GL_DEPTH_TEST);
}
