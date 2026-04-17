#include "PhaseSystemScript.h"
#include "../../Engine/PathUtils.h"
#include "Editor/EditorUI.h"
#include "ObjectTypes.h"
#include "PhaseTransition.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cfloat>
#include <cmath>
#include <fstream>
#include <string.h>
#include <vector>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include <iostream>

// Button UI
#include "InstallationButton.h"

namespace Game {

namespace {
float SnapTo2x2Grid(float value) { return std::floor(value / 2.0f) * 2.0f; }

std::vector<Engine::Vector3> BuildPipePathPoints(const Engine::Vector3& start, const Engine::Vector3& end) {
	std::vector<Engine::Vector3> points;

	const int x0 = static_cast<int>(SnapTo2x2Grid(start.x));
	const int z0 = static_cast<int>(SnapTo2x2Grid(start.z));
	const int x1 = static_cast<int>(SnapTo2x2Grid(end.x));
	const int z1 = static_cast<int>(SnapTo2x2Grid(end.z));
	constexpr int kStep = 2;

	const float y = end.y;
	points.push_back({static_cast<float>(x0), y, static_cast<float>(z0)});

	int x = x0;
	int z = z0;
   const int stepX = (x1 > x0) ? kStep : -kStep;
	const int stepZ = (z1 > z0) ? kStep : -kStep;
	const int totalX = std::abs((x1 - x0) / kStep);
	const int totalZ = std::abs((z1 - z0) / kStep);

	int movedX = 0;
	int movedZ = 0;
	while (movedX < totalX || movedZ < totalZ) {
		const bool canMoveX = movedX < totalX;
		const bool canMoveZ = movedZ < totalZ;

		bool moveX = false;
		if (canMoveX && canMoveZ) {
			const int nextXScore = (movedX + 1) * totalZ;
			const int nextZScore = (movedZ + 1) * totalX;
			moveX = nextXScore <= nextZScore;
		} else {
			moveX = canMoveX;
		}

		if (moveX) {
			x += stepX;
			++movedX;
		} else {
			z += stepZ;
			++movedZ;
		}

		points.push_back({static_cast<float>(x), y, static_cast<float>(z)});
	}

	return points;
}
} // namespace

void PhaseSystemScript::Start(entt::entity entity, GameScene* scene) {
	(void)entity;
	(void)scene;
	isPhase_ = PreparationPhase;
	NextPhase_ = PreparationPhase;
	preIsPhase_ = PreparationPhase;
	isPhaseTransitioning_ = false;
	isFadeFinished_ = false;
	hasPipeStartPoint_ = false;

	// スキルツリーの初期化
	if (auto* renderer = Engine::Renderer::GetInstance()) {
		skillTree_.Init(renderer);
		skillTree_.LoadFromJson("Resources/Scenes/skills.json", renderer);
	}
}

void PhaseSystemScript::Update(entt::entity entity, GameScene* scene, float dt) {
	(void)entity;
	(void)scene;
	(void)dt;
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;

	// スクリプト動作確認用の白い線 (常に表示)
	auto* renderer = scene->GetRenderer();
	if (renderer) {
		renderer->DrawLine3D({0, 20, 0}, {5, 20, 0}, {1, 1, 1, 1}, true);
		if (isPhase_ == PreparationPhase)
			renderer->DrawLine3D({0, 21, 0}, {5, 21, 0}, {0, 1, 0, 1}, true);
		if (isPlacementMode_)
			renderer->DrawLine3D({0, 22, 0}, {5, 22, 0}, {0, 0, 1, 1}, true);
	}

	// ★入力処理: キーボードとUI両方からの入力を受け付ける
	bool key1 = input->Trigger(DIK_1) || (GetAsyncKeyState('1') & 0x8001);
	bool key2 = input->Trigger(DIK_2) || (GetAsyncKeyState('2') & 0x8001);
	bool key3 = input->Trigger(DIK_3) || (GetAsyncKeyState('3') & 0x8001);
	bool keyP = input->Trigger(DIK_P) || (GetAsyncKeyState('P') & 0x8001);
	bool keySpace = input->Trigger(DIK_SPACE) || (GetAsyncKeyState(VK_SPACE) & 0x8001);

	// ★ スキルツリーの入力処理 (準備フェーズ中のみ)
	bool keyN = input->Trigger(DIK_N) || (GetAsyncKeyState('N') & 0x8001);

	// 外部(EnemySpawnerScript など)からのフェーズ変更要求を反映
	if (!isPhaseTransitioning_ && isPhase_ != Transition && NextPhase_ != isPhase_) {
		RequestPhaseChange(NextPhase_);
	}

	if (isPhase_ == PreparationPhase) {
		// Nキーでスキルツリーの開閉
		if (keyN && !preKeyN_) {
			skillTree_.Toggle();
		}

		// スキルツリーが開いている間はスキルツリーの更新のみ
		if (skillTree_.IsOpen()) {
			float mx = 0, my = 0;
			float tW = (float)Engine::WindowDX::kW;
			float tH = (float)Engine::WindowDX::kH;

#if defined(USE_IMGUI) && !defined(NDEBUG)
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 gameMin = EditorUI::GetGameImageMin();
			ImVec2 gameMax = EditorUI::GetGameImageMax();
			float viewW = gameMax.x - gameMin.x;
			float viewH = gameMax.y - gameMin.y;
			if (viewW > 0 && viewH > 0) {
				mx = (mousePos.x - gameMin.x) * (tW / viewW);
				my = (mousePos.y - gameMin.y) * (tH / viewH);
			}
#else
			input->GetMousePos(mx, my);
#endif
			skillTree_.Update(renderer, tW, tH, mx, my);
			preKeyN_ = keyN;
			return; // 設置モードの入力を抑制
		}

		// 設置モードへの切り替え

		if (key1 || InstallationButton::IsButtonPressed(InstallationButton::Tank)) {
			selectedObjPath_ = "Resources/Prefabs/BulletTank.prefab";
			isPlacementMode_ = true;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
		}

		if (key2 || InstallationButton::IsButtonPressed(InstallationButton::Pipe)) {
			selectedObjPath_ = "Resources/Prefabs/Pipe.prefab";
			isPipeSet_ = true;
			isPlacementMode_ = true;
			hasPipeStartPoint_ = false;
		}

		if (key3 || InstallationButton::IsButtonPressed(InstallationButton::Cannon)) {
			selectedObjPath_ = "Resources/Prefabs/Canon.prefab";
			isPlacementMode_ = true;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
		}
		if (input->IsMouseTrigger(1) && isPlacementMode_) {
			isPlacementMode_ = false;
			isPipeSet_ = false;
			hasPipeStartPoint_ = false;
		}

		Installation(scene, selectedObjPath_);

		if (keySpace) {
			RequestPhaseChange(BattlePhase);
			isPlacementMode_ = false;
			hasPipeStartPoint_ = false;
			skillTree_.Close(); // フェーズ移行時にスキルツリーを閉じる
		}

	} else if (isPhase_ == BattlePhase) {
		if (keyP) {
			RequestPhaseChange(PreparationPhase);
		}
		isPlacementMode_ = false;
		hasPipeStartPoint_ = false;
	} else {
		isPlacementMode_ = false;
		hasPipeStartPoint_ = false;
	}

	UpdatePhaseTransition();

	// フェーズが切り替わった瞬間の検知
	if (isPhase_ != preIsPhase_) {
		auto& nav = scene->GetNavigationManager();

		if (isPhase_ == BattlePhase) {
			// 準備から戦闘に切り替わった瞬間
			// 設置物を反映するためにコストマップを更新
			nav.UpdateCostMap(scene);

			// 敵が目指すコアをゴールの位置としてフローフィールドを計算
			auto core = scene->FindObjectByName("Core");
			if (scene->GetRegistry().valid(core)) {
				auto& tc = scene->GetRegistry().get<TransformComponent>(core);
				nav.GenerateFlowField(tc.translate.x, tc.translate.z);
			}

			// 敵のスポーン地点の生成
			currentPhase_++;
			std::string enemyPrefabPath = "Resources/Prefabs/EnemySpawner" + std::to_string(currentPhase_) + ".prefab";
			std::vector<entt::entity> spawnedEnemies = EditorUI::LoadPrefab(scene, enemyPrefabPath);

			auto& registry = scene->GetRegistry();
			for (auto spawnedEntity : spawnedEnemies) {
				if (registry.all_of<TransformComponent>(spawnedEntity)) {
					auto& tc = registry.get<TransformComponent>(spawnedEntity);
					if (!registry.all_of<HierarchyComponent>(spawnedEntity) || registry.get<HierarchyComponent>(spawnedEntity).parentId == entt::null) {
						tc.translate = {-50.0f, 20.0f, 50.0f};
					}
				}
			}
		}

		// 状態を同期
		preIsPhase_ = isPhase_;
	}

	preKeyN_ = keyN;
}

void PhaseSystemScript::RequestPhaseChange(PhaseState nextPhase) {
	if (isPhase_ == Transition || isPhaseTransitioning_)
		return;
	if (isPhase_ == nextPhase)
		return;

	NextPhase_ = nextPhase;
	isPhase_ = Transition;
	isPhaseTransitioning_ = true;
	isFadeFinished_ = false;

	if (PhaseTransition::IsAvailable()) {
		PhaseTransition::RequestFade();
	}
}

void PhaseSystemScript::UpdatePhaseTransition() {
	if (!isPhaseTransitioning_)
		return;

	if (PhaseTransition::IsAvailable()) {
		isFadeFinished_ = PhaseTransition::ConsumeSwitchPoint();
	} else {
		isFadeFinished_ = true;
	}

	if (isFadeFinished_) {
		isPhase_ = NextPhase_;
		isPhaseTransitioning_ = false;
		isFadeFinished_ = false;
	}
}

void PhaseSystemScript::Installation(GameScene* scene, const std::string& objPath) {
	if (!isPlacementMode_)
		return;

	auto* input = Engine::Input::GetInstance();
	if (!input)
		return;
	Engine::Vector3 hitPoint{};
	if (!TryGetTerrainHitPoint(scene, hitPoint))
		return;

	Engine::Vector3 snappedHitPoint = hitPoint;
	snappedHitPoint.x = std::floor(snappedHitPoint.x);
	snappedHitPoint.z = std::floor(snappedHitPoint.z);

	if (isPipeSet_) {
		snappedHitPoint.x = SnapTo2x2Grid(snappedHitPoint.x);
		snappedHitPoint.z = SnapTo2x2Grid(snappedHitPoint.z);

		if (!hasPipeStartPoint_) {
			const bool canPlaceStart = !IsPlacementBlocked(scene, snappedHitPoint);
			DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlaceStart);

			if (input->IsMouseTrigger(0) && canPlaceStart) {
				pipeStartX_ = snappedHitPoint.x;
				pipeStartY_ = snappedHitPoint.y;
				pipeStartZ_ = snappedHitPoint.z;
				hasPipeStartPoint_ = true;
			}
			return;
		}

		Engine::Vector3 startPoint{pipeStartX_, pipeStartY_, pipeStartZ_};
		auto pathPoints = BuildPipePathPoints(startPoint, snappedHitPoint);
		bool canPlaceAll = !pathPoints.empty();
		for (const auto& p : pathPoints) {
			const bool canPlacePoint = !IsPlacementBlocked(scene, p);
			DrawPlacementPreview(scene, p, objPath, canPlacePoint);
			if (!canPlacePoint) {
				canPlaceAll = false;
			}
		}

		if (input->IsMouseTrigger(0)) {
			if (canPlaceAll) {
				for (const auto& p : pathPoints) {
					SpawnPlacedObject(scene, p, objPath);
				}
				isPlacementMode_ = false;
				isPipeSet_ = false;
			}
			hasPipeStartPoint_ = false;
		}
		return;
	}

	const bool canPlace = !IsPlacementBlocked(scene, snappedHitPoint);

	DrawPlacementPreview(scene, snappedHitPoint, objPath, canPlace);

	if (input->IsMouseTrigger(0) && canPlace) {
		SpawnPlacedObject(scene, snappedHitPoint, objPath);
		isPlacementMode_ = false;
	}
}

bool PhaseSystemScript::TryGetTerrainHitPoint(GameScene* scene, Engine::Vector3& outHitPoint) const {
	float localX = 0, localY = 0;
	float tW = 0, tH = 0;

#if defined(USE_IMGUI) && !defined(NDEBUG)
	ImVec2 mousePos = ImGui::GetMousePos();
	ImVec2 gameMin = EditorUI::GetGameImageMin();
	ImVec2 gameMax = EditorUI::GetGameImageMax();
	tW = gameMax.x - gameMin.x;
	tH = gameMax.y - gameMin.y;
	if (tW <= 0.0f || tH <= 0.0f)
		return false;

	localX = mousePos.x - gameMin.x;
	localY = mousePos.y - gameMin.y;
	bool insideImage = (localX >= 0.0f && localY >= 0.0f && localX <= tW && localY <= tH);
	if (!insideImage)
		return false;
#else
	auto* input = Engine::Input::GetInstance();
	if (!input)
		return false;
	input->GetMousePos(localX, localY);
	tW = (float)Engine::WindowDX::kW;
	tH = (float)Engine::WindowDX::kH;
#endif

	auto& camera = scene->GetCamera();
	DirectX::XMMATRIX view = camera.View();
	DirectX::XMMATRIX proj = camera.Proj();

	DirectX::XMVECTOR rayOrig, rayDir;
	EditorUI::ScreenToWorldRay(localX, localY, tW, tH, view, proj, rayOrig, rayDir);

	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return false;

	float bestDist = FLT_MAX;
	bool hitTerrain = false;

	auto& registry = scene->GetRegistry();
	registry.view<NameComponent, TransformComponent>().each([&](entt::entity entity, const NameComponent& nc, const TransformComponent& tc) {
		bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
		                 (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
		if (!isTerrain)
			return;

		Engine::Model* model = nullptr;
		// GpuMeshCollider か MeshRenderer からモデルを取得
		if (registry.all_of<GpuMeshColliderComponent>(entity)) {
			auto& gmc = registry.get<GpuMeshColliderComponent>(entity);
			if (gmc.meshHandle != 0) {
				model = renderer->GetModel(gmc.meshHandle);
			}
		}

		if (!model && registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			if (mr.modelHandle != 0) {
				model = renderer->GetModel(mr.modelHandle);
			}
		}

		if (!model)
			return;

		float d;
		Engine::Vector3 hp;
		if (model->RayCast(rayOrig, rayDir, tc.ToMatrix(), d, hp) && d < bestDist) {
			bestDist = d;
			outHitPoint = hp;
			hitTerrain = true;
		}
	});

	// --- フォールバック: 仮想的な y=0 平面との交差判定 ---
	if (!hitTerrain) {
		DirectX::XMFLOAT3 orig, dir;
		DirectX::XMStoreFloat3(&orig, rayOrig);
		DirectX::XMStoreFloat3(&dir, rayDir);

		if (std::abs(dir.y) > 0.0001f) {
			float t = -orig.y / dir.y;
			if (t > 0) {
				outHitPoint = {orig.x + dir.x * t, 0.0f, orig.z + dir.z * t};
				hitTerrain = true;
			}
		}
	}

	return hitTerrain;
}

void PhaseSystemScript::DrawPlacementPreview(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath, bool canPlace) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	std::string previewModelPath = objPath;
	std::string previewTexturePath = "Resources/Textures/white1x1.png";
	if (IsPrefabPath(objPath)) {
		ExtractPrefabRenderPaths(objPath, previewModelPath, previewTexturePath);
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != previewModelPath) {
		previewModelHandle_ = renderer->LoadObjMesh(previewModelPath);
		previewObjPath_ = previewModelPath;
		previewTextureHandle_ = 0;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D(previewTexturePath);
	}

	Engine::Transform tr;
	tr.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tr.scale = {1.0f, 1.0f, 1.0f};
	const Engine::Vector4 previewColor = canPlace ? Engine::Vector4{0.6f, 1.0f, 0.6f, 0.6f} : Engine::Vector4{1.0f, 0.3f, 0.3f, 0.6f};
	renderer->DrawMesh(previewModelHandle_, previewTextureHandle_, tr, previewColor, "Toon");
}

bool PhaseSystemScript::IsPrefabPath(const std::string& path) const {
	if (path.size() < 7)
		return false;
	return path.compare(path.size() - 7, 7, ".prefab") == 0;
}

bool PhaseSystemScript::ExtractPrefabRenderPaths(const std::string& prefabPath, std::string& outModelPath, std::string& outTexturePath) const {
	std::string absPath = EditorUI::GetUnifiedProjectPath(prefabPath);
	// ★修正: UTF-8パスをFromUTF8経由でワイドパスに変換してオープン
	std::ifstream f(Engine::PathUtils::FromUTF8(absPath));
	if (!f.is_open()) {
		f.open(Engine::PathUtils::FromUTF8(prefabPath));
		if (!f.is_open())
			return false;
	}

	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	f.close();

	auto extractValue = [&](const char* key, std::string& outValue) {
		size_t keyPos = content.find(key);
		if (keyPos == std::string::npos)
			return;
		size_t colonPos = content.find(':', keyPos);
		if (colonPos == std::string::npos)
			return;
		size_t firstQuote = content.find('"', colonPos);
		if (firstQuote == std::string::npos)
			return;
		size_t secondQuote = content.find('"', firstQuote + 1);
		if (secondQuote == std::string::npos)
			return;
		outValue = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
	};

	extractValue("\"modelPath\"", outModelPath);
	extractValue("\"texturePath\"", outTexturePath);

	return !outModelPath.empty();
}

bool PhaseSystemScript::IsPlacementBlocked(GameScene* scene, const Engine::Vector3& hitPoint) const {
	constexpr float kBlockHalfExtent = 2.0f; // 2x2 square

	auto& registry = scene->GetRegistry();
	auto view = registry.view<TransformComponent>();
	for (auto entity : view) {
		// MeshRenderer, BoxCollider, GpuMeshCollider のいずれも持たないエンティティ（不可視のシステムオブジェクトなど）は無視する
		if (!registry.any_of<MeshRendererComponent, BoxColliderComponent, GpuMeshColliderComponent>(entity)) {
			continue;
		}

		if (registry.all_of<NameComponent>(entity)) {
			const auto& nc = registry.get<NameComponent>(entity);
			const bool isTerrain = (nc.name.find("Terrain") != std::string::npos) || (nc.name.find("Floor") != std::string::npos) || (nc.name.find("Ground") != std::string::npos) ||
			                       (nc.name.find("Stage") != std::string::npos) || (nc.name.find("Plane") != std::string::npos);
			if (isTerrain)
				continue;
		}

		const auto& tc = view.get<TransformComponent>(entity);
		const float dx = tc.translate.x - hitPoint.x;
		const float dz = tc.translate.z - hitPoint.z;
		if (std::abs(dx) < kBlockHalfExtent && std::abs(dz) < kBlockHalfExtent) {
			return true;
		}
	}

	return false;
}

void PhaseSystemScript::SpawnPlacedObject(GameScene* scene, const Engine::Vector3& hitPoint, const std::string& objPath) {
	auto* renderer = scene->GetRenderer();
	if (!renderer)
		return;

	auto& registry = scene->GetRegistry();

	if (IsPrefabPath(objPath)) {
		EditorUI::Log("Spawning prefab: " + objPath);
		std::vector<entt::entity> createdEntities = EditorUI::LoadPrefab(scene, objPath);

		if (createdEntities.empty()) {
			EditorUI::LogError("SpawnPlacedObject: LoadPrefab returned 0 entities for " + objPath);
			return;
		}

		// 新しく追加されたエンティティの座標をセット
		int movedCount = 0;
		for (auto entity : createdEntities) {
			if (registry.all_of<TransformComponent>(entity)) {
				auto& tc = registry.get<TransformComponent>(entity);
				// 親がいない（ルート）のエンティティのみ座標を更新
				if (!registry.all_of<HierarchyComponent>(entity) || registry.get<HierarchyComponent>(entity).parentId == entt::null) {
					tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
					movedCount++;
				}
			}
		}
		EditorUI::Log("Prefab spawned and positioned. Root entities moved: " + std::to_string(movedCount));
		return;
	}

	if (previewModelHandle_ == 0 || previewObjPath_ != objPath) {
		previewModelHandle_ = renderer->LoadObjMesh(objPath);
		previewObjPath_ = objPath;
	}
	if (previewTextureHandle_ == 0) {
		previewTextureHandle_ = renderer->LoadTexture2D("Resources/Textures/white1x1.png");
	}

	entt::entity newEntity = scene->CreateEntity((objPath.find("cylinder") != std::string::npos || objPath.find("Cylinder") != std::string::npos) ? "PlacedCylinder" : "PlacedCube");

	auto& tc = registry.get<TransformComponent>(newEntity);
	tc.translate = {hitPoint.x, hitPoint.y + 0.5f, hitPoint.z};
	tc.scale = {1.0f, 1.0f, 1.0f};

	auto& mr = registry.emplace<MeshRendererComponent>(newEntity);
	mr.modelHandle = previewModelHandle_;
	mr.textureHandle = previewTextureHandle_;
	mr.modelPath = objPath;
	mr.texturePath = "Resources/Textures/white1x1.png";
	mr.shaderName = "Toon";
}

void PhaseSystemScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

REGISTER_SCRIPT(PhaseSystemScript);

} // namespace Game
