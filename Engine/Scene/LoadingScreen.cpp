#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "LoadingScreen.h"
#include "../Core/LoadProfile.h"
#include "../Core/Input.h"
#include "../Graphics/Renderer.h"
#include "../Core/WindowDX.h"
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

struct Expectation {
	float units = 0, seconds = 0;
};

// メインスレッドのメッセージ処理で集めた、ロード用スレッド向けの入力。
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
	// ロード用スレッドがフレームを出せなかった最長の時間と、そのときメインスレッドがしていたこと。
	std::atomic<float> longestStallMs{0};
	std::atomic<int> frames{0};
	// バーの伸びが 1% 未満だった最長の時間 (プレイヤーには「止まった」と見える)。
	std::atomic<float> longestFlatMs{0};
	float flatFrom = 0, flatProgress = 0;
};

Context& Get() {
	static Context context;
	return context;
}

// ロードの量はシーンごとに覚え、次回はバーが実際の作業に沿って進むようにする。
std::filesystem::path HistoryPath() {
	wchar_t buffer[32768]{};
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
	return n > 0 && n < 32768 ? std::filesystem::path(buffer) / L"LiquidTime" / L"loading-v2.txt"
	                          : std::filesystem::path{};
}

void LoadHistory(Context& c) {
	if (c.historyLoaded)
		return;
	c.historyLoaded = true;
	try {
		std::ifstream file(HistoryPath());
		std::string scene;
		Expectation e;
		while (file >> scene >> e.units >> e.seconds)
			if (e.units > 0 && e.seconds > 0)
				c.history[scene] = e;
	} catch (...) {
		// 履歴を利用できない場合は、シーンごとの初期見積もりを使う。
	}
}

void SaveHistory(const Context& c) {
	try {
		auto path = HistoryPath();
		if (path.empty())
			return;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path);
		for (const auto& [scene, e] : c.history)
			file << scene << ' ' << e.units << ' ' << e.seconds << '\n';
	} catch (...) {
		// 次回用の見積もり保存に失敗しても、現在のロードは継続する。
	}
}

// 初回の見積もり。以降はロードごとに測った量を使う。
Expectation DefaultExpectation(const std::string& scene) {
	if (scene == "Startup")
		return {60, 1.5f};
	// 単位はローダーの手順 (シェーダー・メッシュ・GPU 待ち) で、どの PC でも同じ。秒は
	// この PC のロードを一度測るまで、バーの時間の半分の初期値にだけ使う。
	// リリース版・同梱のシェーダーキャッシュで計測 (2026-11-13): Game+Engine で 258 単位・1.9 秒。
	if (scene.find("+Engine") != std::string::npos)
		return {260, 3.f};
	if (scene == "Game")
		return {110, 2.f};
	return {20, 1.f};
}

bool SkipAllowed(const Context& c) {
	return std::chrono::duration<float>(Clock::now() - c.start).count() >= c.skippableAfter;
}

// メインスレッド: ウィンドウのメッセージを処理し、ロード画面が使う入力をためる。
void PumpMessages(Context& c) {
	MSG msg{};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			c.quit = true;
			continue;
		}
		const bool key = msg.message == WM_KEYDOWN && !(msg.lParam & (1 << 30));
		const bool click = msg.message == WM_LBUTTONDOWN;
		if (key || click || msg.message == WM_MOUSEMOVE) {
			RECT rc{};
			GetClientRect(msg.hwnd, &rc);
			const float w = float(std::max<LONG>(1, rc.right - rc.left)),
			            h = float(std::max<LONG>(1, rc.bottom - rc.top));
			float x = float(short(LOWORD(msg.lParam))) * float(WindowDX::kW) / w;
			float y = float(short(HIWORD(msg.lParam))) * float(WindowDX::kH) / h;
			bool inside = true;
			if (!key) {
				if (auto* renderer = Renderer::GetInstance())
					inside = renderer->MapLoadingPointer(x, y);
				if (!inside && click) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
					continue;
				}
				if (!inside)
					x = y = -1;
			}
			std::lock_guard<std::mutex> lock(c.input.mutex);
			if (key) {
				if (msg.wParam == VK_LEFT || msg.wParam == 'A')
					c.input.step = -1;
				if (msg.wParam == VK_RIGHT || msg.wParam == 'D')
					c.input.step = 1;
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
	if (c.finished) {
		s.progress = 1;
		s.finished = true;
		return;
	}
	// 終わった作業と経過時間を混ぜる: 遅い手順が 1 つあってもバーは進む。
	// 単位 (ローダーの手順) はどの PC でも同じだが秒は違うので、単位を主にする。それぞれの半分に上限を設け、
	// 想定より遅い PC でも、作業が残っているのに時間の半分でバーが止まらないようにする。
	const float byUnits = c.expected.units > 0 ? std::min(float(c.units) / c.expected.units, 1.f) : 0.f;
	const float bySeconds = c.expected.seconds > 0 ? std::min(s.elapsed / c.expected.seconds, 1.f) : 0.f;
	s.progress = std::max(s.progress, std::clamp(0.7f * byUnits + 0.3f * bySeconds, 0.f, 0.97f));
}

// ロード用スレッド: メインスレッドが何をしていても一定の 60 fps で描く。
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
			if (c.input.step || c.input.clicked)
				s.padMode = c.input.padMode;
			c.input.step = 0;
			c.input.clicked = false;
		}
		// XInput にはメッセージの待ち行列が無い。ここで状態を読み、パッドの入力がメインスレッドに縛られないようにする。
		XINPUT_STATE xs{};
		if (pad >= 0 && XInputGetState(DWORD(pad), &xs) != ERROR_SUCCESS)
			pad = -1;
		if (pad < 0 && scanDelay-- <= 0) {
			scanDelay = 60;
			for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
				if (XInputGetState(i, &xs) == ERROR_SUCCESS) {
					pad = int(i);
					previousButtons = xs.Gamepad.wButtons;
					break;
				}
		}
		if (pad >= 0) {
			const WORD buttons = xs.Gamepad.wButtons, pressed = WORD(buttons & ~previousButtons);
			const float stick = float(xs.Gamepad.sThumbLX) / 32767.f;
			if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_LEFT_SHOULDER) ||
			    (stick < -.6f && previousStickX >= -.6f))
				s.step = -1;
			if (pressed & (XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_RIGHT_SHOULDER) ||
			    (stick > .6f && previousStickX <= .6f))
				s.step = 1;
			if (pressed || std::abs(stick) > .6f) {
				s.padMode = true;
				if (SkipAllowed(c))
					c.skipped = true;
			}
			previousButtons = buttons;
			previousStickX = stick;
		}
		UpdateProgress(c, s);
		const auto drawStart = Clock::now();
		const std::string stepBefore = LoadProfile::CurrentStep();
		renderer->RenderUIFrame([&] { c.drawer(s); });
		++c.frames;
		// 16 ms を大きく超えたフレームはバーが目に見えて止まったということ: そのときメインスレッドがしていたことを記録する。
		const float frameMs = std::chrono::duration<float, std::milli>(Clock::now() - drawStart).count();
		if (frameMs > 100.f) {
			const std::string stepAfter = LoadProfile::CurrentStep();
			LoadProfile::Log(
			    "stall",
			    "loading frame blocked at " + std::to_string(int(s.progress * 100)) +
			        "% while main: " + (stepBefore.empty() ? std::string("(no profiled step)") : stepBefore) +
			        (stepAfter != stepBefore ? " -> " + stepAfter : std::string()),
			    frameMs);
		}
		if (frameMs > c.longestStallMs)
			c.longestStallMs = frameMs;
		if (s.progress >= c.flatProgress + 0.01f) {
			c.flatProgress = s.progress;
			c.flatFrom = s.elapsed;
		}
		const float flatMs = (s.elapsed - c.flatFrom) * 1000.f;
		if (flatMs > c.longestFlatMs)
			c.longestFlatMs = flatMs;
		if (LoadProfile::Enabled() && c.frames % 6 == 0)
			LoadProfile::Record(
			    "bar",
			    std::to_string(int(s.progress * 1000) / 10.f).substr(0, 4) +
			        "% units=" + std::to_string(c.units.load()) + " main: " + LoadProfile::CurrentStep(),
			    s.elapsed * 1000.0);
		next += frame;
		const auto after = Clock::now();
		if (next < after)
			next = after;
		std::this_thread::sleep_until(next);
	}
}
} // namespace

void LoadingScreen::SetDrawer(Drawer drawer) {
	Get().drawer = std::move(drawer);
}

void LoadingScreen::Begin(const std::string& scene, bool startup) {
	auto& c = Get();
	// 起動中に読み込んだシーンは起動時のロードの一部として扱う。
	if (c.depth++ > 0)
		return;
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
	if (auto* input = Input::GetInstance())
		c.state.padMode = input->UsingGamepad();
	{
		std::lock_guard<std::mutex> lock(c.input.mutex);
		c.input.step = 0;
		c.input.clicked = false;
		c.input.mouseX = c.input.mouseY = -1;
		c.input.padMode = c.state.padMode;
	}
	// 起動のたびに 3 秒を足さずに、短いロゴ画面だけ見せる。
	c.minimumSeconds = startup ? 0.6f : 0.f;
	c.skippableAfter = startup ? 0.3f : 0.f;
	auto found = c.history.find(scene);
	c.expected = found != c.history.end() ? found->second : DefaultExpectation(scene);
	c.start = c.lastPump = Clock::now();

	auto* renderer = Renderer::GetInstance();
	if (!c.drawer || !renderer || !renderer->UIReady())
		return;
	// メインスレッドでの準備用フレーム: 描画で使いうるテクスチャ / 文字をすべてここで
	// キャッシュするので、ロード用スレッドはローダーに一切触れない。
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
	if (c.depth == 0 || std::this_thread::get_id() != c.mainThread)
		return;
	++c.units;
	const auto now = Clock::now();
	if (now - c.lastPump < std::chrono::milliseconds(8))
		return;
	c.lastPump = now;
	PumpMessages(c);
}

void LoadingScreen::End() {
	auto& c = Get();
	if (c.depth == 0 || --c.depth > 0)
		return;
	const float loadSeconds = std::chrono::duration<float>(Clock::now() - c.start).count();
	c.finished = true;
	if (c.worker.joinable()) {
		// 短いロゴ画面を最低時間まで、またはプレイヤーが飛ばすまで表示し続ける。
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
	LoadProfile::Log(
	    "load",
	    c.state.scene + " frames=" + std::to_string(c.frames.load()) +
	        " longestFrameMs=" + std::to_string(int(c.longestStallMs.load())) + " longestFlatBarMs=" +
	        std::to_string(int(c.longestFlatMs.load())) + " units=" + std::to_string(c.units.load()) + "/" +
	        std::to_string(int(c.expected.units)) + " expectedS=" + std::to_string(c.expected.seconds).substr(0, 5) +
	        " firstRun=" + (c.history.count(c.state.scene) ? "0" : "1"),
	    loadSeconds * 1000.0);
	if (loadSeconds > 0.05f) {
		c.history[c.state.scene] = {float(std::max(c.units.load(), 1)), loadSeconds};
		SaveHistory(c);
	}
	if (c.quit)
		PostQuitMessage(0);
	c.quit = false;
}

bool LoadingScreen::Active() {
	return Get().depth > 0;
}

const LoadingScreen::State& LoadingScreen::Last() {
	return Get().last;
}

} // namespace Engine
