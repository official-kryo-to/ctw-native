// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <string>
bool ModelView_Init(const std::string& dataDir);
void ModelView_Enter();
void ModelView_Key(int scancode);
void ModelView_Update();          // per frame: mouse drag / wheel
void ModelView_Render();
void ModelView_Select(int resourceId);
void ModelView_SetCamera(float yawDeg, float pitchDeg);
