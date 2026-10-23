// GameScene.cpp
// ゲーム本編のシーンの実装。
// 初期化 (シーンファイルの読み込みとシステムの登録)、毎フレームの更新と描画、
// エンティティの生成・破棄の予約処理、タグ検索のキャッシュ、PLAY の開始・停止を扱う。
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

// このファイルの中だけで使う関数
namespace {
	// スライムのぷよぷよした揺れを CPU で計算するための簡易 3D ノイズ (Simplex ノイズの代わり)
	float Noise3D(float x, float y, float z) {
		// sin・cos を組み合わせた滑らかな値
		float n = std::sin(x) * std::cos(y) * std::sin(z);
		n += std::sin(x * 2.1f + y * 1.3f) * 0.5f;
		n += std::cos(y * 1.7f + z * 2.5f) * 0.5f;
		return n * 0.5f;
	}
}

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

	// PLAY 中の入力: F7 でグラフィック設定、ポーズメニュー、Esc / START でポーズ
	if (isPlaying_) {
        // F7: グラフィック設定画面を開閉する (開いている間はポーズ)
        if(!stageClear&&UI::Pressed(DIK_F7)){SetPaused(true);graphicsOpen_=!graphicsOpen_;return;}
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
            return;
        }
		// ポーズメニューのボタン入力 (グラフィック設定 / タイトルへ / 再開)
        if (!stageClear && isPaused_) {
            // UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
            UI::Canvas ui(renderer_,ctx_.viewportSize.x,ctx_.viewportSize.y);if(ctx_.useOverrideMouse)ui.SetPointer(ctx_.overrideMouseX,ctx_.overrideMouseY);
            // グラフィック設定ボタン・G・パッドの BACK で設定画面を開く
            if(ui.Click(UI::GraphicsButton)||UI::Pressed(DIK_G)||UI::PadPressed(XINPUT_GAMEPAD_BACK)){graphicsOpen_=true;return;}
            auto* input=Engine::Input::GetInstance();
            // スライム本編の強化パレット画面かどうか (ボタンの位置が違う)
            bool roguePause=chronoMode_&&!registry_.view<Chrono::InkPlayer>().empty();
            // タイトルへ戻るボタンか Tab でタイトルへ
            if (ui.Click(roguePause?UI::PaletteTitle:UI::Title) || input->Trigger(DIK_TAB)) {
                Engine::SceneManager::GetInstance()->RequestChange("Title");return;
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

	// エディタがある構成では Scene ビューのカメラを EditorUI が操作する (ビューポート上で右ドラッグ中のみ)。
	// 以前はここで停止中に camera_ を生のキー入力で動かしており、Inspector の入力中も WASD でカメラが動いていた
#ifndef USE_IMGUI
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
#endif
	// カメラの揺れなどを進める
	camera_.Tick(dt);







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
#ifndef NDEBUG
	// システムの後の処理時間を記録する
	updateTimings_.push_back({"After systems", std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - profileAfterSystems).count()});
#endif
}

// 選択を解除する
void GameScene::ClearSelection() {
    // 主な選択と、複数選択の一覧を空にする
    selectedEntity_ = entt::null;
    selectedEntities_.clear();
}

// 1つだけ選択する (無効なエンティティなら解除のみ)
void GameScene::SelectEntity(entt::entity entity) {
    ClearSelection();
    // 存在すれば選ぶ
    if (registry_.valid(entity)) {
        selectedEntity_ = entity;
        selectedEntities_.insert(entity);
    }
}

// 名前もトランスフォームも付けない空のエンティティを作る
entt::entity GameScene::CreateEmptyEntity() {
    // 生成・破棄の予約と同時に触らないようロックする
    std::lock_guard<std::mutex> lock(spawnMutex_);
    return registry_.create();
}

// エディタ用の一時エンティティ (プレビューなど) を即座に削除する
void GameScene::DestroyEditorEntity(entt::entity entity) {
    RemoveEntity(entity);
}

// 即座に削除する。選択中なら選択から外し、行列のキャッシュも捨てる
void GameScene::RemoveEntity(entt::entity entity) {
    // 存在しなければ何もしない
    if (!registry_.valid(entity)) return;
    // 選択から外し、主な選択が消えたら残りの先頭にする
    selectedEntities_.erase(entity);
    if (selectedEntity_ == entity) selectedEntity_ = selectedEntities_.empty() ? entt::null : *selectedEntities_.begin();
    // ECS から消し、行列のキャッシュを捨てる
    registry_.destroy(entity);
    ClearMatrixCache();
}

// エディタの Game ビューの位置・大きさと、内部解像度に換算したマウス座標を保存する
void GameScene::SetEditorViewport(DirectX::XMFLOAT2 offset, DirectX::XMFLOAT2 size, float mouseX, float mouseY) {
    ctx_.viewportOffset = offset;
    ctx_.viewportSize = size;
    ctx_.overrideMouseX = mouseX;
    ctx_.overrideMouseY = mouseY;
    ctx_.useOverrideMouse = true;
}

// エンティティを生成する (名前とトランスフォームを付ける)
entt::entity GameScene::CreateEntity(const std::string& name) {
	// 生成・破棄の予約と同時に触らないようロックする
	std::lock_guard<std::mutex> lock(spawnMutex_);
	auto entity = registry_.create();
	// 名前とトランスフォームを付ける
	registry_.emplace<NameComponent>(entity, name);
	registry_.emplace<TransformComponent>(entity);
	return entity;
}

// ID を指定して破棄を予約する
void GameScene::DestroyObject(uint32_t id) {
	// 生成・破棄の予約と同時に触らないようロックする
	std::lock_guard<std::mutex> lock(spawnMutex_);
	// ID をそのまま entt::entity として扱う
	pendingDestroys_.push_back(static_cast<entt::entity>(id));
}


// 名前でエンティティを探す
entt::entity GameScene::FindObjectByName(const std::string& name) {
	// 名前を持つエンティティを順に調べる
	auto view = registry_.view<NameComponent>();
	for (auto entity : view) {
		if (view.get<NameComponent>(entity).name == name) {
			return entity;
		}
	}
	// 見つからなければ無し
	return entt::null;
}

// 指定座標の真下にあるメッシュ表面の高さを求める
float GameScene::GetHeightAt(float x, float z, float startY, uint32_t excludeId) {
	// 当たった中で一番高い面の高さ。何にも当たらなければ -10000
	float maxHeight = -1000.0f;
	bool hitAny = false;

	// startY (既定 1000) から真下へレイを飛ばす
	DirectX::XMVECTOR rayPos = DirectX::XMVectorSet(x, startY, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	// 位置を持つエンティティを順に調べる
	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		// 除外するもの (自分自身など) は飛ばす
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// 敵・弾・プレイヤー・エフェクトは地面として扱わない
		bool isEnemyOrBullet = false;
		if (registry_.all_of<TagComponent>(entity)) {
			// タグを調べる
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX) {
				isEnemyOrBullet = true;
			}
		}
		if (isEnemyOrBullet)
			continue;

		// 当たり判定用のメッシュがあればそれを使い、無ければ描画用のメッシュを使う
		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}

		// メッシュが無ければ飛ばす
		if (modelHandle == 0)
			continue;

		// モデルの実体 (無ければ飛ばす)
		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		// 当たった距離・位置と、このエンティティのワールド行列
		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = this->GetWorldMatrix(static_cast<int>(entity));

		// 当たったら、一番高い位置を覚える
		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (hitPoint.y > maxHeight) {
				maxHeight = hitPoint.y;
				hitAny = true;
			}
		}
	}

	// 当たったものがあればその高さ、無ければ -10000
	return hitAny ? maxHeight : -10000.0f;
}

// レイキャスト: 地形などのメッシュに当たった最も近い距離を返す (敵・弾・プレイヤーなどは無視)
bool GameScene::RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist) {
	// 当たったか・一番近い距離 (最初は最大距離)
	bool hitAny = false;
	float minDist = maxDist;

	// 始点と向きを DirectXMath の形にする
	DirectX::XMVECTOR rayPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&origin));
	DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&direction));

	// 位置を持つエンティティを順に調べる
	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		// 除外するもの (自分自身など) は飛ばす
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// タグで対象外のものを除く
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX)
				continue;
		}

		// 当たり判定用のメッシュがあればそれを使い、無ければ描画用のメッシュを使う
		uint32_t modelHandle = 0;
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			auto& mc = registry_.get<GpuMeshColliderComponent>(entity);
			if (mc.enabled)
				modelHandle = mc.meshHandle;
		}
		if (modelHandle == 0 && registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled)
				modelHandle = mr.modelHandle;
		}
		if (modelHandle == 0)
			continue;

		// モデルの実体 (無ければ飛ばす)
		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		// 当たった距離・位置と、このエンティティのワールド行列
		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = GetWorldMatrix(static_cast<int>(entity));

		// 当たったら、一番近い距離を覚える
		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (dist < minDist) {
				minDist = dist;
				hitAny = true;
			}
		}
	}

	// 当たったものがあれば距離を返す
	if (hitAny) {
		outDist = minDist;
		return true;
	}
	return false;
}

// エンティティのワールド行列 (親子関係を含む)
Engine::Matrix4x4 GameScene::GetWorldMatrix(int entityId) const { return GetWorldMatrixRecursive(static_cast<entt::entity>(entityId), 0); }

// ワールド行列を親へたどって計算する (深さ 32 を超える循環は単位行列で打ち切り、結果はフレーム内でキャッシュする)
Engine::Matrix4x4 GameScene::GetWorldMatrixRecursive(entt::entity e, int depth) const {
	// 深すぎるときは循環しているとみなして打ち切る
	if (depth > 32)
		return Engine::Matrix4x4::Identity();

	// このフレームで計算済みならそれを返す
	auto it = matrixCache_.find(e);
	if (it != matrixCache_.end())
		return it->second;

	// 存在しないか位置が無ければ単位行列
	if (!registry_.valid(e) || !registry_.all_of<TransformComponent>(e))
		return Engine::Matrix4x4::Identity();
	// 自分のローカル行列
	const auto& tc = registry_.get<TransformComponent>(e);
	Engine::Matrix4x4 local = tc.ToMatrix();

	Engine::Matrix4x4 world = local;
	// 親があれば親のワールド行列を掛ける
	if (registry_.all_of<HierarchyComponent>(e)) {
		const auto& hc = registry_.get<HierarchyComponent>(e);
		if (hc.parentId != entt::null && registry_.valid(hc.parentId)) {
			world = Engine::Matrix4x4::Multiply(local, GetWorldMatrixRecursive(hc.parentId, depth + 1));
		}
	}
	// キャッシュに入れて返す
	matrixCache_[e] = world;
	return world;
}

namespace {
// Scene ビュー表示中は、描画の間だけ camera_ にエディタカメラを入れる。
// 描画処理 (ChronoSystem など) は ctx.camera = &camera_ を参照するため、全体がエディタ視点で描かれる。
// 描画中にカメラへ書き込まれても、終了時にゲームカメラを書き戻すので更新処理には影響しない
class ViewCameraScope {
public:
	ViewCameraScope(Engine::Camera& gameCamera, const Engine::Camera& editorCamera, bool active)
		: camera_(gameCamera), active_(active) {
		// 有効なときだけ、ゲームのカメラを覚えてエディタのカメラに差し替える
		if (!active_) return;
		saved_ = gameCamera;
		gameCamera = editorCamera;
	}
	// 終わったらゲームのカメラに戻す
	~ViewCameraScope() { if (active_) camera_ = saved_; }
	// コピーはできない
	ViewCameraScope(const ViewCameraScope&) = delete;
	ViewCameraScope& operator=(const ViewCameraScope&) = delete;
private:
	// 差し替えたカメラ・元のカメラ・有効か
	Engine::Camera& camera_;
	Engine::Camera saved_;
	bool active_;
};
} // namespace

// 描画: 流体の設定・メッシュ・パーティクル・ギズモ・ゲーム内 UI
void GameScene::Draw() {
	// 描画の窓口が無ければ何もしない
	if (!renderer_)
		return;
	// Scene ビューなら描画の間だけエディタのカメラにする
	ViewCameraScope viewCamera(camera_, editorCamera_, editorView_ == EditorView::Scene);
    // スライムの調合室を開いているときは、調合室の画面だけを描く
    if(isPlaying_&&!isPaused_&&chronoSystem_&&chronoSystem_->SlimeLabOpen()){
        chronoSystem_->DrawSlimeLab(registry_,ctx_);return;
    }

	// プレイヤーの位置・大きさ・液状化の状態 (流体の描画に使う)
	DirectX::XMFLOAT3 corePos = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 blobRadii = { 1.2f, 0.85f, 1.2f };
	bool isLiquidated = false;
	float liquefyInitialFlowSpeed = 0.0f;
	Engine::Vector3 liquefyFlowForward = {0.0f, 0.0f, 1.0f};
	bool hasPlayerSlime = false;
	bool isPlayerDead = false;

	// プレイヤーを探し、位置・液状化・死亡の状態を読む
	const auto& playersForCore = GetEntitiesByTag(TagType::Player);
	// プレイヤーのエンティティ
	entt::entity playerEntity = entt::null;
	if (!playersForCore.empty() && registry_.valid(playersForCore[0])) {
		playerEntity = playersForCore[0];
		// 位置と大きさ
		if (registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			corePos = { tc.translate.x, tc.translate.y, tc.translate.z };
			blobRadii = { tc.scale.x, tc.scale.y, tc.scale.z };
		}
		// 液状化中か・流れの速さと向き
		if (registry_.all_of<PlayerActionComponent>(playerEntity)) {
			const auto& pa = registry_.get<PlayerActionComponent>(playerEntity);
			isLiquidated = pa.state == PlayerActionState::Liquefy;
			liquefyInitialFlowSpeed = pa.CurrentLiquefyFlowSpeed();
			liquefyFlowForward = {pa.liquefyFlowDir.x, pa.liquefyFlowDir.y, pa.liquefyFlowDir.z};
		}
		// 死んでいるか
		if (registry_.all_of<HealthComponent>(playerEntity)) {
			const auto& hc = registry_.get<HealthComponent>(playerEntity);
			isPlayerDead = hc.IsDead() || hc.Hp() <= 0.0f;
		}
	}

	// 流体エミッターは当たり判定用の箱が必要 (プレイヤーがいないプレビューも含む)。壁と既定タグの箱を AABB にして渡す
	std::vector<Engine::Renderer::FluidAABB> fluidAABBs;
	// 箱の当たり判定を持つエンティティを調べる
	auto aabbView = registry_.view<TransformComponent, BoxColliderComponent>();
	aabbView.each([&](entt::entity entity, TransformComponent&, BoxColliderComponent& bc) {
		// 無効なものとすり抜けるものは除く
		if (!bc.enabled || bc.isTrigger) return;
		// 壁と既定のタグだけ
		const auto* tag = registry_.try_get<TagComponent>(entity);
		if (!tag || (tag->tag != TagType::Wall && tag->tag != TagType::Default)) return;
		// ワールド行列
		const auto matrix = GetWorldMatrix(static_cast<int>(entity));
		const auto world = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&matrix));
		// 最小・最大の点
		DirectX::XMVECTOR lo = DirectX::XMVectorReplicate(FLT_MAX);
		DirectX::XMVECTOR hi = DirectX::XMVectorReplicate(-FLT_MAX);
		// 箱の 8 つの角をワールド座標にして、最小・最大を広げる
		for (int corner = 0; corner < 8; ++corner) {
			const auto local = DirectX::XMVectorSet(
				bc.center.x + ((corner & 1) ? .5f : -.5f) * bc.size.x,
				bc.center.y + ((corner & 2) ? .5f : -.5f) * bc.size.y,
				bc.center.z + ((corner & 4) ? .5f : -.5f) * bc.size.z, 1);
			const auto point = DirectX::XMVector3TransformCoord(local, world);
			lo = DirectX::XMVectorMin(lo, point); hi = DirectX::XMVectorMax(hi, point);
		}
		// AABB にして一覧へ追加する
		Engine::Renderer::FluidAABB box{};
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&box.min), lo);
		DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&box.max), hi);
		fluidAABBs.push_back(box);
	});
	// レンダラーへ渡す
	renderer_->SetFluidAABBs(fluidAABBs);

	// プレイヤーがいないとき (水のプレビューなど) は、最初の流体エミッター付近を高画質で描く
	if (!registry_.valid(playerEntity)) {
		// プレイヤーがいなくてもプレビューの周りは高画質で再構成する
		// 流体エミッターを探す
		auto sources = registry_.view<TransformComponent, FluidEmitterComponent>();
		if (sources.begin() != sources.end()) {
			// 最初のエミッターの少し下を中心にする
			const auto source = *sources.begin();
			const auto matrix = GetWorldMatrix(static_cast<int>(source));
			corePos = {matrix.m[3][0], matrix.m[3][1] - 3, matrix.m[3][2]};
		}
		// 中心だけ渡し、引き寄せ・分身・糸は無効にする
		renderer_->SetGPUFluidCore({corePos.x, corePos.y, corePos.z}, 0);
		renderer_->SetGPUFluidDecoy({0,-1000,0}, 0, {1,1,1}, {0,0,1});
		renderer_->SetGPUFluidTether({}, false);
		renderer_->SetChronoFluidPresentation(1, 0, false);
	}

	// エディタで自由に置ける流体エミッターの処理 (プレイヤーの有無や表示に関係なく動かす)
	{
		// 流体エミッターごとに、毎フレームの粒子数 (端数と上限を考慮) を GPU 流体へ追加する
		auto emitterView = registry_.view<TransformComponent, FluidEmitterComponent>();
		emitterView.each([&](entt::entity e, TransformComponent&, FluidEmitterComponent& fec) {
			// 無効か、出す数が 0 なら何もしない
			if (!fec.enabled || fec.emitCountPerFrame <= 0) return;

			// ワールド座標での位置・初速・色
			const auto world = GetWorldMatrix(static_cast<int>(e));
			Engine::Vector3 pos = {world.m[3][0], world.m[3][1], world.m[3][2]};
			Engine::Vector3 vel = { fec.velocity.x, fec.velocity.y, fec.velocity.z };
			Engine::Vector4 col = { fec.color.x, fec.color.y, fec.color.z, fec.color.w };

			// 毎フレーム粒子を出す
			const int emitCount=Engine::LimitFluidEmission(Engine::AccumulateFluidEmission(fec.emitCountPerFrame,
				Engine::TimeManager::GetInstance().GetDeltaTime(),fec.emissionRemainder), fec.particleLimit, fec.emittedParticles);
			// GPU 流体へ追加する
			renderer_->EmitGPUFluid(pos, vel, col, emitCount, fec.fluidType, fec.retainWater);

			// 水 (fluidType >= 0.5) の真下 (半径 1.5 以内) にプレイヤーがいれば HP を回復する
			if (isPlaying_ && !isPaused_ && fec.fluidType >= 0.5f && registry_.valid(playerEntity)) {
				// エミッターとプレイヤーの水平の差
				float dx = corePos.x - pos.x;
				float dz = corePos.z - pos.z;
				if (dx * dx + dz * dz < 1.5f * 1.5f) {
					if (registry_.all_of<HealthComponent>(playerEntity)) {
						auto& hc = registry_.get<HealthComponent>(playerEntity);
						// 回復の間隔を数える (関数をまたいで覚えておく)
						static float healTimer = 0.0f;
						healTimer += ctx_.dt;
						if (healTimer > 0.05f) { // 0.05 秒ごとに 1 回復 (1 秒で 20)
							healTimer = 0.0f;
							hc.RecoverHp(1);
						}
					}
				}
			}
		});
	}

	// プレイヤーのスライム: 見た目は GPU 流体で描き、元のメッシュは非表示にする
	if (registry_.valid(playerEntity) && registry_.all_of<MeshRendererComponent>(playerEntity)) {
		// プレイヤーの見た目
		auto& mr = registry_.get<MeshRendererComponent>(playerEntity);
			
			// CPU 側のスライムメッシュの準備
			// 初回だけ、元の球メッシュから変形用の動的メッシュとパーティクルを作る
			if (!slimeCpuLogic_.initialized) {
				// 元のモデル
				auto* model = renderer_->GetModel(mr.modelHandle);
				if (model) {
					// 元の頂点と添え字を覚える
					slimeCpuLogic_.baseVertices = model->GetData().vertices;
					// 法線の初期状態を保存する
					slimeCpuLogic_.baseNormals = slimeCpuLogic_.baseVertices; 
					slimeCpuLogic_.indices = model->GetData().indices;
					// 変形後の頂点 (最初は元と同じ)
					slimeCpuLogic_.dynamicVerts = slimeCpuLogic_.baseVertices;
					
					// 変形用の動的メッシュを作る
					slimeCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(slimeCpuLogic_.baseVertices, slimeCpuLogic_.indices);
					
					// 体の中に漂うパーティクル (50個) の初期化
					slimeCpuLogic_.particles.resize(50);
					for (auto& p : slimeCpuLogic_.particles) {
						// ランダムな向きに、ランダムな距離 (最大 1.5) だけ離した位置
						Engine::Vector3 randPos = { (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f, (rand()%100/50.0f)-1.0f };
						p.basePos = Engine::Normalize(randPos);
						p.basePos.x *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.y *= ((rand()%100)/100.0f) * 1.5f;
						p.basePos.z *= ((rand()%100)/100.0f) * 1.5f;
						p.color = {0.4f, 0.8f, 0.1f, 1.0f}; // 濃い黄緑
					}
					slimeCpuLogic_.initialized = true;
				}
			}

			if (slimeCpuLogic_.initialized && registry_.all_of<TransformComponent>(playerEntity)) {
				// CPU で変形させたメッシュは描かず、GPU 流体のスライムで描く
				mr.enabled = false;
				
				if (!gpuSlimeEmitted_) {
					// 最初の1回だけ、大量の GPU 流体粒子を体の中心に放出する
					// スライムの色 (変形の見本があればその色)
					Engine::Vector4 pColor = {Chrono::SlimeRed, Chrono::SlimeGreen, Chrono::SlimeBlue, 1.0f};
                    if(!renderer_->fluidMorphTargets_.empty())pColor=renderer_->fluidMorphTargets_.front().color;
					// プレイヤーの粒子数だけ出す
					renderer_->EmitGPUFluid({corePos.x, corePos.y, corePos.z}, {0, -2, 0}, pColor, Engine::Renderer::kPlayerFluidParticles);
					gpuSlimeEmitted_ = true;
				}
				
				// 状態で引き寄せる力を変える (死亡中は 0、液状化中は弱く、通常は強く)
				float attraction = isPlayerDead ? 0.0f : (isLiquidated ? 1.0f : 32.0f);
				
				// 流体の中心 (圧力の源) を体の少し上に置く
				Engine::Vector3 targetCore = {corePos.x, corePos.y + 0.18f, corePos.z};
				// 向き (回転行列の Z 軸。液状化中は流れの向き)
				auto& playerTc = registry_.get<TransformComponent>(playerEntity);
				Engine::Matrix4x4 mat = playerTc.GetTransform().ToMatrix();
				Engine::Vector3 forward = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				if (isLiquidated) {
					forward = liquefyFlowForward;
				}
				// 大きさと、液状化の流れの速さ
				Engine::Vector3 scaleVec = {blobRadii.x, blobRadii.y, blobRadii.z};
				float liquidFlowSpeed = 0.0f;
				if (isLiquidated) {
					liquidFlowSpeed = liquefyInitialFlowSpeed;
				}
				// スライム本編: 地面の傾き・顔の向き・体の形 (伸び・空中・動き)・質量を流体へ渡す
				if (chronoMode_) {
					// 地面の傾き・高さを渡す
					if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity)){renderer_->SetSlimeGround({ink->groundSlope.x,ink->groundHeight,ink->groundSlope.z},registry_.get<Chrono::Player>(playerEntity).grounded);renderer_->SetSlimeFaceCamera(ink->rogue.MenuEase());scaleVec={ink->fluidAspect,ink->airBlend,ink->fluidMotion};forward={ink->fluidDirection.x,0,ink->fluidDirection.z};}
					// 質量 (体力)。変形の見本があれば最大にする
					float mass = registry_.get<HealthComponent>(playerEntity).Hp();
                    if(!renderer_->fluidMorphTargets_.empty())mass=Chrono::SlimeMaximumMass;
                    if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity);ink&&ink->perfectAge>=0&&ink->perfectAge<.16f)
                        mass*=.18f; // 一瞬だけ見た目を縮める (ゲーム上の質量は変えない)
					// 流体の大きさの基準 (100) に換算する
					if(registry_.all_of<Chrono::InkPlayer>(playerEntity))mass*=100.f/Chrono::SlimeMaximumMass;
					// 中心・引き寄せ・大きさ・向き・状態を渡す
					renderer_->SetGPUFluidCore(targetCore, attraction, scaleVec, forward, (registry_.all_of<Chrono::InkPlayer>(playerEntity) && registry_.get<Chrono::InkPlayer>(playerEntity).dodgeLiquid) ? 5.0f : (registry_.all_of<Chrono::InkPlayer>(playerEntity) ? 4.0f : 2.0f), mass);
					// 被弾時の点滅・不安定時の明滅、ヒットストップ中の停止、触手を伸ばしている間の糸
					if (auto* action = registry_.try_get<Chrono::Player>(playerEntity)) {
						// 強化の選択画面を開いているか
						bool upgradeOpenForFluid=false;if(auto* ink=registry_.try_get<Chrono::InkPlayer>(playerEntity))upgradeOpenForFluid=ink->rogue.menu;
						// 被弾から 0.6 秒は激しく点滅する
						float flash=action->damageAge<.6f?(std::sin(action->damageAge*70)>0?.75f:0):0;
						// 不安定さが 75 を超えたらゆっくり明滅する
						if(action->instability>75)flash=std::max(flash,.35f*(.5f+.5f*std::sin(action->stats.seconds*10)));
						// カメラ用の透明度・点滅・止めるか (ポーズ中かヒットストップ中) を渡す
						renderer_->SetChronoFluidPresentation(action->cameraOpacity,flash,isPaused_||(action->hitStop>0&&!upgradeOpenForFluid));
						// 触手を伸ばしている・引き寄せている間は手まで糸を張る
						bool tether = action->action == Chrono::Action::Extending || action->action == Chrono::Action::Pulling || action->action == Chrono::Action::Retracting;
						renderer_->SetGPUFluidTether({action->hand.x, action->hand.y, action->hand.z}, tether);
					}
				// 旧アクションゲーム: 液状化の流れの速さを渡す
				} else {
					// 糸と点滅は無し
					renderer_->SetGPUFluidTether({}, false);
					renderer_->SetChronoFluidPresentation(1,0,false);
					// 中心・引き寄せ・大きさ・向き・液状化の状態と流れの速さを渡す
					renderer_->SetGPUFluidCore(targetCore, attraction, scaleVec, forward, isLiquidated ? 1.0f : 0.0f, liquidFlowSpeed);
				}

				// 分身 (デコイ) の流体の設定
				bool decoyFound = false;
				// タグ・位置・名前を持つエンティティから分身を探す
				auto decoyView = registry_.view<TagComponent, TransformComponent, NameComponent>();
				decoyView.each([&](entt::entity e, TagComponent& tagComp, TransformComponent& dtc, NameComponent& nc) {
					// 見つかったら残りは飛ばす
					if (decoyFound) return;
					// プレイヤーのタグで、名前が Decoy のもの
					if (tagComp.tag == TagType::Player && nc.name == "Decoy") {
						
						// 分身の呼吸のような伸び縮み
						float timeLived = 5.0f;
						if (registry_.all_of<AutoDestroyComponent>(e)) {
							timeLived = 5.0f - registry_.get<AutoDestroyComponent>(e).timer;
						}
						float breathe = std::sin(timeLived * 3.0f);
						
						// 幅は自機より少し小さく、厚みは自機と同じにしてぺたんこにならないようにする
						dtc.scale.x = 0.9f + breathe * 0.05f;
						dtc.scale.y = 0.8f - breathe * 0.05f; // 自機 (0.8) と同じ高さ
						dtc.scale.z = 0.9f + breathe * 0.05f;

						// 中心を持ち上げて地面に潰されないようにする
						Engine::Vector3 targetDecoyCore = {dtc.translate.x, dtc.translate.y + 0.8f, dtc.translate.z};
						// 向きと大きさ
						Engine::Matrix4x4 dMat = dtc.GetTransform().ToMatrix();
						Engine::Vector3 dForward = {dMat.m[2][0], dMat.m[2][1], dMat.m[2][2]};
						Engine::Vector3 dScale = {dtc.scale.x, dtc.scale.y, dtc.scale.z};
						// 分身はプレイヤーの状態 (液状化など) に関係なく常に形を保つ
						float decoyAttraction = 80.0f;
						// 分身の流体の設定を渡す
						renderer_->SetGPUFluidDecoy(targetDecoyCore, decoyAttraction, dScale, dForward);
						decoyFound = true;
					}
				});
				if (!decoyFound) {
					// 分身がいなければ引き寄せる力を 0 にしておく
					renderer_->SetGPUFluidDecoy({0,-1000,0}, 0.0f, {1,1,1}, {0,0,1});
				}


			}
			// ----------------------------------------
			
			// 星形の弾のメッシュの準備
			if (!projectileCpuLogic_.initialized && slimeCpuLogic_.initialized && !slimeCpuLogic_.baseVertices.empty()) {
				// プレイヤーの元の球の頂点を複製して、弾の元のメッシュにする
				projectileCpuLogic_.baseVertices = slimeCpuLogic_.baseVertices;
				projectileCpuLogic_.baseNormals = slimeCpuLogic_.baseNormals; 
				projectileCpuLogic_.indices = slimeCpuLogic_.indices;
				projectileCpuLogic_.dynamicVerts = projectileCpuLogic_.baseVertices;
				projectileCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(projectileCpuLogic_.baseVertices, projectileCpuLogic_.indices);
				projectileCpuLogic_.initialized = true;
			}

			// 弾のメッシュをノイズで毎フレーム変形させ、トゲのある星形にする
			if (projectileCpuLogic_.initialized) {
				// 時間を進める
				projectileCpuLogic_.time += ctx_.dt;
				const float pNoiseScale = 4.0f;  // 細かくうねらせる
				const float pNoiseSpeed = 8.0f;  // 速くうねらせる
				
				// 頂点ごとに変形する
				for (size_t i = 0; i < projectileCpuLogic_.baseVertices.size(); ++i) {
					// 元の頂点と法線、書き込む頂点
					const auto& baseV = projectileCpuLogic_.baseVertices[i];
					const auto& baseN = projectileCpuLogic_.baseNormals[i];
					auto& dynV = projectileCpuLogic_.dynamicVerts[i];

					// 元の位置
					float bx = baseV.position.x;
					float by = baseV.position.y;
					float bz = baseV.position.z;

					// 球を少し大きくしてから変形する
					float scaleUp = 1.8f;
					float sx = bx * scaleUp;
					float sy = by * scaleUp;
					float sz = bz * scaleUp;

					// 時間で流れる 3D ノイズ
					float noise = Noise3D(
						sx * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sy * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sz * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed
					);
					
					// ノイズの絶対値を強めて、星形のトゲを作る
					float spike = std::abs(noise) * 1.5f;

					// 法線の向きへトゲの分だけ押し出す
					Engine::Vector3 v = {
						sx + baseN.normal.x * spike,
						sy + baseN.normal.y * spike,
						sz + baseN.normal.z * spike
					};

					// 変形後の位置と法線
					dynV.position = { v.x, v.y, v.z, 1.0f };
					dynV.normal = baseN.normal; 
				}
				// GPU のメッシュを更新する
				renderer_->UpdateDynamicMesh(projectileCpuLogic_.dynamicMeshHandle, projectileCpuLogic_.dynamicVerts);
			}
			// ----------------------------------------

			// プレイヤーがスライムのシェーダーで描かれているか
			hasPlayerSlime = mr.shaderName == "Slime";
		}

	// 液状化の開始・終了時の流体リセット (現在は使っていない)
	if (hasPlayerSlime) {
		if (isLiquidated && !wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Puddle, blobRadii);
		} else if (!isLiquidated && wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Blob, blobRadii);
		}
	}
	// 今の液状化の状態を覚える
	wasLiquidated_ = isLiquidated;

	// 弾の当たり判定を持つエンティティに星形メッシュとスライム用シェーダーを使う
	auto projView = registry_.view<MeshRendererComponent>();
	for (auto entity : projView) {
		// 攻撃判定を持つもののうち
		if (registry_.all_of<HitboxComponent>(entity)) {
			auto& hb = registry_.get<HitboxComponent>(entity);
			// 弾で、星形メッシュが準備できているもの
			if (hb.isProjectile && projectileCpuLogic_.initialized) {
				auto& pmr = registry_.get<MeshRendererComponent>(entity);
				pmr.modelHandle = projectileCpuLogic_.dynamicMeshHandle;
				// プレイヤーの弾は深度ありで描き、それ以外は深度なしで描く (手前に見せる)
				if (registry_.all_of<NameComponent>(entity) && registry_.get<NameComponent>(entity).name == "PlayerProjectile") {
					pmr.shaderName = "SlimeNoFace";
				} else {
					pmr.shaderName = "SlimeNoFaceNoDepth";
				}
			}
		}
	}

	// 描画に使うカメラを渡す
	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	// ギズモは Unity と同様に Scene ビューだけに描く (Game ビューは実際のゲーム画面)
	if (editorView_ == EditorView::Scene) {
		DrawEditorGizmos();
	}
#endif

	// プレイヤーの位置をレンダラーへ渡す (タグのキャッシュで素早く探す)
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		playerEntity = players[0];
		if (registry_.valid(playerEntity) && registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			renderer_->SetPlayerPos(Engine::Vector3{tc.translate.x, tc.translate.y, tc.translate.z});
		}
	}

	// メッシュの描画: 色の上書き・カメラを遮る物の透明化・アニメーション・シェーダーごとの描画方法
	auto renderView = registry_.view<TransformComponent>();
	for (auto entity : renderView) {
		// 色 (ColorComponent があればその色)
		Engine::Vector4 color = {1, 1, 1, 1};
		if (registry_.all_of<ColorComponent>(entity)) {
			const auto& cc = registry_.get<ColorComponent>(entity);
			color = {cc.color.x, cc.color.y, cc.color.z, cc.color.w};
		}

		// メッシュで描いたか
		bool hasMeshRenderer = false;
		if(chronoMode_&&isPlaying_)if(auto* occluder=registry_.try_get<Chrono::CameraOccluder>(entity))
			if(occluder->opacity<.995f)color.w=-occluder->opacity; // カメラを遮る物は網目状に透かす (影と当たり判定はそのまま)
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			// 見た目を持ち、有効でモデルがあるものを描く
			const auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled && mr.modelHandle != 0) {
				hasMeshRenderer = true;
				// アニメーションしているかと、骨の行列
				bool hasAnim = false;
				std::vector<Engine::Matrix4x4> bonePalette;

				// アニメーターがあれば骨の行列 (直前のクリップとのクロスフェード込み) を計算する
				if (registry_.all_of<AnimatorComponent>(entity)) {
					const auto& anim = registry_.get<AnimatorComponent>(entity);
					if (anim.enabled && !anim.currentAnimation.empty()) {
						auto* m = renderer_->GetModel(mr.modelHandle);
						if (m) {
							const auto& data = m->GetData();
							// 再生中のクリップを探す
							const Engine::Animation* currAnim = nullptr;
							for (const auto& a : data.animations) {
								if (a.name == anim.currentAnimation) {
									currAnim = &a;
									break;
								}
							}
							if (currAnim) {
								// 骨の数だけ単位行列で用意する
								bonePalette.resize(data.bones.size());
								for (auto& b : bonePalette)
									b = Engine::Matrix4x4::Identity();

								// クロスフェード中なら直前のクリップと、混ぜる割合 (0〜1) を求める
								const Engine::Animation* prevAnim = nullptr;
								float blendFactor = 0.0f;
								if (anim.crossfadeTimer > 0.0f && anim.crossfadeDuration > 0.0f) {
									for (const auto& a : data.animations) {
										if (a.name == anim.prevAnimation) {
											prevAnim = &a;
											break;
										}
									}
									blendFactor = 1.0f - (anim.crossfadeTimer / anim.crossfadeDuration);
								}

								// 骨の行列を計算する
								std::vector<Engine::Model::DebugBone> debugBones;
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, prevAnim, anim.prevTime, blendFactor, bonePalette, anim.drawSkeleton ? &debugBones : nullptr);
								hasAnim = true;

								// 骨の表示が有効なら、親の骨から各骨へ線を引く
								if (anim.drawSkeleton) {
									// このエンティティのワールド行列
									Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
									DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
									for (const auto& bone : debugBones) {
										// 親の骨の位置と、この骨の位置
										DirectX::XMVECTOR p1 = DirectX::XMVectorSet(bone.parentPos.x, bone.parentPos.y, bone.parentPos.z, 1.0f);
										Engine::Vector3 bPos = {bone.globalMatrix.m[3][0], bone.globalMatrix.m[3][1], bone.globalMatrix.m[3][2]};
										DirectX::XMVECTOR p2 = DirectX::XMVectorSet(bPos.x, bPos.y, bPos.z, 1.0f);
										// ワールド座標へ変換する
										p1 = DirectX::XMVector3TransformCoord(p1, w);
										p2 = DirectX::XMVector3TransformCoord(p2, w);
										// Engine の型へ取り出す
										Engine::Vector3 wp1, wp2;
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp2), p2);
										renderer_->DrawLine3D(wp1, wp2, {0.0f, 1.0f, 0.0f, 1.0f}, true); // 手前の物に隠れずに描く
									}
								}
							}
						}
					}
				}

				// このエンティティのワールド行列
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				// アニメーションありはスキニング描画、半透明は後回し、特殊シェーダーは個別描画、それ以外はインスタンシング描画
				if (hasAnim) {
					// 骨の行列を使って描く
					renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
				} else {
					// スライム系・ビームなどの半透明のシェーダー
					if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth" || mr.shaderName == "EnergyBeam" || mr.shaderName == "EnergyCylinder") {
						// 半透明なので、不透明な物の後でまとめて描く
						continue;
					}
					// 特殊なシェーダーは 1 つずつ描く
					if (mr.shaderName == "Toon" || mr.shaderName == "ToonSkinning" || mr.shaderName == "Hologram" || mr.shaderName == "EmissiveGlow" || mr.shaderName == "ForceField" ||
					    mr.shaderName == "Dissolve" || mr.shaderName == "Distortion" || mr.shaderName == "Reflection" || mr.shaderName == "Glass") {
						renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.reflectivity, mr.useCubemap);
					} else {
						// それ以外は同じモデルをまとめて描く
						renderer_->DrawMeshInstanced(
						    mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.extraTextureHandles,uint64_t(entt::to_integral(entity))+1);
					}
				}
			}
		}

		// 旧 SceneObject との互換用 (MeshRenderer を持たず自前の modelHandle を持つ場合)
		// 基本は MeshRendererComponent にまとめる方針なので、処理は無効化して残している
		/*
		if (!hasMeshRenderer && obj.modelHandle != 0) { ... }
		*/

		// 川の描画 (メッシュはワールド座標で作ってあるので単位行列で描く)
		// 川を持っていれば
		if (registry_.all_of<RiverComponent>(entity)) {
			const auto& rv = registry_.get<RiverComponent>(entity);
			if (rv.enabled && rv.meshHandle != 0) {
				// 川のテクスチャ
				auto tex = renderer_->LoadTexture2D(rv.texturePath);
				// 単位行列の代わりの、移動・回転なし・大きさ 1 のトランスフォーム
				Engine::Transform identity;
				identity.translate = {0, 0, 0};
				identity.rotate = {0, 0, 0};
				identity.scale = {1, 1, 1};
				// 流れの速さと UV の倍率を色の欄で渡して、川のシェーダーで描く
				renderer_->DrawMesh(rv.meshHandle, tex, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
			}
		}
	}

	// 半透明の物 (スライムなど) を後から描く
	for (auto entity : renderView) {
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			// 無効なら飛ばす
			if (!mr.enabled) continue;
			// 半透明のシェーダーのものだけ描く
			if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth" || mr.shaderName == "EnergyBeam" || mr.shaderName == "EnergyCylinder") {
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				// 色 (ColorComponent があればその色)
				Engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (registry_.all_of<ColorComponent>(entity)) {
					const auto& cc = registry_.get<ColorComponent>(entity);
					color = { cc.color.x, cc.color.y, cc.color.z, cc.color.w };
				}
				// 半透明として描く
				renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world,
					{ color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w },
					mr.shaderName, mr.reflectivity > 0 ? mr.reflectivity : 1.0f, true);
			}
		}
	}

#if defined(USE_IMGUI) && !defined(NDEBUG)
	// Scene ビューのときだけ、選択中の強調とライトの印を描く
	if (editorView_ == EditorView::Scene) {
		DrawSelectionHighlight();
		DrawLightGizmos();
	}
#endif
	// パーティクルの描画
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		// 有効なものだけ描く
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});

	// 旧: CPU 計算のスライム描画 (軽量なスライム用シェーダーに置き換えたので無効)
	renderer_->SetCustomDrawJob(nullptr);

    // Scene ビューは Unity と同様にゲーム内 UI (ポーズ画面・HUD) を重ねず、空間だけを見せる
    const bool sceneView = editorView_ == EditorView::Scene;
    // ポーズ中: グラフィック設定画面またはポーズメニューを描く
    if (isPlaying_ && isPaused_) {
        // Scene ビューでは何も重ねない
        if (sceneView) return;
        // UI の描画先 (エディタの Game ビューならマウス座標を上書きする)
        UI::Canvas ui(renderer_,ctx_.viewportSize.x,ctx_.viewportSize.y);if(ctx_.useOverrideMouse)ui.SetPointer(ctx_.overrideMouseX,ctx_.overrideMouseY);
        // グラフィック設定画面 (保存に失敗したら注意を出す)
        if(graphicsOpen_){UI::DrawGraphics(ui,*renderer_);if(graphicsSaveFailed_)ui.Text(UI::Str("pause.saveFailed"),110,689,17,UI::Gold,UI::JapaneseFont);return;}
        // スライム本編は強化パレット、旧アクションゲームはポーズメニュー
        if(chronoMode_&&!registry_.view<Chrono::InkPlayer>().empty()){auto view=registry_.view<Chrono::InkPlayer>();UI::RoguePalette(ui,view.get<Chrono::InkPlayer>(*view.begin()).rogue);}else ui.Pause();
        // 右上のグラフィック設定ボタン
        ui.Button(UI::GraphicsButton,UI::Str(UI::UsingPad()?"pause.graphics":"pause.graphicsKey"),false,"xbox_button_view");
        return;
    }
    // 各システムの描画
    for (auto& system : systems_) {
        system->Draw(registry_, ctx_);
		// スライム本編の HUD は Renderer::EndFrame でまとめて描かれる前に登録する
		if (chronoMode_ && isPlaying_ && !sceneView) system->DrawUI(registry_, ctx_);
	}
}

// ゲーム内 UI の描画 (旧アクションゲームのシステムが使う)
void GameScene::DrawUI() {
	// 描画の窓口が無ければ何もしない
	if (!renderer_) return;

	// Scene ビューではゲームの HUD を描かない (Draw と同じ扱い)
	if (!isPlaying_ || editorView_ == EditorView::Scene)
		return;
	// 旧アクションゲームのときだけ、各システムの UI を描く
	for (auto& sys : systems_) {
		if (!chronoMode_) sys->DrawUI(registry_, ctx_);
	}
}


// ステージクリアしたか (本編は ChronoSystem、旧モードは WaveSystem で判定する)
bool GameScene::IsStageClear() const {
	// 本編は ChronoSystem の終了判定
	if (chronoSystem_) return chronoSystem_->Finished();
	// 旧モードはウェーブがクリアの状態か
	return waveSystem_ && waveSystem_->state == WaveSystem::State::Clear;
}

// エディタ: 選択中のエンティティを箱の線で強調表示する
void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	// 選択中のエンティティを順に処理する
	for (auto entity : selectedEntities_) {
		// 消えているか位置が無ければ飛ばす
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity))
			continue;

		// 位置
		auto& tc = registry_.get<TransformComponent>(entity);
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};

		// ワールド行列
		Engine::Matrix4x4 mat = this->GetWorldMatrix(static_cast<int>(entity));
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		// モデルの基準の箱 (±1) をワールド行列で変換し、12本の辺を黄色で描く
		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
		Engine::Vector3 v[8] = {
		    {-1.0f, -1.0f, -1.0f},
            {1.0f,  -1.0f, -1.0f},
            {1.0f,  1.0f,  -1.0f},
            {-1.0f, 1.0f,  -1.0f},
            {-1.0f, -1.0f, 1.0f },
            {1.0f,  -1.0f, 1.0f },
            {1.0f,  1.0f,  1.0f },
            {-1.0f, 1.0f,  1.0f },
		};

		// 8 つの角をワールド座標へ変換する
		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
		// 箱の 12 本の辺 (角の番号の組)
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		// 辺を描く
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		// 箱の当たり判定 (緑)
		if (registry_.all_of<BoxColliderComponent>(entity)) {
			const auto& bc = registry_.get<BoxColliderComponent>(entity);
			if (bc.enabled) {
				// 箱の半分の大きさと中心
				float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
				Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
				Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f};
				Engine::Vector3 cv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
			}
		}

		// GPU メッシュコライダー (青、重なっていれば赤)
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			const auto& gmc = registry_.get<GpuMeshColliderComponent>(entity);
			if (gmc.enabled) {
				Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
				// モデルの基準の箱 (±1)
				float hs = 1.0f;
				Engine::Vector3 cv[8] = {
				    {-hs, -hs, -hs},
                    {hs,  -hs, -hs},
                    {hs,  hs,  -hs},
                    {-hs, hs,  -hs},
                    {-hs, -hs, hs },
                    {hs,  -hs, hs },
                    {hs,  hs,  hs },
                    {-hs, hs,  hs }
                };
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
			}
		}

		// 攻撃判定 (赤。有効なときは濃く)
		if (registry_.all_of<HitboxComponent>(entity)) {
			const auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.enabled) {
				// 箱の半分の大きさと中心
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		// 食らい判定 (緑)
		if (registry_.all_of<HurtboxComponent>(entity)) {
			const auto& hb = registry_.get<HurtboxComponent>(entity);
			if (hb.enabled) {
				// 箱の半分の大きさと中心
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		// 変形ツールの形を描く (移動は矢印、回転は円、拡大は先端の × 印)。ドラッグ中の軸は明るくする
		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) * DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
		// ギズモのローカル座標の線を、ワールド座標に直して描く
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		// 軸の長さと、矢印の先の大きさ
		const float al = 2.0f, ar = 0.3f;
		// ドラッグ中の軸 (選択中のものをドラッグしているときだけ)
		int dAxis = (EditorUI::IsGizmoDragging() && entity == selectedEntity_) ? EditorUI::GetGizmoDragAxis() : -1;
		// 軸の色 (X 赤・Y 緑・Z 青、ドラッグ中は明るく)
		auto axCol = [](int axis, int drag) -> Engine::Vector4 {
			bool a = (drag == axis);
			switch (axis) {
			case 0:
				return a ? Engine::Vector4{1, .6f, .6f, 1} : Engine::Vector4{1, .2f, .2f, 1};
			case 1:
				return a ? Engine::Vector4{.6f, 1, .6f, 1} : Engine::Vector4{.2f, 1, .2f, 1};
			case 2:
				return a ? Engine::Vector4{.6f, .6f, 1, 1} : Engine::Vector4{.2f, .2f, 1, 1};
			default:
				return {1, 1, 1, 1};
			}
		};
		// 3 軸の色
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		// 移動: 各軸の線と矢印の先
		if (EditorUI::GetGizmoMode() == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (EditorUI::GetGizmoMode() == GizmoMode::Rotate) {
			// 回転: 各軸のまわりの円を 32 分割で描く
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
			// 拡大: 各軸の線と先の × 印
			float e = 0.15f;
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
			drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
			drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
			drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		}
	}
}

// エディタ: 地面の格子 (10 マスごとに濃く)、X 軸 (赤) と Z 軸 (青)、原点の XYZ 軸
void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	// 格子の範囲 (±100) と間隔 (1)
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		// 中心の線は軸として別に描く
		if (std::fabs(i) < 0.01f)
			continue;
		// 10 ごとの線は濃く
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		// X 方向と Z 方向の線
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	// X 軸 (赤) と Z 軸 (青)
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	// 原点の XYZ 軸 (手前に描く)
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

// エディタ UI の描画
void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

// エディタ: ライトの位置と向きを線で描く (選択中は濃く)
void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	// 平行光源 (黄): 十字と向き
	auto dlView = registry_.view<DirectionalLightComponent, TransformComponent>();
	dlView.each([&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!dl.enabled)
			return;
		// 位置と向き (回転行列の Z 軸)
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
		// 向きの線 (長さ 5) と十字
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});

	// 点光源 (緑): 3軸の十字
	auto plView = registry_.view<PointLightComponent, TransformComponent>();
	plView.each([&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!pl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
		float s = 0.5f;
		// X・Y・Z の十字
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
	});

	// スポットライト (水色): 十字と向き
	auto slView = registry_.view<SpotLightComponent, TransformComponent>();
	slView.each([&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!sl.enabled)
			return;
		// 位置と向き (回転行列の Z 軸)
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
		// 向きの線 (長さ 5) と十字
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
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

// =====================================================
// タグ検索の実装
// =====================================================

// タグでエンティティの一覧を返す (無ければ空の一覧)
const std::vector<entt::entity>& GameScene::GetEntitiesByTag(TagType tag) {
	// 見つからないときに返す空の一覧
	static const std::vector<entt::entity> emptyResult;
	// タグの一覧を探す
	auto it = tagCache_.find(tag);
	if (it != tagCache_.end()) {
		return it->second;
	}
	return emptyResult;
}

// タグを設定する (キャッシュへの反映は次の Update で行う)
void GameScene::SetTag(entt::entity entity, TagType tag) {
	// 存在しなければ何もしない
	if (!registry_.valid(entity)) {
		return;
	}
	// タグのコンポーネント (無ければ付ける) に書き込む
	auto& tc = registry_.get_or_emplace<TagComponent>(entity);
	tc.tag = tag;
	// すぐには同期せず、同期待ちの一覧に入れる
	pendingTagSync_.push_back(entity);
}

// タグが付いたとき
void GameScene::OnTagAdded(entt::registry& /*registry*/, entt::entity entity) {
	// すぐには同期せず、次の Update で同期する
	pendingTagSync_.push_back(entity);
}

// タグが外れたとき
void GameScene::OnTagRemoved(entt::registry& /*registry*/, entt::entity entity) {
	// すぐには消さず、次の Update の最初に消す
	// (反復処理の途中でコンテナを書き換えて壊さないため)
	pendingTagRemoved_.push_back(entity);
}

// スクリプトが外れたとき
void GameScene::OnScriptDestroyed(entt::registry& registry, entt::entity entity) {
	// スクリプトが破棄されるときに C++ スクリプトの OnDestroy を呼ぶ
	// スクリプトのコンポーネントがあれば止める
	if (auto* sc = registry.try_get<ScriptComponent>(entity)) {
		StopScripts(*sc, entity, this);
	}
}

// タグのキャッシュにエンティティを追加する
void GameScene::SyncTag(entt::entity entity) {
	// 存在しないかタグが無ければ何もしない
	if (!registry_.valid(entity) || !registry_.all_of<TagComponent>(entity)) {
		return;
	}

	// 新しいタグの一覧に追加する (古い一覧からの削除は Update の最初にまとめて行う前提)
	const TagType tag = registry_.get<TagComponent>(entity).tag;
	
	// 1件ずつ重複を調べると遅いので、Update 側の削除を信頼する
	tagCache_[tag].push_back(entity);
}

// 文字列のタグで探す / 設定する
const std::vector<entt::entity>& GameScene::GetEntitiesByTag(const std::string& tag) {
	return GetEntitiesByTag(StringToTag(tag));
}

void GameScene::SetTag(entt::entity entity, const std::string& tagStr) {
	// 文字列をタグの列挙値に変換して設定する
	SetTag(entity, StringToTag(tagStr));
}

// 流体のプレビューを初期状態にする (GPU 流体を空にし、エミッターの放出数を 0 に戻す)
void GameScene::ResetFluidPreview() {
	// GPU 流体を空にする
	if (renderer_) renderer_->ResetGPUFluid();
	gpuSlimeEmitted_ = false;
	// 流体エミッターの出した数と端数を 0 に戻す
	auto emitters = registry_.view<FluidEmitterComponent>();
	for (auto entity : emitters) {
		auto& emitter = emitters.get<FluidEmitterComponent>(entity);
		emitter.emittedParticles = 0;
		emitter.emissionRemainder = 0;
	}
}

// シーンを完全に空にする
void GameScene::ClearScene() {
	gpuSlimeEmitted_ = false;
    // 選択を外し、止めていた世界を動かす
    ClearSelection();
    if(renderer_)renderer_->SetRogueWorldFrozen(false);
	// すべてのスクリプトを止める
	StopAllScripts(registry_, this);
	// 1. 各システムの状態をリセットする
	for (auto& sys : systems_) {
		sys->Clear(registry_);
	}

	// 2. 予約やキャッシュを全部消す
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
		pendingTagSync_.clear();
		pendingTagRemoved_.clear();
		tagCache_.clear();
		matrixCache_.clear();
	}

	// 3. レジストリを空にする
	// 全消去の間は OnDestroy などのシグナルで二重に破棄しないよう (反復処理の破壊を防ぐ)、シグナルを一時的に外す
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);

	registry_.clear();

	// シグナルをつなぎ直す
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);

	// 4. CPU 側スライムメッシュを未初期化に戻す
	slimeCpuLogic_.initialized = false;
	projectileCpuLogic_.initialized = false;

	if (renderer_) {
		// GPU 流体を空にする
		renderer_->ClearGPUFluid();
	}
}

// ポーズメニューは共通の UI キャンバスで描くので、ここでは何も作らない
void GameScene::CreatePauseMenu() { /* 共通の UI キャンバスで描くので、ここでは何もしない */ }

// ポーズメニュー用のエンティティを破棄する
void GameScene::DestroyPauseMenu() {
	// ポーズメニューのタグを持つものを破棄する
	const auto& pauseUIs = GetEntitiesByTag(TagType::PauseUI);
	for (auto e : pauseUIs) {
		if (registry_.valid(e)) {
			DestroyObject(static_cast<uint32_t>(e));
		}
	}
}

} // namespace Game






