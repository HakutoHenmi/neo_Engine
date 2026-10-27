// SlimeSignalRules.h
// 信号機に変形したスライムの規則 (形・色の順と待ち時間・赤の足止め・青の整列と横断歩道・黄のマーカー)。エンジンに依存しない。
#pragma once
#include "SlimeWeaponRules.h"
#include "SwarmRules.h"
#include <vector>

namespace Game::Chrono {
// 形: 台座・柱・3 つの灯器を横に並べた箱 (本体の大きさ 1 のときの寸法)
inline constexpr float SignalHeight=6.6f,SignalHousingY=5.8f,SignalLampSpacing=1.5f;
inline bool LifeInsideSignal(Vec p){
    float radial=p.x*p.x+p.z*p.z;
    bool base=radial<=1.2f*1.2f&&p.y>=0&&p.y<=.4f;
    bool pole=radial<=.42f*.42f&&p.y>=.4f&&p.y<=5.2f;
    bool housing=std::abs(p.x)<=2.3f&&std::abs(p.y-SignalHousingY)<=.8f&&std::abs(p.z)<=.55f;
    return base||pole||housing;
}
// 灯器の位置 (ローカル)。カメラ側から見て左から青・黄・赤。side が 1 なら前、-1 なら後ろの面
inline Vec SignalLampLocal(SignalColor color,float side){
    float x=color==SignalColor::Green?-SignalLampSpacing:color==SignalColor::Yellow?0.f:SignalLampSpacing;
    return {x,SignalHousingY,.62f*side};
}
// 色の表示用の値
inline Vec SignalColorRGB(SignalColor color){return color==SignalColor::Red?Vec{1,.16f,.1f}:color==SignalColor::Green?Vec{.15f,1,.55f}:Vec{1,.78f,.12f};}

// 色の順と待ち時間: 色ごとの短い待ち・黄の後の長い点検
inline constexpr float SignalStepWait=1.8f,SignalRestTime=8;
// 本体の成長に合わせた範囲の倍率 (開始の大きさで 1、育つほど広がる)
inline float SignalReach(float mass){return std::pow(LifeScale(mass),.75f);}
// 赤: 足を止める時間 / 範囲
inline constexpr float SignalStopTime=4;
inline float SignalStopRadius(float mass){return 20*SignalReach(mass);}
// 青: 並ぶ速さ・横断歩道の幅・人影の数と出る間隔・渡る時間・1 人が 1 体に与えるダメージ
// 横断歩道を端から敷き終える時間 (人影は敷き始めて少ししてから渡り出す)
inline constexpr float SignalLineRate=12,SignalCrossWidth=8,SignalWalkerGap=.12f,SignalWalkTime=1.1f,SignalLayTime=.6f,SignalWalkDelay=.42f;
inline constexpr int SignalWalkers=12;
inline float SignalWalkerDamage(float mass){return 6*std::sqrt(LifeScale(mass));} // 全員に当たれば兵士 (体力 120) をほぼ倒せる
// 黄: マーカーの時間・動いたとみなす距離・届く範囲・時間切れの爆発・動いたときの倍率
inline constexpr float SignalMarkTime=3,SignalMarkMove=2.5f,SignalMarkRange=34,SignalMoveBurst=2.5f;
inline float SignalMarkReach(float mass){return SignalMarkRange*SignalReach(mass);}
inline float SignalMarkDamage(float mass,bool moved){return 30*std::sqrt(LifeScale(mass))*(moved?SignalMoveBurst:1.f);}

// 時間を進める
inline void SignalTick(SignalState& s,float dt){s.wait=std::max(0.f,s.wait-dt);s.flash=std::max(0.f,s.flash-dt);}
// 点検中 (黄の後の長い待ち) か
inline bool SignalResting(const SignalState& s){return s.wait>0&&s.next==SignalColor::Red;}
// 次の色を出す。待ち時間中は出せない。出したら次の色へ進め、待ち時間を始める
inline bool SignalFire(SignalState& s,SignalColor& fired){
    if(s.wait>0)return false;
    fired=s.next;s.next=SignalColor((int(fired)+1)%3);
    s.waitTotal=s.wait=fired==SignalColor::Yellow?SignalRestTime:SignalStepWait;s.flash=.5f;s.flashed=fired;return true;
}
// 前方の扇の中か (すぐ近くなら向きは問わない)
inline bool SignalInCone(Vec from,Vec forward,Vec target,float range){
    Vec d=target-from;d.y=0;float length=Length(d);
    return length<range&&(length<3||Dot(d*(1/length),forward)>.5f);
}
// 赤: 範囲内の敵の足を止める (突進や予告は取りやめる)。止めた数を返す
inline int SignalStopEnemies(std::vector<SwarmEnemy>& enemies,Vec at,float radius){
    int count=0;
    for(auto& e:enemies){if(!SwarmAlive(e))continue;Vec d=e.at-at;d.y=0;if(Length(d)>radius+e.scale)continue;
        e.signalStop=SignalStopTime;e.signalLined=false;if(e.phase==SwarmPhase::Windup||e.phase==SwarmPhase::Dash)ChangeSwarmPhase(e,SwarmPhase::Chase);++count;}
    return count;
}
// 青: 横一列に並べる位置 (中心から side の向きへ等間隔)
inline std::vector<Vec> SignalLinePositions(Vec center,Vec side,int count,float spacing){
    std::vector<Vec> out;out.reserve(size_t(count));
    for(int i=0;i<count;++i)out.push_back(center+side*((float(i)-.5f*float(count-1))*spacing));
    return out;
}
// 横断歩道は敵の列より少し手前 (プレイヤー側) に敷き、人影が敵に隠れずに見えるようにする (敵には届く距離)
inline constexpr float SignalCrossNear=3.5f;
// 横断歩道の端から端までの半分の長さ (size は成長の倍率)
inline float SignalCrossHalf(float length,float size){return length*.5f+5*size;}
// 横断歩道の i 人目の人影の位置 (渡り始める前・渡り終えた後は false)。歩道は side の向きに、dir (1 か -1) の端から渡る
inline bool SignalWalkerAt(Vec center,Vec side,Vec forward,float length,float size,float dir,int i,float age,Vec& at){
    float start=SignalWalkDelay+float(i)*SignalWalkerGap,t=(age-start)/SignalWalkTime;if(t<0||t>1)return false;
    float lane=(-SignalCrossNear+(float(i%3)-1)*1.f)*size,half=SignalCrossHalf(length,size);
    at=center+side*(dir*(-half+2*half*t))+forward*lane;return true;
}
// 人影が敵に届く距離 (列の少し手前を歩くので、その分を含める)
inline float SignalWalkerReach(float enemyScale,float size){return enemyScale+(SignalCrossNear+1.5f)*size;}
// 端から k 本目 (全 bars 本) の縞が敷かれる時刻
inline float SignalStripeTime(int k,int bars){return SignalLayTime*float(k)/float(std::max(1,bars));}
// 横断歩道が終わる時間
inline float SignalCrossDuration(){return SignalWalkDelay+float(SignalWalkers-1)*SignalWalkerGap+SignalWalkTime+.35f;}
// 黄: マーカーを進める。0: 何もない、1: 時間切れで爆発、2: 動いたので大きく爆発
inline int SignalTickMark(SwarmEnemy& e,float dt){
    if(e.signalMark<=0)return 0;
    Vec d=e.at-e.signalMarkAt;d.y=0;if(Length(d)>SignalMarkMove){e.signalMark=0;return 2;}
    e.signalMark-=dt;if(e.signalMark<=0){e.signalMark=0;return 1;}
    return 0;
}
}
