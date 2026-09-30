// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Synthetic stand-in for the table initializers of libGame.so (parity test only).
#include <new>
struct PopulationProfile { PopulationProfile(int,int,int,int,int,int,int,int,int,int,int,int); char d[20]; };
struct ZoneImpl { void SetPopulationZone(const char*, int, PopulationProfile*, int,int,int,int,int,int) const; };
struct cZoneManager {
    PopulationProfile* profiles; ZoneImpl* zone;
    void DefinePopProfiles();
    void SetupDefaultPopulation();
};
extern "C" void other_call(int);
void cZoneManager::DefinePopProfiles() {
    PopulationProfile* p = reinterpret_cast<PopulationProfile*>(this);
    new (p + 0) PopulationProfile(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    other_call(3);
    new (p + 1) PopulationProfile(20, 0, 255, 17, 33, 1, 2, 250, 0, 1, 0, 99);
    new (p + 2) PopulationProfile(5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5);
}
void cZoneManager::SetupDefaultPopulation() {
    PopulationProfile* p = reinterpret_cast<PopulationProfile*>(this);
    zone->SetPopulationZone("CHINA", 3, p + 1, 1, 7, 1000, 2, 700, 5);
    zone->SetPopulationZone("DOWNTWN", 12, p + 0, 4, 0, 1, 2, 3, 4);
    zone->SetPopulationZone("PORT", 7, p + 2, 65536, 1 << 20, 9, 9, 0, 1);
}
