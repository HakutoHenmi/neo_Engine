#pragma once
#include "FluidMorphShape.h"

namespace Engine::FluidShape {
// Stable particle identities for a rear body, two rounded arms, cupped palms,
// three broad fingers and opposing thumbs. No additional render-only hands.
enum class EngulfPart {Body,Arm,Palm,Finger,Thumb};
struct EngulfSample {Point rest;EngulfPart part;float side,u,v,w;int finger;};
inline float EngulfSmooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
inline Point EngulfMix(Point a,Point b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
inline Point EngulfBezier(Point a,Point b,Point c,float t){return EngulfMix(EngulfMix(a,b,t),EngulfMix(b,c,t),t);}
inline Point EngulfTube(Point a,Point b,Point c,float along,float disc,float angle,float radius){
    Point center=EngulfBezier(a,b,c,along),tangent=Sub(EngulfMix(b,c,along),EngulfMix(a,b,along));
    float length=std::sqrt(Dot(tangent,tangent));if(length<.001f)tangent={0,1,0};else tangent={tangent.x/length,tangent.y/length,tangent.z/length};
    Point right=Cross(tangent,std::abs(tangent.y)<.95f?Point{0,1,0}:Point{0,0,1});float n=std::sqrt(Dot(right,right));right={right.x/n,right.y/n,right.z/n};Point up=Cross(right,tangent);
    float r=radius*std::sqrt(disc),x=r*std::cos(angle*6.2831853f),y=r*std::sin(angle*6.2831853f);
    return {center.x+right.x*x+up.x*y,center.y+right.y*x+up.y*y,center.z+right.z*x+up.z*y};
}
inline std::vector<EngulfSample> SampleEngulf(const std::vector<Point>& rest){
    std::vector<EngulfSample> result;result.reserve(rest.size());
    for(size_t i=0;i<rest.size();++i){auto p=rest[i];uint32_t id=uint32_t(i+1);float selector=Halton(id,7);
        auto part=p.z<-.7f?EngulfPart::Body:p.z<.25f?EngulfPart::Arm:selector<.57f?EngulfPart::Palm:selector<.9f?EngulfPart::Finger:EngulfPart::Thumb;
        result.push_back({p,part,p.x<0?-1.f:1.f,Halton(id,2),Halton(id,3),Halton(id,5),int(Halton(id,11)*3)});}
    return result;
}
inline Point EngulfPose(const EngulfSample& p,float phase,Point target){
    float t=std::clamp(phase,0.f,1.f),open=EngulfSmooth((t-.03f)/.25f),scoop=EngulfSmooth((t-.28f)/.32f);
    float seal=EngulfSmooth((t-.58f)/.2f),pull=EngulfSmooth((t-.79f)/.21f);
    float reach=std::clamp(target.z,1.8f,9.f),height=std::clamp(target.y+.8f,1.2f,3.f),side=p.side;
    float radius=1.55f*(1-.4f*pull),wide=2.9f+.16f*reach;
    float lateral=(wide-1.55f)*open*(1-scoop);
    Point center{target.x*open*(1-.8f*pull)+side*lateral,height,reach*(.55f+.45f*scoop)};
    center=EngulfMix(center,{side*.35f,.9f,1.25f},pull);
    Point root{side*1.15f,.55f,-.55f};
    Point wrist{center.x+side*radius*.88f,center.y-.25f,center.z-.45f};
    Point elbow{side*(2.15f+.1f*reach)*open*(1-.6f*pull),.6f+.4f*open,reach*.18f*(1-pull)};
    Point pose;
    if(p.part==EngulfPart::Body){pose={p.rest.x*(.72f+.12f*open),p.rest.y*(.75f-.12f*open+.14f*pull),p.rest.z*.65f-.15f};}
    else if(p.part==EngulfPart::Arm){float length=std::sqrt(Dot(Sub(wrist,root),Sub(wrist,root)));
        float thickness=std::clamp(.63f*std::sqrt(3.5f/std::max(2.f,length)),.45f,.74f);
        pose=EngulfTube(root,elbow,wrist,p.u,p.v,p.w,thickness);}
    else if(p.part==EngulfPart::Palm){float latitude=(p.v*2-1)*.9f,phi=(p.u*2-1)*(1.05f+.3f*seal);
        float r=radius+(p.w-.5f)*.58f,ring=std::sqrt(std::max(0.f,1-latitude*latitude));
        pose={center.x+side*r*ring*std::cos(phi),center.y+r*latitude,center.z+r*ring*std::sin(phi)};}
    else if(p.part==EngulfPart::Finger){float spread=float(p.finger-1)*.64f*radius,curl=EngulfSmooth((t-.43f)/.33f);
        Point base{center.x+side*radius*.74f,center.y+radius*.63f,center.z+spread};
        Point joint{base.x+side*radius*(.16f-.62f*curl),base.y+radius*.65f,base.z};
        Point tip{center.x+side*radius*(.8f-.76f*curl),center.y+radius*(1.5f-.43f*curl),center.z+spread*.75f};
        pose=EngulfTube(base,joint,tip,p.u,p.v,p.w,.3f*(1-.2f*pull));}
    else {float curl=EngulfSmooth((t-.48f)/.3f);
        Point base{center.x+side*radius*.62f,center.y-radius*.16f,center.z-radius*.8f};
        Point joint{base.x+side*radius*(.35f-.6f*curl),base.y+radius*.3f,base.z-radius*.12f};
        Point tip{center.x+side*radius*(1.05f-.9f*curl),center.y+radius*.35f,center.z-radius*.64f};
        pose=EngulfTube(base,joint,tip,p.u,p.v,p.w,.34f*(1-.2f*pull));}
    pose.y=std::max(.18f,pose.y);
    return EngulfMix(p.rest,pose,EngulfSmooth(t/.17f));
}
}
