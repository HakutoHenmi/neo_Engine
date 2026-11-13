// TitleScene.h
// タイトル画面のシーン。
#pragma once
#include "IScene.h"
#include "../../externals/entt/entt.hpp"
#include "../../Engine/Camera.h"
#include "../../Engine/Renderer.h"
#include "../ObjectTypes.h"
#include "../UI/CollectionUI.h"
#include <memory>
#include <array>
#include <chrono>

namespace Game {

// UI システムの前方宣言 (このヘッダーでは使わない)
class UISystem;

// ========================================================
// TitleScene: タイトル画面
//   - 物差しの UI (巻尺・大きさ順の物・大きくなるロゴ) を描く
//   - はじめるでスライムが物と文字を飲みながら転がり、セレクト画面へ
//   - クレジットでクレジット画面を開く
// ========================================================
class TitleScene : public Engine::IScene {
public:
    // 生成・破棄 (後片付けは不要)
    TitleScene() = default;
    ~TitleScene() override = default;

    // UI 専用のシーン: 3D 描画の準備前 (起動直後の軽量な経路) でも動く
    bool RequiresFullRenderer() const override { return false; }
    // IScene の実装: 初期化・更新・描画
    void Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) override;
    void Update() override;
    void Draw() override;
    void DrawUI() override;

private:
    // クレジット画面を開いているかと、表示中のページ
    bool creditsOpen_ = false;
    int creditsPage_ = 0;
    // ずかんを開いているかと、その状態
    bool collectionOpen_ = false;
    UI::CollectionState collection_;
    // 経過時間・登場の経過時間・出発の経過時間 (負なら出発前)
    float totalTime_ = 0.0f, intro_ = 0.0f, leaving_ = -1.0f;
    // 選んでいる項目 (0: はじめる / 1: ずかん / 2: クレジット / 3: やめる) と、前フレームのマウスの位置
    int menu_ = 0;
    float pointerX_ = 0.0f, pointerY_ = 0.0f;
    bool pointerReady_ = false;
    // 出発のときに物と文字が飲まれた時刻 (負ならまだ飲まれていない)
    std::array<float, 24> eatenAt_{};
    float dt_ = 1.0f / 60.0f;

    // === カメラ ===
    // タイトル用のカメラ (現在の 2D タイトルでは固定の視点として使う)
    Engine::Camera camera_;
    // 初期状態
    DirectX::XMFLOAT3 camStartPos_ = { -1.5f, 0.8f, -1.0f };
    DirectX::XMFLOAT3 camStartTarget_ = { 0.0f, 1.0f, 45.0f };
    float camStartFov_ = DirectX::XMConvertToRadians(38.0f);
    // 現在の画角
    float currentFov_;

    // === レンダリングリソース ===
    Engine::WindowDX* dx_ = nullptr;
    Engine::Renderer* renderer_ = nullptr;

    // 時間管理
    // 前フレームの時刻 (dt の計算用)
    std::chrono::steady_clock::time_point lastTime_;
};

} // namespace Game
