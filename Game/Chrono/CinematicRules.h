// CinematicRules.h
// ムービーのカメラの計算 (緩急・構図の補間・注視・全体を収める構図)。
#pragma once
#include "ChronoRules.h"
namespace Game::Chrono {
// start〜end の間をなめらかに 0〜1 にする
inline float ShotEase(float age,float start,float end){
    float t=std::clamp((age-start)/(end-start),0.f,1.f);return t*t*(3-2*t);
}
// カメラの姿勢 (位置・回転・画角)
struct CinematicPose {Vec at{},rotation{};float fov=1.13f;};
// 2 つの構図を補間する (回転は近い向きで回す)
inline CinematicPose BlendShot(CinematicPose a,CinematicPose b,float t){
    Vec rotation{a.rotation.x+std::remainder(b.rotation.x-a.rotation.x,6.283185f)*t,
        a.rotation.y+std::remainder(b.rotation.y-a.rotation.y,6.283185f)*t,a.rotation.z+(b.rotation.z-a.rotation.z)*t};
    return {Lerp(a.at,b.at,t),rotation,a.fov+(b.fov-a.fov)*t};
}
// 位置から注視点を向く構図
inline CinematicPose AimShot(Vec at,Vec focus,float fov){
    Vec view=Unit(focus-at);return {at,{-std::asin(std::clamp(view.y,-1.f,1.f)),std::atan2(view.x,view.z),0},fov};
}
// モデル全体の角が収まるよう合わせ、上下の黒帯と下の字幕の分の余白を取る
inline CinematicPose FrameShot(Bounds bounds,Vec outward,float fov,float aspect){
    outward=Unit(outward);Vec forward=outward*-1;
    Vec right=Unit(Vec{forward.z,0,-forward.x});
    Vec up{forward.y*right.z-forward.z*right.y,forward.z*right.x-forward.x*right.z,forward.x*right.y-forward.y*right.x};
    Vec center=(bounds.min+bounds.max)*.5f;
    float tangent=std::tan(fov*.5f),distance=5;
    for(float x:{bounds.min.x,bounds.max.x})for(float y:{bounds.min.y,bounds.max.y})for(float z:{bounds.min.z,bounds.max.z}){
        Vec offset=Vec{x,y,z}-center;float depth=Dot(offset,forward),height=Dot(offset,up);
        distance=std::max(distance,std::abs(Dot(offset,right))/(tangent*std::max(.1f,aspect)*.84f)-depth);
        distance=std::max(distance,(height>=0?height/.78f:-height/.48f)/tangent-depth);
    }
    return AimShot(center+outward*(distance+3),center,fov);
}
// 開幕とボスの出現のムービーの長さ / 強化カードの表示の時間
inline constexpr float OpeningShotSeconds=3.4f,BossShotHoldSeconds=.65f,BossShotReturnSeconds=.85f;
inline float CardReveal(float age,int slot){return std::clamp((age-float(slot)*.07f)/.14f,0.f,1.f);}
inline constexpr float CardRevealSeconds=.28f;
}
