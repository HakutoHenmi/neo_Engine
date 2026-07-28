#include "AssignmentScene.h"
#include "../../Engine/Input.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <cmath>
#include <algorithm>
#include <imgui.h>

namespace Game {

AssignmentScene::~AssignmentScene() {
    // Cleanup is handled by renderer
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
        meshEmitter.emitterExtents = { 0.0f, 0.0f, 0.0f };
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
    
    currentAnim_ = "Mutant Idle";
    prevAnim_ = "Mutant Idle";
    
    cameraDistance_ = 12.0f;
    cameraAngleX_ = 0.3f;
    cameraAngleY_ = 0.0f;
}

void AssignmentScene::Update() {
    float dt = 1.0f / 60.0f; // Fixed time step for simplicity
    
    auto* input = Engine::Input::GetInstance();
    
    // Check if we want to go back to Title (ESC)
    if (input->Trigger(0x01)) { // DIK_ESCAPE
        Engine::SceneManager::GetInstance()->RequestChange("Title");
        return;
    }
    
    // PostEffect switching (0-9 keys)
    if (input->Trigger(0x0B)) currentEffect_ = "Dissolve";         // 0
    if (input->Trigger(0x02)) currentEffect_ = "Default";          // 1
    if (input->Trigger(0x03)) currentEffect_ = "Grayscale";        // 2
    if (input->Trigger(0x04)) currentEffect_ = "GaussianFilter";   // 3
    if (input->Trigger(0x05)) currentEffect_ = "OutlinePost";      // 4
    if (input->Trigger(0x06)) currentEffect_ = "DepthBasedOutline";// 5
    if (input->Trigger(0x07)) currentEffect_ = "RadialBlur";       // 6
    if (input->Trigger(0x08)) currentEffect_ = "Vignetting";       // 7
    if (input->Trigger(0x09)) currentEffect_ = "BoxFilter";        // 8
    if (input->Trigger(0x0A)) currentEffect_ = "Random";           // 9
    
    if (renderer_) {
        renderer_->SetPostEffect(currentEffect_);
        
        // Reset post process params to avoid gray blur / noise from previous states
        auto params = renderer_->GetPostProcessParams();
        params.noiseStrength = 0.0f;
        params.distortion = 0.0f;
        params.chromaShift = 0.0f;
        params.vignette = 0.0f;
        params.scanline = 0.0f;
        params.time += dt;
        renderer_->SetPostProcessParams(params);
    }
    
    // Update particle system
    if (particleSys_) {
        particleSys_->Update(dt);
    }

    // Pad input (Left stick) using XInput
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    bool aPressed = false;
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
    
    // Clamp
    lx = std::fmaxf(-1.0f, std::fminf(1.0f, lx));
    ly = std::fmaxf(-1.0f, std::fminf(1.0f, ly));
    rx = std::fmaxf(-1.0f, std::fminf(1.0f, rx));
    ry = std::fmaxf(-1.0f, std::fminf(1.0f, ry));
    
    float magnitude = std::sqrt(lx * lx + ly * ly);
    
    bool wasMoving = isMoving_;
    isMoving_ = (magnitude > 0.0f);
    
    if (aPressed && !isAttacking_) {
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
                animTime_ += dt * a.ticksPerSecond * 0.7f; // Slow down slightly for heavier feel
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
                prevAnimTime_ += dt * a.ticksPerSecond * 0.7f;
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
    
    // Camera controls (Right stick)
    if (std::abs(rx) > 0.2f) cameraAngleY_ += rx * dt * 2.0f;
    if (std::abs(ry) > 0.2f) cameraAngleX_ -= ry * dt * 2.0f;
    
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
        
        // Update emitter to spawn at sword tip and shoot outward
        emitters_[1].emitPos = { swordTip_.x, swordTip_.y, swordTip_.z };
        
        // Shoot particles in the direction the sword is pointing (with some speed)
        float shootSpeed = 10.0f;
        emitters_[1].emitVel = { swordDir_.x * shootSpeed, swordDir_.y * shootSpeed, swordDir_.z * shootSpeed };
        
        // Change shape to Sphere if it was Mesh, so it acts like a nozzle at the tip
        emitters_[1].emitterShape = 1; // Sphere
        emitters_[1].emitterExtents = { 0.5f, 0.0f, 0.0f }; // Small radius
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
        renderer_->DrawString("Pad: Move to blend walk animation", 20, 70, 0.5f, {1, 1, 1, 1});
        renderer_->DrawString("ESC: Back to Title", 20, 100, 0.5f, {1, 1, 1, 1});
        
        // Show current blend state
        std::string debugStr = "Anim: " + currentAnim_ + " | Blend: " + std::to_string(1.0f - blendFactor_);
        renderer_->DrawString(debugStr, 20, 130, 0.4f, {0, 1, 0, 1});
        
        // PostEffect Guide
        float sw = (float)Engine::WindowDX::kW;
        float sx = sw - 300.0f;
        float sy = 20.0f;
        renderer_->DrawString("Current PostEffect: " + currentEffect_, sx, sy, 0.6f, {1, 1, 0, 1});
        sy += 30.0f;
        renderer_->DrawString("[1] Default", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[2] Grayscale", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[3] GaussianFilter", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[4] LuminanceBasedOutline", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[5] DepthBasedOutline", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[6] RadialBlur", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[7] Vignetting", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[8] BoxFilter", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[9] Random", sx, sy, 0.5f, {1, 1, 1, 1}); sy += 25.0f;
        renderer_->DrawString("[0] Dissolve", sx, sy, 0.5f, {1, 1, 1, 1});
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
