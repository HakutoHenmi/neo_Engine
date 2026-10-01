#pragma once
#include "ChronoRules.h"
#include <cstdint>
namespace Game::Chrono {
enum class BattlePhase {Horde,Emerging,Boss};
enum class SwarmPhase {Spawn,Chase,Windup,Dash,Recover,Dead};
enum class SwarmEvent {None,Detonate};
inline constexpr int SwarmBossKills=1000,SwarmCapacity=180;
inline constexpr float SwarmEmergenceSeconds=3.2f;
inline constexpr uint32_t SwarmNoTarget=0xffffffffu;
struct SwarmEnemy {
    uint32_t id=0,attacks=0;Vec at{},heading{0,0,1},attackAt{};float health=3,age=0,clock=0,reserved=0,flash=0,attackDelay=0,cameraOpacity=1,corrosion=0,burn=0,slow=0;bool inPool=false;
    bool bomber=false,flying=false,struck=false;SwarmPhase phase=SwarmPhase::Spawn;
};
inline float SwarmVariation(uint32_t id,uint32_t cycle=0){uint32_t hash=id*747796405u+cycle*2891336453u+277803737u;hash=(hash^(hash>>16))*2246822519u;return float(hash&65535u)/65535.f;}
inline float SwarmWindup(const SwarmEnemy& enemy){return (enemy.bomber?3.3f:enemy.flying?1.3f:1.1f)+SwarmVariation(enemy.id,enemy.attacks)*(enemy.bomber?.7f:.5f);}
inline float SwarmDashSpeed(const SwarmEnemy& enemy){return (enemy.flying?28.f:20.f)+SwarmVariation(enemy.id,enemy.attacks)*4;}
inline void SwarmRole(SwarmEnemy& enemy){enemy.flying=enemy.id%10!=0;enemy.bomber=enemy.flying&&enemy.id%3==0;enemy.attackDelay=1.5f+SwarmVariation(enemy.id)*3;}
inline Vec SwarmSpawnPosition(Vec player,uint32_t id){
    float radius=85+SwarmVariation(id)*35;
    for(int attempt=0;attempt<32;++attempt){float angle=float(id)*2.399963f+attempt*2.399963f;
        Vec at=player+Vec{std::cos(angle)*radius,0,std::sin(angle)*radius};
        if(at.x>=-300&&at.x<=300&&at.z>=-310&&at.z<=350)return at;}
    return {0,player.y,20};
}
inline bool SwarmAlive(const SwarmEnemy& enemy){return enemy.health>0&&enemy.phase!=SwarmPhase::Dead;}
inline float SwarmCameraOpacity(Vec camera,Vec player,Vec enemy,bool flying){
    float radius=flying?4.8f:3.f;
    auto ease=[](float value){value=std::clamp(value,0.f,1.f);return value*value*(3-2*value);};
    float opacity=ease((Length(enemy-camera)-radius-1)/6);
    Vec segment=player+Vec{0,1.5f,0}-camera;float lengthSquared=Dot(segment,segment);
    if(lengthSquared>.01f){float along=Dot(enemy-camera,segment)/lengthSquared;
        if(along>0&&along<1){float side=Length(enemy-(camera+segment*along));
            opacity=std::min(opacity,.03f+.97f*ease((side-radius)/2));}}
    return opacity;
}
inline SwarmEvent TickSwarm(SwarmEnemy& enemy,Vec player,float dt,float ground=0,bool canAttack=true){
    if(!SwarmAlive(enemy))return SwarmEvent::None;
    dt=std::clamp(dt,0.f,.05f);enemy.age+=dt;enemy.clock+=dt;enemy.flash=std::max(0.f,enemy.flash-dt);
    Vec delta=player-enemy.at;Vec planar=delta;planar.y=0;float distance=Length(planar);
    if(!enemy.flying)delta.y=0;
    auto phase=[&](SwarmPhase next){enemy.phase=next;enemy.clock=0;};
    if(enemy.phase==SwarmPhase::Spawn){if(enemy.clock>=.6f)phase(SwarmPhase::Chase);}
    else if(enemy.phase==SwarmPhase::Chase){
        enemy.attackDelay=std::max(0.f,enemy.attackDelay-dt);bool ready=canAttack&&enemy.attackDelay<=0;Vec approach=delta;
        if(!ready){float hold=enemy.flying?(enemy.bomber?22.f:27.f):8.f;
            Vec radial=distance>.01f?Unit(planar):Vec{std::cos(float(enemy.id)),0,std::sin(float(enemy.id))};Vec side{radial.z,0,-radial.x};
            approach=radial*(distance-hold)+side*(enemy.flying?(enemy.id%2?5.f:-5.f):0.f);}
        if(enemy.flying)approach.y=ground+14+float(enemy.id%5)*2+std::sin(enemy.age*2+float(enemy.id))*1.5f-enemy.at.y;
        enemy.heading=Unit(Lerp(enemy.heading,Unit(approach),1-std::exp(-9*dt)));
        float speed=(enemy.flying?10.f:6.f)+SwarmVariation(enemy.id)*2;
        enemy.at=enemy.at+Unit(approach)*std::min(Length(approach),speed*dt);
        if(ready&&distance<(enemy.flying?(enemy.bomber?28.f:32.f):9.f)){enemy.heading=Unit(delta);enemy.attackAt=player;
            if(!enemy.flying)enemy.attackAt.y=enemy.at.y;
            ++enemy.attacks;enemy.struck=false;phase(SwarmPhase::Windup);}
    }else if(enemy.phase==SwarmPhase::Windup){
        if(enemy.bomber){enemy.heading=Unit(delta);
            if(enemy.clock>.7f)enemy.at=enemy.at+enemy.heading*std::min(std::max(0.f,Length(delta)-1.5f),(12+SwarmVariation(enemy.id,enemy.attacks)*3)*dt);
            if(enemy.clock>=SwarmWindup(enemy)){phase(SwarmPhase::Dead);return SwarmEvent::Detonate;}}
        else if(enemy.clock>=SwarmWindup(enemy))phase(SwarmPhase::Dash);
    }else if(enemy.phase==SwarmPhase::Dash){float remaining=Length(enemy.attackAt-enemy.at);
        enemy.at=enemy.at+enemy.heading*std::min(remaining,SwarmDashSpeed(enemy)*dt);
        if(remaining<.3f||enemy.clock>=2){phase(SwarmPhase::Recover);enemy.attackDelay=1.5f+SwarmVariation(enemy.id,enemy.attacks+1)*2;}}
    else if(enemy.phase==SwarmPhase::Recover){
        if(enemy.flying){enemy.at.y+=std::min(std::max(0.f,ground+18-enemy.at.y),20*dt);enemy.heading.y*=std::exp(-8*dt);}
        if(enemy.clock>=3+SwarmVariation(enemy.id,enemy.attacks)*2)phase(SwarmPhase::Chase);
    }
    if(enemy.flying)enemy.at.y=std::max(ground+1.2f,enemy.at.y);
    return SwarmEvent::None;
}
inline bool SwarmBossReady(int creditedKills){return creditedKills>=SwarmBossKills;}
inline Vec SwarmCross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
}
