// LaunchSettings.h
// 起動前の準備: 作業ディレクトリの決定と、コマンドライン引数による起動シーンの切り替え。
#pragma once

namespace Engine {
class App;
}

namespace Game::Boot {

// Resources を相対パスで読めるように作業ディレクトリを移す
// (リリースは実行ファイルの隣、開発中はプロジェクトのルート)
void MoveToResourceRoot();

// 起動シーンと全画面かどうかを game.json の設定とコマンドライン引数から決める
void ApplyLaunchSettings(Engine::App& app);

// タイトルなど GameScene 以外のシーンもエディタで確認できるようにする (ImGui 有効時のみ)
void InstallEditor(Engine::App& app);

} // namespace Game::Boot
