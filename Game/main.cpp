// main.cpp
// アプリケーションの入口 (WinMain)。
// 作業ディレクトリの設定、シーンの登録、エディタの接続、コマンドライン引数による
// 起動シーンの切り替えを行い、エンジン (Engine::App) を起動する。
#include "App.h"
// ゲームの各シーン
#include "GameScene.h"
#include "Scenes/SlimePlayerDemoScene.h"
#include "GameOverScene.h"
#include "TitleScene.h"
#include "SelectScene.h"
#include "AssignmentScene.h"
// エディタの画面 (ImGui)
#include "Editor/EditorUI.h"
// ロード画面とシーン切り替えの演出
#include "UI/LoadingUI.h"
// ファイル入出力 / 構造化例外のヘッダー
#include <fstream>
#include <eh.h>
// 実行ファイルとプロジェクトのパスを求める便利関数
#include "../Engine/PathUtils.h"
// ゲーム全体の設定 (Resources/Data/game.json)
#include "Data/GameConfig.h"
#include <shellapi.h>
// 自動テスト・計測用のシーン (コマンドライン引数で起動する)
#include "../tests/PackageValidationScene.h"
#include "../tests/RtLightingValidationScene.h"
#include "../tests/SceneryBenchmarkScene.h"
#include "../tests/SlimeLifeValidationScene.h"
// Debug / Development ビルドだけで使う検証用シーン
#ifndef NDEBUG
#include "../tests/FluidSandboxValidationScene.h"
#include "../tests/RigidbodyValidationScene.h"
#include "../tests/FluidValidationScene.h"
#include "../tests/FluidGameBenchmarkScene.h"
#include "../tests/ChronoValidationScene.h"
#include "../tests/UIValidationScene.h"
#include "../tests/EditorLoadingValidationScene.h"
#include "../tests/SlimePlayerDemoValidationScene.h"
#include "../tests/UIGalleryScene.h"
#include "../tests/ComponentValidationScene.h"
#endif

// 起動ログのファイル (game.json の logFile。作業ディレクトリを決めた後に設定する)
static std::string g_logPath;

// 起動ログに追記する (起動途中で落ちたときの調査用。ファイルが決まる前は何もしない)
void LogFileMain(const char* msg) {
	if (g_logPath.empty()) return;
	// 追記モードで開く
	FILE* f = nullptr;
	fopen_s(&f, g_logPath.c_str(), "a");
	// 開けたときだけ 1 行書いて閉じる
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

// Windows アプリケーションのエントリーポイント
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int cmdShow) {
	// コマンドライン文字列はここでは使わない (GetCommandLineW で読み直す)
	(void)commandLine;
	// 高 DPI 環境でもウィンドウとマウス座標がずれないよう、モニターごとの DPI に対応させる
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// 作業ディレクトリを決める (Resources を相対パスで読むため)
	{
		// 実行ファイルのフルパスを取得する
		wchar_t exePath[32768];
		DWORD length = GetModuleFileNameW(nullptr, exePath, 32768);
		// 取得できたら末尾を閉じ、最後の \ より後ろ (ファイル名) を切ってフォルダーにする
		if (length > 0 && length < 32768) {
			exePath[length] = L'\0';
			wchar_t* lastSlash = wcsrchr(exePath, L'\\');
			if (lastSlash)
				*lastSlash = L'\0';

			// リリースは実行ファイルの隣にある Resources だけを読む
#ifdef NDEBUG
            SetCurrentDirectoryW(exePath);
#else
			// 開発中はプロジェクトの目印を探してルートへ移動する (独自の出力先から起動しても Resources が見つかる)
			SetCurrentDirectoryW(Engine::PathUtils::FromUTF8(Engine::PathUtils::GetRootPath()).c_str());
#endif
		}
	}

	// ゲーム全体の設定を読み、ログファイルを新しく作り直す (読めなければ理由を出して終了する)
	try {
		g_logPath = Game::Data::Config().logFile;
	} catch (const std::exception& e) {
		MessageBoxA(nullptr, e.what(), "Game data error", MB_ICONERROR);
		return -1;
	}
	{
		FILE* f = nullptr;
		fopen_s(&f, g_logPath.c_str(), "w");
		if (f) {
			fputs("WinMain started\n", f);
			fclose(f);
		}
	}

	// エンジン本体 (ウィンドウ・描画・入力・音・シーン管理をまとめたもの)
	Engine::App app;

	// 最初にロード画面を準備する (エンジン自体の初期化中もこれで覆う)
	Game::UI::Loading::InstallPresentation();
	// シーンの登録。名前と、そのシーンを作る関数を対応付ける
	app.SetSceneRegistrar([](Engine::SceneManager& sm, Engine::WindowDX& dx) {
		(void)dx;
		// シーン切り替え時の暗転とロード画面の演出
		Game::UI::Loading::InstallTransitions(sm);
        // 自動テスト・計測用 (リリースでも使う)
        sm.Register("PackageValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<PackageValidationScene>(); });
        sm.Register("DlssPackageValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<DlssPackageValidationScene>(); });
        sm.Register("RtLightingValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<RtLightingValidationScene>(); });
        sm.Register("SceneryBenchmark", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<SceneryBenchmarkScene>(); });
        sm.Register("SlimeLifeValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<SlimeLifeValidationScene>(); });
		// ゲームの通常の流れ: タイトル → セレクト → ゲーム → ゲームオーバー
		sm.Register("Title", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::TitleScene()); });
		sm.Register("Select", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::SelectScene()); });
		sm.Register("SlimePlayerDemo", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<Game::SlimePlayerDemoScene>(); });
        sm.Register("Game", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameScene()); });
		// 評価課題の確認用デモ (スキニング・パーティクルなど)
		sm.Register("Assignment", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::AssignmentScene()); });
		sm.Register("GameOver", []() -> std::unique_ptr<Engine::IScene> { return std::unique_ptr<Engine::IScene>(new Game::GameOverScene()); });
// Debug / Development 限定の検証シーン
#ifndef NDEBUG
		// 流体の検証・サンドボックス・剛体の検証
		sm.Register("FluidValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidValidationScene>(); });
		sm.Register("FluidSandboxValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidSandboxValidationScene>(); });
		sm.Register("RigidbodyValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<RigidbodyValidationScene>(); });
		sm.Register("RigidbodyLegacyValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<RigidbodyValidationScene>(false); });
		// コンポーネントの検証と、それをエディタで開く版
		sm.Register("ComponentValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ComponentValidationScene>(); });
		sm.Register("ComponentEditor", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ComponentValidationScene>(true); });
		// UI の検証と見本
		sm.Register("UIResultValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<UIResultValidationScene>(); });
        sm.Register("SlimePlayerDemoValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<SlimePlayerDemoValidationScene>(); });
        sm.Register("EditorLoadingValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<EditorLoadingValidationScene>(); });
        sm.Register("UIValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<UIValidationScene>(); });
        sm.Register("UIGallery", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<UIGalleryScene>(); });
        // 描画・影・ボス・回避・ビームのカメラの検証
        sm.Register("GraphicsValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<GraphicsValidationScene>(); });
        sm.Register("ShadowValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ShadowValidationScene>(); });
        sm.Register("BossValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<BossValidationScene>(); });
        sm.Register("DodgeValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<DodgeValidationScene>(); });
        sm.Register("BeamCameraValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<BeamCameraValidationScene>(); });
        // Chrono (スライム) の検証
        sm.Register("ChronoValidation", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<ChronoValidationScene>(); });
		// 流体を使ったゲームの負荷計測 (通常・エディタ・衝突)
		sm.Register("FluidGameBenchmark", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(); });
		sm.Register("FluidGameBenchmarkEditor", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(true); });
		sm.Register("FluidCollisionSmoke", []() -> std::unique_ptr<Engine::IScene> { return std::make_unique<FluidGameBenchmarkScene>(false, true); });
#endif
	});

#ifdef USE_IMGUI
	// タイトルやデモなど GameScene 以外のシーンでも、同じエディタの中で確認できるようにする
	app.SetEditorPresenter([](Engine::IScene& scene, const std::string& name) { Game::EditorUI::PresentScene(scene, name); });
#ifndef NDEBUG
	// タイトル・セレクトは本来 3D を使わない軽量な経路 (ImGui なし) で動くため、
	// エディタを出す Development ビルドでは最初から通常の経路にする。リリースは従来どおり軽量起動
	{ const auto& names = Game::Data::Config().fullRendererScenes; app.GetSceneManager().SetFullRendererScenes({names.begin(), names.end()}); }
#endif
#endif

	// 通常は game.json の initialScene (タイトル) から始める
	app.SetInitialSceneKey(Game::Data::Config().initialScene);
	// コマンドライン引数で起動シーンを差し替える (自動テスト用。対応は game.json の launchOptions)
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (arguments) {
		// 0 番目は実行ファイル名なので 1 番目から
		for (int i = 1; i < argumentCount; ++i) {
			const std::string argument = Engine::PathUtils::ToUTF8(arguments[i]);
#ifndef NDEBUG
			if (argument == "--editor-loading-smoke") app.SetInitialSceneKey("EditorLoadingValidation");
#endif
			for (const auto& option : Game::Data::Config().launchOptions) {
				if (option.flag != argument) continue;
#ifdef NDEBUG
				// 検証用の引数は Debug / Development ビルドだけ受け付ける
				if (option.debugOnly) continue;
#endif
				app.SetInitialSceneKey(option.scene);
				if (!option.log.empty()) LogFileMain(option.log.c_str());
			}
		}
		LocalFree(arguments);
	}

	// エンジンの初期化 → メインループ → 終了処理。例外はログとメッセージボックスで知らせる
	try {
		LogFileMain("Initializing App...");
		// 初期化に失敗したら終了コード -1 で終わる
		if (!app.Initialize(hInst, cmdShow)) {
			LogFileMain("App::Initialize failed");
			return -1;
		}

		// メインループ (ウィンドウが閉じられるまで戻らない)
		LogFileMain("Running App...");
		app.Run();

		// 後片付け
		LogFileMain("Shutting down...");
		app.Shutdown();
	// 標準例外: 内容をログとデバッグ出力に残して通知する
	} catch (const std::exception& e) {
		LogFileMain("Caught exception:");
		LogFileMain(e.what());
		OutputDebugStringA("EXCEPT: ");
		OutputDebugStringA(e.what());
		OutputDebugStringA("\n");
		MessageBoxA(nullptr, e.what(), "Unhandled Exception", MB_ICONERROR);
		return -1;
	// 原因不明の例外
	} catch (...) {
		LogFileMain("Caught unknown exception");
		OutputDebugStringA("EXCEPT: Unknown\n");
		MessageBoxA(nullptr, "Unknown exception caught", "Unhandled Exception", MB_ICONERROR);
		return -1;
	}
	// 正常終了
	return 0;
}
