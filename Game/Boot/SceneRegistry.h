// SceneRegistry.h
// ゲームで使うシーンを、名前と生成関数の組としてシーンマネージャーに登録する。
#pragma once

namespace Engine {
class SceneManager;
}

namespace Game::Boot {

// 通常のゲームの流れ・自動テスト・(Debug / Development のみ) 検証用シーンをすべて登録する
void RegisterScenes(Engine::SceneManager& sceneManager);

} // namespace Game::Boot
