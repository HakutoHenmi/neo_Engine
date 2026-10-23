// SlimeTentacleRules.h
// スライムの取り込みの触手の形 (太さと、伸びて縮む曲線)。
#pragma once
#include "SlimeLifeRules.h"

namespace Game::Chrono {
// 取り込みが確定する時点 (時間の割合) と、触手の分割数
inline constexpr float LifeTentacleCapturePhase=.60f;
inline constexpr int LifeTentacleSegments=28;
// 触手の太さ (根元から先へ細くなる)
inline float LifeTentacleRadius(float mass,float along){float u=std::clamp(along,0.f,1.f);return LifeScale(mass)*(.25f-.12f*u+.20f*std::exp(-u*14));}
// 触手の along (0: 根元, 1: 先) の位置。伸びて対象をつかみ、確定後は縮んで戻る (3 次ベジェに揺れを足す)
inline Vec LifeTentaclePoint(const LifeBody& body,const SlimeRecipe& recipe,const LifeGrab& grab,size_t index,float along){
    // 取り込みの進み具合 (取り消したら取り消した時点で止める)
    float t=std::clamp(body.actionAge/recipe.AbsorbDuration(),0.f,1.f);
    float phase=grab.cancelled?std::min(t,grab.cancelAge/recipe.AbsorbDuration()):t;
    // 伸びる量と、確定後に縮む量
    float extend=LifeSmooth((phase-float(index%5)*.025f)/.25f),pull=LifeSmooth((phase-LifeTentacleCapturePhase)/(1-LifeTentacleCapturePhase));
    float active=extend*(1-pull);
    // 取り消したら 0.08 秒で消える
    if(grab.cancelled)active*=1-LifeSmooth((body.actionAge-grab.cancelAge)/.08f);
    float scale=LifeScale(body.mass),u=std::clamp(along,0.f,1.f),seed=float(index)*2.399963f;
    // 体から対象への水平の向きと横の向き
    Vec direction=grab.at-body.at;direction.y=0;if(Length(direction)<.1f)direction=body.forward;direction=Unit(direction);
    Vec side{direction.z,0,-direction.x};
    auto radii=LifeRadii(body,recipe);
    // 根元 (体の前側、少しずらす)
    Vec root=body.at+body.fluidOffset+direction*(radii.x*.6f)+side*(std::sin(seed)*radii.x*.12f);
    root.y=body.at.y-1.25f+.8f*body.air+radii.y*(.4f+.1f*std::sin(seed));
    // 先端 (伸びる量で根元から対象へ)
    Vec tip=Lerp(root,grab.at,active),delta=tip-root;
    // 曲がりの量
    float distance=Length(delta),bend=std::min(2.7f*scale,distance*.23f)*active;
    // 3 次ベジェの 2 つの制御点 (横と上にずらす)
    Vec first=root+delta*.27f+side*(std::sin(seed)*.8f*scale*active);
    Vec second=tip-delta*.25f+side*(std::cos(seed)*.55f*scale*active);
    first.y+=bend;second.y+=bend*.65f;
    float v=1-u;
    // ベジェの点
    Vec point=root*(v*v*v)+first*(3*v*v*u)+second*(3*v*u*u)+tip*(u*u*u);
    // うねりの波を横に足す
    float wave=std::sin(u*3.14159265f)*std::sin(u*8+seed-t*7)*.12f*scale*active;
    return point+side*wave;
}
}
