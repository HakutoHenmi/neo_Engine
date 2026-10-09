#include "App.h"
#include "GameScene.h"
#include "GameOverScene.h"
#include "TitleScene.h"
#include "SelectScene.h"
#include "AssignmentScene.h"
#include <fstream>
#include <eh.h>
#include "../Engine/PathUtils.h"
#include "../tests/PackageValidationScene.h"
#include "../tests/RtLightingValidationScene.h"
#include "../tests/SceneryBenchmarkScene.h"
#ifndef NDEBUG
#include "../tests/FluidValidationScene.h"
#include "../tests/FluidGameBenchmarkScene.h"
#include "../tests/ChronoValidationScene.h"
#include "../tests/UIValidationScene.h"
#include "../tests/ComponentValidationScene.h"
#include <shellapi.h>
#endif

void LogFileMain(const char* msg) {
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int cmdShow) {
	(void)commandLine;
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

			// Release reads only the Resources beside the executable.
#ifdef NDEBUG
            SetCurrentDirectoryW(exePath);
#else
			// Use the existing project-marker search for custom diagnostic build directories too.
			SetCurrentDirectoryW(Engine::PathUtils::FromUTF8(Engine::PathUtils::GetRootPath()).c_str());
#endif
		}
	}

	Engine::App app;

	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX& dx) {
		(void)dx;
        sm.Register("PackageValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<PackageValidationScene>(); });
        sm.Register("DlssPackageValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<DlssPackageValidationScene>(); });
        sm.Register("RtLightingValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<RtLightingValidationScene>(); });
        sm.Register("SceneryBenchmark", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<SceneryBenchmarkScene>(); });
		sm.Register("Title", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::TitleScene()); });
		sm.Register("Select", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::SelectScene()); });
		sm.Register("Game", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameScene()); });
		sm.Register("Assignment", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::AssignmentScene()); });
		sm.Register("GameOver", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameOverScene()); });
#ifndef NDEBUG
		sm.Register("FluidValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidValidationScene>(); });
		sm.Register("ComponentValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ComponentValidationScene>(); });
		sm.Register("ComponentEditor", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ComponentValidationScene>(true); });
		sm.Register("UIResultValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<UIResultValidationScene>(); });
        sm.Register("UIValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<UIValidationScene>(); });
        sm.Register("GraphicsValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<GraphicsValidationScene>(); });
        sm.Register("ShadowValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ShadowValidationScene>(); });
        sm.Register("BossValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<BossValidationScene>(); });
        sm.Register("DodgeValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<DodgeValidationScene>(); });
        sm.Register("BeamCameraValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<BeamCameraValidationScene>(); });
        sm.Register("ChronoValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ChronoValidationScene>(); });
		sm.Register("FluidGameBenchmark", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(); });
		sm.Register("FluidGameBenchmarkEditor", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(true); });
		sm.Register("FluidCollisionSmoke", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(false, true); });
#endif
	});

	// Default Scene
	app.SetInitialSceneKey("Title");
    if(wcsstr(GetCommandLineW(),L"--package-smoke"))app.SetInitialSceneKey("PackageValidation");
    if(wcsstr(GetCommandLineW(),L"--dlss-package-smoke"))app.SetInitialSceneKey("DlssPackageValidation");
    if(wcsstr(GetCommandLineW(),L"--rt-lighting-smoke"))app.SetInitialSceneKey("RtLightingValidation");
    if(wcsstr(GetCommandLineW(),L"--scenery-benchmark"))app.SetInitialSceneKey("SceneryBenchmark");
#ifndef NDEBUG
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (arguments) {
		for (int i = 1; i < argumentCount; ++i) {
			if (wcscmp(arguments[i], L"--ui-result") == 0) app.SetInitialSceneKey("UIResultValidation");
            if (wcscmp(arguments[i], L"--ui-smoke") == 0) app.SetInitialSceneKey("UIValidation");
            if (wcscmp(arguments[i], L"--component-smoke") == 0) app.SetInitialSceneKey("ComponentValidation");
            if (wcscmp(arguments[i], L"--component-editor") == 0) app.SetInitialSceneKey("ComponentEditor");
            if (wcscmp(arguments[i], L"--graphics-ui-smoke") == 0) app.SetInitialSceneKey("GraphicsValidation");
            if (wcscmp(arguments[i], L"--shadow-band-smoke") == 0) app.SetInitialSceneKey("ShadowValidation");
            if (wcscmp(arguments[i], L"--boss-smoke") == 0) app.SetInitialSceneKey("BossValidation");
            if (wcscmp(arguments[i], L"--dodge-smoke") == 0) app.SetInitialSceneKey("DodgeValidation");
            if (wcscmp(arguments[i], L"--beam-camera-smoke") == 0) app.SetInitialSceneKey("BeamCameraValidation");
            if (wcscmp(arguments[i], L"--test-scene") == 0) app.SetInitialSceneKey("Assignment");
			if (wcscmp(arguments[i], L"--ink-smoke") == 0 || wcscmp(arguments[i], L"--chrono-smoke") == 0 || wcscmp(arguments[i], L"--creature-smoke") == 0 || wcscmp(arguments[i], L"--snake-smoke") == 0) {
				app.SetInitialSceneKey("ChronoValidation");
				LogFileMain("Chrono validation scene requested");
			}
			if (wcscmp(arguments[i], L"--fluid-smoke") == 0) {
				app.SetInitialSceneKey("FluidValidation");
				LogFileMain("Fluid smoke scene requested");
			}
			if (wcscmp(arguments[i], L"--fluid-game-benchmark") == 0) {
				app.SetInitialSceneKey("FluidGameBenchmark");
				LogFileMain("Fluid game benchmark scene requested");
			}
			if (wcscmp(arguments[i], L"--fluid-game-benchmark-editor") == 0) {
				app.SetInitialSceneKey("FluidGameBenchmarkEditor");
				LogFileMain("Fluid game editor benchmark scene requested");
			}
			if (wcscmp(arguments[i], L"--fluid-collision-smoke") == 0) {
				app.SetInitialSceneKey("FluidCollisionSmoke");
				LogFileMain("Fluid collision smoke scene requested");
			}
		}
		LocalFree(arguments);
	}
#endif

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
