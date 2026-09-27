#include "SelectScene.h"
#include "../UI/GameUI.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>

namespace Game {


SelectScene::~SelectScene() {
}

void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    (void)params;
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    if (renderer_) {
        whiteTexture_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

        auto pp = renderer_->GetPostProcessParams();
        pp.noiseStrength = 0.0f;
        pp.distortion = 0.0f;
        pp.chromaShift = 0.0f;
        pp.vignette = 0.0f;
        pp.scanline = 0.0f;
        pp.san = 0.0f;
        renderer_->SetPostProcessParams(pp);
        renderer_->SetPostEffect("Default");
    }
    
    // カーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
}

void SelectScene::Update() {
    Engine::WindowDX::SetCursorVisible(true);
#ifndef NDEBUG
    auto* input = Engine::Input::GetInstance();
    // Keep the retained test scene accessible without adding a second stage.
    if (input->Trigger(0x3B)) { // F1
        Engine::SceneManager::GetInstance()->RequestChange("Assignment");
        return;
    }
#endif
    
    auto confirmSelection = [this]() {
            Engine::SceneParameters params;
            params.stagePath = "Resources/Scenes/chrono.json";
            Engine::SceneManager::GetInstance()->RequestChange("Game", params);
    };

    UI::Canvas ui(renderer_);
    if (ui.Click(UI::Stage)) { confirmSelection(); return; }
    if (UI::Pressed(DIK_ESCAPE)) {
        Engine::SceneManager::GetInstance()->RequestChange("Title"); return;
    }
    // Only the actions advertised on this screen.
    if (UI::Pressed(DIK_RETURN)) { // 0x1C = Enter
        confirmSelection();
    }
}

void SelectScene::Draw() {
    DrawMenu();
}

void SelectScene::DrawUI() {
}

void SelectScene::DrawMenu() {
    if (!renderer_) return;
    UI::Canvas ui(renderer_);
    ui.Background("EXPEDITION SELECT");
    ui.Panel({220,160,840,450});
    ui.Text("01  /  BOSS ENCOUNTER",270,203,22,UI::Lime);
    ui.Text("VERDANT BASIN",270,244,56);
    ui.Text("One slime. One boss. An entire valley to fight.",270,314,26,UI::Muted);
    ui.Button(UI::Stage,"DEPLOY TO VERDANT BASIN",true);
    ui.Prompt("mouse_left","CLICK TO DEPLOY",350,506);
    ui.Prompt("keyboard_enter","DEPLOY",690,506);
    ui.Prompt("keyboard_escape","TITLE",1035,670);
}
void SelectScene::DrawEditor() {
}

} // namespace Game
