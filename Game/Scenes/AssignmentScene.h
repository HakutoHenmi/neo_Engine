#pragma once
#include "IScene.h"
#include "Camera.h"
#include "Renderer.h"
#include "Model.h"
#include "Transform.h"
#include "WindowDX.h"
#include "Particle.h"
#include "GPUParticle.h"
#include "../ObjectTypes.h"
#include <string>

namespace Game {

class AssignmentScene : public Engine::IScene {
public:
    ~AssignmentScene() override;
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;
    void DrawEditor() override;

private:
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;
    Engine::Camera camera_;

    // Models for assignment
    uint32_t mutantModelHandle_ = 0;
    std::shared_ptr<Engine::Model> mutantModel_;

    // Sword and particles
    uint32_t swordMesh_ = 0;
    uint32_t swordTex_ = 0;
    float particleTimer_ = 0.0f;
    std::unique_ptr<Engine::ParticleSystem> particleSys_;
    Engine::Vector3 swordPos_ = {0.0f, 0.0f, 0.0f};
    Engine::Vector3 swordTip_ = {0.0f, 0.0f, 0.0f};
    Engine::Vector3 swordDir_ = {0.0f, 1.0f, 0.0f};
    
    // New GPU Particle System for Tornado & others
    std::unique_ptr<Engine::GPUParticleSystem> gpuParticleSys_;
    std::vector<Engine::GPUParticleEmitterData> emitters_;

    Engine::Transform transform_;
    
    // Animation state
    float animTime_ = 0.0f;
    float prevAnimTime_ = 0.0f;
    float blendFactor_ = 0.0f;
    std::string currentAnim_ = "Mutant Idle";
    std::string prevAnim_ = "";
    
    bool isMoving_ = false;
    bool isAttacking_ = false;
    
    // Camera settings
    float cameraAngleX_ = 0.0f;
    float cameraAngleY_ = 0.0f;
    float cameraDistance_ = 5.0f;
    
    // Default Tornado state
    Engine::Vector3 tornadoVel_ = {0.0f, 0.0f, 0.0f};

    // Post effect tracking
    std::string currentEffect_ = "Default";
};

} // namespace Game
