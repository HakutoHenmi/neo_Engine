#include "ChronoSystem.h"
#include "../Scenes/GameScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Model.h"
#include "../../Engine/Audio.h"
#include "../../externals/nlohmann/json.hpp"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <limits>
#include <cstdio>

namespace Game {
using namespace Chrono;
namespace {
Vec Read(const DirectX::XMFLOAT3& p){return {p.x,p.y,p.z};}
DirectX::XMFLOAT3 Write(Vec p){return {p.x,p.y,p.z};}
Engine::Vector3 EV(Vec p){return {p.x,p.y,p.z};}
Vec Forward(float yaw,float pitch){return {std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};}
bool Down(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}
constexpr const char* grass="Resources/Models/Quaternius/Modular Platforms/Single Cube/FBX/Cube_Grass_Single.fbx";
constexpr const char* horn="Resources/Models/Quaternius/Enemies/FBX/Enemy.fbx";
constexpr const char* ball="Resources/Models/player_ball/ball.obj";
std::filesystem::path RecordPath(){
    wchar_t buffer[32768]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    return n>0 && n<32768 ? std::filesystem::path(buffer)/L"LiquidTime"/L"records-v1.json" : std::filesystem::path{};
}
std::string TimeText(float seconds){char b[32];int s=static_cast<int>(seconds);std::snprintf(b,sizeof(b),"%02d:%02d.%02d",s/60,s%60,static_cast<int>(seconds*100)%100);return b;}
}

entt::entity ChronoSystem::Mesh(entt::registry& r,const std::string& name,const std::string& path,V center,V size){
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name);
    auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelPath=path;
    mr.modelHandle=Engine::Renderer::GetInstance()->LoadObjMesh(path);mr.useCubemap=false;
    auto& t=r.emplace<TransformComponent>(e);t.translate=Write(center);
    auto* model=Engine::Renderer::GetInstance()->GetModel(mr.modelHandle);
    if(model && !model->GetData().vertices.empty()){
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:model->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        V scale{size.x/std::max(hi.x-lo.x,0.001f),size.y/std::max(hi.y-lo.y,0.001f),size.z/std::max(hi.z-lo.z,0.001f)};
        V mid=(lo+hi)*0.5f;t.scale=Write(scale);
        t.translate=Write(center-V{mid.x*scale.x,mid.y*scale.y,mid.z*scale.z});
    }
    if(path==grass || path.find("/Nature/")!=std::string::npos)
        r.emplace<CameraOccluder>(e,center,size,1.0f);
    return e;
}
void ChronoSystem::Block(entt::registry& r,const std::string& name,V center,V size){
    Mesh(r,name,grass,center,size);
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name+" collision");
    r.emplace<TransformComponent>(e).translate=Write(center);
    r.emplace<BoxColliderComponent>(e).size=Write(size);
    r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
}
void ChronoSystem::Build(entt::registry& r){
    Block(r,"Valley",{0,-4,12},{110,8,130});
    Block(r,"West terrace",{-16,2,2},{14,4,16});
    Block(r,"East terrace",{16,4,15},{14,8,16});
    Block(r,"Upper terrace",{-10,6,29},{16,12,14});
    Block(r,"Citadel",{0,9,49},{28,18,24});
    // Broad steps provide a recovery route even with no enemies remaining.
    for(int i=0;i<36;++i){float y=static_cast<float>(i+1)*0.5f;
        Block(r,"Stair "+std::to_string(i),{31,y*0.5f,-2+static_cast<float>(i)*1.35f},{8,y,1.42f});}
    Block(r,"Citadel landing",{24,9,49},{22,18,10});
    // Single-use shortcuts supplement the permanent stairs and landings.
    // Anchors approach from in front of each cliff; the landing behind catches a missed follow-up.
    Block(r,"Recovery lower",{-25,2.5f,17},{9,5,9});
    Block(r,"Recovery middle",{-23,5,29},{9,10,9});
    Block(r,"Recovery upper",{-19,7.5f,40},{9,15,9});
    for(int i=0;i<8;++i){float a=static_cast<float>(i)*0.785398f;
        Mesh(r,"Mountain "+std::to_string(i),"Resources/Models/Quaternius/Nature/FBX/RockPlatforms_Large.fbx",
            {std::sin(a)*66,12+static_cast<float>(i%3)*4,16+std::cos(a)*76},{26,38+static_cast<float>(i%3)*9,26});}
    const V trees[]={{-21,4,-1},{20,8,19},{-15,12,30},{-28,0,23},{30,0,-12},{-26,0,-20}};
    for(int i=0;i<6;++i)Mesh(r,"Tree "+std::to_string(i),"Resources/Models/Quaternius/Nature/FBX/Tree.fbx",trees[i]+V{0,4,0},{4,8,4});
    const V anchors[]={{-16,6,0},{16,10,12},{-10,14,27},{0,20,39},{24,2,-8},
        {-25,9,10.5f},{-23,14,22},{-19,19,34},{-8,22,35}};
    for(int i=0;i<9;++i){
        auto e=Mesh(r,"Anchor "+std::to_string(i),ball,anchors[i],{1.4f,1.8f,1.4f});
        auto& target=r.emplace<Target>(e);target.kind=Kind::Anchor;target.radius=1.1f;target.reward=0;
        target.offset=anchors[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Anchor>(e);r.get<MeshRendererComponent>(e).color={0.1f,1.2f,1.4f,1};
        if(i>=5)r.get<MeshRendererComponent>(e).color={.2f,1.4f,.6f,1};
    }
    const V enemies[]={{-5,1.4f,-19},{4,1.4f,-15},{0,1.4f,-7},{-6,1.4f,2},{5,1.4f,6},{0,1.4f,15},
        {-16,5.4f,5},{16,9.4f,16},{-10,13.4f,29}};
    for(int i=0;i<9;++i){
        auto e=Mesh(r,"Horned hopper "+std::to_string(i),horn,enemies[i],{2.4f,2.8f,2.4f});
        auto& target=r.emplace<Target>(e);target.radius=1.4f;target.hp=42;target.reward=18;
        target.offset=enemies[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Hopper>(e).timer=static_cast<float>(i%4)*0.18f;
        r.emplace<TagComponent>(e).tag=TagType::Enemy;
    }
    if(r.valid(boss_)){
        auto& t=r.get<TransformComponent>(boss_);t.translate={0,18,49};t.rotate={0,3.14159f,0};
        r.emplace_or_replace<Boss>(boss_);
        auto& bt=r.emplace_or_replace<Target>(boss_);bt.kind=Kind::Boss;bt.radius=3;bt.hp=360;bt.offset={0,4,0};bt.reward=0;
        if(auto* a=r.try_get<AnimatorComponent>(boss_)){
            auto* m=Engine::Renderer::GetInstance()->GetModel(r.get<MeshRendererComponent>(boss_).modelHandle);
            if(m && !m->GetData().animations.empty()){
                const auto& data=m->GetData();const auto& clip=data.animations.front();
                a->currentAnimation=clip.name;a->time=0;a->isPlaying=true;a->loop=true;
                // Animated FBX units can differ from the static imported bounds.
                // Fit the evaluated pose to the ten-unit citadel guardian.
                std::vector<Engine::Matrix4x4> palette(data.bones.size(),Engine::Matrix4x4::Identity());
                m->UpdateSkeleton(data.rootNode,Engine::Matrix4x4::Identity(),clip,0,nullptr,0,0,palette,nullptr);
                V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
                for(const auto& v:data.vertices){
                    using namespace DirectX;XMVECTOR point=XMLoadFloat4(&v.position),skin=XMVectorZero();float weight=0;
                    for(int j=0;j<4;++j)if(v.boneWeights[j]>0.0001f&&v.boneIndices[j]<palette.size()&&v.boneIndices[j]<128){
                        auto matrix=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&palette[v.boneIndices[j]]));
                        skin=XMVectorAdd(skin,XMVectorScale(XMVector4Transform(point,matrix),v.boneWeights[j]));weight+=v.boneWeights[j];
                    }
                    XMFLOAT3 at;XMStoreFloat3(&at,weight>0.001f?XMVectorScale(skin,1/weight):point);
                    lo={std::min(lo.x,at.x),std::min(lo.y,at.y),std::min(lo.z,at.z)};
                    hi={std::max(hi.x,at.x),std::max(hi.y,at.y),std::max(hi.z,at.z)};
                }
                if(hi.y>lo.y){float size=10/(hi.y-lo.y);V mid=(lo+hi)*0.5f;
                    t.scale={size,size,size};t.translate={mid.x*size,18-lo.y*size,49+mid.z*size};
                    bt.offset=V{0,23,49}-Read(t.translate);
                }
            }
        }
    }
    // Invisible targeting volume fitted to the guardian's torso, not a floating orb.
    weakpoint_=Mesh(r,"Weakpoint",ball,{0,23,49},{6,6,6});
    auto& w=r.emplace<Target>(weakpoint_);w.kind=Kind::Weakpoint;w.radius=3;w.reward=0;w.active=false;
    w.offset=V{0,23,49}-Read(r.get<TransformComponent>(weakpoint_).translate);
    r.get<MeshRendererComponent>(weakpoint_).enabled=false;
    if(arena_)BuildArena(r);
}
void ChronoSystem::BuildArena(entt::registry& r){
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(e!=weakpoint_&&e!=boss_&&
        r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    Block(r,"Arena floor",{0,-2,0},{64,4,64});
    Block(r,"Arena north",{0,3,32},{64,6,2});Block(r,"Arena south",{0,3,-32},{64,6,2});
    Block(r,"Arena west",{-32,3,0},{2,6,64});Block(r,"Arena east",{32,3,0},{2,6,64});
    Block(r,"Arena pillar",{18,3,5},{3,6,3});Block(r,"Arena step",{-18,.25f,5},{5,.5f,5});
    const V enemies[]={{-7,1.4f,-4},{7,1.4f,-2},{0,1.4f,2}};
    for(int i=0;i<3;++i){auto e=Mesh(r,"Arena hopper "+std::to_string(i),horn,enemies[i],{2.4f,2.8f,2.4f});
        auto& tg=r.emplace<Target>(e);tg.radius=1.4f;tg.hp=42;tg.reward=18;
        tg.offset=enemies[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Hopper>(e).timer=i*.3f;r.emplace<TagComponent>(e).tag=TagType::Enemy;}
    const V anchors[]={{-12,3,-9},{12,3,-9},{-12,3,13},{12,3,13}};
    for(int i=0;i<4;++i){auto e=Mesh(r,"Arena anchor "+std::to_string(i),ball,anchors[i],{1.4f,1.8f,1.4f});
        auto& tg=r.emplace<Target>(e);tg.kind=Kind::Anchor;tg.radius=1.1f;tg.reward=0;
        tg.offset=anchors[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Anchor>(e).reusable=true;r.get<MeshRendererComponent>(e).color={.15f,1.2f,.7f,1};}
    if(r.valid(boss_)){
        V shift=V{0,5,10}-Center(r,boss_);auto& t=r.get<TransformComponent>(boss_);t.translate=Write(Read(t.translate)+shift);
        auto& b=r.get<Boss>(boss_);b.engaged=true;
        r.get<Target>(weakpoint_).offset=Center(r,boss_)-Read(r.get<TransformComponent>(weakpoint_).translate);
    }
    r.get<TransformComponent>(player_).translate={0,1.3f,-12};
    auto& p=r.get<Player>(player_);p.recoveryPoint={0,1.3f,-12};p.automatic=true;
    pitch_=.28f;
}
void ChronoSystem::Reset(entt::registry& r){
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    player_=boss_=entt::null;
    for(auto e:r.view<NameComponent>()){
        const auto& n=r.get<NameComponent>(e).name;if(n=="Player")player_=e;if(n=="Boss")boss_=e;
    }
    initialized_=r.valid(player_);finished_=won_=false;finishAge_=0;sparks_.clear();
    prevL_=Down(VK_LBUTTON);prevM_=Down(VK_MBUTTON);prevSpace_=Down(VK_SPACE);prevShift_=Down(VK_SHIFT);
    yaw_=0;pitch_=0.12f;zoom_=17;shoulder_=1.8f;chainCameraHold_=0;fov_=1.0472f;
    cameraReady_=false;composition_={};viewPitch_=pitch_;postStrength_=0;droplets_.clear();manualCameraHold_=0;
    if(!initialized_)return;
    r.emplace_or_replace<Player>(player_);
    auto& pt=r.get<TransformComponent>(player_);pt.translate={0,1.3f,-27};pt.scale={1,1,1};
    auto& hp=r.get<HealthComponent>(player_);hp.hp=100;hp.maxHp=200;hp.isDead=false;
    auto& pi=r.get<PlayerInputComponent>(player_);pi.selectedCan=CanType::None;pi.lockedEnemy=entt::null;pi.isRadialMenuOpen=false;
    if(auto* box=r.try_get<BoxColliderComponent>(player_))box->enabled=false;
    auto* renderer=Engine::Renderer::GetInstance();white_=renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    sphere_=renderer->LoadObjMesh(ball);
    if(hitSound_==0xffffffff)hitSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/suittikirikae.mp3");
    if(coreSound_==0xffffffff)coreSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/kettei.mp3");
    renderer->SetPostEffect("Default");renderer->SetPostProcessParams({});
    Build(r);CacheSolids(r);
    bestTime_=0;bestChain_=0;newTime_=newChain_=false;
    try{std::ifstream file(RecordPath());if(file){nlohmann::json j;file>>j;bestTime_=j.value("time",0.0f);bestChain_=j.value("chain",0);}}catch(...){}
    renderer->SetAmbientColor({0.65f,0.68f,0.75f});
}
void ChronoSystem::CacheSolids(entt::registry& r){
    solids_.clear();
    for(auto e:r.view<Solid,BoxColliderComponent,TransformComponent>()){
        const auto& b=r.get<BoxColliderComponent>(e);const auto& t=r.get<TransformComponent>(e);
        V c=Read(t.translate)+Read(b.center),h=Read(b.size)*0.5f;solids_.push_back({c-h,c+h});
    }
}
bool ChronoSystem::Sweep(V p,V d,V half,float& fraction,V& normal)const{
    bool found=false;fraction=1;for(const auto& box:solids_){float f;V n;
        if(Chrono::Sweep(p,d,box,half,f,n)&&f<fraction){fraction=f;normal=n;found=true;}}
    return found;
}
bool ChronoSystem::Clear(V a,V b,float radius)const{float f;V n;return !Sweep(a,b-a,{radius,radius,radius},f,n);}
ChronoSystem::V ChronoSystem::Move(V p,V d,V half,bool& grounded,V* velocity)const{
    grounded=false;
    for(int i=0;i<3 && Length(d)>1e-5f;++i){float f;V n;
        if(!Sweep(p,d,half,f,n)){p=p+d;break;}
        p=p+d*std::max(0.0f,f-0.001f);if(n.y>0.5f)grounded=true;
        if(Length(n)<0.5f)break;
        d=d*(1-f);d=d-n*Dot(d,n);
        if(velocity)*velocity=*velocity-n*std::min(0.0f,Dot(*velocity,n));
    }
    float f;V n;if(Sweep(p,{0,-0.06f,0},half,f,n)&&n.y>0.5f)grounded=true;
    return p;
}
ChronoSystem::V ChronoSystem::Center(entt::registry& r,entt::entity e)const{
    return Read(r.get<TransformComponent>(e).translate)+r.get<Target>(e).offset;
}
entt::entity ChronoSystem::Choose(entt::registry& r,Player& p,GameContext& ctx,bool manual){
    V origin=Read(r.get<TransformComponent>(player_).translate),forward=Forward(yaw_,pitch_);
    V cam=Read(ctx.camera->Position());float score=-1e9f,nearestRay=1e9f;entt::entity best=entt::null;
    for(auto e:r.view<Target,TransformComponent>()){
        const auto& t=r.get<Target>(e);if(!t.active || t.kind==Kind::Boss)continue;
        V c=Center(r,e),delta=c-origin;float distance=Length(delta);
        if(distance>Reach(p.mass)+t.radius || !Reachable(r,p,e,origin))continue;
        if(manual){float hit;
            if(RaySphere(cam,forward,c,t.radius,100,hit)&&hit<nearestRay){nearestRay=hit;best=e;}
        }else{
            // A forward hemisphere prevents unexpected backwards/ground snaps.
            float alignment=Dot(Unit(c-cam),forward);if(alignment<0.55f)continue;
            V motion=Unit(p.velocity);float s=alignment*35-distance*0.06f+Dot(Unit(delta),motion)*0.6f;
            if(t.kind==Kind::Anchor)s-=1.2f;if(t.kind==Kind::Weakpoint)s+=6;
            if(e==p.preview)s+=2.0f; // Hysteresis keeps a nearby hopper from stealing the intended target.
            if(s>score){score=s;best=e;}
        }
    }
    return best;
}
bool ChronoSystem::Reachable(entt::registry& r,const Player& p,entt::entity e,V origin,V* lift) const{
    const auto& tg=r.get<Target>(e);V c=Center(r,e);
    float radius=std::max(.55f,p.collisionScale*.9f);
    V end=tg.kind==Kind::Anchor?c+V{0,1.7f,0}:c-Unit(c-origin)*(tg.radius+radius+.15f);
    float f;V n;V half{radius,.8f+p.collisionScale*.4f,radius};
    if(lift)*lift=origin;
    if(!Clear(origin,c,.15f))return false;
    if(!Sweep(origin,end-origin,half,f,n))return true;
    V raised=origin+V{0,.56f,0};
    if(p.grounded&&!Sweep(origin,raised-origin,half,f,n)&&!Sweep(raised,end-raised,half,f,n)){
        if(lift)*lift=raised;return true;
    }
    return false;
}
void ChronoSystem::Shoot(entt::registry& r,Player& p,GameContext& ctx){
    V origin=Read(r.get<TransformComponent>(player_).translate);p.shotManual=p.bufferedManual;
    p.target=p.bufferedTarget;p.bufferedTarget=entt::null;p.buffer=0;
    if(r.valid(p.target)&&(!r.get<Target>(p.target).active||!Reachable(r,p,p.target,origin)||
        Length(Center(r,p.target)-origin)>Reach(p.mass)+r.get<Target>(p.target).radius)){
        p.target=entt::null;p.failure=ChainFailure::Obstructed;p.failureTime=.6f;return;
    }
    p.failure=r.valid(p.target)?ChainFailure::None:Failure(r,p,ctx);
    p.failureTime=p.failure==ChainFailure::None?0:0.8f;
    if(!p.shotManual&&p.automatic&&!r.valid(p.target))return;
    p.shotStart=origin;p.hand=origin;p.shotDistance=0;p.shotLimit=Reach(p.mass);
    if(p.shotManual){
        V cam=Read(ctx.camera->Position()),dir=Forward(yaw_,pitch_);
        V aim=cam+dir*100;float wall;V n;if(Sweep(cam,dir*100,{},wall,n))aim=cam+dir*(100*wall);
        if(r.valid(p.target))aim=Center(r,p.target);
        p.shotDirection=Unit(aim-origin);
    }else if(r.valid(p.target))p.shotDirection=Unit(Center(r,p.target)-origin);
    else {p.shotDirection=Forward(yaw_,0);p.shotLimit=3.3f;}
    p.action=Action::Extending;p.timer=0;p.buffer=0;
}
void ChronoSystem::Damage(entt::registry& r,Player& p,float amount,GameContext& ctx,V source){
    if(p.invincible>0||finished_)return;
    p.mass=std::max(0.0f,p.mass-amount);p.invincible=0.65f;++p.stats.hitsTaken;p.flow.Break();
    p.damageSource=source;p.damageAge=0;
    p.buffer=0;p.bufferedTarget=entt::null;
    if(p.action!=Action::Collapsed){p.action=Action::Recovery;p.timer=0.12f;p.target=entt::null;}
    ctx.camera->StartShake(0.1f,0.12f);
    r.get<HealthComponent>(player_).hp=p.mass;
    if(p.mass<=0)Finish(p,false);
}
ChainFailure ChronoSystem::Failure(entt::registry& r,Player& p,GameContext& ctx){
    V origin=Read(r.get<TransformComponent>(player_).translate),cam=Read(ctx.camera->Position());
    V forward=Forward(yaw_,pitch_);float best=-1;ChainFailure reason=ChainFailure::NoTarget;
    for(auto e:r.view<Target,TransformComponent>()){
        const auto& tg=r.get<Target>(e);if(!tg.active||tg.kind==Kind::Boss)continue;
        V c=Center(r,e);float alignment=Dot(Unit(c-cam),forward),hit;
        if(p.aiming&&!RaySphere(cam,forward,c,tg.radius,100,hit))continue;
        if(alignment<0.55f||alignment<=best)continue;best=alignment;
        reason=Length(c-origin)>Reach(p.mass)+tg.radius?ChainFailure::OutOfRange:
            !Reachable(r,p,e,origin)?ChainFailure::Obstructed:ChainFailure::None;
    }
    return reason;
}
void ChronoSystem::Feedback(Player& p,V at,bool heavy){
    p.hitStop=std::max(p.hitStop,heavy?0.065f:0.035f);
    for(int i=0;i<(heavy?18:10)&&droplets_.size()<96;++i){
        float angle=i*2.39996f;float speed=heavy?8.0f:5.0f;
        droplets_.push_back({at,{std::cos(angle)*speed,2.0f+(i%4)*1.2f,std::sin(angle)*speed},0});
    }
    auto* audio=Engine::Audio::GetInstance();
    audio->Play(heavy?coreSound_:hitSound_,false,0.3f*audio->GetMasterSEVolume(),
        std::min(1.8f,1.0f+0.065f*p.flow.combo));
}
void ChronoSystem::Impact(entt::registry& r,Player& p,entt::entity e,GameContext& ctx,bool grabbed){
    if(!r.valid(e)||!r.all_of<Target>(e))return;
    auto& t=r.get<Target>(e);if(!t.active)return;
    if(t.kind==Kind::Anchor){
        // Consume only on arrival; an interrupted or obstructed pull can be retried.
        if(grabbed){
            t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
            if(auto* a=r.try_get<Anchor>(e);a&&a->reusable)a->cooldown=1.2f;
            if(sparks_.size()<64)sparks_.push_back({Center(r,e),Read(r.get<TransformComponent>(player_).translate),0});
        }
        return;
    }
    if(t.kind==Kind::Weakpoint){
        auto& b=r.get<Boss>(boss_);
        if(b.countered||b.waveBreaks<3)return;
        b.countered=true;t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
        auto& bt=r.get<Target>(boss_);bt.hp-=120;++p.stats.counters;
        p.instability=std::max(0.0f,p.instability-55);p.mass=std::min(200.0f,p.mass+25);
        p.flow.grace=2;ctx.camera->StartShake(0.10f,p.aiming?0.025f:0.08f);
        Feedback(p,Center(r,e),true);
        b.phase=3;b.clock=0;
        if(bt.hp<=0)Finish(p,true);
        return;
    }
    const float damage=(grabbed?46.0f:24.0f)*(0.8f+p.mass*0.002f);
    t.hp-=t.kind==Kind::Boss ? damage*0.18f : damage;
    if(t.kind==Kind::Boss){if(t.hp<=0)Finish(p,true);return;}
    bool defeated=t.hp<=0;p.flow.Hit(defeated);
    Feedback(p,Center(r,e),false);
    if(!p.aiming)ctx.camera->StartShake(0.035f,defeated?0.035f:0.015f);
    if(!defeated)return;
    V c=Center(r,e);t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
    p.mass=std::min(200.0f,p.mass+t.reward);p.stats.absorbed+=t.reward;
    if(!p.grounded){++p.stats.airKills;p.stats.maxAirKills=std::max(p.stats.maxAirKills,p.stats.airKills);}
    if(t.kind==Kind::Projectile){++p.stats.projectiles;auto& b=r.get<Boss>(boss_);if(t.wave==b.wave)++b.waveBreaks;}
    if(sparks_.size()<64)sparks_.push_back({c,Read(r.get<TransformComponent>(player_).translate),0});
}

void ChronoSystem::Camera(entt::registry& r,Player& p,GameContext& ctx,float dt){
    manualCameraHold_=std::max(0.0f,manualCameraHold_-dt);
    bool combat=r.valid(boss_)&&r.get<Boss>(boss_).engaged;
    bool assist=combat&&!p.aiming&&manualCameraHold_<=0;
    bool chaining=p.shotLimit>4 && (p.action==Action::Extending||p.action==Action::Pulling||p.action==Action::Retracting);
    if(chaining)chainCameraHold_=0.35f;else chainCameraHold_=std::max(0.0f,chainCameraHold_-dt);
    bool wide=chainCameraHold_>0;
    float scale=std::max(1.0f,BodyScale(p.mass));
    float desiredZoom=(p.aiming?(wide?16.0f:12.0f):(combat?25.0f:wide?22.0f:19.0f))*scale;
    float desiredShoulder=(p.aiming?4.6f:(wide?4.2f:1.8f))*scale;
    float blend=1-std::exp(-9*dt);
    zoom_+=(desiredZoom-zoom_)*blend;shoulder_+=(desiredShoulder-shoulder_)*blend;
    V pos=Read(r.get<TransformComponent>(player_).translate);
    if(!cameraReady_||Length(pos-cameraFollow_)>35){cameraFollow_=pos;cameraReady_=true;composition_={};}
    if(dt>0){float oldY=cameraFollow_.y;cameraFollow_=Follow(cameraFollow_,pos,dt,2.2f*scale);
        if(!p.aiming)cameraFollow_.y=std::clamp(oldY+(pos.y-oldY)*(1-std::exp(-4*dt)),pos.y-5*scale,pos.y+5*scale);}
    V aimOffset{};
    if(combat&&!p.aiming){
        V delta=Center(r,boss_)-(pos+V{0,2.8f*scale,0});
        aimOffset=Unit(delta)*std::min(6.0f,Length(delta)*.30f);
        if(assist&&Length(V{delta.x,0,delta.z})>4){
            float error=std::remainder(std::atan2(delta.x,delta.z)-yaw_,6.283185f);
            yaw_+=std::clamp(error,-.8f,.8f)*(1-std::exp(-2*dt));
        }
    }
    composition_=Lerp(composition_,aimOffset,1-std::exp(-7*dt));
    V focus=cameraFollow_+V{0,2.8f*scale,0}+composition_;
    float goalPitch=pitch_;
    if(assist)goalPitch=.28f;
    // Manual aim remains exactly under the crosshair. Automatic framing never
    // changes the player's selection direction, avoiding target/camera feedback.
    viewPitch_=p.aiming?pitch_:viewPitch_+(goalPitch-viewPitch_)*(1-std::exp(-9*dt));
    V right{std::cos(yaw_),0,-std::sin(yaw_)};
    V orbit=Forward(yaw_,viewPitch_);
    V desired=focus-orbit*zoom_+right*shoulder_;
    // Preserve the full orbit while airborne. At ground level, lift the camera
    // onto the floor instead of shortening the boom into the player's body.
    float f;V n;
    if(desired.y<focus.y){
        V above{desired.x,focus.y,desired.z};float drop=focus.y-desired.y;
        if(Sweep(above,{0,-drop,0},{0.35f,0.35f,0.35f},f,n)&&n.y>0.5f)desired.y=above.y-drop*f+0.05f;
    }
    if(Sweep(focus,desired-focus,{0.35f,0.35f,0.35f},f,n))desired=focus+(desired-focus)*std::max(0.0f,f-0.02f);
    if(Length(desired-pos)<5.0f*scale){
        V raised=focus-Forward(yaw_,0)*(8.0f*scale)+right*shoulder_+V{0,3*scale,0};
        if(!Sweep(focus,raised-focus,{0.35f,0.35f,0.35f},f,n))desired=raised;
    }
    // A floor-clamped upward orbit needs extra vertical space for the body and
    // the root of the arm. Keep the reticle's ray unchanged; widen the lens.
    V up{std::sin(yaw_)*std::sin(viewPitch_),std::cos(viewPitch_),std::cos(yaw_)*std::sin(viewPitch_)};
    V body=pos+V{0,0.7f*scale,0}-desired;
    float depth=std::max(0.1f,Dot(body,orbit));
    float bodyAngle=std::abs(std::atan2(Dot(body,up),depth));
    float required=2*(bodyAngle+std::atan2(1.8f*scale,Length(body)));
    if(chaining&&r.valid(p.target)){
        V next=Center(r,p.target)-desired;float z=std::max(0.1f,Dot(next,orbit));
        float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.f/9;
        float halfAngle=std::max(std::abs(std::atan2(Dot(next,up),z)),
            std::atan(std::abs(Dot(next,right))/z/aspect));
        required=std::max(required,2*(halfAngle+0.13f));
    }
    if(combat&&!p.aiming){
        V boss=Center(r,boss_)-desired;float z=Dot(boss,orbit);
        if(z>1){float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.f/9;
            required=std::max(required,2*(std::max(std::abs(std::atan2(Dot(boss,up),z)),
                std::atan(std::abs(Dot(boss,right))/z/aspect))+std::atan2(6.0f,Length(boss))));}
    }
    float desiredFov=std::clamp(required,wide?1.2217f:1.0472f,1.7453f);
    fov_+=(desiredFov-fov_)*(1-std::exp(-(desiredFov>fov_?18.0f:4.0f)*dt));
    float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.0f/9.0f;
    ctx.camera->SetProjection(fov_,aspect,0.1f,10000.0f);
    ctx.camera->SetRotation(viewPitch_,yaw_,0);ctx.camera->SetPosition(Write(desired));
    p.cameraOpacity=std::clamp((Length(desired-pos)-2.5f*scale)/(4.0f*scale),0.18f,1.0f);
    for(auto e:r.view<Hopper,Target,TransformComponent>()){
        auto& o=r.get_or_emplace<CameraOccluder>(e);o.center=Center(r,e);o.size={2.4f,2.8f,2.4f};
    }
    if(dt>0)for(auto e:r.view<CameraOccluder>()){
        auto& o=r.get<CameraOccluder>(e);float fraction;V normal;
        bool hidesBody=Chrono::Sweep(desired,pos+V{0,scale,0}-desired,
            {o.center-o.size*0.5f,o.center+o.size*0.5f},{0.3f,0.3f,0.3f},fraction,normal);
        bool hidesBoss=combat&&e!=boss_&&Chrono::Sweep(desired,Center(r,boss_)-desired,
            {o.center-o.size*.5f,o.center+o.size*.5f},{.5f,.5f,.5f},fraction,normal);
        bool close=r.all_of<Hopper>(e)&&Length(o.center-desired)<Length(o.size)*.6f+2;
        o.opacity+=((hidesBody||hidesBoss||close?0.18f:1.0f)-o.opacity)*(1-std::exp(-12*dt));
    }
}
void ChronoSystem::UpdatePlayer(entt::registry& r,Player& p,GameContext& ctx){
    const float dt=ctx.dt;
    const auto* control=r.try_get<ControlFrame>(player_);
    bool left=control?control->attack:Down(VK_LBUTTON),middle=control?control->toggle:Down(VK_MBUTTON);
    bool jump=control?control->jump:Down(VK_SPACE),dodge=control?control->dodge:Down(VK_SHIFT);
    if(middle&&!prevM_)p.automatic=!p.automatic;
    bool attackPressed=left&&!prevL_;
    if(!left){p.buffer=0;p.bufferedTarget=entt::null;}
    if(jump&&!prevSpace_)p.jumpBuffer=0.14f;
    bool dodgePressed=dodge&&!prevShift_;
    prevL_=left;prevM_=middle;prevSpace_=jump;prevShift_=dodge;
    p.aiming=control?control->aim:Down(VK_RBUTTON);
    if(control){yaw_=control->yaw;pitch_=control->pitch;if(control->cameraInput)manualCameraHold_=1.5f;}
    else if(ctx.input){float sens=p.aiming?0.0018f:0.003f;
        if(std::abs(ctx.input->GetMouseDeltaX())+std::abs(ctx.input->GetMouseDeltaY())>0.01f)manualCameraHold_=1.5f;
        yaw_+=ctx.input->GetMouseDeltaX()*sens;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*sens,-1.35f,1.3f);}
    Camera(r,p,ctx,0);
    if(attackPressed){p.buffer=.3f;p.bufferedManual=p.aiming;
        p.bufferedTarget=(p.aiming||p.automatic)?Choose(r,p,ctx,p.aiming):entt::null;}
    V input{static_cast<float>(Down('D'))-static_cast<float>(Down('A')),0,static_cast<float>(Down('W'))-static_cast<float>(Down('S'))};
    if(control)input=control->move;
    input=Unit(input);V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    if(dodgePressed){p.buffer=0;p.bufferedTarget=entt::null;p.dodgeBuffer=.2f;
        p.bufferedDodge=Length(wish)>.1f?wish:Forward(yaw_,0);}
    p.flow.Tick(dt);p.stats.seconds+=dt;
    p.aimScale=AimTimeScale(p.aimScale,p.aiming,dt);
    p.worldScale=WorldTimeScale(p.flow.scale,p.aimScale);
    p.buffer=std::max(0.0f,p.buffer-dt);p.jumpBuffer=std::max(0.0f,p.jumpBuffer-dt);
    p.invincible=std::max(0.0f,p.invincible-dt);p.cooldown=std::max(0.0f,p.cooldown-dt);
    p.failureTime=std::max(0.0f,p.failureTime-dt);p.damageAge+=dt;p.landingAge+=dt;
    if(p.buffer>0&&!r.valid(p.target)&&p.action!=Action::Free&&p.action!=Action::Extending&&p.action!=Action::Pulling){
        p.failure=p.action==Action::Collapsed?ChainFailure::Unstable:ChainFailure::Recovering;p.failureTime=0.3f;
    }
    // Keep reading/buffering input and moving the camera during impact freeze.
    // Timers use real time; actors and projectiles stay frozen for 2-4 frames.
    if(p.hitStop>0){p.hitStop=std::max(0.0f,p.hitStop-dt);Camera(r,p,ctx,dt);return;}
    auto& t=r.get<TransformComponent>(player_);V pos=Read(t.translate);
    float desiredScale=BodyScale(p.mass);
    if(desiredScale!=p.collisionScale){
        V adjusted=pos;
        if(p.grounded)adjusted.y+=(desiredScale-p.collisionScale)*0.4f+0.002f;
        float contact;V normal;float wide=std::max(0.55f,desiredScale*0.9f);
        if(desiredScale<p.collisionScale||!Sweep(adjusted,{0,0.0001f,0},{wide,0.8f+desiredScale*0.4f,wide},contact,normal)){
            pos=adjusted;p.collisionScale=desiredScale;
        }
    }
    float scale=p.collisionScale,radius=std::max(0.55f,scale*0.9f);V half{radius,0.8f+scale*0.4f,radius};
    if(p.grounded){p.coyote=0.1f;p.stats.airKills=0;}else p.coyote=std::max(0.0f,p.coyote-dt);
    if(p.dodgeBuffer>0&&p.cooldown==0&&p.action!=Action::Collapsed){
        p.action=Action::Dodge;p.timer=0.19f;p.cooldown=0.55f;p.invincible=0.15f;p.target=entt::null;
        p.dodgeDirection=p.bufferedDodge;p.buffer=0;p.dodgeBuffer=0;p.bufferedTarget=entt::null;
        p.failureTime=0;p.hand=pos;p.velocity.y=std::max(p.velocity.y,0.0f);p.airHang=.2f;
    }
    p.dodgeBuffer=std::max(0.0f,p.dodgeBuffer-dt);
    if(p.action==Action::Dodge||p.action==Action::Collapsed){p.buffer=0;p.bufferedTarget=entt::null;}
    if(p.action==Action::Free && p.buffer>0)Shoot(r,p,ctx);
    if(p.action==Action::Extending){
        // Auto steering happens only while the hand travels. Manual shots are
        // real rays: missed shots retract, and never move the body.
        if(!p.shotManual&&r.valid(p.target)&&r.get<Target>(p.target).active)p.shotDirection=Unit(Center(r,p.target)-p.hand);
        float step=std::min(110*dt,p.shotLimit-p.shotDistance);
        float nearest=step;entt::entity hit=entt::null;V dir=p.shotDirection;
        float wf;V wn;bool wall=Sweep(p.hand,dir*step,{0.08f,0.08f,0.08f},wf,wn);
        if(wall)nearest=step*wf;
        for(auto e:r.view<Target,TransformComponent>()){
            const auto& tg=r.get<Target>(e);if(!tg.active)continue;
            // The exposed torso owns this hit; the overlapping ordinary boss
            // volume must not steal manually aimed counter attacks.
            if(tg.kind==Kind::Boss&&r.valid(weakpoint_)&&r.get<Target>(weakpoint_).active)continue;
            if(!p.shotManual && p.target!=entt::null && e!=p.target)continue;
            float distance;if(RaySphere(p.hand,dir,Center(r,e),tg.radius,nearest,distance)) {nearest=distance;hit=e;}
        }
        p.hand=p.hand+dir*nearest;p.shotDistance+=nearest;
        if(hit!=entt::null){
            p.target=hit;auto& tg=r.get<Target>(hit);
            if(p.shotLimit<4){Impact(r,p,hit,ctx,false);p.action=Action::Recovery;p.timer=0.10f;}
            else {
                V c=Center(r,hit);p.endpoint=tg.kind==Kind::Anchor ? c+V{0,1.7f,0} : c-Unit(c-pos)*(tg.radius+radius+0.15f);
                if(!Reachable(r,p,hit,pos,&p.pullWaypoint)){p.action=Action::Retracting;p.timer=0.06f;p.failure=ChainFailure::Obstructed;p.failureTime=.8f;}
                else {
                    p.pullStep=Length(p.pullWaypoint-pos)>.01f;
                    p.action=Action::Pulling;p.timer=0;p.pullCost=StrainCost(p.mass,Length(p.endpoint-pos));
                    p.instability+=p.pullCost;if(p.shotManual)++p.stats.manual;else ++p.stats.automatic;
                    if(Overload(p.instability,p.mass)){p.action=Action::Collapsed;p.timer=0.6f;++p.stats.collapses;p.flow.Break();}
                }
            }
        }else if(wall||p.shotDistance>=p.shotLimit-0.001f){
            p.target=entt::null;p.action=Action::Retracting;p.timer=0.1f;
            if(wall||p.failure==ChainFailure::None)p.failure=wall?ChainFailure::Obstructed:ChainFailure::Missed;
            p.failureTime=.8f;
        }
    }
    if(p.action==Action::Pulling){
        p.timer+=dt;
        if(!r.valid(p.target)||!r.get<Target>(p.target).active||p.timer>0.65f){
            p.action=Action::Free;p.target=entt::null;p.failure=ChainFailure::OutOfRange;p.failureTime=.8f;
        }
        else {
            auto& tg=r.get<Target>(p.target);V c=Center(r,p.target);
            V end=tg.kind==Kind::Anchor?c+V{0,1.7f,0}:c-Unit(c-pos)*(tg.radius+radius+0.15f);
            bool stepping=p.pullStep;
            if(stepping)end=p.pullWaypoint;
            V delta=end-pos;float dist=Length(delta);float step=std::min(dist,PullSpeed(dist)*dt);
            float f;V n;
            if(Sweep(pos,Unit(delta)*step,half,f,n)){
                pos=pos+Unit(delta)*(step*std::max(0.0f,f-0.005f));p.action=Action::Recovery;p.timer=0.1f;p.target=entt::null;
                p.failure=ChainFailure::Obstructed;p.failureTime=.8f;
            }else{
                pos=pos+Unit(delta)*step;p.velocity={0,2.0f,0};p.grounded=false;p.hand=c;
                if(dist<=step+0.08f){
                    if(stepping){p.pullStep=false;}
                    else {
                    t.translate=Write(pos);Impact(r,p,p.target,ctx,true);
                    p.action=Action::Recovery;p.timer=0.055f;p.velocity={0,4,0};p.airHang=0.28f;
                    }
                }
            }
        }
    }else{
        if(p.action==Action::Recovery||p.action==Action::Retracting||p.action==Action::Dodge||p.action==Action::Collapsed){
            p.timer-=dt;
            if(p.action==Action::Retracting)p.hand=Lerp(p.hand,pos,std::min(1.0f,dt*25));
            if(p.timer<=0){p.action=Action::Free;p.target=entt::null;}
        }
        V desired=wish*15;float response=1-std::exp(-(p.grounded?35.0f:13.0f)*dt);
        p.velocity.x+=(desired.x-p.velocity.x)*response;p.velocity.z+=(desired.z-p.velocity.z)*response;
        if(p.action==Action::Dodge){p.velocity.x=p.dodgeDirection.x*32;p.velocity.z=p.dodgeDirection.z*32;}
        if(p.action==Action::Collapsed){p.velocity.x*=0.5f;p.velocity.z*=0.5f;}
        if(p.jumpBuffer>0&&p.coyote>0&&p.action!=Action::Collapsed){p.velocity.y=13;p.grounded=false;p.coyote=0;p.jumpBuffer=0;}
        p.airHang=std::max(0.0f,p.airHang-dt);
        p.velocity.y=std::max(-42.0f,p.velocity.y-Gravity(p.velocity.y,p.airHang>0)*dt);
        bool wasGrounded=p.grounded;float fallingSpeed=-p.velocity.y;
        // Half-unit block stairs remain walkable after the chain ends.
        bool stepped=false;
        V horizontal{p.velocity.x*dt,0,p.velocity.z*dt};
        float stepFraction;V stepNormal;
        if(p.grounded && Length(horizontal)>0.001f &&
           Sweep(pos,horizontal,half,stepFraction,stepNormal) && std::abs(stepNormal.y)<0.1f){
            V raised=pos+V{0,0.56f,0};
            if(!Sweep(pos,{0,0.56f,0},half,stepFraction,stepNormal) &&
               !Sweep(raised,horizontal,half,stepFraction,stepNormal) &&
               Sweep(raised+horizontal,{0,-0.62f,0},half,stepFraction,stepNormal) && stepNormal.y>0.5f){
                pos=raised+horizontal+V{0,-0.62f*stepFraction+0.001f,0};
                p.velocity.y=0;stepped=true;
            }
        }
        if(!stepped)pos=Move(pos,p.velocity*dt,half,p.grounded,&p.velocity);
        if(p.grounded&&!wasGrounded){
            p.landingAge=0;p.landingStrength=std::clamp(fallingSpeed/24,0.2f,1.0f);
            for(int i=0;i<8&&droplets_.size()<96;++i){float a=i*0.785398f;
                droplets_.push_back({pos-V{0,half.y-.15f,0},{std::cos(a)*4,2,std::sin(a)*4},0});}
        }
        if(p.grounded){
            // Only save places that have a complete body-width support area.
            for(const auto& b:solids_)if(pos.x>b.min.x+radius&&pos.x<b.max.x-radius&&pos.z>b.min.z+radius&&pos.z<b.max.z-radius&&std::abs(pos.y-half.y-b.max.y)<.12f)
                p.recoveryPoint=pos+V{0,.3f,0};
        }
        if(p.grounded&&p.action!=Action::Collapsed)p.instability=std::max(0.0f,p.instability-32*dt);
    }
    if(pos.y<-15){pos=p.recoveryPoint;p.velocity={};p.action=Action::Free;p.target=entt::null;
        p.mass=std::max(1.0f,p.mass-12);p.flow.Break();p.failure=ChainFailure::Fell;p.failureTime=1.4f;cameraReady_=false;}
    t.translate=Write(pos);
    if(Length(wish)>0.1f)t.rotate.y=std::atan2(wish.x,wish.z);
    // Visual volume follows mass; collision growth is applied only where it fits.
    t.scale={p.collisionScale,p.collisionScale,p.collisionScale};
    float squash=LandingSquash(p.landingAge,p.landingStrength);
    if(!p.grounded&&p.action!=Action::Pulling)squash+=std::min(.18f,std::abs(p.velocity.y)*.009f);
    t.scale.y*=squash;t.scale.x/=std::sqrt(squash);t.scale.z/=std::sqrt(squash);
    if(p.action==Action::Dodge){t.scale.x*=1.15f;t.scale.y*=0.65f;t.scale.z*=1.3f;}
    if(p.action==Action::Collapsed){t.scale.x*=1.4f;t.scale.y*=0.35f;t.scale.z*=1.4f;}
    p.stats.maxMass=std::max(p.stats.maxMass,p.mass);
    auto& hp=r.get<HealthComponent>(player_);hp.hp=p.mass;hp.maxHp=200;hp.isDead=p.mass<=0;
    Camera(r,p,ctx,dt);
    p.preview=(p.action==Action::Extending||p.action==Action::Pulling)?p.target:
        (p.aiming||p.automatic)?Choose(r,p,ctx,p.aiming):entt::null;
    p.blockedPreview=entt::null;
    if(!r.valid(p.preview)&&(p.aiming||p.automatic)){
        float best=.55f;V forward=Forward(yaw_,pitch_),cam=Read(ctx.camera->Position());
        for(auto e:r.view<Target,TransformComponent>()){
            const auto& tg=r.get<Target>(e);if(!tg.active||tg.kind==Kind::Boss)continue;
            V c=Center(r,e);float alignment=Dot(Unit(c-cam),forward),hit;
            if(p.aiming&&!RaySphere(cam,forward,c,tg.radius,100,hit))continue;
            if(alignment>best&&Length(c-pos)<=Reach(p.mass)+tg.radius&&!Reachable(r,p,e,pos)){
                best=alignment;p.blockedPreview=e;}
        }
    }
}

void ChronoSystem::UpdateBoss(entt::registry& r,Player& p,GameContext& ctx){
    if(!r.valid(boss_))return;
    auto& b=r.get<Boss>(boss_);V player=Read(r.get<TransformComponent>(player_).translate);
    if(!b.engaged){if(player.z>3)b.engaged=true;else return;}
    const float dt=ctx.dt*p.worldScale;b.clock+=dt;
    if(arena_){
        V c=Center(r,boss_);
        if(b.phase==0&&b.clock>1.4f){b.phase=1;b.clock=0;b.countered=false;b.attackHit=false;
            ++b.wave;++p.stats.opportunities;b.attackDirection=Unit(V{player.x-c.x,0,player.z-c.z});}
        bool charge=b.wave%2==0;
        if(b.phase==1&&b.clock>(charge?1.1f:.85f)){b.phase=2;b.clock=0;}
        if(b.phase==2){
            V before=c;
            if(charge){bool grounded;V next=Move(c,b.attackDirection*(24*dt),{3,4.8f,3},grounded);
                auto& t=r.get<TransformComponent>(boss_);t.translate=Write(Read(t.translate)+next-c);c=next;}
            float hit=0;V delta=c-before;
            bool contact=charge?(Length(delta)<.001f?Length(player-c)<4.4f:
                RaySphere(before,Unit(delta),player,4.4f,Length(delta),hit)):
                Length(player-(c+V{0,-2,0}))<7&&Dot(Unit(V{player.x-c.x,0,player.z-c.z}),b.attackDirection)>.1f;
            if(contact&&!b.attackHit){if(p.invincible<=0)p.damageReason=charge?"HIT - BOSS CHARGE":"HIT - BOSS SWEEP";
                Damage(r,p,charge?22.f:18.f,ctx,c);b.attackHit=true;}
            if(b.clock>(charge?.65f:.22f)){b.phase=3;b.clock=0;b.waveBreaks=3;}
        }
        r.get<Target>(weakpoint_).active=b.phase==3&&!b.countered;
        r.get<Target>(weakpoint_).offset=c-Read(r.get<TransformComponent>(weakpoint_).translate);
        if(b.phase==3&&b.clock>2.6f){b.phase=0;b.clock=0;}
        return;
    }
    if(b.phase==0 && b.clock>1.4f){b.phase=1;b.clock=0;b.emitted=0;b.waveBreaks=0;b.countered=false;++b.wave;++p.stats.opportunities;}
    auto spawn=[&](V pos,V velocity,bool breakable){
        auto e=Mesh(r,breakable?"Chain orb":"Hazard",ball,pos,breakable?V{2,2,2}:V{1,1,1});
        auto& target=r.emplace<Target>(e);target.kind=Kind::Projectile;target.radius=breakable?1.25f:0.65f;
        target.hp=1;target.wave=b.wave;target.reward=8;target.active=breakable;
        target.offset=pos-Read(r.get<TransformComponent>(e).translate);
        auto& projectile=r.emplace<Projectile>(e);projectile.velocity=velocity;projectile.breakable=breakable;projectile.lifetime=20;
        r.get<MeshRendererComponent>(e).color=breakable?DirectX::XMFLOAT4{0.15f,0.9f,1.8f,1}:DirectX::XMFLOAT4{2,0.12f,0.35f,1};
    };
    if(b.phase==1){
        const V route[]={{0,3,18},{-2,7,22},{1,12,26},{3,17,29},{1,22,32},{-1,26,37},{0,28,43}};
        while(b.emitted<7 && b.clock>=static_cast<float>(b.emitted)*0.08f){
            // A common drift preserves the staircase while still approaching.
            spawn(route[b.emitted],{0,0,-0.45f},true);++b.emitted;
        }
        if(b.clock>2.0f){
            for(int i=-1;i<=1;++i){V start{static_cast<float>(i)*9,23,40};spawn(start,Unit(player-start)*12,false);}
            b.phase=2;b.clock=0;
        }
    }
    bool exposed=(b.phase==1||b.phase==2) && b.waveBreaks>=3 && !b.countered;
    r.get<Target>(weakpoint_).active=exposed;
    r.get<Target>(weakpoint_).offset=Center(r,boss_)-Read(r.get<TransformComponent>(weakpoint_).translate);
    r.get<MeshRendererComponent>(weakpoint_).enabled=false;
    if(b.phase==2&&b.clock>14){b.phase=3;b.clock=0;}
    if(b.phase==3&&b.clock>2){
        b.phase=0;b.clock=0;
        for(auto e:r.view<Projectile,Target>()){r.get<Projectile>(e).lifetime=0;r.get<Target>(e).active=false;}
    }
}
void ChronoSystem::UpdateWorld(entt::registry& r,Player& p,GameContext& ctx){
    UpdateBoss(r,p,ctx);float dt=ctx.dt*p.worldScale;V player=Read(r.get<TransformComponent>(player_).translate);
    for(auto e:r.view<Anchor,Target,MeshRendererComponent>()){
        auto& a=r.get<Anchor>(e);if(a.reusable&&a.cooldown>0){a.cooldown=std::max(0.0f,a.cooldown-dt);
            if(a.cooldown==0){r.get<Target>(e).active=true;r.get<MeshRendererComponent>(e).enabled=true;}}
    }
    for(auto e:r.view<Hopper,Target,TransformComponent>()){
        auto& tg=r.get<Target>(e);if(!tg.active)continue;
        auto& h=r.get<Hopper>(e);auto& t=r.get<TransformComponent>(e);V c=Center(r,e),delta=player-c;
        h.timer-=dt;
        if(h.grounded&&h.timer<=0 && Length(delta)<26){
            V dir=Unit(V{delta.x,0,delta.z});h.velocity=dir*6;h.velocity.y=8;h.timer=0.9f;h.grounded=false;
            t.rotate.y=std::atan2(dir.x,dir.z);
        }
        h.velocity.y-=24*dt;c=Move(c,h.velocity*dt,{1.1f,1.4f,1.1f},h.grounded,&h.velocity);
        if(h.grounded){h.velocity.x=0;h.velocity.z=0;}
        t.translate=Write(c-tg.offset);
        if(Length(c-player)<2.2f && !(p.action==Action::Pulling&&p.target==e)&&p.invincible<=0){p.damageReason="HIT - HOPPER";Damage(r,p,12,ctx,c);}
    }
    // Symmetric, collision-aware separation of living hoppers.
    std::vector<entt::entity> enemies;
    for(auto e:r.view<Hopper,Target>())if(r.get<Target>(e).active)enemies.push_back(e);
    for(size_t i=0;i<enemies.size();++i)for(size_t j=i+1;j<enemies.size();++j){
        auto a=enemies[i],b=enemies[j];V ca=Center(r,a),cb=Center(r,b),d=ca-cb;
        if(std::abs(d.y)>2.5f)continue;d.y=0;float n=Length(d);if(n>=2.5f)continue;
        V push=(n<0.001f?V{1,0,0}:d*(1/n))*((2.5f-n)*0.5f);bool ground;
        r.get<TransformComponent>(a).translate=Write(Move(ca,push,{1.1f,1.4f,1.1f},ground)-r.get<Target>(a).offset);
        r.get<TransformComponent>(b).translate=Write(Move(cb,push*-1,{1.1f,1.4f,1.1f},ground)-r.get<Target>(b).offset);
    }
    std::vector<entt::entity> expired;
    for(auto e:r.view<Projectile,Target,TransformComponent>()){
        auto& q=r.get<Projectile>(e);auto& tg=r.get<Target>(e);auto& t=r.get<TransformComponent>(e);
        q.lifetime-=dt;if(q.lifetime<=0 || (q.breakable&&!tg.active)){expired.push_back(e);continue;}
        V before=Center(r,e),after=before+q.velocity*dt;
        float hit;bool contact=RaySphere(before,Unit(after-before),player,tg.radius+0.9f,Length(after-before),hit);
        if(contact && !(p.action==Action::Pulling&&p.target==e)){
            if(p.invincible<=0)p.damageReason=q.breakable?"HIT - ORB":"HIT - RED PROJECTILE";
            Damage(r,p,q.breakable?10.0f:16.0f,ctx,before);expired.push_back(e);continue;
        }
        if(!Clear(before,after,0.15f)){expired.push_back(e);continue;}
        t.translate=Write(after-tg.offset);
    }
    for(auto e:expired)r.destroy(e);
}
void ChronoSystem::Presentation(entt::registry& r,Player& p,GameContext& ctx){
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(auto& spark:sparks_){spark.age+=ctx.dt;spark.to=player;}
    sparks_.erase(std::remove_if(sparks_.begin(),sparks_.end(),[](const Spark& s){return s.age>0.35f;}),sparks_.end());
    for(auto& d:droplets_){d.age+=ctx.dt;d.velocity.y-=16*ctx.dt;d.position=d.position+d.velocity*ctx.dt;}
    droplets_.erase(std::remove_if(droplets_.begin(),droplets_.end(),[](const Droplet& d){return d.age>.45f;}),droplets_.end());
    float intensity=finished_?0:std::clamp((1-p.worldScale)/.8f,0.f,1.f);
    postStrength_+=(intensity-postStrength_)*(1-std::exp(-8*ctx.dt));
    Engine::Renderer::PostProcessParams params{};
    params.time=ctx.renderer->GetPostProcessParams().time;params.san=postStrength_;
    params.distortion=p.action==Action::Pulling?1.0f:0.65f;
    params.vignette=p.damageAge<.35f?1-p.damageAge/.35f:0;
    ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("ChronoFocus");
}
void ChronoSystem::Finish(Player& p,bool win){
    if(finished_)return;finished_=true;won_=win;finishAge_=0;p.target=entt::null;p.preview=entt::null;
    p.action=Action::Free;p.velocity={};p.flow.remaining=0;p.flow.scale=1;p.aimScale=1;p.worldScale=1;p.aiming=false;
    p.hitStop=0;p.damageAge=1;
    if(!win||diagnostic_||arena_)return;
    newTime_=bestTime_==0||p.stats.seconds<bestTime_;newChain_=p.flow.maxCombo>bestChain_;
    if(newTime_)bestTime_=p.stats.seconds;if(newChain_)bestChain_=p.flow.maxCombo;
    try{auto path=RecordPath();if(!path.empty()){
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);file<<nlohmann::json{{"time",bestTime_},{"chain",bestChain_}}.dump(2);
    }}catch(...){OutputDebugStringA("Chrono: record could not be saved.\n");}
}
void ChronoSystem::Update(entt::registry& r,GameContext& ctx){
    if(!initialized_||!r.valid(player_))return;
    const auto* control=r.try_get<ControlFrame>(player_);
    bool arenaKey=control?control->arena:Down(VK_F2);
    if(arenaKey&&!prevArena_){arena_=!arena_;prevArena_=true;Reset(r);return;}
    prevArena_=arenaKey;
    auto& p=r.get<Player>(player_);
    diagnostic_=r.all_of<ControlFrame>(player_);
    if(finished_){
        Presentation(r,p,ctx);
        finishAge_+=ctx.dt;Engine::WindowDX::SetCursorVisible(true);
        if(finishAge_>0.2f&&ctx.input){
            float mx=0,my=0;ctx.input->GetMousePos(mx,my);
            float uiScale=std::min(ctx.viewportSize.x/1280,ctx.viewportSize.y/720);
            if(uiScale>0){mx/=uiScale;my/=uiScale;}
            bool click=ctx.input->IsMouseTrigger(0);
            if(ctx.input->Trigger(0x13)||ctx.input->Trigger(0x1C)||(click&&mx>=290&&mx<=610&&my>=520&&my<=582)){
                if(arena_){Reset(r);return;}
                Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
                Engine::SceneManager::GetInstance()->RequestChange("Game",params);
            }
            if(ctx.input->Trigger(0x0F)||(click&&mx>=650&&mx<=970&&my>=520&&my<=582))Engine::SceneManager::GetInstance()->RequestChange("Select");
        }
        return;
    }
    UpdatePlayer(r,p,ctx);if(!finished_&&p.hitStop<=0)UpdateWorld(r,p,ctx);
    Presentation(r,p,ctx);
    if(ctx.combatFlow)ctx.combatFlow->enemyScale=p.hitStop>0?0:p.worldScale;
}
void ChronoSystem::Draw(entt::registry& r,GameContext& ctx){
    if(!initialized_||!ctx.isPlaying||!r.valid(player_)||!ctx.renderer)return;
    auto sphere=[&](V at,float size,Engine::Vector4 color){
        Engine::Transform transform;transform.translate=EV(at);transform.scale={size,size,size};
        ctx.renderer->DrawMesh(sphere_,white_,transform,color,"Default",0.3f,false);
    };
    // The arm is part of the player's SPH volume (GameScene::SetGPUFluidTether).
    for(const auto& s:sparks_){float a=std::clamp(s.age/0.35f,0.0f,1.0f);
        V at=Lerp(s.from,s.to,a*a);sphere(at,0.2f*(1-a)+0.08f,{0.2f,1.4f,1.6f,1});}
    for(const auto& d:droplets_)sphere(d.position,.18f*(1-d.age/.45f)+.035f,{.18f,1.1f,.6f,1});
    if(arena_&&r.valid(boss_)){
        const auto& b=r.get<Boss>(boss_);V c=Center(r,boss_)+V{0,-4.9f,0};
        if(b.phase==1||b.phase==2){
            Engine::Vector4 color=b.phase==1?Engine::Vector4{1,.65f,.08f,1}:Engine::Vector4{1,.1f,.1f,1};
            V side{b.attackDirection.z,0,-b.attackDirection.x};
            if(b.wave%2==0){for(float sign:{-1.f,1.f})ctx.renderer->DrawLine3D(EV(c+side*(sign*4.4f)),
                EV(c+side*(sign*4.4f)+b.attackDirection*19),color);
                ctx.renderer->DrawLine3D(EV(c+b.attackDirection*19-side*4.4f),EV(c+b.attackDirection*19+side*4.4f),color);
            }else for(int i=0;i<32;++i){float a=-1.47f+i*2.94f/32,aa=-1.47f+(i+1)*2.94f/32;
                ctx.renderer->DrawLine3D(EV(c+(b.attackDirection*std::cos(a)+side*std::sin(a))*7),
                    EV(c+(b.attackDirection*std::cos(aa)+side*std::sin(aa))*7),color);}
        }
    }
    // Projectiles keep their cyan/red silhouettes. Only the selected target
    // gets a HUD frame; no overlapping route lines through the battlefield.
    if(r.valid(weakpoint_)&&r.get<Target>(weakpoint_).active){
        V c=Center(r,weakpoint_);float radius=2.5f+.2f*std::sin(r.get<Player>(player_).stats.seconds*8);
        for(int i=0;i<32;++i){float a=i*6.283185f/32,b=(i+1)*6.283185f/32;
            ctx.renderer->DrawLine3D(EV(c+V{std::cos(a)*radius,std::sin(a)*radius,0}),
                EV(c+V{std::cos(b)*radius,std::sin(b)*radius,0}),{1,.75f,.15f,1});}
    }
}
void ChronoSystem::DrawUI(entt::registry& r,GameContext& ctx){
    if(!initialized_||!r.valid(player_)||!ctx.renderer)return;
    const auto& p=r.get<Player>(player_);auto* render=ctx.renderer;
    float w=ctx.viewportSize.x>0?ctx.viewportSize.x:1280,h=ctx.viewportSize.y>0?ctx.viewportSize.y:720;
    float s=std::min(w/1280,h/720);
    auto rect=[&](float x,float y,float width,float height,Engine::Vector4 color){
        Engine::Renderer::SpriteDesc d;d.x=x*s;d.y=y*s;d.w=width*s;d.h=height*s;d.color=color;d.layer=0;render->DrawSprite(white_,d);};
    auto text=[&](const std::string& label,float x,float y,float scale,Engine::Vector4 color=Engine::Vector4{0.9f,0.98f,1,1}){
        render->DrawString(label,x*s,y*s,scale*s,color);};
    const Engine::Vector4 bg{0.025f,0.045f,0.085f,0.94f},cyan{0.1f,0.9f,1,1},gold{1,0.75f,0.2f,1};
    if(finished_){
        rect(0,0,1280,720,{0.02f,0.03f,0.06f,0.86f});rect(250,82,780,530,bg);
        text(won_?"CHRONO CHAIN  /  CLEAR":"REFORM AND TRY AGAIN",290,106,0.65f);
        text(won_?p.stats.Rank(p.flow.maxCombo):"RETRY",840,160,1.7f,gold);
        text("CLEAR TIME     "+TimeText(p.stats.seconds)+(newTime_?"  NEW BEST":""),290,194,0.43f);
        text("MAX CHAIN      "+std::to_string(p.flow.maxCombo)+(newChain_?"  NEW BEST":""),290,239,0.43f);
        text("COUNTERS       "+std::to_string(p.stats.counters),290,284,0.43f);
        text("AIR KILLS  "+std::to_string(p.stats.maxAirKills)+"    ORBS BROKEN  "+std::to_string(p.stats.projectiles),290,347,0.32f);
        text("MAX MASS  "+std::to_string(static_cast<int>(p.stats.maxMass))+"    HITS  "+std::to_string(p.stats.hitsTaken)+"    COLLAPSES  "+std::to_string(p.stats.collapses),290,382,0.32f);
        text("MANUAL  "+std::to_string(p.stats.manual)+"    AUTO  "+std::to_string(p.stats.automatic)+"    SLOW  "+TimeText(p.flow.slowSeconds),290,417,0.32f);
        text("BEST  "+TimeText(bestTime_)+"    CHAIN  "+std::to_string(bestChain_),290,455,0.31f,cyan);
        rect(290,520,320,62,{0.07f,0.35f,0.48f,1});rect(650,520,320,62,{0.14f,0.17f,0.26f,1});
        text("[R / ENTER] RETRY",312,537,0.39f);text("[TAB] STAGE SELECT",672,537,0.38f);
        return;
    }
    rect(422,610,436,76,bg);
    text("MASS "+std::to_string(static_cast<int>(p.mass)),438,621,.24f,cyan);
    rect(438,648,188,7,{.1f,.17f,.22f,1});rect(438,648,188*p.mass/200,7,cyan);
    text(p.instability>75?"STRAIN - LAND TO STABILIZE":"STRAIN",651,621,.22f,p.instability>75?gold:cyan);
    rect(651,648,190,7,{.16f,.15f,.13f,1});rect(651,648,190*p.instability/100,7,gold);
    text(p.aiming?"RMB FOCUS  /  MANUAL":p.automatic?"AUTO CHAIN":"MANUAL  /  HOLD RMB",438,665,.20f);
    if(p.worldScale<.99f)text("TIME "+std::to_string(static_cast<int>(p.worldScale*100))+"%",737,665,.20f,cyan);
    if(p.flow.combo>0){text(std::to_string(p.flow.combo)+" CHAIN",582,542,.42f,gold);
        rect(580,579,125*p.flow.grace/2,3,gold);}
    if(r.valid(boss_)){
        auto& boss=r.get<Boss>(boss_);auto& bt=r.get<Target>(boss_);
        if(boss.engaged){
            rect(380,8,520,55,bg);
            std::string instruction=boss.phase==0?"APPROACH - GET READY":boss.phase==3?(boss.countered?"COUNTER LANDED - REFORM":"WAVE ENDED - REGROUP"):
                boss.waveBreaks<3?"BREAK ORBS  "+std::to_string(std::min(3,boss.waveBreaks))+" / 3":"BOSS EXPOSED - GRAB THE BODY";
            if(arena_)instruction=boss.phase==1?(boss.wave%2==0?"CHARGE INCOMING - DODGE SIDEWAYS":"SWEEP INCOMING - LEAVE THE ARC"):
                boss.phase==2?"ATTACK ACTIVE":boss.phase==3?"OPENING - COUNTER OR LAND":"ARENA - WATCH THE BOSS";
            text(instruction,408,15,.27f,gold);
            if(!arena_)for(int i=0;i<3;++i)rect(410.0f+static_cast<float>(i)*154,36,143,3,boss.waveBreaks>i?cyan:Engine::Vector4{.17f,.23f,.29f,1});
            rect(410,44,451,4,{0.1f,0.12f,0.2f,1});rect(410,44,451*std::max(0.0f,bt.hp)/360,4,{0.95f,0.23f,0.45f,1});
            text("COUNTERS "+std::to_string(p.stats.counters)+" / 3",410,51,.14f);
            V c=Center(r,boss_);using namespace DirectX;
            XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
            if(clip.w<=0||std::abs(clip.x)>clip.w*.85f||std::abs(clip.y)>clip.w*.75f){
                V d=c-Read(ctx.camera->Position());float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
                float len=std::max(.01f,std::sqrt(x*x+y*y));x/=len;y/=len;
                float sx=std::clamp(640+x*490,110.f,1090.f),sy=std::clamp(360+y*245,95.f,575.f);
                text(boss.phase==1?"! BOSS ATTACK !":"BOSS",sx-40,sy,.26f,gold);
                for(int i=0;i<7;++i){float q=i-3.f;rect(sx+x*(22-std::abs(q)*3)-y*q*3,sy+y*(22-std::abs(q)*3)+x*q*3,4,4,gold);}
            }
        }else text("CHAIN UP TO THE CITADEL",494,34,0.3f,gold);
    }
    if(p.stats.seconds<12)text("WASD Move  SPACE Jump  SHIFT Cancel/Dodge  LMB Grab  RMB Aim  MMB Auto  F2 Arena",170,698,.20f);
    text(p.action==Action::Collapsed?"REFORMING - DODGE LOCKED":p.cooldown>0?"DODGE RECHARGING":"SHIFT - CANCEL / DODGE",487,594,.18f,cyan);
    if(p.landingAge<.6f)text("LANDED - STABILIZING",515,495,.25f,cyan);
    if(p.instability>75){float pulse=.5f+.5f*std::sin(p.stats.seconds*10);
        rect(600,460,80,3,{1,.45f,.1f,pulse});}
    if(p.action==Action::Collapsed)text("UNSTABLE!  REFORMING",480,430,0.47f,{1,0.25f,0.25f,1});
    if(p.aiming||p.automatic){
        auto color=r.valid(p.preview)?cyan:Engine::Vector4{0.65f,0.7f,0.75f,1};
        rect(631,359,7,2,color);rect(643,359,7,2,color);rect(639,351,2,7,color);rect(639,363,2,7,color);
        if(r.valid(p.preview)&&r.get<Target>(p.preview).active){
            V c=Center(r,p.preview);float cost=StrainCost(p.mass,Length(c-Read(r.get<TransformComponent>(player_).translate)));
            bool unsafe=p.instability+cost>=100;auto col=unsafe?Engine::Vector4{1,0.25f,0.15f,1}:cyan;
            using namespace DirectX;auto v=XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj());
            XMFLOAT4 clip;XMStoreFloat4(&clip,v);
            if(clip.w>0){float x=(clip.x/clip.w*0.5f+0.5f)*w/s,y=(-clip.y/clip.w*0.5f+0.5f)*h/s;
                rect(x-20,y-20,40,2,col);rect(x-20,y+18,40,2,col);rect(x-20,y-20,2,40,col);rect(x+18,y-20,2,40,col);}
            text(unsafe?"OVERLOAD RISK":p.action==Action::Pulling?"PULLING":r.get<Target>(p.preview).kind==Kind::Anchor?"ANCHOR":"GRAB",590,392,0.25f,col);
        }
    }
    if(p.failureTime>0){
        const char* reason="";
        switch(p.failure){
        case ChainFailure::NoTarget:reason="NO TARGET - AIM AT AN ORB";break;
        case ChainFailure::OutOfRange:reason="OUT OF REACH - MOVE CLOSER";break;
        case ChainFailure::Obstructed:reason="BLOCKED - CHANGE ANGLE";break;
        case ChainFailure::Recovering:reason=p.buffer>0?"QUEUED - RELEASE LMB TO CANCEL":"RECOVERING - SHIFT TO DODGE";break;
        case ChainFailure::Unstable:reason="UNSTABLE - WAIT TO REFORM";break;
        case ChainFailure::Missed:reason="MISSED - AIM AT THE TARGET";break;
        case ChainFailure::Fell:reason="FALL - RETURNED TO LAST LANDING";break;
        default:break;
        }
        text(reason,505,419,.22f,gold);
    }
    if(r.valid(p.blockedPreview)){
        V c=Center(r,p.blockedPreview);using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=(clip.x/clip.w*.5f+.5f)*w/s,y=(-clip.y/clip.w*.5f+.5f)*h/s;
            for(int i=0;i<7;++i){float q=i*5.f-15;rect(x+q,y+q,3,3,gold);rect(x+q,y-q,3,3,gold);}
            text("BLOCKED",x-28,y+23,.18f,gold);}
    }
    if(p.damageAge<.6f){
        V d=Unit(p.damageSource-Read(r.get<TransformComponent>(player_).translate));
        float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
        float len=std::sqrt(x*x+y*y);if(len>.01f){x/=len;y/=len;}
        auto red=Engine::Vector4{1,.18f,.12f,1-p.damageAge/.6f};
        for(int i=0;i<7;++i){float q=i-3.f;
            rect(640+x*(79-std::abs(q)*3)-y*q*3,360+y*(79-std::abs(q)*3)+x*q*3,4,4,red);}
        text(p.damageReason,534,469,.25f,red);
    }
}
} // namespace Game


