// GameScene.cpp
// ゲーム本編のシーンの実装。
// 初期化 (シーンファイルの読み込みとシステムの登録)、毎フレームの更新と描画、
// エンティティの生成・破棄の予約処理、タグ検索のキャッシュ、PLAY の開始・停止を扱う。
//
// このファイル: 破棄・初期化 (Initialize)・毎フレームの更新 (Update) の流れ。
// 初期化の各手順は GameSceneSetup.cpp、更新の各手順は GameSceneUpdate.cpp、
// PLAY の開始と停止・ポーズは GameScenePlay.cpp、描画は GameSceneDraw.cpp、
// エディタ用の線の表示は GameSceneGizmos.cpp、エンティティの管理・探索・タグは GameSceneEntities.cpp にある。
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../../Engine/Core/Audio.h"
#include "../../Engine/Core/Input.h"
#include "../../Engine/Core/LoadProfile.h"
#include "../../Engine/Core/NetworkProfiler.h"
#include "../../Engine/Time/TimeManager.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/GameRumble.h"
#include "../UI/SceneMusic.h"
#include <chrono>

namespace Game {

using namespace GameSceneConstants;

// 破棄: シグナルの接続を外し、シーンとシステムを片付ける
GameScene::~GameScene() {
	// 履歴のコマンドが、破棄後のシーンを参照しないようにする。
	EditorUI::ClearHistory();
	// 振動を止める
	Rumble::Stop();
	// ポーズから抜けても音が止まったままにならないようにする
	if (chronoSystem_)
		chronoSystem_->StopLoopSounds();
	if (auto* audio = Engine::Audio::GetInstance())
		audio->SetSEPaused(false);
	// タグとスクリプトの追加・削除のコールバックを外す
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
	ClearScene(); // シーンを完全に空にする (共通の後片付け)

	// ClearScene 内で再接続されてしまうため、デストラクタの最後で確実に切断する
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
	// システムの一覧と、その中の特別なシステムへのポインタを消す
	systems_.clear();
	chronoSystem_ = nullptr;

	// プロファイラに登録したコールバックを外す (破棄後に呼ばれないように)
	Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback(nullptr);
	Engine::NetworkProfiler::GetInstance().SetParameterGetCallback(nullptr);
}

// 初期化: シーンファイルを読み込み、カメラ・システム・タグのキャッシュなどを準備する
void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	Engine::LoadProfile::Scope profile("phase", "GameScene::Initialize");
	// グラフィック設定画面を閉じた状態から始める
	graphicsOpen_ = false;
	graphicsSaveFailed_ = false;
	// ウィンドウを覚える
	dx_ = dx;
	// 描画の窓口
	renderer_ = Engine::Renderer::GetInstance();
	// 最初の Update より先に Draw が呼ばれても (検証シーンなど) 調合室の描画が使えるよう、
	// フレームに依らない共有データはここで用意しておく
	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.scene = this;
	ctx_.eventSystem = &eventSystem_;
	ctx_.pendingSpawns = &pendingSpawns_;
	ctx_.viewportSize = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};
	// シーンファイルが空か本編のシーン (game.json の mainStage) ならスライム本編モード (ChronoSystem で動かす)
	slimePlayerDemo_ = Data::IsSlimePlayerDemoStage(params.stagePath);
	chronoMode_ = slimePlayerDemo_ || Data::IsMainStage(params.stagePath);
	// 戦闘 BGM を流す (スライム本編は ChronoSystem::Reset がステージの曲を流すので、別の曲を展開しない)
	if (!chronoMode_)
		Music::Play(Music::Battle);
	// 本編では GPU の流体を最初の状態に戻す
	if (chronoMode_)
		renderer_->ResetGPUFluid();

	ResetPostEffects();
	// イベント・計測時間・カメラを初期状態にする
	InitializeCameras();

	// シーンファイルを読み込む (読み込めなければ予備のシーンを使う)
	const bool loaded = LoadStageFile(params);

	// GPU 流体の初期化
	Game::FluidSystem::GetInstance()->Initialize(renderer_->GetDevice());

	// エディタ UI の初期化 (アイコンの読み込みなど)
#ifndef NDEBUG
	EditorUI::Initialize(renderer_);
#endif

	// 空 (キューブマップ) の読み込み
	if (auto sky = renderer_->LoadCubeMap("Resources/Textures/PolyHaven/kloppenheim_06_puresky_8k_cube.dds"))
		renderer_->SetSkyboxTexture(sky);

	// パーティクルエディターの初期化
	particleEditor_.Initialize();

	// スクリプトエンジンの初期化
	ScriptEngine::GetInstance()->Initialize();

	// システムの登録 (登録順に処理されるので順序が重要)
	RegisterSystems();
	ConnectRegistryCallbacks();

	// 初期状態を保存する (読み込みで作った場合以外)
	if (!loaded) {
		initialSceneSnapshot_ = EditorUI::SaveToMemory(this);
	}

	// 各システムを初期状態にする
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}

	// タグ検索のキャッシュを作り直す
	RebuildTagCache();
	// Web プロファイラとの双方向のパラメータ通信を登録する
	ConnectNetworkProfiler();

	// 最初の自動 PLAY も、ツールバーの PLAY と同じ「停止時に戻す地点」を持たせる
	FrameStoppedGameCamera();
	editorCameraPos_ = camera_.Position();
	editorCameraRot_ = camera_.Rotation();
	initialSceneSnapshot_ = sceneSnapshot_ = EditorUI::SavePlaySnapshot(this);
	// 常に PLAY 状態から始める
	isPlaying_ = true;
}

// =====================================================
// Update: 処理の大半は各システムに任せる
// =====================================================
// 更新: タグの同期 → 時間計算 → ポーズ/グラフィック設定の入力 → アニメーション → 各システム → 生成・破棄の反映 → ライト
void GameScene::Update() {
	Data::SlimeTuningData().Poll();
	// エディタの Pause 中は、Step で要求された分だけ進める
	if (!renderer_)
		return;
	// コントローラーの振動を進める (プレイ外・ポーズ中・エディタの一時停止中は止める)
	Rumble::Update(
	    std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(), 0.f, .1f),
	    isPlaying_ && !editorPaused_ && !isPaused_);
	if (isPlaying_ && editorPaused_) {
		// Step が要求された分だけ1フレームずつ進める。それ以外は描画のみ (GPU の流体も止める)
		// Step の要求が無ければ世界を止めて何もしない
		if (editorStepFrames_ <= 0) {
			renderer_->SetRogueWorldFrozen(true);
			return;
		}
		// 1 フレーム分を使い、止める前の状態に戻して進める
		--editorStepFrames_;
		renderer_->SetRogueWorldFrozen(worldFrozenBeforeEditorPause_);
	}
// Debug / Development: システムごとの処理時間の計測を始める
#ifndef NDEBUG
	const auto profileBegin = std::chrono::steady_clock::now();
	updateTimings_.clear();
#endif

	// ワールド行列のキャッシュを毎フレーム捨てる
	ClearMatrixCache();

	// 生成・破棄で予約したタグのキャッシュの更新を反映する
	FlushPendingTags();

	// 前フレームからの経過時間
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	// 長く止まった後 (極端なラグ) は 1/60 秒として扱う
	if (dt > kMaxDeltaTime) {
		dt = kFallbackDeltaTime;
	}
	// スライム本編では、操作・カメラ・流体の目標値でエンジンの時計を共有する
	// 別の時計を使うと BeginFrame やエディタの処理時間まで移動量に含まれてしまうため
	if (chronoMode_)
		dt = std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(), 0.f, .05f);

	// システムへ渡す共有データを更新する
	// このフレームの経過時間
	ctx_.dt = dt;
	// ステージをクリアしたか
	const bool stageClear = IsStageClear();

	// PLAY 中の入力 (グラフィック設定・ポーズメニュー・ポーズの切り替え)。画面を開いている間はここで終わる
	if (HandlePlayInput(dt, stageClear))
		return;

	// システムへ渡す共有データ (カメラ・描画・入力・シーン・イベント・生成予約)
	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = isPlaying_;
	// シーン・イベント・生成の予約
	ctx_.scene = this;
	ctx_.eventSystem = &eventSystem_;
	ctx_.pendingSpawns = &pendingSpawns_;

	// ビューポートの初期値はウィンドウ全体 (エディタが無い構成で配置がずれないように)。エディタがあれば後で上書きする
	ctx_.viewportOffset = {0.0f, 0.0f};
	ctx_.viewportSize = {(float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH};

	// GPU での当たり判定は PhysicsSystem に移したので、ここでは何もしない

	// アニメーションの再生時刻を進める
	UpdateAnimations(dt);

	// パーティクルエディタのプレビュー更新
	// パーティクルエディター
	if (!isPaused_) {
		particleEditor_.Update(dt);
	}

	// 各システムを登録順に更新する
#ifndef NDEBUG
	// システムの前の時間を記録する
	const auto profileSystems = std::chrono::steady_clock::now();
	updateTimings_.push_back(
	    {"Before systems", std::chrono::duration<float, std::milli>(profileSystems - profileBegin).count()});
#endif
	for (auto& system : systems_) {
		// PLAY 停止中・ポーズ中はシステムを動かさない (結果画面への遷移中はエンティティが消えている可能性がある)
		if (!isPlaying_ || isPaused_)
			break;
		// クリア後に動かないシステムは飛ばす
		if (stageClear && !system->UpdatesAfterStageClear())
			continue;
#ifndef NDEBUG
		// このシステムの処理時間を記録する
		const auto profileSystemBegin = std::chrono::steady_clock::now();
#endif
		system->Update(registry_, ctx_);
#ifndef NDEBUG
		updateTimings_.push_back(
		    {typeid(*system).name(),
		     std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - profileSystemBegin).count()});
#endif
	}
#ifndef NDEBUG
	// システムの後の時間を記録する
	const auto profileAfterSystems = std::chrono::steady_clock::now();
#endif
	// エディタがある構成では Scene ビューのカメラを EditorUI が操作する (ビューポート上で右ドラッグ中のみ)。
#ifndef USE_IMGUI
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
#endif
	// 停止中のゲーム画面は、シミュレーションを進めずにエディタでのプレイヤーの変更に追従する。
	if (!isPlaying_)
		FrameStoppedGameCamera();
	// カメラの揺れなどを進める
	camera_.Tick(dt);

	// 予約された生成と破棄を反映する
	FlushPendingSpawnsAndDestroys();

	// ライトをレンダラーへ渡す
	ApplyLights();

	// パーティクルエミッターを動かす
	UpdateParticleEmitters(dt);

#ifndef NDEBUG
	// システムの後の処理時間を記録する
	updateTimings_.push_back(
	    {"After systems",
	     std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - profileAfterSystems).count()});
#endif
}

} // namespace Game
