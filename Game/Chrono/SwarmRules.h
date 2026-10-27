// SwarmRules.h
// 群れの敵の規則 (段階・状態・出現位置・カメラでの透明度・1 フレームの更新)。
#pragma once
#include "../../Engine/IState.h"
#include "ChronoRules.h"
#include <cstdint>
#include <iterator>
namespace Game::Chrono {
// 戦闘の段階 / 敵の状態 / 敵の出来事 (自爆)
enum class BattlePhase {Horde,Emerging,Boss};
enum class SwarmPhase {Spawn,Chase,Windup,Dash,Recover,Dead};
enum class SwarmEvent {None,Detonate};
// ボスが出るまでの撃破数と、同時に出せる上限
inline constexpr int SwarmBossKills=1000,SwarmCapacity=180;
// 輪はこの高さまでの敵を捕獲する: 歩く敵・急降下する飛ぶ敵・落ちる自爆の敵
inline constexpr float SwarmCaptureHeight=8.f;
inline constexpr float SwarmEmergenceSeconds=3.2f;
inline constexpr uint32_t SwarmNoTarget=0xffffffffu;
// 群れの敵 1 体の状態
struct SwarmEnemy {
    float scale=1,maxHealth=3;
    uint64_t lifeHitRevision=0;
    uint32_t id=0,attacks=0;Vec at{},heading{0,0,1},attackAt{};float health=3,age=0,clock=0,reserved=0,flash=0,attackDelay=0,cameraOpacity=1,corrosion=0,burn=0,slow=0;bool inPool=false;
    bool bomber=false,flying=false,struck=false;SwarmPhase phase=SwarmPhase::Spawn;
    // 街のステージでの役割: 種類・出現した段階・巡回の経路・遠距離と装甲の状態
    uint8_t kind=0;int8_t step=0;bool alerted=false;Vec home{},patrol{};float shotClock=0,armor=0,stagger=0;
    // 信号機: 赤で足を止められた残り時間・青で並ぶ位置 (並ぶ間だけ有効)・黄のマーカーの残り時間とそのときの位置
    float signalStop=0,signalMark=0;Vec signalLine{},signalMarkAt{};bool signalLined=false;
};
// id から決まる 0〜1 のばらつき
inline float SwarmVariation(uint32_t id,uint32_t cycle=0){uint32_t hash=id*747796405u+cycle*2891336453u+277803737u;hash=(hash^(hash>>16))*2246822519u;return float(hash&65535u)/65535.f;}
// 予告の時間 / 突進の速さ / 役割 (飛ぶ・自爆) を決める
inline float SwarmWindup(const SwarmEnemy& enemy){return (enemy.bomber?3.3f:enemy.flying?1.3f:1.1f)+SwarmVariation(enemy.id,enemy.attacks)*(enemy.bomber?.7f:.5f);}
inline float SwarmDashSpeed(const SwarmEnemy& enemy){return ((enemy.flying?28.f:20.f)+SwarmVariation(enemy.id,enemy.attacks)*4)*std::sqrt(enemy.scale);}
inline void SwarmRole(SwarmEnemy& enemy){enemy.flying=enemy.id%10!=0;enemy.bomber=enemy.flying&&enemy.id%3==0;enemy.attackDelay=1.5f+SwarmVariation(enemy.id)*3;}
// プレイヤーの周りの、遊べる範囲内の出現位置
inline Vec SwarmSpawnPosition(Vec player,uint32_t id){
    float radius=85+SwarmVariation(id)*35;
    for(int attempt=0;attempt<32;++attempt){float angle=float(id)*2.399963f+attempt*2.399963f;
        Vec at=player+Vec{std::cos(angle)*radius,0,std::sin(angle)*radius};
        if(at.x>=-300&&at.x<=300&&at.z>=-310&&at.z<=350)return at;}
    return {0,player.y,20};
}
// 生きているか
inline bool SwarmAlive(const SwarmEnemy& enemy){return enemy.health>0&&enemy.phase!=SwarmPhase::Dead;}
// カメラとプレイヤーの間にいる敵を透明にする不透明度
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
// 状態を切り替える (時間を 0 に戻す)
inline void ChangeSwarmPhase(SwarmEnemy& enemy, SwarmPhase next) {
	enemy.phase = next;
	enemy.clock = 0;
}
// 各状態の定義
#include "SwarmStates.inl"

// 1 フレームの更新: 時間を進め、現在の状態を更新する。自爆したら Detonate を返す
inline SwarmEvent TickSwarm(SwarmEnemy& enemy,Vec player,float dt,float ground=0,bool canAttack=true){
    if(!SwarmAlive(enemy))return SwarmEvent::None;
    dt=std::clamp(dt,0.f,.05f);enemy.age+=dt;enemy.clock+=dt;enemy.flash=std::max(0.f,enemy.flash-dt);
    Vec delta=player-enemy.at;Vec planar=delta;planar.y=0;float distance=Length(planar);
    if(!enemy.flying)delta.y=0;
	SwarmStates::Context context{enemy, player, delta, distance, dt, ground, canAttack};
	SwarmStates::Get(enemy.phase).Update(context);
	if (context.event == SwarmEvent::Detonate)
		return context.event;
	if(enemy.flying)enemy.at.y=std::max(ground+1.2f,enemy.at.y);
	return context.event;
}

// ボスが出る条件 / 外積
inline bool SwarmBossReady(int creditedKills){return creditedKills>=SwarmBossKills;}
inline Vec SwarmCross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
}
