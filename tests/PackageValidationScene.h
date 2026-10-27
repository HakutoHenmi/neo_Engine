#pragma once
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/Scenes/GameScene.h"
#include "../Game/Scenes/GameOverScene.h"
#include "../Game/UI/CreditsUI.h"
#include "../Game/UI/GraphicsUI.h"
#include "../Game/Chrono/InkRules.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>

// Explicit --package-smoke only: exercises the actual Release renderer and assets.
// Runs in Release from the EXE directory, with no repository SDK lookup.
class DlssPackageValidationScene final : public Engine::IScene {
    Engine::WindowDX* window_=nullptr;Game::GameScene game_;int frame_=0;bool passed_=true;
    void Capture(const wchar_t* path){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);passed_&=SUCCEEDED(hr);
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("DlssCheck");
        Engine::SceneParameters p;p.stagePath=Game::Data::MainStagePath();game_.Initialize(dx,p);
    }
    void Update()override{
        if(frame_<90)game_.Update();++frame_;auto* r=Engine::Renderer::GetInstance();
        if(frame_==80)Capture(L"DlssCheck/game.png");
        if(frame_==120){Capture(L"DlssCheck/settings.png");
            passed_&=r->SupportsDlss()&&r->DlssActive()&&r->DlssEvaluatedFrames()>60;
            std::ofstream("DlssCheck/result.txt")<<(passed_?"PASS":"FAIL")<<" Release DLSS: frames="<<r->DlssEvaluatedFrames()<<" status="<<r->DlssStatus();
            PostQuitMessage(passed_?0:2);
        }
    }
    void Draw()override{
        if(frame_<90)game_.Draw();else {Game::UI::Canvas ui(Engine::Renderer::GetInstance());Game::UI::DrawGraphics(ui,*Engine::Renderer::GetInstance());}
    }
};

class PackageValidationScene final : public Engine::IScene {
    Engine::WindowDX* window_=nullptr;
    std::unique_ptr<Engine::IScene> scene_;
    int page_=0,frame_=0;
    bool passed_=true;
    void Load(){
        if(page_>=8){auto* game=static_cast<Game::GameScene*>(scene_.get());auto view=game->GetComponents().view<Game::Chrono::InkPlayer>();auto& state=view.get<Game::Chrono::InkPlayer>(*view.begin()).rogue;
            if(page_==8)state.Gain(20);else {for(auto& rank:state.ranks)rank=1;game->SetPaused(true);}return;}
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else if(page_==7)scene_=std::make_unique<Game::GameScene>();
        else if(page_==2)scene_=std::make_unique<Game::GameOverScene>();
        else scene_.reset();
        if(scene_){Engine::SceneParameters p;p.stagePath=Game::Data::MainStagePath();scene_->Initialize(window_,p);}
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("PackageCheck");Load();
    }
    void Update()override{
        if(page_>=7)scene_->Update();
        if(++frame_<90)return;
        auto* r=Engine::Renderer::GetInstance();
        passed_&=r->MeasureTextWidth(Game::UI::GameTitle(),1,Game::UI::JapaneseFont)>0;
        passed_&=r->MeasureTextWidth("BEGIN EXPEDITION",1,Game::UI::Font)>0;
        // 道路の材質は配布版の NTC ランタイムで復元できること (失敗すると空が返る)
        if(page_==7){auto road=r->LoadNtcTextureSet("Resources/Textures/PolyHaven/asphalt_02_material.ntc",{"albedo","roughness","ao"});passed_&=road.size()==3;
            // 復元した BCn を保存し、実験の GPU readback と外部で照合できるようにする
            const wchar_t* names[]={L"albedo",L"roughness",L"ao"};
            for(size_t i=0;i<road.size();++i){DirectX::ScratchImage bcn;
                auto saved=DirectX::CaptureTexture(window_->Queue(),r->GetTextureResource(road[i]),false,bcn,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                if(SUCCEEDED(saved))saved=DirectX::SaveToDDSFile(bcn.GetImages(),bcn.GetImageCount(),bcn.GetMetadata(),DirectX::DDS_FLAGS_NONE,(std::wstring(L"PackageCheck/ntc-")+names[i]+L".dds").c_str());
                passed_&=SUCCEEDED(saved);}
            // ゲーム中の HUD (変形のステッカー・アイコン・体の粒・見せ場の暗幕) が配布物に入っていること
            for(const char* hud:{"blob","sticker","spotlight","form_tire","form_hydrant","form_signal","form_hammer","form_sword","form_plane"})
                passed_&=r->LoadTexture2D(std::string("Resources/Textures/UI/HUD/")+hud+".png")!=0;
            // 街の材質は GPU でミップを作る。CPU (DirectXTex) の従来の結果と段ごとに比べ、PSNR を残す
            std::ofstream mipReport("PackageCheck/gpu-mips.txt");
            for(auto [file,srgb]:{std::pair{"Resources/Textures/PolyHaven/concrete_floor_worn_001_diff_2k.jpg",true},std::pair{"Resources/Textures/PolyHaven/concrete_floor_worn_001_Rough_2k.jpg",false}}){
                DirectX::ScratchImage gpu,source,cpu;const DXGI_FORMAT view=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
                auto hr=DirectX::CaptureTexture(window_->Queue(),r->GetTextureResource(r->LoadTexture2D(file,srgb)),false,gpu,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                if(SUCCEEDED(hr))hr=DirectX::LoadFromWICFile(std::filesystem::path(file).c_str(),DirectX::WIC_FLAGS_NONE,nullptr,source);
                DirectX::ScratchImage rgba;const DirectX::Image* top=SUCCEEDED(hr)?source.GetImage(0,0,0):nullptr;
                if(top&&top->format!=DXGI_FORMAT_R8G8B8A8_UNORM){hr=DirectX::Convert(*top,DXGI_FORMAT_R8G8B8A8_UNORM,DirectX::TEX_FILTER_DEFAULT,DirectX::TEX_THRESHOLD_DEFAULT,rgba);top=SUCCEEDED(hr)?rgba.GetImage(0,0,0):nullptr;}
                if(top){DirectX::Image base=*top;base.format=view;hr=DirectX::GenerateMipMaps(base,DirectX::TEX_FILTER_DEFAULT,0,cpu);}
                passed_&=SUCCEEDED(hr)&&gpu.GetMetadata().mipLevels==cpu.GetMetadata().mipLevels;
                mipReport<<file<<(srgb?" (sRGB)":" (linear)")<<" mips gpu="<<gpu.GetMetadata().mipLevels<<" cpu="<<cpu.GetMetadata().mipLevels;
                for(size_t level=0;SUCCEEDED(hr)&&level<gpu.GetMetadata().mipLevels&&level<cpu.GetMetadata().mipLevels;++level){
                    // どちらも保存された 8 ビット値として比べる (sRGB の変換はしない)
                    DirectX::Image a=*gpu.GetImage(level,0,0),b=*cpu.GetImage(level,0,0);a.format=b.format=DXGI_FORMAT_R8G8B8A8_UNORM;
                    float mse=0;if(SUCCEEDED(DirectX::ComputeMSE(a,b,mse,nullptr,DirectX::CMSE_IGNORE_ALPHA)))mipReport<<" L"<<level<<"="<<(mse>0?10*std::log10(1/mse):999.f)<<"dB";}
                mipReport<<'\n';}}
        DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(L"PackageCheck/page-"+std::to_wstring(page_)+L".png").c_str());
        passed_&=SUCCEEDED(hr);
        if(++page_==10){std::ofstream("PackageCheck/result.txt")<<(passed_?"PASS":"FAIL")<<" Release package: title, select, battle (NTC road, HUD textures), upgrade selection, palette pause, result, credits, both fonts";PostQuitMessage(passed_?0:2);return;}
        frame_=0;Load();
    }
    void Draw()override{
        if(scene_)scene_->Draw();else {Game::UI::Canvas ui(Engine::Renderer::GetInstance());Game::UI::DrawCredits(ui,page_-3);}
    }
};
