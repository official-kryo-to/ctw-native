// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Exercise production WAD/table loading with synthetic geometry and definitions, never game lookup data.
#include "zones.h"
#include "traffic.h"
#include "os/wad.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>

namespace {
int failures;
void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
using Bytes = std::vector<uint8_t>;
void append(Bytes& bytes, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data); bytes.insert(bytes.end(), p, p + size);
}
template<class T> void append(Bytes& bytes, const T& value) { append(bytes, &value, sizeof value); }
void save(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary); file.write((const char*)bytes.data(), bytes.size());
}
void name(char out[8], const char* value) { std::memset(out, 0, 8); std::memcpy(out, value, std::min(size_t(8), std::strlen(value))); }
void rectangle(Bytes& bytes, const char* key, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    char text[8]; name(text, key); append(bytes, text, 8);
    append(bytes, x0); append(bytes, y0); append(bytes, x1); append(bytes, y1);
}
int32_t q(int grid) { return grid * 5 * 4096; }

struct Fixture {
    std::filesystem::path dir;
    Bytes navTable, populationTable;
    std::map<std::string, Bytes> files;
    Fixture() {
        dir = std::filesystem::temp_directory_path() /
              ("ctw-zones-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        Bytes geometry;
        rectangle(geometry, "A", -20,-20,20,20); rectangle(geometry, "B", 10,-20,30,20);
        rectangle(geometry, "A", 40,-20,50,20); rectangle(geometry, "UNKNOWN", 60,-20,70,20);
        rectangle(geometry, "NEG", -50,-40,-30,-20);
        for (int i = 0; i < 7; ++i) rectangle(geometry, ("CACHE" + std::to_string(i)).c_str(),
                                             (int16_t)(80 + i*10), -20, (int16_t)(90+i*10), 20);
        files["navzones.bin"] = files["infozones.bin"] = geometry;
        rectangle(files["islzones.bin"], "ONE", -20,-20,0,20);
        rectangle(files["islzones.bin"], "TWO", 0,-20,20,20);
        rectangle(files["islzones.bin"], "THREE", 20,-20,40,20);
        rectangle(files["islzones.bin"], "THREE", 40,-20,50,20);
        append(navTable, "CTWNAV1", 8); const uint32_t counts[] = {4, 3}; append(navTable, counts, sizeof counts);
        for (const auto& row : std::vector<NavigationSetup>{{{},501,2,1},{{},502,0,0},{{},503,3,0},{{},777,1,1}}) append(navTable, row);
        const char* keys[] = {"A","B","NEG","A"};
        for (int i = 0; i < 4; ++i) name((char*)navTable.data()+16+i*12,keys[i]);
        for (int i = 0; i < 3; ++i) { IslandSetup row{}; name(row.name, i == 0 ? "ONE" : i == 1 ? "TWO" : "THREE"); row.island=i; append(navTable,row); }
        append(populationTable, "CTWZONE1", 8); const uint32_t popCounts[] = {3,2}; append(populationTable,popCounts,sizeof popCounts);
        ZoneSetup a{}; name(a.name,"A"); a.pedMakeup=4; a.vehMakeup=2; a.pedDensity=150; a.carDensity=200; a.sex=1; a.flag=1;
        ZoneSetup night=a; night.night=night.profile=1; night.pedMakeup=5; night.vehMakeup=3; night.pedDensity=40; night.carDensity=10; night.sex=2;
        ZoneSetup b=night; name(b.name,"B"); b.night=0;
        append(populationTable,a); append(populationTable,night); append(populationTable,b);
        const int32_t profiles[2][12] = {{255,2,3,4,5,6,7,8,9,10,11,12},{13,14,15,16,17,18,19,20,21,22,23,24}};
        append(populationTable,profiles,sizeof profiles);
        Bytes popinfo(14 + 8*8); const uint32_t offsets[] = {14,14,14}; std::memcpy(popinfo.data(),offsets,sizeof offsets);
        files["popinfo.bin"] = popinfo;
        write();
    }
    ~Fixture() { std::filesystem::remove_all(dir); }
    void write() {
        struct Entry { uint32_t hash, offset, size; };
        Bytes wad; std::vector<Entry> entries;
        for (const auto& [key,value] : files) {
            entries.push_back({Wad::hashName(key.c_str()), (uint32_t)wad.size(), (uint32_t)value.size()});
            append(wad,value.data(),value.size());
        }
        std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.hash<b.hash;});
        Bytes toc; append(toc,entries.data(),entries.size()*sizeof(Entry));
        save(dir/"rom.wad",wad); save(dir/"rom.toc",toc);
        save(dir/"navigation_tables.bin",navTable); save(dir/"population_tables.bin",populationTable);
    }
};

void behavior(Fixture& f) {
    ZoneManager zones;
    check(zones.init(f.dir.string()),"production zone startup loads all three synthetic WAD geometries and both setup tables");
    check(zones.textKey(q(15),0)==777,"a fresh overlapping query uses file order and the last matching setup definition");
    check(zones.textKey(q(25),0)==502 && zones.textKey(q(15),0)==502,
          "native recent-zone history wins when returning to an overlap");
    for (int i=0;i<7;++i) zones.textKey(q(85+i*10),0);
    check(zones.textKey(q(15),0)==777,"evicting all six native history slots restores the file-order overlap result");
    check(zones.textKey(q(45),0)==777 && zones.affluenceAt(q(45),0)==1 && zones.populationBleedDisallowedAt(q(45),0),
          "navigation text, affluence and population-bleed metadata reaches every repeated-name rectangle");
    check(zones.textKey(q(65),0)==143 && zones.affluenceAt(q(65),0)==0 && !zones.populationBleedDisallowedAt(q(65),0),
          "unconfigured navigation keeps the native default text and deterministic zero metadata");
    check(zones.islandAt(q(-10),0)==0 && zones.islandAt(q(10),0)==1 && zones.islandAt(q(30),0)==2 && zones.islandAt(q(45),0)==2,
          "all island definitions including the final island apply across repeated geometry names");
    const auto* day=zones.populationAt(q(5),0,false); const auto* night=zones.populationAt(q(45),0,true);
    const int fields[12]={1,10,0,3,5,19,7,11,12,8,15,18};
    check(day && night && day->fields[1]==127 && night->fields[1]==13 && day->pedMakeup==4 && night->pedMakeup==5 &&
          day->vehMakeup==2 && night->vehMakeup==3 && day->pedDensity==100 && day->carDensity==100 &&
          night->pedDensity==40 && night->carDensity==10 && day->sex==1 && night->sex==2 && day->flag && night->flag,
          "profile construction, day/night data, density clamps and shared names reach real population queries");
    for(int i=1;i<12;++i) check(day && day->fields[fields[i]]==i+1 && night && night->fields[fields[i]]==i+13,
                                "each extracted constructor weight maps to its original seven-bit population field");
    check(zones.populationAt(q(45),0,false)==day,"repeated population rectangles share a single day info object");
    const auto& def=zones.populationOrDefault(q(1000),0,false);
    check(!zones.populationAt(q(1000),0,false) && def.fields[1]==10 && def.vehMakeup==5 && def.pedDensity==25 && def.carDensity==25,
          "out-of-zone population keeps the native civilian default");
    check(zones.populationName(q(45),0)=="A" && zones.populationName(q(1000),0).empty(),"population names use the production geometry lookup");
    check(zones.textKey(q(-50),q(-30))==0 && zones.textKey(q(-50)+1,q(-30))==0 &&
          zones.textKey(q(-50)+4096,q(-30))==503 && zones.textKey(q(-30),q(-30))==503 &&
          zones.textKey(q(-30)+4095,q(-30))==503 && zones.textKey(q(-30)+4096,q(-30))==0,
          "negative X boundaries retain native floor-then-truncate arithmetic with exclusive minima and inclusive maxima");
    check(zones.textKey(q(-40),q(-40))==0 && zones.textKey(q(-40),q(-40)+4096)==503 &&
          zones.textKey(q(-40),q(-20)+4095)==503 && zones.textKey(q(-40),q(-20)+4096)==0,
          "negative Y boundaries retain the same native quantization and strict edges");
    check(zones.textKey(std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max())==0 &&
          zones.islandAt(q(1000),0)==-1 && zones.affluenceAt(q(1000),0)==1 && zones.populationBleedDisallowedAt(q(1000),0),
          "extreme coordinates and no-zone navigation/island queries have the native safe fallbacks");
    check(zones.init(f.dir.string()) && zones.textKey(q(15),0)==777,"reinitialization clears every geometry history cache");
    Traffic traffic;
    check(traffic.init(f.dir.string()) && traffic.ok() && traffic.zoneAt(q(5),0,false)->fields[1]==127 &&
          traffic.zoneName(q(45),0)=="A" && traffic.zones().islandAt(q(30),0)==2,
          "Traffic initializes and uses the same zone manager consumed by game population");
}

void invalid(Fixture& f) {
    const Bytes good=f.navTable;
    ZoneManager zones;
    for (int kind=0;kind<7;++kind) {
        f.navTable=good; f.write();
        check(zones.init(f.dir.string()) && zones.textKey(q(5),0)==777,
              "valid navigation is present before testing failed-reinitialization cleanup");
        if(kind==0) f.navTable[0]='?';
        if(kind==1) f.navTable.pop_back();
        if(kind==2) f.navTable.push_back(0);
        if(kind==3) f.navTable[8]=0xFF;
        if(kind==4) f.navTable[16+10]=4;
        if(kind==5) f.navTable[16+11]=2;
        if(kind==6) f.navTable[16+4*12+9]=1;  // island id 256
        f.write();
        check(!zones.init(f.dir.string()) && zones.textKey(q(5),0)==0 && !zones.populationAt(q(5),0,false),
              "malformed, truncated, oversized or out-of-range navigation tables reject startup and clear stale zones");
    }
    f.navTable=good; f.write();
    check(zones.init(f.dir.string()),"valid zone data recovers after a failed initialization");
    auto geometry=f.files["navzones.bin"];
    f.files["navzones.bin"].push_back(0xAA); f.write();
    check(zones.init(f.dir.string()) && zones.textKey(q(5),0)==777,"geometry ignores a trailing partial record as the original loader does");
    f.files["navzones.bin"]=geometry;
    const int16_t reversed=30; std::memcpy(f.files["navzones.bin"].data()+8,&reversed,2); f.write();
    check(!zones.init(f.dir.string()) && zones.textKey(q(5),0)==0,"reversed geometry bounds reject the whole initialization");
    f.files["navzones.bin"]=geometry; f.write();
    std::filesystem::remove(f.dir/"navigation_tables.bin");
    check(!zones.init(f.dir.string()) && zones.islandAt(0,0)==-1,"a missing new setup table never leaves stale navigation state");
}
}
int main(int argc, char** argv) {
    Fixture fixture; behavior(fixture); invalid(fixture);
    if (argc == 2) {
        ZoneManager original;
        check(original.init(argv[1]), "the user's extracted original navigation, island and population data initializes");
    }
    return failures?1:0;
}
