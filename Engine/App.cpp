#include "App.h"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include "JobSystem.h"
#include "Time/TimeManager.h" // ★追加
#include "NetworkProfiler.h"
#include <psapi.h> // ★追加: メモリ使用量取得用
#pragma comment(lib, "psapi.lib")

namespace Engine {

#include <cstdio>

static void LogFile(const char* msg) {
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

bool App::Initialize(HINSTANCE hInst, int cmdShow) {
	LogFile("Step 1: dx_.Initialize");
	sceneManager_.SetDX(&dx_);
	if (!dx_.Initialize(hInst, cmdShow, hwnd_))
		return false;

	LogFile("Step 2: JobSystem::Initialize");
	JobSystem::Initialize();

	LogFile("Step 3: NetworkProfiler::Initialize");
	NetworkProfiler::GetInstance().Initialize(8080);

	LogFile("Step 4: renderer_.Initialize");
	if (!renderer_.Initialize(&dx_))
		return false;

	LogFile("Step 5: input_.Initialize");
	input_.Initialize(hInst, hwnd_);
	LogFile("Step 6: camera_.Initialize");
	camera_.Initialize();
	LogFile("Step 7: audio_.Initialize");
	audio_.Initialize();

#ifdef USE_IMGUI
	LogFile("Step 8: imgui_.Initialize");
	uint32_t imguiSrvIdx = renderer_.AllocateTextSrvIndex();
	if (!imgui_.Initialize(hwnd_, dx_, dx_.SRV(), dx_.SRV_CPU((int)imguiSrvIdx), dx_.SRV_GPU((int)imguiSrvIdx), 18.0f, "Resources/Textures/fonts/Huninn/Huninn-Regular.ttf")) {
		return false;
	}
#endif

	LogFile("Step 9: registrar_");
	if (registrar_) {
		registrar_(sceneManager_, dx_);
	}

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
	MSG msg{};
	bool running = true;

	auto prevTime = std::chrono::high_resolution_clock::now();
	int frameCount = 0;
	float timeElapsed = 0.0f;

	int debugFrameCount = 0;
	while (running) {
		{
			char buf[128];
			if (debugFrameCount < 5 || debugFrameCount % 60 == 0)
			{
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



		// ★追加: 動的パラメータ制御（出来による加点枠のアピール）
		// スペースキーでダメージ（またはアクション）演出を発動し、時間経過で減衰させる
		static float effectIntensity = 0.0f;
		if (input_.Trigger(DIK_SPACE)) {
			effectIntensity = 1.0f; // 演出開始
		}
		
		auto params = renderer_.GetPostProcessParams();
		if (effectIntensity > 0.0f) {
			effectIntensity -= 1.0f / 60.0f; // 簡易的な減衰（毎秒約1.0減る）
			if (effectIntensity < 0.0f) effectIntensity = 0.0f;
			
			params.vignette = effectIntensity; // 赤いダメージビネット等
			params.distortion = effectIntensity; // ブラーの強さやレンズ歪み
			params.chromaShift = effectIntensity; // 色収差の強さ
		} else {
			params.vignette = 0.0f;
			params.distortion = 0.0f;
			params.chromaShift = 0.0f;
		}
		params.time += 1.0f / 60.0f; // ノイズ等の時間経過用
		renderer_.SetPostProcessParams(params);

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

			// ★追加: シーン切り替えをBeginFrame前に処理（GPUフェンス不整合を防ぐ）
		sceneManager_.ProcessPendingChange();

		try {
		dx_.BeginFrame();
		} catch (const std::exception& e) { LogFile("EXCEPTION in dx_.BeginFrame():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in dx_.BeginFrame()"); throw; }

		const float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
		try {
		renderer_.BeginFrame(clearColor);
		} catch (const std::exception& e) { LogFile("EXCEPTION in renderer_.BeginFrame():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in renderer_.BeginFrame()"); throw; }

#ifdef USE_IMGUI
		imgui_.NewFrame(dx_);
#endif
		// ★追加: CPU Logic Timeの計測開始
		auto logicStart = std::chrono::high_resolution_clock::now();

		try {
		sceneManager_.Update();
		} catch (const std::exception& e) { LogFile("EXCEPTION in sceneManager_.Update():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in sceneManager_.Update()"); throw; }

		auto logicEnd = std::chrono::high_resolution_clock::now();
		float logicTimeMs = std::chrono::duration<float, std::milli>(logicEnd - logicStart).count();
		NetworkProfiler::GetInstance().SetCpuLogicTime(logicTimeMs);

		// ★追加: GPU Render Time (CPU側のコマンド構築〜Present完了まで) の計測開始
		auto renderStart = std::chrono::high_resolution_clock::now();

		try {
		sceneManager_.Draw();
		} catch (const std::exception& e) { LogFile("EXCEPTION in sceneManager_.Draw():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in sceneManager_.Draw()"); throw; }

		try {
		renderer_.EndFrame();
		} catch (const std::exception& e) { LogFile("EXCEPTION in renderer_.EndFrame():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in renderer_.EndFrame()"); throw; }

		Engine::IScene* currentScene = sceneManager_.Current();
		if (currentScene) {
			try {
			currentScene->DrawUI();
			} catch (const std::exception& e) { LogFile("EXCEPTION in DrawUI():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in DrawUI()"); throw; }
		}

#ifdef USE_IMGUI
		if (currentScene) {
#ifndef NDEBUG
			currentScene->DrawEditor();
#endif
		}
		imgui_.Render(dx_);
#endif
		try {
		dx_.EndFrame();
		} catch (const std::exception& e) { LogFile("EXCEPTION in dx_.EndFrame():"); LogFile(e.what()); throw; } catch (...) { LogFile("UNKNOWN EXCEPTION in dx_.EndFrame()"); throw; }

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
