// CombatSystem.cpp
// CombatSystem の関数の実装 (宣言は CombatSystem.h)。
#include "../Scenes/GameScene.h"
#include "CombatSystem.h"

namespace Game {

void CombatSystem::Update(entt::registry& registry, GameContext& ctx) {
	// PLAY 中でなければ何もしない
	if (!ctx.isPlaying) return;



	// 前フレームのヒット済みの組を消す
	hitPairs_.clear();

	// すべての攻撃判定と被弾判定を集める
	auto hitboxView = registry.view<HitboxComponent, TransformComponent>();
	auto hurtboxView = registry.view<HurtboxComponent, TransformComponent>();
	// 反復の後で実行する処理 (エフェクトの生成など) の一覧
	std::vector<std::function<void()>> deferredActions;

	// 有効な攻撃判定を 1 つずつ調べる
	for (auto hbEntity : hitboxView) {
		auto& hb = hitboxView.get<HitboxComponent>(hbEntity);
		if (!hb.enabled || !hb.isActive) continue;

		// 攻撃判定のワールド中心 (Y 軸回転だけを考慮する)
		auto& hbTc = hitboxView.get<TransformComponent>(hbEntity);

		float hbSin = std::sin(hbTc.rotate.y);
		float hbCos = std::cos(hbTc.rotate.y);
		DirectX::XMFLOAT3 hbWorldCenter = {
			hbTc.translate.x + (hb.center.x * hbCos + hb.center.z * hbSin),
			hbTc.translate.y + hb.center.y,
			hbTc.translate.z + (-hb.center.x * hbSin + hb.center.z * hbCos)
		};
		// 弾の移動の分だけ判定を伸ばす量
		DirectX::XMFLOAT3 hbSweepSize = {0.0f, 0.0f, 0.0f};
		// 缶の弾 (剛体で飛ぶ) は 1 フレームの移動量を求める
		if (registry.all_of<CanAttackEffectComponent, RigidbodyComponent>(hbEntity)) {
			const auto& rb = registry.get<RigidbodyComponent>(hbEntity);
			const DirectX::XMFLOAT3 frameMove = {
				rb.velocity.x * ctx.dt,
				rb.velocity.y * ctx.dt,
				rb.velocity.z * ctx.dt
			};
			// 高速な缶弾がフレーム間で敵を通り抜けないよう、直前位置まで判定を伸ばす。
			hbWorldCenter.x -= frameMove.x * 0.5f;
			hbWorldCenter.y -= frameMove.y * 0.5f;
			hbWorldCenter.z -= frameMove.z * 0.5f;
			hbSweepSize = {std::abs(frameMove.x), std::abs(frameMove.y), std::abs(frameMove.z)};
		}

		// 攻撃判定の持ち主のタグ
		TagType hbTag = TagType::Untagged;
		if (registry.all_of<TagComponent>(hbEntity)) {
			hbTag = registry.get<TagComponent>(hbEntity).tag;
		}
		// 弾の攻撃判定はプレイヤー陣営として扱い、自分自身に当たらないようにする
		if (hbTag == TagType::Projectile) {
			hbTag = TagType::Player;
		}

		for (auto hrEntity : hurtboxView) {
			// 自分自身とは衝突しない
			if (hbEntity == hrEntity) continue;

			// 無効な被弾判定はとばす
			auto& hr = hurtboxView.get<HurtboxComponent>(hrEntity);
			if (!hr.enabled) continue;

			// 同じタグ同士は衝突しない（味方同士の攻撃を防ぐ）
			TagType hrTag = TagType::Untagged;
			if (registry.all_of<TagComponent>(hrEntity)) {
				hrTag = registry.get<TagComponent>(hrEntity).tag;
			}
			if (hbTag == hrTag && hbTag != TagType::Untagged) continue;

			// 被弾側のトランスフォーム
			auto& hrTc = hurtboxView.get<TransformComponent>(hrEntity);

			// Hurtbox のワールド座標
			float hrSin = std::sin(hrTc.rotate.y);
			float hrCos = std::cos(hrTc.rotate.y);
			DirectX::XMFLOAT3 hrWorldCenter = {
				hrTc.translate.x + (hr.center.x * hrCos + hr.center.z * hrSin),
				hrTc.translate.y + hr.center.y,
				hrTc.translate.z + (-hr.center.x * hrSin + hr.center.z * hrCos)
			};

			// 回転を加味したAABBサイズの再計算（OBBを包むAABB）
			DirectX::XMFLOAT3 hbWorldSize = {
				std::abs(hb.size.x * hbCos) + std::abs(hb.size.z * hbSin) + hbSweepSize.x,
				hb.size.y + hbSweepSize.y,
				std::abs(hb.size.x * hbSin) + std::abs(hb.size.z * hbCos) + hbSweepSize.z
			};
			DirectX::XMFLOAT3 hrWorldSize = {
				std::abs(hr.size.x * hrCos) + std::abs(hr.size.z * hrSin),
				hr.size.y,
				std::abs(hr.size.x * hrSin) + std::abs(hr.size.z * hrCos)
			};

			// AABB の当たり判定
			if (!AABBOverlap(hbWorldCenter, hbWorldSize, hrWorldCenter, hrWorldSize)) continue;

			// この攻撃で既に当たった相手ならとばす
			if (std::find(hb.hitTargets.begin(), hb.hitTargets.end(), hrEntity) != hb.hitTargets.end()) {
				continue;
			}

			// 弾は、ダメージが通ったかどうか (敵の無敵時間) に関係なく、触れた時点ですぐ消す
			if (hb.isProjectile) {
				if (registry.all_of<AutoDestroyComponent>(hbEntity)) {
					registry.get<AutoDestroyComponent>(hbEntity).timer = 0.0f;
				}
			}

			// 通常のダメージ処理
			// 攻撃の向き (被弾側への方向に少し上向きを足して正規化する)
			DirectX::XMFLOAT3 hitDir = {
				hrWorldCenter.x - hbWorldCenter.x,
				0.35f,
				hrWorldCenter.z - hbWorldCenter.z
			};
			float hitDirLen = std::sqrt(hitDir.x * hitDir.x + hitDir.y * hitDir.y + hitDir.z * hitDir.z);
			if (hitDirLen > 0.001f) {
				hitDir.x /= hitDirLen;
				hitDir.y /= hitDirLen;
				hitDir.z /= hitDirLen;
			} else {
				hitDir = { std::sin(hbTc.rotate.y), 0.35f, std::cos(hbTc.rotate.y) };
			}

			// 缶の弾なら、その缶の種類
			const auto* canEffect = registry.try_get<CanAttackEffectComponent>(hbEntity);
			const CanType incomingCanType = canEffect ? canEffect->canType : CanType::None;
			// ダメージを与える (被弾側の倍率を掛ける)
			bool hitSuccess = ApplyDamage(registry, hrEntity, hb.damage * hr.damageMultiplier, ctx,
				hrWorldCenter, hitDir, hbTag, incomingCanType);
			// スライムのトゲが跳ねる敵に当たったらコンボに記録する
			if (hitSuccess && ctx.combatFlow &&
				registry.all_of<PlayerActionComponent>(hbEntity) &&
				registry.get<PlayerActionComponent>(hbEntity).state == PlayerActionState::SlimeSpike &&
				registry.all_of<EnemyAIComponent, HealthComponent>(hrEntity) &&
				registry.get<EnemyAIComponent>(hrEntity).hopper) {
				const auto& health = registry.get<HealthComponent>(hrEntity);
				ctx.combatFlow->RegisterHit(health.Hp() <= 0.0f);
			}
			if (canEffect) {
				// 状態弾は接触そのものを成功とし、敵の無敵時間中でも固有効果を与える。
				ApplyCanEffect(registry, hrEntity, hbWorldCenter, *canEffect);
			}

			// 無敵時間などでダメージが適用されなかった場合は、履歴に残さず（後で当たるように）スキップ
			if (!hitSuccess && !canEffect) continue;

			// ダメージが通ったのでヒット履歴に記録
			hb.hitTargets.push_back(hrEntity);
			
			// この組を今フレームのヒット済みとして記録する
			uint64_t pairKey = MakePairKey(hbEntity, hrEntity);
			hitPairs_.insert(pairKey);

				// 攻撃側のヒットストップ
				if (registry.all_of<PlayerActionComponent>(hbEntity)) {
					auto& attackerPa = registry.get<PlayerActionComponent>(hbEntity);
					attackerPa.hitStopTimer = 0.035f;
				}

				// カメラシェイク（軽い）
				if (ctx.camera) {
					ctx.camera->StartShake(0.3f, 0.15f); // 時間, 振幅
				}

				// ヒットエフェクト (粒子など) を作る
				// プレイヤーの攻撃（スライム攻撃）がヒットした時のみエフェクトを出す
				if (hb.tag == TagType::Player) {
					// 攻撃の向き (攻撃側の前方向)
					Engine::Vector3 attackDir = {
						std::sin(hbTc.rotate.y),
						0.0f,
						std::cos(hbTc.rotate.y)
					};
					// 攻撃側と被弾側の中間から少し手前にエフェクトを出す
					DirectX::XMFLOAT3 spawnPos = {
						(hbWorldCenter.x + hrWorldCenter.x) * 0.5f - attackDir.x * 1.5f,
						(hbWorldCenter.y + hrWorldCenter.y) * 0.5f,
						(hbWorldCenter.z + hrWorldCenter.z) * 0.5f - attackDir.z * 1.5f
					};
					
					// 爆発の攻撃判定か
					bool isExplosionAttack = false;
					if (registry.all_of<NameComponent>(hbEntity)) {
						if (registry.get<NameComponent>(hbEntity).name == "ExplosionHitbox") {
							isExplosionAttack = true;
						}
					}
					
					bool isProj = hb.isProjectile;

					// 反復中にエンティティを作ると判定の格納領域が動くので、後で作る
					deferredActions.push_back([&registry, spawnPos, attackDir, isExplosionAttack, isProj]() {
						auto effectEntity = registry.create();
						registry.emplace<NameComponent>(effectEntity, "HitEffect");
						auto& tc = registry.emplace<TransformComponent>(effectEntity);
						tc.translate = spawnPos;

						// ヒットエフェクトのスクリプトを付ける
						auto& sc = registry.emplace<ScriptComponent>(effectEntity);
						ScriptEntry entry;
						entry.scriptPath = "HitEffectScript";

						if (isExplosionAttack) {
							entry.parameterData = "isExplosionHit=1";
						} else {
							// 近接攻撃や水の弾が当たったときは液体の飛び散りを出す
							entry.parameterData = "isLiquidSplatter=1,dirX=" + std::to_string(attackDir.x) + ",dirZ=" + std::to_string(attackDir.z);
						}
						sc.scripts.push_back(entry);
					});
				}
		}
	}

	// ループ後に生成処理を実行
	for (auto& action : deferredActions) {
		action();
	}
}

} // namespace Game
