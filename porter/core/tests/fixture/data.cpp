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
unsigned char gHornPatterns[80] = {1, 0, 9, 0, 0, 0, 3, 0, 2};
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

// Gui::ShardEffect1..9: shard sets whose records point at vertex arrays (relocated pointers).
namespace Gui {
struct ShardRecord { const short* start; const short* end; int count; };
struct ShardSet { int count; const ShardRecord* records; };
static const short shardA[12] = {0, 0, 0, 0, 512, 0, 1024, 0, 0, 384, 0, 768};
static const short shardB[12] = {-10, -20, 0, 0, 500, -30, 0, 0, -5, 400, 0, 0};
static const short shardC[8] = {512, 0, 1024, 0, 1024, 768, 2048, 1536};
static const short shardD[8] = {530, -40, 0, 0, 1100, 800, 0, 0};
static const ShardRecord shardRecords[2] = {{shardA, shardB, 3}, {shardC, shardD, 2}};
ShardSet ShardEffect1 = {2, shardRecords};
ShardSet ShardEffect2 = {1, shardRecords + 1};
ShardSet ShardEffect3 = {2, shardRecords};
ShardSet ShardEffect4 = {1, shardRecords};
ShardSet ShardEffect5 = {2, shardRecords};
ShardSet ShardEffect6 = {1, shardRecords + 1};
ShardSet ShardEffect7 = {2, shardRecords};
ShardSet ShardEffect8 = {1, shardRecords};
ShardSet ShardEffect9 = {2, shardRecords};
}
