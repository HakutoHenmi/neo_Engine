// Exercise the real input and menu code without a window, GPU or controller.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>
#include <cstdint>
// Only expose Input's state to inject deterministic controller frames.
#define private public
#include "../Engine/Input.h"
#undef private
#include "../Game/UI/CreditsUI.h"
#include <cassert>
#include <crtdbg.h>
#include <iostream>

namespace Engine {
Input* Input::instance_ = nullptr;
Audio* Audio::GetInstance() { return nullptr; }
uint32_t Audio::Load(const std::string&) { return 0; }
size_t Audio::Play(uint32_t, bool, float, float) { return 0; }
}

int main() {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    using namespace Game::UI;
    Engine::Input input;
    Engine::Input::instance_ = &input;
    input.usingGamepad_ = true;
    input.padIndex_ = 0;
    Canvas ui(nullptr);
    auto frame = [&](uint32_t buttons) {
        input.prevPadButtons_ = input.padButtons_;
        input.padButtons_ = buttons;
        input.padLatch_ &= buttons;
        ++input.frame_;
    };

    // A mouse-only back hint must never become the default A-button target.
    const Game::UI::Rect back{1112,40,128,36};
    Nav() = Navigator{};
    frame(0);
    assert(!ui.MouseClick(back));
    frame(XINPUT_GAMEPAD_A);
    assert(!ui.MouseClick(back));
    assert(PadPressed(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START));
    assert(Nav().current.empty());

    // Mouse-only controls retain mouse hit testing and click behavior.
    input.usingGamepad_ = false;
    input.mouseState_.rgbButtons[0] = 0x80;
    input.prevMouseState_.rgbButtons[0] = 0;
    ui.SetPointer(0,0);
    assert(ui.MouseClick({-1,-1,2,2}));
    assert(!ui.MouseClick(back));
    input.usingGamepad_ = true;

    // Holding one alternative must not hide a new press of another.
    frame(XINPUT_GAMEPAD_START);
    frame(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A);
    assert(input.PadTrigger(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A));
    input.LatchPad();
    assert(!input.PadTrigger(XINPUT_GAMEPAD_A));
    frame(XINPUT_GAMEPAD_START);
    frame(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A);
    assert(input.PadTrigger(XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_A));

    // Credits accept both stick directions, do not close on A, and close on B.
    Nav() = Navigator{};
    int page = 0;
    frame(0);
    assert(!UpdateCredits(ui,page));
    frame(Engine::Input::kPadLsRight);
    assert(!UpdateCredits(ui,page) && page == 1);
    frame(0);
    frame(Engine::Input::kPadLsLeft);
    assert(!UpdateCredits(ui,page) && page == 0);
    frame(0);
    frame(XINPUT_GAMEPAD_A);
    assert(!UpdateCredits(ui,page));
    frame(0);
    frame(XINPUT_GAMEPAD_B);
    assert(UpdateCredits(ui,page));

    // Directional focus selects retry/title, and one A press fires once.
    Nav() = Navigator{};
    frame(0);
    ui.Track(Scale::ResultNext,true);
    ui.Track(Scale::ResultRetry);
    ui.Track(Scale::ResultTitle);
    frame(XINPUT_GAMEPAD_DPAD_DOWN);
    assert(!ui.Click(Scale::ResultNext));
    assert(ui.Focused(Scale::ResultTitle));
    ui.Track(Scale::ResultRetry);
    ui.Track(Scale::ResultTitle);
    frame(XINPUT_GAMEPAD_DPAD_LEFT);
    assert(!ui.Click(Scale::ResultNext));
    assert(ui.Focused(Scale::ResultRetry));
    assert(!ui.Click(Scale::ResultRetry));
    ui.Track(Scale::ResultTitle);
    frame(0);
    ui.Track(Scale::ResultNext,true);
    ui.Track(Scale::ResultRetry);
    ui.Track(Scale::ResultTitle);
    frame(XINPUT_GAMEPAD_A);
    assert(!ui.Click(Scale::ResultNext));
    assert(ui.Click(Scale::ResultRetry));
    assert(!ui.Click(Scale::ResultRetry));
    assert(!input.PadDown(XINPUT_GAMEPAD_A));
    ui.Track(Scale::ResultTitle);
    frame(XINPUT_GAMEPAD_DPAD_RIGHT);
    assert(!ui.Click(Scale::ResultNext));
    assert(!ui.Click(Scale::ResultRetry));
    assert(ui.Focused(Scale::ResultTitle));
    assert(!ui.Click(Scale::ResultTitle));
    frame(XINPUT_GAMEPAD_A);
    assert(!ui.Click(Scale::ResultNext));
    assert(!ui.Click(Scale::ResultRetry));
    assert(ui.Click(Scale::ResultTitle));

    // Without a next stage, A defaults to retry; holding it never repeats.
    Nav() = Navigator{};
    frame(0);
    ui.Track(Scale::ResultRetryOnly,true);
    ui.Track(Scale::ResultTitleOnly);
    frame(XINPUT_GAMEPAD_A);
    assert(ui.Click(Scale::ResultRetryOnly));
    ui.Track(Scale::ResultTitleOnly);
    frame(XINPUT_GAMEPAD_A);
    assert(!ui.Click(Scale::ResultRetryOnly));
    assert(!ui.Click(Scale::ResultTitleOnly));

    // The actual pause menu supports stick navigation and A activation.
    Nav() = Navigator{};
    int menu = 0;
    frame(0);
    assert(Scale::UpdatePause(ui,menu) == -1);
    frame(Engine::Input::kPadLsDown);
    assert(Scale::UpdatePause(ui,menu) == -1 && menu == 1);
    frame(XINPUT_GAMEPAD_A);
    assert(Scale::UpdatePause(ui,menu) == 1);

    Engine::Input::instance_ = nullptr;
    std::cout << "PASS: controller shortcuts, mouse-only hints, credits, result/pause focus and single activation\n";
}
