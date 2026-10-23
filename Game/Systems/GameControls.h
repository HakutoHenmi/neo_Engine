// GameControls.h
// ゲームの操作 (キーボード/マウスと Xbox コントローラーの共通の入力)。
#pragma once
#include "../../Engine/Input.h"
#include <algorithm>
#include <cmath>

// キーボード/マウスと Xbox コントローラーで共通のゲーム操作
//   移動     : WASD          / 左スティック (アナログ)
//   カメラ   : マウス        / 右スティック
//   射撃     : 左クリック    / RT または X
//   回避     : 右クリック    / B または RB
//   ジャンプ : Space         / A
//   ロックオン: 中クリック   / 右スティック押し込み または LB
//   ポーズ   : Esc           / メニュー
namespace Game::Controls {
// キーが押されているか (Windows の非同期キー状態) / ゲームパッドのボタンが押されているか
inline bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
inline bool Pad(uint32_t buttons) {
    auto* input = Engine::Input::GetInstance();
    return input && input->PadDown(buttons);
}
// 各操作が押されているか (射撃・回避・ジャンプ・ロックオン・吸収・変形攻撃・スライムに戻る)
inline bool Fire() { return Key(VK_LBUTTON) || Pad(Engine::Input::kPadRT | XINPUT_GAMEPAD_X); }
inline bool Dodge() { return Key(VK_RBUTTON) || Pad(XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_RIGHT_SHOULDER); }
inline bool Jump() { return Key(VK_SPACE) || Pad(XINPUT_GAMEPAD_A); }
inline bool LockOn() { return Key(VK_MBUTTON) || Pad(XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_LEFT_SHOULDER); }
inline bool Absorb() { return Key(VK_RBUTTON) || Pad(XINPUT_GAMEPAD_X); }
inline bool MorphAttack() { return Key(VK_LBUTTON) || Pad(Engine::Input::kPadRT); }
inline bool ReturnToSlime() { return Key('R') || Pad(XINPUT_GAMEPAD_B); }

// x: 右, y: 前。キーボードは 0/1、スティックはアナログの長さのまま (最大 1)
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

// 右スティックによるこのフレームのカメラの回転量 (ラジアン。ヨーは右が +、ピッチは下が +)
inline void Look(float dt, float& yaw, float& pitch) {
    yaw = pitch = 0;
    auto* input = Engine::Input::GetInstance();
    if (!input) return;
    // 2乗の応答にして、小さな調整は正確に、大きく倒したときは素早く回す
    float rx = input->PadRX(), ry = input->PadRY();
    yaw = rx * std::abs(rx) * 3.2f * dt;
    pitch = -ry * std::abs(ry) * 2.0f * dt;
}
} // namespace Game::Controls
