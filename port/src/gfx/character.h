// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Optional, user-imported Leeds skinned characters. No game logic or camera
// policy. Parsers accept the mobile RSL PS2 stream and ANPK animation bank,
// with bounded reads.
class CharacterModel {
  public:
    using Matrix = std::array<float, 16>;
    struct Vertex {
        float pos[3], normal[3], uv[2], weights[4];
        uint8_t bones[4];
    };
    struct Mesh {
        int material;
        std::vector<Vertex> vertices;
    };
    struct Frame {
        Matrix local;
        int parent = -1, id = -1;
    };
    struct Key {
        float time, rotation[4], translation[3];
    };
    struct Track {
        int id;
        bool translates;
        std::vector<Key> keys;
    };
    struct Animation {
        std::string name;
        float duration = 0;
        std::vector<Track> tracks;
    };
    struct Material {
        std::string texture;
        unsigned gl = 0;
    };

    bool parse(const std::vector<uint8_t>& model, const std::vector<uint8_t>& animation,
               std::string* error = nullptr);
    bool load(const std::string& model, const std::string& animation, const std::string& textures,
              std::string* error = nullptr);
    void release(); // with a current GL context
    bool pose(const char* animation, float seconds, bool loop, std::vector<Matrix>& out,
              std::vector<Matrix>* frameWorld = nullptr) const;
    // Unscaled skinned bounds, before translating the rest-pose floor to zero.
    bool bounds(const char* animation, float seconds, float minimum[3], float maximum[3],
                float* duration = nullptr) const;
    bool draw(const float position[3], float headingDegrees, float scale, const char* animation,
              float seconds, bool loop, bool groundPose = false,
              const Matrix* attachment = nullptr) const;
    // Align an animated bone to a native anchor using the complete right/forward/up frame.
    // No rest-floor adjustment: the bone, rather than the feet, owns this attachment.
    bool attachmentTransform(const char* animation, float seconds, bool loop, float scale, int boneId,
                             const float basis[9], const float anchor[3], Matrix& out) const;
    std::vector<Frame> frames;
    std::vector<int> boneFrames;
    std::vector<Matrix> inverse;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Animation> animations;
    float floor = 0;
};
