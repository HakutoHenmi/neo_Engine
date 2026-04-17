#include "UISystem.h"
#include "../ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../Scripts/IScript.h" // ★追加
#include "../../Engine/WindowDX.h"
#include "../../externals/imgui/imgui.h"
#include "../Scenes/GameScene.h" // ★追加
#include <unordered_map>
#include <set>
#include <algorithm>

namespace Game {

void UISystem::Update(entt::registry& /*registry*/, GameContext& /*ctx*/) {
    // ボタンの更新や入力判定はワールド座標が確定するDrawフェーズ (RenderNodeWithRect) で実行するため、ここでは何もしない
}

UISystem::WorldRect UISystem::CalculateWorldRect(entt::entity entity, entt::registry& registry, float screenW, float screenH) {
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

void UISystem::Draw(entt::registry& registry, GameContext& ctx) {
    std::unordered_map<uint32_t, WorldRect> cache;

    // --- 既存のUI（Canvasベース）の描画 ---
    auto renderRecursive = [&](auto self, entt::entity parentId, WorldRect parentRect) -> void {
        auto view = registry.view<RectTransformComponent>();
        for (auto e : view) {
            entt::entity currentParentId = entt::null;
            if (registry.all_of<HierarchyComponent>(e)) {
                currentParentId = registry.get<HierarchyComponent>(e).parentId;
            }

            if (currentParentId == parentId) {
                auto& rect = view.get<RectTransformComponent>(e);
                // ★追加: enabledがfalseならこのノードと子ノードをスキップ
                if (!rect.enabled) continue;
                float worldW = rect.size.x;
                float worldH = rect.size.y;
                float anchorX = parentRect.x + parentRect.w * rect.anchor.x;
                float anchorY = parentRect.y + parentRect.h * rect.anchor.y;
                float worldX = anchorX - worldW * rect.pivot.x + rect.pos.x;
                float worldY = anchorY - worldH * rect.pivot.y + rect.pos.y;
                
                WorldRect selfRect = { worldX, worldY, worldW, worldH };
                uint32_t eId = static_cast<uint32_t>(e);
                cache[eId] = selfRect;

                RenderNodeWithRect(e, registry, selfRect, ctx);
                self(self, e, selfRect);
            }
        }
    };

    WorldRect screen = { 0, 0, (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };
    renderRecursive(renderRecursive, entt::null, screen);

    // ★追加: RectTransformを持たないが、Transform と UIText を持つエンティティの簡易2D描画
    auto viewRawText = registry.view<TransformComponent, UITextComponent>(entt::exclude<RectTransformComponent>);
    for (auto e : viewRawText) {
        auto& text = viewRawText.get<UITextComponent>(e);
        auto& transform = viewRawText.get<TransformComponent>(e);
        if (text.enabled) {
            // Transformの X/Y をスクリーンのピクセル座標として扱う (Zは無視)
            DrawTextW(e, registry, text, transform.translate.x, transform.translate.y, 0.0f, 0.0f, ctx.renderer);
        }
    }
}

// ★追加: ワールド空間UI（HPバー）の描画パス
void UISystem::DrawUI(entt::registry& registry, GameContext& ctx) {
    if (!ctx.camera) return;

    // OS画面全体に対して描画するため GetForegroundDrawList を使用
#ifdef USE_IMGUI
    ImDrawList* drawList = ImGui::GetForegroundDrawList(); 
    if (!drawList) return;
    // 以下の3D空間UI（HPバーなど）は GameScene コンテキストが必要
    if (!ctx.scene) return;

    auto viewHealth = registry.view<HealthComponent>();
    for (auto e : viewHealth) {
        auto& hc = viewHealth.get<HealthComponent>(e);
        
        const WorldSpaceUIComponent* uiComp = registry.try_get<WorldSpaceUIComponent>(e);

        // 1. HPバーの描画
        if (hc.enabled && !hc.isDead) {
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
                
                float barW = 60.0f;
                float barH = 6.0f;
                DirectX::XMFLOAT3 pos = basePos;

                // 頭上の高さを動的に計算（Colliderの大きさに合わせる）
                float heightOffset = 1.0f;
                if (registry.all_of<BoxColliderComponent>(e)) {
                    auto& bc = registry.get<BoxColliderComponent>(e);
                    // 中心高さ + 半分 に スケールを掛ける
                    float yBasis = (bc.center.y + bc.size.y * 0.5f) * std::abs(DirectX::XMVectorGetY(scale));
                    heightOffset = yBasis + 0.3f;
                } else if (registry.all_of<TransformComponent>(e)) {
                    heightOffset = registry.get<TransformComponent>(e).scale.y + 0.5f;
                }

                if (uiComp) {
                    pos.x += uiComp->offset.x;
                    pos.y += heightOffset + uiComp->offset.y - 1.2f; 
                    pos.z += uiComp->offset.z;
                    barW = uiComp->barWidth;
                    barH = uiComp->barHeight;
                } else {
                    pos.y += heightOffset;
                }

                // 最新のViewport（画像描画位置）を使用して投影
                if (WorldToScreenWithView(pos, *ctx.camera, ctx.viewportOffset, ctx.viewportSize, sx, sy)) {
                    float hpRate = hc.hp / (hc.maxHp > 0 ? hc.maxHp : 1.0f);
                    float curW = barW * std::clamp(hpRate, 0.0f, 1.0f);
                    
                    ImVec2 pMin(sx - barW * 0.5f, sy - barH * 0.5f);
                    ImVec2 pMax(sx + barW * 0.5f, sy + barH * 0.5f);
                    
                    // 背景
                    drawList->AddRectFilled(pMin, pMax, IM_COL32(40, 40, 40, 180));
                    // HP残量
                    drawList->AddRectFilled(pMin, ImVec2(pMin.x + curW, pMax.y), IM_COL32(50, 230, 50, 255));
                    // 枠
                    drawList->AddRect(pMin, pMax, IM_COL32(255, 255, 255, 200));
                }
            }
        }
    }
#endif
}

bool UISystem::WorldToScreen(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, float& screenX, float& screenY) {
    return WorldToScreenWithView(worldPos, camera, {0, 0}, {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH}, screenX, screenY);
}

bool UISystem::WorldToScreenWithView(const DirectX::XMFLOAT3& worldPos, const Engine::Camera& camera, const DirectX::XMFLOAT2& viewOffset, const DirectX::XMFLOAT2& viewSize, float& screenX, float& screenY) {
    DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&worldPos);
    
    // DirectXの標準関数を使用して投影
    DirectX::XMMATRIX view = camera.View();
    DirectX::XMMATRIX proj = camera.Proj();
    DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();

    // ビューポートサイズが0の場合、投影計算に失敗するためガード
    float vw = std::max(1.0f, viewSize.x);
    float vh = std::max(1.0f, viewSize.y);
    DirectX::XMVECTOR screenPos = DirectX::XMVector3Project(p, 0, 0, vw, vh, 0.0f, 1.0f, proj, view, world);
    
    DirectX::XMFLOAT3 sp;
    DirectX::XMStoreFloat3(&sp, screenPos);

    // デバッグ投影結果の妥当性チェック
    DirectX::XMMATRIX vp = view * proj;
    DirectX::XMVECTOR clipPos = DirectX::XMVector3TransformCoord(p, vp);
    float cz = DirectX::XMVectorGetZ(clipPos);
    if (cz < 0.0f || cz > 1.0f) return false;

    screenX = viewOffset.x + sp.x;
    screenY = viewOffset.y + sp.y;
    
    return true;
}

void UISystem::Reset(entt::registry& /*registry*/) {
    // 必要に応じて初期化処理を記述
}

void UISystem::RenderNodeWithRect(entt::entity entity, entt::registry& registry, const WorldRect& wr, GameContext& ctx) {
    // ボタンの更新
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
        auto& img = registry.get<UIImageComponent>(entity);
        if (img.enabled) {
            // ★追加: ボタンの場合は白い枠線を描画
            if (registry.all_of<UIButtonComponent>(entity)) {
                Engine::Renderer::SpriteDesc border;
                border.x = wr.x - 2.0f;
                border.y = wr.y - 2.0f;
                border.w = wr.w + 4.0f;
                border.h = wr.h + 4.0f;
                border.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                border.layer = img.layer; // ★追加: レイヤー引き継ぎ
                ctx.renderer->DrawSprite(0, border); // 0番テクスチャはRenderer初期化時に生成された白色
            }

            DirectX::XMFLOAT4 finalColor = { img.color.x * buttonColor.x, img.color.y * buttonColor.y, img.color.z * buttonColor.z, img.color.w * buttonColor.w };
            if (img.is9Slice) {
                Engine::Renderer::Sprite9SliceDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.left = img.borderLeft; s.right = img.borderRight; s.top = img.borderTop; s.bottom = img.borderBottom;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                s.layer = img.layer; // ★追加: レイヤー値を設定
                // ★注意: 9Sliceは内部でDrawSpriteに分解されるため、layer値は個別のSpriteDescで設定が必要
                // → DrawSprite9Sliceの内部で生成されるSpriteDescにはlayerが引き継がれないため、
                //   通常描画にフォールバックするか、Renderer側で対応する
                ctx.renderer->DrawSprite9Slice(img.textureHandle, s);
            } else {
                Engine::Renderer::SpriteDesc s;
                s.x = wr.x; s.y = wr.y; s.w = wr.w; s.h = wr.h;
                s.color = { finalColor.x, finalColor.y, finalColor.z, finalColor.w };
                s.rotationRad = DirectX::XMConvertToRadians(registry.get<RectTransformComponent>(entity).rotation);
                s.layer = img.layer; // ★追加: レイヤー値を設定
                ctx.renderer->DrawSprite(img.textureHandle, s);
            }
        }
    }

    // テキストの描画
    if (registry.all_of<UITextComponent>(entity)) {
        auto& text = registry.get<UITextComponent>(entity);
        if (text.enabled) {
            DrawTextW(entity, registry, text, wr.x, wr.y, wr.w, wr.h, ctx.renderer);
        }
    }
}

void UISystem::DrawTextW(entt::entity /*entity*/, entt::registry& /*registry*/, const UITextComponent& text, float worldX, float worldY, float worldW, float worldH, Engine::Renderer* renderer) {
	if (!renderer || text.text.empty() || text.color.w <= 0.01f) return;

	// フォントレンダラーの初期化サイズ (Renderer 内で 64.0f) を基準にスケール
	float fontScale = text.fontSize / 64.0f;

	float tw = renderer->MeasureTextWidth(text.text, fontScale, text.fontPath);
	float th = renderer->GetTextLineHeight(fontScale, text.fontPath);

	// 中央揃え (worldW/worldHが0の場合は左上揃え)
	float px = worldX;
	float py = worldY;
	if (worldW > 0.0f) px += (worldW - tw) * 0.5f;
	if (worldH > 0.0f) py += (worldH - th) * 0.5f;

	Engine::Vector4 colorVec = { text.color.x, text.color.y, text.color.z, text.color.w };
	renderer->DrawString(text.text, px, py, fontScale, colorVec, text.fontPath);
}

void UISystem::ProcessButton(entt::entity entity, entt::registry& registry, UIButtonComponent& btn, float worldX, float worldY, float worldW, float worldH, GameContext& ctx) {
    if (!ctx.input) return;

    float mx, my;
    if (ctx.useOverrideMouse) {
        mx = ctx.overrideMouseX;
        my = ctx.overrideMouseY;
    } else {
        float fmx, fmy;
        ctx.input->GetMousePos(fmx, fmy);
        
        // ★修正: ビューポートオフセットを引いて、内部解像度(1920x1080)に変換
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

    btn.isHovered = hovered;
    btn.isPressed = hovered && ctx.input->IsMouseDown(0); // 左ボタン

    if (hovered && ctx.input->IsMouseTrigger(0)) {
        // クリック時: スクリプト側へ通知
        if (registry.all_of<ScriptComponent>(entity)) {
            auto& sc = registry.get<ScriptComponent>(entity);
            if (sc.enabled) {
                for (auto& entry : sc.scripts) {
                    if (entry.instance) {
                        // To DO: on click needs to accept entt::entity instead of SceneObject
                        // entry.instance->OnClick(entity, ctx.scene, btn.onClickCallback);
                    }
                }
            }
        }
    }
}

} // namespace Game
