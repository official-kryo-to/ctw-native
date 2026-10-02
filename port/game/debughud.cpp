// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// F3: the debug overlay (a PC addition). Collision and road diagnostics are drawn in the world by Game::render.
#include "game.h"
#include "hud.h"
#include "particles.h"
#include "audio/audio.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Panel {
    std::vector<std::pair<std::string, uint32_t>> lines;
    void head(const char* s) { lines.emplace_back(s, 0xF2C230FFu); }
    void row(const char* fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        lines.emplace_back(buf, 0xE6E8EEFFu);
    }
    void draw(float x, float y, float scale) const {
        float w = 0.f;
        for (auto& l : lines) w = std::max(w, Hud_TextWidth(l.first, scale));
        const float lh = Hud_LineHeight(scale);
        Hud_Rect(x - 8, y - 6, w + 16, lh * lines.size() + 12, 0x0B0D12C0u);
        for (auto& l : lines) { Hud_Text(x, y, scale, l.second, l.first); y += lh; }
    }
};
float units(int32_t q12) { return q12 / 4096.f; }
float degrees(int16_t a) { return a * 360.f / 65536.f; }
}

void Game::drawDebug(int W, int H) {
    (void)H;
    const float s = 0.8f;
    Panel left;
    const uint32_t t = world.timeCycle().time();
    left.head("PERFORMANCE");
    left.row("%.0f fps   tick %u   speed x%.2f%s", fps, frame, gameSpeed, uiPaused ? "   paused" : "");
    const auto ws = world.stats();
    left.row("blocks %zu   draws %zu   objects %d", ws.blocks, ws.draws, ws.objects);
    left.head("POSITION");
    int32_t at[3];
    focus(at);
    int cx, cy;
    Collision::cellOfPos(at[0], at[1], cx, cy);
    left.row("%.2f  %.2f  %.2f", units(at[0]), units(at[1]), units(at[2]));
    left.row("cell %d, %d   heading %.0f", cx, cy, degrees(playerCar >= 0 ? cars[playerCar].heading() : player.heading()));
    const std::string zone = traffic.zoneName(at[0], at[1]);
    left.row("zone %s", zone.empty() ? "-" : zone.c_str());
    left.head("WORLD");
    left.row("time %02u:%02u%s   weather %d -> %d%s   rain %.2f", t >> 12, (t & 0xFFF) * 60 >> 12,
             clockRunning ? "" : " (frozen)", world.timeCycle().weather(), world.timeCycle().nextWeather(),
             weather.forced != 8 ? " (forced)" : "", world.timeCycle().value(28) / 4096.f);
    left.row("camera %s", freeCam.on ? "free (mod)" : player.dead ? "death" : playerCar >= 0 ? "car" : "on foot");
    if (playerCar >= 0)
        left.row("car cam blocked %u frames", carCam.blockedFrames());
    else
        left.row("ped cam blocked %u frames", camera.blockedFrames());
    left.draw(16, 16, s);

    Panel right;
    if (playerCar >= 0) {
        const Vehicle& c = cars[playerCar];
        right.head("VEHICLE");
        std::string name = c.infoId < (int)vehicleInfos.size() ? vehicleInfos[c.infoId].name() : "?";
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".vehicle") == 0) name.resize(name.size() - 8);
        right.row("%s  (#%d, palette %d)", name.c_str(), c.infoId, c.palette);
        right.row("%.0f km/h   gear %d", units(c.speed()) * 3.6f, c.gear());
        const auto ss = c.soundState();
        right.row("rpm %d / %d   %s", ss.rpm >> 12, ss.maxRpm >> 12, c.physicsActive() ? "physics" : "simple");
        right.row("health %d%s", c.health(), c.dead() ? "  (wrecked)" : c.health() < 31 ? "  (burning)" : "");
        right.row("radio %s   volume %d", radio.stationName(radio.station()) ? radio.stationName(radio.station()) : "-",
                  radio.volumeLevel());
    } else {
        right.head("PLAYER");
        right.row("%.1f units/s   level %d", units(player.currentSpeed()), player.level());
        right.row("%s%s", player.onGround() ? "on ground" : "in the air", player.dead ? "   dead" : "");
    }
    right.head("POPULATION");
    int down = 0, evading = 0;
    for (const auto& p : peds.peds) { down += p.deadFrames >= 0; evading += p.evading; }
    right.row("peds %d / %d   down %d   evading %d", peds.alive(), peds.maxPeds, down, evading);
    right.row("vehicles %zu   moving traffic %d / %d", cars.size(), traffic.count(), traffic.maxCars);
    right.row("loose props %d   emitters %d", propDynamics.looseCount(), TheParticles().emitterCount());
    right.head("AUDIO");
    right.row("sound voices %d / 32", Audio_SfxActive());
    right.row("music %s   %.1f s", Audio_MusicPlaying() ? "playing" : "stopped", Audio_MusicPosition());
    float w = 0.f;
    for (auto& l : right.lines) w = std::max(w, Hud_TextWidth(l.first, s));
    right.draw(W - 16 - w, 16, s);
}
