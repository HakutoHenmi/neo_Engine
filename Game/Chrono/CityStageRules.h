#pragma once
#include "SwarmRules.h"
#include "SlimeLifeRules.h"
#include <array>
#include <cstdint>
#include <vector>
// One continuous modern city stage, driven by the slime's size (Docs/SlimeLife.md).
// Back alley -> (construction barricade, hammer) -> avenue -> (steel shutter gate, absorb/break)
// -> scramble crossing and guardian. World units: about 6 units per metre (the crossing is
// roughly 127 m square). The playable floor is flat at y=0.
namespace Game::Chrono {
enum class CityArea : uint8_t {Alley,Street,Plaza};
enum class CityEnemy : uint8_t {Rat,Guard,Archer,Shield,Knight,Cannon,Soldier,Guardian};
struct CityRect {
    float x0,z0,x1,z1;
    bool Contains(Vec p)const {return p.x>=x0&&p.x<=x1&&p.z>=z0&&p.z<=z1;}
    Vec Center()const {return {(x0+x1)*.5f,0,(z0+z1)*.5f};}
};
// Floor regions; buildings line every outer edge. Overlaps connect neighbouring regions.
inline constexpr std::array<CityRect,8> CityWalk{{
    {-22,-700,22,-580},  // back alley: start
    {-22,-584,100,-540}, // back alley: bend
    {56,-544,100,-468},  // back alley: exit to the barricade
    {-12,-472,168,-72},  // avenue (pavement to pavement)
    {-160,-300,-8,-230}, // west side street
    {164,-300,316,-230}, // east side street
    {48,-76,108,-36},    // gate passage between the two towers
    {-302,-40,458,720},  // scramble crossing
}};
inline constexpr CityRect CityBarricade{54,-486,102,-474};
inline constexpr CityRect CityIronGate{46,-62,110,-50};
inline constexpr Vec CityBarricadeCenter{78,0,-480},CityGateCenter{78,0,-56},CityCrossing{78,0,340};
inline constexpr float CityRoadHalf=80; // Half width of both crossing roads.
inline constexpr Vec CityStart{0,0,-680};
inline constexpr float CityBarricadeHealth=120,CityGateHealth=900,CityGateAbsorbMass=1400,CityGateReward=320;
inline CityArea CityAreaAt(Vec p){return p.z<-476?CityArea::Alley:p.z<-44?CityArea::Street:CityArea::Plaza;}
inline const char* CityAreaName(CityArea area){return area==CityArea::Alley?"路地裏":area==CityArea::Street?"市街地":"大交差点";}
struct CityGates {bool barricadeOpen=false,gateOpen=false;};
// A disc of radius margin must lie on the floor: the centre and eight rim points are tested,
// so junctions between overlapping regions stay passable while walls and closed gates block.
inline bool CityWalkable(Vec p,float margin,CityGates gates){
    auto onFloor=[&](Vec q){
        if(!gates.barricadeOpen&&CityBarricade.Contains(q))return false;
        if(!gates.gateOpen&&CityIronGate.Contains(q))return false;
        for(const auto& rect:CityWalk)if(rect.Contains(q))return true;
        return false;};
    if(!onFloor(p))return false;
    for(int i=0;i<8;++i){float a=float(i)*.7853982f;if(!onFloor(p+Vec{std::cos(a)*margin,0,std::sin(a)*margin}))return false;}
    return true;
}
// Slides along walls axis by axis. A body that grew while touching a wall uses the
// largest clearance it currently has, so it can always slide out and never sinks deeper.
inline Vec CityConstrain(Vec from,Vec to,float margin,CityGates gates){
    margin=std::clamp(margin,.5f,18.f);
    while(margin>.5f&&!CityWalkable(from,margin,gates))margin=std::max(.5f,margin*.85f);
    if(CityWalkable(to,margin,gates))return to;
    // Blocked: pick the walkable candidate that makes the most progress along the step.
    // Axis slides follow straight walls; tangent and full-length axis moves round corners.
    Vec step{to.x-from.x,0,to.z-from.z};float length=Length(step);
    if(length<1e-5f)return {from.x,to.y,from.z};
    // Direction away from the blocked part of the rim at the target position.
    Vec push{};
    for(int i=0;i<8;++i){float a=float(i)*.7853982f;Vec rim{std::cos(a),0,std::sin(a)};
        if(!CityWalkable(Vec{to.x,0,to.z}+rim*margin,.01f,gates))push=push-rim;}
    Vec n=Length(push)>1e-4f?Unit(push):Vec{};
    Vec best{from.x,to.y,from.z};float bestProgress=0;
    auto consider=[&](Vec candidate){candidate.y=to.y;Vec moved{candidate.x-from.x,0,candidate.z-from.z};
        float progress=Dot(moved,step)/length;
        // Never slide back towards the wall that blocked the step; that would oscillate in corners.
        if(Dot(moved,n)< -1e-4f)return;
        if(progress>bestProgress&&CityWalkable(candidate,margin,gates)){best=candidate;bestProgress=progress;}};
    consider({to.x,0,from.z});consider({from.x,0,to.z});
    for(Vec axis:{Vec{1,0,0},Vec{-1,0,0},Vec{0,0,1},Vec{0,0,-1}})consider(from+axis*length);
    if(Length(n)>0){Vec tangent{n.z,0,-n.x};
        consider(from+Unit(Unit(step)+n)*length);consider(from+tangent*length);consider(from-tangent*length);
        // Nothing advances: step straight off the wall (not backwards) so the next step can.
        Vec away=from+n*length;away.y=to.y;
        if(best.x==from.x&&best.z==from.z&&Dot(n,step)>-.8f*length&&CityWalkable(away,margin,gates))return away;}
    return best;
}
struct CityEnemyStats {float scale,health,required,reward,armor;};
inline CityEnemyStats CityStats(CityEnemy kind){
    switch(kind){
    case CityEnemy::Rat:return {.75f,18,15,14,0};
    case CityEnemy::Guard:return {3.2f,70,140,40,0};
    case CityEnemy::Archer:return {3.f,60,220,45,0};
    case CityEnemy::Shield:return {3.4f,110,280,70,0};
    case CityEnemy::Knight:return {3.8f,170,520,80,0};
    case CityEnemy::Cannon:return {4.2f,260,1300,180,0};
    case CityEnemy::Soldier:return {3.4f,120,650,60,0};
    default:return {15.f,3200,3600,400,700};
    }
}
// Contact/dash damage; ranged enemies hurt with projectiles instead (CityShotDamage).
inline float CityDamage(CityEnemy kind){const float damage[]={4,9,5,8,14,0,12,28};return damage[int(kind)];}
inline float CityShotDamage(bool shell){return shell?16.f:6.f;}
inline const char* CityEnemyName(CityEnemy kind){
    const char* names[]={"ネズミ","警備兵","弓兵","盾持ち","騎士","防衛砲台","兵士","城砦ゴーレム"};return names[int(kind)];
}
inline CityEnemy CityKind(const SwarmEnemy& enemy){return CityEnemy(enemy.kind);}
// Shields hold their front against everything but the sword; the guardian's armour yields only to the hammer.
inline bool CityShieldBlocks(const SwarmEnemy& enemy,Vec from,bool sword){
    if(CityKind(enemy)!=CityEnemy::Shield||sword||enemy.stagger>0)return false;
    Vec d=from-enemy.at;d.y=0;return Length(d)>.01f&&Dot(Unit(d),enemy.heading)>.2f;
}
struct CitySpawn {CityEnemy kind;Vec at,patrol;};
struct CityStep {CityArea area;const char* title;const char* goal;};
inline constexpr int CityStepCount=8;
inline const CityStep& CityStepInfo(int step){
    static const CityStep steps[CityStepCount]={
        {CityArea::Alley,"路地裏の掃除","ゴミ袋や箱を吸い込み、ネズミを弱らせて取り込む"},
        {CityArea::Alley,"見回りの警備兵","警備兵を止めて吸い込み、ハンマーで工事バリケードを壊す"},
        {CityArea::Street,"大通りの警備隊","消火栓を吸収し、放水で盾を崩して水で矢を防ぐ"},
        {CityArea::Street,"脇道の制圧","消火栓の放水で脇道の警備兵を押し返す"},
        {CityArea::Street,"騎士団と封鎖シャッター","水圧で騎士団を倒し、放水で封鎖シャッターを破る"},
        {CityArea::Plaza,"交差点の防衛陣地","防衛砲台を黙らせ、兵士をまとめて飲み込む"},
        {CityArea::Plaza,"押し寄せる兵士","子分の大群で兵士を弱らせ、一気に取り込む"},
        {CityArea::Plaza,"城砦ゴーレム","足の装甲をハンマーで砕き、守護者を倒す"},
    };
    return steps[std::clamp(step,0,CityStepCount-1)];
}
inline std::vector<CitySpawn> CityStepSpawns(int step){
    std::vector<CitySpawn> out;
    auto add=[&](CityEnemy kind,Vec at,Vec patrol){out.push_back({kind,at,patrol});};
    auto here=[&](CityEnemy kind,Vec at){add(kind,at,at);};
    const Vec c=CityCrossing;
    switch(step){
    case 0:for(Vec at:{Vec{-12,0,-660},Vec{12,0,-652},Vec{6,0,-636},Vec{-10,0,-622},Vec{13,0,-606},Vec{-3,0,-594}})here(CityEnemy::Rat,at);break;
    case 1:
        add(CityEnemy::Guard,{-6,0,-562},{88,0,-562});add(CityEnemy::Guard,{78,0,-500},{78,0,-536});
        for(Vec at:{Vec{60,0,-570},Vec{92,0,-552},Vec{64,0,-520},Vec{92,0,-498}})here(CityEnemy::Rat,at);break;
    case 2:
        for(Vec at:{Vec{0,0,-380},Vec{156,0,-360},Vec{4,0,-300},Vec{152,0,-262},Vec{40,0,-200},Vec{116,0,-200}})here(CityEnemy::Archer,at);
        for(Vec at:{Vec{48,0,-440},Vec{68,0,-446},Vec{88,0,-446},Vec{108,0,-440}})here(CityEnemy::Shield,at);break;
    case 3:
        add(CityEnemy::Guard,{-140,0,-276},{-30,0,-276});add(CityEnemy::Guard,{-100,0,-248},{-148,0,-248});add(CityEnemy::Guard,{-40,0,-252},{-40,0,-288});
        add(CityEnemy::Guard,{196,0,-248},{300,0,-248});add(CityEnemy::Guard,{256,0,-282},{184,0,-282});add(CityEnemy::Guard,{292,0,-254},{292,0,-290});
        add(CityEnemy::Guard,{30,0,-420},{30,0,-330});add(CityEnemy::Guard,{128,0,-392},{128,0,-460});
        add(CityEnemy::Guard,{40,0,-150},{116,0,-150});add(CityEnemy::Guard,{116,0,-120},{40,0,-120});break;
    case 4:for(int row=0;row<3;++row)for(int column=0;column<4;++column)here(CityEnemy::Knight,{48+float(column)*20,0,-180+float(row)*18});break;
    case 5:
        for(Vec at:{Vec{-172,0,90},Vec{328,0,90},Vec{-172,0,590},Vec{328,0,590}})here(CityEnemy::Cannon,at);
        for(int i=0;i<16;++i){float a=float(i)*.3926991f,r=i%2?170.f:230.f;here(CityEnemy::Soldier,c+Vec{std::cos(a)*r,0,std::sin(a)*r});}
        break;
    case 6:for(int i=0;i<32;++i){float a=float(i)*.1963495f,r=260+float(i%3)*30;here(CityEnemy::Soldier,c+Vec{std::cos(a)*r,0,std::sin(a)*r});}break;
    default:
        here(CityEnemy::Guardian,c+Vec{0,0,220});
        for(int i=0;i<8;++i){float a=float(i)*.7853982f;here(CityEnemy::Soldier,c+Vec{std::cos(a)*130,0,180+std::sin(a)*90});}
        break;
    }
    return out;
}
inline int CityEnemyTotal(){int n=0;for(int s=0;s<CityStepCount;++s)n+=int(CityStepSpawns(s).size());return n;}
inline SwarmEnemy CitySpawnEnemy(const CitySpawn& spawn,uint32_t id,int step){
    SwarmEnemy enemy;enemy.id=id;enemy.step=int8_t(step);enemy.kind=uint8_t(spawn.kind);enemy.flying=enemy.bomber=false;
    auto stats=CityStats(spawn.kind);enemy.scale=stats.scale;enemy.health=enemy.maxHealth=stats.health;enemy.armor=stats.armor;
    enemy.at=enemy.home=spawn.at;enemy.patrol=spawn.patrol;
    // Every enemy faces the way the player arrives: south (-z).
    enemy.heading={0,0,-1};enemy.attackDelay=1.5f+float(id%4)*.6f;enemy.shotClock=1.2f+float(id%5)*.45f;
    enemy.alerted=spawn.kind==CityEnemy::Soldier||spawn.kind==CityEnemy::Guardian||step==6;
    return enemy;
}
// Drops and absorb requirements follow the enemy role, not its spawn order.
inline float CityEnemyRequired(const SwarmEnemy& enemy){return CityStats(CityKind(enemy)).required;}
inline float CityEnemyReward(const SwarmEnemy& enemy){return CityStats(CityKind(enemy)).reward;}
inline LifeCollectible CityEnemyDrop(const SwarmEnemy& enemy){return {enemy.at,CityEnemyRequired(enemy),CityEnemyReward(enemy),true,0xffffffffu,true};}
inline float CityAlertRange(CityEnemy kind){return kind==CityEnemy::Rat?34.f:kind==CityEnemy::Archer?95.f:kind==CityEnemy::Cannon?150.f:70.f;}
// Archers keep this far from their target; everything else closes in.
inline float CityStandoff(CityEnemy kind){return kind==CityEnemy::Archer?42.f:0.f;}
// Ballistic shell: lands at target after `flight` seconds from a muzzle at `from`.
inline Vec CityShellVelocity(Vec from,Vec target,float flight,float gravity=30){
    Vec d=target-from;return {d.x/flight,d.y/flight+.5f*gravity*flight,d.z/flight};
}
}
