// The road network cars drive on: ai.bin (cAIHub::Init), in ROM.WAD.
//
//   u32 linkCount, u32 nodeCount, u32 (0), u32 nodeOffset; u16 links[linkCount] at 0x10; nodes at nodeOffset,
//   10 bytes each (cBaseNode): u16 firstLink, u16 flags, i16 x, i16 y (1/8 unit), i8 z (1/2 unit), u8 cover slots.
//   flags: bits 0-1 reference count (runtime), 2-4 number of links, 5 ?, 7 bridge, 9 two lanes, 10 highway,
//          11 restricted (only taken when asked), 15 junction with traffic control (cNodeId::IsToSlowForHoldingPattern).
// Links are one-way: node -> links[firstLink .. firstLink + count). cNodeId (u32) = index | type << 16 with type 0 for
// these global nodes (1 = a sector's own nodes, 2 = dynamic nodes; not used for traffic).
// sNodeMetaData (cBaseNode::GetMetaData): lanes = 1 + bit 9; lane half width 2.5 (3.75 on highways, bit 10);
// +0x14 lane offset 0.5 (0.75 on highways); +0x18 per-car wobble 1/16 (0x140/4096 on highways).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RoadMeta { int lanes, halfWidth, lanesMinus1, width; bool flag5; int laneOffset, wobble; };   // sNodeMetaData

class RoadNetwork {
public:
    struct Node { uint16_t first, flags; int16_t x, y; int8_t z; uint8_t cover; };
    bool load(const std::string& dataDir);
    bool ok() const { return !nodes_.empty(); }

    int count() const { return (int)nodes_.size(); }
    const Node& node(int i) const { return nodes_[i]; }
    int linkCount(int i) const { return nodes_[i].flags >> 2 & 7; }
    int link(int i, int k) const { return links_[nodes_[i].first + k]; }
    void pos(int i, int32_t out[3]) const;      // cBaseNode::Pos (20.12)
    void pos2d(int i, int32_t out[2]) const;    // cBaseNode::Pos2D
    RoadMeta meta(int i) const;                 // cBaseNode::GetMetaData

    // debug: the links around (x, y) as lines (arrow heads at the far end)
    void debugDraw(float x, float y, float range) const;

private:
    std::vector<Node> nodes_;
    std::vector<uint16_t> links_;
};
