#include "KamikazeEnemyScript.h"
#include "ObjectTypes.h"
#include "Scenes/GameScene.h"
#include "ScriptEngine.h"
#include <cmath>
#include <cstdlib>
#include <string>

namespace Game {

void KamikazeEnemyScript::Start(entt::entity /*entity*/, GameScene* /*scene*/) {
	isExploded_ = false;
}

void KamikazeEnemyScript::Update(entt::entity entity, GameScene* scene, float dt) {
	// 死亡済みなら何もしない
	auto& registry = scene->GetRegistry();
	if (registry.all_of<HealthComponent>(entity)) {
		if (registry.get<HealthComponent>(entity).hp <= 0.0f) {
			return;
		}
	}
	if (isExploded_) return;
	
	if (!registry.all_of<TransformComponent>(entity)) return;
	auto& myTc = registry.get<TransformComponent>(entity);

	// プレイヤー検索
	DirectX::XMFLOAT3 targetPos = {0, 0, 0};
	// キャッシュが有効か確認
	bool found = false;
	
	const auto& players = scene->GetEntitiesByTag(TagType::Player);
	for (auto p : players) {
		if (registry.valid(p) && registry.all_of<TransformComponent>(p)) {
			targetPos = registry.get<TransformComponent>(p).translate;
			found = true;
			break;
		}
	}

	if (!found) {
		// プレイヤーが見つからない場合、一度だけログを出して警告する
		static bool playerNotFoundWarned = false;
		if (!playerNotFoundWarned) {
			playerNotFoundWarned = true;
		}
		return;
	}

	if (found) {
		float dx = myTc.translate.x - targetPos.x;
		float dy = myTc.translate.y - targetPos.y;
		float dz = myTc.translate.z - targetPos.z;
		float distSq = dx * dx + dy * dy + dz * dz;
		float dist = std::sqrt(distSq);

		// 視界内なら追尾
		if (dist <= sightRange_) {
			// 自爆距離に到達したら爆発
			if (dist <= triggerRange_) {
				Explode(entity, scene);
				return;
			}

			// プレイヤーに向かって移動
			if (dist > 0.001f) {
				myTc.translate.x -= (dx / dist) * speed_ * dt;
				myTc.translate.y -= (dy / dist) * speed_ * dt;
				myTc.translate.z -= (dz / dist) * speed_ * dt;

				// 進行方向を向く (Y軸回転)
				myTc.rotate.y = std::atan2(dx, dz) + 3.14159f;
			}
		}
	}
}

void KamikazeEnemyScript::Explode(entt::entity entity, GameScene* scene) {
	isExploded_ = true;

	auto& registry = scene->GetRegistry();

	// 1. 攻撃判定 (Hitbox) を有効化して範囲攻撃にする
	if (!registry.all_of<HitboxComponent>(entity)) {
		registry.emplace<HitboxComponent>(entity);
	}
	// 既存のHitboxがあればそれを上書き、なければ新規設定
	auto& hb = registry.get<HitboxComponent>(entity);
	hb.isActive = true;
	hb.damage = damage_;
	hb.size = {explosionRadius_, explosionRadius_, explosionRadius_};
	hb.tag = TagType::Explosion;

	// 2. 爆発エフェクト（破片）を生成
	// （※旧コードの SpawnObject が未対応だが、本来は EnTT ベースで行うべき）

	// 3. 自身を死亡させる (HPを0にする)
	if (!registry.all_of<HealthComponent>(entity)) {
		registry.emplace<HealthComponent>(entity);
	}
	registry.get<HealthComponent>(entity).hp = 0.0f;
}

void KamikazeEnemyScript::OnDestroy(entt::entity /*entity*/, GameScene* /*scene*/) {}

// 自動登録
REGISTER_SCRIPT(KamikazeEnemyScript);

} // namespace Game