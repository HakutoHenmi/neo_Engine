#pragma once
#include "ChronoRules.h"
#include "CreatureCombat.h"
#include <array>
#include <vector>

namespace Game::Chrono {
inline constexpr float CreatureScale=6.5f;
inline constexpr float CreatureMorphSeconds=8.f;
// Stable part indices: the same scales become flight feathers during assembly.
// Evaluated in boss time, so hit stop and focus affect every part equally.
inline float Ease(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
struct CreaturePiece { Vec position{},size{1,1,1},rotation{}; int material=0; bool plate=false; };
struct CreaturePose {
    std::array<CreaturePiece,256> pieces{};
    int count=0;
    Vec core{},head{};
    void Add(Vec at,Vec size,Vec rotation,int material,bool plate=false){
        pieces[count++]={at,size,rotation,material,plate};
    }
};
inline Vec CreatureSpine(int i,float age,float bird){
    float u=static_cast<float>(i)/25;
    // Raised cobra neck merges into a broad coil resting above the floor.
    float a=u*5.4f+age*.32f;
    Vec snake{std::sin(a)*std::min(11.f,u*32),2.5f+7*std::exp(-u*9),9+u*23+std::sin(a)*3};
    snake.x+=std::sin(age*1.5f-u*7)*.65f;
    Vec flight;
    if(i<7){float v=static_cast<float>(i)/6;
        flight={std::sin(age*.6f)*.8f,23-v*6,7+v*8};
    }else{float v=static_cast<float>(i-6)/19;
        flight={std::sin(age*1.7f-v*6)*v*3.2f,17-v*8,15+v*17};}
    flight.y+=std::sin(age*2)*.45f;
    return Lerp(snake,flight,bird);
}
inline CreaturePose EvaluateCreature(float age,float form){
    CreaturePose out;float bird=Ease(form),scatter=std::sin(bird*3.14159265f);
    for(int i=0;i<26;++i){
        float u=static_cast<float>(i)/25;Vec at=CreatureSpine(i,age,bird);
        Vec tangent=CreatureSpine(std::min(25,i+1),age,bird)-CreatureSpine(std::max(0,i-1),age,bird);
        Vec rotation{-std::atan2(tangent.y,std::sqrt(tangent.x*tangent.x+tangent.z*tangent.z)),std::atan2(tangent.x,tangent.z),0};
        float v=(u-.24f)*9;
        float taper=std::max(.3f,1-u*.78f),thickness=(3.6f+bird*3.2f*std::exp(-v*v))*taper;
        out.Add(at,{thickness,thickness*.82f,1.9f},rotation,0);
        Vec right{std::cos(rotation.y),0,-std::sin(rotation.y)};
        for(int side=-1;side<=1;++side){
            Vec local=right*(side*thickness*.36f)+Vec{0,thickness*(side==0?.43f:.16f),0};
            float phase=i*.73f+side*1.7f;
            Vec burst{std::cos(phase)*3,std::sin(phase*.7f)*2+2,std::sin(phase)*3};
            out.Add(at+local+burst*scatter,{thickness*.60f,.42f,2.5f},
                rotation+Vec{scatter*.7f,scatter*std::sin(phase),side*.55f},1+(i+side+3)%2,true);
        }
    }
    Vec shoulder=CreatureSpine(6,age,bird);
    for(int side:{-1,1}){
        // Shoulder -> elbow -> wrist -> finger. Distal joints lag the upstroke.
        Vec joints[]={shoulder+Vec{side*1.6f,0,0},
            shoulder+Vec{side*8.f,3+std::sin(age*1.65f)*3,-1.5f},
            shoulder+Vec{side*15.f,1+std::sin(age*1.65f-.45f)*6,3},
            shoulder+Vec{side*21.f,-1+std::sin(age*1.65f-.8f)*9,7}};
        for(int j=0;j<3;++j){
            Vec d=joints[j+1]-joints[j];
            Vec rot{-std::atan2(d.y,std::sqrt(d.x*d.x+d.z*d.z)),std::atan2(d.x,d.z),0};
            out.Add(Lerp(CreatureSpine(8+j*5,age,0),(joints[j]+joints[j+1])*.5f,bird),
                Lerp(Vec{1.5f,1,2},Vec{2.8f-j*.55f,1.8f-j*.4f,Length(d)+1},bird),rot*bird,1);
        }
        // Three staggered layers wrap the curved spar: long flight feathers,
        // shorter coverts and a raised leading edge, instead of a single comb.
        for(int layer=0;layer<3;++layer)for(int i=0;i<18;++i){
            float u=(static_cast<float>(i)+layer*.22f)/18;
            float along=u*3;int joint=std::min(2,static_cast<int>(along));
            Vec root=Lerp(joints[joint],joints[joint+1],along-joint);
            float fan=Ease((u-.55f)/.45f);
            Vec direction=Unit(Vec{side*(.12f+fan*1.7f),-.7f+std::sin(age*1.65f-u)*.35f,1.1f-fan*.45f});
            float length=(layer==0?8.5f:layer==1?5.4f:3.1f)*(1-.22f*u)+std::sin(u*3.14159f)*1.4f;
            Vec flying=root+Vec{0,layer*.55f,-layer*.65f}+direction*(length*.40f);
            Vec folded=CreatureSpine(7+i,age,0)+Vec{side*(.8f+layer*.3f),.7f+layer*.25f,0};
            float phase=i*1.8f+side+layer;
            Vec burst{side*(2+u*5),3+std::sin(phase)*2,std::cos(phase)*4};
            Vec rotation{-std::atan2(direction.y,std::sqrt(direction.x*direction.x+direction.z*direction.z)),
                std::atan2(direction.x,direction.z),side*(-.2f+u*.35f)};
            out.Add(Lerp(folded,flying,bird)+burst*scatter,
                Lerp(Vec{1.5f,.3f,2.1f},Vec{layer==0?1.85f:2.2f,.24f,length},bird),rotation*bird,
                layer==0?1:2,true);
        }
    }
    Vec head=CreatureSpine(0,age,bird);out.head=head;
    out.Add(head+Vec{0,.35f,-1},Lerp(Vec{3.7f,2.6f,4},Vec{3,2.8f,3.2f},bird),{},2);
    out.Add(head+Vec{0,-.6f,-2},Lerp(Vec{3,.65f,2.5f},Vec{1.7f,1.1f,3.3f},bird),{.12f,0,0},1,true);
    for(int side:{-1,1}){
        out.Add(head+Vec{side*1.22f,.65f,-2.1f},{.42f,.38f,.55f},{},3);
        out.Add(head+Vec{side*1.1f,1,-1.8f},{1.2f,.35f,1.7f},{0,side*.3f,side*.18f},2,true);
    }
    out.core=shoulder+Vec{0,-.2f,-2.3f};
    // Grow in world space, around the front of the encounter, not the camera.
    auto expand=[](Vec v){return Vec{v.x*CreatureScale,v.y*CreatureScale,9+(v.z-9)*CreatureScale};};
    for(int i=0;i<out.count;++i){
        auto& piece=out.pieces[i];
        // Every physical part leaves the silhouette, including the spine and
        // head. Seven nested diamond lattices rotate in alternating directions.
        // The pattern is made of the creature itself, without drawn circles.
        int tier=i/32,slot=i%32;
        // Each part travels in 0.32 seconds; waves fill the unchanged 8s cycle.
        float depart=.075f+(1.f-static_cast<float>(i)/223)*.13f;
        float arrive=.625f+static_cast<float>(i)/223*.265f;
        float spread=AttackEase((form-depart)/.04f)*(1-AttackEase((form-arrive)/.04f));
        float edge=static_cast<float>(slot%8)/8;
        constexpr Vec corners[]={{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{1,0,0}};
        Vec diamond=Lerp(corners[slot/8],corners[slot/8+1],edge);
        float pulse=AttackEase((form-.29f-tier*.007f)/.04f)+AttackEase((form-.44f-tier*.007f)/.04f)+AttackEase((form-.56f-tier*.005f)/.035f);
        float turn=(tier%2==0?1.f:-1.f)*(pulse*1.570796f+(form-.5f)*.5f)+tier*.18f;
        float radius=22.f+tier*3.2f;
        Vec pattern{(diamond.x*std::cos(turn)-diamond.z*std::sin(turn))*radius,
            10.f+tier*5.5f+(slot%2==0?.8f:-.8f),
            16.f+(diamond.x*std::sin(turn)+diamond.z*std::cos(turn))*radius*.65f};
        Vec arc{std::sin(slot*2.4f)*4,5,std::cos(slot*2.4f)*4};
        piece.position=Lerp(piece.position,pattern,spread)+arc*(4*spread*(1-spread));
        Vec patternRotation{.35f+form*1.2f,turn+slot/8*1.570796f,(tier%2==0?1.f:-1.f)*.6f};
        piece.rotation=Lerp(piece.rotation,patternRotation,spread);
        piece.size=piece.size*(1+spread*.25f);
        piece.position=expand(piece.position);piece.size=piece.size*CreatureScale;
    }
    out.head=expand(out.head);out.core=expand(out.core);
    return out;
}
// Each part departs independently, scatters locally, then flies to its own final socket.
inline Vec ReformPart(Vec from,Vec to,int index,float progress){
    float delay=float((index*37)%223)/222*.20f;
    float t=std::clamp((progress-delay)/(.96f-delay),0.f,1.f);
    float phase=index*2.399963f;
    Vec scatter{std::cos(phase)*(10+index%7),12+float(index%11),std::sin(phase)*(10+index%7)};
    float depart=Ease(t/.24f),travel=AttackEase((t-.18f)/.82f);
    Vec start=from+scatter*depart;
    Vec arc{std::sin(phase)*18,22+float(index%13),std::cos(phase)*18};
    return Lerp(start,to,travel)+arc*(4*travel*(1-travel));
}
// One attack with enough feathers to keep each hop within minimum reach.
// The route is frozen at launch, and remains usable
// after its brief dangerous arrival so a missed chain can be retried.
inline std::vector<Vec> FeatherRoute(Vec player,Vec core){
    Vec start{std::clamp(player.x,-348.f,348.f),3,std::clamp(player.z,-398.f,398.f)};
    int count=std::max(5,static_cast<int>(std::ceil(Length(core-start)/7.f)));
    std::vector<Vec> route(static_cast<size_t>(count));
    for(int i=0;i<count;++i){float u=static_cast<float>(i+1)/(count+1);
        route[i]=Lerp(start,core,u)+Vec{std::sin(u*6.283185f)*1.5f,0,0};}
    return route;
}
}
