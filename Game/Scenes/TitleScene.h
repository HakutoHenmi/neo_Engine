#pragma once
#include "IScene.h"
#include "../../externals/entt/entt.hpp"
#include "../../Engine/Camera.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include <memory>
#include <chrono>

namespace Game {

class UISystem;

// ========================================================
// TitleScene: 映画的な3Dタイトル画面
//   - 手前にプレイヤー、遠景に霧の中のボス
//   - フェイク反射（水面）
//   - "Press Any Button" → シームレスにゲーム遷移
// ========================================================
class TitleScene : public Engine::IScene {
public:
    TitleScene() = default;
    ~TitleScene() override = default;

    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;

private:
    // === 遷移フェーズ ===
    enum class Phase {
        Idle,        // タイトル表示中（入力待ち）
        FadeOut,     // UIフェードアウト (0.0~0.2s)
        CameraMove,  // カメラ移動 + FOV変更 (0.2~2.2s)
        InkSplash,   // 墨トランジション (1.5~2.0s)
        GameStart    // ゲームシーンへ遷移
    };

    Phase phase_ = Phase::Idle;
    float phaseTimer_ = 0.0f;
    float totalTime_ = 0.0f;   // 経過時間（サイン波等に使用）
    float uiAlpha_ = 1.0f;     // UI全体の透明度
    float inkAlpha_ = 0.0f;    // 墨トランジションの不透明度
    float dt_ = 1.0f / 60.0f;

    // === カメラ ===
    Engine::Camera camera_;
    // 初期状態
    DirectX::XMFLOAT3 camStartPos_ = { -1.5f, 0.8f, -1.0f };
    DirectX::XMFLOAT3 camStartTarget_ = { 0.0f, 1.0f, 45.0f };
    float camStartFov_ = DirectX::XMConvertToRadians(38.0f);
    // 遷移先
    DirectX::XMFLOAT3 camEndPos_ = { 0.0f, 1.5f, -2.0f };
    DirectX::XMFLOAT3 camEndTarget_ = { 0.0f, 1.0f, 10.0f };
    float camEndFov_ = DirectX::XMConvertToRadians(60.0f);
    float currentFov_;

    // === レンダリングリソース ===
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;

    // モデルハンドル
    uint32_t groundMesh_ = 0;
    uint32_t groundTex_ = 0;
    uint32_t cubeMesh_ = 0;
    uint32_t cubeTex_ = 0;
    uint32_t titleMesh_ = 0;
    uint32_t titleTex_ = 0;
    uint32_t titleFutiMesh_ = 0;
    uint32_t titleFutiTex_ = 0;

    // === ヘルパー ===
    void DrawMeshAt(uint32_t mesh, uint32_t tex,
                    const DirectX::XMFLOAT3& pos,
                    const DirectX::XMFLOAT3& rot,
                    const DirectX::XMFLOAT3& scale,
                    const Engine::Vector4& color,
                    const std::string& shader = "Default");

    // イージング: EaseInOutCubic
    static float EaseInOut(float t);

    // 時間管理
    std::chrono::steady_clock::time_point lastTime_;
};

} // namespace Game
