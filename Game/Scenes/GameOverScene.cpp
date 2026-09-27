#include "GameOverScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../UI/GameUI.h"
namespace Game {
void GameOverScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters&) {
    dx_=dx;
    Engine::WindowDX::SetCursorVisible(true);
    if(auto* r=Engine::Renderer::GetInstance()){r->ResetGPUFluid();r->SetPostEffect("Default");}
}
void GameOverScene::Update() {
    auto* input=Engine::Input::GetInstance(); if(!input)return;
    UI::Canvas ui(Engine::Renderer::GetInstance());
    if(input->Trigger(DIK_R)||input->Trigger(DIK_RETURN)||ui.Click(UI::Retry)){
        Engine::SceneParameters params;params.stagePath="Resources/Scenes/chrono.json";
        Engine::SceneManager::GetInstance()->RequestChange("Game",params);
    } else if(input->Trigger(DIK_TAB)||ui.Click(UI::Select)) {
        Engine::SceneManager::GetInstance()->RequestChange("Select");
    }
}
void GameOverScene::Draw() {
    UI::Canvas ui(Engine::Renderer::GetInstance());
    ui.Result(false,"EXPEDITION ENDED","Regroup, rebuild your mass, and try again.");
}
void GameOverScene::DrawUI() {}
}