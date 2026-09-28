#pragma once
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/Scenes/GameScene.h"
#include "../Game/Scenes/GameOverScene.h"
#include "../Game/UI/CreditsUI.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>

// Explicit --package-smoke only: exercises the actual Release renderer and assets.
class PackageValidationScene final : public Engine::IScene {
    Engine::WindowDX* window_=nullptr;
    std::unique_ptr<Engine::IScene> scene_;
    int page_=0,frame_=0;
    bool passed_=true;
    void Load(){
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else if(page_==7)scene_=std::make_unique<Game::GameScene>();
        else if(page_==2)scene_=std::make_unique<Game::GameOverScene>();
        else scene_.reset();
        if(scene_){Engine::SceneParameters p;p.stagePath="Resources/Scenes/chrono.json";scene_->Initialize(window_,p);}
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("PackageCheck");Load();
    }
    void Update()override{
        if(page_==7)scene_->Update();
        if(++frame_<90)return;
        auto* r=Engine::Renderer::GetInstance();
        passed_&=r->MeasureTextWidth(Game::UI::GameTitle,1,Game::UI::JapaneseFont)>0;
        passed_&=r->MeasureTextWidth("BEGIN EXPEDITION",1,Game::UI::Font)>0;
        DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(L"PackageCheck/page-"+std::to_wstring(page_)+L".png").c_str());
        passed_&=SUCCEEDED(hr);
        if(++page_==8){std::ofstream("PackageCheck/result.txt")<<(passed_?"PASS":"FAIL")<<" Release package: title, select, battle, result, credits, both fonts";PostQuitMessage(passed_?0:2);return;}
        frame_=0;Load();
    }
    void Draw()override{
        if(scene_)scene_->Draw();else {Game::UI::Canvas ui(Engine::Renderer::GetInstance());Game::UI::DrawCredits(ui,page_-3);}
    }
};
