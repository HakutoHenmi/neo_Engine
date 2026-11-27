// SceneRegistry.cpp
// シーンの登録。名前と、そのシーンを作る関数を対応付ける。
#include "SceneRegistry.h"
#include "../../Engine/Scene/SceneManager.h"
// ゲームの各シーン
#include "../Scenes/GameScene.h"
#include "../Scenes/SlimePlayerDemoScene.h"
#include "../Scenes/WaterDioramaDemoScene.h"
#include "../Scenes/GameOverScene.h"
#include "../Scenes/TitleScene.h"
#include "../Scenes/SelectScene.h"
#include "../Scenes/AssignmentScene.h"
// シーン切り替え時の暗転とロード画面の演出
#include "../UI/LoadingUI.h"
// 自動テスト・計測用のシーン (コマンドライン引数で起動する)
#include "../../tests/PackageValidationScene.h"
#include "../../tests/RtLightingValidationScene.h"
#include "../../tests/SlimeLifeValidationScene.h"
#include "../../tests/ChallengeCaptureScene.h"
// Debug / Development ビルドだけで使う検証用シーン
#ifndef NDEBUG
#include "../../tests/FluidSandboxValidationScene.h"
#include "../../tests/RigidbodyValidationScene.h"
#include "../../tests/FluidValidationScene.h"
#include "../../tests/FluidGameBenchmarkScene.h"
#include "../../tests/UIValidationScene.h"
#include "../../tests/EditorLoadingValidationScene.h"
#include "../../tests/SlimePlayerDemoValidationScene.h"
#include "../../tests/UIGalleryScene.h"
#include "../../tests/ComponentValidationScene.h"
#endif

#include <memory>
#include <string>
#include <utility>

namespace Game::Boot {
namespace {

// シーンの型と、コンストラクタに渡す引数を指定して登録する
template<typename TScene, typename... Args>
void Register(Engine::SceneManager& sm, const std::string& name, Args... args) {
	sm.Register(name, [args...]() -> std::unique_ptr<Engine::IScene> { return std::make_unique<TScene>(args...); });
}

// 自動テスト・計測用 (リリースでも使う)
void RegisterAutomatedTestScenes(Engine::SceneManager& sm) {
	Register<PackageValidationScene>(sm, "PackageValidation");
	Register<DlssPackageValidationScene>(sm, "DlssPackageValidation");
	Register<RtLightingValidationScene>(sm, "RtLightingValidation");
	Register<SlimeLifeValidationScene>(sm, "SlimeLifeValidation");
	Register<ChallengeCaptureScene>(sm, "ChallengeCapture");
}

// ゲームの通常の流れ: タイトル → セレクト → ゲーム → ゲームオーバー と、デモ
void RegisterGameScenes(Engine::SceneManager& sm) {
	Register<TitleScene>(sm, "Title");
	Register<SelectScene>(sm, "Select");
	Register<SlimePlayerDemoScene>(sm, "SlimePlayerDemo");
	// 水のデモ (箱庭に積んだ岩から水が流れてたまる。--water-demo)
	Register<WaterDioramaDemoScene>(sm, "WaterDioramaDemo");
	Register<GameScene>(sm, "Game");
	// 評価課題の確認用デモ (スキニング・パーティクルなど)
	Register<AssignmentScene>(sm, "Assignment");
	Register<GameOverScene>(sm, "GameOver");
}

#ifndef NDEBUG
// Debug / Development 限定の検証シーン
void RegisterDevelopmentScenes(Engine::SceneManager& sm) {
	// 流体の検証・サンドボックス・剛体の検証
	Register<FluidValidationScene>(sm, "FluidValidation");
	Register<FluidSandboxValidationScene>(sm, "FluidSandboxValidation");
	Register<RigidbodyValidationScene>(sm, "RigidbodyValidation");
	Register<RigidbodyValidationScene>(sm, "RigidbodyLegacyValidation", false);
	// コンポーネントの検証と、それをエディタで開く版
	Register<ComponentValidationScene>(sm, "ComponentValidation");
	Register<ComponentValidationScene>(sm, "ComponentEditor", true);
	// UI の検証と見本
	Register<UIResultValidationScene>(sm, "UIResultValidation");
	Register<SlimePlayerDemoValidationScene>(sm, "SlimePlayerDemoValidation");
	Register<EditorLoadingValidationScene>(sm, "EditorLoadingValidation");
	Register<UIValidationScene>(sm, "UIValidation");
	Register<UIGalleryScene>(sm, "UIGallery");
	// 描画・影の検証
	Register<GraphicsValidationScene>(sm, "GraphicsValidation");
	Register<ShadowValidationScene>(sm, "ShadowValidation");
	// 流体を使ったゲームの負荷計測 (通常・エディタ・衝突)
	Register<FluidGameBenchmarkScene>(sm, "FluidGameBenchmark");
	Register<FluidGameBenchmarkScene>(sm, "FluidGameBenchmarkEditor", true);
	Register<FluidGameBenchmarkScene>(sm, "FluidCollisionSmoke", false, true);
}
#endif

} // namespace

void RegisterScenes(Engine::SceneManager& sceneManager) {
	UI::Loading::InstallTransitions(sceneManager);
	RegisterAutomatedTestScenes(sceneManager);
	RegisterGameScenes(sceneManager);
#ifndef NDEBUG
	RegisterDevelopmentScenes(sceneManager);
#endif
}

} // namespace Game::Boot
