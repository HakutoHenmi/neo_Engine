#include "TitleScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../Systems/UISystem.h"
#include "../ObjectTypes.h"
#include "../../externals/imgui/imgui.h"

namespace Game {

void TitleScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& /*params*/) {
    dx_ = dx;
    uiSystem_ = std::make_unique<UISystem>();

    ctx_.dt = 1.0f / 60.0f;
    ctx_.camera = &camera_;
    ctx_.renderer = Engine::Renderer::GetInstance();
    ctx_.input = Engine::Input::GetInstance();
    ctx_.scene = nullptr;
    ctx_.isPlaying = true; // UIをアクティブにするため



    // --- 背景用画像のセット（もし必要であれば） ---
    // SceneRenderなどで適宜描画されるが、ここではクリアカラーまたはUI背景を使う
    // Renderer の背景を使用するかどうか（TitleScene固有のモデルやスプライトを置くことも可能）

    // --- タイトル文字 ---
    auto titleEnt = registry_.create();
    auto& rtTitle = registry_.emplace<RectTransformComponent>(titleEnt);
    rtTitle.pos = { 0.0f, -(float)Engine::WindowDX::kH * 0.25f }; // 中央から少し上
    rtTitle.size = { 800.0f, 150.0f };
    auto& txtTitle = registry_.emplace<UITextComponent>(titleEnt);
    txtTitle.text = "NEO ENGINE";
    txtTitle.fontSize = 120.0f;
    txtTitle.color = { 1.0f, 0.8f, 0.2f, 1.0f };

    // --- ボタン共通設定関数 ---
    auto createButton = [&](const std::string& textStr, float yOffset, entt::entity& outEnt) {
        outEnt = registry_.create();
        auto& rt = registry_.emplace<RectTransformComponent>(outEnt);
        rt.pos = { 0.0f, yOffset }; // Xは中央、Yは指定のオフセット
        rt.size = { 300.0f, 80.0f };
        
        auto& img = registry_.emplace<UIImageComponent>(outEnt);
        img.textureHandle = 0; // 白ベタ (Renderer初期化時のデフォルト白テクスチャ)
        img.color = { 0.1f, 0.1f, 0.15f, 0.9f }; // 少し暗めの背景
        img.is9Slice = false;
        
        auto& btn = registry_.emplace<UIButtonComponent>(outEnt);
        btn.normalColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        btn.hoverColor = { 0.8f, 1.0f, 0.8f, 1.0f };
        btn.pressedColor = { 0.5f, 0.8f, 0.5f, 1.0f };
        
        auto& txt = registry_.emplace<UITextComponent>(outEnt);
        txt.text = textStr;
        txt.fontSize = 40.0f;
        txt.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // 項目は上から タイトル / スタート / 設定 / 終わる
    createButton("START", 50.0f, btnStart_);
    createButton("SETTINGS", 150.0f, btnSettings_);
    createButton("EXIT", 250.0f, btnExit_);
}

void TitleScene::Update() {
    ctx_.dt = 1.0f / 60.0f;
    
    // エディタ等からの描画オフセットを同期（Standaloneの場合は0）
    ctx_.viewportOffset = { 0.0f, 0.0f };
    ctx_.viewportSize = { (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };

    // UIのステート更新 (ホバー判定など)
    uiSystem_->Update(registry_, ctx_);

    // ボタンのクリック判定
    auto checkClick = [&](entt::entity ent) -> bool {
        if (registry_.all_of<UIButtonComponent>(ent)) {
            auto& btn = registry_.get<UIButtonComponent>(ent);
            if (btn.isHovered && ctx_.input->IsMouseTrigger(0)) {
                return true;
            }
        }
        return false;
    };

    if (checkClick(btnStart_)) {
        Engine::SceneManager::GetInstance()->RequestChange("Game");
        return;
    }
    if (checkClick(btnSettings_)) {
        // TODO: Settings
    }
    if (checkClick(btnExit_)) {
        PostQuitMessage(0);
        return;
    }
}

void TitleScene::Draw() {
    // 3D/2D描画
    // タイトルシーンの背景などを描画したい場合はここに追加
    uiSystem_->Draw(registry_, ctx_);
}

void TitleScene::DrawUI() {
    uiSystem_->DrawUI(registry_, ctx_);

    // --- カスタムマウスカーソルの描画 ---
#ifdef USE_IMGUI
    ImGui::SetMouseCursor(ImGuiMouseCursor_None); // デフォルトのOSカーソルを隠す
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 mousePos = ImGui::GetMousePos();
    
    // スチームパンク風・またはスタイリッシュなクロスヘアカーソル
    float size = 10.0f;
    ImU32 colorOuter = IM_COL32(255, 180, 50, 255);
    ImU32 colorInner = IM_COL32(255, 255, 255, 255);
    
    // 中央のドットと円
    dl->AddCircleFilled(mousePos, 2.5f, colorInner);
    dl->AddCircle(mousePos, size, colorOuter, 0, 2.0f);
    
    // 十字線の装飾
    dl->AddLine(ImVec2(mousePos.x - size - 6, mousePos.y), ImVec2(mousePos.x - size + 2, mousePos.y), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x + size + 6, mousePos.y), ImVec2(mousePos.x + size - 2, mousePos.y), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x, mousePos.y - size - 6), ImVec2(mousePos.x, mousePos.y - size + 2), colorOuter, 2.0f);
    dl->AddLine(ImVec2(mousePos.x, mousePos.y + size + 6), ImVec2(mousePos.x, mousePos.y + size - 2), colorOuter, 2.0f);
    
    // 少し回転させた四角形
    dl->AddQuad(
        ImVec2(mousePos.x, mousePos.y - size * 0.7f),
        ImVec2(mousePos.x + size * 0.7f, mousePos.y),
        ImVec2(mousePos.x, mousePos.y + size * 0.7f),
        ImVec2(mousePos.x - size * 0.7f, mousePos.y),
        IM_COL32(255, 180, 50, 150), 1.5f
    );
#endif
}

} // namespace Game
