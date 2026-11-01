// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "ctw_types.h"
#include "localarea.h"
#include <cstddef>
#include <deque>
#include <map>
#include <array>
#include <vector>

class Game;
class MultiplayerWorld {
public:
    int role() const { return role_; }
    bool client() const { return role_ == CTW_MULTIPLAYER_CLIENT; }
    bool replaying() const { return replaying_; }
    bool simulatesVehicle(const Game& game, int index) const;
    std::vector<std::array<int32_t, 3>> interestCenters(const Game& game) const;
    bool nearPlayer(const Game& game, const int32_t pos[3], int32_t radius) const;
    // Host: cBaseCam::CanSee with another player's camera. The original's "on screen" means the one player's
    // screen; here it is anyone's, so nothing switches to its unseen behaviour (or appears) in front of them.
    bool seenByOthers(const Game& game, const int32_t pos[3], int32_t radius) const;
    int setPuppetView(Game& game, CtwPuppet puppet, const CtwView* view);
    int setRole(Game& game, int role);
    CtwVehicle vehicleHandle(const Game& game, uint32_t id) const;
    uint32_t vehicleId(const Game& game, CtwVehicle handle) const;
    int getVehicle(const Game& game, int index, CtwWorldVehicle* out) const;
    int setVehicle(Game& game, const CtwWorldVehicle* state, double received = -1.);
    void advanceVehicles(Game& game, double now);
    int removeVehicle(Game& game, uint32_t id);
    int adoptVehicle(Game& game, CtwVehicle local, uint32_t id);
    int getPed(const Game& game, int index, CtwWorldPed* out) const;
    int setPed(Game& game, const CtwWorldPed* state);
    int removePed(Game& game, uint32_t id);
    int takePedDamage(Game& game, uint32_t id);
    int damagePed(Game& game, uint32_t id, int amount);
    int setPlayerSeat(Game& game, CtwVehicle handle, int seat);
    void captureExplosion(int type, const int32_t* pos, float scale, const int32_t* velocity = nullptr);
    int takeExplosion(CtwWorldExplosion* out);
    int addExplosion(Game& game, const CtwWorldExplosion* state);
    void captureSkid(const Game& game, CtwVehicle vehicle, uint32_t wheel, uint32_t strip, uint32_t sequence, const int32_t* pos, const int32_t* dir, int hold);
    int takeSkid(CtwSkidPoint* out);
    int addSkid(Game& game, const CtwSkidPoint* point);
private:
    std::map<uint32_t, CameraView> views_;   // the other players' cameras, by puppet
    const std::vector<std::array<int32_t, 3>>& interestList(const Game& game) const;
    mutable std::vector<std::array<int32_t, 3>> interestCache_;
    mutable uint32_t interestFrame_ = 0;
    mutable size_t interestPedCount_ = 0;
    int role_ = CTW_MULTIPLAYER_OFF;
    bool replaying_ = false;
    std::map<uint32_t, CtwVehicle> vehicles_;
    struct Sample { double time; std::array<float,3> position, velocity; std::array<float,4> rotation; };
    std::map<CtwVehicle, std::deque<Sample>> motion_;
    std::map<uint32_t, CtwPuppet> peds_;
    std::deque<CtwWorldExplosion> explosions_;
    std::deque<CtwSkidPoint> skids_;
    std::map<std::pair<uint32_t, uint32_t>, CtwSkidPoint> skidSources_;
};
