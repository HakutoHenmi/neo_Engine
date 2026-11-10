// UISystem.cpp
// UI コンポーネント (RectTransform・UIImage・UIText・UIButton) の描画とボタンの処理の実装。
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
#include <unordered_map>
#include <set>
#include <algorithm>
#include <cstdio>

namespace Game {

void UISystem::Update(entt::registry& /*registry*/, GameContext& /*ctx*/) {
    // ボタンの更新や入力判定はワールド座標が確定するDrawフェーズ (RenderNodeWithRect) で実行するため、ここでは何もしない
}

// 白テクスチャを一度だけ読み込む (ボタンの枠の描画に使う)
void UISystem::EnsureWhiteTexture(GameContext& ctx) {
    if (!ctx.renderer || whiteTexture_ != 0) return;
    whiteTexture_ = ctx.renderer->LoadTexture2D("Resources/Textures/white1x1.png");
}

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
    EnsureWhiteTexture(ctx);

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
}

// ワールド空間の UI (体力ゲージ) を描く処理
// 3D 空間の UI と HUD の描画
void UISystem::DrawUI(entt::registry& registry, GameContext& ctx) {
    // 現在は処理なし
    (void)registry;
    (void)ctx;
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

// PLAY 開始時: 何もしない
void UISystem::Reset(entt::registry& /*registry*/) {}

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
