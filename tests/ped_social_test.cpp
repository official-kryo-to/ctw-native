// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "random.h"
#include "sound.h"
#include "os/wad.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}

struct PedsTestAccess {
    static void sensor(Game& game,Pedestrians::Ped& ped){game.frame=(15-ped.uid)&15;game.peds.updateSocialSensor(game,ped);}
    static bool eligible(Game& game,const Pedestrians::Ped& ped){return game.peds.canTalk(ped);}
    static void start(Game& game,Pedestrians::Ped& ped){game.peds.startTalk(ped);}
    static bool tick(Game& game,Pedestrians::Ped& ped){return game.peds.talk(game,ped);}
    static void records(Game& game,bool friendOverride){
        game.peds.records_.assign(2,{});game.peds.firstRecord_.assign(20,0);
        auto& a=game.peds.records_[0];a.type=1;a.subtype=0;a.enemies[0]=2;a.friends[0]=friendOverride?2:0;
        auto& b=game.peds.records_[1];b.type=1;b.subtype=1;
    }
    static void clearMasks(Game& game){game.peds.records_.clear();game.peds.firstRecord_.clear();}
};

struct SoundTestAccess {
    static void run(Game& game,const std::filesystem::path& dir) {
        PedCommentTables data;
        data.samples[0][5]={17,6};data.samples[1][5]={31,6};
        data.timing[5].ownGap=100;data.timing[5].repeatGap=500;
        data.timing[5].randomGap=10;data.timing[5].peerGap=20;
        std::fill_n(data.recentSmall,2,-1);std::fill_n(data.recentLarge,4,-1);
        const auto path=dir/"ped_comments.bin";
        {std::ofstream file(path,std::ios::binary);file.write("CTWPEDV1",8);
         file.write((const char*)data.samples,sizeof data.samples);file.write((const char*)data.timing,sizeof data.timing);
         file.write((const char*)data.recentSmall,sizeof data.recentSmall);file.write((const char*)data.recentLarge,sizeof data.recentLarge);}
        Sound sound;sound.res_.data.resize(1);sound.tables_.events[60].mode=2;
        check(sound.commentTables_.load(path.string()),"synthetic speech ranges, cooldown records and repeat histories load");
        game.peds.peds.resize(3);
        for(int i=0;i<3;++i){auto& p=game.peds.peds[i];p={};p.uid=i+1;p.type=1;p.body.bodySet=i==1?0:1;}
        RandInit(24);const int sample=17+Rand16NonCritical(6);RandInit(24);
        check(sound.conversationAt(game,1,1000),"a permitted speaker queues its native conversation event");
        const auto& slot=sound.people_.at(1).s[0];
        check(slot.event==60 && slot.sfx==sample && slot.volume==70 && slot.radius==600,
              "speech uses its gender range, noncritical sample roll and original positional event parameters");
        check(sound.commentTables_.timing[5].nextAllowed>=1020 && sound.commentTables_.timing[5].nextAllowed<1030,
              "accepted speech applies the extracted global fixed and random cooldown");
        check(!sound.conversationAt(game,2,1020),"other speakers respect the strict global cooldown boundary");
        sound.commentTables_.timing[5].nextAllowed=0;
        check(sound.conversationAt(game,2,1000) && sound.people_.at(2).s[0].sfx>=31 && sound.people_.at(2).s[0].sfx<37,
              "female speech selects the independent female sample group");
        sound.commentTables_.timing[5].nextAllowed=0;sound.commentTables_.samples[0][5]={41,6};
        check(!sound.conversationAt(game,3,1000),"a third speaker cannot exceed the native two-comment reservation pool");
        sound.comments_[0].active=sound.comments_[1].active=false;
        std::fill_n(sound.commentTables_.recentLarge,4,-1);
        check(!sound.conversationAt(game,1,1500),"an actor's repeat cooldown includes its exact time boundary");
        check(sound.conversationAt(game,1,1501),"the same actor can speak immediately after its repeat cooldown expires");
        sound.comments_[0].active=sound.comments_[1].active=false;
        sound.commentHistory_.clear();sound.commentTables_=data;sound.commentTables_.loaded=true;
        sound.commentTables_.samples[0][5]={55,1};
        check(sound.conversationAt(game,1,2000),"a small sample group can queue a first line");
        sound.commentTables_.timing[5].nextAllowed=0;
        check(!sound.conversationAt(game,3,2000),"the two-entry small-group history prevents immediate repeats across actors");
        game.peds.peds[0].vehicle=8;
        check(!sound.conversationAt(game,1,5000),"on-foot conversation lines are rejected for seated occupants");
        game.peds.peds.clear();sound.updatePedComments(game);
        check(sound.people_.empty() && !sound.comments_[0].active && !sound.comments_[1].active,
              "destroyed actors release their sound slots, history and both comment reservations safely");
        {std::ofstream file(path,std::ios::binary);file.write("CTWPEDV1",8);}
        check(!sound.commentTables_.load(path.string()) && !sound.commentTables_.loaded,
              "truncated speech data cannot leave a previous valid sample table active");
        std::filesystem::remove(path);
    }
};

static void scene(Game& game){
    game.cars.clear();game.peds.clear();game.peds.peds.resize(2);PedsTestAccess::clearMasks(game);
    for(int i=0;i<2;++i){auto& p=game.peds.peds[i];p.uid=i+1;p.body.playerControlled=false;p.type=1;
        p.body.pos[0]=0;p.body.pos[1]=i*8*4096;p.body.pos[2]=0;}
}

int main(){
    SDL_Init(0);auto& game=TheGame();scene(game);
    auto& source=game.peds.peds[0];auto& other=game.peds.peds[1];
    PedsTestAccess::sensor(game,source);
    check(source.socialFriend==other.uid && !source.socialFriendVehicle,"the sensor caches a visible neutral pedestrian in its forward cone");
    source.body.setHeading((int16_t)0x8000);PedsTestAccess::sensor(game,source);
    check(source.socialFriend==other.uid,"an existing live friend remains cached behind the sensor until it leaves range");
    source.socialFriend=0;PedsTestAccess::sensor(game,source);
    check(source.socialFriend==0,"a new friend behind the native sixty-degree half cone is rejected");
    source.body.setHeading(0);other.body.pos[1]=43*4096;PedsTestAccess::sensor(game,source);
    check(source.socialFriend==0,"new friends outside the pulled-back forty-unit sensor are rejected");
    other.body.pos[1]=8*4096;other.vehicle=99;PedsTestAccess::sensor(game,source);
    check(source.socialFriend==0,"seated pedestrians are not inserted by the on-foot friend scan");
    other.vehicle=0;other.subtype=1;PedsTestAccess::records(game,false);PedsTestAccess::sensor(game,source);
    check(source.socialFriend==0,"the native record-index enemy mask excludes conversation candidates");
    PedsTestAccess::records(game,true);PedsTestAccess::sensor(game,source);
    check(source.socialFriend==other.uid,"an explicit friend bit takes precedence over an enemy bit");
    other.body.dead=true;PedsTestAccess::sensor(game,source);
    check(source.socialFriend==0,"a dead cached friend is released");other.body.dead=false;
    PedsTestAccess::clearMasks(game);
    VehicleInfo info{};const int32_t at[3]={0,4*4096,0};
    Vehicle car;car.init(info,0,at,0,0);car.uid=other.uid;game.cars.push_back(car);
    PedsTestAccess::sensor(game,source);
    check(source.socialFriend==other.uid && !source.socialFriendVehicle,
          "a closer neutral vehicle cannot replace an existing pedestrian friend, even with the same UID");
    other.body.pos[1]=100*4096;PedsTestAccess::sensor(game,source);
    check(source.socialFriend==car.uid && source.socialFriendVehicle,"the friend cache distinguishes the native vehicle candidate kind");

    scene(game);auto& master=game.peds.peds[0];auto& slave=game.peds.peds[1];
    master.socialFriend=slave.uid;
    check(PedsTestAccess::eligible(game,master),"an ordinary available civilian can initiate a master/slave task");
    master.ambientDisabled=true;check(!PedsTestAccess::eligible(game,master),"ambient disabling prevents conversations");master.ambientDisabled=false;
    master.type=10;check(!PedsTestAccess::eligible(game,master),"nonambient police cannot enter the civilian conversation task");master.type=1;
    RandInit(12);const int expected=Rand16Critical(3)+1;RandInit(12);
    PedsTestAccess::start(game,master);
    check(master.talk.limit==expected && master.talkingCooldown==840,"starting a conversation retains the native round count and 840-tick cooldown");
    check(!PedsTestAccess::eligible(game,master),"an actor in a master task cannot also be selected as a new slave");
    PedsTestAccess::tick(game,master);
    check(master.talk.state==1 && slave.talk.state==20 && slave.talk.partner==master.uid,
          "the initiating task connects a reciprocal slave before requesting its approach");
    PedsTestAccess::tick(game,slave);
    check(slave.talk.approachStarted && slave.talkingCooldown==840,"the slave starts its own cooldown when it creates the approach task");
    slave.body.pos[0]=2*4096;slave.body.pos[1]=0;
    PedsTestAccess::tick(game,slave);
    check(slave.talk.state==21 && master.talk.state==10 && master.talk.timer==61,
          "reaching the master's local two-unit right offset signals readiness and starts the first speech turn");
    PedsTestAccess::tick(game,master);
    check(master.talk.timer==60 && master.talk.exchanges==1 && slave.talk.state==11,
          "the master's first speaking tick sends the listening state and increments its exchange count once");
    PedsTestAccess::tick(game,master);
    check(master.talk.idle && master.talk.idleTimer==59,"the following master tick creates the original timed gesture child");
    master.talk.timer=0;master.talk.idle=false;
    PedsTestAccess::tick(game,master);
    check(master.talk.state==11 && slave.talk.state==10,"the zero master timer hands speech to the responder");
    PedsTestAccess::tick(game,slave);
    check(slave.talk.timer==59 && slave.talk.idleTimer==59,"the responder creates its gesture after its first native timer decrement");
    for(int i=0;i<59;++i)PedsTestAccess::tick(game,slave);
    check(slave.talk.timer==59 && !slave.talk.idle,
          "the responder's outer timer pauses while its child task runs, preserving native task ordering");
    PedsTestAccess::tick(game,slave);
    check(slave.talk.timer==58 && slave.talk.idleTimer==58,"the responder resumes its outer state only after the child is removed");
    slave.talk.idle=false;slave.talk.timer=0;master.talk.exchanges=master.talk.limit;
    PedsTestAccess::tick(game,slave);
    check(master.talk.state==12,"the last successful responder state requests conversation completion");
    PedsTestAccess::tick(game,master);PedsTestAccess::tick(game,slave);
    check(master.talk.role==Pedestrians::Ped::Talk::None && slave.talk.role==Pedestrians::Ped::Talk::None &&
          master.wander.phase==Pedestrians::Ped::Wander::Rejoin,"both tasks disconnect and the initiator rejoins the pavement network");
    master.talkingCooldown=0;master.socialFriend=slave.uid;slave.talkingCooldown=0;
    PedsTestAccess::start(game,master);PedsTestAccess::tick(game,master);
    game.peds.peds.erase(game.peds.peds.begin());
    PedsTestAccess::tick(game,game.peds.peds[0]);
    check(game.peds.peds[0].talk.role==Pedestrians::Ped::Talk::None,"removing a partner safely releases the surviving task's stable reference");

    const auto dir=std::filesystem::temp_directory_path()/"ctw-social-pedinfo";
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> bytes(180,0);auto word=[&](int offset,uint32_t value){std::memcpy(bytes.data()+offset,&value,4);};
    word(4,116);word(116,88);word(120,95);word(148,102);word(152,109);
    bytes[88]=2;bytes[95]=1;bytes[102]=4;bytes[109]=8;
    bytes[116+13]=bytes[148+13]=1;bytes[148+14]=1;
    {std::ofstream file(dir/"rom.wad",std::ios::binary);file.write((const char*)bytes.data(),bytes.size());}
    {const uint32_t entry[3]={Wad::hashName("pedinfo.bin"),0,(uint32_t)bytes.size()};
     std::ofstream file(dir/"rom.toc",std::ios::binary);file.write((const char*)entry,sizeof entry);}
    check(game.peds.init(dir.string()) && game.peds.record(1,0)->enemies[0]==2 && game.peds.record(1,0)->friends[0]==1 &&
          game.peds.record(1,1)->enemies[0]==4 && game.peds.record(1,1)->friends[0]==8,
          "pedinfo relationship pointers load their native seven-byte masks independently for each subtype");
    SoundTestAccess::run(game,dir);
    std::filesystem::remove(dir/"rom.wad");std::filesystem::remove(dir/"rom.toc");
    std::filesystem::remove(dir/"pedinfo.bin");std::filesystem::remove(dir);
    SDL_Quit();return failures?1:0;
}
