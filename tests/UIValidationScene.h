#pragma once
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/Scenes/GameOverScene.h"
#include "../Game/UI/GameUI.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
#include "../Game/Scenes/GameScene.h"
#include "../Game/Chrono/ChronoComponents.h"

// An opt-in defeat fixture for testing real mouse/keyboard result navigation.
// All subsequent input, cursor and scene transitions use production code.
class UIResultValidationScene final : public Game::GameScene {
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);
        auto player=FindObjectByName("Player");
        GetRegistry().emplace<Game::Chrono::ControlFrame>(player);
        GetRegistry().get<Game::Chrono::Player>(player).mass=0;
    }
};

// Opt-in GPU captures of production menus, with no synthetic OS input.
class UIValidationScene final : public Engine::IScene {
    Engine::WindowDX* window_=nullptr;
    std::unique_ptr<Engine::IScene> scene_;
    int frame_=0, page_=0;
    bool passed_=true;
    void Load() {
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else if(page_==2)scene_=std::make_unique<Game::GameOverScene>();
        else scene_.reset();
        if(scene_)scene_->Initialize(window_,{});
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        window_=dx;std::filesystem::create_directories("tests/out");
        auto* r=Engine::Renderer::GetInstance();
        passed_=r->InitTextSystem(Game::UI::Font,64);
        passed_&=r->InitTextSystem(Game::UI::JapaneseFont,64);
        const float titleWidth=r->MeasureTextWidth(Game::UI::GameTitle,60.f/64,Game::UI::JapaneseFont);
        passed_&=titleWidth>0 && titleWidth<760;
        Load();
    }
    void Update()override {
        if(++frame_!=30)return;
        DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        const wchar_t* paths[]={L"tests/out/ui-title.png",L"tests/out/ui-select.png",L"tests/out/ui-defeat.png",L"tests/out/ui-clear.png",L"tests/out/ui-pause.png"};
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),paths[page_]);
        passed_&=SUCCEEDED(hr);
        if(++page_==5){std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" font, title fit, five GPU captures";PostQuitMessage(passed_?0:2);return;}
        frame_=0;Load();
    }
    void Draw()override {
        if(scene_){scene_->Draw();return;}
        Game::UI::Canvas ui(Engine::Renderer::GetInstance());
        if(page_==3)ui.Result(true,"TIME  02:34.56","CORES  3 / 3     |     HITS TAKEN  12");
        else ui.Pause();
    }
};
