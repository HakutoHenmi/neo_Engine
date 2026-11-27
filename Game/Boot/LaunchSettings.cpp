// LaunchSettings.cpp
// 作業ディレクトリの決定と、コマンドライン引数による起動シーンの切り替え。
#include "LaunchSettings.h"
#include "StartupLog.h"
#include "../../Engine/Core/App.h"
#include "../../Engine/Core/PathUtils.h"
#include "../Data/GameConfig.h"
#include "../Editor/EditorUI.h"
#include <shellapi.h>
#include <string>

namespace Game::Boot {
namespace {

// Windows のパスの最大長 (\\?\ 付きの長いパスも入る大きさ)
constexpr DWORD kMaxPathLength = 32768;

// 引数 1 つを game.json の launchOptions と照らし合わせ、一致したら起動シーンを差し替える
void ApplyLaunchArgument(Engine::App& app, const std::string& argument) {
#ifndef NDEBUG
	if (argument == "--editor-loading-smoke") {
		app.SetInitialSceneKey("EditorLoadingValidation");
		app.SetStartFullscreen(false);
	}
#endif
	for (const auto& option : Data::Config().launchOptions) {
		if (option.flag != argument) {
			continue;
		}
#ifdef NDEBUG
		// 検証用の引数は Debug / Development ビルドだけ受け付ける
		if (option.debugOnly) {
			continue;
		}
#endif
		app.SetInitialSceneKey(option.scene);
		// 自動テストは画面を奪わないように窓のまま
		app.SetStartFullscreen(false);
		if (!option.log.empty()) {
			LogFileMain(option.log.c_str());
		}
	}
}

} // namespace

void MoveToResourceRoot() {
#ifdef NDEBUG
	// リリースは実行ファイルの隣にある Resources だけを読む
	std::wstring exePath(kMaxPathLength, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, exePath.data(), kMaxPathLength);
	if (length == 0 || length >= kMaxPathLength) {
		return;
	}
	// 最後の \ より後ろ (ファイル名) を切ってフォルダーにする
	exePath.resize(length);
	if (const size_t lastSlash = exePath.find_last_of(L'\\'); lastSlash != std::wstring::npos) {
		exePath.resize(lastSlash);
	}
	SetCurrentDirectoryW(exePath.c_str());
#else
	// 開発中はプロジェクトの目印を探してルートへ移動する (独自の出力先から起動しても Resources が見つかる)
	SetCurrentDirectoryW(Engine::PathUtils::FromUTF8(Engine::PathUtils::GetRootPath()).c_str());
#endif
}

void ApplyLaunchSettings(Engine::App& app) {
	// 通常は game.json の initialScene (タイトル) から全画面で始める (F11 / Alt+Enter で窓に戻せる)
	app.SetInitialSceneKey(Data::Config().initialScene);
	app.SetStartFullscreen(true);

	// コマンドライン引数で起動シーンを差し替える (自動テスト用。対応は game.json の launchOptions)
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (!arguments) {
		return;
	}
	// 0 番目は実行ファイル名なので 1 番目から
	for (int i = 1; i < argumentCount; ++i) {
		ApplyLaunchArgument(app, Engine::PathUtils::ToUTF8(arguments[i]));
	}
	LocalFree(arguments);
}

void InstallEditor(Engine::App& app) {
#ifdef USE_IMGUI
	app.SetEditorPresenter([](Engine::IScene& scene, const std::string& name) { EditorUI::PresentScene(scene, name); });
#ifndef NDEBUG
	// 必要なシーンだけ 3D 初期化を強制する。タイトル・セレクトのエディタ表示も軽量経路で動く
	const auto& names = Data::Config().fullRendererScenes;
	app.GetSceneManager().SetFullRendererScenes({names.begin(), names.end()});
#endif
#else
	(void)app;
#endif
}

} // namespace Game::Boot
