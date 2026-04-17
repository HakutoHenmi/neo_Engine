#include "App.h"
#include "GameScene.h"
#include "TitleScene.h"
#include "SelectScene.h"
#include "ResultScene.h"
#include <Windows.h>
#include <memory>

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int cmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// ★変更: 開発用ディレクトリ (TD_Engine) を優先する
	{
		wchar_t exePath[32768];
		DWORD length = GetModuleFileNameW(nullptr, exePath, 32768);
		if (length > 0 && length < 32768) {
			exePath[length] = L'\0';
			wchar_t* lastSlash = wcsrchr(exePath, L'\\');
			if (lastSlash)
				*lastSlash = L'\0';

			wchar_t devPath[32768] = {};
			wcscpy_s(devPath, exePath);
			wcscat_s(devPath, L"\\..\\..\\..\\TD_Engine");

			DWORD attr = GetFileAttributesW(devPath);
			if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
				SetCurrentDirectoryW(devPath);
			} else {
				SetCurrentDirectoryW(exePath);
			}
		}
	}

	Engine::App app;

	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX& dx) {
		(void)dx;
		sm.Register("Title", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::TitleScene()); });
		sm.Register("Select", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::SelectScene()); });
		sm.Register("Game", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameScene()); });
		sm.Register("Result", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::ResultScene()); });
	});

	// Default Scene
	app.SetInitialSceneKey("Title");

	if (!app.Initialize(hInst, cmdShow)) {
		return -1;
	}

	app.Run();
	app.Shutdown();
	return 0;
}
