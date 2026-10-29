#pragma once
// ===============================
//  Input : DirectInput (Keyboard + Mouse) + XInput (Gamepad)
// ===============================
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>
#include <cstdint>

namespace Engine {

class Input {
public:
	// XINPUT_GAMEPAD_* のボタンに加え、トリガーと左スティックの方向をボタンとして扱う
	static constexpr uint32_t kPadLT = 1u << 16;
	static constexpr uint32_t kPadRT = 1u << 17;
	static constexpr uint32_t kPadLsUp = 1u << 18;
	static constexpr uint32_t kPadLsDown = 1u << 19;
	static constexpr uint32_t kPadLsLeft = 1u << 20;
	static constexpr uint32_t kPadLsRight = 1u << 21;

	void Initialize(HINSTANCE hInst, HWND hwnd);
	void Update();
	void Shutdown();

	static Input* GetInstance() { return instance_; }

	// ===== Keyboard =====
	bool Down(BYTE k) const { return (keyState_[k] & 0x80) != 0; }
	bool Trigger(BYTE k) const { return Down(k) && !(prevKey_[k] & 0x80); }

	// ===== Mouse =====
	float GetMouseDeltaX() const { return mouseX_; }
	float GetMouseDeltaY() const { return mouseY_; }
	float GetMouseWheelDelta() const { return wheel_; }

	// ★追加: 絶対座標とボタン状態
	void GetMousePos(float& x, float& y) const { x = absMouseX_; y = absMouseY_; }
	// エディタの Game ビュー (クライアント座標の矩形) に画面を表示している間、
	// マウス座標をその矩形基準で内部解像度へ換算する。Clear で通常 (ウィンドウ全体) に戻す
	void SetPointerViewport(float x, float y, float width, float height) {
		viewportX_ = x; viewportY_ = y; viewportW_ = width; viewportH_ = height; useViewport_ = width > 0 && height > 0;
	}
	void ClearPointerViewport() { useViewport_ = false; }
	bool IsMouseDown(int button) const { return (mouseState_.rgbButtons[button] & 0x80) != 0; }
	bool IsMouseTrigger(int button) const { return IsMouseDown(button) && !(prevMouseState_.rgbButtons[button] & 0x80); }

	// ===== Gamepad (XInput) =====
	bool PadConnected() const { return padIndex_ >= 0; }
	bool PadDown(uint32_t button) const { return (padButtons_ & ~padLatch_ & button) != 0; }
	bool PadTrigger(uint32_t button) const { return PadDown(button) && !(prevPadButtons_ & button); }
	// スティックはデッドゾーン補正済みの -1〜1（上が +Y）
	float PadLX() const { return padLX_; }
	float PadLY() const { return padLY_; }
	float PadRX() const { return padRX_; }
	float PadRY() const { return padRY_; }
	// 押されているボタンを離すまで無効化する（メニュー決定がゲーム操作へ漏れないように）
	void LatchPad() { padLatch_ |= padButtons_; }
	// 振動 (0〜1。low は左の重いモーター、high は右の軽いモーター)。値が変わったときだけ送る
	void SetPadVibration(float low, float high);

	// 最後に操作されたデバイスがゲームパッドか。UIの表示切り替えに使う
	bool UsingGamepad() const { return usingGamepad_; }
	uint64_t FrameIndex() const { return frame_; }

private:
	void UpdatePad();

	// --- DirectInput Core ---
	Microsoft::WRL::ComPtr<IDirectInput8> di_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> kb_;
	Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

	// --- Keyboard ---
	BYTE keyState_[256]{};
	BYTE prevKey_[256]{};

	// --- Mouse ---
	DIMOUSESTATE2 mouseState_{};
	DIMOUSESTATE2 prevMouseState_{};

	float mouseX_ = 0.0f;
	float mouseY_ = 0.0f;
	float absMouseX_ = 0.0f; // ★追加
	float absMouseY_ = 0.0f; // ★追加
	bool useViewport_ = false;
	float viewportX_ = 0.0f, viewportY_ = 0.0f, viewportW_ = 0.0f, viewportH_ = 0.0f;
	float wheel_ = 0.0f;
	HWND hwnd_ = nullptr; // ★追加

	// --- Gamepad ---
	int padIndex_ = -1;
	int padScanDelay_ = 0;
	uint32_t padButtons_ = 0;
	uint32_t prevPadButtons_ = 0;
	uint32_t padLatch_ = 0;
	float padLX_ = 0.0f, padLY_ = 0.0f, padRX_ = 0.0f, padRY_ = 0.0f;
	bool usingGamepad_ = false;
	WORD vibrationLow_ = 0, vibrationHigh_ = 0;
	int vibrationPad_ = -1;
	uint64_t frame_ = 0;

	static Input* instance_;
};

} // namespace Engine
