// RoomRules.h
// 吸収モードの部屋の高さ (ベッド・机・こたつ・座布団・将棋盤と、そこへ登る階段やレール)。エンジンに依存しない。
// 1cm = 1.2 単位の実寸で作り、手前が子ども部屋、奥が和室になっている。見た目はすべてモデルで、地面はモデルと同じ形にする。
// 面は 3 種類: 平らな面・x の向きに高さが変わる坂・周りがなだらかな丘 (hill はなだらかな縁の幅、base は縁の高さ)
#pragma once
#include "CityStageRules.h"
#include <array>
namespace Game::Chrono {
// 面: 床の範囲・高さ (x0 の側・反対側)・物を置く区画 (3 ベッド・4 机。0 は決まった物だけ)
// 丘の縁の幅・縁の高さ・この高さより下にいるときは無い物とする高さ (ベッドと机の下を通る)
struct RoomSurface {CityRect foot{};float h0=0,h1=0;int area=0;float hill=0,base=0,minY=0;};
// ベッドの上の面の高さと、本の階段 1 段の高さと奥行き
inline constexpr float RoomBedTop=56,RoomStairRise=7,RoomStairDepth=12;
// ベッドの下を囲む収納ケースの高さ (開始の大きさでは跳び越えられず、8cm で跳び越えられる)
inline constexpr float RoomCaseHeight=8.4f;
// ベッドの下 (収納ケースの内側の床。ステージの最初のチュートリアルの場所)。ケースの上やベッドの上は含まない
inline bool RoomUnderBed(Vec p){return p.x<-72&&p.z<-192&&p.y<20;}
// 本棚とおもちゃ箱の上の高さ
inline constexpr float RoomShelfTop=141.6f,RoomToyboxTop=43.2f;
inline constexpr std::array<RoomSurface,27> RoomSurfaces{{
    // ベッド: マットレス・掛け布団・枕・頭の板
    {{-300,-300,-60,-180},52.8f,52.8f,3,0,0,20},{{-232.8f,-296.4f,-69.6f,-180},RoomBedTop,RoomBedTop,0,4,52.8f,20},{{-297.6f,-288,-235.2f,-192},62.4f,62.4f,0,12,52.8f,20},{{-300,-300,-292.8f,-180},86,86,0,0,0,20},
    // ベッドの下を囲む収納ケース
    {{-300,-192,-60,-180},RoomCaseHeight,RoomCaseHeight},{{-72,-300,-60,-192},RoomCaseHeight,RoomCaseHeight},
    // ベッドへ登る本の階段 (高い段から順に)
    {{-230,-180,-130,-168},56,56},{{-230,-168,-130,-156},49,49},{{-230,-156,-130,-144},42,42},{{-230,-144,-130,-132},35,35},
    {{-230,-132,-130,-120},28,28},{{-230,-120,-130,-108},21,21},{{-230,-108,-130,-96},14,14},{{-230,-96,-130,-84},7,7},
    // 机と背の板 (ベッド側へこぼれた蜂蜜をつたって登る)
    {{170,-300,300,-228},86.4f,86.4f,4,0,0,40},{{170,-300,300,-296},117.6f,117.6f,0,0,0,40},
    // こたつ: 布団の裾からそのまま天板まで登れる
    {{-238,58,-2,294},42,42,0,74},
    // 座布団: 周りがなだらかなので乗り越えられる
    {{-299,139,-225,213},6,6,0,11},{{0,83,74,157},6,6,0,11},{{0,195,74,269},6,6,0,11},{{133,93,207,167},6,6,0,11},{{209,153,283,227},6,6,0,11},
    // 将棋盤と、積んだ本の段
    {{148.4f,166.6f,191.6f,213.4f},22.9f,22.9f},{{136.4f,179,148.4f,201},15.3f,15.3f},{{124.4f,179,136.4f,201},7.65f,7.65f},
    // 本棚とおもちゃ箱の上 (こぼれた液体をつたって登る。下にいる間は無い物とする)
    {{264,-110,300,-10},RoomShelfTop,RoomShelfTop,0,0,0,100},{{-268,-44,-196,4},RoomToyboxTop,RoomToyboxTop,0,0,0,30}}};
// 面の中のその点の高さ
inline float RoomSurfaceHeight(const RoomSurface& s,Vec p){
    const auto& f=s.foot;
    if(s.hill>0){float edge=std::min(std::min(p.x-f.x0,f.x1-p.x),std::min(p.z-f.z0,f.z1-p.z));return s.base+(s.h0-s.base)*std::min(1.f,std::max(0.f,edge)/s.hill);}
    float u=(p.x-f.x0)/(f.x1-f.x0);return s.h0+(s.h1-s.h0)*u;
}
inline bool RoomOnSurface(const RoomSurface& s,Vec p){return p.y>=s.minY&&p.x>=s.foot.x0&&p.x<=s.foot.x1&&p.z>=s.foot.z0&&p.z<=s.foot.z1;}
// その点の一番高い面の高さ (面が無ければ床の 0)。p.y より上の面だけが対象なので、ベッドの下では床になる
inline float RoomGround(Vec p){float height=0;for(const auto& s:RoomSurfaces)if(RoomOnSurface(s,p))height=std::max(height,RoomSurfaceHeight(s,p));return height;}
// 高い所も含めた一番上の面の高さ (物を置くときに使う)
inline float RoomTop(Vec p){p.y=1e6f;return RoomGround(p);}
}
