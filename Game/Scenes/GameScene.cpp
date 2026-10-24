// GameScene.cpp
// ゲーム本編のシーンの実装。
// 初期化 (シーンファイルの読み込みとシステムの登録)、毎フレームの更新と描画、
// エンティティの生成・破棄の予約処理、タグ検索のキャッシュ、PLAY の開始・停止を扱う。
//
// このファイル: 破棄・初期化・毎フレームの更新 (Update とその手順)・PLAY の開始と停止・ポーズ。
// 描画は GameSceneDraw.cpp、エディタ用の線の表示は GameSceneGizmos.cpp、
// エンティティの管理・探索・タグは GameSceneEntities.cpp にある。
#include <cfloat>
// シーンの BGM
#include "../UI/SceneMusic.h"
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
// プレイヤー向けの UI (共通部品・強化カード・グラフィック設定)
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../UI/GraphicsUI.h"
// 流体の放出と時間の管理 (エンジン側)
#include "../../Engine/FluidEmission.h"
#include "../../Engine/Time/TimeManager.h"
// 缶の装備 (旧アクションゲーム)
#include "../CanLoadout.h"
#include "../ObjectTypes.h"
// 音・パス・シーン管理 (エンジン側)
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
// エディタとスクリプト
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
// ゲームの処理を分担するシステム群
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // 武器の管理
#include "../../Engine/Input.h"      // キーボード・マウス入力

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // キーフレームのモーション
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/PlayerActionSystem.h" // プレイヤーアクション
#include "../Systems/CombatSystem.h"       // 戦闘の当たり判定
#include "../Systems/CanAbilitySystem.h"
#include "../Systems/EnemyAISystem.h"      // 敵の AI
#include "../Systems/BossActionSystem.h"    // ボスの AI
#include "../Systems/WaveSystem.h"         // ウェーブ管理
#include "../Scripts/BossTestScript.h"      // ボスの生成に使う

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // ポストエフェクト
// Web プロファイラ (外部ツールから値を見たり変えたりする)
#include "../../Engine/NetworkProfiler.h"
// JSON の読み書き
#include "../../externals/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h" // ImGui (エディタの画面)
#include <Windows.h> // デバッグ出力 (OutputDebugStringA) を使うため
#include <algorithm>
#include <cmath>
#include <filesystem> // 空のテクスチャ検索などのファイル操作

namespace Game {

// 破棄: シグナルの接続を外し、シーンとシステムを片付ける
GameScene::~GameScene() {
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
    waveSystem_ = nullptr;

    // プロファイラに登録したコールバックを外す (破棄後に呼ばれないように)
    Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback(nullptr);
    Engine::NetworkProfiler::GetInstance().SetParameterGetCallback(nullptr);
}

// 初期化: シーンファイルを読み込み、カメラ・システム・タグのキャッシュなどを準備する
void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
    // グラフィック設定画面を閉じた状態から始める
    graphicsOpen_=false;graphicsSaveFailed_=false;
	// ウィンドウを覚える
	dx_ = dx;
    // 戦闘 BGM を流す
    Music::Play(Music::Battle);
	// 描画の窓口
	renderer_ = Engine::Renderer::GetInstance();
	// シーンファイルが空か本編のシーン (game.json の mainStage) ならスライム本編モード (ChronoSystem で動かす)
	chronoMode_ = Data::IsMainStage(params.stagePath);
	// 本編では GPU の流体を最初の状態に戻す
	if (chronoMode_) renderer_->ResetGPUFluid();

	// 前のシーンやテストで変えたポストエフェクトを初期状態に戻す
	if (renderer_) {
		// ノイズ・歪み・色ずれ・周辺減光・走査線・SAN 値の効果をすべて 0 にする
		auto ppParams = renderer_->GetPostProcessParams();
		ppParams.noiseStrength = 0.0f;
		ppParams.distortion = 0.0f;
		ppParams.chromaShift = 0.0f;
		ppParams.vignette = 0.0f;
		ppParams.scanline = 0.0f;
		ppParams.san = 0.0f;
		renderer_->SetPostProcessParams(ppParams);
		renderer_->SetPostEffect("Default");
	}

	// イベント・計測時間・カメラを初期状態にする
	eventSystem_.Clear();
	playTime_ = 0.0f;
	// ゲームのカメラ (視野角 45 度・画面の縦横比・描画範囲 0.1〜10000)
	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 10000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
	
	// エディタのカメラも同じ初期位置にする
	editorCameraPos_ = {0, 2, -5};
	editorCameraRot_ = {0.2f, 0, 0};
	editorCamera_.Initialize();
	editorCamera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 10000.0f);
	editorCamera_.SetPosition(0, 2, -5);
	editorCamera_.SetRotation(0.2f, 0, 0);

	// 環境光
	renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});

	// シーンファイルを読み込めたか
	bool loaded = false;
	// シーンファイルを読み込む (指定が無ければ本編のシーン)
	try {
		// 読み込むパス
		std::string scenePath = params.stagePath.empty() ? EditorUI::GetUnifiedProjectPath(Data::MainStagePath()) : params.stagePath;
		// UTF-8 のパスを FromUTF8 で変換して日本語のパスにも対応する
		if (std::filesystem::exists(Engine::PathUtils::FromUTF8(scenePath))) {
			// 見つかったことをデバッグ出力に残し、読み込む
			OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
			EditorUI::LoadScene(this, scenePath);
			isPlaying_ = true; // 読み込めたら PLAY 状態から始める
			loaded = true;
		} else {
			// 見つからなかったことを残す
			OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
		}

	} catch (const std::exception& e) {
		// 読み込み中の例外は内容をデバッグ出力とメッセージボックスで知らせる
		std::string msg = "[GameScene] EXCEPTION during scene load: " + std::string(e.what()) + "\n";
		OutputDebugStringA(msg.c_str());
		MessageBoxA(NULL, msg.c_str(), "Scene Load Error", MB_OK | MB_ICONERROR);
	}

	// 読み込みに失敗した、またはエンティティが空のときは、予備のシーン (game.json の fallbackStage:
	// 太陽・地形・空・プレイヤー) を追加で読み込む
	if (registry_.storage<entt::entity>().empty() || !loaded) {
		const std::string fallback = EditorUI::GetUnifiedProjectPath(Data::Config().fallbackStage);
		OutputDebugStringA(("[GameScene] Loading fallback scene: " + fallback + "\n").c_str());
		EditorUI::AddScene(this, fallback);
	}

	// GPU 流体の初期化
	Game::FluidSystem::GetInstance()->Initialize(renderer_->GetDevice());

	// エディタ UI の初期化 (アイコンの読み込みなど)
#ifndef NDEBUG
	EditorUI::Initialize(renderer_);
#endif

	// 空 (キューブマップ) の読み込み
	if(auto sky=renderer_->LoadCubeMap("Resources/Textures/PolyHaven/kloppenheim_06_puresky_8k_cube.dds"))renderer_->SetSkyboxTexture(sky);

// パーティクルエディターの初期化
	particleEditor_.Initialize();

	// スクリプトエンジンの初期化
	ScriptEngine::GetInstance()->Initialize();

	// システムの登録 (登録順に処理されるので順序が重要)
	// 前のシステムをすべて消す
	systems_.clear();
	chronoSystem_ = nullptr;
	waveSystem_ = nullptr;
	// スライム本編: ChronoSystem と物理だけで動く
	if (chronoMode_) {
		// スライム本編のシステム
		auto chrono = std::make_unique<ChronoSystem>(this);
		chronoSystem_ = chrono.get();
		systems_.push_back(std::move(chrono));
		// エディタで追加した Rigidbody も本編シーンで物理演算する
		systems_.push_back(std::make_unique<PhysicsSystem>());
	// 旧アクションゲーム: 入力 → 行動 → 移動 → 物理 → 敵 → 戦闘 → カメラ → 体力 → ウェーブ の順に処理する
	} else {
	systems_.push_back(std::make_unique<PlayerInputSystem>());
	systems_.push_back(std::make_unique<PlayerActionSystem>());  // 攻撃・パリィ・回避
	systems_.push_back(std::make_unique<WeaponSystem>());        // 武器の管理とアニメーション
	systems_.push_back(std::make_unique<CharacterMovementSystem>());
	systems_.push_back(std::make_unique<PhysicsSystem>());
	systems_.push_back(std::make_unique<CanAbilitySystem>());
	systems_.push_back(std::make_unique<BossActionSystem>());   // ボスの移動を雑魚の押し合いより先に処理する
	systems_.push_back(std::make_unique<EnemyAISystem>());      // 戦闘の判定より先に敵同士を離す
	systems_.push_back(std::make_unique<CombatSystem>());         // 攻撃判定 (Hitbox) と食らい判定 (Hurtbox) の判定
	systems_.push_back(std::make_unique<CameraFollowSystem>());
	systems_.push_back(std::make_unique<HealthSystem>());
	auto wave = std::make_unique<WaveSystem>();
	waveSystem_ = wave.get();
	systems_.push_back(std::move(wave));

	// スクリプト・音・UI・モーション・ポストエフェクト・後片付け
	auto scriptSys = std::make_unique<ScriptSystem>();
	scriptSys->SetScene(this);
	systems_.push_back(std::move(scriptSys));

// 音・UI・モーション
	systems_.push_back(std::make_unique<AudioSystem>());
	systems_.push_back(std::make_unique<UISystem>());
	systems_.push_back(std::make_unique<MotionSystem>());
	systems_.push_back(std::make_unique<PostProcessSystem>()); // ポストエフェクト
	// 破棄の予約を処理するシステム
	systems_.push_back(std::make_unique<CleanupSystem>());
	}

	// タグ・スクリプトの追加/削除を検知するコールバックを登録する (二重登録しないよう一度外す)
	// リスナー登録
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);

	// 初期状態を保存する (読み込みで作った場合以外)
	if (!loaded) {
		initialSceneSnapshot_ = EditorUI::SaveToMemory(this);
	}

// 各システムを初期状態にする
	// 各Systemのリセット
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}

// タグ検索のキャッシュを作り直す
	tagCache_.clear();
	pendingTagSync_.clear();
	pendingTagRemoved_.clear();
	// タグを持つエンティティをタグごとの一覧に入れる
	auto tagInitView = registry_.view<TagComponent>();
	for (auto entity : tagInitView) {
		const auto tag = tagInitView.get<TagComponent>(entity).tag;
		tagCache_[tag].push_back(entity);
	}

// Web プロファイラとの双方向のパラメータ通信を登録する
	// Web プロファイラから値を変更されたとき: 環境光の色とプレイヤーの速さ
	Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback([this](const std::string& target, const std::string& prop, float val) {
		// 環境光の R / G / B のどれかを変える
		if (target == "AmbientColor") {
			auto current = renderer_->GetAmbientColor();
			if (prop == "R") current.x = val;
			else if (prop == "G") current.y = val;
			else if (prop == "B") current.z = val;
			renderer_->SetAmbientColor(current);
		// プレイヤーの移動の速さを変える
		} else if (target == "Player" && prop == "Speed") {
			// 最初のプレイヤーを探して書き換える
			const auto& players = GetEntitiesByTag(TagType::Player);
			if (!players.empty()) {
				entt::entity p = players[0];
				if (registry_.valid(p) && registry_.all_of<CharacterMovementComponent>(p)) {
					registry_.get<CharacterMovementComponent>(p).speed = val;
				}
			}
		}
	});

	// Web プロファイラへ現在の値を返す
	Engine::NetworkProfiler::GetInstance().SetParameterGetCallback([this]() -> std::string {
		// 環境光の色
		nlohmann::json j;
		auto amb = renderer_->GetAmbientColor();
		j["AmbientColor"]["R"] = amb.x;
		j["AmbientColor"]["G"] = amb.y;
		j["AmbientColor"]["B"] = amb.z;
		
		j["Player"]["Speed"] = 15.0f; // プレイヤーが見つからないときの既定値
		const auto& players = GetEntitiesByTag(TagType::Player);
		if (!players.empty()) {
			// 最初のプレイヤーの速さ
			entt::entity p = players[0];
			if (registry_.valid(p) && registry_.all_of<CharacterMovementComponent>(p)) {
				j["Player"]["Speed"] = registry_.get<CharacterMovementComponent>(p).speed;
			}
		}
		// JSON の文字列にして返す
		return j.dump();
	});

	// 常に PLAY 状態から始める
	isPlaying_ = true;
}

// =====================================================
// Update: 処理の大半は各システムに任せる
// =====================================================
// 更新: タグの同期 → 時間計算 → ポーズ/グラフィック設定の入力 → アニメーション → 各システム → 生成・破棄の反映 → ライト
void GameScene::Update() {
	// エディタの Pause 中は、Step で要求された分だけ進める
	if (!renderer_) return;
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

	// 旧アクションゲーム: Y キーで流体のデバッグ矢印の表示を切り替える
	if (!chronoMode_ && Engine::Input::GetInstance()->Trigger(0x15)) { // エディタ・旧モードの流体診断用
		renderer_->SetDrawFluidDebugArrows(!renderer_->GetDrawFluidDebugArrows());
	}

	// ワールド行列のキャッシュを毎フレーム捨てる
	ClearMatrixCache();

	// 生成・破棄で予約したタグのキャッシュの更新を反映する
	FlushPendingTags();

	// 前フレームからの経過時間
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	if (dt > 1.0f / 10.0f)
		dt = 1.0f / 60.0f; // 長く止まった後 (極端なラグ) は 1/60 秒として扱う
	// スライム本編では、操作・カメラ・流体の目標値でエンジンの時計を共有する
	// 別の時計を使うと BeginFrame やエディタの処理時間まで移動量に含まれてしまうため
	if(chronoMode_)dt=std::clamp(Engine::TimeManager::GetInstance().GetUnscaledDeltaTime(),0.f,.05f);

	// システムへ渡す共有データを更新する
	// このフレームの経過時間
	ctx_.dt = dt;
	// 戦闘の流れ (ヒットストップなど) の状態
	ctx_.combatFlow = &combatFlow_;
	// 旧アクションゲームの PLAY 中だけ戦闘の流れの時間を進める
	if (isPlaying_ && !isPaused_ && !chronoMode_) combatFlow_.Tick(dt);
	// ステージをクリアしたか
	const bool stageClear = IsStageClear();

	// PLAY 中の入力 (グラフィック設定・ポーズメニュー・ポーズの切り替え)。画面を開いている間はここで終わる
	if (HandlePlayInput(dt, stageClear)) return;

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
	ctx_.viewportOffset = { 0.0f, 0.0f };
	ctx_.viewportSize = { (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };

	// GPU での当たり判定は PhysicsSystem に移したので、ここでは何もしない

	// アニメーションの再生時刻を進める
	UpdateAnimations(dt);

	// 旧アクションゲーム: 缶を選ぶラジアルメニュー
	UpdateCanRadialMenu(stageClear);

	// パーティクルエディタのプレビュー更新
	// パーティクルエディター
	if (!isPaused_) {
		particleEditor_.Update(dt);
	}

	// 各システムを登録順に更新する
#ifndef NDEBUG
	// システムの前の時間を記録する
	const auto profileSystems = std::chrono::steady_clock::now();
	updateTimings_.push_back({"Before systems", std::chrono::duration<float, std::milli>(profileSystems - profileBegin).count()});
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
		updateTimings_.push_back({typeid(*system).name(), std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - profileSystemBegin).count()});
#endif
	}
#ifndef NDEBUG
	// システムの後の時間を記録する
	const auto profileAfterSystems = std::chrono::steady_clock::now();
#endif
	// 旧アクションゲーム: プレイヤーの頭上に浮かぶ缶を追従させる
	UpdateCanFollow(dt, stageClear);

	// エディタがある構成では Scene ビューのカメラを EditorUI が操作する (ビューポート上で右ドラッグ中のみ)。
	// 以前はここで停止中に camera_ を生のキー入力で動かしており、Inspector の入力中も WASD でカメラが動いていた
#ifndef USE_IMGUI
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
#endif
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
	updateTimings_.push_back({"After systems", std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - profileAfterSystems).count()});
#endif
}

// タグのキャッシュを遅れて同期する (生成直後と破棄時に予約したもの)
void GameScene::FlushPendingTags() {
	// タグのキャッシュを遅れて同期する (生成直後と破棄時に予約したもの)
	// 予約があるときだけ処理する
	if (!pendingTagRemoved_.empty() || !pendingTagSync_.empty()) {
		// 1. 削除・変更予定のエンティティを全タグの一覧から取り除く
		std::vector<entt::entity> toRemove = std::move(pendingTagRemoved_);
		// 同期待ちのエンティティはタグが変わった可能性があるので、古い一覧から一旦消しておく
		for (auto e : pendingTagSync_) {
			toRemove.push_back(e);
		}

		// それぞれをすべてのタグの一覧から消す
		for (auto e : toRemove) {
			for (auto& pair : tagCache_) {
				auto& vec = pair.second;
				// すべてのタグの一覧からそのエンティティを消す
				vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
			}
		}

		// 2. 最新のタグで一覧に入れ直す
		std::vector<entt::entity> toSync = std::move(pendingTagSync_);
		// まだ存在するものだけ入れ直す
		for (auto e : toSync) {
			if (registry_.valid(e)) {
				SyncTag(e);
			}
		}
	}
}

// PLAY 中の入力: F7 でグラフィック設定、ポーズメニュー、Esc / START でポーズ。
// グラフィック設定やポーズメニューを処理したフレームは true を返す (Update はそこで終わる)
bool GameScene::HandlePlayInput(float dt, bool stageClear) {
	// PLAY 中の入力: F7 でグラフィック設定、ポーズメニュー、Esc / START でポーズ
	if (isPlaying_) {
        // F7: グラフィック設定画面を開閉する (開いている間はポーズ)
        if(!stageClear&&UI::Pressed(DIK_F7)){SetPaused(true);graphicsOpen_=!graphicsOpen_;return true;}
        // グラフィック設定画面: 変更があれば設定ファイルへ保存する
        if(!stageClear&&isPaused_&&graphicsOpen_){
            // UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
            UI::Canvas ui(renderer_,ctx_.viewportSize.x,ctx_.viewportSize.y);if(ctx_.useOverrideMouse)ui.SetPointer(ctx_.overrideMouseX,ctx_.overrideMouseY);
            // 変更前の設定
            auto previous=renderer_->GetGraphicsSettings();
            // 入力を処理し、戻る操作なら画面を閉じる
            if(UI::UpdateGraphics(ui,*renderer_))graphicsOpen_=false;
            // 変更後の設定
            const auto& current=renderer_->GetGraphicsSettings();
            // 効果のどれかが変わったら保存し、失敗したら覚えておく
            if(previous.bloom!=current.bloom||previous.lensFlare!=current.lensFlare||previous.grading!=current.grading||previous.ambientOcclusion!=current.ambientOcclusion||previous.motionBlur!=current.motionBlur||previous.dof!=current.dof||previous.exposure!=current.exposure||previous.dlssQuality!=current.dlssQuality||previous.rtShadows!=current.rtShadows||previous.rtReflections!=current.rtReflections||previous.rtIndirect!=current.rtIndirect)
                graphicsSaveFailed_=!renderer_->SaveGraphicsSettings();
            return true;
        }
		// ポーズメニューのボタン入力 (グラフィック設定 / タイトルへ / 再開)
        if (!stageClear && isPaused_) {
            // UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
            UI::Canvas ui(renderer_,ctx_.viewportSize.x,ctx_.viewportSize.y);if(ctx_.useOverrideMouse)ui.SetPointer(ctx_.overrideMouseX,ctx_.overrideMouseY);
            // グラフィック設定ボタン・G・パッドの BACK で設定画面を開く
            if(ui.Click(UI::GraphicsButton)||UI::Pressed(DIK_G)||UI::PadPressed(XINPUT_GAMEPAD_BACK)){graphicsOpen_=true;return true;}
            auto* input=Engine::Input::GetInstance();
            // スライム本編の強化パレット画面かどうか (ボタンの位置が違う)
            bool roguePause=chronoMode_&&!registry_.view<Chrono::InkPlayer>().empty();
            // タイトルへ戻るボタンか Tab でタイトルへ
            if (ui.Click(roguePause?UI::PaletteTitle:UI::Title) || input->Trigger(DIK_TAB)) {
                Engine::SceneManager::GetInstance()->RequestChange("Title");return true;
            }
            // 再開ボタン・Enter・パッドの B でポーズを解く (同じ押下を他で使わないよう止める)
            if (ui.Click(roguePause?UI::PaletteResume:UI::Resume) || input->Trigger(DIK_RETURN) || input->PadTrigger(XINPUT_GAMEPAD_B)) {
                SetPaused(false);input->LatchPad();
            }
        }
		// Esc (0x01 = DIK_ESCAPE) / START でポーズを切り替える
		if (!stageClear && (Engine::Input::GetInstance()->Trigger(0x01) || Engine::Input::GetInstance()->PadTrigger(XINPUT_GAMEPAD_START))) {
            SetPaused(!isPaused_);Engine::Input::GetInstance()->LatchPad();
        }

		// ステージクリア中はカーソルを出す。通常プレイ中はプレイ時間を計測する
		if (stageClear) {
			Engine::WindowDX::SetCursorVisible(true);
		} else if (!isPaused_) {
			// 強化の選択画面・スライムの調合画面が開いているか
			bool upgradeOpen=false;for(auto e:registry_.view<Chrono::InkPlayer>())upgradeOpen|=registry_.get<Chrono::InkPlayer>(e).rogue.menu;
			upgradeOpen|=chronoSystem_&&chronoSystem_->SlimeLabOpen();
			// カットシーン中か
			const bool cinematic = chronoSystem_ && chronoSystem_->CinematicActive();
			// どちらでもなければプレイ時間を進める
			if(!upgradeOpen&&!cinematic)playTime_ += dt;
			
			// ラジアルメニューを開いているか調べる
			bool isRadialMenuOpen = false;
			// プレイヤーの入力を調べる
			auto piView = registry_.view<PlayerInputComponent>();
			for (auto e : piView) {
				if (piView.get<PlayerInputComponent>(e).isRadialMenuOpen) {
					isRadialMenuOpen = true;
					break;
				}
			}

			// PLAY 中はマウスカーソルを画面中央に固定する (メニューを開いていない場合)
			for(auto e:registry_.view<Chrono::InkPlayer>())if(registry_.get<Chrono::InkPlayer>(e).rogue.menu)isRadialMenuOpen=true;
            if (!upgradeOpen && !isRadialMenuOpen && dx_ && dx_->GetHwnd()) {
				// 画面中央をスクリーン座標に直してカーソルを置く
				POINT center = { (LONG)Engine::WindowDX::kW / 2, (LONG)Engine::WindowDX::kH / 2 };
				ClientToScreen(dx_->GetHwnd(), &center);
				SetCursorPos(center.x, center.y);
			}
		}
	}
	return false;
}

// アニメーション (エンジン側の処理なのでシーンに残している)
void GameScene::UpdateAnimations(float dt) {
	// アニメーション (エンジン側の処理なのでシーンに残している)
	// アニメーションの再生時刻を進める (エンティティごとに並列で処理する)
	auto animView = registry_.view<AnimatorComponent, MeshRendererComponent>();
	// PLAY 中でポーズしていなければ、対象のエンティティを集める
	if (isPlaying_ && !isPaused_) {
		std::vector<entt::entity> animEntities;
		animView.each([&](entt::entity entity, auto&, auto&) { animEntities.push_back(entity); });

		// 64 件ずつ別のスレッドで処理する
		if (!animEntities.empty()) {
			Engine::JobSystem::Dispatch((uint32_t)animEntities.size(), 64, [&](uint32_t i) {
				// このエンティティのアニメーションとモデル
				auto entity = animEntities[i];
				auto& anim = registry_.get<AnimatorComponent>(entity);
				auto& meshWrapper = registry_.get<MeshRendererComponent>(entity);

				// 有効で再生中のものだけ進める
				if (anim.enabled && anim.isPlaying) {
						// ボスは戦闘の流れの速さ (スロー演出など) に合わせて再生する
						const float animDt = (registry_.all_of<BossActionComponent>(entity) || registry_.all_of<Chrono::Boss>(entity))
							? dt * combatFlow_.EnemyScale() : dt;
						// 1 秒 60 フレームとして、再生の速さを掛けて進める
						anim.time += animDt * 60.0f * anim.speed;
						// クロスフェード中は残り時間を減らし、直前のクリップも進める
						if (anim.crossfadeTimer > 0.0f) {
							anim.crossfadeTimer -= animDt;
							if (anim.crossfadeTimer < 0.0f) anim.crossfadeTimer = 0.0f;
							anim.prevTime += animDt * 60.0f * anim.speed;
					}

					// クリップの長さを超えたら、ループならはじめから・ループでなければ最後で止める
					auto* m = renderer_->GetModel(meshWrapper.modelHandle);
					if (m) {
						// モデルのアニメーションの一覧から、再生中のクリップを探す
						const auto& data = m->GetData();
						for (const auto& a : data.animations) {
							if (a.name == anim.currentAnimation) {
								if (anim.time > a.duration) {
									if (anim.loop)
										anim.time = std::fmod(anim.time, a.duration);
									else {
										anim.time = a.duration;
										anim.isPlaying = false;
									}
								}
							}
							// クロスフェード中の直前のクリップも同様に処理する
							if (anim.crossfadeTimer > 0.0f && a.name == anim.prevAnimation) {
								if (anim.prevTime > a.duration) {
									if (anim.prevLoop) anim.prevTime = std::fmod(anim.prevTime, a.duration);
									else anim.prevTime = a.duration;
								}
							}
						}
					}
				}
			});
			// すべての処理が終わるまで待つ
			Engine::JobSystem::Wait();
		}
	}
}

// 旧アクションゲーム: 缶を選ぶラジアルメニュー (選択と描画)
void GameScene::UpdateCanRadialMenu(bool stageClear) {
	// 旧アクションゲーム: 缶を選ぶラジアルメニュー
	if (isPlaying_ && !isPaused_) {
		// プレイヤーの入力を調べる
		auto piView = registry_.view<PlayerInputComponent>();
		for (auto e : piView) {
			auto& pi = registry_.get<PlayerInputComponent>(e);
			// メニューを開いている間だけ処理する
			if (pi.isRadialMenuOpen) {
				Engine::WindowDX::SetCursorVisible(true); // 開いている間はカーソルを表示する
				
				// 画面中央とマウスの位置から、マウスがどの扇形を指しているかを求める
				float centerX = ctx_.viewportSize.x > 0 ? ctx_.viewportSize.x * 0.5f : (float)Engine::WindowDX::kW * 0.5f;
				float centerY = ctx_.viewportSize.y > 0 ? ctx_.viewportSize.y * 0.5f : (float)Engine::WindowDX::kH * 0.5f;
				
				// マウスの位置 (エディタの Game ビューなら換算済みの座標、それ以外はビューポートから内部解像度へ換算する)
				float mx, my;
				if (ctx_.useOverrideMouse) {
					mx = ctx_.overrideMouseX;
					my = ctx_.overrideMouseY;
				} else {
					ctx_.input->GetMousePos(mx, my);
					if (ctx_.viewportSize.x > 0 && ctx_.viewportSize.y > 0) {
						mx = (mx - ctx_.viewportOffset.x) * (float)Engine::WindowDX::kW / ctx_.viewportSize.x;
						my = (my - ctx_.viewportOffset.y) * (float)Engine::WindowDX::kH / ctx_.viewportSize.y;
					}
				}
				
				// 中心からマウスへの角度 (0〜2π)
				float dx_mouse = mx - centerX;
				float dy_mouse = my - centerY;
				float angle = std::atan2(dy_mouse, dx_mouse);
				if (angle < 0) angle += 3.14159f * 2.0f;
				
				// 装備中の缶の数で円を等分し、角度から選択中の番号を決める
				const auto& equippedCans = CanLoadout::GetEquipped();
				const int canCount = (std::max)(1, CanLoadout::Count());
				const float segment = (3.14159f * 2.0f) / static_cast<float>(canCount);
				int selectedIndex = static_cast<int>(angle / segment);
				// 範囲外にならないようにする
				if (selectedIndex < 0) selectedIndex = 0;
				if (selectedIndex >= canCount) selectedIndex = canCount - 1;
				
				// 選択中の缶を入力へ設定する
				pi.selectedCan = equippedCans[selectedIndex];

				// 描画 (ImGui を使わずレンダラーのスプライトで描く)
				// 円の半径とアイコンの大きさ
				float radius = 120.0f;
				float boxSize = 80.0f;
				
				// 円の画像
				auto texH = ctx_.renderer->LoadTexture2D("Resources/Textures/ball.png");

				// 各缶のアイコン (円) と名前を円周上に並べる。缶の種類ごとに色を変え、選択中は明るくする
				for (int i = 0; i < canCount; ++i) {
					// この扇形の始まりの角度
					float startAngle = segment * static_cast<float>(i);
					float textAngle = startAngle + segment * 0.5f; // 扇形の中心の角度
					
					// アイコンの中心
					float px = centerX + std::cos(textAngle) * radius;
					float py = centerY + std::sin(textAngle) * radius;
					
					// 既定の灰色。缶の種類ごとに色を決め、選択中は濃く、それ以外は薄くする
					Engine::Vector4 color = {100.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Fire) color = (i == selectedIndex) ? Engine::Vector4{255.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{150.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Water) color = (i == selectedIndex) ? Engine::Vector4{100.0f/255.0f, 150.0f/255.0f, 255.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{50.0f/255.0f, 80.0f/255.0f, 150.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Thunder) color = (i == selectedIndex) ? Engine::Vector4{255.0f/255.0f, 255.0f/255.0f, 100.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{150.0f/255.0f, 150.0f/255.0f, 50.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Soda) color = (i == selectedIndex) ? Engine::Vector4{100.0f/255.0f, 255.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{45.0f/255.0f, 145.0f/255.0f, 130.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Ice) color = (i == selectedIndex) ? Engine::Vector4{120.0f/255.0f, 235.0f/255.0f, 1.0f, 220.0f/255.0f} : Engine::Vector4{45.0f/255.0f, 125.0f/255.0f, 165.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Magnet) color = (i == selectedIndex) ? Engine::Vector4{215.0f/255.0f, 100.0f/255.0f, 1.0f, 220.0f/255.0f} : Engine::Vector4{110.0f/255.0f, 45.0f/255.0f, 150.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Acid) color = (i == selectedIndex) ? Engine::Vector4{160.0f/255.0f, 1.0f, 75.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{70.0f/255.0f, 145.0f/255.0f, 30.0f/255.0f, 180.0f/255.0f};
					if (equippedCans[i] == CanType::Bubble) color = (i == selectedIndex) ? Engine::Vector4{1.0f, 115.0f/255.0f, 215.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{155.0f/255.0f, 55.0f/255.0f, 125.0f/255.0f, 180.0f/255.0f};
					
					// 枠の代わりに少し大きい円を背景に置く
					Engine::Renderer::SpriteDesc border;
					border.x = px - (boxSize + 8.0f) * 0.5f;
					border.y = py - (boxSize + 8.0f) * 0.5f;
					border.w = boxSize + 8.0f;
					border.h = boxSize + 8.0f;
					border.color = {1.0f, 1.0f, 1.0f, 0.5f};
					if (i == selectedIndex) border.color = {1.0f, 1.0f, 0.2f, 0.8f}; // 選択中は黄色く光らせる
					border.layer = 199;
					ctx_.renderer->DrawSprite(texH, border);

					// 缶の色の円
					Engine::Renderer::SpriteDesc box;
					box.x = px - boxSize * 0.5f;
					box.y = py - boxSize * 0.5f;
					box.w = boxSize;
					box.h = boxSize;
					box.color = color;
					box.layer = 200;
					ctx_.renderer->DrawSprite(texH, box);

// 缶の名前
					const char* canName = CanLoadout::Name(equippedCans[i]);
					// 文字の幅を測って中央に書く
					float tw = ctx_.renderer->MeasureTextWidth(canName, 0.3f);
					ctx_.renderer->DrawString(canName, px - tw * 0.5f, py - 12.0f, 0.3f, {1.0f, 1.0f, 1.0f, 1.0f});
				}
			} else {
				// メニューを閉じたら、ポーズ・クリア中以外は調合画面のときだけカーソルを出す
				if (!isPaused_ && !stageClear) Engine::WindowDX::SetCursorVisible(chronoSystem_&&chronoSystem_->SlimeLabOpen());
			}
		}
	}
}

// 旧アクションゲーム: プレイヤーの頭上に浮かぶ缶を、少し遅れて追従させる
void GameScene::UpdateCanFollow(float dt, bool stageClear) {
	// 旧アクションゲーム: プレイヤーの頭上に浮かぶ缶を、少し遅れて追従させる
	if (isPlaying_ && !isPaused_ && !stageClear) {
		// プレイヤーは PlayerActionSystem の後に移動するので、缶はここで付ける。
		// 最終的な位置に合わせつつ、旋回や急停止のときは周りの流体と同じだけ少し遅れて付いてくる
		auto playersWithCan = registry_.view<PlayerActionComponent, TransformComponent>();
		playersWithCan.each([&](entt::entity, PlayerActionComponent& pa, TransformComponent& playerTc) {
			// 缶が無いか、消えているか、見た目が無ければ何もしない
			if (pa.canEntity == entt::null || !registry_.valid(pa.canEntity) ||
				!registry_.all_of<TransformComponent, MeshRendererComponent>(pa.canEntity)) return;
			// 缶の位置と見た目
			auto& canTc = registry_.get<TransformComponent>(pa.canEntity);
			auto& canMesh = registry_.get<MeshRendererComponent>(pa.canEntity);
			// 初回はプレイヤーの位置から始める
			if (!pa.canFollowInitialized) {
				pa.canFollowPos = playerTc.translate;
				pa.canFollowInitialized = true;
			}
			// プレイヤーとの差
			const float dx = playerTc.translate.x - pa.canFollowPos.x;
			const float dy = playerTc.translate.y - pa.canFollowPos.y;
			const float dz = playerTc.translate.z - pa.canFollowPos.z;
			// 大きく離れたら即座に移動し、それ以外は指数的に近づける
			if (dx * dx + dy * dy + dz * dz > 100.0f) {
				pa.canFollowPos = playerTc.translate;
			} else {
				// 経過時間を 1/30 秒までに抑え、毎秒 14 の速さで近づく
				const float followDt = (std::clamp)(dt, 0.0f, 1.0f / 30.0f);
				const float blend = 1.0f - std::exp(-14.0f * followDt);
				pa.canFollowPos.x += dx * blend;
				pa.canFollowPos.y += dy * blend;
				pa.canFollowPos.z += dz * blend;
			}
			// 上下にゆらゆら揺らしながら回転させる
			canTc.translate = pa.canFollowPos;
			canTc.translate.y += 0.35f + std::sin(pa.totalTime * 3.0f) * 0.025f;
			canTc.rotate.y += dt * 2.0f;
			canTc.rotate.z = std::sin(pa.totalTime * 2.0f) * 0.2f;
			// 液状化中の水たまりは缶より薄いので、通常の体に戻ってから少し待って缶を表示する
			// 液状化中は 0 に戻し、それ以外は時間を数える (最大 1 秒)
			pa.canRevealTimer = pa.state == PlayerActionState::Liquefy ? 0.0f :
				(std::min)(1.0f, pa.canRevealTimer + (std::max)(dt, 0.0f));
			// 0.45 秒経ったら表示する
			canMesh.enabled = pa.canRevealTimer >= 0.45f;
		});
	}
}

// 予約された生成 (弾など) と破棄を反映する
void GameScene::FlushPendingSpawnsAndDestroys() {
	// 予約された生成 (弾など) と破棄を反映する
	// 生成・破棄の予約を取り出す (ロック中に取り出すだけにして、削除はロックの外で行う)
	std::vector<entt::entity> destroysToProcess;
	{
		// 生成・破棄の予約を守るロック
		std::lock_guard<std::mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 生成は直接 registry_.create() で行うので、ここの予約は使っていない (空にするだけ)
			pendingSpawns_.clear();
		}

		if (!pendingDestroys_.empty()) {
			// 同じエンティティの重複を除いて二重に破棄しない
			// 並べ替えて重複を消す
			std::sort(pendingDestroys_.begin(), pendingDestroys_.end());
			pendingDestroys_.erase(std::unique(pendingDestroys_.begin(), pendingDestroys_.end()), pendingDestroys_.end());
			
			// 処理する一覧へ移す
			destroysToProcess = std::move(pendingDestroys_);
			pendingDestroys_.clear();
		}
	}

	// 破棄はロックの外で行う (OnDestroy の中から DestroyObject が呼ばれてもデッドロックしないように)
	for (auto id : destroysToProcess) {
		// まだ存在するものだけ削除する
		if (registry_.valid(id)) {
			RemoveEntity(id);
		}
	}
}

// ライト (描画の設定なのでシーンに残している)
void GameScene::ApplyLights() {
	// ライト (描画の設定なのでシーンに残している)
	// ライトの反映: 平行光源1つ・点光源・スポットライトをレンダラーへ渡す
	if (renderer_) {
		// 使った点光源・スポットライトの数と、平行光源があったか
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		// 平行光源
		auto dirLightView = registry_.view<DirectionalLightComponent, TransformComponent>();
		// 平行光源は最初に見つかった1つだけ使う。向きは回転行列の Z 軸
		dirLightView.each([&](auto, const DirectionalLightComponent& dl, const TransformComponent& tc) {
			if (dl.enabled && !hasDirLight) {
				// 回転から向きを求め、色に強さを掛ける
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
				renderer_->SetDirectionalLight(dir, color, true);
				hasDirLight = true;
			}
		});

		auto plView = registry_.view<PointLightComponent, TransformComponent>();
		// 点光源は上限数まで
		plView.each([&](auto, const PointLightComponent& pl, const TransformComponent& tc) {
			if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
				// 位置・色 (強さを掛ける)・減衰
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
				Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
				renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
				plCount++;
			}
		});

		// スポットライトも上限数まで (向きは回転行列の Z 軸)
		auto slView = registry_.view<SpotLightComponent, TransformComponent>();
		slView.each([&](auto, const SpotLightComponent& sl, const TransformComponent& tc) {
			if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
				// 回転から向きを求める
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				// 位置・色 (強さを掛ける)・減衰
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
				Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
				// 範囲と、内側・外側の角度 (cos) も渡す
				renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
				slCount++;
			}
		});

		// 使わなかった枠はライトを無効にしておく
		if (!hasDirLight) {
			renderer_->SetDirectionalLight({0, -1, 0}, {0, 0, 0}, false);
		}
		for (int i = plCount; i < Engine::Renderer::kMaxPointLights; ++i) {
			renderer_->SetPointLight(i, {0, 0, 0}, {0, 0, 0}, 0, {1, 0, 0}, false);
		}
		for (int i = slCount; i < Engine::Renderer::kMaxSpotLights; ++i) {
			renderer_->SetSpotLight(i, {0, 0, 0}, {0, -1, 0}, {0, 0, 0}, 0, 0.0f, 0.0f, {1, 0, 0}, false);
		}
	}
}

// パーティクルエミッターの更新
void GameScene::UpdateParticleEmitters(float dt) {
	// パーティクルエミッターの更新
	auto peView = registry_.view<ParticleEmitterComponent, TransformComponent, NameComponent>();
	// 初回にエミッターを初期化し (保存された設定ファイルがあれば読み込む)、位置を合わせて更新する
	peView.each([&](auto, ParticleEmitterComponent& pe, const TransformComponent& tc, const NameComponent& nc) {
		// 無効なら何もしない
		if (!pe.enabled)
			return;

		// まだ初期化していなければ、名前を付けて作る
		if (!pe.isInitialized && renderer_) {
			pe.emitter.Initialize(*renderer_, nc.name + "_Emitter");
			// 設定ファイルがあれば読み込む
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}
			pe.isInitialized = true;
		}

		// エミッターをエンティティの位置へ動かし、時間を進める
		pe.emitter.params.position = {tc.translate.x, tc.translate.y, tc.translate.z};
		pe.emitter.Update(dt);
	});
}

// ゲーム内のポーズの切り替え。BGM を小さくし、カーソルを出し、流体などを止めてメニューを出す
void GameScene::SetPaused(bool paused) {
    // 強化の選択画面・スライムの調合画面が開いているか
    isPaused_=paused;bool upgradeOpen=false;
    for(auto e:registry_.view<Chrono::InkPlayer>())upgradeOpen|=registry_.get<Chrono::InkPlayer>(e).rogue.menu;
    upgradeOpen|=chronoSystem_&&chronoSystem_->SlimeLabOpen();
    // BGM の音量を下げる
    if(auto* audio=Engine::Audio::GetInstance())audio->SetBGMDucked(paused||upgradeOpen);
    // カーソルを出す
    Engine::WindowDX::SetCursorVisible(paused||upgradeOpen);
    // 流体などの世界を止める
    if(renderer_)renderer_->SetRogueWorldFrozen(paused||upgradeOpen);
    // ポーズメニューを作る / 消す
    if(paused)CreatePauseMenu();else DestroyPauseMenu();
}
// エディタの Pause: ゲームの更新だけを止める。止める前の流体の停止状態を覚えておき、再開時に戻す
void GameScene::SetEditorPaused(bool paused) {
	// 変わらなければ何もしない
	if (editorPaused_ == paused) return;
	if (renderer_) {
		if (paused) worldFrozenBeforeEditorPause_ = renderer_->GetRogueWorldFrozen();
		else renderer_->SetRogueWorldFrozen(worldFrozenBeforeEditorPause_);
	}
	editorPaused_ = paused;
	// Step で進める残りのフレームを 0 に戻す
	editorStepFrames_ = 0;
}

// PLAY の開始と停止
void GameScene::SetIsPlaying(bool play) {
	// 変わらなければ何もしない
	if (isPlaying_ == play)
		return;
	// エディタの Pause は解く
	SetEditorPaused(false);

	// 流体のプレビューとスライム本編のポストエフェクトを初期状態にする
	wasLiquidated_ = false;
	if (play || chronoMode_) ResetFluidPreview();
	if (chronoMode_) {
		gpuSlimeEmitted_ = false;
		renderer_->SetPostEffect("Default");renderer_->SetPostProcessParams({});
	}

	if (play) {
		// PLAY 開始時: インスペクターで変えたスクリプトの設定をコンポーネントへ確実に反映する
		auto scView = registry_.view<ScriptComponent>();
		// スクリプトを持つエンティティを順に処理する
		for (auto entity : scView) {
			auto& sc = scView.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
				// 実体があれば、今のパラメータを書き出して保存し、変わったかをログに残す
				if (entry.instance) {
					std::string oldParam = entry.parameterData;
					entry.parameterData = entry.instance->SerializeParameters();
					if (entry.parameterData != oldParam) {
						char logBuf[2048];
						sprintf_s(logBuf, "[GameScene] Script synced: %s from %s to %s\n", entry.scriptPath.c_str(), oldParam.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					} else {
						char logBuf[1024];
						sprintf_s(logBuf, "[GameScene] Script already in sync: %s (%s)\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
						OutputDebugStringA(logBuf);
					}
				} else {
					// 実体が無ければ反映できないことをログに残す
					char logBuf[1024];
					sprintf_s(logBuf, "[GameScene] Script instance NULL, skipping sync: %s (current param: %s)\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
					OutputDebugStringA(logBuf);
				}
			}
		}

		// 各システムをリセットする (スクリプトの再初期化やインスタンスの破棄など) のを先に行う
		// 戦闘の流れ (ヒットストップなど) を戻す
		combatFlow_.Reset();
		for (auto& sys : systems_) {
			sys->Reset(registry_);
		}

		// 停止時に戻すため、PLAY 開始時のシーンを保存する
		sceneSnapshot_ = EditorUI::SaveToMemory(this);
		{
			char logBuf[128];
			sprintf_s(logBuf, "[GameScene] Saved snapshot for PLAY mode (size: %zu)\n", sceneSnapshot_.size());
			OutputDebugStringA(logBuf);
		}

		// PLAY 前のゲームカメラの位置と回転を保存する
		DirectX::XMFLOAT3 pos = camera_.Position();
		editorCameraPos_ = {pos.x, pos.y, pos.z};
		DirectX::XMFLOAT3 rot = camera_.Rotation();
		editorCameraRot_ = {rot.x, rot.y, rot.z};

		// PLAY 中にして、ポーズを解く
		isPlaying_ = true;
		isPaused_ = false;
		Engine::WindowDX::SetCursorVisible(false); // カーソルを隠す
	} else {
		// PLAY 停止時: PLAY を押す直前の状態 (sceneSnapshot_) に戻す
		// 選択中のエンティティの名前を覚えておく (復元後に名前で選び直す)
		std::vector<std::string> selectedNames;
		auto& reg = registry_;
		for (auto entity : selectedEntities_) {
			if (reg.valid(entity) && reg.all_of<NameComponent>(entity)) {
				selectedNames.push_back(reg.get<NameComponent>(entity).name);
			}
		}

		isPlaying_ = false;
		gpuSlimeEmitted_ = false; // GPU スライムの放出フラグを戻す
		Engine::WindowDX::SetCursorVisible(true); // カーソルを表示する

		// ゲームカメラを PLAY 前の位置に戻す
		camera_.SetPosition(editorCameraPos_.x, editorCameraPos_.y, editorCameraPos_.z);
		camera_.SetRotation(editorCameraRot_.x, editorCameraRot_.y, editorCameraRot_.z);
		
		// 保存したシーンがあれば戻す
		if (!sceneSnapshot_.empty()) {
			OutputDebugStringA(("[GameScene] Restoring from memory snapshot (size: " + std::to_string(sceneSnapshot_.size()) + ")...\n").c_str());
			EditorUI::LoadFromMemory(this, sceneSnapshot_);

			// 覚えておいた名前で選択を復元する
			selectedEntities_.clear();
			selectedEntity_ = entt::null;
			// 名前が同じエンティティを選び直す (最初のものを主な選択にする)
			auto view = reg.view<NameComponent>();
			for (const auto& name : selectedNames) {
				for (auto entity : view) {
					if (view.get<NameComponent>(entity).name == name) {
						selectedEntities_.insert(entity);
						if (selectedEntity_ == entt::null)
							selectedEntity_ = entity;
						break;
					}
				}
			}
			if (!selectedNames.empty()) {
				OutputDebugStringA(("[GameScene] Restored selection for " + std::to_string(selectedEntities_.size()) + " entities.\n").c_str());
			}
		} else {
			OutputDebugStringA("[GameScene] ERROR: Memory snapshot is empty on STOP! Falling back to initial state.\n");
			// 保存が無ければ最初の状態に戻す
			if (!initialSceneSnapshot_.empty()) {
				EditorUI::LoadFromMemory(this, initialSceneSnapshot_);
			}
		}
		// 保存内容は PLAY 開始のたびに取り直すので、停止したら空にする
		sceneSnapshot_ = "";

// 残っている生成・破棄の予約を捨てる
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
	}
}

} // namespace Game
