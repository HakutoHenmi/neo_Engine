#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "LoadingScreen.h"
#include "LoadProfile.h"
#include "Input.h"
#include "Renderer.h"
#include "WindowDX.h"
#include <Windows.h>
#include <Xinput.h>
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "xinput.lib")

namespace Engine {
namespace {
using Clock = std::chrono::steady_clock;

struct Expectation { float units = 0, seconds = 0; };

// Input gathered by the main thread's message pump for the loading thread.
struct QueuedInput {
	std::mutex mutex;
	int step = 0;
	bool clicked = false, padMode = false;
	float clickX = 0, clickY = 0, mouseX = -1, mouseY = -1;
};

struct Context {
	LoadingScreen::Drawer drawer;
	LoadingScreen::State state, last;
	int depth = 0;
	bool quit = false;
	std::atomic<int> units{0};
	std::atomic<bool> finished{false}, stop{false}, skipped{false};
	Expectation expected;
	float minimumSeconds = 0, skippableAfter = 0;
	Clock::time_point start, lastPump;
	std::thread worker;
	std::thread::id mainThread;
	QueuedInput input;
	std::map<std::string, Expectation> history;
	bool historyLoaded = false;
	// Longest time the loading thread could not show a frame, and what the main thread was doing then.
	std::atomic<float> longestStallMs{0};
	std::atomic<int> frames{0};
	// Longest time the bar gained less than 1% (what the player reads as "stuck").
	std::atomic<float> longestFlatMs{0};
	float flatFrom = 0, flatProgress = 0;
};

Context& Get() {
	static Context context;
	return context;
}

// Load sizes are remembered per scene so the bar tracks the real work on the next run.
std::filesystem::path HistoryPath() {
	wchar_t buffer[32768]{};
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
	return n > 0 && n < 32768 ? std::filesystem::path(buffer) / L"LiquidTime" / L"loading-v2.txt" : std::filesystem::path{};
}

void LoadHistory(Context& c) {
	if (c.historyLoaded) return;
	c.historyLoaded = true;
	try {
		std::ifstream file(HistoryPath());
		std::string scene;
		Expectation e;
		while (file >> scene >> e.units >> e.seconds)
			if (e.units > 0 && e.seconds > 0) c.history[scene] = e;
	} catch (...) {}
}

void SaveHistory(const Context& c) {
	try {
		auto path = HistoryPath();
		if (path.empty()) return;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path);
		for (const auto& [scene, e] : c.history) file << scene << ' ' << e.units << ' ' << e.seconds << '\n';
	} catch (...) {}
}

// First-run guesses; afterwards the measured size of each load is used.
Expectation DefaultExpectation(const std::string& scene) {
	if (scene == "Startup") return {60, 1.5f};
	// Units are loader steps (shaders, meshes, GPU waits) and are the same on every PC; the seconds
	// only seed the time half of the bar until this PC's own load has been measured once.
	// Measured in Release with the shipped shader cache (2026-11-13): Game+Engine 258 units in 1.9 s.
	if (scene.find("+Engine") != std::string::npos) return {260, 3.f};
	if (scene == "Game") return {110, 2.f};
	return {20, 1.f};
}

bool SkipAllowed(const Context& c) {
	return std::chrono::duration<float>(Clock::now() - c.start).count() >= c.skippableAfter;
}

// Main thread: dispatch window messages and queue the input the loading screen uses.
void PumpMessages(Context& c) {
	MSG msg{};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) { c.quit = true; continue; }
		const bool key = msg.message == WM_KEYDOWN && !(msg.lParam & (1 << 30));
		const bool click = msg.message == WM_LBUTTONDOWN;
		if (key || click || msg.message == WM_MOUSEMOVE) {
			RECT rc{};
			GetClientRect(msg.hwnd, &rc);
			const float w = float(std::max<LONG>(1, rc.right - rc.left)), h = float(std::max<LONG>(1, rc.bottom - rc.top));
			float x = float(short(LOWORD(msg.lParam))) * float(WindowDX::kW) / w;
			float y = float(short(HIWORD(msg.lParam))) * float(WindowDX::kH) / h;
			bool inside = true;
			if (!key) {
				if (auto* renderer = Renderer::GetInstance()) inside = renderer->MapLoadingPointer(x, y);
				if (!inside && click) { TranslateMessage(&msg); DispatchMessage(&msg); continue; }
				if (!inside) x = y = -1;
			}
			std::lock_guard<std::mutex> lock(c.input.mutex);
			if (key) {
				if (msg.wParam == VK_LEFT || msg.wParam == 'A') c.input.step = -1;
				if (msg.wParam == VK_RIGHT || msg.wParam == 'D') c.input.step = 1;
				c.input.padMode = false;
				c.skipped = SkipAllowed(c);
			} else if (click) {
				c.input.clicked = true;
				c.input.clickX = x;
				c.input.clickY = y;
				c.input.padMode = false;
				c.skipped = SkipAllowed(c);
			} else {
				c.input.mouseX = x;
				c.input.mouseY = y;
			}
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void UpdateProgress(Context& c, LoadingScreen::State& s) {
	if (c.finished) { s.progress = 1; s.finished = true; return; }
	// Blend completed work with elapsed time: a single slow step still moves the bar.
	// Units (loader steps) are the same on every PC, seconds are not, so units lead; each half is capped
	// so a PC slower than expected cannot park the bar on the time half while work is left.
	const float byUnits = c.expected.units > 0 ? std::min(float(c.units) / c.expected.units, 1.f) : 0.f;
	const float bySeconds = c.expected.seconds > 0 ? std::min(s.elapsed / c.expected.seconds, 1.f) : 0.f;
	s.progress = std::max(s.progress, std::clamp(0.7f * byUnits + 0.3f * bySeconds, 0.f, 0.97f));
}

// Loading thread: a steady 60 fps regardless of what the main thread is doing.
void Worker(Context& c) {
	auto* renderer = Renderer::GetInstance();
	renderer->BeginUIThread();
	LoadingScreen::State& s = c.state;
	int pad = -1, scanDelay = 0;
	WORD previousButtons = 0;
	float previousStickX = 0;
	auto lastFrame = Clock::now(), next = lastFrame;
	constexpr auto frame = std::chrono::microseconds(16667);
	while (!c.stop) {
		const auto now = Clock::now();
		s.dt = std::min(std::chrono::duration<float>(now - lastFrame).count(), 0.1f);
		s.elapsed = std::chrono::duration<float>(now - c.start).count();
		lastFrame = now;
		{
			std::lock_guard<std::mutex> lock(c.input.mutex);
			s.step = c.input.step;
			s.clicked = c.input.clicked;
			s.clickX = c.input.clickX;
			s.clickY = c.input.clickY;
			s.mouseX = c.input.mouseX;
			s.mouseY = c.input.mouseY;
			if (c.input.step || c.input.clicked) s.padMode = c.input.padMode;
			c.input.step = 0;
			c.input.clicked = false;
		}
		// XInput has no message queue; poll it here so pad presses are never tied to the main thread.
		XINPUT_STATE xs{};
		if (pad >= 0 && XInputGetState(DWORD(pad), &xs) != ERROR_SUCCESS) pad = -1;
		if (pad < 0 && scanDelay-- <= 0) {
			scanDelay = 60;
			for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
				if (XInputGetState(i, &xs) == ERROR_SUCCESS) { pad = int(i); previousButtons = xs.Gamepad.wButtons; break; }
		}
		if (pad >= 0) {
			const WORD buttons = xs.Gamepad.wButtons, pressed = WORD(buttons & ~previousButtons);
			const float stick = float(xs.Gamepad.sThumbLX) / 32767.f;
			if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_LEFT_SHOULDER) || (stick < -.6f && previousStickX >= -.6f)) s.step = -1;
			if (pressed & (XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_RIGHT_SHOULDER) || (stick > .6f && previousStickX <= .6f)) s.step = 1;
			if (pressed || std::abs(stick) > .6f) { s.padMode = true; if (SkipAllowed(c)) c.skipped = true; }
			previousButtons = buttons;
			previousStickX = stick;
		}
		UpdateProgress(c, s);
		const auto drawStart = Clock::now();
		const std::string stepBefore = LoadProfile::CurrentStep();
		renderer->RenderUIFrame([&] { c.drawer(s); });
		++c.frames;
		// A frame that takes far longer than 16 ms means the bar visibly froze: log what the main thread was doing.
		const float frameMs = std::chrono::duration<float, std::milli>(Clock::now() - drawStart).count();
		if (frameMs > 100.f) {
			const std::string stepAfter = LoadProfile::CurrentStep();
			LoadProfile::Log("stall", "loading frame blocked at " + std::to_string(int(s.progress * 100)) + "% while main: " +
				(stepBefore.empty() ? std::string("(no profiled step)") : stepBefore) + (stepAfter != stepBefore ? " -> " + stepAfter : std::string()), frameMs);
		}
		if (frameMs > c.longestStallMs) c.longestStallMs = frameMs;
		if (s.progress >= c.flatProgress + 0.01f) { c.flatProgress = s.progress; c.flatFrom = s.elapsed; }
		const float flatMs = (s.elapsed - c.flatFrom) * 1000.f;
		if (flatMs > c.longestFlatMs) c.longestFlatMs = flatMs;
		if (LoadProfile::Enabled() && c.frames % 6 == 0)
			LoadProfile::Record("bar", std::to_string(int(s.progress * 1000) / 10.f).substr(0, 4) + "% units=" + std::to_string(c.units.load()) + " main: " + LoadProfile::CurrentStep(), s.elapsed * 1000.0);
		next += frame;
		const auto after = Clock::now();
		if (next < after) next = after;
		std::this_thread::sleep_until(next);
	}
}
} // namespace

void LoadingScreen::SetDrawer(Drawer drawer) { Get().drawer = std::move(drawer); }

void LoadingScreen::Begin(const std::string& scene, bool startup) {
	auto& c = Get();
	// A scene loaded during startup is part of the startup load.
	if (c.depth++ > 0) return;
	LoadHistory(c);
	c.mainThread = std::this_thread::get_id();
	LoadProfile::SetMainThread();
	c.units = 0;
	c.longestStallMs = 0;
	c.longestFlatMs = 0;
	c.flatFrom = c.flatProgress = 0;
	c.frames = 0;
	c.finished = c.stop = c.skipped = false;
	c.state = {};
	c.state.scene = scene;
	c.state.startup = startup;
	if (auto* input = Input::GetInstance()) c.state.padMode = input->UsingGamepad();
	{
		std::lock_guard<std::mutex> lock(c.input.mutex);
		c.input.step = 0;
		c.input.clicked = false;
		c.input.mouseX = c.input.mouseY = -1;
		c.input.padMode = c.state.padMode;
	}
	// Keep a brief brand splash without adding three seconds to every launch.
	c.minimumSeconds = startup ? 0.6f : 0.f;
	c.skippableAfter = startup ? 0.3f : 0.f;
	auto found = c.history.find(scene);
	c.expected = found != c.history.end() ? found->second : DefaultExpectation(scene);
	c.start = c.lastPump = Clock::now();

	auto* renderer = Renderer::GetInstance();
	if (!c.drawer || !renderer || !renderer->UIReady()) return;
	// Warm-up frame on the main thread: every texture / glyph the drawer may need
	// is cached here, so the loading thread never touches the loaders.
	renderer->PrepareEditorLoadingFrame();
	renderer->SetRecordUITextures(true);
	LoadingScreen::State warm = c.state;
	warm.warmup = true;
	renderer->RenderUIFrame([&] { c.drawer(warm); });
	renderer->SetRecordUITextures(false);
	renderer->SetUIThreadActive(true);
	timeBeginPeriod(1);
	c.worker = std::thread([&c] { Worker(c); });
}

void LoadingScreen::Pump() {
	auto& c = Get();
	if (c.depth == 0 || std::this_thread::get_id() != c.mainThread) return;
	++c.units;
	const auto now = Clock::now();
	if (now - c.lastPump < std::chrono::milliseconds(8)) return;
	c.lastPump = now;
	PumpMessages(c);
}

void LoadingScreen::End() {
	auto& c = Get();
	if (c.depth == 0 || --c.depth > 0) return;
	const float loadSeconds = std::chrono::duration<float>(Clock::now() - c.start).count();
	c.finished = true;
	if (c.worker.joinable()) {
		// Hold a short splash until its minimum time, or until the player skips it.
		while (!c.skipped && std::chrono::duration<float>(Clock::now() - c.start).count() < c.minimumSeconds) {
			PumpMessages(c);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		c.stop = true;
		c.worker.join();
		timeEndPeriod(1);
		if (auto* renderer = Renderer::GetInstance()) {
			renderer->SetUIThreadActive(false);
			renderer->FinishEditorLoadingFrame();
		}
	}
	c.state.progress = 1;
	c.state.finished = true;
	c.state.elapsed = std::chrono::duration<float>(Clock::now() - c.start).count();
	c.last = c.state;
	LoadProfile::Log("load", c.state.scene + " frames=" + std::to_string(c.frames.load()) + " longestFrameMs=" +
		std::to_string(int(c.longestStallMs.load())) + " longestFlatBarMs=" + std::to_string(int(c.longestFlatMs.load())) +
		" units=" + std::to_string(c.units.load()) + "/" + std::to_string(int(c.expected.units)) + " expectedS=" + std::to_string(c.expected.seconds).substr(0, 5) + " firstRun=" + (c.history.count(c.state.scene) ? "0" : "1"), loadSeconds * 1000.0);
	if (loadSeconds > 0.05f) {
		c.history[c.state.scene] = {float(std::max(c.units.load(), 1)), loadSeconds};
		SaveHistory(c);
	}
	if (c.quit) PostQuitMessage(0);
	c.quit = false;
}

bool LoadingScreen::Active() { return Get().depth > 0; }

const LoadingScreen::State& LoadingScreen::Last() { return Get().last; }

} // namespace Engine
