#pragma once
#include "ChronoRules.h"
#include "CreatureCombat.h"
#include <array>
#include <cassert>
#include <vector>

namespace Game::Chrono {
inline constexpr float CreatureScale=6.5f;
inline constexpr float CreatureMorphSeconds=8.f;
inline constexpr int CreatureSpineCount=26,CreaturePartCount=326;
enum class CreatureRole { Spine, DorsalArmor, FlankArmor, VentralArmor, WingSpar, PrimaryFeather,
    CovertFeather, LeadingFeather, Skull, Jaw, Eye, Brow, Beak, Horn, Cheek, Crest };
enum class CreatureShape { Link, Armor, Spar, Primary, Covert, Beak, Horn, Orb };
inline bool CreatureWing(CreatureRole role){return role==CreatureRole::WingSpar||role==CreatureRole::PrimaryFeather||
    role==CreatureRole::CovertFeather||role==CreatureRole::LeadingFeather;}
inline bool CreatureHead(CreatureRole role){return role>=CreatureRole::Skull;}
// Evaluated in boss time, so hit stop and focus affect every part equally.
inline float Ease(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
struct CreaturePiece { Vec position{},size{1,1,1},rotation{}; int material=0;
    CreatureRole role=CreatureRole::Spine;CreatureShape shape=CreatureShape::Link;
    int segment=-1,side=0,layer=-1,span=-1;
};
struct CreaturePose {
    std::array<CreaturePiece,384> pieces{};
    int count=0;
    std::array<int,CreatureSpineCount> spineIndex{};
    std::array<int,2> wingRoot{{-1,-1}},wingTip{{-1,-1}};
    int skullIndex=-1;
    Vec core{},head{};
    int Add(Vec at,Vec size,Vec rotation,int material,CreatureRole role,CreatureShape shape,
        int segment=-1,int side=0,int layer=-1,int span=-1){
        assert(count<static_cast<int>(pieces.size()));
        int index=count++;
        pieces[index]={at,size,rotation,material,role,shape,segment,side,layer,span};
        return index;
    }
};
inline Vec CreatureSpine(int i,float age,float bird){
    float u=static_cast<float>(i)/25;
    // Raised cobra neck merges into a broad coil resting above the floor.
    float a=u*5.4f+age*.32f;
    float radius=3.6f*std::max(.3f,1-u*.78f);
    float groundedBody=radius*.82f*.85f+.06f;
    Vec snake{std::sin(a)*std::min(11.f,u*32),groundedBody+7*std::exp(-u*9),9+u*23+std::sin(a)*3};
    snake.x+=std::sin(age*1.5f-u*7)*.65f;
    Vec flight;
    if(i<7){float v=static_cast<float>(i)/6;
        flight={std::sin(age*.6f)*.8f,23-v*6,7+v*8};
    }else{float v=static_cast<float>(i-6)/19;
        flight={std::sin(age*1.7f-v*6)*v*3.2f,17-v*8,15+v*17};}
    flight.y+=std::sin(age*2)*.45f;
    return Lerp(snake,flight,bird);
}
// OBJ profiles are scaled directly, so their height is not half of piece.size.y.
inline float CreatureVerticalExtent(const CreaturePiece& piece){
    float x=piece.shape==CreatureShape::Horn?.7f:piece.shape==CreatureShape::Spar?.85f:1.f;
    float y=piece.shape==CreatureShape::Link?.94f:piece.shape==CreatureShape::Armor?.58f:
        piece.shape==CreatureShape::Primary?.26f:piece.shape==CreatureShape::Covert?.35f:
        piece.shape==CreatureShape::Horn?.72f:piece.shape==CreatureShape::Beak?1.02f:.5f;
    float pitch=std::abs(std::sin(piece.rotation.x)),roll=std::abs(std::sin(piece.rotation.z));
    return piece.size.y*y*std::abs(std::cos(piece.rotation.x))*std::abs(std::cos(piece.rotation.z))+
        piece.size.x*x*roll+piece.size.z*.5f*pitch;
}
inline CreaturePose EvaluateCreature(float age,float form){
    CreaturePose out;float bird=Ease(form),scatter=std::sin(bird*3.14159265f);
    for(int i=0;i<26;++i){
        float u=static_cast<float>(i)/25;Vec at=CreatureSpine(i,age,bird);
        Vec tangent=CreatureSpine(std::min(25,i+1),age,bird)-CreatureSpine(std::max(0,i-1),age,bird);
        Vec rotation{-std::atan2(tangent.y,std::sqrt(tangent.x*tangent.x+tangent.z*tangent.z)),std::atan2(tangent.x,tangent.z),0};
        float v=(u-.24f)*9;
        float taper=std::max(.3f,1-u*.78f),thickness=(3.6f+bird*3.2f*std::exp(-v*v))*taper;
        out.spineIndex[i]=out.Add(at,{thickness,thickness*.82f,1.9f},rotation,0,
            CreatureRole::Spine,CreatureShape::Link,i);
        Vec right{std::cos(rotation.y),0,-std::sin(rotation.y)};
        for(int side=-1;side<=1;++side){
            Vec local=right*(side*thickness*.36f)+Vec{0,thickness*(side==0?.43f:.16f),0};
            float phase=i*.73f+side*1.7f;
            Vec burst{std::cos(phase)*3,std::sin(phase*.7f)*2+2,std::sin(phase)*3};
            out.Add(at+local+burst*scatter,{thickness*.67f,.42f,2.8f},
                rotation+Vec{scatter*.7f,scatter*std::sin(phase),side*.55f},2,
                side==0?CreatureRole::DorsalArmor:CreatureRole::FlankArmor,CreatureShape::Armor,i,side);
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
            int index=out.Add(Lerp(CreatureSpine(8+j*5,age,0),(joints[j]+joints[j+1])*.5f,bird),
                Lerp(Vec{.55f,.4f,1.35f},Vec{2.8f-j*.55f,1.8f-j*.4f,Length(d)+1},bird),rot*bird,0,
                CreatureRole::WingSpar,CreatureShape::Spar,-1,side,-1,j);
            if(j==0)out.wingRoot[side<0?0:1]=index;
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
            Vec folded=CreatureSpine(7+i,age,0)+Vec{side*(.18f+layer*.12f),-.2f+layer*.14f,0};
            float phase=i*1.8f+side+layer;
            Vec burst{side*(2+u*5),3+std::sin(phase)*2,std::cos(phase)*4};
            Vec rotation{-std::atan2(direction.y,std::sqrt(direction.x*direction.x+direction.z*direction.z)),
                std::atan2(direction.x,direction.z),side*(-.2f+u*.35f)};
            int index=out.Add(Lerp(folded,flying,bird)+burst*scatter,
                Lerp(Vec{.38f,.16f,1.05f},Vec{layer==0?1.85f:2.2f,.24f,length},bird),rotation*bird,
                layer==0?2:layer==1?2:1,
                layer==0?CreatureRole::PrimaryFeather:CreatureRole::CovertFeather,
                layer==0?CreatureShape::Primary:CreatureShape::Covert,-1,side,layer,i);
            if(layer==0&&i==17)out.wingTip[side<0?0:1]=index;
        }
    }
    Vec head=CreatureSpine(0,age,bird);out.head=head;
    out.skullIndex=out.Add(head+Vec{0,.35f,-1},Lerp(Vec{3.7f,2.6f,4},Vec{3,2.8f,3.2f},bird),{},2,CreatureRole::Skull,CreatureShape::Beak);
    out.Add(head+Vec{0,-.6f,-2},Lerp(Vec{3,.65f,2.5f},Vec{1.7f,1.1f,3.3f},bird),{.12f,0,0},1,CreatureRole::Jaw,CreatureShape::Beak);
    for(int side:{-1,1}){
        out.Add(head+Vec{side*1.22f,.65f,-2.1f},{.42f,.38f,.55f},{},3,CreatureRole::Eye,CreatureShape::Orb,-1,side);
        out.Add(head+Vec{side*1.1f,1,-1.8f},{1.2f,.35f,1.7f},{0,side*.3f,side*.18f},2,CreatureRole::Brow,CreatureShape::Armor,-1,side);
    }
    // The second armor course overlaps each vertebra. The pale dorsal ridge
    // stays legible while the narrow dark belly leaves an S-shaped negative space.
    for(int i=0;i<CreatureSpineCount;++i){
        float u=float(i)/25.f, v=(u-.24f)*9.f;
        float thickness=(3.6f+bird*3.2f*std::exp(-v*v))*std::max(.3f,1-u*.78f);
        Vec at=CreatureSpine(i,age,bird),next=CreatureSpine(std::min(25,i+1),age,bird);
        Vec tangent=next-at;float yaw=std::atan2(tangent.x,tangent.z);
        Vec right{std::cos(yaw),0,-std::sin(yaw)};
        for(int side:{-1,0,1}){
            Vec local=side?right*(side*thickness*.6f)+Vec{0,-thickness*.12f,.65f}:Vec{0,-thickness*.43f,.55f};
            out.Add(at+local+Vec{side*.8f,1.2f,std::sin(i*.9f+side)*.8f}*scatter,
                {thickness*(side?.42f:.56f),side?.3f:.26f,2.6f},
                {0,yaw,side*.68f},side?2:0,
                side?CreatureRole::FlankArmor:CreatureRole::VentralArmor,CreatureShape::Armor,i,side,1);
        }
    }
    // A separate leading edge breaks the comb silhouette into layered wings.
    for(int side:{-1,1})for(int i=0;i<8;++i){
        float u=float(i)/7.f;Vec root=shoulder+Vec{side*(2+u*18),2+std::sin(age*1.65f-u*.7f)*(3+u*5),u*6};
        Vec folded=CreatureSpine(7+i*2,age,0)+Vec{side*.32f,.25f,0};
        Vec flying=root+Vec{side*1.2f,.6f,-.8f};
        out.Add(Lerp(folded,flying,bird)+Vec{side*3.f,2.f,0.f}*scatter,
            Lerp(Vec{.36f,.15f,1.1f},Vec{2.1f-u*.7f,.38f,4.8f-u*1.4f},bird),
            {0,side*.4f,side*.15f},2,CreatureRole::LeadingFeather,CreatureShape::Covert,-1,side,3,i);
    }
    out.Add(head+Vec{0,-.05f,-3.45f},Lerp(Vec{2.3f,.8f,2.1f},Vec{1.5f,1.1f,3.2f},bird),{},2,CreatureRole::Beak,CreatureShape::Beak);
    out.Add(head+Vec{0,-.75f,-3.1f},{1.4f,.3f,2.2f},{.15f,0,0},0,CreatureRole::Beak,CreatureShape::Beak,-1,0,1);
    for(int side:{-1,1}){
        out.Add(head+Vec{side*1.7f,1.7f,.35f},{.55f,.7f,2.8f},{-.45f,side*.4f,side*.32f},2,CreatureRole::Horn,CreatureShape::Horn,-1,side);
        out.Add(head+Vec{side*1.7f,-.2f,-.8f},{1.15f,.7f,2.3f},{0,side*.24f,side*.18f},2,CreatureRole::Cheek,CreatureShape::Armor,-1,side);
        out.Add(head+Vec{side*.55f,1.65f,.8f},{.5f,.45f,2.2f},{-.4f,side*.12f,side*.1f},2,CreatureRole::Crest,CreatureShape::Horn,-1,side);
    }
    assert(out.count==CreaturePartCount);
    out.core=shoulder+Vec{0,-.2f,-2.3f};
    // Grow in world space, around the front of the encounter, not the camera.
    auto expand=[](Vec v){return Vec{v.x*CreatureScale,v.y*CreatureScale,9+(v.z-9)*CreatureScale};};
    for(int i=0;i<out.count;++i){
        auto& piece=out.pieces[i];
        // Anatomical waves preserve the source of each wing and armor course.
        int tier=piece.segment>=0?piece.segment/4:CreatureWing(piece.role)?7+std::max(0,piece.layer):11;
        int slot=i%32;
        float order=piece.segment>=0?1.f-float(piece.segment)/25.f:
            CreatureWing(piece.role)?.25f+float(std::max(0,piece.span))/17.f*.55f:1.f;
        // Each part travels in 0.32 seconds; waves fill the unchanged 8s cycle.
        float depart=.075f+order*.13f;
        float arrive=.625f+order*.265f;
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
// A fixed flank gives the serpent a destination it can commit to before attacking.
inline Vec CreatureCruisePoint(Vec player,Vec boss,int step,bool ink){
    float side=step%2==0?1.f:-1.f;
    if(ink){
        Vec away=Unit(Vec{boss.x-player.x,0,boss.z-player.z});
        if(Length(away)<.1f)away={0,0,1};
        Vec flank{away.z,0,-away.x},best{};float bestScore=-1e9f;
        for(int i=0;i<12;++i){
            float angle=i*6.283185f/12.f;
            Vec direction=away*std::cos(angle)+flank*std::sin(angle);
            Vec at{std::clamp(player.x+direction.x*185.f,-300.f,300.f),0,
                std::clamp(player.z+direction.z*185.f,-305.f,345.f)};
            Vec delta=at-Vec{player.x,0,player.z};float distance=Length(delta);
            float score=-std::abs(distance-185.f)+Dot(Unit(delta),away)*24.f+Dot(Unit(delta),flank)*side*5.f;
            if(score>bestScore){bestScore=score;best=at;}
        }
        return best;
    }
    return {std::clamp(player.x+side*(step%4<2?85.f:65.f),-200.f,200.f),0,
        std::clamp(player.z+48.f+(step%3-1)*18.f,-220.f,220.f)};
}
inline Vec CreatureCruiseVelocity(Vec current,Vec delta,float dt,float speed){
    float distance=Length(delta);
    Vec desired=distance<1.f?Vec{}:Unit(delta)*std::min(speed,distance*2.2f);
    return Lerp(current,desired,1-std::exp(-6.f*std::max(0.f,dt)));
}
// Body sections depart from tail to head; wing pieces follow their span.
inline Vec ReformPart(Vec from,Vec to,const CreaturePiece& piece,int index,float progress){
    float delay=piece.segment>=0?(1.f-float(piece.segment)/25.f)*.16f:
        CreatureWing(piece.role)?.035f+float(std::max(0,piece.span))/17.f*.14f:.17f;
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
    Vec start{std::clamp(player.x,-328.f,328.f),3,std::clamp(player.z,-338.f,378.f)};
    int count=std::max(5,static_cast<int>(std::ceil(Length(core-start)/7.f)));
    std::vector<Vec> route(static_cast<size_t>(count));
    for(int i=0;i<count;++i){float u=static_cast<float>(i+1)/(count+1);
        route[i]=Lerp(start,core,u)+Vec{std::sin(u*6.283185f)*1.5f,0,0};}
    return route;
}
}
