#include "Input.h"
#include "WindowDX.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

namespace Engine {

Input* Input::instance_ = nullptr;

namespace {
// 円形デッドゾーンを除いた範囲を 0〜1 に再マップする
void ApplyDeadzone(SHORT rawX, SHORT rawY, SHORT deadzone, float& outX, float& outY) {
	float x = static_cast<float>(rawX) / 32767.0f;
	float y = static_cast<float>(rawY) / 32767.0f;
	float length = std::sqrt(x * x + y * y);
	float dead = static_cast<float>(deadzone) / 32767.0f;
	if (length <= dead) {
		outX = outY = 0.0f;
		return;
	}
	float scaled = (std::min)(1.0f, (length - dead) / (1.0f - dead));
	outX = x / length * scaled;
	outY = y / length * scaled;
}
} // namespace

void Input::Initialize(HINSTANCE hInst, HWND hwnd) {
	instance_ = this;
	hwnd_ = hwnd;
	HRESULT hr;

	// --- DirectInput本体 ---
	hr = DirectInput8Create(hInst, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)di_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "DirectInput8Create failed");

	// --- Keyboard ---
	hr = di_->CreateDevice(GUID_SysKeyboard, kb_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "Keyboard CreateDevice failed");

	hr = kb_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr) && "Keyboard SetDataFormat failed");

	hr = kb_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr) && "Keyboard SetCooperativeLevel failed");

	kb_->Acquire();

	// --- Mouse ---
	hr = di_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(), nullptr);
	assert(SUCCEEDED(hr) && "Mouse CreateDevice failed");

	hr = mouse_->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr) && "Mouse SetDataFormat failed");

	hr = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr) && "Mouse SetCooperativeLevel failed");

	mouse_->Acquire();

	// 初期化
	ZeroMemory(keyState_, 256);
	ZeroMemory(prevKey_, 256);
	ZeroMemory(&mouseState_, sizeof(mouseState_));
	ZeroMemory(&prevMouseState_, sizeof(prevMouseState_));
	mouseX_ = mouseY_ = wheel_ = 0.0f;
	absMouseX_ = absMouseY_ = 0.0f;
}

void Input::Update() {
	// --- Keyboard更新 ---
	memcpy(prevKey_, keyState_, 256);
	if (FAILED(kb_->GetDeviceState(256, keyState_))) {
		kb_->Acquire();
		kb_->GetDeviceState(256, keyState_);
	}

	// --- Mouse更新 ---
	prevMouseState_ = mouseState_;
	if (FAILED(mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
		mouse_->Acquire();
		mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
	}

	// マウスの移動差分
	mouseX_ = static_cast<float>(mouseState_.lX);
	mouseY_ = static_cast<float>(mouseState_.lY);

	// ★絶対座標の更新（OSカーソルと同期し、内部解像度にスケーリング）
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(hwnd_, &pt);

	// クライアント領域のサイズを取得してスケーリング
	RECT rc;
	GetClientRect(hwnd_, &rc);
	float clientW = static_cast<float>(rc.right - rc.left);
	float clientH = static_cast<float>(rc.bottom - rc.top);

	if (clientW > 0 && clientH > 0) {
		absMouseX_ = static_cast<float>(pt.x) * (static_cast<float>(WindowDX::kW) / clientW);
		absMouseY_ = static_cast<float>(pt.y) * (static_cast<float>(WindowDX::kH) / clientH);
	} else {
		absMouseX_ = static_cast<float>(pt.x);
		absMouseY_ = static_cast<float>(pt.y);
	}

	// ホイール量（上:+、下:-）
	wheel_ = static_cast<float>(mouseState_.lZ) / WHEEL_DELTA;

	UpdatePad();

	// --- 最後に触ったデバイスを判定 ---
	bool keyboardMouse = std::abs(mouseX_) + std::abs(mouseY_) > 4.0f || std::abs(wheel_) > 0.0f;
	for (int i = 0; i < 256 && !keyboardMouse; ++i)
		keyboardMouse = (keyState_[i] & 0x80) && !(prevKey_[i] & 0x80);
	for (int i = 0; i < 8 && !keyboardMouse; ++i)
		keyboardMouse = IsMouseTrigger(i);
	bool gamepad = (padButtons_ & ~prevPadButtons_) != 0 ||
		std::abs(padLX_) + std::abs(padLY_) + std::abs(padRX_) + std::abs(padRY_) > 0.3f;
	const bool wasGamepad = usingGamepad_;
	if (keyboardMouse)
		usingGamepad_ = false;
	else if (gamepad)
		usingGamepad_ = true;
	if (!PadConnected())
		usingGamepad_ = false;
	if (wasGamepad != usingGamepad_)
		WindowDX::RefreshCursor();
	++frame_;
}

void Input::UpdatePad() {
	prevPadButtons_ = padButtons_;
	padButtons_ = 0;
	padLX_ = padLY_ = padRX_ = padRY_ = 0.0f;

	// 未接続スロットの問い合わせは重いので、未接続時は約1秒ごとに探す
	XINPUT_STATE state{};
	if (padIndex_ >= 0 && XInputGetState(static_cast<DWORD>(padIndex_), &state) != ERROR_SUCCESS)
		padIndex_ = -1;
	if (padIndex_ < 0) {
		if (padScanDelay_-- > 0)
			return;
		padScanDelay_ = 60;
		for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
			if (XInputGetState(i, &state) == ERROR_SUCCESS) {
				padIndex_ = static_cast<int>(i);
				break;
			}
		}
		if (padIndex_ < 0) {
			padLatch_ = 0;
			return;
		}
	}

	const XINPUT_GAMEPAD& pad = state.Gamepad;
	padButtons_ = pad.wButtons;
	if (pad.bLeftTrigger > 64)
		padButtons_ |= kPadLT;
	if (pad.bRightTrigger > 64)
		padButtons_ |= kPadRT;
	ApplyDeadzone(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, padLX_, padLY_);
	ApplyDeadzone(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, padRX_, padRY_);

	// 左スティックの方向ボタン（ヒステリシス付き）
	auto stick = [&](uint32_t bit, float value) {
		bool held = (prevPadButtons_ & bit) ? value > 0.35f : value > 0.6f;
		if (held)
			padButtons_ |= bit;
	};
	stick(kPadLsUp, padLY_);
	stick(kPadLsDown, -padLY_);
	stick(kPadLsLeft, -padLX_);
	stick(kPadLsRight, padLX_);

	// ラッチは対象ボタンを離したら解除
	padLatch_ &= padButtons_;
}

void Input::Shutdown() {
	if (kb_)
		kb_->Unacquire();
	if (mouse_)
		mouse_->Unacquire();
	kb_.Reset();
	mouse_.Reset();
	di_.Reset();
	instance_ = nullptr;
}

} // namespace Engine
