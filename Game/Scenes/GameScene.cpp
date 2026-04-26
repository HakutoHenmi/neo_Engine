#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GameScene.h"
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/WeaponSystem.h" // ★追加

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // ★追加
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/PlayerActionSystem.h" // ★追加: プレイヤーアクション
#include "../Systems/CombatSystem.h"       // ★追加: 戦闘判定
#include "../Systems/EnemyAISystem.h"      // ★追加: 敵AI
#include "../Systems/BossActionSystem.h"    // ★追加: ボスAI
#include "../Systems/WaveSystem.h"         // ★追加: ウェーブ管理

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "imgui.h"
#include <Windows.h> // OutputDebugStringA
#include <algorithm>
#include <cmath>
#include <filesystem> // ★追加: Skybox DDS検索用

namespace Game {

GameScene::~GameScene() {
	// ★追加: 破棄時にシグナルを解除し、安全にレジストリをクリアする
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.clear();
}

void GameScene::Initialize(Engine::WindowDX* dx, const Engine::SceneParameters& params) {
	dx_ = dx;
	renderer_ = Engine::Renderer::GetInstance();
	eventSystem_.Clear(); // ★追加: イベントリスナーをクリア
	playTime_ = 0.0f;
	camera_.Initialize();
	// ★追加: 明示的にプロジェクションを設定 (1920x1080のアスペクト比)
	camera_.SetProjection(0.7854f, (float)Engine::WindowDX::kW / (float)Engine::WindowDX::kH, 0.1f, 1000.0f);
	camera_.SetPosition(0, 2, -5);
	camera_.SetRotation(0.2f, 0, 0);
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

		auto plane = registry_.create();
		registry_.emplace<NameComponent>(plane, "Plane");

		auto& mesh = registry_.emplace<MeshRendererComponent>(plane);
		mesh.modelHandle = renderer_->LoadObjMesh("Resources/Models/plane.obj");
		mesh.textureHandle = renderer_->LoadTexture2D("Resources/Textures/white1x1.png");
		mesh.modelPath = "Resources/Models/plane.obj";
		mesh.texturePath = "Resources/Textures/white1x1.png";

		registry_.emplace<TransformComponent>(plane, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{0, 0, 0}, DirectX::XMFLOAT3{20, 1, 20});

		// ★追加: 物理判定用にGpuMeshColliderを付与
		auto& gmc = registry_.emplace<GpuMeshColliderComponent>(plane);
		gmc.meshHandle = mesh.modelHandle;
		gmc.enabled = true;

	}

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
	systems_.push_back(std::make_unique<CleanupSystem>());

	// ★追加: 起動直後の状態を初期スナップショットとして保存
	initialSceneSnapshot_ = EditorUI::SaveToMemory(this);



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
	// リスナー登録
	registry_.on_construct<TagComponent>().disconnect<&GameScene::OnTagAdded>(this);
	registry_.on_construct<TagComponent>().connect<&GameScene::OnTagAdded>(this);
	registry_.on_destroy<TagComponent>().disconnect<&GameScene::OnTagRemoved>(this);
	registry_.on_destroy<TagComponent>().connect<&GameScene::OnTagRemoved>(this);

}

// =====================================================
// ★ Update: 各Systemに処理を委譲
// =====================================================
void GameScene::Update() {
	if (!renderer_) return;

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
		playTime_ += dt;
		// ★追加: Play中はマウスカーソルを画面中央に固定
		if (dx_ && dx_->GetHwnd()) {
			POINT center = { (LONG)Engine::WindowDX::kW / 2, (LONG)Engine::WindowDX::kH / 2 };
			ClientToScreen(dx_->GetHwnd(), &center);
			SetCursorPos(center.x, center.y);
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
	if (isPlaying_) {
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

	// パーティクルエディター
	particleEditor_.Update(dt);

	// ★ 全Systemを順に実行
	for (auto& system : systems_) {
		// リザルト遷移中などはシステムを動かさない (エンティティが削除されている可能性があるため)
		if (!isPlaying_)
			break;
		system->Update(registry_, ctx_);
	}

	// ★ 追加: 停止中のみデバッグカメラを有効化
	if (!isPlaying_) {
		camera_.Update(*Engine::Input::GetInstance());
	}
	camera_.Tick(dt);

	// ★ ペンディングオブジェクト（弾など）をflushし、破棄要求を処理
	{
		std::lock_guard<std::mutex> lock(spawnMutex_);

		if (!pendingSpawns_.storage<entt::entity>().empty()) {
			// 一旦、pendingSpawns_ をダミーとして運用するか、直接 `registry_.create()` するのでここは実質空になる
			pendingSpawns_.clear();
		}

		if (!pendingDestroys_.empty()) {
			for (auto id : pendingDestroys_) {
				if (registry_.valid(id)) {
					registry_.destroy(id);
				}
			}
			pendingDestroys_.clear();
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



	renderer_->SetCamera(camera_);
#ifdef USE_IMGUI
	if (!isPlaying_) {
		DrawEditorGizmos();
	}
#endif

	// ★ 高速タグ検索を用いてプレイヤー位置を同期（O(N) -> O(1)）
	const auto& players = GetEntitiesByTag(TagType::Player);
	if (!players.empty()) {
		entt::entity playerEntity = players[0];
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
								m->UpdateSkeleton(data.rootNode, Engine::Matrix4x4::Identity(), *currAnim, anim.time, bonePalette);
								hasAnim = true;
							}
						}
					}
				}

				Engine::Matrix4x4 world = this->GetWorldMatrix(static_cast<int>(entity));
				if (hasAnim) {
					renderer_->DrawSkinnedMesh(mr.modelHandle, mr.textureHandle, world, bonePalette, {color.x * mr.color.x, color.y * mr.color.y, color.z * mr.color.z, color.w * mr.color.w});
				} else {
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
#ifdef USE_IMGUI
	DrawSelectionHighlight();
	DrawLightGizmos();
#endif
	auto peView = registry_.view<ParticleEmitterComponent>();
	peView.each([&](auto, ParticleEmitterComponent& pe) {
		if (pe.enabled) {
			pe.emitter.Draw(camera_);
		}
	});

	// ★ 各Systemの描画処理を呼び出す（UISystem等）
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

		isPlaying_ = true;
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
		ShowCursor(TRUE); // ★追加: カーソルを表示
		
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

} // namespace Game
