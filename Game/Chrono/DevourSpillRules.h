// DevourSpillRules.h
// 部屋のこぼれた液体 (蜂蜜・のり・ジュース) の粒子の液体の規則。エンジンに依存しない。
// 倒れた入れ物の口から粒を出し、家具の上を縁まで流れ、壁をつたって落ち、床のたまりに広がって溶け込む。
// 粘り (ドロドロ / サラサラ) は種類ごとの値で決める:
//   粘い液体は壁を落ちる速さの上限 (終端速度) が小さく、前後の粒の速さへ強く引き寄せられて (粘性) 1 本の糸のようにつながる。
//   縁では少し溜まってから垂れる。サラサラの液体は重力でそのまま速く落ち、床で跳ねて飛び散る。
#pragma once
#include "CityStageRules.h"
#include <algorithm>
#include <cmath>
namespace Game::Chrono {
enum class SpillKind {Honey,Glue,Juice};
struct SpillParams {
    float topSpeed;   // 家具の上を流れる速さ
    float terminal;   // 壁を落ちる速さの上限
    float gravity;    // 壁を落ち始めるときの加速
    float viscosity;  // 前後の粒の速さへ寄せる割合 (1 秒あたり。大きいほど 1 本につながる)
    float hang;       // 縁で溜まってから垂れるまでの時間
    float radius;     // 粒の半径
    float rate;       // 1 秒に出す粒の数
    float spread;     // 床で広がる速さ
    float floorLife;  // 床でたまりに溶け込むまでの時間
    float strand;     // 前後の粒を糸でつなぐ距離 (0 ならつながない)
    int splash;       // 床に落ちたときに飛び散る粒の数
    float alpha;      // 透けぐあい
};
inline const SpillParams& SpillParamsOf(SpillKind kind){
    static const SpillParams honey{7,9,40,7,.7f,1.3f,8,3,2.6f,7,0,.88f},glue{5,7,34,6,.55f,1.15f,7,2.4f,2.2f,6,0,.82f},juice{22,75,240,.4f,0,.5f,26,13,.8f,0,2,.62f};
    return kind==SpillKind::Honey?honey:kind==SpillKind::Glue?glue:juice;
}
// 壁を落ちる 1 歩: 重力で速くなり、終端速度で頭打ちになる (速さは下向きを正)
inline float SpillWallSpeed(const SpillParams& k,float speed,float dt){return std::min(k.terminal,speed+k.gravity*dt);}
// 粘性: 前後の粒の速さの平均へ寄せる
inline float SpillViscous(const SpillParams& k,float speed,float neighbors,float dt){return speed+(neighbors-speed)*(1-std::exp(-k.viscosity*dt));}
// 壁から床まで落ちる時間のめやす (テスト用)
inline float SpillFallTime(const SpillParams& k,float height){float speed=0,y=height,t=0;const float dt=1.f/120;while(y>0&&t<60){speed=SpillWallSpeed(k,speed,dt);y-=speed*dt;t+=dt;}return t;}
}
