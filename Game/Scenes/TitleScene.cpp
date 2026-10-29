// TitleScene.cpp
// タイトル画面の初期化・入力処理・描画。
#include "../UI/SceneMusic.h"
#include "../UI/CreditsUI.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TitleScene.h"
#include "../UI/GameUI.h"
#include "../Data/GameConfig.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include "../Systems/UISystem.h"
#include "../ObjectTypes.h"
#include "../../externals/imgui/imgui.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <filesystem>

// main.cpp の起動ログ出力関数
void LogFileMain(const char* msg);

namespace Game {

// ============================================================
// EaseInOutCubic: 0〜1 をゆっくり始まりゆっくり終わる曲線に変換
// ============================================================
float TitleScene::EaseInOut(float t) {
    // 前半は 3 次で加速、後半は 3 次で減速する
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// ============================================================
// DrawMeshAt: 指定した位置・回転・拡大でメッシュを描くヘルパー
// ============================================================
void TitleScene::DrawMeshAt(uint32_t mesh, uint32_t tex,
                            const DirectX::XMFLOAT3& pos,
                            const DirectX::XMFLOAT3& rot,
                            const DirectX::XMFLOAT3& scale,
                            const Engine::Vector4& color,
                            const std::string& shader) {
    // メッシュが無い・描画先が無いときは何もしない
    if (!mesh || !renderer_) return;
    Engine::Transform t;
    t.translate = { pos.x, pos.y, pos.z };
    t.rotate = { rot.x, rot.y, rot.z };
    t.scale = { scale.x, scale.y, scale.z };
    renderer_->DrawMesh(mesh, tex, t, color, shader);
}

// ============================================================
// Initialize: カメラ・ライト・ポストエフェクトと画面の状態を初期化する
// ============================================================
void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& /*params*/) {
    LogFileMain("    TitleScene::Initialize 1");
    // 初期状態に戻す (クレジットを閉じた状態から始める)
    dx_ = dx;
    creditsOpen_ = false;
    creditsPage_ = 0;
    // タイトル BGM を流す
    Music::Play(Music::Title);
    renderer_ = Engine::Renderer::GetInstance();
    lastTime_ = std::chrono::steady_clock::now();

    // カメラの位置と向き
    camStartPos_ = { 0.0f, 0.8f, -2.5f };
    camera_.Initialize();
    currentFov_ = camStartFov_;
    float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
    camera_.SetProjection(currentFov_, aspect, 0.1f, 500.0f);
    camera_.SetPosition(camStartPos_);
    camera_.LookAt(camStartTarget_, { 0, 1, 0 });

    // 環境光と平行光源
    renderer_->SetAmbientColor({ 0.15f, 0.15f, 0.2f });
    renderer_->SetDirectionalLight(
        { 0.3f, -0.8f, 0.5f },
        { 0.6f, 0.6f, 0.7f },
        true
    );

    // ポストエフェクトを標準にする (前のシーンの設定を引き継がない)
    renderer_->SetPostProcessEnabled(true);
    renderer_->SetPostEffect("Default");
    renderer_->SetPostProcessParams({});

    // 遷移の段階と経過時間をリセット
    phase_ = Phase::Idle;
    phaseTimer_ = 0.0f;
    totalTime_ = 0.0f;
    uiAlpha_ = 1.0f;
    inkAlpha_ = 0.0f;
    
    // タイトルではマウスで操作するのでカーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
    
    LogFileMain("    TitleScene::Initialize Complete");
}

// ============================================================
// Update: 経過時間の更新とボタン入力の処理
// ============================================================
void TitleScene::Update() {
    // 前フレームからの経過時間 (長く止まった後は 1/60 秒として扱う)
    auto now = std::chrono::steady_clock::now();
    dt_ = std::chrono::duration<float>(now - lastTime_).count();
    lastTime_ = now;
    if (dt_ > 0.1f) dt_ = 1.0f / 60.0f;

    // 経過時間を進め、カメラの揺れなどを更新
    totalTime_ += dt_;
    camera_.Tick(dt_);

    // ポストエフェクトのアニメーション用の時間
    auto pp = renderer_->GetPostProcessParams();
    pp.time = totalTime_;
    renderer_->SetPostProcessParams(pp);

    // マウス・キーボード・ゲームパッドの入力でボタンを処理する
    auto* input = Engine::Input::GetInstance();
    if (input) {
        UI::Canvas ui(renderer_);
        // クレジット表示中: 戻る / 前のページ / 次のページ
        if(creditsOpen_){
            if(UI::BackPressed()||ui.Click(UI::CreditsBack))creditsOpen_=false;
            else if(creditsPage_>0&&(ui.Click(UI::CreditsPrev)||UI::PadPressed(XINPUT_GAMEPAD_LEFT_SHOULDER)))--creditsPage_;
            else if(creditsPage_+1<UI::CreditsPageCount()&&(ui.Click(UI::CreditsNext)||UI::PadPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)))++creditsPage_;
            renderer_->SetCamera(camera_);
            return; // クレジット表示中は Enter でゲームを始めない
        }
        // CREDITS ボタン: クレジット画面を開く
        if(ui.Click(UI::CreditsButton)){
            creditsOpen_=true;creditsPage_=0;
            renderer_->SetCamera(camera_);
            return;
        }
        // 開始: Enter / START / BEGIN EXPEDITION ボタンで本編ステージへ直接移る
        bool start=UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_START)||UI::Canvas(renderer_).Click(UI::TitleStart);
        if(start){
            Engine::SceneParameters params;params.stagePath=Data::MainStagePath();
            Engine::SceneManager::GetInstance()->RequestChange("Game",params);
            return;
        }
    }
    // このフレームのカメラを描画に渡す
    renderer_->SetCamera(camera_);
}

// ============================================================
// Draw: タイトル画面 (2D UI) の描画
// ============================================================
void TitleScene::Draw() {
    if (!renderer_) return;
    // クレジット表示中はクレジットだけを描く
    if(creditsOpen_){UI::Canvas ui(renderer_);UI::DrawCredits(ui,creditsPage_);return;}

    // UI の不透明度 (遷移時のフェード) を掛けて描く
    UI::Canvas ui(renderer_, Engine::WindowDX::kW, Engine::WindowDX::kH, uiAlpha_);
    if (uiAlpha_ > .01f) {
        // 背景の模様と上部のロゴ
        ui.Background(UI::Str("title.section").c_str());
        const float t=UI::Seconds();
        // 主役: 右側で無限記号の軌跡を描いて動くスライム
        const float cx=990,cy=330;
        ui.Splat(cx+30,cy+40,310,UI::WithAlpha(UI::Lime,.05f),.6f);
        ui.Halo(cx,cy,320,UI::WithAlpha(UI::Lime,.09f));
        ui.Halo(cx,cy+142,190,UI::WithAlpha(UI::Night,.7f));
        auto loop=[&](float s){return std::array<float,2>{cx+std::cos(s)*215,cy+70+std::sin(s*2)*62};};
        for(int k=90;k>=0;--k){const float s=t*.85f-float(k)*.034f,fade=1-float(k)/90;auto p=loop(s);
            ui.Dot(p[0],p[1],2+7*fade,k==0?UI::Paper:UI::WithAlpha(UI::Lime,.1f+.7f*fade));}
        ui.SlimeMark(cx,cy+8*std::sin(t*1.7f),112);
        // タイトル: 左寄せの大きな文字。1行目の上にスライムの塗りを重ねる
        ui.Label(UI::Str("title.tagline"),98,148,14,UI::WithAlpha(UI::Lime,.9f),3.4f);
        ui.Text(UI::Str("title.line1"),86,168,88,UI::Paper,UI::JapaneseFont);
        ui.Brush({52,258,710,148},UI::WithAlpha(UI::Lime,.95f),-.028f);
        ui.Text(UI::Str("title.line2"),86,282,88,UI::Ink,UI::JapaneseFont);
        ui.Label(UI::Str("title.motto"),100,414,17,UI::WithAlpha(UI::Paper,.85f),5.f);
        // ボタンと操作の案内
        ui.Button(UI::TitleStart,UI::Str("title.start"),true);
        ui.Button(UI::CreditsButton,UI::Str("title.credits"));
        ui.Prompt("keyboard_enter",UI::Str("title.promptKey"),"xbox_button_a",UI::Str("title.promptPad"),338,594,28,17);
        // BGM のクレジット表記 (右下)
        const std::string& music=UI::Str("title.music");
        ui.Label(music,1232-ui.SpacedWidth(music,11,1.6f),664,11,UI::WithAlpha(UI::Muted,.7f),1.6f);
    }
    // 遷移時に画面全体を暗くする
    if (inkAlpha_ > .01f) {
        UI::Canvas transition(renderer_);
        transition.Fill({0,0,1280,720},{.02f,.045f,.035f,inkAlpha_});
    }
}

// ============================================================
// DrawUI: ImGui 用 (タイトルでは使わない)
// ============================================================
void TitleScene::DrawUI() {}

} // namespace Game
