#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "../ObjectTypes.h"
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // ★追加
#include "../../Engine/Input.h"      // ★追加

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // ★追加
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/PlayerActionSystem.h" // ★追加: プレイヤーアクション
#include "../Systems/CombatSystem.h"       // ★追加: 戦闘判定
#include "../Systems/EnemyAISystem.h"      // ★追加: 敵AI
#include "../Systems/BossActionSystem.h"    // ★追加: ボスAI
#include "../Systems/WaveSystem.h"         // ★追加: ウェーブ管理
#include "../Scripts/BossTestScript.h"      // ★追加: ボス生成用

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // ★追加
#include "../../Engine/NetworkProfiler.h"
#include "../../externals/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h" // ★追加: ImGui
#include <Windows.h> // OutputDebugStringA
#include <algorithm>
#include <cmath>
#include <filesystem> // ★追加: Skybox DDS検索用

namespace Game {

namespace {
	// CPU側でのぷよぷよ感計算用の簡易3Dノイズ関数 (SimplexNoise代用)
	float Noise3D(float x, float y, float z) {
		float n = std::sin(x) * std::cos(y) * std::sin(z);
		n += std::sin(x * 2.1f + y * 1.3f) * 0.5f;
		n += std::cos(y * 1.7f + z * 2.5f) * 0.5f;
		return n * 0.5f;
	}
}

GameScene::~GameScene() {
    registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
    registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
    registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
    ClearScene(); // ★重複排除: シーンの完全クリア処理を呼び出す
    
    // ClearScene 内で再接続されてしまうため、デストラクタの最後で確実に切断する
    registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
    registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
    systems_.clear();

    Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback(nullptr);
    Engine::NetworkProfiler::GetInstance().SetParameterGetCallback(nullptr);
}

void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	eventSystem_.Clear();
	playTime_ = 0.0f;
	camera_.Initialize();
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 10000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
	
	editorCameraPos_ = {0, 2, -5};
	editorCameraRot_ = {0.2f, 0, 0};

	renderer_->SetAmbientColor({0.4f, 0.4f, 0.45f});

	bool loaded = false;
	// ★ リリース構成等での自動ロード
	try {
		std::string scenePath = params.stagePath.empty() ? EditorUI::GetUnifiedProjectPath("Resources/Scenes/scene.json") : params.stagePath;
		// ★修正: UTF-8文字列をFromUTF8経由でfs::pathに変換し、日本語パスに対応
		if (std::filesystem::exists(Engine::PathUtils::FromUTF8(scenePath))) {
			OutputDebugStringA(("[GameScene] " + scenePath + " found. Loading...\n").c_str());
			EditorUI::LoadScene(this, scenePath);
			isPlaying_ = true; // リリース/起動時はプレイ状態から開始する
			loaded = true;
		} else {
			OutputDebugStringA(("[GameScene] " + scenePath + " NOT found.\n").c_str());
		}

	} catch (const std::exception& e) {
		std::string msg = "[GameScene] EXCEPTION during scene load: " + std::string(e.what()) + "\n";
		OutputDebugStringA(msg.c_str());
		MessageBoxA(NULL, msg.c_str(), "Scene Load Error", MB_OK | MB_ICONERROR);
	}

	// 既にオブジェクトが存在する場合（リスタート時）やロード失敗時は最低限の内容を作成
	if (registry_.storage<entt::entity>().empty() || !loaded) {
		auto sun = registry_.create();
		registry_.emplace<NameComponent>(sun, "Sun");
		registry_.emplace<TransformComponent>(
		    sun, DirectX::XMFLOAT3{0, 10, 0}, DirectX::XMFLOAT3{DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(30.0f), 0}, DirectX::XMFLOAT3{1, 1, 1});
		registry_.emplace<DirectionalLightComponent>(sun);

		// ステージ：メインモデル（地形・コリジョンあり）
		auto stageMain = registry_.create();
		registry_.emplace<NameComponent>(stageMain, "CrystalForest_Main");
		auto& meshMain = registry_.emplace<MeshRendererComponent>(stageMain);
		meshMain.modelHandle = renderer_->LoadObjMesh("Resources/Models/Stages/map/CrystalForest_02_Main.glb");
		meshMain.modelPath = "Resources/Models/Stages/map/CrystalForest_02_Main.glb";
		registry_.emplace<TransformComponent>(stageMain, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{10.0f, 10.0f, 10.0f});
		
		// 物理判定用にGpuMeshColliderを付与
		auto& gmcMain = registry_.emplace<GpuMeshColliderComponent>(stageMain);
		gmcMain.meshHandle = meshMain.modelHandle;
		gmcMain.meshPath = meshMain.modelPath;
		gmcMain.enabled = true;

		// ステージ：空（Sky）モデル（コリジョンなし）
		auto stageSky = registry_.create();
		registry_.emplace<NameComponent>(stageSky, "CrystalForest_Sky");
		auto& meshSky = registry_.emplace<MeshRendererComponent>(stageSky);
		meshSky.modelHandle = renderer_->LoadObjMesh("Resources/Models/Stages/map/CrystalForest_01_Sky.glb");
		meshSky.modelPath = "Resources/Models/Stages/map/CrystalForest_01_Sky.glb";
		registry_.emplace<TransformComponent>(stageSky, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{10.0f, 10.0f, 10.0f});

		// ★追加: プレイヤー（スライム）の生成
		auto player = registry_.create();
		registry_.emplace<NameComponent>(player, "Player");
		registry_.emplace<TransformComponent>(player, DirectX::XMFLOAT3{0, 1.0f, -0.15f}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{1, 1, 1});
		
		auto& mrP = registry_.emplace<MeshRendererComponent>(player);
		mrP.modelPath = "Resources/Models/player_ball/ball.obj";
		mrP.modelHandle = renderer_->LoadObjMesh(mrP.modelPath);
		mrP.shaderName = "Slime";
		mrP.color = {0.4f, 0.8f, 0.1f, 0.8f};
		mrP.useCubemap = true;
		mrP.reflectivity = 1.0f;

		auto& bc = registry_.emplace<BoxColliderComponent>(player);
		bc.center = {0,0,0}; bc.size = {1.5f, 1.5f, 1.5f};
		
		auto& rb = registry_.emplace<RigidbodyComponent>(player);
		rb.useGravity = true; rb.isKinematic = true;
		
		auto& hc = registry_.emplace<HealthComponent>(player);
		hc.hp = 100; hc.maxHp = 100;
		
		auto& tc = registry_.emplace<TagComponent>(player);
		tc.tag = TagType::Player;
		
		registry_.emplace<PlayerInputComponent>(player);
		
		auto& cmc = registry_.emplace<CharacterMovementComponent>(player);
		cmc.speed = 8.0f; cmc.jumpPower = 10.0f; cmc.gravity = 9.8f; cmc.heightOffset = 0.4f;
		
		auto& ctc = registry_.emplace<CameraTargetComponent>(player);
		ctc.distance = 12.0f; ctc.height = 2.5f; ctc.smoothSpeed = 8.0f;
		
		auto& hbc = registry_.emplace<HitboxComponent>(player);
		hbc.center = {0,0,1.5f}; hbc.size = {1.5f,1.5f,4.0f}; hbc.damage = 15; hbc.tag = TagType::Player;
		
		auto& hurtc = registry_.emplace<HurtboxComponent>(player);
		hurtc.center = {0,0,0}; hurtc.size = {2.0f,2.0f,2.0f}; hurtc.damageMultiplier = 1.0f; hurtc.tag = TagType::Player;
		
		auto& pac = registry_.emplace<PlayerActionComponent>(player);
		pac.dodgeDuration = 0.4f; pac.dodgeSpeed = 15.0f;
	}


	Game::FluidSystem::GetInstance()->Initialize(renderer_->GetDevice());

	// エディターUIの初期化
	EditorUI::Initialize(renderer_);

	// ★追加: Skybox用キューブマップの読み込み (00. 環境マップ)
	// DDSファイルが Resources/Textures/ に配置されていれば読み込む
	// 注意: rostock_laage_airport_4k.dds は使用禁止
	{
		namespace fs = std::filesystem;
		std::string texDir = EditorUI::GetUnifiedProjectPath("Resources/Textures");
		try {
			for (const auto& entry : fs::directory_iterator(Engine::PathUtils::FromUTF8(texDir))) {
				if (entry.is_regular_file() && entry.path().extension() == L".dds") {
					std::string filename = Engine::PathUtils::ToUTF8(entry.path().filename().wstring());
					// 使用禁止のファイルをスキップ
					if (filename.find("rostock_laage_airport") != std::string::npos) continue;
					
					std::string ddsPath = Engine::PathUtils::ToUTF8(entry.path().wstring());
					auto cubeHandle = renderer_->LoadCubeMap(ddsPath);
					if (cubeHandle > 0) {
						renderer_->SetSkyboxTexture(cubeHandle);
						OutputDebugStringA(("[GameScene] Skybox loaded: " + filename + "\n").c_str());
					}
					break; // 最初に見つかったDDSを使用
				}
			}
		} catch (...) {
			OutputDebugStringA("[GameScene] Skybox DDS search failed, using default\n");
		}
	}

	// パーティクルエディターの初期化
	particleEditor_.Initialize();

	// スクリプトエンジンの初期化
	ScriptEngine::GetInstance()->Initialize();

	// ★ Systemの登録（順序が重要）
	systems_.clear();
	systems_.push_back(std::make_unique<PlayerInputSystem>());
	systems_.push_back(std::make_unique<PlayerActionSystem>());  // ★追加: 攻撃・パリィ・回避
	systems_.push_back(std::make_unique<WeaponSystem>());        // ★追加: 武器の管理・アニメーション
	systems_.push_back(std::make_unique<CharacterMovementSystem>());
	systems_.push_back(std::make_unique<PhysicsSystem>());
	systems_.push_back(std::make_unique<EnemyAISystem>());      // ★追加: 敵AI（CombatSystemの前）
	systems_.push_back(std::make_unique<BossActionSystem>());   // ★追加: ボスAI（CombatSystemの前）
	systems_.push_back(std::make_unique<CombatSystem>());         // ★追加: Hitbox vs Hurtbox 判定
	systems_.push_back(std::make_unique<CameraFollowSystem>());
	systems_.push_back(std::make_unique<HealthSystem>());
	systems_.push_back(std::make_unique<WaveSystem>());           // ★追加: ウェーブ管理

	auto scriptSys = std::make_unique<ScriptSystem>();
	scriptSys->SetScene(this);
	systems_.push_back(std::move(scriptSys));


	systems_.push_back(std::make_unique<AudioSystem>());
	systems_.push_back(std::make_unique<UISystem>());
	systems_.push_back(std::make_unique<MotionSystem>());
	systems_.push_back(std::make_unique<PostProcessSystem>()); // ★追加
	systems_.push_back(std::make_unique<CleanupSystem>());

	// リスナー登録
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);

	// ★追加: 操作説明テキストUIの生成
	{
		std::vector<std::string> controls = {
			"【操作説明】",
			"W, A, S, D : 移動",
			"Space : ジャンプ",
			"Shift : 回避",
			"左クリック : 攻撃(大剣は長押しで溜め)",
			"右クリック : パリィ",
			"中クリック : ロックオン",
			"マウス : 視点移動"
		};
		float startX = 20.0f;
		float startY = 120.0f; // プレイヤーHUD(左上)に被らないように下げる
		float gapY = 35.0f;
		for (size_t i = 0; i < controls.size(); ++i) {
			auto textEntity = registry_.create();
			registry_.emplace<NameComponent>(textEntity, "ControlText_" + std::to_string(i));
			auto& tc = registry_.emplace<TransformComponent>(textEntity);
			tc.translate.x = startX;
			tc.translate.y = startY + (i * gapY);
			tc.translate.z = 0.0f;
			
			auto& txt = registry_.emplace<UITextComponent>(textEntity);
			txt.text = controls[i];
			txt.fontSize = (i == 0) ? 28.0f : 24.0f;
			txt.color = (i == 0) ? DirectX::XMFLOAT4{0.6f, 1.0f, 0.4f, 1.0f} : DirectX::XMFLOAT4{0.4f, 1.0f, 0.4f, 1.0f}; // 見出しは黄緑、通常は緑色に変更して背景との同化を防ぐ
		}
	}

	// 5. 初期シーン状態の保存 (ロード時以外)
	if (!loaded) {
		initialSceneSnapshot_ = EditorUI::SaveToMemory(this);
	}


	// 各Systemのリセット
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}



	// ★追加: タグシステムの初期化
	tagCache_.clear();
	pendingTagSync_.clear();
	pendingTagRemoved_.clear();
	auto tagInitView = registry_.view<TagComponent>();
	for (auto entity : tagInitView) {
		const auto tag = tagInitView.get<TagComponent>(entity).tag;
		tagCache_[tag].push_back(entity);
	}



	// ★追加: WebProfilerの双方向通信パラメータのコールバック登録
	Engine::NetworkProfiler::GetInstance().SetParameterUpdateCallback([this](const std::string& target, const std::string& prop, float val) {
		if (target == "AmbientColor") {
			auto current = renderer_->GetAmbientColor();
			if (prop == "R") current.x = val;
			else if (prop == "G") current.y = val;
			else if (prop == "B") current.z = val;
			renderer_->SetAmbientColor(current);
		} else if (target == "Player" && prop == "Speed") {
			const auto& players = GetEntitiesByTag(TagType::Player);
			if (!players.empty()) {
				entt::entity p = players[0];
				if (registry_.valid(p) && registry_.all_of<CharacterMovementComponent>(p)) {
					registry_.get<CharacterMovementComponent>(p).speed = val;
				}
			}
		}
	});

	Engine::NetworkProfiler::GetInstance().SetParameterGetCallback([this]() -> std::string {
		nlohmann::json j;
		auto amb = renderer_->GetAmbientColor();
		j["AmbientColor"]["R"] = amb.x;
		j["AmbientColor"]["G"] = amb.y;
		j["AmbientColor"]["B"] = amb.z;
		
		j["Player"]["Speed"] = 15.0f; // Default if not found
		const auto& players = GetEntitiesByTag(TagType::Player);
		if (!players.empty()) {
			entt::entity p = players[0];
			if (registry_.valid(p) && registry_.all_of<CharacterMovementComponent>(p)) {
				j["Player"]["Speed"] = registry_.get<CharacterMovementComponent>(p).speed;
			}
		}
		return j.dump();
	});

	// ★追加: 常にプレイ状態で開始する
	isPlaying_ = true;
}

// =====================================================
// ★ Update: 各Systemに処理を委譲
// =====================================================
void GameScene::Update() {
	if (!renderer_) return;

	// ★追加: Yキーでデバッグベクトルの表示/非表示を切り替え
	if (Engine::Input::GetInstance()->Trigger(0x15)) { // 0x15 = DIK_Y
		renderer_->SetDrawFluidDebugArrows(!renderer_->GetDrawFluidDebugArrows());
	}

	// ★追加: 行列キャッシュを毎フレームクリア
	ClearMatrixCache();

	// ★追加: タグの遅延同期および削除（生成直後や破棄時の同期待ちを処理）
	// リストが空でない場合のみ処理
	if (!pendingTagRemoved_.empty() || !pendingTagSync_.empty()) {
		// 1. 削除・変更予定のエンティティを全キャッシュから取り除く
		std::vector<entt::entity> toRemove = std::move(pendingTagRemoved_);
		// pendingTagSync に入っているものは「タグが変わる」可能性があるので、一旦古いキャッシュから消しておく
		for (auto e : pendingTagSync_) {
			toRemove.push_back(e);
		}

		for (auto e : toRemove) {
			for (auto& pair : tagCache_) {
				auto& vec = pair.second;
				// すべてのタグリストから、そのエンティティを削除
				vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
			}
		}

		// 2. 最新のタグで同期
		std::vector<entt::entity> toSync = std::move(pendingTagSync_);
		for (auto e : toSync) {
			if (registry_.valid(e)) {
				SyncTag(e);
			}
		}
	}

	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	last = now;

	if (dt > 1.0f / 10.0f)
		dt = 1.0f / 60.0f; // 極端なラグ対策

	// コンテキストを更新
	ctx_.dt = dt;

	if (isPlaying_) {
		// ポーズメニューのボタン入力判定
		if (isPaused_) {
			const auto& pauseUIs = GetEntitiesByTag(TagType::PauseUI);
			for (auto e : pauseUIs) {
				if (registry_.valid(e) && registry_.all_of<UIButtonComponent, NameComponent>(e)) {
					auto& btn = registry_.get<UIButtonComponent>(e);
					if (btn.isHovered && Engine::Input::GetInstance()->IsMouseTrigger(0)) {
						auto& name = registry_.get<NameComponent>(e).name;
						if (name == "ResumeButton") {
							isPaused_ = false;
							ShowCursor(FALSE);
							DestroyPauseMenu();
							break;
						} else if (name == "TitleButton") {
							isPaused_ = false;
							ShowCursor(FALSE);
							// シーン破棄前にUIを個別に削除すると、次フレームのClearScene()と競合するリスクがあるため
							// DestroyPauseMenu() は呼ばずにそのままシーン遷移をリクエストする
							Engine::SceneManager::GetInstance()->RequestChange("Title");
							break;
						}
					}
				}
			}
		}

		// ESCキーでポーズ切り替え (0x01 = DIK_ESCAPE)
		if (Engine::Input::GetInstance()->Trigger(0x01)) {
			isPaused_ = !isPaused_;
			if (isPaused_) {
				ShowCursor(TRUE);
				CreatePauseMenu();
			} else {
				ShowCursor(FALSE);
				DestroyPauseMenu();
			}
		}

		if (!isPaused_) {
			playTime_ += dt;
			
			// ★追加: ラジアルメニューが開いているかチェック
			bool isRadialMenuOpen = false;
			auto piView = registry_.view<PlayerInputComponent>();
			for (auto e : piView) {
				if (piView.get<PlayerInputComponent>(e).isRadialMenuOpen) {
					isRadialMenuOpen = true;
					break;
				}
			}

			// ★追加: Play中はマウスカーソルを画面中央に固定 (ラジアルメニューを開いていない場合)
			if (!isRadialMenuOpen && dx_ && dx_->GetHwnd()) {
				POINT center = { (LONG)Engine::WindowDX::kW / 2, (LONG)Engine::WindowDX::kH / 2 };
				ClientToScreen(dx_->GetHwnd(), &center);
				SetCursorPos(center.x, center.y);
			}
		}
	}




	ctx_.camera = &camera_;
	ctx_.renderer = renderer_;
	ctx_.input = Engine::Input::GetInstance();
	ctx_.isPlaying = isPlaying_;
	ctx_.scene = this;
	ctx_.eventSystem = &eventSystem_;
	ctx_.pendingSpawns = &pendingSpawns_;

	// ★追加: ビューポート情報をデフォルトのウィンドウサイズで初期設定 (エディタ非実行時のレイアウト崩れ防止)
	ctx_.viewportOffset = { 0.0f, 0.0f };
	ctx_.viewportSize = { (float)Engine::WindowDX::kW, (float)Engine::WindowDX::kH };

	// GPU Collision Dispatch（エンジンの汎用 PhysicsSystem.h に移行したため、ここでは何もしない）

	// Animation（エンジン固有処理のため残留）
	auto animView = registry_.view<AnimatorComponent, MeshRendererComponent>();
	if (isPlaying_ && !isPaused_) {
		std::vector<entt::entity> animEntities;
		animView.each([&](entt::entity entity, auto&, auto&) { animEntities.push_back(entity); });

		if (!animEntities.empty()) {
			Engine::JobSystem::Dispatch((uint32_t)animEntities.size(), 64, [&](uint32_t i) {
				auto entity = animEntities[i];
				auto& anim = registry_.get<AnimatorComponent>(entity);
				auto& meshWrapper = registry_.get<MeshRendererComponent>(entity);

				if (anim.enabled && anim.isPlaying) {
					anim.time += dt * 60.0f * anim.speed;
					auto* m = renderer_->GetModel(meshWrapper.modelHandle);
					if (m) {
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
								break;
							}
						}
					}
				}
			});
			Engine::JobSystem::Wait();
		}
	}

	// ★追加: ラジアルメニューの描画
	if (isPlaying_ && !isPaused_) {
		auto piView = registry_.view<PlayerInputComponent>();
		for (auto e : piView) {
			auto& pi = registry_.get<PlayerInputComponent>(e);
			if (pi.isRadialMenuOpen) {
				ShowCursor(TRUE); // 開いている間はカーソルを表示
				
				float centerX = ctx_.viewportSize.x > 0 ? ctx_.viewportSize.x * 0.5f : (float)Engine::WindowDX::kW * 0.5f;
				float centerY = ctx_.viewportSize.y > 0 ? ctx_.viewportSize.y * 0.5f : (float)Engine::WindowDX::kH * 0.5f;
				
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
				
				float dx_mouse = mx - centerX;
				float dy_mouse = my - centerY;
				float angle = std::atan2(dy_mouse, dx_mouse);
				if (angle < 0) angle += 3.14159f * 2.0f;
				
				// 3分割: 右=Fire(0~120度), 左下=Water(120~240度), 左上=Thunder(240~360度)
				int selectedIndex = 0;
				if (angle > 3.14159f * 2.0f / 3.0f * 2.0f) {
					selectedIndex = 2; // Thunder
				} else if (angle > 3.14159f * 2.0f / 3.0f) {
					selectedIndex = 1; // Water
				} else {
					selectedIndex = 0; // Fire
				}
				
				// 選択中の缶をセット
				if (selectedIndex == 0) pi.selectedCan = CanType::Fire;
				else if (selectedIndex == 1) pi.selectedCan = CanType::Water;
				else if (selectedIndex == 2) pi.selectedCan = CanType::Thunder;

				// 描画 (ImGuiに依存せずRendererを使う)
				const char* names[] = {"Fire", "Water", "Thunder"};
				
				float radius = 120.0f;
				float boxSize = 80.0f;
				
				auto texH = ctx_.renderer->LoadTexture2D("Resources/Textures/ball.png");

				for (int i = 0; i < 3; ++i) {
					float startAngle = (3.14159f * 2.0f / 3.0f) * i;
					float textAngle = startAngle + (3.14159f * 2.0f / 6.0f); // 扇形の中心角度
					
					float px = centerX + std::cos(textAngle) * radius;
					float py = centerY + std::sin(textAngle) * radius;
					
					Engine::Vector4 color = {100.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 180.0f/255.0f};
					if (i == 0) color = (i == selectedIndex) ? Engine::Vector4{255.0f/255.0f, 100.0f/255.0f, 100.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{150.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 180.0f/255.0f};
					if (i == 1) color = (i == selectedIndex) ? Engine::Vector4{100.0f/255.0f, 150.0f/255.0f, 255.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{50.0f/255.0f, 80.0f/255.0f, 150.0f/255.0f, 180.0f/255.0f};
					if (i == 2) color = (i == selectedIndex) ? Engine::Vector4{255.0f/255.0f, 255.0f/255.0f, 100.0f/255.0f, 220.0f/255.0f} : Engine::Vector4{150.0f/255.0f, 150.0f/255.0f, 50.0f/255.0f, 180.0f/255.0f};
					
					// 枠線の代わりの背景円 (少し大きめ)
					Engine::Renderer::SpriteDesc border;
					border.x = px - (boxSize + 8.0f) * 0.5f;
					border.y = py - (boxSize + 8.0f) * 0.5f;
					border.w = boxSize + 8.0f;
					border.h = boxSize + 8.0f;
					border.color = {1.0f, 1.0f, 1.0f, 0.5f};
					if (i == selectedIndex) border.color = {1.0f, 1.0f, 0.2f, 0.8f}; // 選択中は黄色く光る
					border.layer = 199;
					ctx_.renderer->DrawSprite(texH, border);

					Engine::Renderer::SpriteDesc box;
					box.x = px - boxSize * 0.5f;
					box.y = py - boxSize * 0.5f;
					box.w = boxSize;
					box.h = boxSize;
					box.color = color;
					box.layer = 200;
					ctx_.renderer->DrawSprite(texH, box);
					
					// 枠線は背景円で表現するため削除
					
					// テキスト描画
					float tw = ctx_.renderer->MeasureTextWidth(names[i], 0.3f);
					ctx_.renderer->DrawString(names[i], px - tw * 0.5f, py - 12.0f, 0.3f, {1.0f, 1.0f, 1.0f, 1.0f});
				}
			} else {
				if (!isPaused_) ShowCursor(FALSE);
			}
		}
	}

	// パーティクルエディター
	if (!isPaused_) {
		particleEditor_.Update(dt);
	}

	// ★ 全Systemを順に実行
	for (auto& system : systems_) {
		// リザルト遷移中などはシステムを動かさない (エンティティが削除されている可能性があるため)
		if (!isPlaying_ || isPaused_)
			break;
		system->Update(registry_, ctx_);
	}

	// ★ 追加: 停止中のみデバッグカメラを有効化
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
	camera_.Tick(dt);







	// ★ペンディングオブジェクト（弾など）をflushし、破棄要求を処理
	std::vector<entt::entity> destroysToProcess;
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 一旦、pendingSpawns_ をダミーとして運用するか、直接 `registry_.create()` するのでここは実質空になる
			pendingSpawns_.clear();
		}

		if (!pendingDestroys_.empty()) {
			// ★重複を排除して二重破棄を防ぐ
			std::sort(pendingDestroys_.begin(), pendingDestroys_.end());
			pendingDestroys_.erase(std::unique(pendingDestroys_.begin(), pendingDestroys_.end()), pendingDestroys_.end());
			
			destroysToProcess = std::move(pendingDestroys_);
			pendingDestroys_.clear();
		}
	}

	// ロックを外した状態で破棄を実行（OnDestroy から更に DestroyObject が呼ばれてもデッドロックしないようにする）
	for (auto id : destroysToProcess) {
		if (registry_.valid(id)) {
			registry_.destroy(id);
		}
	}

	// Light System（レンダリング設定のため残留）
	if (renderer_) {
		int plCount = 0;
		int slCount = 0;
		bool hasDirLight = false;

		auto dirLightView = registry_.view<DirectionalLightComponent, TransformComponent>();
		dirLightView.each([&](auto, const DirectionalLightComponent& dl, const TransformComponent& tc) {
			if (dl.enabled && !hasDirLight) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 color = {dl.color.x * dl.intensity, dl.color.y * dl.intensity, dl.color.z * dl.intensity};
				renderer_->SetDirectionalLight(dir, color, true);
				hasDirLight = true;
			}
		});

		auto plView = registry_.view<PointLightComponent, TransformComponent>();
		plView.each([&](auto, const PointLightComponent& pl, const TransformComponent& tc) {
			if (pl.enabled && plCount < Engine::Renderer::kMaxPointLights) {
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {pl.color.x * pl.intensity, pl.color.y * pl.intensity, pl.color.z * pl.intensity};
				Engine::Vector3 atten = {pl.atten.x, pl.atten.y, pl.atten.z};
				renderer_->SetPointLight(plCount, pos, color, pl.range, atten, true);
				plCount++;
			}
		});

		auto slView = registry_.view<SpotLightComponent, TransformComponent>();
		slView.each([&](auto, const SpotLightComponent& sl, const TransformComponent& tc) {
			if (sl.enabled && slCount < Engine::Renderer::kMaxSpotLights) {
				Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
				Engine::Vector3 dir = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
				Engine::Vector3 color = {sl.color.x * sl.intensity, sl.color.y * sl.intensity, sl.color.z * sl.intensity};
				Engine::Vector3 atten = {sl.atten.x, sl.atten.y, sl.atten.z};
				renderer_->SetSpotLight(slCount, pos, dir, color, sl.range, sl.innerCos, sl.outerCos, atten, true);
				slCount++;
			}
		});

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

	// パーティクルエミッターコンポーネント
	auto peView = registry_.view<ParticleEmitterComponent, TransformComponent, NameComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe, const TransformComponent& tc, const NameComponent& nc) {
		if (!pe.enabled)
			return;

		if (!pe.isInitialized && renderer_) {
			pe.emitter.Initialize(*renderer_, nc.name + "_Emitter");
			if (!pe.assetPath.empty()) {
				pe.emitter.LoadFromJson(pe.assetPath);
			}
			pe.isInitialized = true;
		}

		pe.emitter.params.position = {tc.translate.x, tc.translate.y, tc.translate.z};
		pe.emitter.Update(dt);
	});
}

// ★ 汎用スポーン
entt::entity GameScene::CreateEntity(const std::string& name) {
	std::lock_guard<std::mutex> lock(spawnMutex_);
	auto entity = registry_.create();
	registry_.emplace<NameComponent>(entity, name);
	registry_.emplace<TransformComponent>(entity);
	return entity;
}

// ★追加: IDでオブジェクトを検索し、破棄フラグを立てる
void GameScene::DestroyObject(uint32_t id) {
	std::lock_guard<std::mutex> lock(spawnMutex_);
	// IDをそのままentt::entityとして扱う（ダウンキャスト）
	pendingDestroys_.push_back(static_cast<entt::entity>(id));
}


// ★追加: 名前でオブジェクトを検索
entt::entity GameScene::FindObjectByName(const std::string& name) {
	auto view = registry_.view<NameComponent>();
	for (auto entity : view) {
		if (view.get<NameComponent>(entity).name == name) {
			return entity;
		}
	}
	return entt::null;
}

// ★追加: 指定座標のメッシュ表面的高さを取得
float GameScene::GetHeightAt(float x, float z, float startY, uint32_t excludeId) {
	float maxHeight = -1000.0f;
	bool hitAny = false;

	// 指定された startY (またはデフォルト 1000) から下向きにレイを飛ばす
	DirectX::XMVECTOR rayPos = DirectX::XMVectorSet(x, startY, z, 1.0f);
	DirectX::XMVECTOR rayDir = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		bool isEnemyOrBullet = false;
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX) {
				isEnemyOrBullet = true;
			}
		}
		if (isEnemyOrBullet)
			continue;

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

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = this->GetWorldMatrix(static_cast<int>(entity));

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (hitPoint.y > maxHeight) {
				maxHeight = hitPoint.y;
				hitAny = true;
			}
		}
	}

	return hitAny ? maxHeight : -10000.0f;
}

bool GameScene::RayCast(const Engine::Vector3& origin, const Engine::Vector3& direction, float maxDist, uint32_t excludeId, float& outDist) {
	bool hitAny = false;
	float minDist = maxDist;

	DirectX::XMVECTOR rayPos = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&origin));
	DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&direction));

	auto view = registry_.view<TransformComponent>();
	for (auto entity : view) {
		if (excludeId != 0 && static_cast<uint32_t>(entity) == excludeId)
			continue;

		// タグによるフィルタリング
		if (registry_.all_of<TagComponent>(entity)) {
			const auto tag = registry_.get<TagComponent>(entity).tag;
			if (tag == TagType::Enemy || tag == TagType::Bullet || tag == TagType::Player || tag == TagType::Projectile || tag == TagType::VFX)
				continue;
		}

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

		auto* model = renderer_->GetModel(modelHandle);
		if (!model)
			continue;

		float dist = 0.0f;
		Engine::Vector3 hitPoint;
		Engine::Matrix4x4 worldMat = GetWorldMatrix(static_cast<int>(entity));

		if (model->RayCast(rayPos, rayDir, worldMat, dist, hitPoint)) {
			if (dist < minDist) {
				minDist = dist;
				hitAny = true;
			}
		}
	}

	if (hitAny) {
		outDist = minDist;
		return true;
	}
	return false;
}

Engine::Matrix4x4 GameScene::GetWorldMatrix(int entityId) const { return GetWorldMatrixRecursive(static_cast<entt::entity>(entityId), 0); }

Engine::Matrix4x4 GameScene::GetWorldMatrixRecursive(entt::entity e, int depth) const {
	if (depth > 32)
		return Engine::Matrix4x4::Identity();

	auto it = matrixCache_.find(e);
	if (it != matrixCache_.end())
		return it->second;

	if (!registry_.valid(e) || !registry_.all_of<TransformComponent>(e))
		return Engine::Matrix4x4::Identity();
	const auto& tc = registry_.get<TransformComponent>(e);
	Engine::Matrix4x4 local = tc.ToMatrix();

	Engine::Matrix4x4 world = local;
	if (registry_.all_of<HierarchyComponent>(e)) {
		const auto& hc = registry_.get<HierarchyComponent>(e);
		if (hc.parentId != entt::null && registry_.valid(hc.parentId)) {
			world = Engine::Matrix4x4::Multiply(local, GetWorldMatrixRecursive(hc.parentId, depth + 1));
		}
	}
	matrixCache_[e] = world;
	return world;
}

void GameScene::Draw() {
	if (!renderer_)
		return;

	DirectX::XMFLOAT3 corePos = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 blobRadii = { 1.2f, 0.85f, 1.2f };
	DirectX::XMFLOAT3 inputForce = { 0.0f, 0.0f, 0.0f };
	bool isLiquidated = false;
	bool hasPlayerSlime = false;

	const auto& playersForCore = GetEntitiesByTag(TagType::Player);
	entt::entity playerEntity = entt::null;
	if (!playersForCore.empty() && registry_.valid(playersForCore[0])) {
		playerEntity = playersForCore[0];
		if (registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			corePos = { tc.translate.x, tc.translate.y, tc.translate.z };
			blobRadii = { tc.scale.x, tc.scale.y, tc.scale.z };
		}
		if (registry_.all_of<RigidbodyComponent>(playerEntity)) {
			auto& rb = registry_.get<RigidbodyComponent>(playerEntity);
			inputForce = rb.velocity; // 速度を流体への外力として適用
		}
		if (registry_.all_of<PlayerActionComponent>(playerEntity)) {
			isLiquidated = registry_.get<PlayerActionComponent>(playerEntity).state == PlayerActionState::Liquefy;
		}
	}

	// ★追加: エディター上で自由に配置・変更できる流体エミッターの処理 (プレイヤーの存在や非表示に依存しないよう外側に配置)
	{
		auto emitterView = registry_.view<TransformComponent, FluidEmitterComponent>();
		for (auto e : emitterView) {
			auto& tc = emitterView.get<TransformComponent>(e);
			auto& fec = emitterView.get<FluidEmitterComponent>(e);
			if (!fec.enabled || fec.emitCountPerFrame <= 0) continue;

			Engine::Vector3 pos = { tc.translate.x, tc.translate.y, tc.translate.z };
			Engine::Vector3 vel = { fec.velocity.x, fec.velocity.y, fec.velocity.z };
			Engine::Vector4 col = { fec.color.x, fec.color.y, fec.color.z, fec.color.w };

			// 毎フレームエミットを実行
			renderer_->EmitGPUFluid(pos, vel, col, fec.emitCountPerFrame, fec.fluidType);

			// スプラッシュ（水: fluidType >= 0.5f）の場合、プレイヤーが下（半径1.5m以内）にいればHPを回復する
			if (isPlaying_ && !isPaused_ && fec.fluidType >= 0.5f && registry_.valid(playerEntity)) {
				float dx = corePos.x - pos.x;
				float dz = corePos.z - pos.z;
				if (dx * dx + dz * dz < 1.5f * 1.5f) {
					if (registry_.all_of<HealthComponent>(playerEntity)) {
						auto& hc = registry_.get<HealthComponent>(playerEntity);
						static float healTimer = 0.0f;
						healTimer += ctx_.dt;
						if (healTimer > 0.05f) { // 0.05秒ごとに1回復 (1秒で20回復)
							healTimer = 0.0f;
							hc.hp = (std::min)(hc.hp + 1, hc.maxHp);
						}
					}
				}
			}
		}
	}

	if (registry_.valid(playerEntity) && registry_.all_of<MeshRendererComponent>(playerEntity)) {
		auto& mr = registry_.get<MeshRendererComponent>(playerEntity);
			
			// --- CPU側スライムメッシュ更新ロジック ---
			if (!slimeCpuLogic_.initialized) {
				auto* model = renderer_->GetModel(mr.modelHandle);
				if (model) {
					slimeCpuLogic_.baseVertices = model->GetData().vertices;
					// 法線の初期状態を保存
					slimeCpuLogic_.baseNormals = slimeCpuLogic_.baseVertices; 
					slimeCpuLogic_.indices = model->GetData().indices;
					slimeCpuLogic_.dynamicVerts = slimeCpuLogic_.baseVertices;
					
					// 動的メッシュを生成
					slimeCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(slimeCpuLogic_.baseVertices, slimeCpuLogic_.indices);
					
					// パーティクルの初期化
					slimeCpuLogic_.particles.resize(50);
					for (auto& p : slimeCpuLogic_.particles) {
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
				// ★フェーズ3: CPUスライム描画をオフにし、GPU流体スライムに移行
				mr.enabled = false;
				
				if (!gpuSlimeEmitted_) {
					// プレイヤー初期化時に1回だけ、大量のGPUパーティクルをコア位置に放出する
					Engine::Vector4 pColor = {0.4f, 0.8f, 0.1f, 1.0f}; // プレイヤースライムの色（濃い黄緑）
					renderer_->EmitGPUFluid({corePos.x, corePos.y, corePos.z}, {0, -2, 0}, pColor, 2000);
					gpuSlimeEmitted_ = true;
				}
				
				// アクション状態に応じて引力を変える（回避中は引力を弱めて散らばらせるなど）
				float attraction = isLiquidated ? 10.0f : 80.0f;
				
				// ★追加: 右クリック中は引力を完全にゼロにし、自由な液体として広がらせる
				if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
					attraction = 0.0f;
				}
				
				Engine::Vector3 targetCore = {corePos.x, corePos.y + 0.8f, corePos.z};
				auto& playerTc = registry_.get<TransformComponent>(playerEntity);
				Engine::Matrix4x4 mat = playerTc.GetTransform().ToMatrix();
				Engine::Vector3 forward = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
				Engine::Vector3 scaleVec = {blobRadii.x, blobRadii.y, blobRadii.z};
				renderer_->SetGPUFluidCore(targetCore, attraction, scaleVec, forward);

				// ★追加: 流体シミュレーション用のAABBコリジョンを収集してレンダラーに送る
				std::vector<Engine::Renderer::FluidAABB> fluidAABBs;
				auto aabbView = registry_.view<TransformComponent, BoxColliderComponent>();
				for (auto entity : aabbView) {
					auto& tc = aabbView.get<TransformComponent>(entity);
					auto& bc = aabbView.get<BoxColliderComponent>(entity);
					
					// トリガーは当たり判定にしない
					if (bc.isTrigger) continue;
					
					// ★追加: プレイヤー自身の BoxCollider はスライム(流体)と衝突させない（弾き飛ばされて挙動がおかしくなるのを防ぐ）
					if (registry_.all_of<TagComponent>(entity) && registry_.get<TagComponent>(entity).tag == TagType::Player) {
						continue;
					}
					
					Engine::Vector3 scale = {tc.scale.x * bc.size.x, tc.scale.y * bc.size.y, tc.scale.z * bc.size.z};
					// 簡易的なワールド座標（回転を無視したAABB）
					Engine::Vector3 pos = {
						tc.translate.x + bc.center.x * tc.scale.x, 
						tc.translate.y + bc.center.y * tc.scale.y, 
						tc.translate.z + bc.center.z * tc.scale.z
					};
					
					Engine::Renderer::FluidAABB aabb;
					aabb.min = {pos.x - scale.x * 0.5f, pos.y - scale.y * 0.5f, pos.z - scale.z * 0.5f};
					aabb.pad0 = 0.0f;
					aabb.max = {pos.x + scale.x * 0.5f, pos.y + scale.y * 0.5f, pos.z + scale.z * 0.5f};
					aabb.pad1 = 0.0f;
					fluidAABBs.push_back(aabb);
				}
				renderer_->SetFluidAABBs(fluidAABBs);
			}
			// ----------------------------------------
			
			// --- 星形弾用メッシュの更新 ---
			if (!projectileCpuLogic_.initialized && slimeCpuLogic_.initialized && !slimeCpuLogic_.baseVertices.empty()) {
				// プレイヤーの元の球体頂点をコピーして確実なベースメッシュとして使う
				projectileCpuLogic_.baseVertices = slimeCpuLogic_.baseVertices;
				projectileCpuLogic_.baseNormals = slimeCpuLogic_.baseNormals; 
				projectileCpuLogic_.indices = slimeCpuLogic_.indices;
				projectileCpuLogic_.dynamicVerts = projectileCpuLogic_.baseVertices;
				projectileCpuLogic_.dynamicMeshHandle = renderer_->CreateDynamicMesh(projectileCpuLogic_.baseVertices, projectileCpuLogic_.indices);
				projectileCpuLogic_.initialized = true;
			}

			if (projectileCpuLogic_.initialized) {
				projectileCpuLogic_.time += ctx_.dt;
				const float pNoiseScale = 4.0f;  // 細かくうねらせる
				const float pNoiseSpeed = 8.0f;  // 速くうねらせる
				
				for (size_t i = 0; i < projectileCpuLogic_.baseVertices.size(); ++i) {
					const auto& baseV = projectileCpuLogic_.baseVertices[i];
					const auto& baseN = projectileCpuLogic_.baseNormals[i];
					auto& dynV = projectileCpuLogic_.dynamicVerts[i];

					float bx = baseV.position.x;
					float by = baseV.position.y;
					float bz = baseV.position.z;

					// 球体を少し大きめにベーススケール
					float scaleUp = 1.8f;
					float sx = bx * scaleUp;
					float sy = by * scaleUp;
					float sz = bz * scaleUp;

					float noise = Noise3D(
						sx * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sy * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed,
						sz * pNoiseScale + projectileCpuLogic_.time * pNoiseSpeed
					);
					
					// ノイズを鋭くして星形の突起を作る
					float spike = std::abs(noise) * 1.5f;

					Engine::Vector3 v = {
						sx + baseN.normal.x * spike,
						sy + baseN.normal.y * spike,
						sz + baseN.normal.z * spike
					};

					dynV.position = { v.x, v.y, v.z, 1.0f };
					dynV.normal = baseN.normal; 
				}
				renderer_->UpdateDynamicMesh(projectileCpuLogic_.dynamicMeshHandle, projectileCpuLogic_.dynamicVerts);
			}
			// ----------------------------------------

			hasPlayerSlime = mr.shaderName == "Slime";
		}

	if (hasPlayerSlime) {
		if (isLiquidated && !wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Puddle, blobRadii);
		} else if (!isLiquidated && wasLiquidated_) {
			// Game::FluidSystem::GetInstance()->RequestParticleReset(Game::FluidResetShape::Blob, blobRadii);
		}
	}
	wasLiquidated_ = isLiquidated;

	// 弾エンティティに対して、星形メッシュとSlimeシェーダーを適用
	auto projView = registry_.view<MeshRendererComponent>();
	for (auto entity : projView) {
		if (registry_.all_of<HitboxComponent>(entity)) {
			auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.isProjectile && projectileCpuLogic_.initialized) {
				auto& pmr = registry_.get<MeshRendererComponent>(entity);
				pmr.modelHandle = projectileCpuLogic_.dynamicMeshHandle;
				if (registry_.all_of<NameComponent>(entity) && registry_.get<NameComponent>(entity).name == "PlayerProjectile") {
					pmr.shaderName = "SlimeNoFace";
				} else {
					pmr.shaderName = "SlimeNoFaceNoDepth";
				}
			}
		}
	}

	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	if (!isPlaying_) {
		DrawEditorGizmos();
	}
#endif

	// ★ 高速タグ検索を用いてプレイヤー位置を同期（O(N) -> O(1)）
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		playerEntity = players[0];
		if (registry_.valid(playerEntity) && registry_.all_of<TransformComponent>(playerEntity)) {
			auto& tc = registry_.get<TransformComponent>(playerEntity);
			renderer_->SetPlayerPos(Engine::Vector3{tc.translate.x, tc.translate.y, tc.translate.z});
		}
	}

	auto renderView = registry_.view<TransformComponent>();
	for (auto entity : renderView) {
		Engine::Vector4 color = {1, 1, 1, 1};
		if (registry_.all_of<ColorComponent>(entity)) {
			const auto& cc = registry_.get<ColorComponent>(entity);
			color = {cc.color.x, cc.color.y, cc.color.z, cc.color.w};
		}

		bool hasMeshRenderer = false;
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			const auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (mr.enabled && mr.modelHandle != 0) {
				hasMeshRenderer = true;
				bool hasAnim = false;
				std::vector<Engine::Matrix4x4> bonePalette;

				if (registry_.all_of<AnimatorComponent>(entity)) {
					const auto& anim = registry_.get<AnimatorComponent>(entity);
					if (anim.enabled && !anim.currentAnimation.empty()) {
						auto* m = renderer_->GetModel(mr.modelHandle);
						if (m) {
							const auto& data = m->GetData();
							const Engine::Animation* currAnim = nullptr;
							for (const auto& a : data.animations) {
								if (a.name == anim.currentAnimation) {
									currAnim = &a;
									break;
								}
							}
							if (currAnim) {
								bonePalette.resize(data.bones.size());
								for (auto& b : bonePalette)
									b = Engine::Matrix4x4::Identity();

								std::vector<std::pair<Engine::Vector3, Engine::Vector3>> debugLines;
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette, anim.drawSkeleton ? &debugLines : nullptr);
								hasAnim = true;

								if (anim.drawSkeleton) {
									Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
									DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&world));
									for (const auto& line : debugLines) {
										DirectX::XMVECTOR p1 = DirectX::XMVectorSet(line.first.x, line.first.y, line.first.z, 1.0f);
										DirectX::XMVECTOR p2 = DirectX::XMVectorSet(line.second.x, line.second.y, line.second.z, 1.0f);
										p1 = DirectX::XMVector3TransformCoord(p1, w);
										p2 = DirectX::XMVector3TransformCoord(p2, w);
										Engine::Vector3 wp1, wp2;
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
										DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp2), p2);
										renderer_->DrawLine3D(wp1, wp2, {0.0f, 1.0f, 0.0f, 1.0f}, true); // X-Ray true
									}
								}
							}
						}
					}
				}

				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				if (hasAnim) {
					renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
				} else {
					if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth") {
						// 半透明オブジェクトなので、不透明オブジェクトの背後にならないよう後で描画する
						continue;
					}
					if (mr.shaderName == "Toon" || mr.shaderName == "ToonSkinning" || mr.shaderName == "Hologram" || mr.shaderName == "EmissiveGlow" || mr.shaderName == "ForceField" ||
					    mr.shaderName == "Dissolve" || mr.shaderName == "Distortion" || mr.shaderName == "Reflection") {
						renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.reflectivity, mr.useCubemap);
					} else {
						renderer_->DrawMeshInstanced(
						    mr.modelHandle, mr.textureHandle, world, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w}, mr.shaderName, mr.extraTextureHandles);
					}
				}
			}
		}

		// 旧SceneObjectの互換用 (もし MeshRenderer コンポーネントがなく自身の modelHandle 等があった場合)
		// Registry化で基本的には MeshRendererComponent に統合するのが望ましいが、一旦残留
		/*
		if (!hasMeshRenderer && obj.modelHandle != 0) { ... }
		*/

		// ★追加: 川コンポーネントの描画 (メッシュはワールド座標で生成済みなのでIdentity変換)
		if (registry_.all_of<RiverComponent>(entity)) {
			const auto& rv = registry_.get<RiverComponent>(entity);
			if (rv.enabled && rv.meshHandle != 0) {
				auto tex = renderer_->LoadTexture2D(rv.texturePath);
				Engine::Transform identity;
				identity.translate = {0, 0, 0};
				identity.rotate = {0, 0, 0};
				identity.scale = {1, 1, 1};
				renderer_->DrawMesh(rv.meshHandle, tex, identity, {rv.flowSpeed, rv.uvScale, 0.0f, 0.0f}, "River");
			}
		}
	}

	// === 半透明オブジェクト（Slime等）の遅延描画 ===
	for (auto entity : renderView) {
		if (registry_.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry_.get<MeshRendererComponent>(entity);
			if (!mr.enabled) continue;
			if (mr.shaderName == "Slime" || mr.shaderName == "SlimeNoFace" || mr.shaderName == "SlimeNoFaceNoDepth") {
				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				Engine::Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
				if (registry_.all_of<ColorComponent>(entity)) {
					const auto& cc = registry_.get<ColorComponent>(entity);
					color = { cc.color.x, cc.color.y, cc.color.z, cc.color.w };
				}
				renderer_->DrawMesh(mr.modelHandle, mr.textureHandle, world,
					{ color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w },
					mr.shaderName, mr.reflectivity > 0 ? mr.reflectivity : 1.0f, true);
			}
		}
	}

#if defined(USE_IMGUI) && !defined(NDEBUG)
	DrawSelectionHighlight();
	DrawLightGizmos();
#endif
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});

	// プレイヤースライム: Screen-Space Fluid Rendering（参考: UE5 Niagara / 液状スライム）の軽量版Slimeシェーダーに置き換えたため無効化
	renderer_->SetCustomDrawJob(nullptr);

	for (auto& system : systems_) {
		system->Draw(registry_, ctx_);
	}
}

extern GizmoMode currentGizmoMode;
void GameScene::DrawUI() {
	if (!isPlaying_)
		return;
	for (auto& sys : systems_) {
		sys->DrawUI(registry_, ctx_);
	}
}

extern bool gizmoDragging;
extern int gizmoDragAxis;

void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	for (auto entity : selectedEntities_) {
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity))
			continue;

		auto& tc = registry_.get<TransformComponent>(entity);
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};

		Engine::Matrix4x4 mat = this->GetWorldMatrix(static_cast<int>(entity));
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

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

		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
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
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		if (registry_.all_of<BoxColliderComponent>(entity)) {
			const auto& bc = registry_.get<BoxColliderComponent>(entity);
			if (bc.enabled) {
				float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
				// ... Draw lines ...
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
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
			}
		}

		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			const auto& gmc = registry_.get<GpuMeshColliderComponent>(entity);
			if (gmc.enabled) {
				Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
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
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
			}
		}

		if (registry_.all_of<HitboxComponent>(entity)) {
			const auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.enabled) {
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
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		if (registry_.all_of<HurtboxComponent>(entity)) {
			const auto& hb = registry_.get<HurtboxComponent>(entity);
			if (hb.enabled) {
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
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) * DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		const float al = 2.0f, ar = 0.3f;
		int dAxis = (gizmoDragging && entity == selectedEntity_) ? gizmoDragAxis : -1;
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
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		if (currentGizmoMode == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (currentGizmoMode == GizmoMode::Rotate) {
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
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

void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		if (std::fabs(i) < 0.01f)
			continue;
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	auto dlView = registry_.view<DirectionalLightComponent, TransformComponent>();
	dlView.each([&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		if (!dl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});

	auto plView = registry_.view<PointLightComponent, TransformComponent>();
	plView.each([&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		if (!pl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
	});

	auto slView = registry_.view<SpotLightComponent, TransformComponent>();
	slView.each([&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		if (!sl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});
}

void GameScene::SetIsPlaying(bool play) {
	if (isPlaying_ == play)
		return;

	wasLiquidated_ = false;

	if (play) {
		// プレイ開始時: スクリプトの現在の設定（インスペクターでの変更）をコンポーネントに確実に反映 (Flush)
		auto scView = registry_.view<ScriptComponent>();
		for (auto entity : scView) {
			auto& sc = scView.get<ScriptComponent>(entity);
			for (auto& entry : sc.scripts) {
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
					char logBuf[1024];
					sprintf_s(logBuf, "[GameScene] Script instance NULL, skipping sync: %s (current param: %s)\n", entry.scriptPath.c_str(), entry.parameterData.c_str());
					OutputDebugStringA(logBuf);
				}
			}
		}

		// 各Systemのリセット（スクリプトの再初期化、インスタンスのクリアなど）を先に実行
		for (auto& sys : systems_) {
			sys->Reset(registry_);
		}

		sceneSnapshot_ = EditorUI::SaveToMemory(this);
		{
			char logBuf[128];
			sprintf_s(logBuf, "[GameScene] Saved snapshot for PLAY mode (size: %zu)\n", sceneSnapshot_.size());
			OutputDebugStringA(logBuf);
		}

		// ★追加: プレイ開始時のエディタカメラの状態を保存
		DirectX::XMFLOAT3 pos = camera_.Position();
		editorCameraPos_ = {pos.x, pos.y, pos.z};
		DirectX::XMFLOAT3 rot = camera_.Rotation();
		editorCameraRot_ = {rot.x, rot.y, rot.z};

		isPlaying_ = true;
		isPaused_ = false;
		ShowCursor(FALSE); // ★追加: カーソルを非表示
	} else {
		// プレイ停止時: Play ボタンを押した直前の状態 (`sceneSnapshot_`) に戻す
		// 選択状態のエンティティ名を一時保存
		std::vector<std::string> selectedNames;
		auto& reg = GetRegistry();
		for (auto entity : selectedEntities_) {
			if (reg.valid(entity) && reg.all_of<NameComponent>(entity)) {
				selectedNames.push_back(reg.get<NameComponent>(entity).name);
			}
		}

		isPlaying_ = false;
		gpuSlimeEmitted_ = false; // ★追加: プレイ終了時にGPUスライム放出フラグをリセット
		ShowCursor(TRUE); // ★追加: カーソルを表示

		// ★追加: プレイ終了時にエディタカメラの状態を復元
		camera_.SetPosition(editorCameraPos_.x, editorCameraPos_.y, editorCameraPos_.z);
		camera_.SetRotation(editorCameraRot_.x, editorCameraRot_.y, editorCameraRot_.z);
		
		if (!sceneSnapshot_.empty()) {
			OutputDebugStringA(("[GameScene] Restoring from memory snapshot (size: " + std::to_string(sceneSnapshot_.size()) + ")...\n").c_str());
			EditorUI::LoadFromMemory(this, sceneSnapshot_);

			// 保存しておいた名前を元に選択状態を復元
			selectedEntities_.clear();
			selectedEntity_ = entt::null;
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
			if (!initialSceneSnapshot_.empty()) {
				EditorUI::LoadFromMemory(this, initialSceneSnapshot_);
			}
		}
		// sceneSnapshot_ = ""; // これを消すと、再開時に残ってしまう可能性があるが、念のため残すか？
		// 一旦、毎回保存するようにするのでクリアしても良いはず
		sceneSnapshot_ = "";




		// ペンディングデータのクリア
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
	}
}

// =====================================================
// ★ 高速タグアクセス実装
// =====================================================

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(TagType tag) {
	static const std::vector<entt::entity> emptyResult;
	auto it = tagCache_.find(tag);
	if (it != tagCache_.end()) {
		return it->second;
	}
	return emptyResult;
}

void GameScene::SetTag(entt::entity entity, TagType tag) {
	if (!registry_.valid(entity)) {
		return;
	}
	auto& tc = registry_.get_or_emplace<TagComponent>(entity);
	tc.tag = tag;
	// 直接 SyncTag せず、遅延更新リストに追加
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagAdded(entt::registry& /*registry*/, entt::entity entity) {
	// 即座に同期せず、次フレーム等の適切なタイミングで同期
	pendingTagSync_.push_back(entity);
}

void GameScene::OnTagRemoved(entt::registry& /*registry*/, entt::entity entity) {
	// 即座に削除せず、遅延リストに追加して次フレーム開始時に削除を行う
	// これにより、イテレーション中のコンテナ変更による例外を防止する
	pendingTagRemoved_.push_back(entity);
}

void GameScene::OnScriptDestroyed(entt::registry& registry, entt::entity entity) {
	// スクリプトが破棄された時にC++スクリプトの OnDestroy を呼ぶ
	if (auto* sc = registry.try_get<ScriptComponent>(entity)) {
		for (auto& entry : sc->scripts) {
			if (entry.instance) {
				entry.instance->OnDestroy(entity, this);
			}
		}
	}
}

void GameScene::SyncTag(entt::entity entity) {
	if (!registry_.valid(entity) || !registry_.all_of<TagComponent>(entity)) {
		return;
	}

	// 新しいキャッシュに追加（削除は Update の開始時に一括して行われる前提）
	const TagType tag = registry_.get<TagComponent>(entity).tag;
	
	// 重複チェックを一件ずつ行うと遅いため、基本的には Update 側の全削除を信頼する
	tagCache_[tag].push_back(entity);
}

const std::vector<entt::entity>& GameScene::GetEntitiesByTag(const std::string& tag) {
	return GetEntitiesByTag(StringToTag(tag));
}

void GameScene::SetTag(entt::entity entity, const std::string& tagStr) {
	SetTag(entity, StringToTag(tagStr));
}

void GameScene::ClearScene() {
	// 1. 各システムのリセット（システム側の状態をクリア）
	for (auto& sys : systems_) {
		sys->Reset(registry_);
	}

	// 2. 予約バッファやキャッシュの完全クリア
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);
		pendingDestroys_.clear();
		pendingSpawns_.clear();
		pendingTagSync_.clear();
		pendingTagRemoved_.clear();
		tagCache_.clear();
		matrixCache_.clear();
	}

	// 3. レジストリのクリア
	// ★追加: 全クリア中は `OnDestroy` などのシグナル発火による二重破棄（イテレータ破壊）を防ぐため、一時的にシグナルを切断する
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().disconnect<&GameScene::OnScriptDestroyed>(this);

	registry_.clear();

	// シグナルを再接続
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<ScriptComponent>().connect<&GameScene::OnScriptDestroyed>(this);

	// 4. CPUスライム描画ロジックの非初期化
	slimeCpuLogic_.initialized = false;
	projectileCpuLogic_.initialized = false;
}

void GameScene::CreatePauseMenu() {
	float W = (float)Engine::WindowDX::kW;
	float H = (float)Engine::WindowDX::kH;

	// 背景 (半透明の黒)
	auto bg = CreateEntity("PauseBG");
	SetTag(bg, TagType::PauseUI);
	auto& bgRt = registry_.emplace<RectTransformComponent>(bg);
	bgRt.pos = { 0.0f, 0.0f };
	bgRt.size = { W, H };
	bgRt.anchor = { 0.5f, 0.5f };
	bgRt.pivot = { 0.5f, 0.5f };
	auto& bgImg = registry_.emplace<UIImageComponent>(bg);
	bgImg.color = { 0.0f, 0.0f, 0.0f, 0.5f };
	bgImg.layer = -1; 

	// タイトル
	auto title = CreateEntity("PauseTitle");
	SetTag(title, TagType::PauseUI);
	auto& tRt = registry_.emplace<RectTransformComponent>(title);
	tRt.pos = { 0.0f, -H * 0.2f };
	tRt.anchor = { 0.5f, 0.5f };
	tRt.pivot = { 0.5f, 0.5f };
	auto& tTxt = registry_.emplace<UITextComponent>(title);
	tTxt.text = "PAUSE";
	tTxt.fontSize = 64.0f;
	tTxt.color = { 1.0f, 1.0f, 1.0f, 1.0f };

	// 再開ボタン
	auto resume = CreateEntity("ResumeButton");
	SetTag(resume, TagType::PauseUI);
	auto& rRt = registry_.emplace<RectTransformComponent>(resume);
	rRt.pos = { 0.0f, 0.0f };
	rRt.size = { 300, 60 };
	rRt.anchor = { 0.5f, 0.5f };
	rRt.pivot = { 0.5f, 0.5f };
	auto& rImg = registry_.emplace<UIImageComponent>(resume);
	rImg.color = { 0.3f, 0.3f, 0.3f, 0.8f };
	auto& rBtn = registry_.emplace<UIButtonComponent>(resume);
	rBtn.normalColor = { 0.3f, 0.3f, 0.3f, 0.8f };
	rBtn.hoverColor = { 0.5f, 0.5f, 0.5f, 0.9f };
	rBtn.pressedColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	auto& rTxt = registry_.emplace<UITextComponent>(resume);
	rTxt.text = "Resume";
	rTxt.fontSize = 32.0f;
	rTxt.color = { 1.0f, 1.0f, 1.0f, 1.0f };

	// タイトルに戻るボタン
	auto back = CreateEntity("TitleButton");
	SetTag(back, TagType::PauseUI);
	auto& bRt = registry_.emplace<RectTransformComponent>(back);
	bRt.pos = { 0.0f, 80.0f };
	bRt.size = { 300, 60 };
	bRt.anchor = { 0.5f, 0.5f };
	bRt.pivot = { 0.5f, 0.5f };
	auto& bImg = registry_.emplace<UIImageComponent>(back);
	bImg.color = { 0.3f, 0.3f, 0.3f, 0.8f };
	auto& bBtn = registry_.emplace<UIButtonComponent>(back);
	bBtn.normalColor = { 0.3f, 0.3f, 0.3f, 0.8f };
	bBtn.hoverColor = { 0.5f, 0.5f, 0.5f, 0.9f };
	bBtn.pressedColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	auto& bTxt = registry_.emplace<UITextComponent>(back);
	bTxt.text = "Back to Title";
	bTxt.fontSize = 32.0f;
	bTxt.color = { 1.0f, 1.0f, 1.0f, 1.0f };
}

void GameScene::DestroyPauseMenu() {
	const auto& pauseUIs = GetEntitiesByTag(TagType::PauseUI);
	for (auto e : pauseUIs) {
		if (registry_.valid(e)) {
			DestroyObject(static_cast<uint32_t>(e));
		}
	}
}

} // namespace Game


// Dummy line to trigger rebuild



