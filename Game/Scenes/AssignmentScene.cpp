#include "AssignmentScene.h"
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <imgui.h>

namespace Game {

AssignmentScene::~AssignmentScene() {
    // Cleanup is handled by renderer
}

float AssignmentScene::DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b) const {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

void AssignmentScene::QueuePostEffect(const std::string& effect, float duration, const std::string& reason) {
    PostEffectEvent ev;
    ev.effect = effect;
    ev.timer = duration;
    ev.duration = duration;
    ev.reason = reason;
    postEffectQueue_.push_back(ev);
}

void AssignmentScene::UpdatePostEffects(float dt) {
    auto saturateFloat = [](float v) {
        return (std::max)(0.0f, (std::min)(1.0f, v));
    };
    auto smooth01 = [&](float v) {
        v = saturateFloat(v);
        return v * v * (3.0f - 2.0f * v);
    };

    float effectStrength = 0.0f;

    if (!postEffectQueue_.empty()) {
        PostEffectEvent& ev = postEffectQueue_.front();
        currentEffect_ = ev.effect;
        currentEffectReason_ = ev.reason;

        const float elapsed = (std::max)(0.0f, ev.duration - ev.timer);
        const float fadeTime = ev.effect == "Dissolve"
            ? (std::min)(1.35f, ev.duration * 0.45f)
            : ev.effect == "Grayscale"
            ? (std::min)(0.60f, ev.duration * 0.35f)
            : (std::min)(0.30f, ev.duration * 0.35f);
        if (currentEffect_ == "Default") {
            effectStrength = 0.0f;
        } else if (fadeTime > 0.001f) {
            const float fadeIn = smooth01(elapsed / fadeTime);
            const float fadeOut = smooth01(ev.timer / fadeTime);
            effectStrength = (std::min)(fadeIn, fadeOut);
        } else {
            effectStrength = 1.0f;
        }

        ev.timer -= dt;
        if (ev.timer <= 0.0f) {
            postEffectQueue_.erase(postEffectQueue_.begin());
            if (!postEffectQueue_.empty()) {
                currentEffect_ = postEffectQueue_.front().effect;
                currentEffectReason_ = postEffectQueue_.front().reason;
            } else {
                currentEffect_ = "Default";
                currentEffectReason_ = "Normal gameplay";
            }
        }
    } else {
        const float distToEnemy = DistanceXZ(transform_.translate, enemyTransform_.translate);
        std::string desiredEffect = "Default";
        std::string desiredReason = "Normal gameplay";
        if (enemyHp_ <= 0) {
            desiredEffect = "Dissolve";
            desiredReason = "Enemy cube is dissolving after defeat";
        } else if (playerHp_ <= 30) {
            desiredEffect = "Vignetting";
            desiredReason = "Low HP danger state";
        } else if (enemyAttackWindup_ > 0.0f) {
            desiredEffect = "DepthBasedOutline";
            desiredReason = "Enemy attack telegraph";
        } else if (isGuarding_) {
            desiredEffect = "BoxFilter";
            desiredReason = "Guard focus state";
        } else if (isAttacking_) {
            desiredEffect = "RadialBlur";
            desiredReason = "Player attack momentum";
        } else if (enemyAware_ && distToEnemy < 7.0f) {
            desiredEffect = "OutlinePost";
            desiredReason = "Enemy lock-on range";
        }

        const bool fadingToDefault = desiredEffect == "Default" && currentEffect_ != "Default";
        if (!fadingToDefault && currentEffect_ != desiredEffect) {
            currentEffect_ = desiredEffect;
            currentEffectReason_ = desiredReason;
            previousEffect_ = desiredEffect;
            postEffectStrength_ = 0.0f;
        } else if (!fadingToDefault) {
            currentEffectReason_ = desiredReason;
        } else {
            currentEffectReason_ = "Returning to normal gameplay";
        }

        const float targetStrength = desiredEffect == "Default" ? 0.0f : 1.0f;
        const float follow = saturateFloat(dt * 5.0f);
        postEffectStrength_ += (targetStrength - postEffectStrength_) * follow;
        if (fadingToDefault && postEffectStrength_ <= 0.02f) {
            currentEffect_ = "Default";
            currentEffectReason_ = desiredReason;
            previousEffect_ = "Default";
            postEffectStrength_ = 0.0f;
        }
        effectStrength = postEffectStrength_;
    }

    if (!renderer_) return;

    renderer_->SetPostEffect(currentEffect_);

    auto params = renderer_->GetPostProcessParams();
    const float t = params.time + dt;
    params = Engine::Renderer::PostProcessParams{};
    params.time = t;
    params.san = effectStrength;

    if (currentEffect_ == "RadialBlur") {
        params.distortion = 0.9f * effectStrength;
        params.vignette = 0.45f * effectStrength;
    } else if (currentEffect_ == "Vignetting") {
        params.vignette = 2.5f * effectStrength;
    } else if (currentEffect_ == "Random") {
        params.noiseStrength = 0.18f * effectStrength;
        params.chromaShift = 0.003f * effectStrength;
    } else if (currentEffect_ == "Dissolve") {
        params.vignette = 0.8f * effectStrength;
    } else if (currentEffect_ == "GaussianFilter") {
        params.distortion = 0.2f * effectStrength;
    } else if (currentEffect_ == "DepthBasedOutline" || currentEffect_ == "OutlinePost") {
        params.chromaShift = 0.002f * effectStrength;
    }

    renderer_->SetPostProcessParams(params);
}

void AssignmentScene::UpdateEnemyCombat(float dt, bool attackPressed) {
    const Engine::Vector3 playerPos = transform_.translate;
    const Engine::Vector3 enemyPos = enemyTransform_.translate;
    const float distToEnemy = DistanceXZ(playerPos, enemyPos);

    if (distToEnemy < 9.0f && enemyHp_ > 0) {
        if (!enemyAware_) {
            enemyAware_ = true;
            QueuePostEffect("Grayscale", 3.2f, "Enemy cube spotted the player");
        }
    } else if (distToEnemy > 11.0f) {
        enemyAware_ = false;
    }

    if (enemyAttackCooldown_ > 0.0f) enemyAttackCooldown_ -= dt;
    if (enemyHitFlash_ > 0.0f) enemyHitFlash_ -= dt;
    if (playerHitFlash_ > 0.0f) playerHitFlash_ -= dt;

    if (attackPressed) {
        QueuePostEffect("RadialBlur", 0.65f, "Player attack lunge");

        const float forwardX = std::sin(transform_.rotate.y);
        const float forwardZ = std::cos(transform_.rotate.y);
        const float toEnemyX = enemyPos.x - playerPos.x;
        const float toEnemyZ = enemyPos.z - playerPos.z;
        const float toLen = std::sqrt(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);
        const float facing = toLen > 0.001f ? (forwardX * toEnemyX + forwardZ * toEnemyZ) / toLen : 1.0f;

        if (enemyHp_ > 0 && distToEnemy < 5.5f && facing > 0.15f) {
            enemyHp_ = (std::max)(0, enemyHp_ - 20);
            enemyHitFlash_ = 0.35f;
            QueuePostEffect("GaussianFilter", 0.7f, "Hit stop on successful player attack");

            if (enemyHp_ <= 0) {
                QueuePostEffect("Dissolve", 4.0f, "Enemy cube defeated");
                enemyAttackWindup_ = 0.0f;
                enemyAttackArmed_ = false;
                enemyAware_ = false;
                enemyAttackCooldown_ = 2.0f;
            }
        }
    }

    if (enemyHp_ <= 0) {
        enemyAttackCooldown_ -= dt;
        if (enemyAttackCooldown_ <= -1.5f) {
            enemyHp_ = 60;
            enemyTransform_.translate = {9.0f, 1.0f, 6.0f};
            QueuePostEffect("Grayscale", 2.6f, "Enemy cube respawned and reacquired the player");
            enemyAware_ = false;
            enemyAttackCooldown_ = 1.2f;
        }
        return;
    }

    if (distToEnemy < 4.0f && enemyAttackCooldown_ <= 0.0f && enemyAttackWindup_ <= 0.0f) {
        enemyAttackWindup_ = 0.55f;
        enemyAttackArmed_ = true;
    }

    if (enemyAttackWindup_ > 0.0f) {
        enemyAttackWindup_ -= dt;
        if (enemyAttackWindup_ <= 0.0f && enemyAttackArmed_) {
            enemyAttackArmed_ = false;
            enemyAttackCooldown_ = 1.4f;

            if (distToEnemy < 4.4f && playerHp_ > 0) {
                const int damage = isGuarding_ ? 4 : 15;
                playerHp_ = (std::max)(0, playerHp_ - damage);
                playerHitFlash_ = 0.45f;
                if (isGuarding_) {
                    QueuePostEffect("BoxFilter", 0.75f, "Guard blocked most of the enemy attack");
                } else {
                    QueuePostEffect("Random", 0.75f, "Player took damage glitch");
                }

                if (playerHp_ <= 0) {
                    QueuePostEffect("Vignetting", 1.0f, "Player HP reached zero");
                    playerHp_ = 100;
                    transform_.translate = {0.0f, 0.0f, 0.0f};
                }
            } else {
                QueuePostEffect("OutlinePost", 0.7f, "Enemy cube attack missed; lock-on remains");
            }
        }
    }
}

    void AssignmentScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
        (void)params;
        dx_ = dx;
        renderer_ = Engine::Renderer::GetInstance();
        
        camera_.Initialize();
        camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 10000.0f);
        
        // Set up lighting
        renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});
        renderer_->SetDirectionalLight({0, -1, 0.5f}, {1, 1, 1}, true);
        
        // Load weapon
        swordMesh_ = renderer_->LoadObjMesh("Resources/Models/weapons/sword.obj");
        swordTex_  = renderer_->LoadTexture2D("Resources/Models/weapons/bukiUV.png");
        enemyCubeMesh_ = renderer_->LoadObjMesh("Resources/Models/cube/cube.obj");
        enemyCubeTex_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
        
        // Initialize particle system
        particleSys_ = std::make_unique<Engine::ParticleSystem>();
        particleSys_->Initialize(*renderer_, 1000, "Resources/Models/plane.obj", "Resources/Textures/ball.png", true, true);
        
        gpuParticleSys_ = std::make_unique<Engine::GPUParticleSystem>();
        gpuParticleSys_->Initialize(renderer_->GetDevice(), 100000);
    
        // Initialize emitters
        Engine::GPUParticleEmitterData tornado = {};
        tornado.emitPos = { 10.0f, 0.0f, 0.0f };
        tornado.emitRate = 1000.0f; 
        tornado.emitVel = { 0.0f, 0.1f, 0.0f };
        tornado.emitLife = 3.0f;
        tornado.emitterShape = 1; // Sphere
        tornado.emitterExtents = { 0.5f, 0.0f, 0.0f };
        tornado.colorStart = { 2.0f, 0.5f, 3.0f, 1.0f };
        tornado.colorEnd = { 0.0f, 0.5f, 1.0f, 0.0f };
        tornado.fieldType = 3; // Tornado
        tornado.fieldPos = tornado.emitPos;
        tornado.fieldParams = { 10.0f, 3.0f, 1.5f, 0.0f };
        tornado.particleType = 0; // Default
        
        emitters_.push_back(tornado);
        
        Engine::GPUParticleEmitterData meshEmitter = {};
        meshEmitter.emitPos = { 0.0f, 0.0f, 0.0f }; // Attached to sword
        meshEmitter.emitRate = 1000.0f; // Emitting from sword
        meshEmitter.emitVel = { 0.0f, 1.0f, 0.0f };
        meshEmitter.emitLife = 0.5f;
        meshEmitter.emitterShape = 3; // Mesh
        meshEmitter.emitterExtents = { 0.0f, 0.0f, 0.25f };
        meshEmitter.colorStart = { 3.0f, 1.0f, 0.2f, 1.0f }; // Fire color
        meshEmitter.colorEnd = { 1.0f, 0.0f, 0.0f, 0.0f };
        meshEmitter.fieldType = 0; // None
        meshEmitter.particleType = 2; // Lit
        
        emitters_.push_back(meshEmitter);
    // We will use the Mutant model for skinning and animation blending
    mutantModelHandle_ = renderer_->LoadObjMesh("Resources/Models/Animation/Boss/Mutant Idle.fbx");
    if (mutantModelHandle_ > 0) {
        // Load the walking animation as additional animation
        renderer_->LoadAdditionalAnimation(mutantModelHandle_, "Resources/Models/Animation/Boss/Mutant Walking.fbx");
        renderer_->LoadAdditionalAnimation(mutantModelHandle_, "Resources/Models/Animation/Boss/attack1.fbx");
        mutantModel_ = std::shared_ptr<Engine::Model>(renderer_->GetModel(mutantModelHandle_), [](Engine::Model*){});
        
        auto tex1 = renderer_->LoadTexture2D("Resources/Textures/basecolor.jpg");
        auto tex2 = renderer_->LoadTexture2D("Resources/Textures/uvChecker.png");
        auto srv1 = renderer_->GetTextureSrvGpu(tex1);
        auto srv2 = renderer_->GetTextureSrvGpu(tex2);
        for (int i = 0; i < 5; ++i) {
            mutantModel_->SetMaterialSrv(i, i % 2 == 0 ? srv1 : srv2);
        }
    }

    transform_.translate = {0.0f, 0.0f, 0.0f};
    transform_.scale = {1.0f, 1.0f, 1.0f};
    transform_.rotate = {0.0f, 0.0f, 0.0f}; // Euler angles

    enemyTransform_.translate = {9.0f, 1.0f, 6.0f};
    enemyTransform_.scale = {1.2f, 1.2f, 1.2f};
    enemyTransform_.rotate = {0.0f, 0.0f, 0.0f};
    playerHp_ = 100;
    enemyHp_ = 60;
    enemyAttackCooldown_ = 1.0f;
    enemyAttackWindup_ = 0.0f;
    enemyAttackArmed_ = false;
    enemyHitFlash_ = 0.0f;
    playerHitFlash_ = 0.0f;
    
    currentAnim_ = "Mutant Idle";
    prevAnim_ = "Mutant Idle";
    
    cameraDistance_ = 12.0f;
    cameraAngleX_ = 0.3f;
    cameraAngleY_ = 0.0f;

    postEffectQueue_.clear();
    currentEffect_ = "Default";
    previousEffect_ = "Default";
    currentEffectReason_ = "Normal gameplay";
    postEffectStrength_ = 0.0f;
}

void AssignmentScene::Update() {
    float dt = 1.0f / 60.0f; // Fixed time step for simplicity
    
    auto* input = Engine::Input::GetInstance();
    
    // Check if we want to go back to Title (ESC)
    if (input->Trigger(0x01)) { // DIK_ESCAPE
        Engine::SceneManager::GetInstance()->RequestChange("Title");
        return;
    }
    
    // Manual post-effect demo keys. Gameplay events also push effects into this queue.
    if (input->Trigger(0x0B)) QueuePostEffect("Dissolve", 3.5f, "Manual key 0: Dissolve");
    if (input->Trigger(0x02)) QueuePostEffect("Default", 0.45f, "Manual key 1: Default");
    if (input->Trigger(0x03)) QueuePostEffect("Grayscale", 3.2f, "Manual key 2: Grayscale");
    if (input->Trigger(0x04)) QueuePostEffect("GaussianFilter", 0.85f, "Manual key 3: GaussianFilter");
    if (input->Trigger(0x05)) QueuePostEffect("OutlinePost", 0.85f, "Manual key 4: LuminanceBasedOutline");
    if (input->Trigger(0x06)) QueuePostEffect("DepthBasedOutline", 0.85f, "Manual key 5: DepthBasedOutline");
    if (input->Trigger(0x07)) QueuePostEffect("RadialBlur", 0.85f, "Manual key 6: RadialBlur");
    if (input->Trigger(0x08)) QueuePostEffect("Vignetting", 0.85f, "Manual key 7: Vignetting");
    if (input->Trigger(0x09)) QueuePostEffect("BoxFilter", 0.85f, "Manual key 8: BoxFilter");
    if (input->Trigger(0x0A)) QueuePostEffect("Random", 0.85f, "Manual key 9: Random");
    
    // Update particle system
    if (particleSys_) {
        particleSys_->Update(dt);
    }

    // Pad input (Left stick) using XInput
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    bool aPressed = false;
    bool guardHeld = false;
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        float normLX = std::fmaxf(-1, (float)state.Gamepad.sThumbLX / 32767);
        float normLY = std::fmaxf(-1, (float)state.Gamepad.sThumbLY / 32767);
        lx = (abs(normLX) < 0.2f ? 0.0f : normLX);
        ly = (abs(normLY) < 0.2f ? 0.0f : normLY);
        
        float normRX = std::fmaxf(-1, (float)state.Gamepad.sThumbRX / 32767);
        float normRY = std::fmaxf(-1, (float)state.Gamepad.sThumbRY / 32767);
        rx = (abs(normRX) < 0.2f ? 0.0f : normRX);
        ry = (abs(normRY) < 0.2f ? 0.0f : normRY);
        
        aPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        guardHeld = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    }
    
    // Add keyboard input (combine)
    if (input->Down(0x11)) ly += 1.0f; // W
    if (input->Down(0x1F)) ly -= 1.0f; // S
    if (input->Down(0x1E)) lx -= 1.0f; // A
    if (input->Down(0x20)) lx += 1.0f; // D
    
    if (input->Down(0xC8)) ry += 1.0f; // Up
    if (input->Down(0xD0)) ry -= 1.0f; // Down
    if (input->Down(0xCB)) rx -= 1.0f; // Left
    if (input->Down(0xCD)) rx += 1.0f; // Right
    
    if (input->Trigger(0x39)) aPressed = true; // Space for attack
    if (input->Down(0x2A) || input->Down(0x36)) guardHeld = true; // Left/Right Shift for guard focus
    if (input->IsMouseTrigger(0)) aPressed = true; // Left mouse button for attack
    if (input->IsMouseDown(1)) guardHeld = true; // Right mouse button for guard focus
    
    // Clamp
    lx = std::fmaxf(-1.0f, std::fminf(1.0f, lx));
    ly = std::fmaxf(-1.0f, std::fminf(1.0f, ly));
    rx = std::fmaxf(-1.0f, std::fminf(1.0f, rx));
    ry = std::fmaxf(-1.0f, std::fminf(1.0f, ry));
    
    float magnitude = std::sqrt(lx * lx + ly * ly);
    isGuarding_ = guardHeld && !isAttacking_;
    
    bool wasMoving = isMoving_;
    isMoving_ = (magnitude > 0.0f);
    
    bool attackStarted = false;
    if (aPressed && !isAttacking_) {
        attackStarted = true;
        isAttacking_ = true;
        prevAnim_ = currentAnim_;
        prevAnimTime_ = animTime_;
        currentAnim_ = "attack1";
        animTime_ = 0.0f;
        blendFactor_ = 1.0f;
        
        // Wind pressure from attack pushes the tornado
        float pushForce = 20.0f;
        float dirX = std::sin(transform_.rotate.y);
        float dirZ = std::cos(transform_.rotate.y);
        tornadoVel_.x += dirX * pushForce;
        tornadoVel_.z += dirZ * pushForce;
    }
    
    // Apply friction to tornado velocity
    tornadoVel_.x *= 0.95f;
    tornadoVel_.y *= 0.95f;
    tornadoVel_.z *= 0.95f;
    
    if (!isAttacking_) {
        if (isMoving_ && !wasMoving) {
            // Started moving
            prevAnim_ = currentAnim_;
            prevAnimTime_ = animTime_;
            currentAnim_ = "Mutant Walking";
            animTime_ = 0.0f;
            blendFactor_ = 1.0f; // Start blending from 1.0 down to 0.0
        } else if (!isMoving_ && wasMoving) {
            // Stopped moving
            prevAnim_ = currentAnim_;
            prevAnimTime_ = animTime_;
            currentAnim_ = "Mutant Idle";
            animTime_ = 0.0f;
            blendFactor_ = 1.0f;
        }
    }
    
    // Update blend factor
    if (blendFactor_ > 0.0f) {
        blendFactor_ -= dt * 4.0f; // 0.25 seconds blend time
        if (blendFactor_ < 0.0f) blendFactor_ = 0.0f;
    }
    
    // Update animations
    if (mutantModel_) {
        const auto& data = mutantModel_->GetData();
        // Update current animation time
        bool currentUpdated = false;
        bool prevUpdated = false;
        for (const auto& a : data.animations) {
            if (!currentUpdated && a.name.find(currentAnim_) != std::string::npos) {
                animTime_ += dt * a.ticksPerSecond * 0.5f; // Slower, heavier assignment demo motion
                if (animTime_ > a.duration) {
                    animTime_ = std::fmod(animTime_, a.duration);
                    if (isAttacking_ && currentAnim_ == "attack1") {
                        isAttacking_ = false;
                        prevAnim_ = currentAnim_;
                        prevAnimTime_ = animTime_;
                        currentAnim_ = isMoving_ ? "Mutant Walking" : "Mutant Idle";
                        animTime_ = 0.0f;
                        blendFactor_ = 1.0f;
                    }
                }
                currentUpdated = true;
            }
            if (blendFactor_ > 0.0f && !prevUpdated && a.name.find(prevAnim_) != std::string::npos) {
                prevAnimTime_ += dt * a.ticksPerSecond * 0.5f;
                if (prevAnimTime_ > a.duration) {
                    prevAnimTime_ = std::fmod(prevAnimTime_, a.duration);
                }
                prevUpdated = true;
            }
        }
    }

    // Update position and rotation based on pad input
    if (isMoving_ && !isAttacking_) {
        float speed = 3.0f * magnitude * dt;
        
        // Calculate movement direction relative to camera
        float moveAngle = std::atan2(lx, ly);
        float targetAngle = cameraAngleY_ + moveAngle;
        
        transform_.translate.x += std::sin(targetAngle) * speed;
        transform_.translate.z += std::cos(targetAngle) * speed;
        
        // Update rotation to face movement direction (Euler)
        transform_.rotate.y = targetAngle;
    }
    
    // Camera controls (Right stick + mouse)
    if (std::abs(rx) > 0.2f) cameraAngleY_ += rx * dt * 2.0f;
    if (std::abs(ry) > 0.2f) cameraAngleX_ -= ry * dt * 2.0f;
    cameraAngleY_ += input->GetMouseDeltaX() * 0.005f;
    cameraAngleX_ += input->GetMouseDeltaY() * 0.005f;
    cameraDistance_ -= input->GetMouseWheelDelta() * 1.0f;
    if (cameraDistance_ < 5.0f) cameraDistance_ = 5.0f;
    if (cameraDistance_ > 24.0f) cameraDistance_ = 24.0f;
    
    // Clamp camera pitch
    if (cameraAngleX_ < -1.5f) cameraAngleX_ = -1.5f;
    if (cameraAngleX_ > 1.5f) cameraAngleX_ = 1.5f;
    
    // Update camera position
    Engine::Vector3 targetPos = transform_.translate;
    targetPos.y += 4.0f; // Look at head/chest area
    
    Engine::Vector3 camOffset;
    camOffset.x = std::sin(cameraAngleY_) * std::cos(cameraAngleX_) * cameraDistance_;
    camOffset.y = -std::sin(cameraAngleX_) * cameraDistance_;
    camOffset.z = std::cos(cameraAngleY_) * std::cos(cameraAngleX_) * cameraDistance_;
    
    Engine::Vector3 camPos = targetPos;
    camPos.x -= camOffset.x;
    camPos.y -= camOffset.y;
    camPos.z -= camOffset.z;
    
    camera_.SetPosition({camPos.x, camPos.y, camPos.z});
    camera_.LookAt({targetPos.x, targetPos.y, targetPos.z}, {0, 1, 0});
    camera_.Tick(dt);
    
    renderer_->SetCamera(camera_);

    UpdateEnemyCombat(dt, attackStarted);
    UpdatePostEffects(dt);
    
    // Update GPU Particle Emitters
    if (gpuParticleSys_ && !emitters_.empty()) {
        // Update tornado position (emitter 0)
        Engine::Vector3 tornadoPos = { emitters_[0].emitPos.x, emitters_[0].emitPos.y, emitters_[0].emitPos.z };
        tornadoPos.x += tornadoVel_.x * dt;
        tornadoPos.y += tornadoVel_.y * dt;
        tornadoPos.z += tornadoVel_.z * dt;
        
        // Bounce off invisible walls to keep it on stage
        if (tornadoPos.x > 15.0f) { tornadoPos.x = 15.0f; tornadoVel_.x *= -0.5f; }
        if (tornadoPos.x < -15.0f) { tornadoPos.x = -15.0f; tornadoVel_.x *= -0.5f; }
        if (tornadoPos.z > 15.0f) { tornadoPos.z = 15.0f; tornadoVel_.z *= -0.5f; }
        if (tornadoPos.z < -15.0f) { tornadoPos.z = -15.0f; tornadoVel_.z *= -0.5f; }
        
        emitters_[0].emitPos = { tornadoPos.x, tornadoPos.y, tornadoPos.z };
        if (emitters_[0].fieldType == 3) {
            emitters_[0].fieldPos = { tornadoPos.x, tornadoPos.y, tornadoPos.z }; // keep tornado field at emitter pos
        }
        
        // Update mesh emitter to follow the sword and shoot outward.
        emitters_[1].emitPos = { swordPos_.x, swordPos_.y, swordPos_.z };
        
        // Shoot particles in the direction the sword is pointing (with some speed)
        float shootSpeed = 10.0f;
        emitters_[1].emitVel = { swordDir_.x * shootSpeed, swordDir_.y * shootSpeed, swordDir_.z * shootSpeed };
        
        emitters_[1].emitterShape = 3; // Mesh
        emitters_[1].emitterExtents.z = 0.25f;
        if (swordMesh_ > 0) {
            Engine::Model* swordModel = renderer_->GetModel(swordMesh_);
            if (swordModel) {
                gpuParticleSys_->SetEmitterMesh(swordModel->GetVertexBufferAddr(), swordModel->GetVertexCount(), sizeof(Engine::VertexData));
            }
        }
        
        gpuParticleSys_->Update(renderer_->GetCommandList(), dt, emitters_);
    }
}

void AssignmentScene::Draw() {
    if (!renderer_ || !mutantModel_) return;

    // Draw the floor grid or a simple plane (we can just draw lines for now)
    // Draw grid
    Engine::Vector4 gridColor = {0.3f, 0.3f, 0.3f, 1.0f};
    for (int i = -10; i <= 10; ++i) {
        renderer_->DrawLine3D({(float)i, 0.0f, -10.0f}, {(float)i, 0.0f, 10.0f}, gridColor);
        renderer_->DrawLine3D({-10.0f, 0.0f, (float)i}, {10.0f, 0.0f, (float)i}, gridColor);
    }

    if (enemyCubeMesh_ > 0 && enemyHp_ > 0) {
        Engine::Vector4 enemyColor = {0.8f, 0.15f, 0.15f, 1.0f};
        if (enemyHitFlash_ > 0.0f) {
            enemyColor = {1.0f, 1.0f, 0.2f, 1.0f};
        } else if (enemyAttackWindup_ > 0.0f) {
            enemyColor = {1.0f, 0.25f, 0.05f, 1.0f};
        }
        renderer_->DrawMesh(enemyCubeMesh_, enemyCubeTex_, enemyTransform_, enemyColor, "Default");

        const Engine::Vector3 p = enemyTransform_.translate;
        const float r = enemyAttackWindup_ > 0.0f ? 4.4f : 4.0f;
        const Engine::Vector4 rangeColor = enemyAttackWindup_ > 0.0f ? Engine::Vector4{1, 0.1f, 0.05f, 1} : Engine::Vector4{0.8f, 0.2f, 0.2f, 1};
        renderer_->DrawLine3D({p.x - r, 0.03f, p.z - r}, {p.x + r, 0.03f, p.z - r}, rangeColor, true);
        renderer_->DrawLine3D({p.x + r, 0.03f, p.z - r}, {p.x + r, 0.03f, p.z + r}, rangeColor, true);
        renderer_->DrawLine3D({p.x + r, 0.03f, p.z + r}, {p.x - r, 0.03f, p.z + r}, rangeColor, true);
        renderer_->DrawLine3D({p.x - r, 0.03f, p.z + r}, {p.x - r, 0.03f, p.z - r}, rangeColor, true);
    }

    // Prepare skeleton parameters
    std::vector<Engine::Matrix4x4> skeletonParams(Engine::kMaxBones, Engine::Matrix4x4::Identity());
    std::vector<Engine::Model::DebugBone> debugBones;

    const auto& data = mutantModel_->GetData();
    
    const Engine::Animation* currAnimPtr = nullptr;
    const Engine::Animation* prevAnimPtr = nullptr;
    
    for (const auto& a : data.animations) {
        if (!currAnimPtr && a.name.find(currentAnim_) != std::string::npos) currAnimPtr = &a;
        if (blendFactor_ > 0.0f && !prevAnimPtr && a.name.find(prevAnim_) != std::string::npos) prevAnimPtr = &a;
    }
    
    // Initial global transform (Model space)
    Engine::Matrix4x4 rootTransform = Engine::Matrix4x4::Identity();
    
    // Use the model's UpdateSkeleton function which handles interpolation
    // blendFactor here is for the PREVIOUS animation. 
    // In Model.cpp: tBlend = Lerp(prevTrans, currTrans, blendFactor) ... wait, is it?
    // Let's pass the parameters accordingly.
    // If blendFactor is for Lerp(prev, curr), it should be: Lerp(prev, curr, 1 - blendFactor_)
    // Let's check Model.cpp: XMVectorLerp(pTrans, trans, blendFactor).
    // So if blendFactor == 1, it uses 'trans' (current). If 0, uses 'pTrans' (prev).
    // Let's pass (1.0f - blendFactor_) to blendFactor parameter in UpdateSkeleton.
    
    if (currAnimPtr) {
        mutantModel_->UpdateSkeleton(data.rootNode, rootTransform, *currAnimPtr, animTime_, prevAnimPtr, prevAnimTime_, 1.0f - blendFactor_, skeletonParams, &debugBones);
    } else {
        // Fallback
        Engine::Animation dummyAnim;
        mutantModel_->UpdateSkeleton(data.rootNode, rootTransform, dummyAnim, 0.0f, nullptr, 0.0f, 0.0f, skeletonParams, &debugBones);
    }
    
    // Draw Skinned Mesh (MultiMesh & MultiMaterial is handled inside DrawSkinnedMesh -> DrawInstanced)
    // Note: We use the default texture 0, but the model has internal material to texture mappings
    renderer_->DrawSkinnedMesh(mutantModelHandle_, 0, transform_, skeletonParams, {1, 1, 1, 1});

    if (particleSys_) {
        // Draw ParticleSystem
        particleSys_->Draw(camera_, "Particle");
    }
    
    if (gpuParticleSys_) {
        // Draw GPUParticleSystem
        Engine::Matrix4x4 vpMat;
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&vpMat), DirectX::XMMatrixMultiply(camera_.View(), camera_.Proj()));
        
        // Pass to Renderer to draw at the correct time (after scene opaque/transparent pass)
        renderer_->SetCustomDrawJob([this, vpMat](ID3D12GraphicsCommandList* cmd) {
            gpuParticleSys_->Draw(cmd, vpMat, camera_.Position());
        });
    }

    // Debug draw bones
    Engine::Matrix4x4 worldMat = transform_.ToMatrix();
    Engine::Vector4 boneColor = {1.0f, 1.0f, 0.0f, 1.0f}; // Yellow
    
    auto WorldToScreen = [&](const Engine::Vector3& worldPos) -> std::pair<float, float> {
        DirectX::XMVECTOR p = DirectX::XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        DirectX::XMMATRIX v = camera_.View();
        DirectX::XMMATRIX pr = camera_.Proj();
        p = DirectX::XMVector4Transform(p, v);
        p = DirectX::XMVector4Transform(p, pr);
        DirectX::XMFLOAT4 clip;
        DirectX::XMStoreFloat4(&clip, p);
        if (clip.w <= 0.0001f) return {-1.0f, -1.0f};
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        // Engine::WindowDX::kW and kH contain the actual screen dimensions
        float screenX = (ndcX + 1.0f) * 0.5f * static_cast<float>(Engine::WindowDX::kW);
        float screenY = (1.0f - ndcY) * 0.5f * static_cast<float>(Engine::WindowDX::kH);
        return {screenX, screenY};
    };

    for (const auto& bone : debugBones) {
        Engine::Matrix4x4 finalMat = Engine::Matrix4x4::Multiply(bone.globalMatrix, worldMat);
        Engine::Vector3 pCurrent = { finalMat.m[3][0], finalMat.m[3][1], finalMat.m[3][2] };
        Engine::Vector3 pParent = Engine::TransformCoord(bone.parentPos, worldMat);
        
        // Draw bone link
        renderer_->DrawLine3D(pParent, pCurrent, boneColor, true); // xray = true
        
        // Local axes
        Engine::Vector3 localX = { finalMat.m[0][0], finalMat.m[0][1], finalMat.m[0][2] };
        Engine::Vector3 localY = { finalMat.m[1][0], finalMat.m[1][1], finalMat.m[1][2] };
        Engine::Vector3 localZ = { finalMat.m[2][0], finalMat.m[2][1], finalMat.m[2][2] };
        
        // RightHand: Attach Sword
        if (bone.name == "RightHand" || bone.name == "mixamorig:RightHand") {
            // Add a local offset for the sword (scale, rotation, translation) to make it look right
            Engine::Matrix4x4 offset = Engine::Matrix4x4::MakeAffineMatrix(
                {1.0f, 1.0f, 1.0f}, // Scale
                {0.0f, 0.0f, 0.0f}, // Rotate (0, 0, 0 to test forward direction)
                {0.0f, 0.0f, 0.0f} // Translate
            );
            Engine::Matrix4x4 swordWorld = Engine::Matrix4x4::Multiply(offset, finalMat);
            
            swordPos_ = { swordWorld.m[3][0], swordWorld.m[3][1], swordWorld.m[3][2] };
            
            // Assume the sword points along the local Y axis (typical for Mixamo right hand)
            Engine::Vector3 swordForward = { swordWorld.m[1][0], swordWorld.m[1][1], swordWorld.m[1][2] };
            
            // Normalize just in case
            float len = std::sqrt(swordForward.x*swordForward.x + swordForward.y*swordForward.y + swordForward.z*swordForward.z);
            if (len > 0.0001f) {
                swordForward.x /= len; swordForward.y /= len; swordForward.z /= len;
            } else {
                swordForward = {0.0f, 1.0f, 0.0f};
            }
            
            swordDir_ = swordForward;
            
            // The sword is quite large, let's offset the tip by its approximate length (e.g., 5.0f units)
            float swordLength = 5.0f;
            swordTip_ = {
                swordPos_.x + swordForward.x * swordLength,
                swordPos_.y + swordForward.y * swordLength,
                swordPos_.z + swordForward.z * swordLength
            };
            
            if (swordMesh_ > 0) {
                renderer_->DrawMeshInstanced(swordMesh_, swordTex_, swordWorld, {1.0f, 1.0f, 1.0f, 1.0f}, "Default");
            }
        }
        
        // LeftHand: Emit particles
        if (bone.name == "LeftHand" || bone.name == "mixamorig:LeftHand") {
            if (particleSys_) {
                Engine::Vector3 pos = pCurrent;
                Engine::Vector3 vel = {0.0f, 2.0f, 0.0f}; // Upwards
                Engine::Vector3 acc = {0.0f, -0.5f, 0.0f}; // Gravity slightly down
                Engine::Vector3 sScale = {0.3f, 0.3f, 0.3f};
                Engine::Vector3 eScale = {0.0f, 0.0f, 0.0f};
                Engine::Vector4 sColor = {1.0f, 0.6f, 0.1f, 1.0f}; // Orange/Yellow
                Engine::Vector4 eColor = {1.0f, 0.1f, 0.1f, 0.0f}; // Fades to red
                float life = 0.5f;
                
                // Add some random spread
                float rx = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;
                float rz = ((float)(rand() % 100) / 100.0f - 0.5f) * 1.0f;
                vel.x += rx;
                vel.z += rz;
                
                particleSys_->Emit(pos, vel, acc, sScale, eScale, sColor, eColor, life);
            }
        }
        
        auto norm = [](Engine::Vector3 v) { float l = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); return l > 0 ? Engine::Vector3{v.x/l, v.y/l, v.z/l} : v; };
        localX = norm(localX);
        localY = norm(localY);
        localZ = norm(localZ);
        
        float axisLen = 0.5f;
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localX.x * axisLen, pCurrent.y + localX.y * axisLen, pCurrent.z + localX.z * axisLen}, {1,0,0,1}, true);
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localY.x * axisLen, pCurrent.y + localY.y * axisLen, pCurrent.z + localY.z * axisLen}, {0,1,0,1}, true);
        renderer_->DrawLine3D(pCurrent, {pCurrent.x + localZ.x * axisLen, pCurrent.y + localZ.y * axisLen, pCurrent.z + localZ.z * axisLen}, {0,0,1,1}, true);
        
        // Name text
        auto screenPos = WorldToScreen(pCurrent);
        if (screenPos.first >= 0) {
            renderer_->DrawString(bone.name, screenPos.first, screenPos.second, 0.4f, {1,1,1,1});
        }
    }
}

void AssignmentScene::DrawUI() {
    if (renderer_) {
        renderer_->DrawString("Assignment Scene", 20, 20, 1.0f, {1, 1, 1, 1});
        renderer_->DrawString("WASD/Pad: Move to blend walk animation", 20, 70, 0.5f, {1, 1, 1, 1});
        renderer_->DrawString("Mouse/Right stick: Camera", 20, 100, 0.5f, {1, 1, 1, 1});
        renderer_->DrawString("LMB/Space/A: Attack cube enemy", 20, 130, 0.5f, {1, 1, 1, 1});
        renderer_->DrawString("RMB/Shift/LB: Guard focus", 20, 160, 0.5f, {0.7f, 0.9f, 1, 1});
        renderer_->DrawString("Approach cube: enemy attacks", 20, 190, 0.5f, {1, 0.8f, 0.3f, 1});
        renderer_->DrawString("ESC: Back to Title", 20, 220, 0.5f, {1, 1, 1, 1});
        
        // Show current blend state
        std::string debugStr = "Anim: " + currentAnim_ + " | Blend: " + std::to_string(1.0f - blendFactor_);
        renderer_->DrawString(debugStr, 20, 250, 0.4f, {0, 1, 0, 1});
        renderer_->DrawString(std::string("Guard: ") + (isGuarding_ ? "ON" : "OFF"), 20, 275, 0.45f, isGuarding_ ? Engine::Vector4{0.6f, 0.9f, 1, 1} : Engine::Vector4{1, 1, 1, 1});
        renderer_->DrawString("Player HP: " + std::to_string(playerHp_) + " / 100", 20, 300, 0.45f, playerHitFlash_ > 0.0f ? Engine::Vector4{1, 0.15f, 0.1f, 1} : Engine::Vector4{1, 1, 1, 1});
        renderer_->DrawString("Enemy Cube HP: " + std::to_string(enemyHp_) + " / 60", 20, 325, 0.45f, enemyHitFlash_ > 0.0f ? Engine::Vector4{1, 1, 0.2f, 1} : Engine::Vector4{1, 1, 1, 1});
        
        // PostEffect Guide
        float sw = (float)Engine::WindowDX::kW;
        float sx = sw - 430.0f;
        float sy = 20.0f;
        renderer_->DrawString("Current PostEffect: " + currentEffect_, sx, sy, 0.6f, {1, 1, 0, 1});
        sy += 30.0f;
        renderer_->DrawString("Reason: " + currentEffectReason_, sx, sy, 0.42f, {1, 1, 1, 1}); sy += 24.0f;
        renderer_->DrawString("Queued events: " + std::to_string(postEffectQueue_.size()), sx, sy, 0.42f, {0.8f, 1, 1, 1}); sy += 28.0f;
        renderer_->DrawString("Grayscale: enemy detects player", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("Outline: lock-on range", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("DepthOutline: enemy windup", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("RadialBlur: player attack", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("Gaussian: successful hit", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("BoxFilter: guard/block", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("Random: damage glitch", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("Vignetting: low HP", sx, sy, 0.38f, {1, 1, 1, 1}); sy += 20.0f;
        renderer_->DrawString("Dissolve: enemy defeated", sx, sy, 0.38f, {1, 1, 1, 1});
    }
}

void AssignmentScene::DrawEditor() {
#ifdef USE_IMGUI
    if (ImGui::Begin("GPU Particle Editor")) {
        if (ImGui::CollapsingHeader("Emitter 0 (Tornado)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!emitters_.empty()) {
                auto& e = emitters_[0];
                ImGui::DragFloat3("Position##0", &e.emitPos.x, 0.1f);
                ImGui::DragFloat("Emit Rate##0", &e.emitRate, 10.0f, 0.0f, 10000.0f);
                ImGui::DragFloat3("Velocity##0", &e.emitVel.x, 0.1f);
                ImGui::DragFloat("Life##0", &e.emitLife, 0.1f, 0.1f, 10.0f);
                
                const char* shapes[] = { "Point", "Sphere", "Box", "Mesh" };
                int currentShape = e.emitterShape;
                if (ImGui::Combo("Shape##0", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                    e.emitterShape = currentShape;
                }
                ImGui::DragFloat3("Extents##0", &e.emitterExtents.x, 0.1f);
                
                ImGui::ColorEdit4("Color Start##0", &e.colorStart.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::ColorEdit4("Color End##0", &e.colorEnd.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                
                const char* fields[] = { "None", "Gravity", "Wind", "Tornado" };
                int currentField = e.fieldType;
                if (ImGui::Combo("Field##0", &currentField, fields, IM_ARRAYSIZE(fields))) {
                    e.fieldType = currentField;
                }
                ImGui::DragFloat4("Field Params##0", &e.fieldParams.x, 0.1f);
                
                const char* types[] = { "Default", "Trail", "Lit" };
                int currentType = e.particleType;
                if (ImGui::Combo("Type##0", &currentType, types, IM_ARRAYSIZE(types))) {
                    e.particleType = currentType;
                }
            }
        }
        
        if (ImGui::CollapsingHeader("Emitter 1 (Sword Mesh)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (emitters_.size() > 1) {
                auto& e = emitters_[1];
                ImGui::DragFloat("Emit Rate##1", &e.emitRate, 10.0f, 0.0f, 10000.0f);
                ImGui::DragFloat3("Velocity##1", &e.emitVel.x, 0.1f);
                ImGui::DragFloat("Life##1", &e.emitLife, 0.1f, 0.1f, 10.0f);
                
                int currentShape = e.emitterShape;
                const char* shapes[] = { "Point", "Sphere", "Box", "Mesh" };
                if (ImGui::Combo("Shape##1", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
                    e.emitterShape = currentShape;
                }
                
                ImGui::ColorEdit4("Color Start##1", &e.colorStart.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::ColorEdit4("Color End##1", &e.colorEnd.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                
                const char* types[] = { "Default", "Trail", "Lit" };
                int currentType = e.particleType;
                if (ImGui::Combo("Type##1", &currentType, types, IM_ARRAYSIZE(types))) {
                    e.particleType = currentType;
                }
            }
        }
    }
    ImGui::End();
#endif
}

} // namespace Game
