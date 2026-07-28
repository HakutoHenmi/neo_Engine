#include "SelectScene.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

namespace Game {

SelectScene::~SelectScene() {
}

void SelectScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    (void)params;
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    selectedIndex_ = 0;
}

void SelectScene::Update() {
    auto* input = Engine::Input::GetInstance();
    
    // Up / Down logic to select scene
    if (input->Trigger(0xC8) || input->Trigger(0x11)) { // Up Arrow or W
        selectedIndex_--;
        if (selectedIndex_ < 0) selectedIndex_ = 1;
    }
    if (input->Trigger(0xD0) || input->Trigger(0x1F)) { // Down Arrow or S
        selectedIndex_++;
        if (selectedIndex_ > 1) selectedIndex_ = 0;
    }
    
    float ly = 0.0f;
    bool aTrigger = false;
    
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        float normLY = std::fmaxf(-1, (float)state.Gamepad.sThumbLY / 32767);
        ly = (std::abs(normLY) < 0.2f ? 0.0f : normLY);
        
        static bool prevA = false;
        bool currA = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        if (currA && !prevA) aTrigger = true;
        prevA = currA;
        
        static bool prevUp = false;
        bool currUp = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        if (currUp && !prevUp) {
            selectedIndex_--;
            if (selectedIndex_ < 0) selectedIndex_ = 1;
        }
        prevUp = currUp;
        
        static bool prevDown = false;
        bool currDown = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        if (currDown && !prevDown) {
            selectedIndex_++;
            if (selectedIndex_ > 1) selectedIndex_ = 0;
        }
        prevDown = currDown;
    }
    
    static bool stickUp = false;
    static bool stickDown = false;
    
    if (ly > 0.5f && !stickUp) {
        selectedIndex_--;
        if (selectedIndex_ < 0) selectedIndex_ = 1;
        stickUp = true;
    } else if (ly <= 0.5f) {
        stickUp = false;
    }
    
    if (ly < -0.5f && !stickDown) {
        selectedIndex_++;
        if (selectedIndex_ > 1) selectedIndex_ = 0;
        stickDown = true;
    } else if (ly >= -0.5f) {
        stickDown = false;
    }
    
    // Select (Enter or Pad A)
    if (input->Trigger(0x1C) || aTrigger) { // 0x1C = Enter
        if (selectedIndex_ == 0) {
            Engine::SceneManager::GetInstance()->RequestChange("Game");
        } else {
            Engine::SceneManager::GetInstance()->RequestChange("Assignment");
        }
    }
}

void SelectScene::Draw() {
    // Clear screen with a different color
}

void SelectScene::DrawUI() {
    if (!renderer_) return;
    
    float sw = (float)Engine::WindowDX::kW;
    float sh = (float)Engine::WindowDX::kH;
    
    float titleW = renderer_->MeasureTextWidth("Select Scene", 1.5f);
    renderer_->DrawString("Select Scene", sw/2.0f - titleW/2.0f, sh * 0.2f, 1.5f, {1, 1, 1, 1});
    
    // Game Scene Option
    Engine::Vector4 color0 = (selectedIndex_ == 0) ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{0.5f, 0.5f, 0.5f, 1};
    float opt0W = renderer_->MeasureTextWidth("1. Play Game", 1.0f);
    renderer_->DrawString("1. Play Game", sw/2.0f - opt0W/2.0f, sh * 0.5f, 1.0f, color0);

    // Assignment Scene Option
    Engine::Vector4 color1 = (selectedIndex_ == 1) ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{0.5f, 0.5f, 0.5f, 1};
    float opt1W = renderer_->MeasureTextWidth("2. Assignment Scene", 1.0f);
    renderer_->DrawString("2. Assignment Scene", sw/2.0f - opt1W/2.0f, sh * 0.6f, 1.0f, color1);
    
    float guideW = renderer_->MeasureTextWidth("Use W/S or D-Pad to Select, Enter/A to Confirm", 0.5f);
    renderer_->DrawString("Use W/S or D-Pad to Select, Enter/A to Confirm", sw/2.0f - guideW/2.0f, sh * 0.9f, 0.5f, {1, 1, 1, 1});
}

void SelectScene::DrawEditor() {
}

} // namespace Game
