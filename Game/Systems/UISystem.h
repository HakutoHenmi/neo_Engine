// UISystem.h
// UI コンポーネント (RectTransform・UIImage・UIText・UIButton) の描画とクリック処理。
#pragma once
#include "ISystem.h"
#include <vector>
#include <string>
#include "../../externals/entt/entt.hpp"
#include "../ObjectTypes.h"

namespace Game {

// UI システム
class UISystem : public ISystem {
public:
    // UI 部品の画面上の矩形
    struct WorldRect { float x, y, w, h; };

    // 更新 (ボタンの状態) / 描画 / ImGui / リセット
    void Update(entt::registry& registry, GameContext& ctx) override;
    void Draw(entt::registry& registry, GameContext& ctx) override;
    void DrawUI(entt::registry& registry, GameContext& ctx) override;
    void Reset(entt::registry& registry) override;

    // 親子関係とアンカー・ピボットから UI 部品の画面上の矩形を求める
    static WorldRect CalculateWorldRect(entt::entity entity, const entt::registry& registry, float screenW, float screenH);

    // 3D のワールド座標から画面座標へ変換する (ビューポートを考慮する版もある)
    static bool WorldToScreen(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, float& screenX, float& screenY);
    static bool WorldToScreenWithView(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, const DirectX::XMFLOAT2& viewOffset, const DirectX::XMFLOAT2& viewSize, float& screenX, float& screenY);

private:
    // ボタンの枠に使う白テクスチャを用意する
    void EnsureWhiteTexture(GameContext& ctx);
    // UI 部品の描画と、文字・ボタンの処理
    void RenderNodeWithRect(entt::entity entity, entt::registry& registry, const WorldRect& wr, GameContext& ctx);
    void DrawTextW(entt::entity entity, entt::registry& registry, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer);
    void ProcessButton(entt::entity entity, entt::registry& registry, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx);
    
    // 白テクスチャ
    Engine::Renderer::TextureHandle whiteTexture_ = 0;
};

} // namespace Game
