#pragma once
#include "../../Engine/Input.h"
#include <algorithm>
#include <cmath>

// Gameplay actions shared by keyboard / mouse and an Xbox controller.
//   Move    : WASD          / Left stick (analog)
//   Camera  : Mouse         / Right stick
//   Fire    : Left click    / RT or X
//   Dodge   : Right click   / B or RB
//   Jump    : Space         / A
//   Lock on : Middle click  / RS click or LB
//   Pause   : Escape        / Menu
namespace Game::Controls {
inline bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
inline bool Pad(uint32_t buttons) {
    auto* input = Engine::Input::GetInstance();
    return input && input->PadDown(buttons);
}
inline bool Fire() { return Key(VK_LBUTTON) || Pad(Engine::Input::kPadRT | XINPUT_GAMEPAD_X); }
inline bool Dodge() { return Key(VK_RBUTTON) || Pad(XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_RIGHT_SHOULDER); }
inline bool Jump() { return Key(VK_SPACE) || Pad(XINPUT_GAMEPAD_A); }
inline bool LockOn() { return Key(VK_MBUTTON) || Pad(XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_LEFT_SHOULDER); }
inline bool Absorb() { return Key('F') || Pad(XINPUT_GAMEPAD_X); }
inline bool MorphAttack() { return Key(VK_LBUTTON) || Pad(Engine::Input::kPadRT); }

// x: right, y: forward. Keyboard is digital; the stick keeps its analog length (max 1).
inline void Move(float& x, float& y) {
    x = float(Key('D')) - float(Key('A'));
    y = float(Key('W')) - float(Key('S'));
    if (auto* input = Engine::Input::GetInstance()) {
        x += input->PadLX();
        y += input->PadLY();
    }
    float length = std::sqrt(x * x + y * y);
    if (length > 1) { x /= length; y /= length; }
}

// Right stick camera rate in radians for this frame (yaw right +, pitch down +).
inline void Look(float dt, float& yaw, float& pitch) {
    yaw = pitch = 0;
    auto* input = Engine::Input::GetInstance();
    if (!input) return;
    // Squared response keeps small corrections precise while full tilt still turns quickly.
    float rx = input->PadRX(), ry = input->PadRY();
    yaw = rx * std::abs(rx) * 3.2f * dt;
    pitch = -ry * std::abs(ry) * 2.0f * dt;
}
} // namespace Game::Controls
