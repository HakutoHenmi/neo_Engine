#pragma once
#include "ISystem.h"
#include "EnemyAISystem.h" // ★追加: パリィ成功時の敵AI連携
#include <cmath>
#include <unordered_set>

namespace Game {

// パリィの歪みエフェクト用コンポーネント
struct ParryDistortionComponent {
	float timer = 0.0f;
	float duration = 0.5f;
	float startScale = 0.5f;
	float endScale = 12.0f;
};

// ★ CombatSystem: Hitbox vs Hurtbox の衝突判定 + パリィ処理
class CombatSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// --- パリィ歪みエフェクトのアニメーション ---
		auto distView = registry.view<ParryDistortionComponent, TransformComponent, MeshRendererComponent>();
		for (auto entity : distView) {
			auto& pd = distView.get<ParryDistortionComponent>(entity);
			auto& tc = distView.get<TransformComponent>(entity);
			auto& mr = distView.get<MeshRendererComponent>(entity);
			pd.timer += ctx.dt;
			float t = pd.timer / pd.duration;
			if (t > 1.0f) t = 1.0f;
			
			// イーズアウト（急速に広がり、ゆっくり止まる）
			float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
			float s = pd.startScale + (pd.endScale - pd.startScale) * easeT;
			tc.scale = { s, s, s };
			
			// アルファ値（シェーダー側では 1.0 - color.w を progress として扱う）
			// easeT を渡すことで、スケールの拡大とリングの広がりが完全に同期する
			mr.color.w = 1.0f - easeT;

			// 毎フレーム常にカメラの方を向く（ビルボード）
			if (ctx.camera) {
				// カメラと同じ回転を与えると、オブジェクトのローカル座標系がカメラと一致する
				tc.rotate = ctx.camera->Rotation();
				
				// Planeモデルの法線が上（Y+）だとすると、この時点では上を向いている。
				// カメラ方向（Z軸）に面を向かせるため、ローカルでX軸に-90度回転を追加する
				tc.rotate.x -= DirectX::XM_PIDIV2;
			}
		}

		// --- 前フレームのヒット済みペアをクリア ---
		hitPairs_.clear();

		// --- 全Hitboxエンティティを収集 ---
		auto hitboxView = registry.view<HitboxComponent, TransformComponent>();
		auto hurtboxView = registry.view<HurtboxComponent, TransformComponent>();

		for (auto hbEntity : hitboxView) {
			auto& hb = hitboxView.get<HitboxComponent>(hbEntity);
			if (!hb.enabled || !hb.isActive) continue;

			auto& hbTc = hitboxView.get<TransformComponent>(hbEntity);

			float hbSin = std::sin(hbTc.rotate.y);
			float hbCos = std::cos(hbTc.rotate.y);
			DirectX::XMFLOAT3 hbWorldCenter = {
				hbTc.translate.x + (hb.center.x * hbCos + hb.center.z * hbSin),
				hbTc.translate.y + hb.center.y,
				hbTc.translate.z + (-hb.center.x * hbSin + hb.center.z * hbCos)
			};

			// Hitboxの所有者のタグを取得
			TagType hbTag = TagType::Untagged;
			if (registry.all_of<TagComponent>(hbEntity)) {
				hbTag = registry.get<TagComponent>(hbEntity).tag;
			}

			for (auto hrEntity : hurtboxView) {
				// 自分自身とは衝突しない
				if (hbEntity == hrEntity) continue;

				auto& hr = hurtboxView.get<HurtboxComponent>(hrEntity);
				if (!hr.enabled) continue;

				// 同じタグ同士は衝突しない（味方同士の攻撃を防ぐ）
				TagType hrTag = TagType::Untagged;
				if (registry.all_of<TagComponent>(hrEntity)) {
					hrTag = registry.get<TagComponent>(hrEntity).tag;
				}
				if (hbTag == hrTag && hbTag != TagType::Untagged) continue;

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
					std::abs(hb.size.x * hbCos) + std::abs(hb.size.z * hbSin),
					hb.size.y,
					std::abs(hb.size.x * hbSin) + std::abs(hb.size.z * hbCos)
				};
				DirectX::XMFLOAT3 hrWorldSize = {
					std::abs(hr.size.x * hrCos) + std::abs(hr.size.z * hrSin),
					hr.size.y,
					std::abs(hr.size.x * hrSin) + std::abs(hr.size.z * hrCos)
				};

				// --- AABB衝突判定 ---
				if (!AABBOverlap(hbWorldCenter, hbWorldSize, hrWorldCenter, hrWorldSize)) continue;

				// 重複ヒット防止（同じペアは1フレームに1回まで）
				uint64_t pairKey = MakePairKey(hbEntity, hrEntity);
				if (hitPairs_.count(pairKey)) continue;
				hitPairs_.insert(pairKey);

				// --- パリィ判定 ---
				bool parried = false;
				if (registry.all_of<PlayerActionComponent>(hrEntity)) {
					auto& pa = registry.get<PlayerActionComponent>(hrEntity);
					if (pa.state == PlayerActionState::Parry) {
						// パリィ成功！
						parried = true;
						OnParrySuccess(registry, hrEntity, hbEntity, pa, ctx);
					}
				}

				if (!parried) {
					// --- 通常ダメージ処理 ---
					ApplyDamage(registry, hrEntity, hb.damage * hr.damageMultiplier, ctx);

					// 攻撃側のヒットストップ
					if (registry.all_of<PlayerActionComponent>(hbEntity)) {
						auto& attackerPa = registry.get<PlayerActionComponent>(hbEntity);
						attackerPa.hitStopTimer = attackerPa.hitStopDuration;
					}

					// カメラシェイク（軽い）
					if (ctx.camera) {
						ctx.camera->StartShake(0.3f, 0.15f); // 時間, 振幅
					}

					// ★追加: ヒットエフェクト（パーティクル）の生成
					auto effectEntity = registry.create();
					registry.emplace<AutoDestroyComponent>(effectEntity).timer = 0.5f;
					auto& tc = registry.emplace<TransformComponent>(effectEntity);
					tc.translate = {
						(hbWorldCenter.x + hrWorldCenter.x) * 0.5f,
						(hbWorldCenter.y + hrWorldCenter.y) * 0.5f,
						(hbWorldCenter.z + hrWorldCenter.z) * 0.5f
					};
					auto& pe = registry.emplace<ParticleEmitterComponent>(effectEntity);
					pe.emitter.params.name = "HitEffect";
					pe.emitter.params.position = {0, 0, 0};
					pe.emitter.params.emitRate = 0; // 継続放出なし
					pe.emitter.params.burstCount = 15; // バーストで放出
					pe.emitter.params.startColor = {1.0f, 0.8f, 0.2f, 1.0f}; // オレンジ
					pe.emitter.params.endColor = {1.0f, 0.2f, 0.0f, 0.0f}; // 赤く消える
					pe.emitter.params.startSize = {0.3f, 0.3f, 0.3f};
					pe.emitter.params.startSizeVariance = {0.1f, 0.1f, 0.1f};
					pe.emitter.params.startVelocity = {0, 3.0f, 0};
					pe.emitter.params.velocityVariance = {5.0f, 5.0f, 5.0f};
					pe.emitter.params.lifeTime = 0.4f;
					pe.emitter.params.lifeTimeVariance = 0.2f;
					pe.emitter.params.shape = Engine::EmissionShape::Sphere;
					pe.emitter.params.shapeRadius = 0.2f;
					pe.emitter.params.damping = 2.0f;
				}
			}
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		hitPairs_.clear();
	}

private:
	std::unordered_set<uint64_t> hitPairs_; // 1フレーム内の重複ヒット防止

	// 2エンティティのペアキーを生成（順序無関係）
	static uint64_t MakePairKey(entt::entity a, entt::entity b) {
		uint32_t ai = static_cast<uint32_t>(a);
		uint32_t bi = static_cast<uint32_t>(b);
		if (ai > bi) std::swap(ai, bi);
		return (static_cast<uint64_t>(ai) << 32) | static_cast<uint64_t>(bi);
	}

	// AABB同士の重なり判定
	static bool AABBOverlap(const DirectX::XMFLOAT3& c1, const DirectX::XMFLOAT3& s1,
	                         const DirectX::XMFLOAT3& c2, const DirectX::XMFLOAT3& s2) {
		float hx1 = s1.x * 0.5f, hy1 = s1.y * 0.5f, hz1 = s1.z * 0.5f;
		float hx2 = s2.x * 0.5f, hy2 = s2.y * 0.5f, hz2 = s2.z * 0.5f;
		return (std::abs(c1.x - c2.x) < hx1 + hx2) &&
		       (std::abs(c1.y - c2.y) < hy1 + hy2) &&
		       (std::abs(c1.z - c2.z) < hz1 + hz2);
	}

	// パリィ成功時の処理
	void OnParrySuccess(entt::registry& registry, entt::entity defender, entt::entity attacker,
	                    PlayerActionComponent& defenderPa, GameContext& ctx) {
		// 1. パリィ成功ステートへ遷移
		defenderPa.state = PlayerActionState::ParrySuccess;
		defenderPa.stateTimer = 0.0f;
		defenderPa.stateDuration = 0.3f; // パリィ成功後の硬直（短め）

		// 2. ヒットストップ（パリィ時は長め）
		defenderPa.hitStopTimer = 0.15f;

		// 3. 攻撃側もヒットストップ + のけぞり
		if (registry.all_of<PlayerActionComponent>(attacker)) {
			auto& attackerPa = registry.get<PlayerActionComponent>(attacker);
			attackerPa.hitStopTimer = 0.15f;
			attackerPa.state = PlayerActionState::Stagger;
			attackerPa.stateTimer = 0.0f;
			attackerPa.stateDuration = 0.8f; // パリィされた側は長い隙
		}
		// 敵がPlayerActionComponentを持っていない場合、HealthComponentのhitStopTimerで対応
		if (registry.all_of<HealthComponent>(attacker)) {
			auto& attackerHp = registry.get<HealthComponent>(attacker);
			attackerHp.hitStopTimer = 0.8f; // 敵がスタン的な硬直
			attackerHp.hitFlashTimer = 0.2f;
		}
		// ★追加: EnemyAIComponentを持つ敵はStunned状態に遷移
		if (registry.all_of<EnemyAIComponent>(attacker)) {
			auto& ai = registry.get<EnemyAIComponent>(attacker);
			ai.state = EnemyAIState::Stunned;
			ai.stateTimer = 0.0f;
			// 攻撃中のHitboxを即座に無効化
			if (registry.all_of<HitboxComponent>(attacker)) {
				registry.get<HitboxComponent>(attacker).isActive = false;
			}
		}

		// 4. カメラシェイク（パリィは強め）
		if (ctx.camera) {
			ctx.camera->StartShake(0.2f, 0.4f); // 時間, 振幅（パリィは強め）
		}

		// 5. イベント発火（スクリプトから反応可能に）
		if (ctx.eventSystem) {
			ctx.eventSystem->Emit("OnParrySuccess", static_cast<float>(static_cast<uint32_t>(defender)));
		}

		// 6. ★追加: 空間歪みエフェクト（Distortion）の生成
		if (registry.all_of<TransformComponent>(defender) && registry.all_of<TransformComponent>(attacker)) {
			auto& defTc = registry.get<TransformComponent>(defender);
			auto& attTc = registry.get<TransformComponent>(attacker);

			auto distEntity = registry.create();
			registry.emplace<AutoDestroyComponent>(distEntity).timer = 0.6f;
			auto& dtc = registry.emplace<TransformComponent>(distEntity);
			dtc.translate = {
				(defTc.translate.x + attTc.translate.x) * 0.5f,
				(defTc.translate.y + attTc.translate.y) * 0.5f + 3.0f, // 敵の高さに合わせてしっかり上に
				(defTc.translate.z + attTc.translate.z) * 0.5f
			};
			dtc.scale = { 0.1f, 0.1f, 0.1f };

			// カメラの方向を向くように回転（ビルボード初期化）
			if (ctx.camera) {
				dtc.rotate = ctx.camera->Rotation();
				dtc.rotate.x -= DirectX::XM_PIDIV2;
			} else {
				dtc.rotate = { 0, 0, 0 };
			}

			auto& dmr = registry.emplace<MeshRendererComponent>(distEntity);
			dmr.modelPath = "Resources/Models/plane.obj";
			dmr.texturePath = "Resources/Textures/ripple_normal.png"; // ノーマルマップを指定
			dmr.shaderName = "Distortion";
			dmr.color = { 1.0f, 1.0f, 1.0f, 1.0f }; // W成分(アルファ)はDistortionの強度として使われる

			if (ctx.renderer) {
				dmr.modelHandle = ctx.renderer->LoadObjMesh(dmr.modelPath);
				dmr.textureHandle = ctx.renderer->LoadTexture2D(dmr.texturePath);
			}

			auto& pdc = registry.emplace<ParryDistortionComponent>(distEntity);
			pdc.duration = 0.8f;
			pdc.startScale = 5.0f;   // 最初から敵より大きく
			pdc.endScale = 30.0f;    // さらに大きく広がる
		}
	}

	// ダメージ適用
	void ApplyDamage(entt::registry& registry, entt::entity target, float damage, GameContext& ctx) {
		if (!registry.all_of<HealthComponent>(target)) return;
		auto& hc = registry.get<HealthComponent>(target);

		// 無敵時間中はダメージを受けない
		if (hc.invincibleTime > 0.0f) return;

		hc.hp -= damage;
		hc.hitFlashTimer = 0.1f; // ヒットフラッシュ演出
		hc.hitStopTimer = 0.05f; // 被弾側の軽いヒットストップ

		// 被弾後の短い無敵時間
		hc.invincibleTime = 0.2f;

		// ダメージイベント
		if (ctx.eventSystem) {
			ctx.eventSystem->Emit("OnDamage", static_cast<float>(static_cast<uint32_t>(target)));
		}

		// プレイヤーが被弾した場合、のけぞりステートへ
		if (registry.all_of<PlayerActionComponent>(target)) {
			auto& pa = registry.get<PlayerActionComponent>(target);
			if (pa.state != PlayerActionState::Dodge) { // 回避中は被弾しない
				pa.state = PlayerActionState::Stagger;
				pa.stateTimer = 0.0f;
				pa.stateDuration = 0.3f;
			}
		}
	}
};

} // namespace Game
