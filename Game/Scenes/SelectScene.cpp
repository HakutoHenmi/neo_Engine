#include "SelectScene.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

namespace Game {

namespace {
constexpr int kOptionCount = 3;
constexpr const char* kOptionLabels[kOptionCount] = {
    "1. Stage 1", "2. Stage 2", "3. Test Scene"
};
constexpr float kOptionY[kOptionCount] = {0.44f, 0.56f, 0.68f};
}

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
    selectedIndex_ = 0;
    
    // カーソルを表示する
    Engine::WindowDX::SetCursorVisible(true);
}

void SelectScene::Update() {
    auto* input = Engine::Input::GetInstance();
    Engine::WindowDX::SetCursorVisible(true);
    
    // Up / Down logic to select scene
    if (input->Trigger(0xC8) || input->Trigger(0x11)) { // Up Arrow or W
        selectedIndex_--;
        if (selectedIndex_ < 0) selectedIndex_ = kOptionCount - 1;
    }
    if (input->Trigger(0xD0) || input->Trigger(0x1F)) { // Down Arrow or S
        selectedIndex_++;
        if (selectedIndex_ >= kOptionCount) selectedIndex_ = 0;
    }
    
    float ly = 0.0f;
    bool aTrigger = false;
    
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        float normLY = std::fmaxf(-1, (float)state.Gamepad.sThumbLY / 32767);
        ly = (std::abs(normLY) < 0.2f ? 0.0f : normLY);
        
        bool currA = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        if (currA && !prevA_) aTrigger = true;
        prevA_ = currA;
        
        bool currUp = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        if (currUp && !prevUp_) {
            selectedIndex_--;
            if (selectedIndex_ < 0) selectedIndex_ = kOptionCount - 1;
        }
        prevUp_ = currUp;
        
        bool currDown = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        if (currDown && !prevDown_) {
            selectedIndex_++;
            if (selectedIndex_ >= kOptionCount) selectedIndex_ = 0;
        }
        prevDown_ = currDown;
    }
    
    if (ly > 0.5f && !stickUp_) {
        selectedIndex_--;
        if (selectedIndex_ < 0) selectedIndex_ = kOptionCount - 1;
        stickUp_ = true;
    } else if (ly <= 0.5f) {
        stickUp_ = false;
    }
    
    if (ly < -0.5f && !stickDown_) {
        selectedIndex_++;
        if (selectedIndex_ >= kOptionCount) selectedIndex_ = 0;
        stickDown_ = true;
    } else if (ly >= -0.5f) {
        stickDown_ = false;
    }

    auto confirmSelection = [this]() {
        if (selectedIndex_ == 2) {
            Engine::SceneManager::GetInstance()->RequestChange("Assignment");
        } else {
            Engine::SceneParameters params;
            if (selectedIndex_ == 1) params.stagePath = "Resources/Scenes/stage2.json";
            Engine::SceneManager::GetInstance()->RequestChange("Equipment", params);
        }
    };

    if (renderer_) {
        const float sw = static_cast<float>(Engine::WindowDX::kW);
        const float sh = static_cast<float>(Engine::WindowDX::kH);
        const float optionH = 72.0f;
        const float optionPaddingX = 80.0f;


        float mx = 0.0f;
        float my = 0.0f;
        input->GetMousePos(mx, my);

        auto inRect = [](float px, float py, float x, float y, float w, float h) {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        };

        for (int i = 0; i < kOptionCount; ++i) {
            const float width = renderer_->MeasureTextWidth(kOptionLabels[i], 1.0f) + optionPaddingX;
            if (inRect(mx, my, sw * 0.5f - width * 0.5f,
                       sh * kOptionY[i] - 14.0f, width, optionH)) {
                selectedIndex_ = i;
                if (input->IsMouseTrigger(0)) {
                    confirmSelection();
                    return;
                }
            }
        }
    }
    
    // Select (Enter or Pad A)
    if (input->Trigger(0x1C) || aTrigger) { // 0x1C = Enter
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
    
    float sw = (float)Engine::WindowDX::kW;
    float sh = (float)Engine::WindowDX::kH;

    auto drawRect = [this](float x, float y, float w, float h, const Engine::Vector4& color, int layer) {
        Engine::Renderer::SpriteDesc desc;
        desc.x = x;
        desc.y = y;
        desc.w = w;
        desc.h = h;
        desc.color = color;
        desc.layer = layer;
        renderer_->DrawSprite(whiteTexture_, desc);
    };

    drawRect(0.0f, 0.0f, sw, sh, {0.025f, 0.035f, 0.055f, 1.0f}, 0);

    const float panelW = 1180.0f;
    const float panelH = 640.0f;
    const float panelX = sw * 0.5f - panelW * 0.5f;
    const float panelY = sh * 0.15f;
    drawRect(panelX, panelY, panelW, panelH, {0.065f, 0.08f, 0.125f, 0.96f}, 1);
    drawRect(panelX, panelY, panelW, 6.0f, {0.16f, 0.82f, 1.0f, 1.0f}, 2);
    drawRect(panelX, panelY + panelH - 6.0f, panelW, 6.0f, {1.0f, 0.22f, 0.62f, 1.0f}, 2);

    const char* title = "SELECT SCENE";
    float titleW = renderer_->MeasureTextWidth(title, 1.35f);
    renderer_->DrawString(title, sw/2.0f - titleW/2.0f, panelY + 86.0f, 1.35f, {1.0f, 0.93f, 0.34f, 1.0f});

    const char* subtitle = "Choose where to go next";
    float subtitleW = renderer_->MeasureTextWidth(subtitle, 0.45f);
    renderer_->DrawString(subtitle, sw/2.0f - subtitleW/2.0f, panelY + 172.0f, 0.45f, {0.76f, 0.9f, 1.0f, 1.0f});

    const float optionH = 72.0f;
    const float optionPaddingX = 80.0f;
    
    for (int i = 0; i < kOptionCount; ++i) {
        const bool selected = selectedIndex_ == i;
        const float textW = renderer_->MeasureTextWidth(kOptionLabels[i], 1.0f);
        const float boxW = textW + optionPaddingX;
        const float boxX = sw * 0.5f - boxW * 0.5f;
        const float boxY = sh * kOptionY[i] - 14.0f;
        drawRect(boxX - 4.0f, boxY - 4.0f, boxW + 8.0f, optionH + 8.0f,
            selected ? Engine::Vector4{1.0f, 0.9f, 0.25f, 0.95f} : Engine::Vector4{0.18f, 0.28f, 0.42f, 0.9f}, 3);
        drawRect(boxX, boxY, boxW, optionH,
            selected ? Engine::Vector4{0.18f, 0.20f, 0.12f, 0.96f} : Engine::Vector4{0.09f, 0.12f, 0.18f, 0.95f}, 4);
        renderer_->DrawString(kOptionLabels[i], sw * 0.5f - textW * 0.5f,
            sh * kOptionY[i], 1.0f,
            selected ? Engine::Vector4{1, 1, 0, 1} : Engine::Vector4{0.82f, 0.86f, 0.92f, 1});
    }
    
    float guideW = renderer_->MeasureTextWidth("Use W/S or D-Pad / Mouse to Select, Enter/A/Click to Confirm", 0.5f);
    renderer_->DrawString("Use W/S or D-Pad / Mouse to Select, Enter/A/Click to Confirm", sw/2.0f - guideW/2.0f, sh * 0.89f, 0.5f, {0.78f, 0.84f, 0.96f, 1});
}

void SelectScene::DrawEditor() {
}

} // namespace Game
