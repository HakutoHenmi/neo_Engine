#pragma once
#include "ISystem.h"
#include "../Chrono/ChronoComponents.h"
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
    using V=Chrono::Vec;
    GameScene* scene_;
    entt::entity player_=entt::null,boss_=entt::null,weakpoint_=entt::null;
    std::vector<Chrono::Bounds> solids_;
    struct Spark {V from,to; float age=0;};
    std::vector<Spark> sparks_;
    struct Droplet {V position,velocity;float age=0;};
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
    float manualCameraHold_=0;
    bool arena_=false,prevArena_=false;
    float bestTime_=0; int bestChain_=0;
    void Build(entt::registry&);
    void BuildArena(entt::registry&);
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
    void Damage(entt::registry&,Chrono::Player&,float,GameContext&,V);
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
