#include "GameOverScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../externals/imgui/imgui.h"

namespace Game {

void GameOverScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& /*params*/) {
    dx_ = dx;
    // マウスカーソルを表示する
    ShowCursor(TRUE);

    // ゲームオーバー演出として、画面全体に平滑化（ぼかし）エフェクトを適用
    if (auto* renderer = Engine::Renderer::GetInstance()) {
        renderer->SetPostEffect("Random");
    }
}

void GameOverScene::Update() {
    // Rキーでもリトライできるようにショートカットを設ける
    auto* input = Engine::Input::GetInstance();
    if (input && input->Trigger(DIK_R)) {
        if (auto* renderer = Engine::Renderer::GetInstance()) {
            renderer->SetPostEffect(""); // エフェクトをリセット
        }
        Engine::SceneManager::GetInstance()->Change("Game");
    }
}

void GameOverScene::Draw() {
    // ポストプロセスのクリアなどが必要であれば行うが、基本は空でOK
}

void GameOverScene::DrawUI() {
    // 画面全体を覆う暗い背景
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | 
                             ImGuiWindowFlags_NoResize | 
                             ImGuiWindowFlags_NoMove | 
                             ImGuiWindowFlags_NoScrollbar | 
                             ImGuiWindowFlags_NoSavedSettings;
                             
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.85f));
    ImGui::Begin("GameOverWindow", nullptr, flags);
    ImGui::PopStyleColor();

    // GAME OVER テキスト
    ImGui::SetCursorPos(ImVec2((float)Engine::WindowDX::kW * 0.5f - 240.0f, (float)Engine::WindowDX::kH * 0.4f - 50.0f));
    ImGui::SetWindowFontScale(5.0f);
    ImGui::TextColored(ImVec4(0.9f, 0.1f, 0.1f, 1.0f), "G A M E   O V E R");

    // 操作説明テキスト
    ImGui::SetCursorPos(ImVec2((float)Engine::WindowDX::kW * 0.5f - 160.0f, (float)Engine::WindowDX::kH * 0.4f + 40.0f));
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "You were defeated. Try again?");

    // RETRY ボタン
    ImVec2 btnSize(240.0f, 60.0f);
    ImGui::SetCursorPos(ImVec2((float)Engine::WindowDX::kW * 0.5f - btnSize.x * 0.5f, (float)Engine::WindowDX::kH * 0.6f));
    ImGui::SetWindowFontScale(2.0f);
    
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.05f, 0.05f, 1.0f));
    
    if (ImGui::Button("RETRY (R)", btnSize)) {
        if (auto* renderer = Engine::Renderer::GetInstance()) {
            renderer->SetPostEffect(""); // エフェクトをリセット
        }
        Engine::SceneManager::GetInstance()->Change("Game");
    }
    
    ImGui::PopStyleColor(3);
    ImGui::End();
}

} // namespace Game
