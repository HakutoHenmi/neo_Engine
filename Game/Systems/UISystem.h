// UISystem.h
// UI コンポーネント (RectTransform・UIImage・UIText・UIButton) の描画とクリック処理、
// 旧アクションゲームの HUD (体力・缶・ロックオン中の敵)。
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
    // HUD の描画の補助 (テクスチャの準備・矩形・中央揃えの文字・ゲージ・缶のアイコン・各 HUD)
    void EnsureHudTextures(GameContext& ctx);
    void DrawGameplayHud(entt::registry& registry, GameContext& ctx);
    void DrawSpriteRect(Engine::Renderer* renderer, Engine::Renderer::TextureHandle texture, float x, float y, float w, float h, const Engine::Vector4& color, int layer, float rotationRad = 0.0f);
    void DrawCenteredText(Engine::Renderer* renderer, const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color);
    void DrawHudBar(Engine::Renderer* renderer, float x, float y, float w, float h, float currentRate, float reserveRate, const Engine::Vector4& fillColor, int layer);
    void DrawHudCanIcon(Engine::Renderer* renderer, CanType type, float centerX, float centerY, float w, float h, bool selected, int layer);
    void DrawPlayerHud(entt::registry& registry, entt::entity playerEnt, PlayerInputComponent& pi, HealthComponent& pHealth, GameContext& ctx);
    void DrawEquippedCanHud(entt::registry& registry, entt::entity playerEnt, PlayerInputComponent& pi, GameContext& ctx);
    void DrawLockedEnemyHud(entt::registry& registry, PlayerInputComponent& pi, GameContext& ctx);
    // UI 部品の描画と、文字・ボタンの処理
    void RenderNodeWithRect(entt::entity entity, entt::registry& registry, const WorldRect& wr, GameContext& ctx);
    void DrawTextW(entt::entity entity, entt::registry& registry, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer);
    void ProcessButton(entt::entity entity, entt::registry& registry, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx);
    
    // 白テクスチャ・缶の模様テクスチャ・死亡演出の時間
    Engine::Renderer::TextureHandle whiteTexture_ = 0;
    Engine::Renderer::TextureHandle canPatternTexture_ = 0;
    float deathTimer_ = 0.0f;
};

} // namespace Game
