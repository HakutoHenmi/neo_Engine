// RoomRules.h
// 吸収モードの子ども部屋の高さ (布団・テーブル・机・棚・ソファと、そこへ登る坂)。エンジンに依存しない。
// 坂は矩形の中で高さが変わる面。x 方向か z 方向に h0 から h1 へ変わり、平らな面は h0 と h1 が同じ
#pragma once
#include "CityStageRules.h"
#include <array>
namespace Game::Chrono {
// 面: 床の範囲・高さ (x0 または z0 の側・反対側)・z 方向の坂か・色・物を置く区画 (3 低い・4 中くらい・5 高い。0 は坂)
struct RoomSurface {CityRect foot{};float h0=0,h1=0;bool alongZ=false;float r=1,g=1,b=1;int area=0;};
inline constexpr std::array<RoomSurface,10> RoomSurfaces{{
    // 布団と枕
    {{-250,-60,-120,110},8,8,false,.2f,.3f,.6f,3},{{-120,-60,-90,110},8,0,false,.3f,.4f,.7f},
    {{-245,40,-190,100},16,16,false,.45f,.65f,.95f,3},{{-190,40,-165,100},16,8,false,.5f,.7f,.95f},
    // 低いテーブル・椅子・棚と、順に登る坂
    {{10,-50,110,30},26,26,false,.95f,.78f,.3f,4},{{-40,-30,10,10},0,26,false,.75f,.3f,.3f},
    {{140,-30,175,5},42,42,false,.25f,.65f,.55f,5},{{110,-20,140,-5},26,42,false,.85f,.6f,.3f},
    {{150,50,250,140},64,64,false,.55f,.4f,.28f,5},{{155,5,175,50},42,64,true,.7f,.5f,.35f}}};
// 面の中のその点の高さ
inline float RoomSurfaceHeight(const RoomSurface& s,Vec p){
    float u=s.alongZ?(p.z-s.foot.z0)/(s.foot.z1-s.foot.z0):(p.x-s.foot.x0)/(s.foot.x1-s.foot.x0);return s.h0+(s.h1-s.h0)*u;
}
inline bool RoomOnSurface(const RoomSurface& s,Vec p){return p.x>=s.foot.x0&&p.x<=s.foot.x1&&p.z>=s.foot.z0&&p.z<=s.foot.z1;}
// その点の一番高い面の高さ (面が無ければ床の 0)
inline float RoomGround(Vec p){float height=0;for(const auto& s:RoomSurfaces)if(RoomOnSurface(s,p))height=std::max(height,RoomSurfaceHeight(s,p));return height;}
}
