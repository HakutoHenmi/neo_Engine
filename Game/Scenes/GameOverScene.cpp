// GameOverScene.cpp
// ゲームオーバー画面の初期化・入力処理・描画。
#include "../UI/SceneMusic.h"
#include "GameOverScene.h"
// シーン管理・描画・入力 (エンジン側)
#include "../../Engine/SceneManager.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
// 共通の UI 描画
#include "../UI/GameUI.h"
#include "../Data/GameConfig.h"
namespace Game {
// 初期化: 敗北の BGM を流し、ゲーム中の流体粒子とポストエフェクトを消す
void GameOverScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters&) {
    // ウィンドウを覚える
    dx_=dx;
    // 敗北の BGM (音量 0.34)
    Music::Play(Music::Defeat);
    // メニューを操作できるようマウスカーソルを出す
    Engine::WindowDX::SetCursorVisible(true);
    if(auto* r=Engine::Renderer::GetInstance()){r->ResetGPUFluid();r->SetPostEffect("Default");}
}
// 更新: リトライ (R / Enter / X / ボタン) か、ステージ選択 (Tab / Y / ボタン) へ移る
void GameOverScene::Update() {
    // 入力が無ければ何もしない
    auto* input=Engine::Input::GetInstance(); if(!input)return;
    // ボタンのクリック判定に使う
    UI::Canvas ui(Engine::Renderer::GetInstance());
    if(input->Trigger(DIK_R)||input->Trigger(DIK_RETURN)||input->PadTrigger(XINPUT_GAMEPAD_X)||ui.Click(UI::Retry)){
        // 本編のシーンファイルを指定してゲームをやり直す
        Engine::SceneParameters params;params.stagePath=Data::MainStagePath();
        Engine::SceneManager::GetInstance()->RequestChange("Game",params);
    } else if(input->Trigger(DIK_TAB)||input->PadTrigger(XINPUT_GAMEPAD_Y)||ui.Click(UI::Select)) {
        Engine::SceneManager::GetInstance()->RequestChange("Select");
    }
}
// 描画: 失敗の結果カードを表示する
void GameOverScene::Draw() {
    // UI の描画先
    UI::Canvas ui(Engine::Renderer::GetInstance());
    ui.Result(false,UI::Str("result.gameOverTime"),UI::Str("result.gameOverDetail"));
}
// ImGui 用 (この画面では使わない)
void GameOverScene::DrawUI() {}
}