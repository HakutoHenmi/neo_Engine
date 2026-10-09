#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Engine/NetworkProfiler.h"
#include <chrono>
#include <fstream>
#include <filesystem>

// Opt-in production-scene benchmark. No screenshots/readback diagnostics in
// measured frames, no settings saved, identical camera and frozen battlefield.
class SceneryBenchmarkScene final : public Game::GameScene {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start_{},previous_{};
    int frame_=0,phase_=0;bool warmed_=false;
    Engine::Renderer::GraphicsSettings original_{};
    std::array<double,Engine::Renderer::kFluidProfileStageCount> sum_{};
    double frameMs_=0,cpuRender_=0,cpuLogic_=0;int samples_=0;
    std::ofstream report_;
    std::vector<entt::entity> scenery_;
    void Configure(){auto* r=Engine::Renderer::GetInstance();auto s=r->GetGraphicsSettings();
        s.rtShadows=phase_>=2;s.rtReflections=s.rtIndirect=phase_>=3;r->ApplyGraphicsSettings(s);
        for(auto e:scenery_)GetComponents().get<Game::MeshRendererComponent>(e).enabled=phase_!=1;
    }
public:
    void Initialize(Engine::WindowDX* dx,const Engine::SceneParameters&)override{
        start_=Clock::now();Engine::SceneParameters p;p.stagePath="Resources/Scenes/chrono.json";Game::GameScene::Initialize(dx,p);
        auto* r=Engine::Renderer::GetInstance();original_=r->GetGraphicsSettings();r->SetFluidProfilerEnabled(true);
        auto s=r->GetGraphicsSettings();s.rtShadows=s.rtReflections=s.rtIndirect=false;r->ApplyGraphicsSettings(s);
        std::filesystem::create_directories("SceneryBenchmark");report_.open("SceneryBenchmark/result.txt");
        report_<<"initialize_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-start_).count()<<'\n';report_.flush();
        for(auto e:GetComponents().view<Game::MeshRendererComponent>()){
            auto& mr=GetComponents().get<Game::MeshRendererComponent>(e);
            if(mr.enabled&&(mr.shaderName=="EnvironmentSurface"||mr.shaderName=="MeadowGrass"))scenery_.push_back(e);
        }
        previous_=Clock::now();
    }
    void Update()override{
        auto now=Clock::now();double ms=std::chrono::duration<double,std::milli>(now-previous_).count();previous_=now;
        auto* r=Engine::Renderer::GetInstance();
        if(!warmed_){Game::GameScene::Update();warmed_=!r->GetPostProcessParams().cinematicCamera&&std::chrono::duration<double>(now-start_).count()>5;
            if(warmed_){report_<<"warmup_ms="<<std::chrono::duration<double,std::milli>(now-start_).count()<<'\n';Configure();}return;}
        r->SetFluidProfilerEnabled(true);
        if(phase_==4)Game::GameScene::Update();
        if(++frame_>24){const auto& p=r->GetFluidProfileStats();if(p.valid){for(size_t i=0;i<sum_.size();++i)sum_[i]+=p.lastMs[i];
            auto cpu=Engine::NetworkProfiler::GetInstance().GetData();frameMs_+=ms;cpuRender_+=cpu.gpuRenderTimeMs;cpuLogic_+=cpu.cpuLogicTimeMs;++samples_;}}
        if(frame_<64)return;
        if(samples_==0){report_<<"FAIL: GPU profile unavailable\n";report_.close();PostQuitMessage(2);return;}
        const char* names[]={"scenery","without_scenery","rt_shadow","rt_all","live_rt_all"};auto lod=r->GetSceneryLodStats();
        report_<<names[phase_]<<" samples="<<samples_<<" frame_ms="<<frameMs_/samples_<<" fps="<<1000*samples_/frameMs_
            <<" cpu_render_ms="<<cpuRender_/samples_<<" cpu_logic_ms="<<cpuLogic_/samples_<<" dlss="<<r->DlssActive()<<" selected_triangles="<<lod.selectedIndices/3
            <<" visible_triangles="<<lod.visibleIndices/3<<" shadow_triangles="<<lod.shadowIndices/3<<" tlas_builds="<<r->RtTlasBuilds()<<'\n';
        for(size_t i=0;i<sum_.size();++i)report_<<"gpu_"<<i<<"="<<sum_[i]/samples_<<' ';report_<<'\n';report_.flush();
#ifndef NDEBUG
        if(phase_==4)for(const auto& timing:GetUpdateTimings())report_<<"cpu_update "<<timing.name<<"="<<timing.ms<<'\n';
#endif
        sum_.fill(0);samples_=frame_=0;frameMs_=cpuRender_=cpuLogic_=0;
        if(++phase_==5){r->ApplyGraphicsSettings(original_);report_<<"PASS\n";report_.close();PostQuitMessage(0);}else Configure();
    }
    void Draw()override{
        if(warmed_){auto player=FindObjectByName("Player");auto p=GetComponents().get<Game::TransformComponent>(player).translate;
            auto& camera=GetCamera();camera.StopShake();camera.SetHandheld(0);camera.SetPosition(p.x,p.y+6,p.z-16);camera.LookAt(p.x,p.y+1,p.z+12,0,1,0);}
        Game::GameScene::Draw();
    }
    void DrawEditor()override{}
};
