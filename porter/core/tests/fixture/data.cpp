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
unsigned char gDrivingProps[1030] = {3, 4, 5, 6};
unsigned short stuckReverse1[4] = {100, 110, 120, 130};
unsigned short stuckReverse2[4] = {30, 40, 50, 60};
int stuckReverseSteer[10][2] = {{4096, -4096}, {2048, 0}};
}
// cCopProperties' tables are C++ members: give the stand-ins their mangled names.
unsigned char cop0[28] __asm__("_ZN14cCopProperties19mTakeDownsPerCopCarE") = {1};
unsigned char cop1[28] __asm__("_ZN14cCopProperties35mRatingToSubtractOnReachingMaxLevelE") = {2};
unsigned char cop2[28] __asm__("_ZN14cCopProperties15mTakeDownHealthE") = {3};
unsigned char cop3[28] __asm__("_ZN14cCopProperties24mCopMaximumCarsWhenInCarE") = {4};
unsigned char cop4[28] __asm__("_ZN14cCopProperties25mCopMaximumCarsWhenOnFootE") = {5};
unsigned char cop5[28] __asm__("_ZN14cCopProperties26mCopMultiplierTimes10InCarE") = {6};
unsigned char cop6[28] __asm__("_ZN14cCopProperties27mCopMultiplierTimes10OnFootE") = {7};
unsigned char cop7[28] __asm__("_ZN14cCopProperties19mPercentageRamBlockE") = {8};
unsigned char cop8[28] __asm__("_ZN14cCopProperties24mnChanceOfFlipPercentageE") = {9};
unsigned char cop9[28] __asm__("_ZN14cCopProperties16mTargetSpeedMultE") = {10};
unsigned char cop10[28] __asm__("_ZN14cCopProperties27mProbationaryHidePercentageE") = {11};
unsigned char cop11[28] __asm__("_ZN14cCopProperties41mCarToPlayerOnFootArrivalOffsetMultiplierE") = {12};
int copFailedProbation __asm__("_ZN14cCopProperties38mFailedProbationFramesPerMinusTakedownE") = 2048;
