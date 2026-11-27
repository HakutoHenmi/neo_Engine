#include "App.h"
#include "StartupTiming.h"
#include <string_view>
#include <future>
#include <Windows.h>
#include <chrono>
#include <fstream>
#include "../../tests/StartupValidation.h"
#include "JobSystem.h"
#include "../Scene/LoadingScreen.h"
#include "../Time/TimeManager.h"
#include "NetworkProfiler.h"
#include <psapi.h> // メモリ使用量取得用
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dxguid.lib")

namespace Engine {

#include <cstdio>

static void LogFile(const char* msg) {
	if (std::string_view(msg).starts_with("Step ") || std::string_view(msg).starts_with("App::Initialize"))
		StartupTiming::Mark(msg);
	FILE* f = nullptr;
	fopen_s(&f, "error_log.txt", "a");
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

bool App::Initialize(HINSTANCE hInst, int cmdShow) {
	StartupTiming::Mark("Audio initialize/preload (parallel)");
	// 下の get() までは、このワーカーだけが音声に触る。初期化が途中で失敗しても
	// future が合流するので、App とその音声メンバーが破棄される前に必ず終わる。
	auto audioStartup = std::async(std::launch::async, [this] {
		if (!audio_.Initialize())
			return false;
		if (initialSceneKey_ == startupMusicScene_ && !startupMusicPath_.empty())
			audio_.PreloadBGM(startupMusicPath_);
		return true;
	});
	LogFile("Step 1: dx_.Initialize");
	sceneManager_.SetDX(&dx_);
	if (!dx_.Initialize(hInst, cmdShow, hwnd_))
		return false;
	if (startFullscreen_ && !dx_.IsFullscreen())
		dx_.ToggleFullscreen();

	LogFile("Step 2: JobSystem::Initialize");
	JobSystem::Initialize();

	// Web プロファイラー (Tools/WebProfiler) の HTTP サーバー。ゲームはネットワークを使わないので、
	// 配布する Release (NDEBUG) では --web-profiler を付けたときだけ開く (Debug / Development では常に開く)。
	// どちらもこの PC の中 (127.0.0.1) だけで待ち受け、Windows のネットワーク許可の画面を出さない
#ifdef NDEBUG
	const bool webProfiler = wcsstr(GetCommandLineW(), L"--web-profiler") != nullptr;
#else
	const bool webProfiler = true;
#endif
	if (webProfiler) {
		LogFile("Step 3: NetworkProfiler::Initialize");
		NetworkProfiler::GetInstance().Initialize(8080);
	}

	// 起動時のロード画面を操作できるよう、入力はレンダラーより先に初期化する。
	LogFile("Step 5: input_.Initialize");
	input_.Initialize(hInst, hwnd_);
	LogFile("Step 4: renderer_.Initialize");
	// ここで作るのはレンダラーの土台 (2D スプライト / 文字) だけ。3D パイプラインは
	// 最初のワールドシーンのロード画面の裏でコンパイルする。残りの起動処理は
	// 起動時のスプラッシュ画面で覆う。
	if (!renderer_.Initialize(&dx_))
		return false;

	LogFile("Step 6: camera_.Initialize");
	camera_.Initialize();

#ifdef USE_IMGUI
	LogFile("Step 8: imgui_.Initialize");
	uint32_t imguiSrvIdx = renderer_.AllocateTextSrvIndex();
	if (!imgui_.Initialize(
	        hwnd_, dx_, dx_.SRV(), dx_.SRV_CPU((int)imguiSrvIdx), dx_.SRV_GPU((int)imguiSrvIdx), 18.0f,
	        "Resources/Textures/fonts/Huninn/Huninn-Regular.ttf")) {
		return false;
	}
#endif

	LogFile("Step 9: registrar_");
	if (registrar_) {
		registrar_(sceneManager_, dx_);
	}

	StartupTiming::Mark("Audio join");
	if (!audioStartup.get())
		return false;
	LogFile("Step 10: initialSceneKey Change");
	if (!initialSceneKey_.empty()) {
		sceneManager_.Change(initialSceneKey_);
	} else if (sceneManager_.Has("FPS")) {
		sceneManager_.Change("FPS");
	} else {
		const std::string first = sceneManager_.FirstRegisteredName();
		if (!first.empty())
			sceneManager_.Change(first);
	}

	LogFile("App::Initialize complete");
	return true;
}

void App::Run() {
	StartupValidation startupValidation;
	MSG msg{};
	bool running = true;

	auto prevTime = std::chrono::high_resolution_clock::now();
	int frameCount = 0;
	float timeElapsed = 0.0f;

	int debugFrameCount = 0;
	LoadProfile::SetMainThread();
	while (running) {
		{
			char buf[128];
			if (debugFrameCount < 5 || debugFrameCount % 60 == 0) {
				sprintf_s(buf, "=== Frame %d ===", debugFrameCount);
				LogFile(buf);
			}
			debugFrameCount++;
		}
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (!running)
			break;

		try {
			input_.Update();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in input_.Update():");
			LogFile(e.what());
			OutputDebugStringA("EXCEPTION in input_.Update(): ");
			OutputDebugStringA(e.what());
			OutputDebugStringA("\n");
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in input_.Update()");
			throw;
		}

		// フルスクリーン切り替え (F11 or Alt+Enter)
		if (input_.Trigger(DIK_F11) || (input_.Down(DIK_LALT) && input_.Trigger(DIK_RETURN))) {
			dx_.ToggleFullscreen();
		}

		// フレームタイムの計算
		auto currentTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration<float>(currentTime - prevTime).count();
		prevTime = currentTime;
		{
			// 予算を大きく超えたフレームは目に見えるカクつきになる: 何に時間がかかったかを残す (常に記録。LoadProfile.h 参照)。
			const std::string slow = LoadProfile::TakeSlowSteps();
			if (dt > 0.1f && debugFrameCount > 2)
				LoadProfile::Log(
				    "hitch",
				    sceneManager_.CurrentName() + (slow.empty() ? std::string(" (no profiled step)") : " | " + slow),
				    dt * 1000.0);
		}

		// FPSの計算
		frameCount++;
		timeElapsed += dt;
		if (timeElapsed >= 1.0f) {
			NetworkProfiler::GetInstance().SetFPS((float)frameCount / timeElapsed);
			frameCount = 0;
			timeElapsed = 0.0f;
		}

		// TimeManagerの更新
		TimeManager::GetInstance().Update(dt);
		// ポストエフェクトの設定を保ったまま、実時間でノイズなどの時刻を進める。
		auto params = renderer_.GetPostProcessParams();
		params.time += TimeManager::GetInstance().GetUnscaledDeltaTime();
		renderer_.SetPostProcessParams(params);
		audio_.UpdateBGM(dt);

		// プロファイラにフレームタイムをセット
		NetworkProfiler::GetInstance().SetDeltaTime(dt);

		// シーン切り替えをBeginFrame前に処理（GPUフェンス不整合を防ぐ）
		sceneManager_.ProcessPendingChange();
		auto* presentedBackBuffer = dx_.GetCurrentBackBufferResource();

		// ワールドシーンを一度も読み込んでいない間は 3D パイプラインがまだ無い:
		// UI だけのシーン (タイトル / セレクト) は軽量な UI 用フレームで描く。
		if (!renderer_.Ready3D()) {
#if defined(USE_IMGUI) && !defined(NDEBUG)
			imgui_.NewFrame(dx_);
#endif
			renderer_.RenderUIFrame(
			    [&] {
				    sceneManager_.Update();
				    sceneManager_.Draw();
				    if (auto* scene = sceneManager_.Current()) {
					    scene->DrawUI();
#if defined(USE_IMGUI) && !defined(NDEBUG)
					    renderer_.SetEditorLoadingViewport(0, 0, 0, 0);
					    if (editorPresenter_)
						    editorPresenter_(*scene, sceneManager_.CurrentName());
					    else
						    scene->DrawEditor();
#endif
				    }
#if defined(USE_IMGUI) && !defined(NDEBUG)
			    },
			    [&](ID3D12GraphicsCommandList* commands) {
				    imgui_.Render(commands);
#ifndef NDEBUG
				    if (editorPresenter_)
					    renderer_.CaptureEditorLoadingBackground(commands);
#endif
#endif
			    });
			startupValidation.OnPresented(dx_, renderer_, sceneManager_, presentedBackBuffer);
			NetworkProfiler::GetInstance().CommitFrame();
			continue;
		}

		try {
			dx_.BeginFrame();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in dx_.BeginFrame():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in dx_.BeginFrame()");
			throw;
		}

		const float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
		try {
			renderer_.BeginFrame(clearColor);
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in renderer_.BeginFrame():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in renderer_.BeginFrame()");
			throw;
		}

#ifdef USE_IMGUI
		imgui_.NewFrame(dx_);
#endif
		// CPU Logic Timeの計測開始
		auto logicStart = std::chrono::high_resolution_clock::now();

		try {
			LoadProfile::Scope frameStep("frame", "scene update", 30);
			sceneManager_.Update();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in sceneManager_.Update():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in sceneManager_.Update()");
			throw;
		}

		auto logicEnd = std::chrono::high_resolution_clock::now();
		float logicTimeMs = std::chrono::duration<float, std::milli>(logicEnd - logicStart).count();
		NetworkProfiler::GetInstance().SetCpuLogicTime(logicTimeMs);

		// GPU Render Time (CPU側のコマンド構築〜Present完了まで) の計測開始
		auto renderStart = std::chrono::high_resolution_clock::now();

		try {
			LoadProfile::Scope frameStep("frame", "scene draw", 30);
			sceneManager_.Draw();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in sceneManager_.Draw():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in sceneManager_.Draw()");
			throw;
		}

		try {
			LoadProfile::Scope frameStep("frame", "renderer EndFrame", 30);
			renderer_.EndFrame();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in renderer_.EndFrame():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in renderer_.EndFrame()");
			throw;
		}

		Engine::IScene* currentScene = sceneManager_.Current();
		if (currentScene) {
			try {
				currentScene->DrawUI();
			} catch (const std::exception& e) {
				LogFile("EXCEPTION in DrawUI():");
				LogFile(e.what());
				throw;
			} catch (...) {
				LogFile("UNKNOWN EXCEPTION in DrawUI()");
				throw;
			}
		}

#ifdef USE_IMGUI
		if (currentScene) {
#ifndef NDEBUG
			if (editorPresenter_) {
				renderer_.SetEditorLoadingViewport(0, 0, 0, 0);
				editorPresenter_(*currentScene, sceneManager_.CurrentName());
			} else
				currentScene->DrawEditor();
#endif
		}
		imgui_.Render(dx_);
#ifndef NDEBUG
		if (editorPresenter_)
			renderer_.CaptureEditorLoadingBackground();
#endif
#endif
		try {
			LoadProfile::Scope frameStep("frame", "present (waits for the GPU)", 30);
			dx_.EndFrame();
		} catch (const std::exception& e) {
			LogFile("EXCEPTION in dx_.EndFrame():");
			LogFile(e.what());
			throw;
		} catch (...) {
			LogFile("UNKNOWN EXCEPTION in dx_.EndFrame()");
			throw;
		}

		startupValidation.OnPresented(dx_, renderer_, sceneManager_, presentedBackBuffer);
		auto renderEnd = std::chrono::high_resolution_clock::now();
		float renderTimeMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
		NetworkProfiler::GetInstance().SetGpuRenderTime(renderTimeMs);

		// RAM使用量の取得
		PROCESS_MEMORY_COUNTERS pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
			float ramMB = (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
			NetworkProfiler::GetInstance().SetSystemRamUsage(ramMB);
		}

		// 詳細な統計データをプロファイラへ送る
		auto* renderer = Renderer::GetInstance();
		if (renderer) {
			NetworkProfiler::GetInstance().SetDrawCalls(renderer->GetDrawCallCount());
			NetworkProfiler::GetInstance().SetParticleCount(renderer->GetParticleCount());

			// 有効なライトの数を数える
			uint32_t activeLights = 0;
			auto lcb = renderer->GetLightCB();
			for (int i = 0; i < Renderer::kMaxDirLights; ++i)
				if (lcb.dirLights[i].enabled)
					activeLights++;
			for (int i = 0; i < Renderer::kMaxPointLights; ++i)
				if (lcb.pointLights[i].enabled)
					activeLights++;
			for (int i = 0; i < Renderer::kMaxSpotLights; ++i)
				if (lcb.spotLights[i].enabled)
					activeLights++;
			for (int i = 0; i < Renderer::kMaxAreaLights; ++i)
				if (lcb.areaLights[i].enabled)
					activeLights++;
			NetworkProfiler::GetInstance().SetLightCount(activeLights);

			Vector3 pPos = renderer->GetPlayerPos();
			NetworkProfiler::GetInstance().SetPlayerPos(pPos.x, pPos.y, pPos.z);
		}

		// プロファイラのデータを更新
		NetworkProfiler::GetInstance().CommitFrame();
	}
}

void App::Shutdown() {
	// 先にSceneManagerをクリアして、すべてのシーンオブジェクト・コンポーネントを安全に破棄する
	// これにより、コンポーネント破棄時にオーディオやジョブシステムなどのサブシステムへ安全にアクセスできます
	sceneManager_.Clear();

	NetworkProfiler::GetInstance().Shutdown();
	JobSystem::Shutdown();
#ifdef USE_IMGUI
	imgui_.Shutdown();
#endif
	audio_.Shutdown();
	input_.Shutdown();

	renderer_.Shutdown();

	dx_.WaitIdle();
	dx_.Shutdown();
}

} // namespace Engine
