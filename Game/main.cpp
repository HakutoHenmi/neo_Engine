// main.cpp
// アプリケーションの入口 (WinMain)。
// 起動の準備 (作業ディレクトリ・設定・シーン登録・起動引数) は Game/Boot にまとめてあり、
// ここではそれらを順に呼んでエンジン (Engine::App) を動かすだけにしている。
#include "Core/App.h"
#include "../Engine/Core/StartupTiming.h"
#include "Boot/LaunchSettings.h"
#include "Boot/SceneRegistry.h"
#include "Boot/StartupLog.h"
#include "Data/GameConfig.h"
#include "UI/LoadingUI.h"
#include "UI/SceneMusic.h"
#include <exception>

namespace {

// メインスレッドの COM アパートメント (WIC テクスチャ・ダイアログ・エディタで使う)。
// 音声の初期化は起動用ワーカー側で別のアパートメントを持つ
class ScopedComApartment {
public:
	ScopedComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
	~ScopedComApartment() {
		if (SUCCEEDED(result_)) {
			CoUninitialize();
		}
	}
	ScopedComApartment(const ScopedComApartment&) = delete;
	ScopedComApartment& operator=(const ScopedComApartment&) = delete;

private:
	HRESULT result_;
};

// ゲーム全体の設定 (Resources/Data/game.json) を読み、起動ログを作り直す。読めなければ理由を出して false
bool LoadGameData() {
	try {
		Game::Boot::ResetStartupLog(Game::Data::Config().logFile);
		(void)Game::Data::SlimeTuningData();
	} catch (const std::exception& e) {
		MessageBoxA(nullptr, e.what(), "Game data error", MB_ICONERROR);
		return false;
	}
	return true;
}

// 例外の内容をログ・デバッグ出力・メッセージボックスで知らせる
void ReportFatalError(const char* message) {
	LogFileMain("Caught exception:");
	LogFileMain(message);
	OutputDebugStringA("EXCEPT: ");
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
	MessageBoxA(nullptr, message, "Unhandled Exception", MB_ICONERROR);
}

// エンジンの初期化 → メインループ → 終了処理。戻り値はプロセスの終了コード
int RunApp(Engine::App& app, HINSTANCE hInst, int cmdShow) {
	try {
		LogFileMain("Initializing App...");
		if (!app.Initialize(hInst, cmdShow)) {
			LogFileMain("App::Initialize failed");
			return -1;
		}
		// メインループ (ウィンドウが閉じられるまで戻らない)
		LogFileMain("Running App...");
		app.Run();

		LogFileMain("Shutting down...");
		app.Shutdown();
	} catch (const std::exception& e) {
		ReportFatalError(e.what());
		return -1;
	} catch (...) {
		ReportFatalError("Unknown exception caught");
		return -1;
	}
	return 0;
}

} // namespace

// Windows アプリケーションのエントリーポイント
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int cmdShow) {
	Engine::StartupTiming::Mark("WinMain entry");
	ScopedComApartment comApartment;
	// 高 DPI 環境でもウィンドウとマウス座標がずれないよう、モニターごとの DPI に対応させる
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	Game::Boot::MoveToResourceRoot();
	if (!LoadGameData()) {
		return -1;
	}

	// エンジン本体 (ウィンドウ・描画・入力・音・シーン管理をまとめたもの)
	Engine::App app;
	// 最初にロード画面を準備する (エンジン自体の初期化中もこれで覆う)
	Game::UI::Loading::InstallPresentation();
	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX&) { Game::Boot::RegisterScenes(sm); });
	Game::Boot::InstallEditor(app);
	Game::Boot::ApplyLaunchSettings(app);
	app.SetStartupMusic("Title", Game::Music::Path(Game::Music::Title));

	return RunApp(app, hInst, cmdShow);
}
