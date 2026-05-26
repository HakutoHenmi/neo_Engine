#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TitleScene.h"
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
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    lastTime_ = std::chrono::steady_clock::now();

    // --- カメラ初期化 ---
    // プレイヤーが画面中央を塞がないよう、カメラを少し右（X=0）に配置
    camStartPos_ = { 0.0f, 0.8f, -2.5f };
    camera_.Initialize();
    currentFov_ = camStartFov_;
    float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
    camera_.SetProjection(currentFov_, aspect, 0.1f, 500.0f);
    camera_.SetPosition(camStartPos_);
    camera_.LookAt(camStartTarget_, { 0, 1, 0 });

    // --- ライティング ---
    renderer_->SetAmbientColor({ 0.15f, 0.15f, 0.2f }); // 暗めの雰囲気
    renderer_->SetDirectionalLight(
        { 0.3f, -0.8f, 0.5f },   // direction
        { 0.6f, 0.6f, 0.7f },    // color (月光風)
        true
    );

    // --- モデルのロード ---
    groundMesh_ = renderer_->LoadObjMesh("Resources/Models/plane.obj");
    groundTex_  = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
    cubeMesh_   = renderer_->LoadObjMesh("Resources/Models/cube/cube.obj");
    cubeTex_    = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

    // タイトルモデル（存在する場合）
    try {
        namespace fs = std::filesystem;
        if (fs::exists("Resources/Models/TitleParts/title.obj")) {
            titleMesh_ = renderer_->LoadObjMesh("Resources/Models/TitleParts/title.obj");
            titleTex_  = renderer_->LoadTexture2D("Resources/Models/TitleParts/title.png");
        }
        if (fs::exists("Resources/Models/TitleParts/title_futi.obj")) {
            titleFutiMesh_ = renderer_->LoadObjMesh("Resources/Models/TitleParts/title_futi.obj");
            titleFutiTex_  = renderer_->LoadTexture2D("Resources/Models/TitleParts/title_futi.png");
        }
    } catch (...) {}

    // --- Skybox ---
    try {
        namespace fs = std::filesystem;
        for (const auto& entry : fs::directory_iterator("Resources/Textures")) {
            if (entry.is_regular_file() && entry.path().extension() == L".dds") {
                std::string filename = entry.path().filename().string();
                if (filename.find("rostock_laage_airport") != std::string::npos) continue;
                auto cubeHandle = renderer_->LoadCubeMap(entry.path().string());
                if (cubeHandle > 0) {
                    renderer_->SetSkyboxTexture(cubeHandle);
                }
                break;
            }
        }
    } catch (...) {}

    // --- ポストプロセス ---
    renderer_->SetPostProcessEnabled(true);
    auto paper = renderer_->LoadTexture2D("Resources/Textures/paper.png");
    auto vignetteTex = renderer_->LoadTexture2D("Resources/Textures/vignette.png");
    renderer_->SetSumiETextures(paper, vignetteTex);

    Engine::Renderer::PostProcessParams pp;
    pp.noiseStrength = 0.4f;
    pp.chromaShift = 0.5f;
    pp.scanline = 0.0f;
    pp.distortion = 0.0f;
    pp.vignette = 0.0f;
    renderer_->SetPostProcessParams(pp);
    renderer_->SetPostEffect("Rich");

    // --- フェーズ初期化 ---
    phase_ = Phase::Idle;
    phaseTimer_ = 0.0f;
    totalTime_ = 0.0f;
    uiAlpha_ = 1.0f;
    inkAlpha_ = 0.0f;
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

    switch (phase_) {
    // --------------------------------------------------
    case Phase::Idle: {
        // 任意のキーまたはマウスボタンで遷移開始
        auto* input = Engine::Input::GetInstance();
        bool anyKey = false;
        if (input) {
            // マウスボタン
            anyKey |= input->IsMouseTrigger(0);
            anyKey |= input->IsMouseTrigger(1);
            // キーボード（主要なキーのみチェック）
            for (int k = 0; k < 256; ++k) {
                if (input->Trigger(static_cast<BYTE>(k))) { anyKey = true; break; }
            }
        }
        if (anyKey) {
            phase_ = Phase::FadeOut;
            phaseTimer_ = 0.0f;
        }
        break;
    }
    // --------------------------------------------------
    case Phase::FadeOut: {
        phaseTimer_ += dt_;
        // UIを0.2秒でフェードアウト
        uiAlpha_ = std::max(0.0f, 1.0f - phaseTimer_ / 0.2f);
        if (phaseTimer_ >= 0.2f) {
            phase_ = Phase::CameraMove;
            phaseTimer_ = 0.0f;
            uiAlpha_ = 0.0f;
        }
        break;
    }
    // --------------------------------------------------
    case Phase::CameraMove: {
        phaseTimer_ += dt_;
        float duration = 2.0f;
        float t = EaseInOut(std::min(phaseTimer_ / duration, 1.0f));

        // カメラ位置の補間
        DirectX::XMFLOAT3 pos = {
            camStartPos_.x + (camEndPos_.x - camStartPos_.x) * t,
            camStartPos_.y + (camEndPos_.y - camStartPos_.y) * t,
            camStartPos_.z + (camEndPos_.z - camStartPos_.z) * t
        };
        DirectX::XMFLOAT3 target = {
            camStartTarget_.x + (camEndTarget_.x - camStartTarget_.x) * t,
            camStartTarget_.y + (camEndTarget_.y - camStartTarget_.y) * t,
            camStartTarget_.z + (camEndTarget_.z - camStartTarget_.z) * t
        };
        currentFov_ = camStartFov_ + (camEndFov_ - camStartFov_) * t;

        camera_.SetPosition(pos);
        camera_.LookAt(target, { 0, 1, 0 });
        float aspect = (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH;
        camera_.SetProjection(currentFov_, aspect, 0.1f, 500.0f);

        // 墨トランジション（1.3s～1.8sの間で画面を覆う）
        if (phaseTimer_ >= 1.3f && phaseTimer_ < 1.8f) {
            inkAlpha_ = std::min(1.0f, (phaseTimer_ - 1.3f) / 0.3f);
        } else if (phaseTimer_ >= 1.8f) {
            inkAlpha_ = std::max(0.0f, 1.0f - (phaseTimer_ - 1.8f) / 0.4f);
        }

        if (phaseTimer_ >= 2.2f) {
            phase_ = Phase::GameStart;
            phaseTimer_ = 0.0f;
        }
        break;
    }
    // --------------------------------------------------
    case Phase::GameStart: {
        Engine::SceneManager::GetInstance()->RequestChange("Game");
        phase_ = Phase::Idle; // 再呼び出し防止
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

    // =============== 地面（水面） ===============
    // 暗い半透明の床（水面を表現）
    DrawMeshAt(groundMesh_, groundTex_,
        { 0, -0.05f, 20.0f },         // pos: 少し下に
        { 0, 0, 0 },                  // rot
        { 50.0f, 1.0f, 50.0f },       // scale
        { 0.05f, 0.07f, 0.12f, 0.95f } // 暗い紺色
    );

    // =============== プレイヤー（手前左） ===============
    float playerYaw = DirectX::XMConvertToRadians(30.0f); // やや右を向く
    // スケールを小さくし、シルエットのように暗い色にする
    DrawMeshAt(cubeMesh_, cubeTex_,
        { -2.0f, 0.5f, 2.0f },
        { 0, playerYaw, 0 },
        { 0.5f, 1.0f, 0.5f },
        { 0.05f, 0.05f, 0.08f, 1.0f } // 暗いシルエット
    );

    // プレイヤーのフェイク反射（Y反転）
    DrawMeshAt(cubeMesh_, cubeTex_,
        { -2.0f, -0.5f, 2.0f },
        { 0, playerYaw, 0 },
        { 0.5f, -1.0f, 0.5f },
        { 0.02f, 0.02f, 0.04f, 0.3f } // 暗く半透明
    );

    // =============== ボス（遠景） ===============
    // 霧の中にいるような暗い色で描画
    float bossBreath = 1.0f + 0.05f * std::sin(totalTime_ * 1.5f); // 呼吸風の微動
    DrawMeshAt(cubeMesh_, cubeTex_,
        { 0.0f, 3.0f * bossBreath, 45.0f },
        { 0, 0, 0 },
        { 4.0f, 6.0f * bossBreath, 4.0f },
        { 0.1f, 0.05f, 0.05f, 0.7f } // 暗い赤のシルエット
    );

    // ボスの目の発光（小さな明るいキューブ）
    float eyeGlow = 0.7f + 0.3f * std::sin(totalTime_ * 3.0f);
    DrawMeshAt(cubeMesh_, cubeTex_,
        { -0.8f, 4.5f * bossBreath, 44.0f },
        { 0, 0, 0 },
        { 0.3f, 0.3f, 0.3f },
        { 1.0f * eyeGlow, 0.2f * eyeGlow, 0.2f * eyeGlow, 1.0f }
    );
    DrawMeshAt(cubeMesh_, cubeTex_,
        { 0.8f, 4.5f * bossBreath, 44.0f },
        { 0, 0, 0 },
        { 0.3f, 0.3f, 0.3f },
        { 1.0f * eyeGlow, 0.2f * eyeGlow, 0.2f * eyeGlow, 1.0f }
    );

    // ボスのフェイク反射
    DrawMeshAt(cubeMesh_, cubeTex_,
        { 0.0f, -3.0f * bossBreath, 45.0f },
        { 0, 0, 0 },
        { 4.0f, -6.0f * bossBreath, 4.0f },
        { 0.04f, 0.01f, 0.01f, 0.2f }
    );


}

// ============================================================
// DrawUI: 2D UI（ImGui）の描画
// ============================================================
void TitleScene::DrawUI() {
#ifdef USE_IMGUI
    float W = (float)Engine::WindowDX::kW;
    float H = (float)Engine::WindowDX::kH;

    // デフォルトの"Debug"ウィンドウが出ないように、全画面の透明ウィンドウを作成する
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
                             
    ImGui::Begin("TitleUI", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ============ タイトルロゴ（ImGuiテキスト） ============
    if (uiAlpha_ > 0.01f) {
        // メインタイトル
        const char* titleText = "NEO ENGINE";
        ImGui::SetWindowFontScale(1.0f);
        ImVec2 textSize = ImGui::CalcTextSize(titleText);
        // 大きくスケーリング
        float scale = 5.0f;
        ImVec2 titlePos = { W * 0.5f - textSize.x * scale * 0.5f, H * 0.18f };

        // 金箔風の発光エッジ（和風・スチームパンクに合うゴールド系）
        ImU32 glowColor = IM_COL32(255, 180, 50, (int)(140.0f * uiAlpha_));
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dy = -3; dy <= 3; ++dy) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(nullptr, textSize.y * scale,
                    ImVec2(titlePos.x + dx * 2.0f, titlePos.y + dy * 2.0f),
                    glowColor, titleText);
            }
        }

        // 内側の濃いオレンジのサブグロー
        ImU32 orangeGlow = IM_COL32(200, 80, 0, (int)(100.0f * uiAlpha_));
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -2; dy <= 2; ++dy) {
                if (dx == 0 && dy == 0) continue;
                dl->AddText(nullptr, textSize.y * scale,
                    ImVec2(titlePos.x + dx * 1.0f, titlePos.y + dy * 1.0f),
                    orangeGlow, titleText);
            }
        }

        // 本体テキスト（少しクリーム色がかった白和紙風）
        ImU32 textColor = IM_COL32(255, 250, 230, (int)(255.0f * uiAlpha_));
        dl->AddText(nullptr, textSize.y * scale, titlePos, textColor, titleText);

        // ============ "Press Any Button" ============
        const char* pressText = "- Press Any Button -";
        ImVec2 pressSize = ImGui::CalcTextSize(pressText);
        float pressScale = 2.0f;
        float sinAlpha = 0.2f + 0.8f * (0.5f + 0.5f * std::sin(totalTime_ * 2.5f));
        float pressAlpha = sinAlpha * uiAlpha_;
        ImVec2 pressPos = {
            W * 0.5f - pressSize.x * pressScale * 0.5f,
            H * 0.78f
        };
        ImU32 pressColor = IM_COL32(200, 200, 210, (int)(255.0f * pressAlpha));
        dl->AddText(nullptr, pressSize.y * pressScale, pressPos, pressColor, pressText);
    }

    // ============ 墨トランジション ============
    if (inkAlpha_ > 0.01f) {
        ImU32 inkColor = IM_COL32(5, 5, 10, (int)(255.0f * std::min(inkAlpha_, 1.0f)));
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), inkColor);
    }

    // ============ カスタムマウスカーソル ============
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    ImVec2 mousePos = ImGui::GetMousePos();

    float size = 10.0f;
    ImU32 colorOuter = IM_COL32(255, 180, 50, 255);
    ImU32 colorInner = IM_COL32(255, 255, 255, 255);

    dl->AddCircleFilled(mousePos, 2.5f, colorInner);
    dl->AddCircle(mousePos, size, colorOuter, 0, 2.0f);

    dl->AddLine(ImVec2(mousePos.x - size - 6, mousePos.y), ImVec2(mousePos.x - size + 2, mousePos.y), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x + size + 6, mousePos.y), ImVec2(mousePos.x + size - 2, mousePos.y), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x, mousePos.y - size - 6), ImVec2(mousePos.x, mousePos.y - size + 2), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x, mousePos.y + size + 6), ImVec2(mousePos.x, mousePos.y + size - 2), colorOuter, 2.0f);

    dl->AddQuad(
        ImVec2(mousePos.x, mousePos.y - size * 0.7f),
        ImVec2(mousePos.x + size * 0.7f, mousePos.y),
        ImVec2(mousePos.x, mousePos.y + size * 0.7f),
        ImVec2(mousePos.x - size * 0.7f, mousePos.y),
        IM_COL32(255, 180, 50, 150), 1.5f
    );
    
    ImGui::End();
#endif
}

} // namespace Game
