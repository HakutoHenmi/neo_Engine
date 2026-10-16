#include "../UI/SceneMusic.h"
#include "SelectScene.h"
#include "../UI/GameUI.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <utility>

namespace Game {


SelectScene::~SelectScene() {
}

void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    (void)params;
    dx_ = dx;
    Music::Play(Music::Select, .28f);
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
    if (UI::BackPressed()) {
        Engine::SceneManager::GetInstance()->RequestChange("Title"); return;
    }
    // Only the actions advertised on this screen.
    if (UI::Pressed(DIK_RETURN) || UI::PadPressed(XINPUT_GAMEPAD_START)) { // 0x1C = Enter
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
    const float t=UI::Seconds();
    // A huge faded numeral sits behind the stage name.
    ui.Text("01",52,70,260,{.1f,.16f,.125f,1});
    ui.Label("EXPEDITION 01  /  SWARM ASSAULT",98,174,14,UI::Lime,3);
    ui.Text("VERDANT BASIN",90,192,96,UI::Paper);
    ui.Text("群れを一掃し、地中の大蛇を呼び覚ませ。",98,308,24,UI::Paper,UI::JapaneseFont);
    float chipX=98;
    for(auto [label,color]:{std::pair<const char*,Engine::Vector4>{"18 ENEMIES",UI::Lime},{"MEADOW EXPEDITION",UI::Gold},{"SLIME MORPH",UI::Teal}}){
        const float w=ui.SpacedWidth(label,13,2)+30;
        ui.Rounded({chipX,364,w,32},UI::WithAlpha(color,.12f),16);ui.RoundedRing({chipX,364,w,32},UI::WithAlpha(color,.55f),16);
        ui.Label(label,chipX+15,372,13,color,2);chipX+=w+10;
    }
    // Field survey card: a live sketch of the fight (swarm, slime, a closing loop).
    const UI::Rect map{760,128,440,392};
    ui.Halo(map.x+map.w*.5f,map.y+map.h*.5f,330,UI::WithAlpha(UI::Night,.6f));
    ui.Rounded(map,{.07f,.115f,.09f,.97f},24);
    ui.RoundedRing(map,UI::WithAlpha(UI::Paper,.1f),24);
    ui.Brush({940,108,190,44},UI::WithAlpha(UI::Paper,.22f),.09f);
    ui.Label("FIELD SURVEY",map.x+24,map.y+22,12,UI::WithAlpha(UI::Lime,.85f),3);
    const float mx=map.x+map.w*.5f,my=map.y+190;
    for(int i=0;i<4;++i)ui.Line(map.x+20,my-90+float(i)*58+std::sin(float(i)*1.7f)*14,map.x+map.w-20,my-70+float(i)*58,1,UI::WithAlpha(UI::Muted,.12f));
    for(int i=0;i<34;++i){
        const float a=float(i)*2.399963f+t*(.18f+.05f*UI::Noise(float(i))),r=60+120*UI::Noise(float(i)*3.3f);
        ui.Dot(mx+std::cos(a)*r,my+std::sin(a)*r*.62f,3.5f+2*UI::Noise(float(i)*5.1f),UI::WithAlpha({.45f,.72f,.9f,1},.85f));
    }
    const float draw=std::fmod(t*.28f,1.f);
    for(int i=0;i<60;++i){const float s0=float(i)/60,s1=float(i+1)/60;if(s0>draw)break;
        auto at=[&](float s){const float a=s*6.283185f;return std::array<float,2>{mx+40+std::cos(a)*95,my+10+std::sin(a)*64};};
        auto p0=at(s0),p1=at((std::min)(s1,draw));ui.Line(p0[0],p0[1],p1[0],p1[1],5,UI::Lime);}
    ui.SlimeMark(mx+40+std::cos(draw*6.283185f)*95,my+10+std::sin(draw*6.283185f)*64,11);
    ui.Text("いつもの草原で、分裂・取り込み・変形を楽しもう。",map.x+24,map.y+map.h-56,17,UI::Muted,UI::JapaneseFont);
    ui.Button(UI::Stage,"DEPLOY TO VERDANT BASIN",true);
    ui.Prompt("mouse_left","CLICK","xbox_button_a","DEPLOY",100,592,26,17);
    ui.Prompt("keyboard_enter","DEPLOY","xbox_button_menu","DEPLOY",220,592,26,17);
    ui.Prompt("keyboard_escape","TITLE","xbox_button_b","TITLE",1098,640,26,17);
}
void SelectScene::DrawEditor() {
}

} // namespace Game
