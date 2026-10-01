#pragma once
#include "../Game/Scenes/TitleScene.h"
#include "../Game/Scenes/SelectScene.h"
#include "../Game/Scenes/GameOverScene.h"
#include "../Game/UI/GameUI.h"
#include "../Game/UI/SceneMusic.h"
#include "../Game/UI/CreditsUI.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>
#include "../Game/Scenes/GameScene.h"
#include "../Game/Chrono/ChronoComponents.h"
#include "../Game/Chrono/InkRules.h"

class BeamCameraValidationScene final : public Game::GameScene {
    Engine::WindowDX* window_=nullptr;int tier_=1;float start_=0;bool launched_=false,captured_=false,passed_=true,chargeChecked_=false;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);GetRegistry().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override{
        using namespace Game;using namespace Game::Chrono;auto& r=GetRegistry();auto player=FindObjectByName("Player");
        auto& ink=r.get<InkPlayer>(player);auto& p=r.get<Player>(player);auto& input=r.get<ControlFrame>(player);input={};input.pitch=-.15f;p.invincible=1;
        r.get<CreatureBoss>(FindObjectByName("Boss")).sweepTime=-100;
        if(!launched_){p.mass=800;ink.capacity=760;ink.reserve=40;ink.phase=SlimePhase::Charging;ink.charge=tier_==1?150.f:tier_==2?400.f:700.f;ink.previousAttack=true;launched_=true;start_=p.stats.seconds;}
        input.attack=p.stats.seconds-start_<.65f;
        Game::GameScene::Update();
        if(!chargeChecked_&&ink.phase==SlimePhase::Charging&&p.stats.seconds-start_>.5f){
            passed_&=ink.beamView>.7f&&(tier_==1?ink.heavyView==0:tier_==2?(ink.heavyView>.3f&&ink.heavyView<.7f):ink.heavyView>.99f);
            chargeChecked_=true;
        }
        if(!captured_&&ink.phase==SlimePhase::Firing&&ink.phaseAge>.3f){
            passed_&=ink.beamView>.99f&&(tier_!=3||ink.heavyView>.99f);
            DirectX::ScratchImage image;auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),(L"tests/out/beam-camera-"+std::to_wstring(tier_)+L".png").c_str());
            passed_&=SUCCEEDED(hr);captured_=true;
        }
        if(p.stats.seconds-start_>2.8f){passed_&=chargeChecked_&&captured_&&ink.beamView==0&&ink.heavyView==0;
            if(++tier_>3){std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" three beam cameras, GPU captures, eased return";PostQuitMessage(passed_?0:2);}
            launched_=false;captured_=false;chargeChecked_=false;
        }
    }
};
class DodgeValidationScene final : public Game::GameScene {
    bool perfectFollowup_=false,normalFollowup_=false,normalProtected_=false,blur_=false;
    bool grazeShot_=false,bonusHpSafe_=false;
    int phase_=0;float start_=0;bool success_=false,paint_=false,slow_=false,gray_=false,late_=false,liquid_=false,holdEnds_=false,autoSlide_=false;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);
        GetRegistry().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& p=r.get<Player>(player);auto& ink=r.get<InkPlayer>(player);auto& input=r.get<ControlFrame>(player);input={};
        if(phase_==0)input.move={1,0,0};
        if(phase_==1)input.dodge=true; // A held button must not extend/retrigger liquefaction.
        if(phase_!=5)r.get<CreatureBoss>(boss).sweepTime=-100;
        auto shoot=[&](){auto e=r.create();auto pos=r.get<TransformComponent>(player).translate;pos.z+=3;
            r.emplace<TransformComponent>(e).translate=pos;r.emplace<SlimeFeather>(e).velocity={0,0,-300};};
        if(phase_==0&&p.stats.seconds>.2f){input.dodge=true;phase_=1;start_=p.stats.seconds;}
        if(phase_==1&&p.stats.seconds-start_>.16f&&!grazeShot_){
            auto e=r.create();auto pos=r.get<TransformComponent>(player).translate;pos.x+=3;pos.z+=3;
            r.emplace<TransformComponent>(e).translate=pos;r.emplace<SlimeFeather>(e).velocity={0,0,-300};grazeShot_=true;}
        if(phase_==1&&p.stats.seconds-start_>.62f&&!perfectFollowup_){shoot();perfectFollowup_=true;}
        if(phase_==1&&p.stats.seconds-start_>.7f){
            holdEnds_=!ink.dodgeLiquid&&p.action!=Action::Dodge;
            success_=ink.perfectDodges==1&&p.stats.hitsTaken==0;
            paint_=ink.deployed>500&&p.mass+ink.deployed+ink.spent>1300;
            phase_=2;
        }
        if(phase_==2&&p.stats.seconds>1.2f){input.dodge=true;phase_=3;start_=p.stats.seconds;}
        if(phase_==3&&p.stats.seconds-start_>.28f&&!normalFollowup_){shoot();normalFollowup_=true;}
        if(phase_==3&&p.stats.seconds-start_>.55f){normalProtected_=p.stats.hitsTaken==0;shoot();phase_=4;start_=p.stats.seconds;}
        if(phase_==4&&p.stats.seconds-start_>.8f){
            late_=p.stats.hitsTaken==1&&ink.perfectDodges==1;
            auto& c=r.get<CreatureBoss>(boss);auto pos=r.get<TransformComponent>(player).translate;
            c.stage=CreatureStage::Snake;c.attack=CreatureAttack::Slam;c.sweepPhase=2;c.sweepTime=.24f;c.sweepHit=false;c.sweepCenter={pos.x,pos.y,pos.z};
            p.cooldown=0;p.invincible=0;input.dodge=true;phase_=5;start_=p.stats.seconds;
        }
        float hpBefore=p.mass;bool burstBefore=ink.perfectBurst;
        Game::GameScene::Update();
        if(!burstBefore&&ink.perfectBurst)bonusHpSafe_=p.mass>=hpBefore-.001f;
        if(phase_==0)autoSlide_|=ink.swimming&&Length(p.velocity)>15;
        liquid_|=ink.dodgeLiquid;
        blur_|=Engine::Renderer::GetInstance()->GetPostProcessParams().chromaShift>.5f;
        if(ink.perfectAge>=0&&ink.perfectAge<.15f){slow_|=p.worldScale<.2f;gray_|=Engine::Renderer::GetInstance()->GetPostProcessParams().san>.9f;}
        if((phase_==5&&p.stats.seconds-start_>.25f)||p.stats.seconds>4){
            bool snake=p.stats.hitsTaken==1&&ink.perfectDodges==2;
            bool pass=success_&&paint_&&slow_&&gray_&&late_&&snake&&liquid_&&holdEnds_&&autoSlide_&&normalProtected_&&perfectFollowup_&&blur_&&bonusHpSafe_;
            std::ofstream("tests/out/ui-smoke.txt")<<(pass?"PASS":"FAIL")<<" perfect="<<success_<<" recoverable paint / conserved mass="<<paint_<<" slow="<<slow_<<" grayscale="<<gray_<<" early dodge takes hit="<<late_<<" snake perfect="<<snake<<" liquid="<<liquid_<<" held button ends="<<holdEnds_<<" auto slide="<<autoSlide_;
            std::ofstream("tests/out/ui-smoke.txt",std::ios::app)<<" normal followup protected="<<normalProtected_<<" perfect followup tested="<<perfectFollowup_<<" blur pulse="<<blur_<<" bonus HP safe="<<bonusHpSafe_<<" delayed feather graze="<<grazeShot_;
            PostQuitMessage(pass?0:2);
        }
    }
};
class BossValidationScene final : public Game::GameScene {
    Engine::WindowDX* window_=nullptr;
    unsigned frame_=0;
    bool moved_=false,volley_=false,held_=true,outside_=true,lock_=true,captured_=false;
    bool morphHeld_=true,morphCaptured_=false,returned_=false,tail_=false,dive_=false,radial_=false,diveCaptured_=false,snakeCaptured_=false,tailCaptured_=false;
    bool headImpact_=false,diveImpact_=false,headImpactCaptured_=false,diveImpactCaptured_=false;size_t peakFeathers_=0;
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override {
        window_=dx;Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Game::GameScene::Initialize(dx,params);
        GetRegistry().emplace<Game::Chrono::ControlFrame>(FindObjectByName("Player"));
    }
    void Update()override {
        using namespace Game;using namespace Game::Chrono;
        auto& r=GetRegistry();auto player=FindObjectByName("Player"),boss=FindObjectByName("Boss");
        auto& input=r.get<ControlFrame>(player);input={};
        input.lockOn=frame_==5||frame_==6||frame_==30||frame_==60;
        auto& p=r.get<Player>(player);p.invincible=1;
        auto& c=r.get<CreatureBoss>(boss);auto before=c.offset;
        bool waiting=c.stage==CreatureStage::Opening&&c.sweepPhase==1;
        bool morphing=c.stage==CreatureStage::Assemble||c.stage==CreatureStage::Descend;
        auto previousStage=c.stage;
        Game::GameScene::Update();
        auto& ink=r.get<InkPlayer>(player);
        if(frame_==6)lock_&=ink.lockedOn;
        if(frame_==31)lock_&=!ink.lockedOn;
        if(frame_==61)lock_&=ink.lockedOn;
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==0&&Length(c.offset-before)>.01f)moved_=true;
        if(waiting)held_&=Length(c.offset-before)<.01f;
        if(morphing&&c.stage==previousStage)morphHeld_&=Length(c.offset-before)<.01f;
        if(previousStage==CreatureStage::Descend&&c.stage==CreatureStage::Snake)returned_=true;
        tail_|=c.stage==CreatureStage::Snake&&c.attack==CreatureAttack::HeadTail&&c.sweepPhase==6;
        dive_|=c.stage==CreatureStage::Dive;
        radial_|=c.stage==CreatureStage::Dive&&c.sweepPhase==3&&r.view<SlimeFeather>().size()>=18;
        if(c.impactAge<.1f){
            if(c.stage==CreatureStage::Snake)headImpact_|=GetContext().camera->IsShaking();
            if(c.stage==CreatureStage::Dive)diveImpact_|=GetContext().camera->IsShaking();
        }
        peakFeathers_=std::max(peakFeathers_,r.view<SlimeFeather>().size());
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==0&&c.timer>1.f&&!snakeCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-snake-idle.png");
            snakeCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Snake&&c.sweepPhase==6&&c.sweepTime>.18f&&!tailCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-tail-sweep.png");
            tailCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Dive&&c.sweepPhase==2&&c.sweepTime>.25f&&!diveCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-dive.png");
            diveCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Snake&&c.impactAge>.08f&&c.impactAge<.25f&&!headImpactCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-head-impact.png");
            headImpactCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Dive&&c.impactAge>.08f&&c.impactAge<.25f&&!diveImpactCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-dive-impact.png");
            diveImpactCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Assemble&&c.timer>3.5f&&!morphCaptured_){
            DirectX::ScratchImage image;
            auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
            if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-reform.png");
            morphCaptured_=SUCCEEDED(hr);
        }
        if(c.stage==CreatureStage::Opening){
            auto pos=r.get<TransformComponent>(boss).translate;
            outside_&=(std::abs(pos.x)>220||pos.z< -220||pos.z>260)&&std::abs(pos.x)<290&&pos.z> -280&&pos.z<320;
            volley_|=r.get<Boss>(boss).emitted>=180;
            if(!captured_&&r.get<Boss>(boss).emitted>=108){
                DirectX::ScratchImage image;
                auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
                if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),L"tests/out/boss-volley.png");
                captured_=SUCCEEDED(hr);
            }
        }
        ++frame_;
        if((volley_&&returned_)||p.stats.seconds>65){
            bool passed=moved_&&volley_&&held_&&outside_&&lock_&&captured_&&morphHeld_&&morphCaptured_&&returned_&&tail_&&dive_&&radial_&&diveCaptured_&&snakeCaptured_&&tailCaptured_&&headImpact_&&diveImpact_&&headImpactCaptured_&&diveImpactCaptured_&&peakFeathers_>=80;
            std::ofstream("tests/out/ui-smoke.txt")<<(passed?"PASS":"FAIL")<<" boss pursuit="<<moved_<<" volley="<<volley_<<" warning hold="<<held_<<" outer flight="<<outside_<<" lock toggle="<<lock_<<" capture="<<captured_<<" morph root fixed="<<morphHeld_<<" reform capture="<<morphCaptured_<<" returned="<<returned_<<" tail="<<tail_<<" dive="<<dive_<<" radial="<<radial_<<" dive capture="<<diveCaptured_<<" snake capture="<<snakeCaptured_<<" tail capture="<<tailCaptured_<<" head shake="<<headImpact_<<" dive shake="<<diveImpact_<<" impact captures="<<(headImpactCaptured_&&diveImpactCaptured_)<<" peak feathers="<<peakFeathers_;
            PostQuitMessage(passed?0:2);
        }
    }
};

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
        if(page_>=5){scene_.reset();return;}
        if(page_==0)scene_=std::make_unique<Game::TitleScene>();
        else if(page_==1)scene_=std::make_unique<Game::SelectScene>();
        else if(page_==2)scene_=std::make_unique<Game::GameOverScene>();
        else scene_.reset();
        if(scene_)scene_->Initialize(window_,{});
        else Game::Music::Play(page_==3?Game::Music::Victory:Game::Music::Battle);
        auto* audio=Engine::Audio::GetInstance();
        const char* tracks[]={Game::Music::Title,Game::Music::Select,Game::Music::Defeat,Game::Music::Victory,Game::Music::Battle};
        passed_&=audio && audio->IsBGMPlaying() && audio->CurrentBGM()==tracks[page_];
        if(audio)audio->SetBGMDucked(page_==4);
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
        const wchar_t* paths[]={L"tests/out/ui-title.png",L"tests/out/ui-select.png",L"tests/out/ui-defeat.png",L"tests/out/ui-clear.png",L"tests/out/ui-pause.png",
            L"tests/out/credits-music.png",L"tests/out/credits-art.png",L"tests/out/credits-fonts.png",L"tests/out/credits-software.png"};
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),paths[page_]);
        passed_&=SUCCEEDED(hr);
        if(++page_==5+Game::UI::CreditsPageCount){
            auto* audio=Engine::Audio::GetInstance();
            audio->StopAll();passed_&=!audio->IsBGMPlaying() && audio->CurrentBGM().empty();
            Game::Music::Play(Game::Music::Title);passed_&=audio->IsBGMPlaying();
            std::ofstream("tests/out/ui-smoke.txt")<<(passed_?"PASS":"FAIL")<<" font, title fit, nine GPU captures including four credits pages, five BGM tracks, transitions and stop/restart";
            PostQuitMessage(passed_?0:2);return;
        }
        frame_=0;Load();
    }
    void Draw()override {
        if(scene_){scene_->Draw();return;}
        Game::UI::Canvas ui(Engine::Renderer::GetInstance());
        if(page_>=5){Game::UI::DrawCredits(ui,page_-5);return;}
        if(page_==3)ui.Result(true,"TIME  02:34.56","CORES  3 / 3     |     HITS TAKEN  12");
        else ui.Pause();
    }
};
