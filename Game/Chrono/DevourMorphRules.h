// DevourMorphRules.h
// 吸収モードの変形 (掃除機・バネ) の形と規則。エンジンに依存しない。
// 形は液体の粒子で作るので、細い部分は太く、隙間は広くして、スライムのままでも形が読めるようにする
#pragma once
#include "DevourRules.h"
#include "SlimeWeaponRules.h"
namespace Game::Chrono {
// 掃除機の形の範囲 (x は左右の半分、y は 0 から、z は後ろから前)
inline constexpr float DevourVacuumHalf=6.4f,DevourVacuumTop=6.6f,DevourVacuumBack=-5,DevourVacuumFront=7.2f;
// 本体の左右のずれと吸い口の左右の位置。カメラはいつも後ろから見るので、本体を横へずらして
// ホースが斜めに横切り、吸い口が見えるようにする
inline constexpr float DevourVacuumBodyX=2.8f,DevourVacuumMouthX=-1.2f;
// 太い本体に粒子を取られるとホースがやせてちぎれて見えるので、本体の粒子はこの割合に減らす
inline constexpr float DevourVacuumBodyDensity=.8f;
// 掃除機の本体と車輪
inline bool DevourVacuumBody(Vec p){
    constexpr float bx=DevourVacuumBodyX;
    if(LifeSegmentDistance(p,{bx,1.9f,-3.2f},{bx,1.9f,-1.2f})<1.9f)return true;
    float dy=p.y-1.2f,dz=p.z+2.2f;return std::abs(std::abs(p.x-bx)-2.5f)<.45f&&dy*dy+dz*dz<1.2f*1.2f;
}
// 掃除機の形: 車輪の付いた本体・上へ弧を描くホース・まっすぐな柄・床を吸う幅広の吸い口
inline bool DevourInsideVacuum(Vec p){
    if(DevourVacuumBody(p))return true;
    constexpr float bx=DevourVacuumBodyX,mx=DevourVacuumMouthX;
    // ホース (本体の上から前へ弧を描く)
    Vec a{bx,3.6f,-.9f},b{.6f,7.6f,1.4f},c{mx,3.4f,3.8f},previous=a;
    for(int i=1;i<=10;++i){float t=float(i)/10,u=1-t;Vec q=a*(u*u)+b*(2*u*t)+c*(t*t);if(LifeSegmentDistance(p,previous,q)<.6f)return true;previous=q;}
    // 柄
    if(LifeSegmentDistance(p,c,{mx,1,5.6f})<.56f)return true;
    // 吸い口
    return std::abs(p.x-mx)<3.2f&&p.y<1.1f&&p.z>5.4f&&p.z<6.8f;
}
// バネの形: 上下の板と、隙間の広いコイル。
// 粒子の数は決まっているので、形の体積が普段の体より大きいと表面が途切れる (細い部分から消える)
inline constexpr float DevourSpringCoil=2.2f,DevourSpringTube=.58f,DevourSpringPitch=2.7f,DevourSpringPlate=.3f,DevourSpringPlateRadius=2.7f;
inline constexpr int DevourSpringTurns=3;
inline constexpr float DevourSpringHeight=2*DevourSpringPlate+DevourSpringPitch*DevourSpringTurns;
inline bool DevourInsideSpring(Vec p){
    float radius=std::sqrt(p.x*p.x+p.z*p.z);
    if((p.y<DevourSpringPlate||p.y>DevourSpringHeight-DevourSpringPlate)&&radius<DevourSpringPlateRadius)return true;
    // コイルは高さとともに回る。同じ向きの巻きを順に調べる
    float turn=std::atan2(p.z,p.x)/6.2831853f+.5f;
    for(int k=0;k<DevourSpringTurns;++k){float y=DevourSpringPlate+DevourSpringPitch*(turn+float(k)),dr=radius-DevourSpringCoil,dy=p.y-y;
        if(dr*dr+dy*dy<DevourSpringTube*DevourSpringTube)return true;}
    return false;
}
// 掃除機: 吸い込める大きさはタイヤと同じ。届く距離 (吸い口から)・前の広がり (内積)・吸い込む間隔・飛んでくる時間・吸い込む間の移動の速さ
inline constexpr float DevourVacuumCone=.72f,DevourVacuumInterval=.06f,DevourVacuumFlight=.32f,DevourVacuumSlow=.45f;
inline float DevourVacuumRange(float mass){return 15*LifeScale(mass);}
inline bool DevourVacuumReaches(Vec mouth,Vec forward,Vec at,float mass,float radius){
    Vec delta=at-mouth;delta.y=0;float distance=Length(delta);
    return DevourCanCover(mass,radius)&&distance<=DevourVacuumRange(mass)&&(distance<.5f||Dot(Unit(delta),forward)>=DevourVacuumCone);
}
// バネ: 溜めきる時間・溜めなくても出る強さ・着地の後に待つ時間
inline constexpr float DevourSpringChargeTime=.6f,DevourSpringMinPower=.5f,DevourSpringCooldown=.8f;
// 跳ぶ距離・高さ・時間 (溜めるほど遠く高く。大きさに比例するので、どの輪でも建物を越えられる)
inline float DevourSpringRange(float mass,float power){return (8+16*power)*LifeScale(mass);}
inline float DevourSpringRise(float mass,float power){return (10+14*power)*LifeScale(mass);}
inline float DevourSpringTime(float power){return .75f+.35f*power;}
// 着地の衝撃で飲み込む広さ
inline float DevourSpringShock(float mass){return 1.8f*DevourBodyRadius(mass);}
}
