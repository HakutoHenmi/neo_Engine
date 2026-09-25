#pragma once
#include "../Game/Scenes/GameScene.h"
#include "../Engine/NetworkProfiler.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

// Opt-in Development benchmark of the shipped scene, including its emitter.
// It never enters the editor or writes the scene file.
class FluidGameBenchmarkScene final : public Game::GameScene {
    bool editor_ = false;
    bool collisionProbe_ = false;
    unsigned frames_ = 0;
    std::chrono::steady_clock::time_point previous_{};
    std::array<double, 120> frameSeconds_{};
public:
    explicit FluidGameBenchmarkScene(bool editor = false, bool collisionProbe = false)
        : editor_(editor), collisionProbe_(collisionProbe) {}
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override {
        Game::GameScene::Initialize(dx, params);
        Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true);
        if (collisionProbe_) {
            auto player = FindObjectByName("Player");
            if (GetRegistry().valid(player) && GetRegistry().all_of<Game::TransformComponent>(player)) {
                auto probe = CreateEntity("FluidCollisionProbe");
                auto& position = GetRegistry().get<Game::TransformComponent>(probe);
                position.translate = GetRegistry().get<Game::TransformComponent>(player).translate;
                auto& body = GetRegistry().emplace<Game::RigidbodyComponent>(probe);
                body.isKinematic = false;
                body.useGravity = false;
                auto& box = GetRegistry().emplace<Game::BoxColliderComponent>(probe);
                box.size = {2.0f, 2.0f, 2.0f};
            }
        }
        previous_ = std::chrono::steady_clock::now();
    }
    void DrawEditor() override {
        if (editor_) Game::GameScene::DrawEditor();
        Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true);
    }
    void Update() override {
        const auto now = std::chrono::steady_clock::now();
        if (frames_ > 0) frameSeconds_[(frames_ - 1) % frameSeconds_.size()] =
            std::chrono::duration<double>(now - previous_).count();
        previous_ = now;
        Game::GameScene::Update();
        if (++frames_ < (collisionProbe_ ? 80U : 500U)) return;
        const auto& profile = Engine::Renderer::GetInstance()->GetFluidProfileStats();
        std::vector<double> sortedFrames;
        double seconds = 0;
        for (double value : frameSeconds_) if (value > 0.0) {
            seconds += value;
            sortedFrames.push_back(value);
        }
        std::sort(sortedFrames.begin(), sortedFrames.end());
        const auto below60 = std::count_if(sortedFrames.begin(), sortedFrames.end(),
            [](double value) { return value > 1.0 / 60.0; });
        std::ofstream report("tests/out/fluid-game-benchmark.txt");
        const auto collisionDispatches = Engine::Renderer::GetInstance()->CollisionDispatchCount();
        report << "mode=" << (collisionProbe_ ? "collision" : editor_ ? "editor" : "game")
            << " frames=" << frames_ << " fps_last_120=" << (seconds > 0 ? sortedFrames.size() / seconds : 0)
            << " particles=" << profile.particleSlots << " substeps=" << profile.substeps
            << " sampled=" << profile.valid << " samples=" << profile.historyCount
            << " collision_dispatches=" << collisionDispatches
            << " below_60_frames=" << below60
            << " p95_frame_ms=" << sortedFrames[(sortedFrames.size() * 95 - 1) / 100] * 1000.0
            << " worst_frame_ms=" << sortedFrames.back() * 1000.0 << '\n';
        const auto cpu = Engine::NetworkProfiler::GetInstance().GetData();
        report << "cpu_logic_last_ms=" << cpu.cpuLogicTimeMs
            << " cpu_frame_render_last_ms=" << cpu.gpuRenderTimeMs
            << " cpu_sim_record_last_ms=" << profile.cpuSimulationMs
            << " cpu_volume_record_last_ms=" << profile.cpuVolumeMs << '\n';
        for (const auto& timing : GetUpdateTimings())
            report << "cpu_update " << timing.name << "=" << timing.ms << " ms\n";
        for (unsigned stage = 0; stage < Engine::Renderer::kFluidProfileStageCount; ++stage)
            report << stage << " last_ms=" << profile.lastMs[stage] << " mean_ms=" << profile.averageMs[stage] << '\n';
        PostQuitMessage(profile.valid && (collisionProbe_ ? collisionDispatches > 0 : profile.particleSlots >= 27000) ? 0 : 2);
    }
};
