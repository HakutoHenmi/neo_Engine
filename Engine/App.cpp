#include "App.h"
#include <Windows.h>
#include <chrono>
#include "JobSystem.h"
#include "Time/TimeManager.h" // ★追加
#include "NetworkProfiler.h"
#include <psapi.h> // ★追加: メモリ使用量取得用
#pragma comment(lib, "psapi.lib")

namespace Engine {

bool App::Initialize(HINSTANCE hInst, int cmdShow) {
	sceneManager_.SetDX(&dx_);
	if (!dx_.Initialize(hInst, cmdShow, hwnd_))
		return false;

	// Job Systemの初期化
	JobSystem::Initialize();

	// ネットワークプロファイラの初期化
	NetworkProfiler::GetInstance().Initialize(8080);

	if (!renderer_.Initialize(&dx_))
		return false;

	input_.Initialize(hInst, hwnd_);
	camera_.Initialize();
	audio_.Initialize();

#ifdef USE_IMGUI
	if (!imgui_.Initialize(hwnd_, dx_, dx_.SRV(), dx_.SRV_CPU(0), dx_.SRV_GPU(0), 18.0f, "Resources/Textures/fonts/Huninn/Huninn-Regular.ttf")) {
		return false;
	}
#endif

	if (registrar_) {
		registrar_(sceneManager_, dx_);
	}

	if (!initialSceneKey_.empty()) {
		sceneManager_.Change(initialSceneKey_);
	} else if (sceneManager_.Has("FPS")) {
		sceneManager_.Change("FPS");
	} else {
		const std::string first = sceneManager_.FirstRegisteredName();
		if (!first.empty())
			sceneManager_.Change(first);
	}

	return true;
}

void App::Run() {
	MSG msg{};
	bool running = true;

	auto prevTime = std::chrono::high_resolution_clock::now();
	int frameCount = 0;
	float timeElapsed = 0.0f;

	while (running) {
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

		input_.Update();

		// ★追加: フルスクリーン切り替え (F11 or Alt+Enter)
		if (input_.Trigger(DIK_F11) || (input_.Down(DIK_LALT) && input_.Trigger(DIK_RETURN))) {
			dx_.ToggleFullscreen();
		}

		// フレームタイムの計算
		auto currentTime = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration<float>(currentTime - prevTime).count();
		prevTime = currentTime;

		// FPSの計算
		frameCount++;
		timeElapsed += dt;
		if (timeElapsed >= 1.0f) {
			NetworkProfiler::GetInstance().SetFPS((float)frameCount / timeElapsed);
			frameCount = 0;
			timeElapsed = 0.0f;
		}

		// ★追加: TimeManagerの更新
		TimeManager::GetInstance().Update(dt);

		// プロファイラにフレームタイムをセット
		NetworkProfiler::GetInstance().SetDeltaTime(dt);

		dx_.BeginFrame();
		const float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
		renderer_.BeginFrame(clearColor);

#ifdef USE_IMGUI
		imgui_.NewFrame(dx_);
#endif

		// ★追加: CPU Logic Timeの計測開始
		auto logicStart = std::chrono::high_resolution_clock::now();

		sceneManager_.Update();

		auto logicEnd = std::chrono::high_resolution_clock::now();
		float logicTimeMs = std::chrono::duration<float, std::milli>(logicEnd - logicStart).count();
		NetworkProfiler::GetInstance().SetCpuLogicTime(logicTimeMs);

		// ★追加: GPU Render Time (CPU側のコマンド構築〜Present完了まで) の計測開始
		auto renderStart = std::chrono::high_resolution_clock::now();

		sceneManager_.Draw();

		renderer_.EndFrame();

		Engine::IScene* currentScene = sceneManager_.Current();
		if (currentScene) {
			currentScene->DrawUI();
		}

#ifdef USE_IMGUI
		if (currentScene) {
#ifndef NDEBUG
			currentScene->DrawEditor();
#endif
		}
		imgui_.Render(dx_);
#endif
		dx_.EndFrame();

		auto renderEnd = std::chrono::high_resolution_clock::now();
		float renderTimeMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
		NetworkProfiler::GetInstance().SetGpuRenderTime(renderTimeMs);

		// ★追加: RAM使用量の取得
		PROCESS_MEMORY_COUNTERS pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
			float ramMB = (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
			NetworkProfiler::GetInstance().SetSystemRamUsage(ramMB);
		}

		// ★追加: 詳細な統計データをプロファイラへ送る
		auto* renderer = Renderer::GetInstance();
		if (renderer) {
			NetworkProfiler::GetInstance().SetDrawCalls(renderer->GetDrawCallCount());
			NetworkProfiler::GetInstance().SetParticleCount(renderer->GetParticleCount());
			
			// 有効なライトの数を数える
			uint32_t activeLights = 0;
			auto lcb = renderer->GetLightCB();
			for (int i = 0; i < Renderer::kMaxDirLights; ++i) if (lcb.dirLights[i].enabled) activeLights++;
			for (int i = 0; i < Renderer::kMaxPointLights; ++i) if (lcb.pointLights[i].enabled) activeLights++;
			for (int i = 0; i < Renderer::kMaxSpotLights; ++i) if (lcb.spotLights[i].enabled) activeLights++;
			for (int i = 0; i < Renderer::kMaxAreaLights; ++i) if (lcb.areaLights[i].enabled) activeLights++;
			NetworkProfiler::GetInstance().SetLightCount(activeLights);

			Vector3 pPos = renderer->GetPlayerPos();
			NetworkProfiler::GetInstance().SetPlayerPos(pPos.x, pPos.y, pPos.z);
		}

		// プロファイラのデータを更新
		NetworkProfiler::GetInstance().CommitFrame();
	}
}

void App::Shutdown() {
	// ★修正: 先にSceneManagerをクリアして、すべてのシーンオブジェクト・コンポーネントを安全に破棄する
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

void App::BeginFrame_() {}
void App::EndFrame_() {}

} // namespace Engine
