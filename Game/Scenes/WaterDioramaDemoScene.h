#pragma once
// WaterDioramaDemoScene.h
// 水のデモ: 箱庭の中に岩に見立てた cube を積み重ね、いちばん上の岩の割れ目から水を流し続ける。
// 水はプレイヤーのスライムと同じ GPU 流体 (PBF の計算と体積の描画) を、水専用の種類 (Renderer::kPoolWaterType) で使う。
// 時間で消えない水なので、流れ落ちた水は石の枠の中にたまっていく (いっぱいになると古い水から出口へ戻り、流れは止まらない)。
// シーンの中身は Resources/Scenes/water_diorama.json (scripts/make_water_diorama.py で作る)。起動は --water-demo。
// 操作: 右ドラッグ / A・D / ←→ で回る、ホイール / W・S で近づく・離れる、R で水を空にする、Space で水を止める・流す
// 確認用: --water-demo-capture を付けると、決まった時間に画面を tests/out/water-demo-*.png に撮り、
// 1 コマの時間と GPU の流体の時間を tests/out/water-demo.txt に書いて終わる
#include "GameScene.h"
#include "../Components/RenderComponents.h"
#include "Input.h"
#include "Time/TimeManager.h"
#include <algorithm>
#include <cmath>
#include <dinput.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "../../externals/DirectXTex/DirectXTex.h"

namespace Game {
class WaterDioramaDemoScene : public GameScene {
public:
    static constexpr const char* Stage = "Resources/Scenes/water_diorama.json";
    ~WaterDioramaDemoScene() override {
        if (auto* renderer = Engine::Renderer::GetInstance()) renderer->ClearFluidVolumeFocus();
    }
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override {
        auto demo = params; demo.stagePath = Stage;
        GameScene::Initialize(dx, demo);
        window_ = dx;
        capture_ = wcsstr(GetCommandLineW(), L"--water-demo-capture") != nullptr;
        if (capture_) { std::filesystem::create_directories("tests/out"); Engine::Renderer::GetInstance()->SetFluidProfilerEnabled(true); last_ = std::chrono::steady_clock::now(); }
        // プレイヤーのいない箱庭: ゲームのシステムは動かさず、流体と描画だけを動かす
        SetIsPlaying(false);
        ResetFluidPreview();
        ApplyCamera();
    }
    void Update() override {
        GameScene::Update();
        const float dt = std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(), 0.f, .1f);
        auto* input = Engine::Input::GetInstance();
        bool steered = false;
        if (input) {
            // 回る・近づく
            float turn = 0, zoom = 0;
            if (input->Down(DIK_A) || input->Down(DIK_LEFT)) turn -= 1;
            if (input->Down(DIK_D) || input->Down(DIK_RIGHT)) turn += 1;
            if (input->Down(DIK_W) || input->Down(DIK_UP)) zoom -= 1;
            if (input->Down(DIK_S) || input->Down(DIK_DOWN)) zoom += 1;
            yaw_ += turn * 1.2f * dt;
            distance_ += zoom * 14.f * dt;
            if (input->IsMouseDown(1)) {
                yaw_ += input->GetMouseDeltaX() * .005f;
                pitch_ += input->GetMouseDeltaY() * .004f;
                steered = true;
            }
            distance_ -= input->GetMouseWheelDelta() * .01f;
            steered |= turn != 0 || zoom != 0;
            // R: 水を空にして最初から / Space: 水を止める・流す
            if (input->Trigger(DIK_R)) ResetFluidPreview();
            if (input->Trigger(DIK_SPACE)) {
                auto& registry = GetComponents();
                for (auto entity : GetRegistry().view<FluidEmitterComponent>()) {
                    auto& emitter = registry.get<FluidEmitterComponent>(entity);
                    emitter.enabled = !emitter.enabled;
                }
            }
        }
        // 触っていない間はゆっくり回る
        idle_ = steered ? 0.f : idle_ + dt;
        if (idle_ > 3) yaw_ += .08f * dt;
        pitch_ = std::clamp(pitch_, .15f, 1.35f);
        distance_ = std::clamp(distance_, 10.f, 60.f);
        ApplyCamera();
        if (capture_) UpdateCapture();
    }
    void Draw() override {
        // 箱庭全体 (岩の上の出口まで) を細かい升で描く
        if (auto* renderer = Engine::Renderer::GetInstance())
            renderer->SetFluidVolumeFocus({-7.6f, -.6f, -7.6f}, {7.6f, 7.f, 7.6f});
        GameScene::Draw();
    }

private:
    // ---- 確認用の撮影 ----
    Engine::WindowDX* window_ = nullptr;
    bool capture_ = false;
    float clock_ = 0;int shot_ = 0;std::string notes_;
    std::chrono::steady_clock::time_point last_;
    double wall_ = 0, sim_ = 0, fluid_ = 0;int frames_ = 0, gpuFrames_ = 0;
    void Capture(const std::wstring& path) {
        DirectX::ScratchImage image;
        if (SUCCEEDED(DirectX::CaptureTexture(window_->Queue(), window_->GetCurrentBackBufferResource(), false, image, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT)))
            DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str());
    }
    void UpdateCapture() {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last_).count();last_ = now;
        clock_ += std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(), 0.f, .1f);
        if (clock_ > 2) {
            wall_ += ms;++frames_;
            const auto& stats = Engine::Renderer::GetInstance()->GetFluidProfileStats();
            if (stats.valid && stats.framesSinceSample == 0) {
                sim_ += stats.lastMs[Engine::Renderer::FluidSimulation];
                fluid_ += stats.totalMs;++gpuFrames_;
            }
        }
        // 撮る時間 (秒)・名前・カメラ
        struct Shot { float at; const wchar_t* name; float yaw, pitch, distance; };
        static const Shot shots[] = {{4, L"start", -.72f, .62f, 27}, {14, L"filling", -.72f, .62f, 27}, {28, L"close", -.4f, .45f, 16},
                                     {42, L"full", -.72f, .62f, 27}, {48, L"top", .2f, 1.25f, 26}, {54, L"fall", -1.1f, .3f, 14}};
        if (shot_ < int(std::size(shots))) {
            const auto& s = shots[size_t(shot_)];
            if (clock_ >= s.at - 1) { yaw_ = s.yaw;pitch_ = s.pitch;distance_ = s.distance;idle_ = -100;ApplyCamera(); }
            if (clock_ >= s.at) {
                Capture(std::wstring(L"tests/out/water-demo-") + s.name + L".png");
                char line[200];snprintf(line, sizeof(line), "%ls t=%.1f particles=%u cell=%.3f\n", s.name, clock_, Engine::Renderer::GetInstance()->gpuFluidActiveParticleCount_, Engine::Renderer::GetInstance()->volumePreviousVoxelSize_);
                notes_ += line;++shot_;
            }
            return;
        }
        char line[200];snprintf(line, sizeof(line), "frame_ms=%.2f gpu_sim_ms=%.2f gpu_fluid_ms=%.2f frames=%d\n", wall_ / std::max(1, frames_), sim_ / std::max(1, gpuFrames_), fluid_ / std::max(1, gpuFrames_), frames_);
        std::ofstream("tests/out/water-demo.txt") << notes_ << line;
        PostQuitMessage(0);
    }
    // 箱庭の中心を見る、回り込むカメラ
    float yaw_ = -.72f, pitch_ = .62f, distance_ = 27.f, idle_ = 0;
    void ApplyCamera() {
        const float tx = -.5f, ty = 1.2f, tz = 1.f;
        const float horizontal = std::cos(pitch_) * distance_;
        auto& camera = GetCamera();
        camera.SetPosition(tx - std::sin(yaw_) * horizontal, ty + std::sin(pitch_) * distance_, tz - std::cos(yaw_) * horizontal);
        camera.SetRotation(pitch_, yaw_, 0);
    }
};
}
