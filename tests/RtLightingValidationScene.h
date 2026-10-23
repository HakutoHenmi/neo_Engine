#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Game/UI/GraphicsUI.h"
#include "../Game/Editor/EditorUI.h"
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
    double stationaryDelta_=0,whiteFraction_=0;std::vector<unsigned char> stationaryPixels_;
    uint32_t menuDlssFrames_=0,resetsBeforeResume_=0;bool resumePassed_=true;
    bool angleMode_=false,editorMode_=false;double angleMaxDelta_=0;int angleCases_=0;
    bool travelMode_=false;double travelDelta_=0,travelMeanDelta_=0,travelMaximum_=0;int travelCases_=0;
    uint32_t priorDescriptor_=UINT32_MAX,priorSlot_=UINT32_MAX;bool descriptorIsolation_=true;
    void TravelCapture(int caseIndex){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        passed_&=SUCCEEDED(hr);if(FAILED(hr))return;auto* pixels=image.GetImage(0,0,0);if(!pixels)return;
        if(!previousPixels_.empty()){double delta=0,mean=0;size_t count=0;
            auto region=Region(180,360,640,760);
            for(size_t y=region[1];y<region[3];++y)for(size_t x=region[0];x<region[2];++x)for(size_t c=0;c<3;++c){auto at=y*pixels->rowPitch+x*4+c;
                int difference=int(pixels->pixels[at])-int(previousPixels_[at]);delta+=std::abs(difference);mean+=difference;++count;}
            travelDelta_=std::max(travelDelta_,delta/count/255);double brightness=std::abs(mean/count/255);
            if(brightness>travelMeanDelta_){travelMeanDelta_=brightness;auto path=L"RtLightingCheck/travel-"+std::to_wstring(caseIndex)+L".png";
                passed_&=SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str()));}}
        previousPixels_.assign(pixels->pixels,pixels->pixels+pixels->slicePitch);
    }
    std::array<size_t,4> Region(size_t x0,size_t y0,size_t x1,size_t y1){
#ifdef USE_IMGUI
        if(editorMode_){auto minimum=Game::EditorUI::GetGameImageMin(),maximum=Game::EditorUI::GetGameImageMax(),origin=ImGui::GetMainViewport()->Pos;
            float width=maximum.x-minimum.x,height=maximum.y-minimum.y;
            if(width<1||height<1){passed_=false;return {0,0,1,1};}
            auto x=[&](size_t v){return size_t(std::clamp(minimum.x-origin.x+float(v)*width/1920,0.f,1919.f));};
            auto y=[&](size_t v){return size_t(std::clamp(minimum.y-origin.y+float(v)*height/1080,0.f,1079.f));};
            return {x(x0),y(y0),std::max(x(x0)+1,x(x1)),std::max(y(y0)+1,y(y1))};}
#endif
        return {x0,y0,x1,y1};
    }
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
            auto region=Region(1250,600,1600,750);
            for(size_t y=region[1];y<region[3];++y)for(size_t x=region[0];x<region[2];++x)for(size_t c=0;c<3;++c){delta+=std::abs(int(pixels->pixels[y*pixels->rowPitch+x*4+c])-int(previousPixels_[y*pixels->rowPitch+x*4+c]));++count;}
            floorMotionDelta_=std::max(floorMotionDelta_,delta/count/255.0);}
        previousPixels_.assign(pixels->pixels,pixels->pixels+pixels->slicePitch);
        std::wstring path=L"RtLightingCheck/moving-"+std::to_wstring(frame)+L".png";
        passed_&=SUCCEEDED(DirectX::SaveToWICFile(*pixels,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),path.c_str()));
    }
    void StabilityCapture(bool stationary){DirectX::ScratchImage image;
        auto hr=DirectX::CaptureTexture(window_->Queue(),window_->GetCurrentBackBufferResource(),false,image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);
        passed_&=SUCCEEDED(hr);if(FAILED(hr))return;const auto* pixels=image.GetImage(0,0,0);if(!pixels){passed_=false;return;}
        size_t white=0,count=0;
        auto region=Region(20,120,1900,960);
        for(size_t y=region[1];y<region[3];++y)for(size_t x=region[0];x<region[2];++x){auto* p=pixels->pixels+y*pixels->rowPitch+x*4;
            white+=p[0]>249&&p[1]>249&&p[2]>249;++count;}
        whiteFraction_=std::max(whiteFraction_,double(white)/count);
        if(stationary){if(!stationaryPixels_.empty()){double delta=0;size_t samples=0;
            auto floor=Region(1250,600,1600,750);
            for(size_t y=floor[1];y<floor[3];++y)for(size_t x=floor[0];x<floor[2];++x)for(size_t c=0;c<3;++c){
                auto at=y*pixels->rowPitch+x*4+c;delta+=std::abs(int(pixels->pixels[at])-int(stationaryPixels_[at]));++samples;}
            stationaryDelta_=std::max(stationaryDelta_,delta/samples/255.0);}
            stationaryPixels_.assign(pixels->pixels,pixels->pixels+pixels->slicePitch);}
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        window_=dx;std::filesystem::create_directories("RtLightingCheck");Engine::SceneParameters p;p.stagePath=Game::Data::MainStagePath();Game::GameScene::Initialize(dx,p);
        angleMode_=wcsstr(GetCommandLineW(),L"--rt-angle-smoke")!=nullptr;
        editorMode_=wcsstr(GetCommandLineW(),L"--rt-editor-smoke")!=nullptr;
        travelMode_=wcsstr(GetCommandLineW(),L"--rt-travel-smoke")!=nullptr;
        if(travelMode_)std::ofstream("RtLightingCheck/travel-result.txt")<<"Travel tests: four yaw directions, two pitches, five effect masks\n";
        if(angleMode_)std::ofstream("RtLightingCheck/angle-result.txt")<<"Angle tests: four yaw directions, three pitches, shadow/lighting/combined, editor="<<editorMode_<<"\n";
        started_=std::chrono::steady_clock::now();
        auto* r=Engine::Renderer::GetInstance();original_=r->GetGraphicsSettings();auto s=r->GetGraphicsSettings();s.rtReflections=s.rtIndirect=false;s.rtShadows=true;s.dof=s.motionBlur=0;r->ApplyGraphicsSettings(s);
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
        ++frame_;auto* r=Engine::Renderer::GetInstance();auto s=r->GetGraphicsSettings();
        if(travelMode_){auto range=r->DynamicSrvFrameRange();auto descriptor=range[0];auto slot=window_->FrameIndex();
            descriptorIsolation_&=range[1]>range[0]&&(slot==priorSlot_||descriptor!=priorDescriptor_);
            priorDescriptor_=descriptor;priorSlot_=slot;}
        if(travelMode_&&frame_==301){r->ClearGPUFluid();r->SetGPUFluidCore({0,0,0},0);} // Exclude old fluid depth too.
        if(frame_==45){Capture(L"RtLightingCheck/off.png");s.rtReflections=true;r->ApplyGraphicsSettings(s);}
        if(frame_==80){Capture(L"RtLightingCheck/reflections.png");reflectionPixels_=r->RtReflectionPixels();reflectionHits_=r->RtReflectionHits();passed_&=reflectionPixels_>0&&reflectionHits_>0&&r->RtIndirectPixels()==0;s.rtReflections=false;s.rtIndirect=true;r->ApplyGraphicsSettings(s);}
        if(frame_==115){Capture(L"RtLightingCheck/indirect.png");indirectPixels_=r->RtIndirectPixels();indirectHits_=r->RtIndirectHits();passed_&=indirectPixels_>0&&indirectHits_>0&&r->RtReflectionPixels()==0;s.rtReflections=true;r->ApplyGraphicsSettings(s);}
        if(frame_>=120&&frame_<=135)StabilityCapture(true);
        if(frame_==150){Capture(L"RtLightingCheck/combined.png");shadowFrames_=r->RtShadowFrames();s.rtShadows=false;r->ApplyGraphicsSettings(s);}
        if(frame_==185){Capture(L"RtLightingCheck/oblique.png");passed_&=r->RtShadowFrames()==shadowFrames_&&r->RtReflectionHits()>0&&r->RtIndirectHits()>0;s.rtShadows=true;r->ApplyGraphicsSettings(s);}
#ifdef USE_IMGUI
        if(frame_==190){
            using namespace Game::UI;Canvas ui(r);auto& io=ImGui::GetIO();bool down=io.MouseDown[0];float duration=io.MouseDownDuration[0];io.MouseDown[0]=true;io.MouseDownDuration[0]=0;
            auto click=[&](Game::UI::Rect b){ui.SetPointer((b.x+b.w*.5f)*1.5f,(b.y+b.h*.5f)*1.5f);UpdateGraphics(ui,*r);s=r->GetGraphicsSettings();};
            click(RtReflectionOff);passed_&=!s.rtReflections&&s.rtIndirect;click(RtReflectionOn);passed_&=s.rtReflections&&s.rtIndirect;
            click(RtIndirectOff);passed_&=s.rtReflections&&!s.rtIndirect;click(RtIndirectOn);passed_&=s.rtReflections&&s.rtIndirect;
            bool quality=s.dlssQuality;click({546,617,240,54});passed_&=s.rtReflections&&s.rtIndirect&&s.rtShadows&&s.dlssQuality==quality;
            io.MouseDown[0]=down;io.MouseDownDuration[0]=duration;
        }
#endif
        if(frame_==190)menuDlssFrames_=r->DlssEvaluatedFrames();
        if(frame_==210){Capture(L"RtLightingCheck/settings.png");s.motionBlur=.3f;s.dof=1;r->ApplyGraphicsSettings(s);}
        if(frame_==211){resetsBeforeResume_=r->DlssHistoryResets();resumePassed_&=r->DlssEvaluatedFrames()==menuDlssFrames_;}
        if(frame_==212){if(r->DlssActive())resumePassed_&=r->DlssHistoryResets()>resetsBeforeResume_;passed_&=resumePassed_;}
        if(frame_>=212&&frame_<=220)StabilityCapture(false);
        if(frame_>=220&&frame_<=255&&frame_%5==0)MovingCapture(frame_);
        if(frame_==215)orbitBuilds_=r->RtTlasBuilds();
        if(frame_==260){stationaryBuilds_=r->RtTlasBuilds();orbitPassed_=stationaryBuilds_==orbitBuilds_;passed_&=orbitPassed_;
            moved_=FindObjectByName("Chrono Foreground weathered stone");passed_&=GetComponents().valid(moved_);
            if(GetComponents().valid(moved_))GetComponents().get<Game::TransformComponent>(moved_).translate.x+=1;
            // Production Update invalidates cached world transforms. Most of
            // this fixture is frozen; resume one update for the explicit move.
            Game::GameScene::Update();}
        if(frame_==270){movePassed_=r->RtTlasBuilds()>stationaryBuilds_;passed_&=movePassed_;
            if(GetComponents().valid(moved_))GetComponents().get<Game::TransformComponent>(moved_).translate.x-=1;
            Game::GameScene::Update();}
        if(frame_==295)Capture(L"RtLightingCheck/scenery.png");
        if(angleMode_&&frame_>=301&&frame_<=1740){int sample=(frame_-301)%40,caseIndex=(frame_-301)/40;
            if(sample==0){s.ambientOcclusion=s.motionBlur=s.dof=0;s.rtShadows=caseIndex%3!=1;s.rtReflections=s.rtIndirect=caseIndex%3!=0;
                r->ApplyGraphicsSettings(s);stationaryPixels_.clear();stationaryDelta_=0;}
            if(sample>=8&&sample%4==0)StabilityCapture(true);
            if(sample==39){auto path=L"RtLightingCheck/angle-"+std::to_wstring(caseIndex)+L".png";Capture(path.c_str());
                angleMaxDelta_=std::max(angleMaxDelta_,stationaryDelta_);++angleCases_;
                std::ofstream("RtLightingCheck/angle-result.txt",std::ios::app)<<"case="<<caseIndex<<" view="<<caseIndex/3<<" effects="<<caseIndex%3<<" stationary_delta="<<stationaryDelta_<<'\n';}}
        if(travelMode_&&frame_>=301&&frame_<=2700){int sample=(frame_-301)%60,caseIndex=(frame_-301)/60;
            if(sample==0){s.ambientOcclusion=s.motionBlur=s.dof=0;s.rtShadows=caseIndex%5==1||caseIndex%5==4;s.rtReflections=caseIndex%5==2||caseIndex%5==4;s.rtIndirect=caseIndex%5==3||caseIndex%5==4;
                r->ApplyGraphicsSettings(s);previousPixels_.clear();travelDelta_=travelMeanDelta_=0;}
            if(sample>=8&&sample%6==0)TravelCapture(caseIndex); // Leave frames in flight between readbacks.
            if(sample==59){travelMaximum_=std::max(travelMaximum_,travelMeanDelta_);++travelCases_;
                std::ofstream("RtLightingCheck/travel-result.txt",std::ios::app)<<"case="<<caseIndex<<" view="<<caseIndex/5<<" effects="<<caseIndex%5<<" delta="<<travelDelta_<<" brightness_delta="<<travelMeanDelta_<<'\n';}}
        if(frame_==(travelMode_?2700:(angleMode_?1740:300))){passed_&=r->RtLightingFrames()>150&&floorMotionDelta_<.18&&stationaryDelta_<.08&&whiteFraction_<.35;
            if(travelMode_){passed_&=travelCases_==40&&travelMaximum_<.045&&descriptorIsolation_;std::ofstream("RtLightingCheck/travel-result.txt",std::ios::app)<<(passed_?"PASS":"FAIL")<<" cases="<<travelCases_<<" max_brightness_delta="<<travelMaximum_<<" descriptors_isolated="<<descriptorIsolation_;}
            if(angleMode_){passed_&=angleCases_==36&&angleMaxDelta_<.08;std::ofstream("RtLightingCheck/angle-result.txt",std::ios::app)<<(passed_?"PASS":"FAIL")<<" cases="<<angleCases_<<" max_stationary_delta="<<angleMaxDelta_;}
            std::ofstream("RtLightingCheck/result.txt")<<(passed_?"PASS":"FAIL")<<" RT lighting: frames="<<r->RtLightingFrames()<<" reflection pixels="<<reflectionPixels_<<" reflection geometry hits="<<reflectionHits_<<" indirect pixels="<<indirectPixels_<<" indirect geometry hits="<<indirectHits_<<" DLSS frames="<<r->DlssEvaluatedFrames()<<" initial="<<initialPassed_<<" orbit cache="<<orbitPassed_<<" orbit builds="<<orbitBuilds_<<" stationary builds="<<stationaryBuilds_<<" moved entity="<<int(moved_!=entt::null)<<" movement invalidated="<<movePassed_;
            std::ofstream("RtLightingCheck/motion-result.txt")<<"moving camera floor delta="<<floorMotionDelta_<<" (limit .18), stationary delta="<<stationaryDelta_<<" (limit .08), white fraction="<<whiteFraction_<<" (limit .35), menu/resume="<<resumePassed_<<", DLSS resets="<<r->DlssHistoryResets()<<", models LOD0/upright="<<passed_;
            s=original_;PostQuitMessage(passed_?0:2);
        }
    }
    void Draw()override{
        if(frame_>=190&&frame_<=210){Game::UI::Canvas ui(Engine::Renderer::GetInstance());Game::UI::DrawGraphics(ui,*Engine::Renderer::GetInstance());return;}
        auto player=FindObjectByName("Player");auto p=GetComponents().get<Game::TransformComponent>(player).translate;
        auto& camera=GetCamera();camera.StopShake();camera.SetHandheld(0);camera.SetPosition(p.x,p.y+6,p.z-16);camera.LookAt(p.x,p.y+1,p.z+12,0,1,0);
        if(frame_>=150){camera.SetPosition(p.x+14,p.y+3,p.z-14);camera.LookAt(p.x,p.y+1,p.z,0,1,0);}
        if(frame_>210){float angle=(frame_-211)*.008f;camera.SetPosition(p.x+20*std::sin(.78f+angle),p.y+5,p.z-20*std::cos(.78f+angle));camera.LookAt(p.x,p.y-.4f,p.z+4,0,1,0);}
        if(frame_>=270){camera.SetPosition(15,2,-10);camera.LookAt(8,.45f,-5,0,1,0);}
        if(angleMode_&&frame_>=301){int view=(frame_-301)/120;float yaw=(view%4)*1.5707963f;
            float dx=std::sin(yaw),dz=std::cos(yaw),height=view<4?1.2f:3.f;
            camera.SetPosition(p.x-dx*25,p.y+height,p.z-dz*25);camera.LookAt(p.x+dx*80,p.y-.1f,p.z+dz*80,0,1,0);
            if(view>=8){camera.SetPosition(p.x-dx*15,p.y+25,p.z-dz*15);camera.LookAt(p.x,p.y-.1f,p.z,0,1,0);}}
        if(travelMode_&&frame_>=301){int view=(frame_-301)/300;float yaw=(view%4)*1.5707963f;float distance=-30+((frame_-301)%60)*.5f;
            float dx=std::sin(yaw),dz=std::cos(yaw),height=view<4?2.f:6.f;
            camera.SetPosition(p.x+dx*distance,p.y+height,p.z+dz*distance);
            camera.LookAt(p.x+dx*(distance+60),p.y+(view<4?height-.3f:-5.f),p.z+dz*(distance+60),0,1,0);}
        Game::GameScene::Draw();
        // Draw updates the core mode too. Disable fluid after it has submitted
        // geometry, so the later RT/post passes cannot read a stale fluid mask.
        if(travelMode_&&frame_>=301){auto* r=Engine::Renderer::GetInstance();r->ClearGPUFluid();r->SetGPUFluidCore({0,0,0},0);}
    }
    void DrawEditor()override{if(editorMode_)Game::GameScene::DrawEditor();}
};
