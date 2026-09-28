#pragma once
#include "ISystem.h"
#include "../Chrono/ChronoComponents.h"
#include "../Chrono/InkRules.h"
#include <vector>
namespace Game {
// Scene-level orchestration; all persistent actor state lives in ECS components.
class ChronoSystem final : public ISystem {
public:
    explicit ChronoSystem(GameScene* scene):scene_(scene){}
    void Reset(entt::registry&) override;
    void Update(entt::registry&,GameContext&) override;
    void Draw(entt::registry&,GameContext&) override;
    void DrawUI(entt::registry&,GameContext&) override;
    bool Finished() const {return finished_;}
    void Invalidate() {initialized_=false;}
private:
    bool inkMode_=true;
    struct InkShot {Chrono::Vec at,velocity;float age=0;};
    std::vector<InkShot> inkShots_;
    struct SlimeTrail {Chrono::Vec at;int surface;float mass,radius;std::array<float,32> footprint{};float originalMass=0;bool bonus=false;};
    std::vector<SlimeTrail> slimeTrails_;
    bool inkNeedsRebuild_=false;
    uint32_t slimeBeamMesh_=0,slimeRingMesh_=0;
    uint32_t slimePressureSound_=0xffffffff,slimeRecallSound_=0xffffffff,slimeAcidSound_=0xffffffff;
    void DrawSlimeBeam(entt::registry&,GameContext&);
    std::vector<Chrono::InkSurface> inkSurfaces_;
    std::vector<uint32_t> inkMeshes_;
    uint32_t inkTexture_=0;bool inkDirty_=true;float inkUploadClock_=0;
    void BuildInk(entt::registry&);
    void UpdateInk(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateInkBoss(entt::registry&,Chrono::Player&,GameContext&);
    void DrawInk(entt::registry&,GameContext&);
    void DrawInkUI(entt::registry&,GameContext&);
    void InkCamera(entt::registry&,Chrono::Player&,GameContext&);
    float InkGround(Chrono::Vec,int* =nullptr)const;
    void UploadInk();
    using V=Chrono::Vec;
    GameScene* scene_;
    entt::entity player_=entt::null,boss_=entt::null,weakpoint_=entt::null;
    std::vector<Chrono::Bounds> solids_;
    std::vector<Chrono::Bounds> creatureSolids_;
    struct Spark {V from,to; float age=0;};
    std::vector<Spark> sparks_;
    struct Droplet {V position,velocity;float age=0;bool dust=false;};
    std::vector<Droplet> droplets_;
    V cameraFollow_{},composition_{};
    bool cameraReady_=false;
    float viewPitch_=0.12f,postStrength_=0;
    uint32_t hitSound_=0xffffffff,coreSound_=0xffffffff;
    uint32_t white_=0,sphere_=0;
    bool initialized_=false,finished_=false,won_=false,prevL_=false,prevM_=false,prevSpace_=false,prevShift_=false;
    bool newTime_=false,newChain_=false;
    bool diagnostic_=false;
    float yaw_=0,pitch_=0.16f,zoom_=17,finishAge_=0;
    float shoulder_=1.8f,chainCameraHold_=0,fov_=1.0472f;
    float manualCameraHold_=0,zoomVelocity_=0,cameraBoom_=-1;
    float cameraAssist_=0;int shakeSetting_=2;bool prevShake_=false;
    bool arena_=false,prevArena_=false;
    bool creature_=false,prevCreature_=false,snakeOnly_=false,prevSnake_=false;
    float bestTime_=0; int bestChain_=0;
    void Build(entt::registry&);
    void BuildArena(entt::registry&);
    void BuildCreature(entt::registry&);
    void UpdateCreature(entt::registry&,Chrono::Player&,GameContext&);
    void PoseCreature(entt::registry&);
    bool Reachable(entt::registry&,const Chrono::Player&,entt::entity,V,V* =nullptr) const;
    entt::entity Mesh(entt::registry&,const std::string&,const std::string&,V,V);
    void Block(entt::registry&,const std::string&,V,V);
    void CacheSolids(entt::registry&);
    bool Sweep(V,V,V,float&,V&) const;
    bool Clear(V,V,float=0) const;
    V Move(V,V,V,bool&,V* =nullptr) const;
    V Center(entt::registry&,entt::entity) const;
    entt::entity Choose(entt::registry&,Chrono::Player&,GameContext&,bool);
    void Shoot(entt::registry&,Chrono::Player&,GameContext&);
    void Impact(entt::registry&,Chrono::Player&,entt::entity,GameContext&,bool);
    void Damage(entt::registry&,Chrono::Player&,float,GameContext&,V,float perfectWindow=.12f);
    void Feedback(Chrono::Player&,V,bool);
    Chrono::ChainFailure Failure(entt::registry&,Chrono::Player&,GameContext&);
    void Presentation(entt::registry&,Chrono::Player&,GameContext&);
    void UpdatePlayer(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateWorld(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateBoss(entt::registry&,Chrono::Player&,GameContext&);
    void Camera(entt::registry&,Chrono::Player&,GameContext&,float);
    void Finish(Chrono::Player&,bool);
};
}
