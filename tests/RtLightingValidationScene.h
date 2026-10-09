#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/UI/GraphicsUI.h"
#include <filesystem>
#include <fstream>
#include <chrono>

// Production battlefield, fixed gameplay/camera, independently toggled RT effects.
class RtLightingValidationScene final : public Game::GameScene {
    Engine::WindowDX* window_=nullptr;int frame_=0;bool passed_=true;
    Engine::Renderer::GraphicsSettings original_{};
    uint32_t reflectionPixels_=0,indirectPixels_=0,reflectionHits_=0,indirectHits_=0;
    uint32_t shadowFrames_=0;
    double floorMotionDelta_=0;std::vector<unsigned char> previousPixels_;
    std::chrono::steady_clock::time_point started_{};bool warmed_=false;
    entt::entity moved_=entt::null;uint32_t stationaryBuilds_=0,orbitBuilds_=0;bool initialPassed_=false,orbitPassed_=false,movePassed_=false;
    void Capture(const wchar_t* path){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        if(SUCCEEDED(hr))hr=DirectX::SaveToWICFile(*image.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path);passed_&=SUCCEEDED(hr);
    }
    void MovingCapture(int frame){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);passed_&=SUCCEEDED(hr);if(FAILED(hr))return;
        const auto* pixels=image.GetImage(0,0,0);if(!pixels)return;
        // Gross temporal flashing on the floor is measured independently of UI
        // and fluid. Camera travel legitimately changes grass texture details.
        if(!previousPixels_.empty()){double delta=0;size_t count=0;
            for(size_t y=600;y<750;++y)for(size_t x=1250;x<1600;++x)for(size_t c=0;c<3;++c){delta+=std::abs(int(pixels->pixels[y*pixels->rowPitch+x*4+c])-int(previousPixels_[y*pixels->rowPitch+x*4+c]));++count;}
            floorMotionDelta_=std::max(floorMotionDelta_,delta/count/255.0);}
        previousPixels_.assign(pixels->pixels,pixels->pixels+pixels->slicePitch);
        std::wstring path=L"RtLightingCheck/moving-"+std::to_wstring(frame)+L".png";
        passed_&=SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str()));
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("RtLightingCheck");Engine::SceneParameters p;p.stagePath="Resources/Scenes/chrono.json";Game::GameScene::Initialize(dx,p);
        started_=std::chrono::steady_clock::now();
        auto* r=Engine::Renderer::GetInstance();original_=r->GetGraphicsSettings();auto& s=r->GetGraphicsSettings();s.rtReflections=s.rtIndirect=false;s.rtShadows=true;s.dof=s.motionBlur=0;
        passed_&=r->RtLightingAvailable();
        auto* rock=r->GetModel(r->LoadObjMesh("Resources/Models/PolyHaven/boulder_01_2k.fbx"));
        auto* grass=r->GetModel(r->LoadObjMesh("Resources/Models/PolyHaven/grass_medium_02_2k.fbx"));
        passed_&=rock&&rock->GetIndexCount()==66122*3; // Exactly LOD0, no overlap.
        passed_&=grass&&grass->GetData().max.y-grass->GetData().min.y>.15f;
        auto grassLods=r->GetDistanceLodMeshes(r->LoadObjMesh("Resources/Models/PolyHaven/grass_medium_02_2k.fbx"));
        passed_&=r->GetModel(grassLods[1])->GetIndexCount()<grass->GetIndexCount()*.7f;
        initialPassed_=passed_;
    }
    void Update()override{
        if(!warmed_){Game::GameScene::Update();auto params=Engine::Renderer::GetInstance()->GetPostProcessParams();
            // Expensive initial BLAS construction can outlast a wall-time wait.
            // Freeze only after gameplay actually resumes, not mid-introduction.
            warmed_=std::chrono::duration<float>(std::chrono::steady_clock::now()-started_).count()>5&&!params.cinematicCamera;
            if(warmed_)passed_&=params.dofStrength==0;return;}
        ++frame_;auto* r=Engine::Renderer::GetInstance();auto& s=r->GetGraphicsSettings();
        if(frame_==45){Capture(L"RtLightingCheck/off.png");s.rtReflections=true;}
        if(frame_==80){Capture(L"RtLightingCheck/reflections.png");reflectionPixels_=r->RtReflectionPixels();reflectionHits_=r->RtReflectionHits();passed_&=reflectionPixels_>0&&reflectionHits_>0&&r->RtIndirectPixels()==0;s.rtReflections=false;s.rtIndirect=true;}
        if(frame_==115){Capture(L"RtLightingCheck/indirect.png");indirectPixels_=r->RtIndirectPixels();indirectHits_=r->RtIndirectHits();passed_&=indirectPixels_>0&&indirectHits_>0&&r->RtReflectionPixels()==0;s.rtReflections=true;}
        if(frame_==150){Capture(L"RtLightingCheck/combined.png");shadowFrames_=r->RtShadowFrames();s.rtShadows=false;}
        if(frame_==185){Capture(L"RtLightingCheck/oblique.png");passed_&=r->RtShadowFrames()==shadowFrames_&&r->RtReflectionHits()>0&&r->RtIndirectHits()>0;s.rtShadows=true;}
#ifdef USE_IMGUI
        if(frame_==190){
            using namespace Game::UI;Canvas ui(r);auto& io=ImGui::GetIO();bool down=io.MouseDown[0];float duration=io.MouseDownDuration[0];io.MouseDown[0]=true;io.MouseDownDuration[0]=0;
            auto click=[&](Game::UI::Rect b){ui.SetPointer((b.x+b.w*.5f)*1.5f,(b.y+b.h*.5f)*1.5f);UpdateGraphics(ui,*r);};
            click(RtReflectionOff);passed_&=!s.rtReflections&&s.rtIndirect;click(RtReflectionOn);passed_&=s.rtReflections&&s.rtIndirect;
            click(RtIndirectOff);passed_&=s.rtReflections&&!s.rtIndirect;click(RtIndirectOn);passed_&=s.rtReflections&&s.rtIndirect;
            bool quality=s.dlssQuality;click({546,617,240,54});passed_&=s.rtReflections&&s.rtIndirect&&s.rtShadows&&s.dlssQuality==quality;
            io.MouseDown[0]=down;io.MouseDownDuration[0]=duration;
        }
#endif
        if(frame_==210){Capture(L"RtLightingCheck/settings.png");s.motionBlur=.3f;s.dof=1;}
        if(frame_>=220&&frame_<=255&&frame_%5==0)MovingCapture(frame_);
        if(frame_==215)orbitBuilds_=r->RtTlasBuilds();
        if(frame_==260){stationaryBuilds_=r->RtTlasBuilds();orbitPassed_=stationaryBuilds_==orbitBuilds_;passed_&=orbitPassed_;
            moved_=FindObjectByName("Chrono Foreground weathered stone");passed_&=GetRegistry().valid(moved_);
            if(GetRegistry().valid(moved_))GetRegistry().get<Game::TransformComponent>(moved_).translate.x+=1;
            // Production Update invalidates cached world transforms. Most of
            // this fixture is frozen; resume one update for the explicit move.
            Game::GameScene::Update();}
        if(frame_==270){movePassed_=r->RtTlasBuilds()>stationaryBuilds_;passed_&=movePassed_;
            if(GetRegistry().valid(moved_))GetRegistry().get<Game::TransformComponent>(moved_).translate.x-=1;
            Game::GameScene::Update();}
        if(frame_==295)Capture(L"RtLightingCheck/scenery.png");
        if(frame_==300){passed_&=r->RtLightingFrames()>150&&floorMotionDelta_<.18;
            std::ofstream("RtLightingCheck/result.txt")<<(passed_?"PASS":"FAIL")<<" RT lighting: frames="<<r->RtLightingFrames()<<" reflection pixels="<<reflectionPixels_<<" reflection geometry hits="<<reflectionHits_<<" indirect pixels="<<indirectPixels_<<" indirect geometry hits="<<indirectHits_<<" DLSS frames="<<r->DlssEvaluatedFrames()<<" initial="<<initialPassed_<<" orbit cache="<<orbitPassed_<<" orbit builds="<<orbitBuilds_<<" stationary builds="<<stationaryBuilds_<<" moved entity="<<int(moved_!=entt::null)<<" movement invalidated="<<movePassed_;
            std::ofstream("RtLightingCheck/motion-result.txt")<<"moving camera floor delta="<<floorMotionDelta_<<" (limit .18), models LOD0/upright="<<passed_;
            s=original_;PostQuitMessage(passed_?0:2);
        }
    }
    void Draw()override{
        if(frame_>=190&&frame_<=210){Game::UI::Canvas ui(Engine::Renderer::GetInstance());Game::UI::DrawGraphics(ui,*Engine::Renderer::GetInstance());return;}
        auto player=FindObjectByName("Player");auto p=GetRegistry().get<Game::TransformComponent>(player).translate;
        auto& camera=GetCamera();camera.StopShake();camera.SetHandheld(0);camera.SetPosition(p.x,p.y+6,p.z-16);camera.LookAt(p.x,p.y+1,p.z+12,0,1,0);
        if(frame_>=150){camera.SetPosition(p.x+14,p.y+3,p.z-14);camera.LookAt(p.x,p.y+1,p.z,0,1,0);}
        if(frame_>210){float angle=(frame_-211)*.008f;camera.SetPosition(p.x+20*std::sin(.78f+angle),p.y+5,p.z-20*std::cos(.78f+angle));camera.LookAt(p.x,p.y-.4f,p.z+4,0,1,0);}
        if(frame_>=270){camera.SetPosition(15,2,-10);camera.LookAt(8,.45f,-5,0,1,0);}
        Game::GameScene::Draw();
    }
    void DrawEditor()override{}
};
