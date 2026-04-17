#include "App.h"
#include <Windows.h>
#include "JobSystem.h"
#include "Time/TimeManager.h" // ★追加


namespace Engine {

bool App::Initialize(HINSTANCE hInst, int cmdShow) {
	sceneManager_.SetDX(&dx_);
	if (!dx_.Initialize(hInst, cmdShow, hwnd_))
		return false;

	// Job Systemの初期化
	JobSystem::Initialize();


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

		// ★追加: TimeManagerの更新
		// 本来は正確なフレーム時間(dt)を計算すべき
		TimeManager::GetInstance().Update(1.0f / 60.0f);

		dx_.BeginFrame();
		const float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
		renderer_.BeginFrame(clearColor);

#ifdef USE_IMGUI
		imgui_.NewFrame(dx_);
#endif

		sceneManager_.Update();
		sceneManager_.Draw();

		renderer_.EndFrame();

#ifdef USE_IMGUI
 		Engine::IScene* currentScene = sceneManager_.Current();
 		if (currentScene) {
#ifndef NDEBUG
 			currentScene->DrawEditor();
#endif
			currentScene->DrawUI(); // リバースでもエディタでも共通で描画
 		}

		imgui_.Render(dx_);
#endif
		dx_.EndFrame();
	}
}

void App::Shutdown() {
	JobSystem::Shutdown();
#ifdef USE_IMGUI
	imgui_.Shutdown();
#endif
	audio_.Shutdown();
	input_.Shutdown();
	dx_.WaitIdle();
	dx_.Shutdown();
}

void App::BeginFrame_() {}
void App::EndFrame_() {}

} // namespace Engine
