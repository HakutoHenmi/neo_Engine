// SelectScene.cpp
// ステージ選択画面の初期化・入力処理・描画。
#include "../UI/SceneMusic.h"
#include "SelectScene.h"
// 共通の UI 描画と、入力・シーン管理 (エンジン側)
#include "../UI/GameUI.h"
#include "../Data/GameConfig.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <utility>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {


// 破棄: 特に後片付けは無い
SelectScene::~SelectScene() {
}

// 初期化: BGM・白テクスチャ・ポストエフェクトの設定
void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    // パラメータは使わない
    (void)params;
    // ウィンドウを覚え、セレクトの BGM を流す
    dx_ = dx;
    Music::Play(Music::Select);
    // 描画の窓口
    renderer_ = Engine::Renderer::GetInstance();
    if (renderer_) {
        // 単色の塗りつぶしに使う白い 1x1 テクスチャ
        whiteTexture_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");

        // 前のシーンで使ったポストエフェクトの効果を全部切る
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
    
    // マウスで操作するのでカーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
}

// 更新: 出撃・タイトルへ戻る・デモへの移動の入力を処理する
void SelectScene::Update() {
    // ゲームから戻ってきたときもカーソルを出す
    Engine::WindowDX::SetCursorVisible(true);
#ifndef NDEBUG
    // 入力
    auto* input = Engine::Input::GetInstance();
    // F1 で評価課題のデモ (Assignment) へ移動する (ステージを増やさずにデモを残すため)
    if (input->Trigger(0x3B)) { // F1
        Engine::SceneManager::GetInstance()->RequestChange("Assignment");
        return;
    }
#endif
    
    // 出撃: スライムの街ステージ (chrono.json) でゲームシーンを始める
    auto confirmSelection = [this]() {
            // 本編のシーンファイルを指定してゲームへ移る
            Engine::SceneParameters params;
            params.stagePath = Data::MainStagePath();
            Engine::SceneManager::GetInstance()->RequestChange("Game", params);
    };

    // 出撃ボタンのクリック、Esc / B でタイトルへ
    UI::Canvas ui(renderer_);
    if (ui.Click(UI::Stage)) { confirmSelection(); return; }
    if (UI::BackPressed()) {
        Engine::SceneManager::GetInstance()->RequestChange("Title"); return;
    }
    // この画面に表示している操作だけを受け付ける
    if (UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_START)) { // Enter (0x1C) / START で出撃
        confirmSelection();
    }
}

// 描画: メニュー画面を描く
void SelectScene::Draw() {
    DrawMenu();
}

// ImGui 用 (この画面では使わない)
void SelectScene::DrawUI() {
}

// メニュー画面の描画 (ステージ名・説明・目印のチップ・紹介カード・ボタン)
void SelectScene::DrawMenu() {
    // 描画の窓口が無ければ何もしない
    if (!renderer_) return;
    UI::Canvas ui(renderer_);
    // 背景と上部のロゴ
    ui.Background(UI::Str("select.section").c_str());
    // アニメーション用の経過秒数
    const float t=UI::Seconds();
    // ステージ名の後ろに大きく薄い番号を置く
    ui.Text(UI::Str("select.number"),52,70,260,{.1f,.16f,.125f,1});
    // ステージ名と説明
    ui.Label(UI::Str("select.label"),98,174,14,UI::Lime,3);
    ui.Text(UI::Str("select.stageName"),90,192,96,UI::Paper);
    ui.Text(UI::Str("select.description"),98,308,24,UI::Paper,UI::JapaneseFont);
    // 特徴を表す3つのチップを横に並べる
    float chipX=98;
    for(auto [label,color]:{std::pair<const char*,Engine::Vector4>{UI::Str("select.chip1").c_str(),UI::Lime},{UI::Str("select.chip2").c_str(),UI::Gold},{UI::Str("select.chip3").c_str(),UI::Teal}}){
        // チップの幅 (文字の幅 + 余白)
        const float w=ui.SpacedWidth(label,13,2)+30;
        // 色付きの丸い台と縁
        ui.Rounded({chipX,364,w,32},UI::WithAlpha(color,.12f),16);ui.RoundedRing({chipX,364,w,32},UI::WithAlpha(color,.55f),16);
        // 文字を書き、次のチップの位置へ進める
        ui.Label(label,chipX+15,372,13,color,2);chipX+=w+10;
    }
    // 現地調査カード: 戦いの様子 (敵の群れ・スライム・囲む輪) を動きで見せる
    // カードの位置と大きさ
    const UI::Rect map{760,128,440,392};
    // 後ろの暗いぼかしと、カードの台・縁
    ui.Halo(map.x+map.w*.5f,map.y+map.h*.5f,330,UI::WithAlpha(UI::Night,.6f));
    ui.Rounded(map,{.07f,.115f,.09f,.97f},24);
    ui.RoundedRing(map,UI::WithAlpha(UI::Paper,.1f),24);
    // 右上の筆の塗りと小見出し
    ui.Brush({940,108,190,44},UI::WithAlpha(UI::Paper,.22f),.09f);
    ui.Label(UI::Str("select.survey"),map.x+24,map.y+22,12,UI::WithAlpha(UI::Lime,.85f),3);
    // カード内の背景線と、渦を巻くように動く敵の群れ (点)
    const float mx=map.x+map.w*.5f,my=map.y+190;
    // うっすらとした横線
    for(int i=0;i<4;++i)ui.Line(map.x+20,my-90+float(i)*58+std::sin(float(i)*1.7f)*14,map.x+map.w-20,my-70+float(i)*58,1,UI::WithAlpha(UI::Muted,.12f));
    for(int i=0;i<34;++i){
        // 黄金角でばらし、少しずつ回す (点ごとに速さと半径を変える)
        const float a=float(i)*2.399963f+t*(.18f+.05f*UI::Noise(float(i))),r=60+120*UI::Noise(float(i)*3.3f);
        ui.Dot(mx+std::cos(a)*r,my+std::sin(a)*r*.62f,3.5f+2*UI::Noise(float(i)*5.1f),UI::WithAlpha({.45f,.72f,.9f,1},.85f));
    }
    // スライムが群れを囲む輪を描いていく様子 (毎秒少しずつ伸び、1周で最初に戻る)
    const float draw=std::fmod(t*.28f,1.f);
    for(int i=0;i<60;++i){const float s0=float(i)/60,s1=float(i+1)/60;if(s0>draw)break;
        // 楕円の上の位置
        auto at=[&](float s){const float a=s*6.283185f;return std::array<float,2>{mx+40+std::cos(a)*95,my+10+std::sin(a)*64};};
        auto p0=at(s0),p1=at((std::min)(s1,draw));ui.Line(p0[0],p0[1],p1[0],p1[1],5,UI::Lime);}
    // 輪の先端にいる小さなスライム
    ui.SlimeMark(mx+40+std::cos(draw*6.283185f)*95,my+10+std::sin(draw*6.283185f)*64,11);
    // 下の説明文
    ui.Text(UI::Str("select.surveyNote"),map.x+24,map.y+map.h-56,17,UI::Muted,UI::JapaneseFont);
    // 出撃ボタンと操作の案内
    ui.Button(UI::Stage,UI::Str("select.deploy"),true);
    ui.Prompt("mouse_left",UI::Str("select.promptClick"),"xbox_button_a",UI::Str("select.promptDeploy"),100,592,26,17);
    ui.Prompt("keyboard_enter",UI::Str("select.promptDeploy"),"xbox_button_menu",UI::Str("select.promptDeploy"),220,592,26,17);
    ui.Prompt("keyboard_escape",UI::Str("select.promptTitle"),"xbox_button_b",UI::Str("select.promptTitle"),1098,640,26,17);
}
// エディタ用 (この画面では追加の項目なし)
void SelectScene::DrawEditor() {
}

} // namespace Game
