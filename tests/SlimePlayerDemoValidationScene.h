#pragma once
#include "../Game/Scenes/SlimePlayerDemoScene.h"
#include "../Game/Systems/ChronoSystem.h"
#include "../Engine/Time/TimeManager.h"
#include "../externals/DirectXTex/DirectXTex.h"
#include <filesystem>
#include <fstream>

class SlimePlayerDemoValidationScene final : public Game::SlimePlayerDemoScene {
    Engine::WindowDX* window_=nullptr;
    int frame_=0,form_=1;float age_=0,readyAge_=0;
    bool farWaterOnly_=false,nearWaterOnly_=false;float farAge_=0,maxWaterTravel_=0;
    bool cameraOnly_=false;std::string meshName_="Chrono Demo practice floor";
    bool camera_=true,resources_=true,movement_=false,jump_=false,reset_=false,split_=false;
    std::array<bool,2> forms_{},attacks_{}; // 1: タイヤ、2: 消火栓
    void Capture(const std::wstring& path){
        DirectX::ScratchImage image;
        if(SUCCEEDED(DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,
            D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT)))
            DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str());
    }
    void CheckStoppedCamera(){
        using namespace Game;
        auto p=FindObjectByName("Player");const auto& t=GetComponents().get<TransformComponent>(p);
        auto view=DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(t.translate.x,t.translate.y+1,t.translate.z,1),GetCamera().View()*GetCamera().Proj());
        DirectX::XMFLOAT3 clip;DirectX::XMStoreFloat3(&clip,view);
        camera_ &= std::abs(clip.x)<.3f&&std::abs(clip.y)<.5f&&clip.z>0&&clip.z<1;
        const auto at=GetCamera().Position();
        camera_ &= std::abs(at.z-t.translate.z)<35;
        const auto editor=GetEditorCamera().Position();camera_ &= editor.x==90&&editor.y==80&&editor.z==70;
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters& params) override {
        window_=dx;farWaterOnly_=wcsstr(GetCommandLineW(),L"--far-water-demo-smoke")!=nullptr;cameraOnly_=wcsstr(GetCommandLineW(),L"--stopped-camera-check")!=nullptr;
        nearWaterOnly_=wcsstr(GetCommandLineW(),L"--near-water-check")!=nullptr;
        if(cameraOnly_){auto main=params;main.stagePath=Game::Data::MainStagePath();Game::GameScene::Initialize(dx,main);
            for(auto e:GetComponents().view<Game::NameComponent,Game::MeshRendererComponent>()){
                const auto& name=GetComponents().get<Game::NameComponent>(e).name;
                if(name.rfind("Chrono City surface ",0)==0){meshName_=name;break;}
            }
        }else Game::SlimePlayerDemoScene::Initialize(dx,params);
        std::filesystem::create_directories("tests/out");
    }
    void Update() override {
        using namespace Game;using namespace Game::Chrono;
        ++frame_;
        if(farWaterOnly_){
            auto player=FindObjectByName("Player");auto& input=GetComponents().get_or_emplace<ControlFrame>(player);input={};input.pitch=nearWaterOnly_?.65f:.25f;input.morphForm=5;
            float dt=std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(),0.f,.05f);farAge_+=dt;
            input.attack=(farAge_>.1f&&farAge_<.3f)||(farAge_>1.5f&&farAge_<7.5f);
            GameScene::Update();
            for(const auto& sample:GetRenderer()->GetHydrantWaterSnapshot().particles)
                maxWaterTravel_=std::max(maxWaterTravel_,std::sqrt(sample.at.x*sample.at.x+sample.at.z*sample.at.z));
            if(farAge_>6&&farAge_-dt<=6){
                const auto core=GetRenderer()->GetFluidBodySnapshot().core;
                std::ofstream("tests/out/far-water-core.txt")<<core.x<<','<<core.y<<','<<core.z;
                std::ofstream data("tests/out/far-water-particles.csv");data<<"x,y,z,vx,vy,vz,contactAge\n";
                for(const auto& sample:GetRenderer()->GetHydrantWaterSnapshot().particles)
                    data<<sample.at.x<<','<<sample.at.y<<','<<sample.at.z<<','<<sample.velocity.x<<','<<sample.velocity.y<<','<<sample.velocity.z<<','<<sample.contactAge<<'\n';
            }
            if(farAge_>6&&farAge_-dt<=6)Capture(nearWaterOnly_?L"tests/out/near-water-stream.png":L"tests/out/far-water-stream.png");
            if(farAge_>9){Capture(nearWaterOnly_?L"tests/out/near-water-puddle.png":L"tests/out/far-water-puddle.png");
                std::ofstream("tests/out/far-water-demo-smoke.txt")<<(maxWaterTravel_>10?"PASS":"FAIL")<<" far water live rendering, max water travel="<<maxWaterTravel_;
                PostQuitMessage(maxWaterTravel_>10?0:2);
            }
            return;
        }
        if(frame_==2){
            auto floor=FindObjectByName(meshName_);
            if(floor==entt::null||!GetComponents().all_of<MeshRendererComponent>(floor)){
                std::ofstream("tests/out/stopped-camera-smoke.txt")<<"FAIL expected field mesh missing: "<<meshName_;
                PostQuitMessage(2);return;
            }
            const auto model=GetComponents().get<MeshRendererComponent>(floor).modelHandle;
            GetEditorCamera().SetPosition(90,80,70);
            SetIsPlaying(false);GameScene::Update();CheckStoppedCamera();
            floor=FindObjectByName(meshName_);
            resources_ &= GetComponents().get<MeshRendererComponent>(floor).modelHandle==model;
            return;
        }
        if(frame_>=3&&frame_<=6){
            if(frame_==3)GetComponents().get<TransformComponent>(FindObjectByName("Player")).translate.x=12;
            GetEditorCamera().SetPosition(90,80,70);
            GameScene::Update();CheckStoppedCamera();
            if(frame_==6){
                Capture(cameraOnly_?L"tests/out/city-stopped-camera.png":L"tests/out/slime-demo-stopped.png");
                if(cameraOnly_){
                    SetIsPlaying(true);SetIsPlaying(false);GameScene::Update();CheckStoppedCamera();
                    camera_ &= GetComponents().get<TransformComponent>(FindObjectByName("Player")).translate.x==12;
                    std::ofstream("tests/out/stopped-camera-smoke.txt")<<((camera_&&resources_)?"PASS":"FAIL")<<" city camera="<<camera_<<" procedural_meshes="<<resources_<<" automatic and repeated Stop/Play checked";
                    PostQuitMessage(camera_&&resources_?0:2);
                }
            }
            return;
        }
        if(frame_==7){SetIsPlaying(true);}
        if(frame_<7){GameScene::Update();return;}
        auto player=FindObjectByName("Player");auto& input=GetComponents().get_or_emplace<ControlFrame>(player);input={};input.pitch=.23f;
        float dt=std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(),0.f,.05f);age_+=dt;
        if(form_==0){
            // Reset reconstructs the field and all unlocked forms, without leaving the scene.
            const auto& state=GetChronoSystem()->LifeState();reset_ = state.bodies[0].mass==600&&state.ChildCount()==0&&state.bodies[0].tireUnlocked&&state.bodies[0].hydrantUnlocked;
            bool pass=camera_&&resources_&&movement_&&jump_&&reset_&&split_;
            for(int i=0;i<2;++i)pass &= forms_[i]&&attacks_[i];
            std::ofstream report("tests/out/slime-player-demo-smoke.txt");
            report<<(pass?"PASS":"FAIL")<<" camera="<<camera_<<" runtime_meshes="<<resources_<<" movement="<<movement_<<" jump="<<jump_<<" reset="<<reset_<<" split="<<split_;
            for(int i=0;i<2;++i)report<<" form"<<i+1<<"="<<forms_[i]<<" attack"<<i+1<<"="<<attacks_[i];
            PostQuitMessage(pass?0:2);return;
        }
        input.morphForm=form_;input.attack=(age_>.1f&&age_<.25f)||(readyAge_>.2f&&readyAge_<.65f);
        input.returnToSlime=readyAge_>1.8f;
        input.move=(age_>.4f&&age_<1.7f)?Vec{1,0,0}:Vec{};
        input.jump=age_>.5f&&age_<.65f;
        GameScene::Update();
        const auto& life=GetChronoSystem()->LifeState();const auto& body=life.bodies[0];
        if(body.weapon.ready||readyAge_>0)readyAge_+=dt;
        forms_[size_t(form_-1)] |= body.action==(form_==1?LifeAction::Tire:LifeAction::Hydrant)&&body.weapon.ready;
        // タイヤは押していた分の溜めで突進し、消火栓は放水する
        attacks_[size_t(form_-1)] |= (form_==1&&body.weapon.dash>0)||(form_==2&&life.waterAttack);
        movement_ |= std::abs(body.at.x)>2;jump_ |= body.at.y>2;
        if(readyAge_>1.f&&readyAge_-dt<=1.f)Capture(L"tests/out/slime-demo-form-"+std::to_wstring(form_)+L".png");
        if(readyAge_>2.4f||age_>12.f){age_=readyAge_=0;if(++form_==3){
            input={};input.demoHit=true;GameScene::Update();split_=GetChronoSystem()->LifeState().ChildCount()>0;
            input={};input.restart=true;GameScene::Update();form_=0;
        }}
    }
};
