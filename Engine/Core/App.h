#pragma once
#include "Audio.h"
#include "../Camera/Camera.h"
#ifdef USE_IMGUI
#include "../ImGui/ImGuiLayer.h"
#endif
#include "Input.h"
#include "../Math/Matrix4x4.h"
#include "../Graphics/Renderer.h"
#include "../Scene/SceneManager.h"
#include "../Graphics/TextureManager.h"
#include "WindowDX.h"

#include <Windows.h>
#include <chrono>
#include <dxgidebug.h>
#include <functional>
#include <string>
#include <vector>
#include <wrl.h>

namespace Engine {

class App {
public:
	bool Initialize(HINSTANCE hInst, int cmdShow);
	void Run();
	void Shutdown();

	using SceneRegistrar = std::function<void(SceneManager& sm, WindowDX& dx)>;
	void SetSceneRegistrar(SceneRegistrar f) {
		registrar_ = std::move(f);
	}
	void SetStartupMusic(const std::string& scene, const std::string& path) {
		startupMusicScene_ = scene;
		startupMusicPath_ = path;
	}
	void SetInitialSceneKey(const std::string& key) {
		initialSceneKey_ = key;
	}
	// 起動直後に全画面 (ボーダレス) にする
	void SetStartFullscreen(bool fullscreen) {
		startFullscreen_ = fullscreen;
	}
	// エディタ UI の描画をゲーム側へ任せる (未設定なら各シーンの DrawEditor をそのまま呼ぶ)
	using EditorPresenter = std::function<void(IScene& scene, const std::string& sceneName)>;
	void SetEditorPresenter(EditorPresenter f) {
		editorPresenter_ = std::move(f);
	}
	SceneManager& GetSceneManager() {
		return sceneManager_;
	}

private:
	SceneManager sceneManager_;
	EditorPresenter editorPresenter_;
	HWND hwnd_ = nullptr;
	WindowDX dx_;
	Input input_;
	Camera camera_;
	Audio audio_;
	Renderer renderer_;
#ifdef USE_IMGUI
	ImGuiLayer imgui_;
#endif

	SceneRegistrar registrar_{};
	std::string initialSceneKey_{};
	bool startFullscreen_ = false;
	std::string startupMusicScene_, startupMusicPath_;
};

} // namespace Engine
