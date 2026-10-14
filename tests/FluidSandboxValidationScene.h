#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/Editor/EditorUI.h"
#include "../Engine/Time/TimeManager.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

// Opt-in integration scene; never saves editor assets or modifies game settings.
class FluidSandboxValidationScene final : public Game::GameScene {
    Engine::WindowDX* window_ = nullptr;
    unsigned frames_ = 0;
    bool passed_ = true;
    std::ofstream report_;
    void Check(bool condition, const char* message) {
        passed_ &= condition;
        report_ << (condition ? "PASS " : "FAIL ") << message << '\n';
        report_.flush();
    }
    void Capture(const wchar_t* path) {
        DirectX::ScratchImage capture;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,capture,
                                       D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr)) hr=DirectX::SaveToWICFile(*capture.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
                                                 DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);
        Check(SUCCEEDED(hr),"capture glass / water frame");
    }
public:
    void Initialize(Engine::WindowDX* window,const Engine::SceneParameters& params) override {
        window_=window;std::filesystem::create_directories("tests/out");
        auto diagnostic=params;diagnostic.stagePath="Resources/Scenes/fluid_sandbox.json";
        Game::GameScene::Initialize(window,diagnostic);
        SetIsPlaying(false);
        report_.open("tests/out/fluid-sandbox-validation.txt");
        auto* renderer=Engine::Renderer::GetInstance();
        Check(renderer->IsFluidVolumeReady(),"fluid volume ready");
        Check(std::find(renderer->GetShaderNames().begin(),renderer->GetShaderNames().end(),"Glass")!=renderer->GetShaderNames().end(),"Glass pipeline registered");
        auto& registry=GetComponents();
        auto source=FindObjectByName("Water Emitter");
        if(!registry.valid(source)) throw std::runtime_error("Sandbox fixture did not load");
        auto& emitter=registry.get<Game::FluidEmitterComponent>(source);
        Check(emitter.retainWater && emitter.particleLimit==16000 && !emitter.enabled,"sandbox emitter defaults");
        Check(registry.view<Game::BoxColliderComponent>().size()==5,"five tank colliders");
        Check(GetEntitiesByTag(Game::TagType::Player).empty(),"sandbox has no game player");
        // Roundtrip exercises actual editor serialization and default restoration.
        emitter.emitCountPerFrame=40;emitter.particleLimit=2400;
        Game::EditorUI::LoadFromMemory(this,Game::EditorUI::SaveToMemory(this));
        source=FindObjectByName("Water Emitter");
        auto& restored=registry.get<Game::FluidEmitterComponent>(source);
        Check(restored.retainWater && restored.particleLimit==2400 && restored.emitCountPerFrame==40 && restored.emittedParticles==0,"emitter settings survive scene roundtrip");
        auto tank=FindObjectByName("Glass Tank");
        registry.get<Game::TransformComponent>(tank).translate.x=4;
        const auto world=GetWorldMatrix(static_cast<int>(source));
        Check(std::abs(world.m[3][0]-4)<.001f && std::abs(world.m[3][1]-6.6f)<.001f,"nozzle inherits tank world position");
        GetCamera().SetPosition(13,8,-12);GetCamera().SetRotation(std::atan2(5.4f,15.f),std::atan2(-9.f,12.f),0);
        restored.enabled=true;
        SelectEntity(source);
        Game::EditorUI::OpenFluidSandbox();
    }
    void Update() override {
        Game::GameScene::Update();
        auto& emitter=GetComponents().get<Game::FluidEmitterComponent>(FindObjectByName("Water Emitter"));
        if(++frames_==15) Capture(L"tests/out/fluid-sandbox-empty.png");
        if(frames_==240) {
            Check(emitter.emittedParticles==2400,"emission stops exactly at the total limit");
            Capture(L"tests/out/fluid-sandbox-filled.png");
            ResetFluidPreview();
            Check(emitter.emittedParticles==0 && emitter.emissionRemainder==0,"reset clears runtime emission count");
            emitter.enabled=false;
        }
        if(frames_==245) {
            Capture(L"tests/out/fluid-sandbox-reset.png");
            report_ << (passed_ ? "PASS" : "FAIL") << " fluid sandbox integration\n";
            report_.flush();PostQuitMessage(passed_ ? 0 : 2);
        }
    }
    void Draw() override {
        Engine::TimeManager::GetInstance().Update(1.f/60);
        Game::GameScene::Draw();
    }
};
