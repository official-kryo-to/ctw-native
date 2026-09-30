// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <string>
bool WorldView_Init(const std::string& dataDir);
void WorldView_Enter();
void WorldView_Key(int scancode);
void WorldView_Update(float dt);            // camera movement + streaming (a few blocks per call)
void WorldView_Render();
void WorldView_SetCamera(float x, float y, float z, float yawDeg, float pitchDeg);
void WorldView_LoadAllNow();                // finish streaming synchronously (for screenshots)
void WorldView_SetTime(float hours);        // time of day, 0..24
void WorldView_Tick();                      // one 30 fps game frame (water, animations, clock)
