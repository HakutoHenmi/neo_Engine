#include "../UI/SceneMusic.h"
#include "../UI/CreditsUI.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TitleScene.h"
#include "../UI/GameUI.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include "../Systems/UISystem.h"
#include "../ObjectTypes.h"
#include "../../externals/imgui/imgui.h"
#include <cmath>
#include <algorithm>
#include <filesystem>

void LogFileMain(const char* msg);

namespace Game {

// ============================================================
// EaseInOutCubic
// ============================================================
float TitleScene::EaseInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// ============================================================
// DrawMeshAt: 指定座標にメッシュを描画するヘルパー
// ============================================================
void TitleScene::DrawMeshAt(uint32_t mesh, uint32_t tex,
                            const DirectX::XMFLOAT3& pos,
                            const DirectX::XMFLOAT3& rot,
                            const DirectX::XMFLOAT3& scale,
                            const Engine::Vector4& color,
                            const std::string& shader) {
    if (!mesh || !renderer_) return;
    Engine::Transform t;
    t.translate = { pos.x, pos.y, pos.z };
    t.rotate = { rot.x, rot.y, rot.z };
    t.scale = { scale.x, scale.y, scale.z };
    renderer_->DrawMesh(mesh, tex, t, color, shader);
}

// ============================================================
// Initialize
// ============================================================
void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& /*params*/) {
    LogFileMain("    TitleScene::Initialize 1");
    dx_ = dx;
    creditsOpen_ = false;
    creditsPage_ = 0;
    Music::Play(Music::Title, .32f);
    renderer_ = Engine::Renderer::GetInstance();
    lastTime_ = std::chrono::steady_clock::now();

    camStartPos_ = { 0.0f, 0.8f, -2.5f };
    camera_.Initialize();
    currentFov_ = camStartFov_;
    float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
    camera_.SetProjection(currentFov_, aspect, 0.1f, 500.0f);
    camera_.SetPosition(camStartPos_);
    camera_.LookAt(camStartTarget_, { 0, 1, 0 });

    renderer_->SetAmbientColor({ 0.15f, 0.15f, 0.2f });
    renderer_->SetDirectionalLight(
        { 0.3f, -0.8f, 0.5f },
        { 0.6f, 0.6f, 0.7f },
        true
    );

    renderer_->SetPostProcessEnabled(true);
    renderer_->SetPostEffect("Default");
    renderer_->SetPostProcessParams({});

    phase_ = Phase::Idle;
    phaseTimer_ = 0.0f;
    totalTime_ = 0.0f;
    uiAlpha_ = 1.0f;
    inkAlpha_ = 0.0f;
    
    // カーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
    
    LogFileMain("    TitleScene::Initialize Complete");
}

// ============================================================
// Update
// ============================================================
void TitleScene::Update() {
    // dt 計算
    auto now = std::chrono::steady_clock::now();
    dt_ = std::chrono::duration<float>(now - lastTime_).count();
    lastTime_ = now;
    if (dt_ > 0.1f) dt_ = 1.0f / 60.0f;

    totalTime_ += dt_;
    camera_.Tick(dt_);

    // ポストプロセス時間更新
    auto pp = renderer_->GetPostProcessParams();
    pp.time = totalTime_;
    renderer_->SetPostProcessParams(pp);

    auto* input = Engine::Input::GetInstance();
    if (input) {
        UI::Canvas ui(renderer_);
        if(creditsOpen_){
            if(UI::Pressed(DIK_ESCAPE)||ui.Click(UI::CreditsBack))creditsOpen_=false;
            else if(creditsPage_>0&&ui.Click(UI::CreditsPrev))--creditsPage_;
            else if(creditsPage_+1<UI::CreditsPageCount&&ui.Click(UI::CreditsNext))++creditsPage_;
            renderer_->SetCamera(camera_);
            return; // Enter never starts the game while reading credits.
        }
        if(ui.Click(UI::CreditsButton)){
            creditsOpen_=true;creditsPage_=0;
            renderer_->SetCamera(camera_);
            return;
        }
        bool start=UI::Pressed(DIK_RETURN)||UI::Canvas(renderer_).Click({440,520,400,66});
        if(start){
            Engine::SceneManager::GetInstance()->RequestChange("Select");
            return;
        }
    }
    // カメラをRendererに設定
    renderer_->SetCamera(camera_);
}

// ============================================================
// Draw: 3Dシーンの描画
// ============================================================
void TitleScene::Draw() {
    if (!renderer_) return;
    if(creditsOpen_){UI::Canvas ui(renderer_);UI::DrawCredits(ui,creditsPage_);return;}

    UI::Canvas ui(renderer_, Engine::WindowDX::kW, Engine::WindowDX::kH, uiAlpha_);
    if (uiAlpha_ > .01f) {
        ui.Background("FIELD OPERATIONS");
        ui.Panel({240,125,800,300});
        ui.Center("VERDANT BASIN",640,167,22,UI::Muted);
        ui.Center(UI::GameTitle,640,230,60,UI::Lime,UI::JapaneseFont);
        ui.Center("DRAW. ENCIRCLE. UNLEASH.",640,339,27);
        ui.Button({440,520,400,66},"BEGIN EXPEDITION",true);
        ui.Prompt("keyboard_enter","ENTER / CLICK BEGIN",495,607);
        ui.Center("Music: Kevin MacLeod (incompetech.com) / CC BY 4.0",640,635,16,UI::Muted);
        ui.Button(UI::CreditsButton,"CREDITS");
    }
    if (inkAlpha_ > .01f) {
        UI::Canvas transition(renderer_);
        transition.Fill({0,0,1280,720},{.02f,.045f,.035f,inkAlpha_});
    }
}

// ============================================================
// DrawUI: 2D UI（ImGui）の描画
// ============================================================
void TitleScene::DrawUI() {}

} // namespace Game
