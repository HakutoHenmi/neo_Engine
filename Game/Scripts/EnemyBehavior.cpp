#include "EnemyBehavior.h"
#include "../../Engine/ThirdParty/nlohmann/json.hpp"
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"

namespace Game {

static bool HasTag(entt::registry& registry, entt::entity entity, TagType tagName) {
	if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity)) return false;
	return registry.get<TagComponent>(entity).tag == tagName;
}

void EnemyBehavior::Start(entt::entity entity, GameScene* scene) {
	ownerId_ = static_cast<uint32_t>(entity);
	pCurrentScene_ = scene;
	auto& registry = scene->GetRegistry();
	auto& tc = registry.get<TransformComponent>(entity);

	// ★修正: 物理演算を有効化し、ノックバックを受け入れ可能にする
	if (registry.all_of<RigidbodyComponent>(entity)) {
		registry.get<RigidbodyComponent>(entity).isKinematic = false;
	}

	// 出現時に一度ターゲットを検索
	SearchTarget(entity, scene);

	// 出現時に地面の高さを即座に計算（初動の埋まり防止）
	float h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
	if (h > -9999.0f) {
		groundHeight_ = h;
	} else {
		groundHeight_ = tc.translate.y - 1.0f; // 地面が見つからない場合は現在の位置を基準にする
	}

	// Flyタイプの場合は重力を無効化し、初期高度を設定
	if (type_ == Fly) {
		if (registry.all_of<RigidbodyComponent>(entity)) {
			auto& rb = registry.get<RigidbodyComponent>(entity);
			rb.useGravity = false;
			rb.velocity = {0, 0, 0};
		}

		// スポナーの高さではなく、即座に正しい浮遊高度へ移動
		float baseHeight = 9.0f;
		tc.translate.y = groundHeight_ + baseHeight;
	} else {
		// Walkタイプも埋まり防止のためにオフセットを乗せる
		tc.translate.y = groundHeight_ + 1.0f;
	}
}

void EnemyBehavior::Update(entt::entity entity, GameScene* scene, float dt) {
	//if (!scene || !scene->GetRegistry().valid(entity)) return;
	//auto& registry = scene->GetRegistry();
	//if (!registry.all_of<TransformComponent>(entity)) return;

	//ownerId_ = static_cast<uint32_t>(entity);
	//pCurrentScene_ = scene;

	//if (registry.all_of<HealthComponent>(entity)) {
	//	auto& hc = registry.get<HealthComponent>(entity);
	//	if (hc.isDead) return;
	//	if (hc.hitStopTimer > 0.0f) return; // ヒットストップ中は動きを止める
	//}

	//// ターゲットが実在するか確認
	//bool targetExists = false;
	//if (targetId_ != 0) {
	//	entt::entity targetEntity = static_cast<entt::entity>(targetId_);
	//	if (registry.valid(targetEntity)) {
	//		targetExists = true;
	//	}
	//}
	//if (!targetExists) {
	//	targetId_ = 0;
	//	SearchTarget(entity, scene);
	//}

	//// ヒット中（無敵時間中）はスキャンと移動を停止
	//bool isHit = false;
	//if (registry.all_of<HealthComponent>(entity)) {
	//	if (registry.get<HealthComponent>(entity).invincibleTime > 0.0f) isHit = true;
	//}

	//if (!isHit) {
	//	scanTimer_ += dt;
	//	if (scanTimer_ > 0.2f) {
	//		scanTimer_ = 0.0f;
	//	}

	//	Move(entity, scene, dt);
	//}

	Move(entity, scene, dt);
}

void EnemyBehavior::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {
	// 終了時のクリーンアップなどを記述
}

void EnemyBehavior::OnEditorUI() {
#if defined(USE_IMGUI) && !defined(NDEBUG)
	// 敵の移動タイプ(地面や空中)
	int typeNum = static_cast<int>(type_);
	const char* types[] = {"Walk", "Fly"};
	if (ImGui::Combo("Enemy Type", &typeNum, types, IM_ARRAYSIZE(types))) {
		type_ = static_cast<MoveType>(typeNum);
	}

	// 追うオブジェクトのタグを設定
	int targetNum = static_cast<int>(targetType_);
	const char* targetTypes[] = {"Player", "Core", "Defender"};
	if (ImGui::Combo("Target", &targetNum, targetTypes, IM_ARRAYSIZE(targetTypes))) {
		targetType_ = static_cast<TargetType>(targetNum);

		// タグ名を更新(シーンを動かしたときにすぐに検索できるように)
		if (targetType_ == Player) {
			targetName_ = "Player";
		} else if (targetType_ == Core) {
			targetName_ = "Core";
		} else if (targetType_ == Defender) {
			targetName_ = "Defender";
		}

		// 追尾するタグが変わったら新たに検索
		if (ownerId_ != 0 && pCurrentScene_) {
			entt::entity ownerEntity = static_cast<entt::entity>(ownerId_);
			if (pCurrentScene_->GetRegistry().valid(ownerEntity)) {
				SearchTarget(ownerEntity, pCurrentScene_);
			}
		}
	}

	// 複数存在し得るオブジェクトに対して優先順位を選べるように
	if (targetType_ >= Defender) {
		int priorityNum = static_cast<int>(priority_);
		const char* priorities[] = {"Near", "Far"};
		if (ImGui::Combo("Priority", &priorityNum, priorities, IM_ARRAYSIZE(priorities))) {
			priority_ = static_cast<TargetPriority>(priorityNum);
		}

		ImGui::Checkbox("Show Debug Grid", &showDebugGrid_);
	}
#endif
}

void EnemyBehavior::SearchTarget(entt::entity /*entity*/, GameScene* /*scene*/) {
	//if (scene == nullptr) {
	//	return;
	//}

	//entt::entity bestTarget = entt::null;
	//float bestDistance = (priority_ == TargetPriority::Near) ? FLT_MAX : -1.0f;

	//auto view = scene->GetRegistry().view<TagComponent, TransformComponent>();
	//auto& myTc = scene->GetRegistry().get<TransformComponent>(entity);

	//for (auto e : view) {
	//	if (view.get<TagComponent>(e).tag == targetName_) {
	//		// 距離を計算
	//		auto& targetTc = view.get<TransformComponent>(e);
	//		float dx = targetTc.translate.x - myTc.translate.x;
	//		float dz = targetTc.translate.z - myTc.translate.z;
	//		float distSq = dx * dx + dz * dz;	// 軽量化のために平方根は取らない

	//		// priorityごとの対応
	//		if (priority_ == TargetPriority::Near) {
	//			if (distSq < bestDistance) {
	//				bestDistance = distSq;
	//				bestTarget = e;
	//			}
	//		} else { // Far
	//			if (distSq > bestDistance) {
	//				bestDistance = distSq;
	//				bestTarget = e;
	//			}
	//		}
	//	}
	//}
	//targetId_ = bestTarget != entt::null ? static_cast<uint32_t>(bestTarget) : 0;
}

void EnemyBehavior::Move(entt::entity entity, GameScene* scene, float /*dt*/) {
	auto& registry = scene->GetRegistry();
	auto& tc = registry.get<TransformComponent>(entity);

	// 1. NavigationManager を取得
	auto& nav = scene->GetNavigationManager();

	float dirX = 0.0f;
	float dirZ = 0.0f;

	// 2. 自分の足元の「進むべき方向」をマネージャーに聞く
	nav.GetDirection(tc.translate.x, tc.translate.z, dirX, dirZ);

	// 3. 物理コンポーネントがあるかチェック
	if (registry.all_of<RigidbodyComponent>(entity)) {
		auto& rb = registry.get<RigidbodyComponent>(entity);

		// 移動速度を計算
		float vx = dirX * speed_;
		float vz = dirZ * speed_;

		if (type_ == Walk) {
			// 地面を歩くタイプ
			rb.velocity.x = vx;
			rb.velocity.z = vz;

			// 地面の高さに合わせて y 座標を補正（埋まり・浮き防止）
			float h = scene->GetHeightAt(tc.translate.x, tc.translate.z, tc.translate.y + 1.0f, static_cast<uint32_t>(entity));
			if (h > -9000.0f) {
				tc.translate.y = h + 0.1f; // 少しだけ浮かせて接地させるやんす
			}
		} 
		//else {
		//	// 飛行タイプ（y軸はふわふわさせるやんす）
		//	rb.velocity.x = vx;
		//	rb.velocity.z = vz;

		//	float floatHeight = 5.0f; // 地面から5m上を飛ぶ
		//	float targetY = groundHeight_ + floatHeight + std::sin(scene->GetContext().playTime * 2.0f) * 0.5f;
		//	tc.translate.y += (targetY - tc.translate.y) * 2.0f * dt;
		//}

		// 4. 進んでいる方向を向く（滑らかに回転させるとより『スローンフォール』っぽいやんす！）
		if (std::abs(vx) > 0.1f || std::abs(vz) > 0.1f) {
			float targetAngle = std::atan2(vx, vz);
			// 角度の線形補間（Lerp）を自作エンジン側で持ってればそれを使うのがベストやんす
			tc.rotate.y = targetAngle; 
		}
	}
}

void EnemyBehavior::Debug() {
/*
#ifndef NDEBUG
#ifdef USE_IMGUI
	ImGui::Begin("Enemy Debug");
	ImGui::Text("State: %d", (int)state_);
	ImGui::Text("GroundHeight : %f", groundHeight_);
	ImGui::Text("Move Type : %s", (type_ == Fly ? "Fly" : "Walk"));

	if (showDebugGrid_) {
		ImGui::Text("Local Grid Debug");
		// 文字列を一括で構築して表示速度を稼ぐ
		std::string gridStr;
		gridStr.reserve(GRID_SIZE * (GRID_SIZE * 3 + 1));

		for (int z = GRID_SIZE - 1; z >= 0; --z) {
			for (int x = 0; x < GRID_SIZE; ++x) {
				// そのマスが path_ に含まれているかチェック
				bool isPath = false;
				for (const auto& p : path_) {
					int px = static_cast<int>((p.x - myPos_.x) / cellLength_) + (GRID_SIZE / 2);
					int pz = static_cast<int>((p.z - myPos_.z) / cellLength_) + (GRID_SIZE / 2);
					if (px == x && pz == z) {
						isPath = true;
						break;
					}
				}

				if (x == GRID_SIZE / 2 && z == GRID_SIZE / 2)
					gridStr += "|S|";
				else if (isPath)
					gridStr += " * ";
				else if (localGrid_[z][x].isWall)
					gridStr += " # ";
				else
					gridStr += " . ";
			}
			gridStr += "\n";
		}
		ImGui::TextUnformatted(gridStr.c_str());
	}
	ImGui::End();
#endif
*/
}

std::string EnemyBehavior::SerializeParameters() {
	nlohmann::json j;
	j["moveType"] = (int)type_;
	j["targetType"] = (int)targetType_;
	j["priority"] = (int)priority_;
	j["speed"] = speed_;
	return j.dump();
}

void EnemyBehavior::DeserializeParameters(const std::string& data) {
	if (data.empty())
		return;
	try {
		auto j = nlohmann::json::parse(data);
		if (j.contains("moveType"))
			type_ = (MoveType)j["moveType"].get<int>();
		if (j.contains("targetType"))
			targetType_ = (TargetType)j["targetType"].get<int>();
		if (j.contains("priority"))
			priority_ = (TargetPriority)j["priority"].get<int>();
		if (j.contains("speed"))
			speed_ = j["speed"].get<float>();
	} catch (...) {
	}
}

// ★ スクリプト自動登録
REGISTER_SCRIPT(EnemyBehavior);

} // namespace Game
