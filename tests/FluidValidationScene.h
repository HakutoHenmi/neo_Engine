#pragma once
#include "../Engine/IScene.h"
#include "../Engine/Renderer.h"
#include "../Engine/Camera.h"
#include "../Engine/Time/TimeManager.h"
#include <stdexcept>
#include <fstream>

// Opt-in Debug/Development integration smoke scene. No asset/editor saves, no input
// automation. Exercises actual D3D12 initialization, emission, solve and draw.
class FluidValidationScene final : public Engine::IScene {
    Engine::Camera camera_;
    Engine::Renderer* renderer_ = nullptr;
    unsigned frame_ = 0;
public:
    void Initialize(Engine::WindowDX*, const Engine::SceneParameters&) override {
        renderer_=Engine::Renderer::GetInstance();
        if(!renderer_->IsFluidVolumeReady()) throw std::runtime_error("Fluid volume initialization failed in smoke test");
		if(!renderer_->GetFluidProfileStats().available) throw std::runtime_error("Fluid GPU profiler initialization failed in smoke test");
		renderer_->SetFluidProfilerEnabled(true);
        renderer_->ResetGPUFluid();
        renderer_->SetUseCubemapBackground(false);
        camera_.Initialize(); camera_.SetProjection(0.7854f,1920.0f/1080.0f,0.1f,100);
        camera_.SetPosition(5,4,-8); camera_.LookAt(0,1,0,0,1,0);
    }
    void Update() override {
		if(frame_>=12) {
			const auto& profile=renderer_->GetFluidProfileStats();
			std::ofstream sample("tests/out/fluid-profile-sample.txt");
			sample << "valid=" << profile.valid << " particles=" << profile.particleSlots
				<< " total_ms=" << profile.totalMs << " age=" << profile.framesSinceSample
				<< " samples=" << profile.historyCount << " draw_frame=" << frame_
				<< " cpu_sim_ms=" << profile.cpuSimulationMs << " cpu_volume_ms=" << profile.cpuVolumeMs << '\n';
			for(float ms:profile.lastMs) sample << ms << ' ';
			sample << '\n';
			for(bool used:profile.sampledStages) sample << used << ' ';
			sample << '\n';
			const bool complete = profile.valid && profile.particleSlots > 0 &&
				profile.sampledStages[Engine::Renderer::FluidSimulation] &&
				profile.sampledStages[Engine::Renderer::FluidShapes] &&
				profile.sampledStages[Engine::Renderer::FluidRaymarch];
			PostQuitMessage(complete ? 0 : 2);
		}
	}
    void Draw() override {
        Engine::TimeManager::GetInstance().Update(1.0f/60.0f);
        renderer_->SetCamera(camera_);
        renderer_->SetGPUFluidCore({0,1,0},80);
        if(frame_==0) {
            renderer_->EmitGPUFluid({0,1,0},{0,0,0},{0.1f,0.8f,0.1f,1},2000,0);
            for(int z=-2;z<=2;++z) for(int x=-2;x<=2;++x)
                renderer_->EmitGPUFluid({x*0.5f,0.8f,z*0.5f},{0,0,0},{0.1f,0.6f,1,1},32,1);
        }
        // Exercise all debug branches as well as normal material + spray.
        renderer_->SetFluidVolumeDebugMode((frame_/2)%5);
        ++frame_;
    }
};
