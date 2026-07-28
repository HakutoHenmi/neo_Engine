#include "App.h"
#include "GameScene.h"
#include "GameOverScene.h"
#include "TitleScene.h"
#include "SelectScene.h"
#include "AssignmentScene.h"
#include <fstream>
#include <eh.h>

void LogFileMain(const char* msg) {
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int cmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	{
		FILE* f = nullptr;
		fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "w");
		if (f) {
			fputs("WinMain started\n", f);
			fclose(f);
		}
	}

	// カレントディレクトリをexeの場所に設定
	{
		wchar_t exePath[32768];
		DWORD length = GetModuleFileNameW(nullptr, exePath, 32768);
		if (length > 0 && length < 32768) {
			exePath[length] = L'\0';
			wchar_t* lastSlash = wcsrchr(exePath, L'\\');
			if (lastSlash)
				*lastSlash = L'\0';

			// プロジェクトルートを探す (neo_Engine ディレクトリ)
			wchar_t projectPath[32768] = {};
			wcscpy_s(projectPath, exePath);
			wcscat_s(projectPath, L"\\..\\..\\..\\neo_Engine");

			DWORD attr = GetFileAttributesW(projectPath);
			if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
				SetCurrentDirectoryW(projectPath);
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
		sm.Register("Assignment", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::AssignmentScene()); });
		sm.Register("GameOver", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameOverScene()); });
	});

	// Default Scene
	app.SetInitialSceneKey("Title");

	try {
		LogFileMain("Initializing App...");
		if (!app.Initialize(hInst, cmdShow)) {
			LogFileMain("App::Initialize failed");
			return -1;
		}

		LogFileMain("Running App...");
		app.Run();

		LogFileMain("Shutting down...");
		app.Shutdown();
	} catch (const std::exception& e) {
		LogFileMain("Caught exception:");
		LogFileMain(e.what());
		OutputDebugStringA("EXCEPT: ");
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
		MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_ICONERROR);
		return -1;
	} catch (...) {
		LogFileMain("Caught unknown exception");
		OutputDebugStringA("EXCEPT: Unknown\n");
		MessageBoxA(nullptr, "Unknown exception caught", "Unhandled Exception", MB_ICONERROR);
		return -1;
	}
	return 0;
}
