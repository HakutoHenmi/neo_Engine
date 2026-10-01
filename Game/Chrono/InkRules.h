#pragma once
#include "ChronoRules.h"
#include "DomainRules.h"
#include "SwarmRules.h"
#include "RogueliteRules.h"
#include <array>
#include <cstdint>
namespace Game::Chrono {
inline constexpr float InkGravity=18.f, InkShotSpeed=58.f;
inline Vec InkLaunch(Vec delta){
    float horizontal=std::sqrt(delta.x*delta.x+delta.z*delta.z),v2=InkShotSpeed*InkShotSpeed;
    float discriminant=v2*v2-InkGravity*(InkGravity*horizontal*horizontal+2*delta.y*v2);
    if(horizontal<.01f||discriminant<0)return Unit(delta)*InkShotSpeed;
    float tangent=(v2-std::sqrt(discriminant))/(InkGravity*horizontal);
    float planar=InkShotSpeed/std::sqrt(1+tangent*tangent);
    return {delta.x/horizontal*planar,tangent*planar,delta.z/horizontal*planar};
}
inline float InkBossSupport(Vec,float){return 0.f;}
// Persistent UV coverage is the authority for both shading and movement.
struct InkSurface {
    Vec origin{},u{1,0,0},v{0,0,1}; float width=1,depth=1;
    static constexpr int Resolution=256;
    std::array<uint8_t,Resolution*Resolution> mask{};
    Vec Normal()const {return Unit(Vec{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x})*-1;}
    bool UV(Vec p,float& x,float& y)const {Vec d=p-origin;x=Dot(d,u)/width;y=Dot(d,v)/depth;return x>=0&&x<=1&&y>=0&&y<=1;}
    bool Ray(Vec from,Vec delta,float& fraction,Vec& point)const {
        Vec n=Normal();float den=Dot(delta,n);if(den>=-.00001f)return false;
        fraction=Dot(origin-from,n)/den;if(fraction<0||fraction>1)return false;
        point=from+delta*fraction;float x,y;return UV(point,x,y);
    }
    bool Painted(Vec p)const {float x,y;if(!UV(p,x,y)||std::abs(Dot(p-origin,Normal()))>.3f)return false;
        int ix=std::clamp(int(x*Resolution),0,Resolution-1),iy=std::clamp(int(y*Resolution),0,Resolution-1);
        return mask[iy*Resolution+ix]>=128;}
    void Stamp(Vec p,float radius,const std::array<float,32>* footprint=nullptr,float footprintScale=1){float x,y;if(!UV(p,x,y))return;
        int x0=std::max(0,int((x-radius*1.25f/width)*Resolution)),x1=std::min(Resolution-1,int((x+radius*1.25f/width)*Resolution));
        int y0=std::max(0,int((y-radius*1.25f/depth)*Resolution)),y1=std::min(Resolution-1,int((y+radius*1.25f/depth)*Resolution));
        for(int j=y0;j<=y1;++j)for(int i=x0;i<=x1;++i){float dx=((i+.5f)/Resolution-x)*width,dy=((j+.5f)/Resolution-y)*depth;
            float a=std::atan2(dy,dx),edge=radius;
            if(footprint){float bin=(a+3.14159265f)*32/6.2831853f;int k=int(bin)%32;
                edge=((*footprint)[k]*(1-(bin-int(bin)))+(*footprint)[(k+1)%32]*(bin-int(bin)))*footprintScale;}
            else edge*=1+.13f*std::sin(a*7+x*31)+.08f*std::sin(a*11+y*27);
            float coverage=std::clamp((edge-std::sqrt(dx*dx+dy*dy))/.28f+.5f,0.f,1.f);
            auto& m=mask[j*Resolution+i];m=std::max(m,static_cast<uint8_t>(coverage*255));}
    }
};
inline constexpr float SlimeMaximumMass=800.f, SlimeMinimumMass=40.f;
inline constexpr float SlimeChargeCapacity=SlimeMaximumMass-SlimeMinimumMass;
inline constexpr float SlimeBeamRange=700.f;
inline constexpr float SlimeDodgeInvincibility=.45f,SlimePerfectInvincibility=.95f;
inline float SlimePerfectPulse(float age){
    if(age<0||age>=.4f)return 0;
    return age<.06f?age/.06f:1-(age-.06f)/.34f;
}
// Continuous support follows walkable slopes both up and down, but never snaps a jump to ground.
inline bool SlimeGrounded(float oldY,float nextY,float verticalSpeed,bool grounded,bool jumped,float floor,float nextFloor,float travel){
    bool follow=grounded&&!jumped&&std::abs(nextFloor-floor)<=.15f+travel*.65f;
    return follow||(verticalSpeed<=0&&nextY<=nextFloor+1.25f&&nextFloor<=oldY+.1f);
}
inline constexpr float SlimeRed=.4f,SlimeGreen=.8f,SlimeBlue=.1f;
enum class SlimePhase { Roaming, Charging, Firing, Returning };
struct SlimeDissolvable { Vec center{},half{2,2,2};float integrity=30; };
struct SlimeFeather { Vec velocity{};float life=6; };
// A rounded rectangular flight path beyond the expanded arena rim.
inline Vec SlimeFlightPoint(float angle){
    float x=std::sin(angle),z=std::cos(angle);
    float radius=1/std::max(std::abs(x)/270.f,std::abs(z)/290.f);
    return {x*radius,64.f+std::sin(angle*2)*7,20+z*radius};
}
inline float SlimeReserve(float total){return std::min(SlimeMinimumMass,std::max(1.f,total*.05f));}
inline float SlimeCapacity(float total){return std::max(.01f,total-SlimeReserve(total));}
inline int SlimeTier(float charge,float capacity=SlimeChargeCapacity){return charge<=0?0:charge/capacity>=.75f?3:charge/capacity>=.35f?2:1;}
inline float SlimeDeposit(float& body,float distance,float rate,float reserve=SlimeMinimumMass){
    float amount=std::min(std::max(0.f,body-reserve),std::max(0.f,distance)*rate);
    body-=amount;return amount;
}
inline float SlimeMono(float age){return age<0||age>=.2f?0:age<.03f?age/.03f:age<.08f?1:1-(age-.08f)/.12f;}
inline float SlimeActiveStep(float age,float duration,float dt){return std::min(dt,std::max(0.f,duration-age));}
struct InkPlayer {
    Roguelite rogue;
    float rogueBossCorrosion=0,rogueBossBurn=0,rogueBossSlow=0;bool rogueBossPool=false;
    bool hordeEnabled=true;
    BattlePhase battlePhase=BattlePhase::Horde;
    int swarmKills=0,swarmAlive=0,swarmCombo=0,swarmBestCombo=0,swarmSpawned=0,swarmDetonations=0,swarmStrikes=0,swarmActiveAttacks=0;
    float battleAge=0,swarmComboAge=0;
    DomainPath domainPath;
    int domainVolleys=0,domainQueued=0,domainFlying=0,domainLastCount=0;
    int domainLastEnclosures=0;
    float domainLastArea=0,domainLastLength=0;
    float cameraDomainView=0;
    DomainShape domainLastShape=DomainShape::Loop;
    std::array<int,4> domainSkillUses{};
    // charge is a credit on mass already inside the body, never extra HP.
    float tank=0,shotClock=0,coreHealth=100,hitFlash=0;
    float charge=0,spent=0,deployed=0,phaseAge=0,beamCharge=0,beamLength=0,beamRadius=0;
    float beamDuration=0,damageClock=0,armor=0,downTimer=0,effectClock=0;
    float beamYaw=0,beamPitch=0,monoAge=-1;
    float fluidAspect=1,fluidAspectVelocity=0,airBlend=0;
    Vec groundSlope{};float groundHeight=0;
    float capacity=SlimeChargeCapacity,reserve=SlimeMinimumMass;bool regenerating=false;
    float Power(float amount)const{return std::clamp(amount/capacity,0.f,1.f);}
    // HUD reservoir: trail left on the ground, depleted as recall collects it.
    float StoredRatio()const{return std::clamp(deployed/std::max(.01f,capacity),0.f,1.f);}
    void Recover(float& body,float safeAge,float dt){
        regenerating=body>0&&safeAge>=4&&phase==SlimePhase::Roaming&&body+deployed+spent<SlimeMaximumMass*.25f;
        if(regenerating)body+=std::min(32.f*dt,SlimeMaximumMass*.25f-body-deployed-spent);
    }
    Vec fluidDirection{0,0,1};float fluidMotion=0;
    float footprintRadius=0;uint64_t footprintSerial=0;
    Vec beamStart{},beamEnd{};
    SlimePhase phase=SlimePhase::Roaming;
    bool swimming=false,onInk=false,previousAttack=false,limitEligible=false,limitBreak=false,downUsed=false,beamContact=false;
    bool lockedOn=false,previousLock=false;
    bool previousDodge=false,dodgeLiquid=false;
    float beamView=0,heavyView=0,cameraPullback=0;
    Vec cameraBeamDirection{0,0,1};
    float dodgeAge=10,perfectAge=-1;bool perfectUsed=false,perfectBurst=false;Vec dodgeOrigin{};
    int perfectDodges=0;
    bool TryPerfectDodge(float window=.12f){
        if(dodgeAge>window||perfectUsed)return false;
        perfectUsed=true;perfectAge=0;perfectBurst=false;++perfectDodges;return true;
    }
    int shots=0,coreHits=0,tier=0;
    float bonusCharge=0;
    void Collect(float& body,float amount,bool bonus=false){if(bonus)bonusCharge+=amount;else body+=amount;charge+=amount;}
    void Cancel(){charge=0;bonusCharge=0;tier=0;phase=SlimePhase::Roaming;phaseAge=0;limitEligible=false;}
    bool Fire(float& body){
        charge=std::min(charge,std::max(0.f,body-reserve)+bonusCharge);
        if(charge<.1f){Cancel();return false;}
        beamCharge=charge;tier=SlimeTier(charge,capacity);spent=std::max(0.f,charge-bonusCharge);body-=spent;charge=0;bonusCharge=0;
        limitBreak=limitEligible&&tier==3;downUsed=false;phase=SlimePhase::Firing;phaseAge=0;
        float q=Power(beamCharge);beamDuration=.35f+q*.85f;beamRadius=.28f+q*q*2.6f;
        damageClock=0;effectClock=0;monoAge=tier==3?0.f:-1.f;++shots;return true;
    }
    void Return(float& body,float dt){float amount=std::min(spent,SlimeChargeCapacity*dt/.45f);spent-=amount;body+=amount;
        if(spent<=.0001f){spent=0;Cancel();}}
};
inline float InkMoveSpeed(bool submerged,bool /*painted*/){return submerged?27.f:9.f;}
}
