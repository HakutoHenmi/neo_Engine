// UISystem.cpp
// UI コンポーネントの描画とボタンの処理、旧アクションゲームの HUD (体力・缶・ロックオン・ダメージ数字・死亡画面) の実装。
#include "../UI/GameUI.h"
#include "UISystem.h"
#include "../ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include "../Scripts/IScript.h" // ボタンのクリックをスクリプトへ通知するため
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include "../Scenes/GameScene.h" // GameScene を使うため
#include "../Scripts/GameManagerScript.h"
#include "../CanLoadout.h"
#include "PlayerActionSystem.h"
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cstdio>

namespace Game {

void UISystem::Update(entt::registry& /*registry*/, GameContext& /*ctx*/) {
    // ボタンの更新や入力判定はワールド座標が確定するDrawフェーズ (RenderNodeWithRect) で実行するため、ここでは何もしない
}

namespace {
// HUD での缶の色
Engine::Vector4 HudCanColor(CanType type) {
    switch (type) {
    case CanType::Fire: return {0.95f, 0.22f, 0.12f, 1.0f};
    case CanType::Water: return {0.18f, 0.55f, 1.0f, 1.0f};
    case CanType::Thunder: return {1.0f, 0.84f, 0.16f, 1.0f};
    case CanType::Soda: return {0.08f, 0.82f, 0.66f, 1.0f};
    case CanType::Ice: return {0.38f, 0.88f, 1.0f, 1.0f};
    case CanType::Magnet: return {0.78f, 0.32f, 1.0f, 1.0f};
    case CanType::Acid: return {0.55f, 1.0f, 0.16f, 1.0f};
    case CanType::Bubble: return {1.0f, 0.40f, 0.78f, 1.0f};
    default: return {0.42f, 0.48f, 0.58f, 1.0f};
    }
}

// HUD に出す缶の操作の説明
const char* HudCanHint(CanType type) {
    switch (type) {
    case CanType::Fire: return "FIRE  LMB: Flame attack  RMB: Heavy hit";
    case CanType::Water: return "WATER  LMB: Shot  Hold RMB: Liquefy";
    case CanType::Thunder: return "THUNDER  RMB: Decoy warp";
    case CanType::Soda: return "SODA  Hold LMB: Jet boost  RMB: Aim jump";
    case CanType::Ice: return "ICE  LMB: Freeze shot  Hold RMB: Ice slide";
    case CanType::Magnet: return "MAGNET  LMB: Repel pulse  Hold RMB: Pull";
    case CanType::Acid: return "ACID  LMB: Corrode shot  RMB: Acid pool";
    case CanType::Bubble: return "BUBBLE  LMB: Trap shot  RMB: One-hit shield";
    default: return "TAB: Open can menu  MMB: Lock-on";
    }
}
} // namespace

// HUD 用の白テクスチャと缶の模様テクスチャを読み込む
void UISystem::EnsureHudTextures(GameContext& ctx) {
    // 描画が無ければ何もしない
    if (!ctx.renderer) return;
    // 白テクスチャと缶の模様のテクスチャを一度だけ読み込む
    if (whiteTexture_ == 0) {
        whiteTexture_ = ctx.renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    }
    if (canPatternTexture_ == 0) {
        canPatternTexture_ = ctx.renderer->LoadTexture2D("Resources/Textures/ball.png");
    }
}

// テクスチャで矩形を描く
void UISystem::DrawSpriteRect(Engine::Renderer* renderer, Engine::Renderer::TextureHandle texture, float x, float y, float w, float h, const Engine::Vector4& color, int layer, float rotationRad) {
    // テクスチャが無ければ描かない
    if (!renderer || texture == 0) return;
    // 暗い大きめのパネルは、共通の UI の 9 スライスのパネルで描く
    if (texture == whiteTexture_ && w >= 150 && h >= 40 && h < 400 && rotationRad == 0 && color.x+color.y+color.z < .7f) {
        Engine::Renderer::Sprite9SliceDesc panel;
        panel.x=x;panel.y=y;panel.w=w;panel.h=h;
        panel.left=panel.right=panel.top=panel.bottom=8;panel.layer=layer;panel.color=UI::Ink;
        renderer->DrawSprite9Slice(renderer->LoadTexture2D(UI::Canvas::Asset("Grey/Default/button_rectangle_flat.png")),panel);
        return;
    }
    // それ以外は通常のスプライトで描く
    Engine::Renderer::SpriteDesc desc;
    desc.x = x;
    desc.y = y;
    desc.w = w;
    desc.h = h;
    desc.rotationRad = rotationRad;
    desc.color = color;
    desc.layer = layer;
    renderer->DrawSprite(texture, desc);
}

// 中央揃えの文字を描く
void UISystem::DrawCenteredText(Engine::Renderer* renderer, const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color) {
    // 文字の幅を測って中央にそろえる
    if (!renderer) return;
    const float w = renderer->MeasureTextWidth(text, scale, UI::Font);
    renderer->DrawString(text, centerX - w * 0.5f, y, scale, color, UI::Font);
}

// ゲージ (背景・回復できる量・現在の量) を描く
void UISystem::DrawHudBar(Engine::Renderer* renderer, float x, float y, float w, float h, float currentRate, float reserveRate, const Engine::Vector4& fillColor, int layer) {
    // 今の割合と、回復できる分を含めた割合
    const float hpRate = std::clamp(currentRate, 0.0f, 1.0f);
    const float storedRate = std::clamp(reserveRate, hpRate, 1.0f);
    // 枠と暗い地
    DrawSpriteRect(renderer, whiteTexture_, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, {0.18f, 0.78f, 0.98f, 0.85f}, layer);
    DrawSpriteRect(renderer, whiteTexture_, x, y, w, h, {0.03f, 0.04f, 0.07f, 0.94f}, layer + 1);
    // 回復できる分を薄い緑で描く
    if (storedRate > hpRate + 0.01f) {
        DrawSpriteRect(renderer, whiteTexture_, x + w * hpRate, y, w * (storedRate - hpRate), h, {0.78f, 1.0f, 0.40f, 0.42f}, layer + 2);
    }
    // 今の量と、上の光の線
    DrawSpriteRect(renderer, whiteTexture_, x, y, w * hpRate, h, fillColor, layer + 3);
    DrawSpriteRect(renderer, whiteTexture_, x, y, w, 4.0f, {1.0f, 1.0f, 1.0f, 0.22f}, layer + 4);
}

// 缶のアイコン (缶の色の枠と模様、選択中は強調) を描く
void UISystem::DrawHudCanIcon(Engine::Renderer* renderer, CanType type, float centerX, float centerY, float w, float h, bool selected, int layer) {
    // アイコンの左上と、缶の色・枠の色 (選択中は金色)
    const float x = centerX - w * 0.5f;
    const float y = centerY - h * 0.5f;
    const Engine::Vector4 canColor = HudCanColor(type);
    const Engine::Vector4 edge = selected ? Engine::Vector4{1.0f, 0.95f, 0.24f, 1.0f} : Engine::Vector4{0.32f, 0.42f, 0.60f, 0.95f};

    // 枠・地・缶の色・模様・光・ふたを重ねて描く
    DrawSpriteRect(renderer, whiteTexture_, x - 7.0f, y - 7.0f, w + 14.0f, h + 14.0f, edge, layer);
    DrawSpriteRect(renderer, whiteTexture_, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, {0.03f, 0.04f, 0.08f, 1.0f}, layer + 1);
    DrawSpriteRect(renderer, whiteTexture_, x, y, w, h, canColor, layer + 2);
    DrawSpriteRect(renderer, canPatternTexture_, x + 5.0f, y + 18.0f, w - 10.0f, h - 34.0f, {1.0f, 1.0f, 1.0f, 0.26f}, layer + 3);
    DrawSpriteRect(renderer, whiteTexture_, x + 4.0f, y + 8.0f, w - 8.0f, 8.0f, {1.0f, 1.0f, 1.0f, 0.35f}, layer + 4);
    DrawSpriteRect(renderer, whiteTexture_, x + w * 0.34f, y - 7.0f, w * 0.32f, 7.0f, {0.82f, 0.86f, 0.92f, 1.0f}, layer + 5);
    DrawSpriteRect(renderer, whiteTexture_, x + w * 0.42f, y - 13.0f, w * 0.16f, 7.0f, {0.56f, 0.60f, 0.68f, 1.0f}, layer + 6);

    // 中央に缶の種類ごとの印を描く
    const float sx = x + w * 0.5f;
    const float sy = y + h * 0.50f;
    switch (type) {
    // 炎: 3 つの炎の形
    case CanType::Fire:
        DrawSpriteRect(renderer, whiteTexture_, sx - 5.0f, sy - 20.0f, 10.0f, 38.0f, {1.0f, 0.90f, 0.18f, 1.0f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx - 15.0f, sy - 2.0f, 13.0f, 27.0f, {1.0f, 0.50f, 0.06f, 1.0f}, layer + 8, -0.45f);
        DrawSpriteRect(renderer, whiteTexture_, sx + 2.0f, sy - 8.0f, 13.0f, 30.0f, {1.0f, 0.16f, 0.08f, 0.95f}, layer + 8, 0.40f);
        break;
    // 水: 水滴
    case CanType::Water:
        DrawSpriteRect(renderer, whiteTexture_, sx - 10.0f, sy - 18.0f, 20.0f, 38.0f, {0.64f, 0.92f, 1.0f, 1.0f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx - 15.0f, sy + 4.0f, 30.0f, 18.0f, {0.18f, 0.66f, 1.0f, 0.95f}, layer + 8, 0.78f);
        break;
    // 雷: 斜めの稲妻
    case CanType::Thunder:
        DrawSpriteRect(renderer, whiteTexture_, sx - 10.0f, sy - 24.0f, 16.0f, 42.0f, {1.0f, 0.96f, 0.18f, 1.0f}, layer + 7, 0.45f);
        DrawSpriteRect(renderer, whiteTexture_, sx - 3.0f, sy - 4.0f, 16.0f, 42.0f, {1.0f, 0.68f, 0.04f, 1.0f}, layer + 8, 0.45f);
        break;
    // 炭酸: 泡の粒
    case CanType::Soda:
        DrawSpriteRect(renderer, whiteTexture_, sx - 17.0f, sy - 17.0f, 11.0f, 11.0f, {0.82f, 1.0f, 0.94f, 0.95f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx + 6.0f, sy - 11.0f, 9.0f, 9.0f, {0.82f, 1.0f, 0.94f, 0.95f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx - 4.0f, sy + 8.0f, 13.0f, 13.0f, {0.82f, 1.0f, 0.94f, 0.95f}, layer + 7);
        break;
    // 氷: 結晶
    case CanType::Ice:
        DrawSpriteRect(renderer, whiteTexture_, sx - 5.0f, sy - 23.0f, 10.0f, 43.0f, {0.82f, 0.98f, 1.0f, 1.0f}, layer + 7, 0.35f);
        DrawSpriteRect(renderer, whiteTexture_, sx - 14.0f, sy - 2.0f, 9.0f, 27.0f, {0.20f, 0.72f, 1.0f, 1.0f}, layer + 8, -0.55f);
        DrawSpriteRect(renderer, whiteTexture_, sx + 7.0f, sy + 1.0f, 8.0f, 22.0f, {0.55f, 0.92f, 1.0f, 1.0f}, layer + 8, 0.65f);
        break;
    // 磁石: U の字
    case CanType::Magnet:
        DrawSpriteRect(renderer, whiteTexture_, sx - 15.0f, sy - 19.0f, 8.0f, 35.0f, {1.0f, 0.35f, 0.45f, 1.0f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx + 7.0f, sy - 19.0f, 8.0f, 35.0f, {0.38f, 0.58f, 1.0f, 1.0f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, sx - 15.0f, sy + 9.0f, 30.0f, 8.0f, {0.88f, 0.90f, 1.0f, 1.0f}, layer + 8);
        break;
    // 酸: 斜めのしずくとたまり
    case CanType::Acid:
        DrawSpriteRect(renderer, whiteTexture_, sx - 11.0f, sy - 18.0f, 22.0f, 38.0f, {0.70f, 1.0f, 0.10f, 1.0f}, layer + 7, 0.72f);
        DrawSpriteRect(renderer, whiteTexture_, sx - 15.0f, sy + 18.0f, 30.0f, 6.0f, {0.24f, 0.62f, 0.06f, 1.0f}, layer + 8);
        break;
    // 泡: 3 つの泡
    case CanType::Bubble:
        DrawSpriteRect(renderer, canPatternTexture_, sx - 16.0f, sy - 17.0f, 24.0f, 24.0f, {0.92f, 0.78f, 1.0f, 0.95f}, layer + 7);
        DrawSpriteRect(renderer, canPatternTexture_, sx + 5.0f, sy - 5.0f, 17.0f, 17.0f, {0.50f, 0.92f, 1.0f, 0.95f}, layer + 7);
        DrawSpriteRect(renderer, canPatternTexture_, sx - 6.0f, sy + 13.0f, 13.0f, 13.0f, {1.0f, 0.52f, 0.82f, 0.90f}, layer + 7);
        break;
    // 缶なし: 十字
    default:
        DrawSpriteRect(renderer, whiteTexture_, x + w * 0.25f, y + h * 0.50f - 3.0f, w * 0.50f, 6.0f, {0.22f, 0.26f, 0.34f, 1.0f}, layer + 7);
        DrawSpriteRect(renderer, whiteTexture_, x + w * 0.50f - 3.0f, y + h * 0.25f, 6.0f, h * 0.50f, {0.22f, 0.26f, 0.34f, 1.0f}, layer + 7);
        break;
    }
}

// プレイヤーの体力のゲージ (左上) を描く
void UISystem::DrawPlayerHud(entt::registry& /*registry*/, entt::entity /*playerEnt*/, PlayerInputComponent& /*pi*/, HealthComponent& pHealth, GameContext& ctx) {
    auto* renderer = ctx.renderer;
    // パネルの位置と大きさ
    const float x = 34.0f;
    const float y = 28.0f;
    const float panelW = 390.0f;
    const float panelH = 112.0f;
    // 体力の割合と、回復できる分を含めた割合
    const float hpRate = pHealth.Hp() / (pHealth.MaxHp() > 0.0f ? pHealth.MaxHp() : 1.0f);
    const float reserveRate = (pHealth.Hp() + pHealth.recoverableFluid) / (pHealth.MaxHp() > 0.0f ? pHealth.MaxHp() : 1.0f);

    // パネルの枠・地・上の帯・見出し
    DrawSpriteRect(renderer, whiteTexture_, x - 4.0f, y - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.20f, 0.42f, 0.68f, 0.95f}, 180);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.92f}, 181);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, 8.0f, {0.18f, 0.78f, 0.98f, 1.0f}, 182);
    renderer->DrawString("PLAYER", x + 18.0f, y + 18.0f, 0.42f, {0.86f, 0.94f, 1.0f, 1.0f}, UI::Font);

    // 体力の数値とゲージ
    char hpText[48];
    std::snprintf(hpText, sizeof(hpText), "%.0f / %.0f", pHealth.Hp(), pHealth.MaxHp());
    renderer->DrawString(hpText, x + 268.0f, y + 20.0f, 0.34f, {0.80f, 1.0f, 0.84f, 1.0f}, UI::Font);
    DrawHudBar(renderer, x + 20.0f, y + 58.0f, panelW - 40.0f, 24.0f, hpRate, reserveRate, {0.22f, 1.0f, 0.58f, 1.0f}, 183);

    // 回復できる量があれば表示する
    if (pHealth.recoverableFluid > 0.5f) {
        char fluidText[48];
        std::snprintf(fluidText, sizeof(fluidText), "RECOVERABLE +%.0f", pHealth.recoverableFluid);
        renderer->DrawString(fluidText, x + 20.0f, y + 88.0f, 0.26f, {0.78f, 1.0f, 0.40f, 0.92f}, UI::Font);
    }
}

// 装備している缶の一覧と、現在の缶の操作の説明を描く
void UISystem::DrawEquippedCanHud(entt::registry& registry, entt::entity playerEnt, PlayerInputComponent& pi, GameContext& ctx) {
    auto* renderer = ctx.renderer;
    // 画面の右上に置く
    const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
    const float x = viewW - 438.0f;
    const float y = 28.0f;
    const float panelW = 404.0f;
    const float panelH = 184.0f;

    // 今の缶 (行動の缶があればそれ)
    CanType activeCan = pi.selectedCan;
    PlayerActionComponent* action = registry.try_get<PlayerActionComponent>(playerEnt);
    if (action) {
        activeCan = action->currentCan;
    }

    // パネルの枠・地・上の帯・見出し
    DrawSpriteRect(renderer, whiteTexture_, x - 4.0f, y - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.20f, 0.42f, 0.68f, 0.95f}, 180);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, panelH, {0.05f, 0.07f, 0.12f, 0.92f}, 181);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, 8.0f, {1.0f, 0.33f, 0.62f, 1.0f}, 182);
    renderer->DrawString("EQUIPPED CANS", x + 18.0f, y + 18.0f, 0.36f, {0.86f, 0.94f, 1.0f, 1.0f}, UI::Font);

    // 装備している缶を枠に並べる (今の缶は金色の枠)
    const auto& cans = CanLoadout::GetEquipped();
    const float slotY = y + 60.0f;
    for (int i = 0; i < CanLoadout::kMaxEquippedCans; ++i) {
        const float slotX = x + 20.0f + static_cast<float>(i) * 92.0f;
        const CanType can = cans[i];
        const bool selected = can != CanType::None && can == activeCan;
        const Engine::Vector4 slotEdge = selected ? Engine::Vector4{1.0f, 0.95f, 0.24f, 1.0f} : Engine::Vector4{0.22f, 0.32f, 0.48f, 0.95f};

        DrawSpriteRect(renderer, whiteTexture_, slotX - 4.0f, slotY - 4.0f, 74.0f, 76.0f, slotEdge, 183);
        DrawSpriteRect(renderer, whiteTexture_, slotX, slotY, 66.0f, 68.0f, {0.08f, 0.10f, 0.16f, 0.95f}, 184);
        DrawHudCanIcon(renderer, can, slotX + 33.0f, slotY + 34.0f, 28.0f, 48.0f, selected, 185);
        DrawCenteredText(renderer, std::to_string(i + 1), slotX + 33.0f, slotY + 55.0f, 0.22f, {0.86f, 0.92f, 1.0f, 0.72f});
    }

    // 今の缶の操作の説明
    DrawSpriteRect(renderer, whiteTexture_, x + 20.0f, y + 132.0f, panelW - 40.0f, 22.0f, {0.11f, 0.16f, 0.24f, 0.95f}, 183);
    DrawCenteredText(renderer, HudCanHint(activeCan), x + panelW * 0.5f, y + 135.0f, 0.24f, {0.86f, 0.94f, 1.0f, 1.0f});

    // 缶ごとの状態 (滑走中・待ち時間・シールドの残り)
    std::string stateText = "READY";
    if (action) {
        char cooldown[48];
        if (activeCan == CanType::Ice) {
            stateText = action->iceSlideApplied ? "ICE SLIDE ACTIVE" : "SLIDE READY";
        } else if (activeCan == CanType::Magnet) {
            stateText = "MAGNETIC FIELD READY";
        } else if (activeCan == CanType::Acid && action->canSecondaryCooldown > 0.0f) {
            std::snprintf(cooldown, sizeof(cooldown), "ACID POOL %.1fs", action->canSecondaryCooldown);
            stateText = cooldown;
        } else if (activeCan == CanType::Bubble) {
            if (const auto* shield = registry.try_get<BubbleShieldComponent>(playerEnt)) {
                std::snprintf(cooldown, sizeof(cooldown), "SHIELD %d HIT / %.1fs", shield->charges, shield->timer);
                stateText = cooldown;
            } else if (action->canSecondaryCooldown > 0.0f) {
                std::snprintf(cooldown, sizeof(cooldown), "SHIELD COOLDOWN %.1fs", action->canSecondaryCooldown);
                stateText = cooldown;
            }
        }
    }
    // 状態を缶の色で表示する
    DrawCenteredText(renderer, stateText, x + panelW * 0.5f, y + 160.0f, 0.22f, HudCanColor(activeCan));
}

// ロックオン中の敵の名前と体力のゲージを描く
void UISystem::DrawLockedEnemyHud(entt::registry& registry, PlayerInputComponent& pi, GameContext& ctx) {
    // ロックオンしていないか、体力が無い相手なら描かない
    if (pi.lockedEnemy == entt::null || !registry.valid(pi.lockedEnemy) || !registry.all_of<HealthComponent>(pi.lockedEnemy)) return;

    const auto& enemyHealth = registry.get<HealthComponent>(pi.lockedEnemy);
    if (enemyHealth.IsDead() || enemyHealth.MaxHp() <= 0.0f) return;

    auto* renderer = ctx.renderer;
    // 画面の上の中央に置く
    const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
    const float panelW = 560.0f;
    const float panelH = 78.0f;
    const float x = viewW * 0.5f - panelW * 0.5f;
    const float y = 28.0f;
    const float hpRate = enemyHealth.Hp() / enemyHealth.MaxHp();

    // 名前 (無ければ ENEMY)
    std::string enemyName = "ENEMY";
    if (auto* name = registry.try_get<NameComponent>(pi.lockedEnemy)) {
        if (!name->name.empty()) enemyName = name->name;
    }

    // パネル・見出し・名前・体力のゲージ
    DrawSpriteRect(renderer, whiteTexture_, x - 4.0f, y - 4.0f, panelW + 8.0f, panelH + 8.0f, {0.68f, 0.24f, 0.32f, 0.95f}, 190);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, panelH, {0.06f, 0.05f, 0.08f, 0.92f}, 191);
    DrawSpriteRect(renderer, whiteTexture_, x, y, panelW, 8.0f, {1.0f, 0.33f, 0.42f, 1.0f}, 192);
    renderer->DrawString("TARGET", x + 18.0f, y + 18.0f, 0.32f, {1.0f, 0.74f, 0.78f, 1.0f}, UI::Font);
    DrawCenteredText(renderer, enemyName, x + panelW * 0.5f, y + 16.0f, 0.40f, {1.0f, 0.96f, 0.82f, 1.0f});
    DrawHudBar(renderer, x + 42.0f, y + 48.0f, panelW - 84.0f, 18.0f, hpRate, hpRate, {1.0f, 0.22f, 0.30f, 1.0f}, 193);
}

// 親子関係をたどって、アンカー・ピボット・位置から UI 部品の画面上の矩形を求める
UISystem::WorldRect UISystem::CalculateWorldRect(entt::entity entity, const entt::registry& registry, float screenW, float screenH) {
    // RectTransform が無ければ空の矩形
    if (!registry.all_of<RectTransformComponent>(entity)) return {0, 0, 0, 0};

    // 親を辿ってパスを構築
    std::vector<entt::entity> path;
    entt::entity current = entity;
    while (registry.valid(current)) {
        path.push_back(current);
        entt::entity parent = entt::null;
        
        if (registry.all_of<HierarchyComponent>(current)) {
            entt::entity parentId = registry.get<HierarchyComponent>(current).parentId;
            if (parentId != entt::null) {
                parent = parentId;
            }
        }
        current = parent;
    }
    std::reverse(path.begin(), path.end());

    // 画面全体から始め、根元から順に子の矩形を求める
    WorldRect currentRect = { 0, 0, screenW, screenH };

    for (entt::entity pObj : path) {
        if (!registry.all_of<RectTransformComponent>(pObj)) continue;
        auto& rect = registry.get<RectTransformComponent>(pObj);
        
        float worldW = rect.size.x;
        float worldH = rect.size.y;
        float anchorX = currentRect.x + currentRect.w * rect.anchor.x;
        float anchorY = currentRect.y + currentRect.h * rect.anchor.y;
        float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
        float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
        
        currentRect = { worldX, worldY, worldW, worldH };
    }
    return currentRect;
}

// Canvas 以下の UI 部品を親から順に描く (親の矩形を基準に子の矩形を求める)
void UISystem::Draw(entt::registry& registry, GameContext& ctx) {
    // テクスチャを用意する
    EnsureHudTextures(ctx);

    // 求めた矩形のキャッシュ
    std::unordered_map<uint32_t, WorldRect> cache;

    // --- 既存のUI（Canvasベース）の描画 ---
    // 親が parentId の部品を描き、その子を再帰で描く関数
    auto renderRecursive = [&](auto self, entt::entity parentId, WorldRect parentRect) -> void {
        auto view = registry.view<RectTransformComponent>();
        for (auto e : view) {
            entt::entity currentParentId = entt::null;
            if (registry.all_of<HierarchyComponent>(e)) {
                currentParentId = registry.get<HierarchyComponent>(e).parentId;
            }

            if (currentParentId == parentId) {
                auto& rect = view.get<RectTransformComponent>(e);
                // 無効な部品は、その子も含めて描かない
                if (!rect.enabled) continue;
                // 親の矩形とアンカー・ピボット・位置から自分の矩形を求める
                float worldW = rect.size.x;
                float worldH = rect.size.y;
                float anchorX = parentRect.x + parentRect.w * rect.anchor.x;
                float anchorY = parentRect.y + parentRect.h * rect.anchor.y;
                float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
                float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
                
                WorldRect selfRect = { worldX, worldY, worldW, worldH };
                uint32_t eId = static_cast<uint32_t>(e);
                cache[eId] = selfRect;

                // 自分を描いてから子を描く
                RenderNodeWithRect(e, registry, selfRect, ctx);
                self(self, e, selfRect);
            }
        }
    };

	// 画面全体 (ビューの大きさ) を根元にする
	float vw = ctx.viewportSize.x > 0 ? ctx.viewportSize.x : (float)Engine::WindowDX::kW;
	float vh = ctx.viewportSize.y > 0 ? ctx.viewportSize.y : (float)Engine::WindowDX::kH;
	WorldRect screen = { 0.0f, 0.0f, vw, vh };
	renderRecursive(renderRecursive, entt::null, screen);

	// RectTransform を持たず、Transform と UIText を持つエンティティの簡易な 2D 描画
	auto viewRawText = registry.view<TransformComponent, UITextComponent>();
	viewRawText.each([&](entt::entity e, TransformComponent& transform, UITextComponent& text) {
		if (registry.all_of<RectTransformComponent>(e)) return; // RectTransformComponent を持つものは除く
		if (text.enabled) {
			// Transformの X/Y をスクリーンのピクセル座標として扱う (Zは無視)
			DrawTextW(e, registry, text, transform.translate.x, transform.translate.y, 0.0f, 0.0f, ctx.renderer);
		}
	});

    // ゲーム中の HUD を描く
    DrawGameplayHud(registry, ctx);
}

// ワールド空間の UI (体力ゲージ) を描く処理
// 3D 空間の UI と HUD の描画
void UISystem::DrawUI(entt::registry& registry, GameContext& ctx) {
    // 現在は処理なし
    (void)registry;
    (void)ctx;
}

// ゲーム中の HUD: 敵の頭上の体力ゲージ・ダメージ数字・ロックオンの照準・プレイヤーの HUD・死亡画面
void UISystem::DrawGameplayHud(entt::registry& registry, GameContext& ctx) {
    // PLAY 中で、カメラと描画があるときだけ
    if (!ctx.isPlaying) return;
    if (!ctx.camera) return;

    if (!ctx.renderer) return;
    EnsureHudTextures(ctx);
    if (whiteTexture_ == 0) return;

    // 以下の3D空間UI（HPバーなど）は GameScene コンテキストが必要
    if (!ctx.scene) return;

    // 体力を持つエンティティの頭上のゲージ
    auto viewHealth = registry.view<HealthComponent>();
    for (auto e : viewHealth) {
        auto& hc = viewHealth.get<HealthComponent>(e);
        
        const WorldSpaceUIComponent* uiComp = registry.try_get<WorldSpaceUIComponent>(e);

        // 1. HPバーの描画
        // 生きていて、表示の設定が無効でなければ
        if (hc.enabled && !hc.IsDead()) {
            bool shouldShow = (!uiComp || uiComp->showHealthBar);

            if (shouldShow) {
                float sx, sy;
                
                // 親子関係を考慮しワールド行列から正確な位置を取得
                Engine::Matrix4x4 wm = ctx.scene->GetWorldMatrix(static_cast<int>(e));
                DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&wm));
                
                // 行列から情報を抽出
                DirectX::XMVECTOR scale, rot, trans;
                DirectX::XMMatrixDecompose(&scale, &rot, &trans, worldMat);
                DirectX::XMFLOAT3 basePos;
                DirectX::XMStoreFloat3(&basePos, trans);
                
                float barW = 100.0f; // デフォルトを大きく
                float barH = 12.0f;  // デフォルトを大きく
                DirectX::XMFLOAT3 pos = basePos;

                // 頭上の高さを動的に計算（Colliderの大きさに合わせる）
                float heightOffset = 1.0f;
                
                // コライダーの上端の位置を求める
                if (registry.all_of<BoxColliderComponent>(e)) {
                    auto& bc = registry.get<BoxColliderComponent>(e);
                    // コライダーのローカル中心位置をワールド空間に変換して、実際の表示位置を合わせる
                    DirectX::XMVECTOR localCenter = DirectX::XMVectorSet(bc.center.x, bc.center.y + bc.size.y * 0.5f, bc.center.z, 1.0f);
                    DirectX::XMVECTOR worldCenter = DirectX::XMVector3Transform(localCenter, worldMat);
                    DirectX::XMStoreFloat3(&pos, worldCenter);
                    heightOffset = 0.5f; // すでにコライダーの上端基準なので、少し上にずらすだけ
                // コライダーが無ければ大きさから高さを決める
                } else if (registry.all_of<TransformComponent>(e)) {
                    heightOffset = registry.get<TransformComponent>(e).scale.y + 0.5f;
                }

                // WorldSpaceUI の設定でずれと大きさを変える
                if (uiComp) {
                    pos.x += uiComp->offset.x;
                    pos.y += heightOffset + uiComp->offset.y; 
                    pos.z += uiComp->offset.z;
                    if (uiComp->barWidth > 0.0f) barW = uiComp->barWidth;
                    if (uiComp->barHeight > 0.0f) barH = uiComp->barHeight;
                } else {
                    pos.y += heightOffset;
                }
                
                // プレイヤーには専用のHUDが左上にあるため、3D空間上の緑のHPバーは非表示にする
                if (registry.all_of<TagComponent>(e) && registry.get<TagComponent>(e).tag == TagType::Player) {
                    shouldShow = false;
                }
                if (!shouldShow) continue;

                // 最新のViewport（画像描画位置）を使用して投影
                if (WorldToScreenWithView(pos, *ctx.camera, ctx.viewportOffset, ctx.viewportSize, sx, sy)) {
                    float hpRate = hc.Hp() / (hc.MaxHp() > 0 ? hc.MaxHp() : 1.0f);
                    float localX = sx - ctx.viewportOffset.x;
                    float localY = sy - ctx.viewportOffset.y;
                    DrawHudBar(ctx.renderer, localX - barW * 0.5f, localY - barH * 0.5f, barW, barH, hpRate, hpRate, {1.0f, 0.22f, 0.30f, 1.0f}, 96);
                }
            }
        }
    }
    // 2. ダメージ数字の描画
    // ダメージ数字: 上へ昇りながら、最後の 0.5 秒で消える
    auto dmgView = registry.view<DamageNumberComponent>();
    for (auto e : dmgView) {
        auto& dnc = dmgView.get<DamageNumberComponent>(e);
        float sx, sy;
        // 上に昇るアニメーション
        float progress = 1.0f - (dnc.lifetime / dnc.maxLifetime);
        DirectX::XMFLOAT3 pos = dnc.startPos;
        pos.y += progress * 2.5f; // 最大2.5m上昇
        if (WorldToScreenWithView(pos, *ctx.camera, ctx.viewportOffset, ctx.viewportSize, sx, sy)) {
            char text[32];
            snprintf(text, sizeof(text), "%.0f", dnc.damage);
            
            // 少し上に浮き上がりながらフェードアウトする効果
            float alpha = (dnc.lifetime > 0.5f) ? 1.0f : (dnc.lifetime / 0.5f);
            
			// Engine::Renderer の高品質なテキスト描画機能を使う
			if (ctx.renderer) {
				float scale = 2.0f; // 大きめのフォント
				float localX = sx - ctx.viewportOffset.x;
				float localY = sy - ctx.viewportOffset.y;
				
				// ドロップシャドウ（黒縁）
				ctx.renderer->DrawString(text, localX + 2.0f, localY + 2.0f, scale, {0.0f, 0.0f, 0.0f, alpha}, UI::Font);
				
				// メインテキスト
				ctx.renderer->DrawString(text, localX, localY, scale, {dnc.color.x, dnc.color.y, dnc.color.z, alpha}, UI::Font);
			}
        }
    }

    // 3. ロックオンカーソルとプレイヤーHUDの描画
    // プレイヤーごとに、ロックオンの照準と HUD を描く
    auto playerView = registry.view<PlayerInputComponent, HealthComponent>();
    playerView.each([&](entt::entity playerEnt, PlayerInputComponent& pi, HealthComponent& pHealth) {

        // --- ロックオンカーソル ---
        if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy)) {
            if (registry.all_of<TransformComponent>(pi.lockedEnemy)) {
                auto& eTc = registry.get<TransformComponent>(pi.lockedEnemy);
                DirectX::XMFLOAT3 pos = eTc.translate;
                pos.y += 1.0f; // 敵の中央付近

                float sx, sy;
                if (WorldToScreenWithView(pos, *ctx.camera, ctx.viewportOffset, ctx.viewportSize, sx, sy)) {
                    float localX = sx - ctx.viewportOffset.x;
                    float localY = sy - ctx.viewportOffset.y;
                    
                    // カーソルの描画（Spriteで代用）
                    float size = 20.0f;
                    float thick = 2.0f;
                    
                    // 横線
                    Engine::Renderer::SpriteDesc hLine;
                    hLine.x = localX - size; hLine.y = localY - thick * 0.5f;
                    hLine.w = size * 2.0f; hLine.h = thick;
                    hLine.color = {1.0f, 100.0f/255.0f, 100.0f/255.0f, 1.0f};
                    hLine.layer = 110;
                    ctx.renderer->DrawSprite(whiteTexture_, hLine);
                    
                    // 縦線
                    Engine::Renderer::SpriteDesc vLine;
                    vLine.x = localX - thick * 0.5f; vLine.y = localY - size;
                    vLine.w = thick; vLine.h = size * 2.0f;
                    vLine.color = {1.0f, 100.0f/255.0f, 100.0f/255.0f, 1.0f};
                    vLine.layer = 110;
                    ctx.renderer->DrawSprite(whiteTexture_, vLine);
                }
            }
        }

        // ロックオン中の敵・プレイヤー・装備の HUD
        DrawLockedEnemyHud(registry, pi, ctx);
        DrawPlayerHud(registry, playerEnt, pi, pHealth, ctx);
        DrawEquippedCanHud(registry, playerEnt, pi, ctx);
		// コンボが 2 以上ならコンボの数と残り時間のバーを出す
		if (ctx.combatFlow && ctx.combatFlow->Combo() >= 2 && !pHealth.IsDead()) {
			const auto& flow = *ctx.combatFlow;
			const float viewW = ctx.viewportSize.x > 0.0f ? ctx.viewportSize.x : static_cast<float>(Engine::WindowDX::kW);
			const float x = viewW * 0.5f - 110.0f;
			DrawSpriteRect(ctx.renderer, whiteTexture_, x, 95.0f, 220.0f, 56.0f,
				{0.035f, 0.12f, 0.18f, 0.78f}, 120);
			const std::string label = "FLOW COMBO x" + std::to_string(flow.Combo());
			DrawCenteredText(ctx.renderer, label, viewW * 0.5f, 103.0f, 0.65f,
				{0.48f, 0.96f, 1.0f, 1.0f});
			DrawSpriteRect(ctx.renderer, whiteTexture_, x + 12.0f, 137.0f,
				196.0f * (std::min)(1.0f, flow.Remaining() / 1.8f), 4.0f,
				{0.48f, 0.96f, 1.0f, 1.0f}, 121);
		}

        // --- 4. ゲームオーバー（YOU DIED）画面 ---
        // 死亡したら画面を暗くして、YOU DIED を表示する
        if (pHealth.IsDead()) {
            if (ctx.renderer) ctx.renderer->SetPostEffect("Smoothing");
            deathTimer_ += ctx.dt;

            // 画面を徐々に暗くする
            float darkAlpha = std::clamp(deathTimer_ * 120.0f / 255.0f, 0.0f, 180.0f / 255.0f);
            
            Engine::Renderer::SpriteDesc darkScreen;
            darkScreen.x = 0; darkScreen.y = 0;
            darkScreen.w = ctx.viewportSize.x > 0 ? ctx.viewportSize.x : Engine::WindowDX::kW;
            darkScreen.h = ctx.viewportSize.y > 0 ? ctx.viewportSize.y : Engine::WindowDX::kH;
            darkScreen.color = {0.0f, 0.0f, 0.0f, darkAlpha};
            darkScreen.layer = 500; // 最前面
            ctx.renderer->DrawSprite(whiteTexture_, darkScreen);

            // 文字・大きさ・色は GameManagerScript の設定を使う (無ければ既定)
            auto* gm = GameManagerScript::GetInstance();
            std::string defeatStr = gm ? gm->defeatText : "YOU DIED";
            float scale = gm ? gm->textScale : 6.0f;
            float rColor[4] = {1.0f, 0.1f, 0.1f, 1.0f};
            if (gm) { rColor[0]=gm->defeatColor[0]; rColor[1]=gm->defeatColor[1]; rColor[2]=gm->defeatColor[2]; rColor[3]=gm->defeatColor[3]; }

            float centerX = darkScreen.w * 0.5f;
            float centerY = darkScreen.h * 0.5f;
            
            float textAlpha = std::min(1.0f, deathTimer_);

            // 影と本文を描く
            if (ctx.renderer) {
                float defeatWidth = ctx.renderer->MeasureTextWidth(defeatStr, scale, UI::Font);
                float sx = centerX - defeatWidth * 0.5f; 
                float sy = centerY - 180.0f;
                ctx.renderer->DrawString(defeatStr, sx + 5.0f, sy + 5.0f, scale, {0.0f, 0.0f, 0.0f, textAlpha}, UI::Font);
                ctx.renderer->DrawString(defeatStr, sx, sy, scale, {rColor[0], rColor[1], rColor[2], rColor[3] * textAlpha}, UI::Font);
            }

            // 1.5秒後に完全にGameOverシーンへ遷移
            if (deathTimer_ >= 1.5f) {
                Engine::SceneManager::GetInstance()->RequestChange("GameOver");
            }
        }
    });
}

// ワールド座標を画面座標へ変換する (画面全体)
bool UISystem::WorldToScreen(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, float& screenX, float& screenY) {
    return WorldToScreenWithView(worldPos, camera, {0, 0}, {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH}, screenX, screenY);
}

// ワールド座標を、指定のビュー (位置と大きさ) の画面座標へ変換する。カメラの後ろなら false
bool UISystem::WorldToScreenWithView(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, const DirectX::XMFLOAT2& viewOffset, const DirectX::XMFLOAT2& viewSize, float& screenX, float& screenY) {
    // ワールド座標
    DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&worldPos);
    
    // DirectXの標準関数を使用して投影
    DirectX::XMMATRIX view = camera.View();
    DirectX::XMMATRIX proj = camera.Proj();
    DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();

    // ビューポートサイズが0の場合、投影計算に失敗するためガード
    float vw = std::max(1.0f, viewSize.x);
    float vh = std::max(1.0f, viewSize.y);
    // ビューの大きさで画面座標に投影する
    DirectX::XMVECTOR screenPos = DirectX::XMVector3Project(p, 0, 0, vw, vh, 0.0f, 1.0f, proj, view, world);
    
    DirectX::XMFLOAT3 sp;
    DirectX::XMStoreFloat3(&sp, screenPos);

    // デバッグ投影結果の妥当性チェック
    // ビュー射影で深度を求め、範囲外 (カメラの後ろなど) なら false
    DirectX::XMMATRIX vp = view * proj;
    DirectX::XMVECTOR clipPos = DirectX::XMVector3TransformCoord(p, vp);
    float cz = DirectX::XMVectorGetZ(clipPos);
    if (cz < 0.0f || cz > 1.0f) return false;

    // ビューの位置を足す
    screenX = viewOffset.x + sp.x;
    screenY = viewOffset.y + sp.y;
    
    return true;
}

// PLAY 開始時: 死亡演出の時間を戻す
void UISystem::Reset(entt::registry& /*registry*/) {
    deathTimer_ = 0.0f;
}

// UI 部品 1 つを描く (ボタンの処理 → 画像 → 文字)
void UISystem::RenderNodeWithRect(entt::entity entity, entt::registry& registry, const WorldRect& wr, GameContext& ctx) {
    // ボタンの更新
    // ボタンなら入力を処理する
    if (registry.all_of<UIButtonComponent>(entity)) {
        auto& btn = registry.get<UIButtonComponent>(entity);
        ProcessButton(entity, registry, btn, wr.x, wr.y, wr.w, wr.h, ctx);
    }

    // ボタンの状態に応じた色を決定
    DirectX::XMFLOAT4 buttonColor = { 1, 1, 1, 1 };
    if (registry.all_of<UIButtonComponent>(entity)) {
        auto& btn = registry.get<UIButtonComponent>(entity);
        if (btn.isPressed) buttonColor = btn.pressedColor;
        else if (btn.isHovered) buttonColor = btn.hoverColor;
        else buttonColor = btn.normalColor;
    }

    // 画像の描画
    if (registry.all_of<UIImageComponent>(entity)) {
        // 無効な画像は描かない
        auto& img = registry.get<UIImageComponent>(entity);
        if (img.enabled) {
            // ボタンなら水色の枠を描く
            if (registry.all_of<UIButtonComponent>(entity)) {
                Engine::Renderer::SpriteDesc border;
                border.x = wr.x - 2.0f;
                border.y = wr.y - 2.0f;
                border.w = wr.w + 4.0f;
                border.h = wr.h + 4.0f;
                border.color = { 0.18f, 0.78f, 0.98f, 0.88f };
                border.layer = img.layer; // レイヤーを引き継ぐ
                ctx.renderer->DrawSprite(whiteTexture_, border);
            }

            // 画像の色にボタンの色を掛ける
            DirectX::XMFLOAT4 finalColor = { img.color.x * buttonColor.x, img.color.y * buttonColor.y, img.color.z * buttonColor.z, img.color.w * buttonColor.w };
            // 9 スライスか通常のスプライトで描く
            if (img.is9Slice) {
                Engine::Renderer::Sprite9SliceDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.left = img.borderLeft; s.right = img.borderRight; s.top = img.borderTop; s.bottom = img.borderBottom;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                s.layer = img.layer; // レイヤーを設定する
                // 注意: 9 スライスは中で複数の DrawSprite に分かれるため、レイヤーは個々の SpriteDesc に設定する必要がある
                // DrawSprite9Slice の中で作られる SpriteDesc にはレイヤーが引き継がれないので、
                // 通常の描画に切り替えるか、Renderer 側で対応する
                ctx.renderer->DrawSprite9Slice(img.textureHandle, s);
            } else {
                Engine::Renderer::SpriteDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                s.layer = img.layer; // レイヤーを設定する
                ctx.renderer->DrawSprite(img.textureHandle, s);
            }
        }
    }

    // テキストの描画
    if (registry.all_of<UITextComponent>(entity)) {
        // 無効な文字は描かない
        auto& text = registry.get<UITextComponent>(entity);
        if (text.enabled) {
            DrawTextW(entity, registry, text, wr.x, wr.y, wr.w, wr.h, ctx.renderer);
        }
    }
}

// UI の文字を描く (矩形の中央揃えと影付き)
void UISystem::DrawTextW(entt::entity /*entity*/, entt::registry& /*registry*/, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer) {
	// 描画が無い・文字が無い・透明なら描かない
	if (!renderer || text.text.empty() || text.color.w <= 0.01f) return;

	// フォントレンダラーの初期化サイズ (Renderer 内で 64.0f) を基準にスケール
	float fontScale = text.fontSize / 64.0f;

	// 文字の幅と高さを測る
	float tw = renderer->MeasureTextWidth(text.text, fontScale, text.fontPath);
	float th = renderer->GetTextLineHeight(fontScale, text.fontPath);

	// 中央揃え (worldW/worldHが0の場合は左上揃え)
	float px = worldX;
	float py = worldY;
	if (worldW > 0.0f) px += (worldW - tw) * 0.5f;
	if (worldH > 0.0f) py += (worldH - th) * 0.5f;

	// ドロップシャドウを追加 (見やすさ向上)
	// 影と本文を描く
	Engine::Vector4 shadowColor = { 0.0f, 0.0f, 0.0f, text.color.w };
	renderer->DrawString(text.text, px + 2.0f, py + 2.0f, fontScale, shadowColor, text.fontPath);

	Engine::Vector4 colorVec = { text.color.x, text.color.y, text.color.z, text.color.w };
	renderer->DrawString(text.text, px, py, fontScale, colorVec, text.fontPath);
}

// ボタンの処理: マウスが矩形 (当たりの大きさとずれを反映) に入っているかと、クリックをスクリプトへ通知する
void UISystem::ProcessButton(entt::entity entity, entt::registry& registry, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx) {
    // 入力が無ければ何もしない
    if (!ctx.input) return;

    float mx, my;
    // エディタの Game ビューなら上書きの座標を使う
    if (ctx.useOverrideMouse) {
        mx = ctx.overrideMouseX;
        my = ctx.overrideMouseY;
    } else {
        float fmx, fmy;
        ctx.input->GetMousePos(fmx, fmy);
        
        // ビューの位置を引いて、内部の解像度 (1920x1080) に直す
        float rx = fmx - ctx.viewportOffset.x;
        float ry = fmy - ctx.viewportOffset.y;
        
        if (ctx.viewportSize.x > 0 && ctx.viewportSize.y > 0) {
            mx = rx * (float)Engine::WindowDX::kW / ctx.viewportSize.x;
            my = ry * (float)Engine::WindowDX::kH / ctx.viewportSize.y;
        } else {
            mx = rx;
            my = ry;
        }
    }

    // hitboxパラメータを適用した実際の判定矩形を計算
    float hw = worldW * btn.hitboxScale.x;
    float hh = worldH * btn.hitboxScale.y;
    // ビジュアルの中央を基準にスケールとオフセットを適用
    float cx = worldX + worldW * 0.5f + btn.hitboxOffset.x;
    float cy = worldY + worldH * 0.5f + btn.hitboxOffset.y;
    float hx = cx - hw * 0.5f;
    float hy = cy - hh * 0.5f;

    // 矩形内判定
    bool hovered = (mx >= hx && mx <= hx + hw &&
                    my >= hy && my <= hy + hh);

    // 乗っているか・押しているか
    btn.isHovered = hovered;
    btn.isPressed = hovered && ctx.input->IsMouseDown(0); // 左ボタン

    // クリックしたらスクリプトへ通知する
    if (hovered && ctx.input->IsMouseTrigger(0)) {
        // クリック時: スクリプト側へ通知
        if (registry.all_of<ScriptComponent>(entity)) {
            auto& sc = registry.get<ScriptComponent>(entity);
            if (sc.enabled) {
                for (auto& entry : sc.scripts) {
                    if (entry.instance) {
                        // TODO: OnClick は SceneObject ではなく entt::entity を受け取るようにする
                        // entry.instance->OnClick(entity, ctx.scene, btn.onClickCallback);
                    }
                }
            }
        }
    }
}

} // namespace Game
