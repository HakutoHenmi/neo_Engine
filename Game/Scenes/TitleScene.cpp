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

    LogFileMain("    TitleScene::Initialize 2 (Loading models)");
    groundMesh_ = renderer_->LoadObjMesh("Resources/Models/plane.obj");
    groundTex_  = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
    cubeMesh_   = renderer_->LoadObjMesh("Resources/Models/cube/cube.obj");
    cubeTex_    = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

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

    LogFileMain("    TitleScene::Initialize 3 (Skybox)");
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

    LogFileMain("    TitleScene::Initialize 4 (PostProcess)");
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
        bool start=UI::Pressed(DIK_RETURN)||UI::Canvas(renderer_).Click({0,0,1280,720})||input->IsMouseTrigger(1);
        for(int key=0;key<256&&!start;++key)start=input->Trigger(static_cast<BYTE>(key));
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


    UI::Canvas ui(renderer_, Engine::WindowDX::kW, Engine::WindowDX::kH, uiAlpha_);
    if (uiAlpha_ > .01f) {
        ui.Background("FIELD OPERATIONS");
        ui.Panel({240,125,800,300});
        ui.Center("VERDANT BASIN",640,167,22,UI::Muted);
        ui.Center(UI::GameTitle,640,230,60,UI::Lime,UI::JapaneseFont);
        ui.Center("MOVE. RECALL. RELEASE.",640,339,27);
        ui.Button({440,520,400,66},"BEGIN EXPEDITION",true);
        ui.Prompt("keyboard_enter","PRESS ANY KEY / CLICK",495,607);
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
