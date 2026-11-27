#include "CleanupSystem.h"
#include <algorithm>
#include <vector>

namespace Game {

void CleanupSystem::Update(entt::registry& registry, GameContext& ctx) {
	if (!ctx.isPlaying)
		return;

	// 原点から 100 以上離れた弾は死亡扱いにする
	auto bulletView = registry.view<TagComponent, TransformComponent, HealthComponent>();
	for (auto entity : bulletView) {
		// 弾のタグを持つものだけを調べる
		auto& tag = bulletView.get<TagComponent>(entity);
		if (tag.tag != TagType::Bullet)
			continue;

		// 原点からの距離の 2 乗
		auto& tc = bulletView.get<TransformComponent>(entity);
		float distSq =
		    tc.translate.x * tc.translate.x + tc.translate.y * tc.translate.y + tc.translate.z * tc.translate.z;
		if (distSq > 10000.0f) {
			auto& hc = bulletView.get<HealthComponent>(entity);
			hc.SetDead(true);
		}
	}

	// 破棄するエンティティを集める
	std::vector<entt::entity> toDestroy;

	// 自動削除コンポーネント: 残り時間を減らし、0 以下なら破棄する
	auto autoView = registry.view<AutoDestroyComponent>();
	for (auto entity : autoView) {
		// 残り時間を減らす
		auto& adc = registry.get<AutoDestroyComponent>(entity);
		adc.timer -= ctx.dt;

		// ディゾルブのシェーダーなら、残り時間に合わせて不透明度を下げる
		if (registry.all_of<MeshRendererComponent>(entity)) {
			auto& mr = registry.get<MeshRendererComponent>(entity);
			if (mr.shaderName == "Dissolve") {
				// 残り時間 1.5 → 0 を、不透明度 1 → 0 に対応させる
				// (初期値は 1.5 を想定している)
				mr.color.w = std::clamp(adc.timer / 1.5f, 0.0f, 1.0f);
			}
		}

		// 時間が切れたら破棄の一覧へ
		if (adc.timer <= 0.0f) {
			toDestroy.push_back(entity);
		}
	}

	// 死亡して有効なままの体力を持つエンティティも破棄の一覧へ
	auto healthView = registry.view<HealthComponent>();
	for (auto entity : healthView) {
		auto& hc = registry.get<HealthComponent>(entity);
		if (hc.IsDead() && hc.enabled) {
			// プレイヤーは破棄しない
			if (registry.all_of<TagComponent>(entity)) {
				auto& tag = registry.get<TagComponent>(entity);
				if (tag.tag == TagType::Player)
					continue;
			}
			toDestroy.push_back(entity);
		}
	}

	// 重複を除いて二重に破棄しない
	std::sort(toDestroy.begin(), toDestroy.end());
	toDestroy.erase(std::unique(toDestroy.begin(), toDestroy.end()), toDestroy.end());

	// まだ有効なものだけを破棄する
	for (auto e : toDestroy) {
		if (registry.valid(e)) {
			registry.destroy(e);
		}
	}
}

} // namespace Game
