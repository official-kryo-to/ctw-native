// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Synthetic stand-ins for the exported sound and text tables (parity test only).
extern "C" {
unsigned char gEventInfo[156 * 16] = {1, 2, 3};
unsigned char gGears[20 * 48] = {4, 5};
unsigned char gCarCollisionEventsLow[12] = {6};
unsigned char gCarCollisionEventsMed[12] = {7};
unsigned char gCarCollisionEventsHigh[12] = {8};
unsigned char gPropSfx[57 * 4] = {11, 12, 13};
unsigned char TextColours[92] = {9, 10, 11};
}
