#pragma once
#include "ChronoRules.h"
namespace Game::Chrono {
enum class CreatureAttack { Sweep, Charge, Slam, Wing };
inline float AttackEase(float x){x=std::clamp(x,0.f,1.f);return x*x*x*(x*(x*6-15)+10);}
inline float WarningTime(CreatureAttack a){return a==CreatureAttack::Slam?1.5f:1.2f;}
inline float ActiveTime(CreatureAttack a){return a==CreatureAttack::Slam?.24f:a==CreatureAttack::Wing?.38f:.28f;}
// Coordinates are relative to a snapshot made at the START of the warning.
inline Vec AttackHead(CreatureAttack a,int phase,float time){
    float u=AttackEase(time/ActiveTime(a));if(phase==1)u=0;if(phase>=3)u=1;
    if(a==CreatureAttack::Charge)return {0,11,22-38*u};
    if(a==CreatureAttack::Slam)return {0,38-29*u,3};
    return {-12+24*u,13,5};
}
inline bool AttackTouches(CreatureAttack a,Vec p,float before,float now){
    float lo=AttackEase(before/ActiveTime(a)),hi=AttackEase(now/ActiveTime(a));
    if(a==CreatureAttack::Sweep)return p.x>=-15+24*lo&&p.x<=-9+24*hi&&std::abs(p.z)<5&&p.y<4.5f;
    if(a==CreatureAttack::Charge)return std::abs(p.x)<4&&p.z>=18-38*hi&&p.z<=26-38*lo&&p.y<7;
    if(a==CreatureAttack::Slam)return now>=ActiveTime(a)&&p.x*p.x+p.z*p.z<64&&p.y<32;
    return p.x>=-16+28*lo&&p.x<=-10+28*hi&&std::abs(p.z)<7&&std::abs(p.y)<6;
}
}
