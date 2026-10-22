#pragma once
#include "SlimeLifeRules.h"

namespace Game::Chrono {
inline constexpr float LifeTentacleCapturePhase=.60f;
inline constexpr int LifeTentacleSegments=28;
inline float LifeTentacleRadius(float mass,float along){float u=std::clamp(along,0.f,1.f);return LifeScale(mass)*(.25f-.12f*u+.20f*std::exp(-u*14));}
inline Vec LifeTentaclePoint(const LifeBody& body,const SlimeRecipe& recipe,const LifeGrab& grab,size_t index,float along){
    float t=std::clamp(body.actionAge/recipe.AbsorbDuration(),0.f,1.f);
    float phase=grab.cancelled?std::min(t,grab.cancelAge/recipe.AbsorbDuration()):t;
    float extend=LifeSmooth((phase-float(index%5)*.025f)/.25f),pull=LifeSmooth((phase-LifeTentacleCapturePhase)/(1-LifeTentacleCapturePhase));
    float active=extend*(1-pull);
    if(grab.cancelled)active*=1-LifeSmooth((body.actionAge-grab.cancelAge)/.08f);
    float scale=LifeScale(body.mass),u=std::clamp(along,0.f,1.f),seed=float(index)*2.399963f;
    Vec direction=grab.at-body.at;direction.y=0;if(Length(direction)<.1f)direction=body.forward;direction=Unit(direction);
    Vec side{direction.z,0,-direction.x};
    auto radii=LifeRadii(body,recipe);
    Vec root=body.at+body.fluidOffset+direction*(radii.x*.6f)+side*(std::sin(seed)*radii.x*.12f);
    root.y=body.at.y-1.25f+.8f*body.air+radii.y*(.4f+.1f*std::sin(seed));
    Vec tip=Lerp(root,grab.at,active),delta=tip-root;
    float distance=Length(delta),bend=std::min(2.7f*scale,distance*.23f)*active;
    Vec first=root+delta*.27f+side*(std::sin(seed)*.8f*scale*active);
    Vec second=tip-delta*.25f+side*(std::cos(seed)*.55f*scale*active);
    first.y+=bend;second.y+=bend*.65f;
    float v=1-u;
    Vec point=root*(v*v*v)+first*(3*v*v*u)+second*(3*v*u*u)+tip*(u*u*u);
    float wave=std::sin(u*3.14159265f)*std::sin(u*8+seed-t*7)*.12f*scale*active;
    return point+side*wave;
}
}
