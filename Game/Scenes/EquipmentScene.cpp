#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "EquipmentScene.h"
#include "../CanLoadout.h"
#include "../../Engine/Input.h"
#include "../../Engine/SceneManager.h"
#include <Windows.h>
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <vector>
#pragma comment(lib, "xinput.lib")

namespace Game {

const EquipmentScene::CanOption EquipmentScene::canOptions_[EquipmentScene::kCanCount] = {
    {CanType::Fire, "FIRE CAN", "Attack flame", {0.95f, 0.22f, 0.12f, 1.0f}},
    {CanType::Water, "WATER CAN", "Liquid move", {0.18f, 0.55f, 1.0f, 1.0f}},
    {CanType::Thunder, "THUNDER CAN", "Burst power", {1.0f, 0.84f, 0.16f, 1.0f}},
    {CanType::Soda, "SODA CAN", "Air boost", {0.08f, 0.82f, 0.66f, 1.0f}},
    {CanType::Ice, "ICE CAN", "Freeze / Slide", {0.38f, 0.88f, 1.0f, 1.0f}},
    {CanType::Magnet, "MAGNET CAN", "Push / Pull", {0.78f, 0.32f, 1.0f, 1.0f}},
    {CanType::Acid, "ACID CAN", "Corrode / Pool", {0.55f, 1.0f, 0.16f, 1.0f}},
    {CanType::Bubble, "BUBBLE CAN", "Trap / Shield", {1.0f, 0.40f, 0.78f, 1.0f}},
};

const char* EquipmentScene::buttonLabels_[EquipmentScene::kButtonCount] = {
    "BACK",
    "CLEAR",
    "GAME START",
};

EquipmentScene::~EquipmentScene() = default;

void EquipmentScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    (void)params;
    dx_ = dx;
    renderer_ = Engine::Renderer::GetInstance();
    if (renderer_) {
        whiteTexture_ = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
        canPatternTexture_ = renderer_->LoadTexture2D("Resources/Textures/ball.png");
    }
    equipped_ = CanLoadout::GetEquipped();
    selectedIndex_ = 0;

    if (renderer_) {
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

    Engine::WindowDX::SetCursorVisible(true);
}

void EquipmentScene::Update() {
    auto* input = Engine::Input::GetInstance();
    if (!input) return;

    if (input->Trigger(0xCB) || input->Trigger(0x1E)) MoveHorizontal(-1); // Left / A
    if (input->Trigger(0xCD) || input->Trigger(0x20)) MoveHorizontal(1);  // Right / D
    if (input->Trigger(0xC8) || input->Trigger(0x11)) MoveVertical(-1);   // Up / W
    if (input->Trigger(0xD0) || input->Trigger(0x1F)) MoveVertical(1);    // Down / S

    if (input->Trigger(0x01)) {
        Engine::SceneManager::GetInstance()->RequestChange("Select");
        return;
    }
    if (input->Trigger(0x1C) || input->Trigger(0x39)) {
        ConfirmSelection();
        return;
    }

    float mx = 0.0f;
    float my = 0.0f;
    input->GetMousePos(mx, my);
    for (int i = 0; i < kCanCount; ++i) {
        if (PointInRect(mx, my, CanRect(i))) {
            selectedIndex_ = i;
            if (input->IsMouseTrigger(0)) {
                ConfirmSelection();
                return;
            }
        }
    }
    for (int i = 0; i < kButtonCount; ++i) {
        if (PointInRect(mx, my, ButtonRect(i))) {
            selectedIndex_ = kCanCount + i;
            if (input->IsMouseTrigger(0)) {
                ConfirmSelection();
                return;
            }
        }
    }

    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS) {
        const float normLX = (std::max)(-1.0f, static_cast<float>(state.Gamepad.sThumbLX) / 32767.0f);
        const float normLY = (std::max)(-1.0f, static_cast<float>(state.Gamepad.sThumbLY) / 32767.0f);
        const float lx = std::abs(normLX) < 0.2f ? 0.0f : normLX;
        const float ly = std::abs(normLY) < 0.2f ? 0.0f : normLY;

        const bool currA = (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        if (currA && !prevA_) {
            ConfirmSelection();
            prevA_ = currA;
            return;
        }
        prevA_ = currA;

        const bool currB = (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        if (currB && !prevB_) {
            Engine::SceneManager::GetInstance()->RequestChange("Select");
            prevB_ = currB;
            return;
        }
        prevB_ = currB;

        const bool currLeft = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        if (currLeft && !prevDpadLeft_) MoveHorizontal(-1);
        prevDpadLeft_ = currLeft;

        const bool currRight = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        if (currRight && !prevDpadRight_) MoveHorizontal(1);
        prevDpadRight_ = currRight;

        const bool currUp = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        if (currUp && !prevDpadUp_) MoveVertical(-1);
        prevDpadUp_ = currUp;

        const bool currDown = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        if (currDown && !prevDpadDown_) MoveVertical(1);
        prevDpadDown_ = currDown;

        if (lx < -0.55f && !stickLeft_) {
            MoveHorizontal(-1);
            stickLeft_ = true;
        } else if (lx >= -0.55f) {
            stickLeft_ = false;
        }

        if (lx > 0.55f && !stickRight_) {
            MoveHorizontal(1);
            stickRight_ = true;
        } else if (lx <= 0.55f) {
            stickRight_ = false;
        }

        if (ly > 0.55f && !stickUp_) {
            MoveVertical(-1);
            stickUp_ = true;
        } else if (ly <= 0.55f) {
            stickUp_ = false;
        }

        if (ly < -0.55f && !stickDown_) {
            MoveVertical(1);
            stickDown_ = true;
        } else if (ly >= -0.55f) {
            stickDown_ = false;
        }
    }
}

void EquipmentScene::Draw() {
    DrawEquipmentScreen();
}

void EquipmentScene::DrawUI() {
}

void EquipmentScene::DrawEquipmentScreen() {
    if (!renderer_) return;

    const float sw = static_cast<float>(Engine::WindowDX::kW);
    const float sh = static_cast<float>(Engine::WindowDX::kH);
    const int equippedCount = EquippedCount();

    DrawRect(0.0f, 0.0f, sw, sh, {0.035f, 0.045f, 0.07f, 1.0f}, 0);
    DrawRect(0.0f, 0.0f, sw, 142.0f, {0.10f, 0.12f, 0.20f, 1.0f}, 1);
    DrawRect(250.0f, 110.0f, 1420.0f, 860.0f, {0.075f, 0.085f, 0.13f, 0.96f}, 1);
    DrawRect(250.0f, 110.0f, 1420.0f, 6.0f, {0.18f, 0.78f, 0.98f, 1.0f}, 2);
    DrawRect(250.0f, 964.0f, 1420.0f, 6.0f, {1.0f, 0.33f, 0.62f, 1.0f}, 2);

    DrawCenteredText("CAN EQUIPMENT / 装備", sw * 0.5f, 142.0f, 1.20f, {1.0f, 0.95f, 0.55f, 1.0f});
    DrawCenteredText("クリックで装備 / もう一度クリックで外す", sw * 0.5f, 206.0f, 0.48f, {0.88f, 0.94f, 1.0f, 1.0f});

    DrawRect(1350.0f, 246.0f, 220.0f, 48.0f, {0.05f, 0.20f, 0.14f, 0.92f}, 3);
    DrawRect(1350.0f, 246.0f, 10.0f, 48.0f, {0.22f, 1.0f, 0.58f, 1.0f}, 4);
    DrawCenteredText("EQUIPPED " + std::to_string(equippedCount) + " / 4", 1465.0f, 259.0f, 0.34f, {0.8f, 1.0f, 0.84f, 1.0f});

    DrawCenteredText("SLOTS", sw * 0.5f, 268.0f, 0.55f, {0.7f, 0.9f, 1.0f, 1.0f});
    const float slotW = 220.0f;
    const float slotH = 142.0f;
    const float slotGap = 24.0f;
    const float slotStartX = sw * 0.5f - (slotW * 4.0f + slotGap * 3.0f) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float x = slotStartX + static_cast<float>(i) * (slotW + slotGap);
        const float y = 306.0f;
        const CanType can = equipped_[i];
        DrawRect(x - 4.0f, y - 4.0f, slotW + 8.0f, slotH + 8.0f, {0.22f, 0.32f, 0.48f, 0.95f}, 3);
        DrawRect(x, y, slotW, slotH, can == CanType::None ? Engine::Vector4{0.08f, 0.09f, 0.13f, 1.0f} : Engine::Vector4{0.10f, 0.14f, 0.18f, 1.0f}, 4);
        if (can != CanType::None) {
            const auto& opt = canOptions_[static_cast<int>(can) - 1];
            DrawRect(x, y, slotW, 8.0f, opt.color, 5);
        }
        DrawCenteredText("SLOT " + std::to_string(i + 1), x + slotW * 0.5f, y + 10.0f, 0.35f, {0.62f, 0.74f, 0.92f, 1.0f});
        if (can == CanType::None) {
            DrawRect(x + 70.0f, y + 52.0f, 80.0f, 8.0f, {0.28f, 0.32f, 0.42f, 1.0f}, 5);
            DrawRect(x + 106.0f, y + 16.0f, 8.0f, 80.0f, {0.28f, 0.32f, 0.42f, 1.0f}, 5);
            DrawCenteredText("EMPTY", x + slotW * 0.5f, y + 100.0f, 0.36f, {0.45f, 0.5f, 0.62f, 1.0f});
        } else {
            const auto& opt = canOptions_[static_cast<int>(can) - 1];
            DrawCanIcon(can, x + slotW * 0.5f, y + 74.0f, 42.0f, 70.0f, opt.color, false);
            DrawCenteredText(opt.name, x + slotW * 0.5f, y + 112.0f, 0.32f, {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }

    DrawCenteredText("INVENTORY", sw * 0.5f, 472.0f, 0.50f, {0.7f, 0.9f, 1.0f, 1.0f});
    for (int i = 0; i < kCanCount; ++i) {
        const Rect r = CanRect(i);
        const bool selected = selectedIndex_ == i;
        const bool equipped = IsEquipped(canOptions_[i].type);
        const Engine::Vector4 border = selected
            ? Engine::Vector4{1.0f, 0.95f, 0.30f, 1.0f}
            : equipped
            ? Engine::Vector4{0.28f, 0.95f, 0.55f, 0.95f}
            : Engine::Vector4{0.18f, 0.25f, 0.38f, 0.9f};
        if (selected) {
            DrawRect(r.x - 16.0f, r.y - 16.0f, r.w + 32.0f, r.h + 32.0f, {1.0f, 0.86f, 0.18f, 0.26f}, 4);
        }
        DrawRect(r.x - 6.0f, r.y - 6.0f, r.w + 12.0f, r.h + 12.0f, border, 5);
        DrawRect(r.x, r.y, r.w, r.h, equipped ? Engine::Vector4{0.10f, 0.18f, 0.15f, 1.0f} : Engine::Vector4{0.11f, 0.12f, 0.18f, 1.0f}, 6);
        DrawRect(r.x, r.y, r.w, 12.0f, canOptions_[i].color, 7);
        if (equipped) {
            DrawRect(r.x + r.w - 10.0f, r.y + 12.0f, 10.0f, r.h - 12.0f, {0.17f, 0.80f, 0.35f, 1.0f}, 8);
        }

        DrawCanIcon(canOptions_[i].type, r.x + 48.0f, r.y + 76.0f, 42.0f, 72.0f, canOptions_[i].color, selected);
        DrawCenteredText(canOptions_[i].name, r.x + 164.0f, r.y + 32.0f, 0.34f, {1.0f, 1.0f, 1.0f, 1.0f});
        DrawCenteredText(canOptions_[i].detail, r.x + 164.0f, r.y + 66.0f, 0.25f, {0.78f, 0.84f, 0.95f, 1.0f});
        DrawRect(r.x + 92.0f, r.y + 104.0f, r.w - 108.0f, 30.0f,
            equipped ? Engine::Vector4{0.12f, 0.45f, 0.24f, 1.0f} : Engine::Vector4{0.18f, 0.28f, 0.48f, 1.0f}, 8);
        DrawCenteredText(equipped ? "REMOVE" : "EQUIP", r.x + 164.0f, r.y + 110.0f, 0.25f,
            equipped ? Engine::Vector4{0.76f, 1.0f, 0.82f, 1.0f} : Engine::Vector4{0.86f, 0.92f, 1.0f, 1.0f});
    }

    for (int i = 0; i < kButtonCount; ++i) {
        const Rect r = ButtonRect(i);
        const bool selected = selectedIndex_ == kCanCount + i;
        Engine::Vector4 fill = {0.18f, 0.20f, 0.30f, 1.0f};
        Engine::Vector4 edge = selected ? Engine::Vector4{1.0f, 0.95f, 0.30f, 1.0f} : Engine::Vector4{0.28f, 0.36f, 0.52f, 1.0f};
        if (i == 0) fill = {0.62f, 0.24f, 0.12f, 1.0f};
        if (i == 1) fill = {0.72f, 0.22f, 0.48f, 1.0f};
        if (i == 2) fill = {0.12f, 0.62f, 0.30f, 1.0f};

        DrawRect(r.x - 5.0f, r.y - 5.0f, r.w + 10.0f, r.h + 10.0f, edge, 8);
        DrawRect(r.x, r.y, r.w, r.h, fill, 9);
        DrawCenteredText(buttonLabels_[i], r.x + r.w * 0.5f, r.y + 20.0f, i == 2 ? 0.62f : 0.68f, {1.0f, 1.0f, 1.0f, 1.0f});
        DrawCenteredText(i == 0 ? "セレクトへ戻る" : i == 1 ? "装備を空にする" : "この装備で開始",
            r.x + r.w * 0.5f, r.y + 58.0f, 0.28f, {0.92f, 0.94f, 1.0f, 1.0f});
    }

    DrawCenteredText("Mouse: hover and click   Keyboard/Pad: move and confirm", sw * 0.5f, 1002.0f, 0.36f, {0.75f, 0.8f, 0.9f, 1.0f});
}

void EquipmentScene::DrawEditor() {
}

EquipmentScene::Rect EquipmentScene::CanRect(int index) const {
    const float sw = static_cast<float>(Engine::WindowDX::kW);
    const float cardW = 260.0f;
    const float cardH = 142.0f;
    const float gap = 24.0f;
    const int column = index % kCanColumns;
    const int row = index / kCanColumns;
    const float startX = sw * 0.5f - (cardW * static_cast<float>(kCanColumns) + gap * static_cast<float>(kCanColumns - 1)) * 0.5f;
    return {startX + static_cast<float>(column) * (cardW + gap), 510.0f + static_cast<float>(row) * 164.0f, cardW, cardH};
}

EquipmentScene::Rect EquipmentScene::ButtonRect(int index) const {
    const float y = 864.0f;
    const float w = index == 2 ? 330.0f : 260.0f;
    const float h = 86.0f;
    if (index == 0) return {350.0f, y, w, h};
    if (index == 1) return {830.0f, y, w, h};
    return {1310.0f, y, w, h};
}

bool EquipmentScene::PointInRect(float x, float y, const Rect& rect) const {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

bool EquipmentScene::IsEquipped(CanType type) const {
    return std::find(equipped_.begin(), equipped_.end(), type) != equipped_.end();
}

int EquipmentScene::EquippedCount() const {
    return static_cast<int>(std::count_if(equipped_.begin(), equipped_.end(), [](CanType can) {
        return can != CanType::None;
    }));
}

void EquipmentScene::ToggleCan(CanType type) {
    auto it = std::find(equipped_.begin(), equipped_.end(), type);
    if (it != equipped_.end()) {
        *it = CanType::None;
        return;
    }

    for (auto& slot : equipped_) {
        if (slot == CanType::None) {
            slot = type;
            return;
        }
    }
}

void EquipmentScene::ClearEquipped() {
    equipped_.fill(CanType::None);
}

void EquipmentScene::SaveAndStart() {
    CanLoadout::SetEquipped(equipped_);
    Engine::SceneManager::GetInstance()->RequestChange("Game");
}

void EquipmentScene::ConfirmSelection() {
    if (selectedIndex_ < kCanCount) {
        ToggleCan(canOptions_[selectedIndex_].type);
        return;
    }

    const int buttonIndex = selectedIndex_ - kCanCount;
    if (buttonIndex == 0) {
        Engine::SceneManager::GetInstance()->RequestChange("Select");
    } else if (buttonIndex == 1) {
        ClearEquipped();
    } else {
        SaveAndStart();
    }
}

void EquipmentScene::MoveHorizontal(int dir) {
    if (selectedIndex_ < kCanCount) {
        const int row = selectedIndex_ / kCanColumns;
        int column = selectedIndex_ % kCanColumns;
        column = (column + dir + kCanColumns) % kCanColumns;
        selectedIndex_ = row * kCanColumns + column;
        return;
    }

    int buttonIndex = selectedIndex_ - kCanCount;
    buttonIndex = (buttonIndex + dir + kButtonCount) % kButtonCount;
    selectedIndex_ = kCanCount + buttonIndex;
}

void EquipmentScene::MoveVertical(int dir) {
    if (selectedIndex_ < kCanCount) {
        const int row = selectedIndex_ / kCanColumns;
        const int column = selectedIndex_ % kCanColumns;
        if (dir > 0) {
            selectedIndex_ = row == 0 ? selectedIndex_ + kCanColumns : kCanCount + (std::min)(column, kButtonCount - 1);
        } else if (row > 0) {
            selectedIndex_ -= kCanColumns;
        }
    } else if (dir < 0) {
        const int buttonIndex = selectedIndex_ - kCanCount;
        selectedIndex_ = kCanColumns + (buttonIndex == kButtonCount - 1 ? kCanColumns - 1 : buttonIndex);
    }
}

void EquipmentScene::DrawRect(float x, float y, float w, float h, const Engine::Vector4& color, int layer) {
    DrawSpriteRect(whiteTexture_, x, y, w, h, color, layer);
}

void EquipmentScene::DrawSpriteRect(Engine::Renderer::TextureHandle texture, float x, float y, float w, float h, const Engine::Vector4& color, int layer, float rotationRad) {
    Engine::Renderer::SpriteDesc desc;
    desc.x = x;
    desc.y = y;
    desc.w = w;
    desc.h = h;
    desc.rotationRad = rotationRad;
    desc.color = color;
    desc.layer = layer;
    renderer_->DrawSprite(texture, desc);
}

void EquipmentScene::DrawCenteredText(const std::string& text, float centerX, float y, float scale, const Engine::Vector4& color) {
    const float w = renderer_->MeasureTextWidth(text, scale);
    renderer_->DrawString(text, centerX - w * 0.5f, y, scale, color);
}

void EquipmentScene::DrawCanIcon(CanType type, float centerX, float centerY, float w, float h, const Engine::Vector4& color, bool selected) {
    const float x = centerX - w * 0.5f;
    const float y = centerY - h * 0.5f;
    const Engine::Vector4 glow = selected ? Engine::Vector4{1.0f, 0.95f, 0.35f, 0.9f} : Engine::Vector4{0.85f, 0.9f, 1.0f, 0.55f};
    DrawRect(x - 7.0f, y - 7.0f, w + 14.0f, h + 14.0f, glow, 20);
    DrawRect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, {0.03f, 0.04f, 0.08f, 1.0f}, 21);
    DrawRect(x, y, w, h, color, 21);
    DrawSpriteRect(canPatternTexture_, x + 5.0f, y + 20.0f, w - 10.0f, h - 38.0f, {1.0f, 1.0f, 1.0f, 0.28f}, 22);
    DrawRect(x + 4.0f, y + 8.0f, w - 8.0f, 10.0f, {1.0f, 1.0f, 1.0f, 0.35f}, 22);
    DrawRect(x + 5.0f, y + h - 16.0f, w - 10.0f, 10.0f, {0.0f, 0.0f, 0.0f, 0.18f}, 22);
    DrawRect(x + w * 0.34f, y - 8.0f, w * 0.32f, 8.0f, {0.82f, 0.86f, 0.92f, 1.0f}, 23);
    DrawRect(x + w * 0.42f, y - 14.0f, w * 0.16f, 7.0f, {0.56f, 0.60f, 0.68f, 1.0f}, 24);

    const float sx = x + w * 0.5f;
    const float sy = y + h * 0.48f;
    switch (type) {
    case CanType::Fire:
        DrawRect(sx - 5.0f, sy - 22.0f, 10.0f, 44.0f, {1.0f, 0.90f, 0.18f, 1.0f}, 25);
        DrawSpriteRect(whiteTexture_, sx - 16.0f, sy - 2.0f, 14.0f, 30.0f, {1.0f, 0.50f, 0.06f, 1.0f}, 25, -0.45f);
        DrawSpriteRect(whiteTexture_, sx + 2.0f, sy - 8.0f, 14.0f, 34.0f, {1.0f, 0.16f, 0.08f, 0.95f}, 25, 0.40f);
        break;
    case CanType::Water:
        DrawRect(sx - 10.0f, sy - 18.0f, 20.0f, 42.0f, {0.64f, 0.92f, 1.0f, 1.0f}, 25);
        DrawSpriteRect(whiteTexture_, sx - 16.0f, sy + 4.0f, 32.0f, 20.0f, {0.18f, 0.66f, 1.0f, 0.95f}, 26, 0.78f);
        break;
    case CanType::Thunder:
        DrawSpriteRect(whiteTexture_, sx - 11.0f, sy - 26.0f, 18.0f, 46.0f, {1.0f, 0.96f, 0.18f, 1.0f}, 25, 0.45f);
        DrawSpriteRect(whiteTexture_, sx - 3.0f, sy - 4.0f, 18.0f, 46.0f, {1.0f, 0.68f, 0.04f, 1.0f}, 26, 0.45f);
        break;
    case CanType::Soda:
        DrawRect(sx - 18.0f, sy - 18.0f, 12.0f, 12.0f, {0.82f, 1.0f, 0.94f, 0.95f}, 25);
        DrawRect(sx + 6.0f, sy - 12.0f, 10.0f, 10.0f, {0.82f, 1.0f, 0.94f, 0.95f}, 25);
        DrawRect(sx - 4.0f, sy + 9.0f, 14.0f, 14.0f, {0.82f, 1.0f, 0.94f, 0.95f}, 25);
        DrawRect(sx - 15.0f, sy + 25.0f, 9.0f, 9.0f, {0.82f, 1.0f, 0.94f, 0.85f}, 25);
        break;
    case CanType::Ice:
        DrawSpriteRect(whiteTexture_, sx - 5.0f, sy - 25.0f, 10.0f, 48.0f, {0.82f, 0.98f, 1.0f, 1.0f}, 25, 0.35f);
        DrawSpriteRect(whiteTexture_, sx - 15.0f, sy - 3.0f, 10.0f, 30.0f, {0.20f, 0.72f, 1.0f, 1.0f}, 26, -0.55f);
        DrawSpriteRect(whiteTexture_, sx + 7.0f, sy + 2.0f, 8.0f, 24.0f, {0.55f, 0.92f, 1.0f, 1.0f}, 26, 0.65f);
        break;
    case CanType::Magnet:
        DrawRect(sx - 17.0f, sy - 20.0f, 9.0f, 38.0f, {1.0f, 0.35f, 0.45f, 1.0f}, 25);
        DrawRect(sx + 8.0f, sy - 20.0f, 9.0f, 38.0f, {0.38f, 0.58f, 1.0f, 1.0f}, 25);
        DrawRect(sx - 17.0f, sy + 10.0f, 34.0f, 9.0f, {0.88f, 0.90f, 1.0f, 1.0f}, 26);
        break;
    case CanType::Acid:
        DrawSpriteRect(whiteTexture_, sx - 12.0f, sy - 20.0f, 24.0f, 42.0f, {0.70f, 1.0f, 0.10f, 1.0f}, 25, 0.72f);
        DrawRect(sx - 17.0f, sy + 20.0f, 34.0f, 7.0f, {0.24f, 0.62f, 0.06f, 1.0f}, 26);
        break;
    case CanType::Bubble:
        DrawSpriteRect(canPatternTexture_, sx - 18.0f, sy - 18.0f, 26.0f, 26.0f, {0.92f, 0.78f, 1.0f, 0.95f}, 25);
        DrawSpriteRect(canPatternTexture_, sx + 5.0f, sy - 6.0f, 18.0f, 18.0f, {0.50f, 0.92f, 1.0f, 0.95f}, 25);
        DrawSpriteRect(canPatternTexture_, sx - 7.0f, sy + 14.0f, 14.0f, 14.0f, {1.0f, 0.52f, 0.82f, 0.90f}, 25);
        break;
    default:
        break;
    }
}

} // namespace Game
