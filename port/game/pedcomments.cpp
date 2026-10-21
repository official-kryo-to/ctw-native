// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "sound.h"
#include "game.h"
#include "random.h"
#include "audio/audio.h"
#include "os/os.h"
#include <algorithm>

void Sound::conversation(Game& game,uint32_t uid) {
    conversationAt(game,uid,(uint32_t)(OS_TimeAccurate()*1000.0));
}

bool Sound::conversationAt(Game& game,uint32_t uid,uint32_t now) {
    // Comment-type-5 branches of 0x6b40b8 / 0x6b45a8. Other comment remapping/eligibility is not implemented here.
    if(!ok() || !commentTables_.loaded)return false;
    const Pedestrians::Ped* speaker=nullptr;
    for(const auto& ped:game.peds.peds)if(ped.uid==uid){speaker=&ped;break;}
    if(!speaker || speaker->body.dead || speaker->body.hidden || speaker->vehicle || (speaker->type|4)==6)return false;
    constexpr int type=5;
    auto& timing=commentTables_.timing[type];
    const auto& range=commentTables_.samples[speaker->body.bodySet==0][type];
    auto& history=commentHistory_[uid];
    const uint64_t since=(uint64_t)now-history.time;
    if(timing.nextAllowed>=now || since<=timing.ownGap || (history.type==type && since<=timing.repeatGap) || range.count<=0)return false;
    const int sample=range.first+(uint16_t)Rand16NonCritical(range.count);
    const int32_t* previous=range.count<5?commentTables_.recentSmall:commentTables_.recentLarge;
    const int count=range.count<5?2:4;
    if(std::find(previous,previous+count,sample)!=previous+count)return false;
    for(const auto& voice:comments_)if(voice.active && voice.sample==sample)return false;
    CommentVoice* voice=nullptr;
    for(auto& candidate:comments_)if(!candidate.active){voice=&candidate;break;}
    if(!voice)return false;
    auto& entity=people_[uid];std::copy_n(speaker->body.pos,3,entity.pos);
    const int slot=addEvent(entity,60,70,600,sample); // assembly at 0x6b42a0 / 0x6b42f4
    if(slot<0)return false;
    *voice={true,uid,slot,sample,(uint64_t)now+2000};
    timing.nextAllowed=(uint64_t)now+timing.peerGap+Rand32NonCritical(timing.randomGap);
    if(range.count>4) {
        largeCommentCursor_=largeCommentCursor_==3?0:largeCommentCursor_+1;
        commentTables_.recentLarge[largeCommentCursor_]=sample;
    } else {
        smallCommentCursor_=smallCommentCursor_==1?0:smallCommentCursor_+1;
        commentTables_.recentSmall[smallCommentCursor_]=sample;
    }
    history={type,now};return true;
}

void Sound::updatePedComments(Game& game) {
    const uint32_t now=(uint32_t)(OS_TimeAccurate()*1000.0);
    for(auto it=people_.begin();it!=people_.end();) {
        const Pedestrians::Ped* speaker=nullptr;
        for(const auto& ped:game.peds.peds)if(ped.uid==it->first){speaker=&ped;break;}
        if(!speaker) {
            stopSlots(it->second);commentHistory_.erase(it->first);it=people_.erase(it);continue;
        }
        std::copy_n(speaker->body.pos,3,it->second.pos);processEntity(game,it->second,false);++it;
    }
    for(auto& voice:comments_)if(voice.active) {
        auto entity=people_.find(voice.uid);
        if(entity==people_.end()) {voice.active=false;continue;}
        auto& slot=entity->second.s[voice.slot];
        if(slot.event==0x9C)voice.active=false;
        else if(slot.voice && voice.deadline<now) {
            // Native 0x6b3f14 releases the comment reservation; the sample itself may finish naturally.
            voice.active=false;
        }
    }
}
