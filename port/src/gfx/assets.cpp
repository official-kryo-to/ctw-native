#include "assets.h"
#include "os/pak.h"
#include "os/dxtbin.h"
#include "gfx/texload.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <map>

static Pak g_pak;
static DxtBin g_dxt;
static TextureLoader g_loader;
static std::map<int, unsigned> g_cache;
static std::map<int, std::pair<int, int>> g_sizes;
static bool g_open = false;

bool Assets_Open(const std::string& dataDir) {
    if (g_open) return true;
    if (!g_pak.open(dataDir + "/game.pak")) return false;
    bool dxt = g_dxt.open(dataDir + "/dxt.bin");
    g_loader.init(&g_pak, dxt ? &g_dxt : nullptr);
    g_open = true;
    return true;
}

Pak& Assets_Pak() { return g_pak; }

bool Assets_OverrideTexturePNG(int id, const std::string& pngPath) {
    int w, h, n;
    unsigned char* px = stbi_load(pngPath.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    auto it = g_cache.find(id);
    if (it != g_cache.end() && it->second) glDeleteTextures(1, &it->second);
    g_cache[id] = t;
    g_sizes.erase(id);
    return true;
}

void Assets_RestoreTexture(int id) {
    auto it = g_cache.find(id);
    if (it == g_cache.end()) return;
    if (it->second) glDeleteTextures(1, &it->second);
    g_cache.erase(it);
    g_sizes.erase(id);
}

bool Assets_TextureSize(int id, int* w, int* h) {
    auto it = g_sizes.find(id);
    if (it == g_sizes.end()) {
        unsigned t = Assets_Texture(id);
        if (!t) return false;
        GLint tw = 0, th = 0;
        glBindTexture(GL_TEXTURE_2D, t);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        it = g_sizes.emplace(id, std::make_pair((int)tw, (int)th)).first;
    }
    *w = it->second.first;
    *h = it->second.second;
    return *w > 0 && *h > 0;
}

unsigned Assets_Texture(int id) {
    auto it = g_cache.find(id);
    if (it != g_cache.end()) return it->second;
    TexInfo ti;
    unsigned tex = g_open && g_loader.load((uint32_t)id, &ti) ? ti.gl : 0;
    g_cache[id] = tex;
    return tex;
}
