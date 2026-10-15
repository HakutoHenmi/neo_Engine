#pragma once
// Debug-only gallery: draws every player-facing screen with sample data and saves
// tests/out/gallery-*.png, so UI art changes can be reviewed without playing.
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/UI/GameUI.h"
#include "../Game/UI/GraphicsUI.h"
#include "../Game/UI/CreditsUI.h"
#include "../Game/UI/RogueliteUI.h"
#include "../Game/UI/LoadingUI.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <memory>

class UIGalleryScene final : public Engine::IScene {
    Engine::WindowDX* window_=nullptr;
    std::unique_ptr<Engine::IScene> scene_;
    int page_=0,frame_=0;
    static constexpr int kPages=11;
    static const wchar_t* PagePath(int page){
        static const wchar_t* paths[kPages]={L"tests/out/gallery-title.png",L"tests/out/gallery-select.png",L"tests/out/gallery-result-win.png",
            L"tests/out/gallery-result-lose.png",L"tests/out/gallery-pause.png",L"tests/out/gallery-palette.png",L"tests/out/gallery-cards.png",
            L"tests/out/gallery-graphics.png",L"tests/out/gallery-credits.png",L"tests/out/gallery-loading.png",L"tests/out/gallery-splash.png"};
        return paths[page];
    }
    void Load(){
        scene_.reset();
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        if(scene_)scene_->Initialize(window_,{});
    }
    static Game::Chrono::Roguelite SampleRoguelite(bool menu){
        Game::Chrono::Roguelite state;state.level=4;state.pending=1;state.rerolls=2;
        for(int i:{0,1,3,5,8,12})state.ranks[size_t(i)]=1+i%3;
        if(menu){state.menu=true;state.menuBlend=1;state.cardAge=5;state.offers={{2,7,13}};state.selected=1;}
        return state;
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("tests/out");Load();
    }
    void Update()override{
        if(scene_)scene_->Update();
        if(++frame_!=(page_==0?150:40))return;
        DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),PagePath(page_));
        if(++page_==kPages){PostQuitMessage(0);return;}
        frame_=0;Load();
    }
    void Draw()override{
        if(scene_){scene_->Draw();return;}
        auto* renderer=Engine::Renderer::GetInstance();
        Game::UI::Canvas ui(renderer);
        switch(page_){
        case 2:ui.Result(true,"02:34.56","KOs 1000  |  BEST CHAIN 214  |  CORES 3 / 3");break;
        case 3:ui.Result(false,"04:12.08","KOs 612  |  BEST CHAIN 88  |  CORES 1 / 3");break;
        case 4:ui.Pause();break;
        case 5:{auto state=SampleRoguelite(false);Game::UI::RoguePalette(ui,state);ui.Button(Game::UI::GraphicsButton,"G: GRAPHICS",false,"xbox_button_view");break;}
        case 6:{ui.Fill({0,0,1280,720},{.25f,.4f,.22f,1});auto state=SampleRoguelite(true);Game::UI::RogueSelection(ui,state);break;}
        case 7:Game::UI::DrawGraphics(ui,*renderer);break;
        case 8:Game::UI::DrawCredits(ui,1);break;
        case 9:{Engine::LoadingScreen::State state;state.scene="Game+Engine";state.elapsed=3;state.progress=.46f;Game::UI::Loading::DrawLoadingFrame(state);break;}
        default:{Engine::LoadingScreen::State state;state.scene="Startup";state.startup=true;state.elapsed=3.4f;state.progress=1;state.finished=true;Game::UI::Loading::DrawLoadingFrame(state);break;}
        }
    }
};
