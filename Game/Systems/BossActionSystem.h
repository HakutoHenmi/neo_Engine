#pragma once
#include "ISystem.h"
#include <cmath>
#include "../Scripts/WarningEffectScript.h"

namespace Game {

class BossActionSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// プレイヤーの位置を取得
		DirectX::XMFLOAT3 playerPos = {0, 0, 0};
		bool playerFound = false;
		auto playerView = registry.view<TagComponent, TransformComponent>();
		for (auto e : playerView) {
			if (playerView.get<TagComponent>(e).tag == TagType::Player) {
				playerPos = playerView.get<TransformComponent>(e).translate;
				playerFound = true;
				break;
			}
		}
		if (!playerFound) return;

		auto view = registry.view<BossActionComponent, TransformComponent>();
		for (auto entity : view) {
			auto& boss = view.get<BossActionComponent>(entity);
			auto& tc = view.get<TransformComponent>(entity);
			if (!boss.enabled) continue;
			
			// ★追加: サンドバッグモード時はボスの行動（AI更新）を停止する
			if (ctx.isSandbagMode) continue;

			float dx = playerPos.x - tc.translate.x;
			float dz = playerPos.z - tc.translate.z;
			float dist = std::sqrt(dx * dx + dz * dz);
			float targetAngle = std::atan2(dx, dz);

			boss.stateTimer += ctx.dt;
			boss.turnDirection = 0.0f;

			switch (boss.state) {
			case BossState::Idle:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed, ctx.dt, boss);
				if (boss.stateTimer > 1.0f) { // 待機時間
					// アタックパターンを選ぶ (距離ベースなどのロジックを入れる)
					if (!boss.patterns.empty()) {
						int selected = -1;
						bool handled = false;
						
						// 20%の確率で距離に関係なくビーム攻撃
						if (rand() % 100 < 20) {
							for (int i = 0; i < boss.patterns.size(); ++i) {
								if (boss.patterns[i].type == BossAttackType::BeamSweep) {
									selected = i;
									handled = true;
									boss.currentPatternIndex = selected;
									TransitionTo(boss, BossState::WindUp);
									ShowWarningEffect(registry, entity, boss.patterns[selected].windUpDuration);
									break;
								}
							}
						}

						if (!handled) {
							// 遠距離(10m以上)の場合はジャンプ攻撃か走るかをランダムで選択
							if (dist >= 10.0f) {
								if (rand() % 2 == 0) {
									// ジャンプ攻撃
									for (int i = 0; i < boss.patterns.size(); ++i) {
										if (boss.patterns[i].type == BossAttackType::JumpPress) {
											selected = i;
											break;
										}
									}
									boss.currentPatternIndex = selected;
									TransitionTo(boss, BossState::WindUp);
									ShowWarningEffect(registry, entity, boss.patterns[selected].windUpDuration);
								} else {
									// 走って近づく
									std::vector<int> closePatterns;
									for (int i = 0; i < boss.patterns.size(); ++i) {
										if (boss.patterns[i].type != BossAttackType::JumpPress && boss.patterns[i].type != BossAttackType::BeamSweep) {
											closePatterns.push_back(i);
										}
									}
									if (!closePatterns.empty()) {
										selected = closePatterns[rand() % closePatterns.size()];
									}
									boss.currentPatternIndex = selected;
									TransitionTo(boss, BossState::Run);
								}
							} else {
								// 中〜近距離の場合は他の攻撃から選ぶ
								std::vector<int> closePatterns;
								for (int i = 0; i < boss.patterns.size(); ++i) {
									if (boss.patterns[i].type != BossAttackType::JumpPress && boss.patterns[i].type != BossAttackType::BeamSweep) {
										closePatterns.push_back(i);
									}
								}
								if (!closePatterns.empty()) {
									selected = closePatterns[rand() % closePatterns.size()];
								} else {
									selected = rand() % boss.patterns.size();
								}
								
								boss.currentPatternIndex = selected;
								auto& p = boss.patterns[boss.currentPatternIndex];
								if (dist > p.range) {
									TransitionTo(boss, BossState::Chase);
								} else {
									TransitionTo(boss, BossState::WindUp);
									ShowWarningEffect(registry, entity, p.windUpDuration);
								}
							}
						}
					}
				}
				break;

			case BossState::Run:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed * 1.5f, ctx.dt, boss);
				if (dist > 0.5f) {
					float nx = dx / dist;
					float nz = dz / dist;
					float runSpeed = boss.chaseSpeed * 3.0f; // 走る速度
					tc.translate.x += nx * runSpeed * ctx.dt;
					tc.translate.z += nz * runSpeed * ctx.dt;
				}
				if (boss.currentPatternIndex >= 0) {
					if (dist <= boss.patterns[boss.currentPatternIndex].range) {
						TransitionTo(boss, BossState::WindUp);
						ShowWarningEffect(registry, entity, boss.patterns[boss.currentPatternIndex].windUpDuration);
					}
				}
				break;

			case BossState::Chase:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed, ctx.dt, boss);
				
				// もし歩いて追いかけている最中にプレイヤーに逃げられ、再び距離が離れた場合は走りかジャンプに切り替える
				if (dist >= 10.0f) {
					if (rand() % 2 == 0) {
						// ジャンプ攻撃へ切り替え
						int jumpIndex = -1;
						for (int i = 0; i < boss.patterns.size(); ++i) {
							if (boss.patterns[i].type == BossAttackType::JumpPress) {
								jumpIndex = i;
								break;
							}
						}
						if (jumpIndex != -1) {
							boss.currentPatternIndex = jumpIndex;
							TransitionTo(boss, BossState::WindUp);
							ShowWarningEffect(registry, entity, boss.patterns[jumpIndex].windUpDuration);
						}
					} else {
						// 走るモードへ切り替え（攻撃対象の近接パターンは維持したまま）
						TransitionTo(boss, BossState::Run);
					}
					break;
				}

				if (dist > 0.5f) {
					float nx = dx / dist;
					float nz = dz / dist;
					tc.translate.x += nx * boss.chaseSpeed * ctx.dt;
					tc.translate.z += nz * boss.chaseSpeed * ctx.dt;
				}
				if (boss.currentPatternIndex >= 0) {
					if (dist <= boss.patterns[boss.currentPatternIndex].range) {
						TransitionTo(boss, BossState::WindUp);
						ShowWarningEffect(registry, entity, boss.patterns[boss.currentPatternIndex].windUpDuration);
					}
				}
				break;

			case BossState::WindUp:
				SmoothRotate(tc, targetAngle, boss.rotationSpeed * 0.3f, ctx.dt, boss);
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					if (boss.stateTimer >= p.windUpDuration) {
						TransitionTo(boss, BossState::Attack);
						if (registry.all_of<HitboxComponent>(entity)) {
							auto& hb = registry.get<HitboxComponent>(entity);
							hb.isActive = true;
							hb.damage = p.damage;
							hb.hitTargets.clear(); // 多段ヒット防止の履歴をクリア
						}
					}
				}
				break;

			case BossState::Attack:
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					if (p.type == BossAttackType::BeamSweep) {
						// ビームの生成
						if (boss.currentBeamEntity == entt::null) {
							boss.currentBeamEntity = registry.create();
							registry.emplace<TransformComponent>(boss.currentBeamEntity);
							auto& beamMr = registry.emplace<MeshRendererComponent>(boss.currentBeamEntity);
							beamMr.modelPath = "Resources/Models/Cylinder/cylinder.obj";
							beamMr.texturePath = "";
							beamMr.shaderName = "EnergyBeam";
							beamMr.color = { 0.1f, 0.8f, 1.0f, 0.8f }; // 青白い発光色
							
							if (ctx.renderer) {
								beamMr.modelHandle = ctx.renderer->LoadObjMesh(beamMr.modelPath);
							}
							
							auto& hb = registry.emplace<HitboxComponent>(boss.currentBeamEntity);
							hb.damage = p.damage;
							hb.isActive = true;
							hb.isProjectile = false; 
							hb.tag = TagType::Enemy;
							hb.center = { 0.0f, 0.0f, 0.0f }; // 中心をTransformに合わせる
							
							registry.emplace<TagComponent>(boss.currentBeamEntity, TagType::Enemy);

							// ビームの周りに散らすパーティクルエフェクト
							auto& pe = registry.emplace<ParticleEmitterComponent>(boss.currentBeamEntity);
							pe.emitter.params.name = "BeamAura";
							pe.emitter.params.emitRate = 150.0f;
							pe.emitter.params.lifeTime = 0.4f;
							pe.emitter.params.lifeTimeVariance = 0.2f;
							pe.emitter.params.startSize = { 2.0f, 2.0f, 2.0f };
							pe.emitter.params.endSize = { 0.0f, 0.0f, 0.0f };
							pe.emitter.params.startColor = { 0.1f, 0.8f, 1.0f, 1.0f }; // ビームと同系色
							pe.emitter.params.endColor = { 0.0f, 0.2f, 1.0f, 0.0f };
							pe.emitter.params.shape = Engine::EmissionShape::Sphere;
							pe.emitter.params.shapeRadius = 1.0f; // スケールで引き伸ばされるので小さめでOK
							pe.emitter.params.startVelocity = { 0.0f, 2.0f, 0.0f };
							pe.emitter.params.velocityVariance = { 5.0f, 5.0f, 5.0f };
							pe.emitter.params.shaderName = "SoftParticleAdditive";
						}
						
						// ビームのスイープ（左から右へ）
						if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
							auto& beamTc = registry.get<TransformComponent>(boss.currentBeamEntity);
							
							// ボスの向いている方向ベクトル
							float bossYaw = tc.rotate.y;
							float rightX = std::cos(bossYaw);
							float rightZ = -std::sin(bossYaw);
							float forwardX = std::sin(bossYaw);
							float forwardZ = std::cos(bossYaw);
							
							// ボスの頭の位置（アニメーションに合わせて微調整）
							// 横ズレや前後ズレがある場合はここの数値を調整します
							float offsetX = 0.0f;    // 横方向のズレ（プラスで右、マイナスで左）
							float offsetZ = 3.0f;    // 前方向のズレ（頭が前に出ている分）
							float offsetY = 5.5f;    // 高さ
							
							float headX = tc.translate.x + (rightX * offsetX) + (forwardX * offsetZ);
							float headY = tc.translate.y + offsetY;
							float headZ = tc.translate.z + (rightZ * offsetX) + (forwardZ * offsetZ);
							
							float progress = boss.stateTimer / p.activeDuration;
							float sweepAngle = -DirectX::XM_PIDIV4 + (DirectX::XM_PIDIV2 * progress); // -45度 〜 +45度
							
							// プレイヤーへの高低差を計算し、ビームのピッチ角（上下角）を求める
							float targetY = playerPos.y + 1.0f; // プレイヤーの胸〜頭あたりを狙う
							float beamDy = targetY - headY;
							float pitchAngle = std::atan2(-beamDy, dist);
							
							float finalYaw = bossYaw + sweepAngle;
							beamTc.rotate.x = DirectX::XM_PIDIV2 + pitchAngle; // 前方に倒しつつ、プレイヤーの高さへ向ける
							beamTc.rotate.y = finalYaw;
							beamTc.rotate.z = 0.0f;
							
							beamTc.scale = { 1.5f, 50.0f, 1.5f }; // 長さ50
							
							// シリンダーモデルの中心が原点にある場合、そのまま回転させると後ろにもビームが飛び出してしまう。
							// そのため、ビームの長さの半分だけ向いている方向にずらして、根本が頭になるようにする。
							float beamLengthHalf = 50.0f; // モデルの元のサイズによって変わるため、合わない場合はここを調整
							float dirX = std::sin(finalYaw) * std::cos(pitchAngle);
							float dirY = -std::sin(pitchAngle);
							float dirZ = std::cos(finalYaw) * std::cos(pitchAngle);
							
							beamTc.translate.x = headX + dirX * beamLengthHalf;
							beamTc.translate.y = headY + dirY * beamLengthHalf;
							beamTc.translate.z = headZ + dirZ * beamLengthHalf;
							
							// AABB（当たり判定）の更新
							// CombatSystemがY軸回転しか考慮しないため、サイズをワールド座標のバウンディングボックスに合わせる
							if (registry.all_of<HitboxComponent>(boss.currentBeamEntity)) {
								auto& hb = registry.get<HitboxComponent>(boss.currentBeamEntity);
								hb.size.x = 3.0f; // ビームの幅
								hb.size.y = std::abs(dirY * 50.0f) + 3.0f; // 上下の厚み
								hb.size.z = std::abs(std::cos(pitchAngle) * 50.0f) + 3.0f; // 水平面での長さ
							}
						}
					} else if (p.type == BossAttackType::JumpPress) {
						// ジャンプ攻撃中は、離陸直後から前半（0.45）まで前進する
						if (boss.stateTimer > 0.1f && boss.stateTimer < p.activeDuration * 0.45f) {
							float facing = tc.rotate.y;
							tc.translate.x += std::sin(facing) * p.thrustForce * ctx.dt;
							tc.translate.z += std::cos(facing) * p.thrustForce * ctx.dt;
						}
					} else {
						// 通常の攻撃の前半だけ前進させる
						if (boss.stateTimer < p.activeDuration * 0.5f) {
							float facing = tc.rotate.y;
							tc.translate.x += std::sin(facing) * p.thrustForce * ctx.dt;
							tc.translate.z += std::cos(facing) * p.thrustForce * ctx.dt;
						}
					}

					if (boss.stateTimer >= p.activeDuration) {
						TransitionTo(boss, BossState::Cooldown);
						if (registry.all_of<HitboxComponent>(entity)) {
							registry.get<HitboxComponent>(entity).isActive = false;
						}
						// ビームの破棄
						if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
							registry.destroy(boss.currentBeamEntity);
							boss.currentBeamEntity = entt::null;
						}
					}
				}
				break;

			case BossState::Cooldown:
				if (boss.currentPatternIndex >= 0) {
					auto& p = boss.patterns[boss.currentPatternIndex];
					if (boss.stateTimer >= p.recoveryDuration) {
						TransitionTo(boss, BossState::Idle);
						boss.currentPatternIndex = -1;
					}
				}
				break;

			case BossState::Stunned:
				if (boss.stateTimer >= boss.stunDuration) {
					TransitionTo(boss, BossState::Idle);
					boss.currentPatternIndex = -1;
				}
				break;

			case BossState::Down:
				// 部位破壊時などの大ダウン
				if (boss.stateTimer >= boss.stunDuration * 2.0f) {
					TransitionTo(boss, BossState::Idle);
					boss.currentPatternIndex = -1;
				}
				break;

			case BossState::Dead:
				// 何もしない
				break;
			}
		}
	}

	void Reset(entt::registry& registry) override {
		auto view = registry.view<BossActionComponent>();
		for (auto entity : view) {
			auto& boss = view.get<BossActionComponent>(entity);
			boss.state = BossState::Idle;
			boss.stateTimer = 0.0f;
			boss.currentPatternIndex = -1;
			if (boss.currentBeamEntity != entt::null && registry.valid(boss.currentBeamEntity)) {
				registry.destroy(boss.currentBeamEntity);
				boss.currentBeamEntity = entt::null;
			}
			if (registry.all_of<HitboxComponent>(entity)) {
				registry.get<HitboxComponent>(entity).isActive = false;
			}
		}
	}

private:
	void TransitionTo(BossActionComponent& boss, BossState newState) {
		boss.state = newState;
		boss.stateTimer = 0.0f;
	}

	void SmoothRotate(TransformComponent& tc, float targetAngle, float speed, float dt, BossActionComponent& boss) {
		float diff = targetAngle - tc.rotate.y;
		while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
		while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
		boss.turnDirection = diff;
		tc.rotate.y += diff * std::min(1.0f, speed * dt);
	}

	void ShowWarningEffect(entt::registry& registry, entt::entity entity, float duration) {
		if (!registry.all_of<ScriptComponent>(entity)) {
			registry.emplace<ScriptComponent>(entity);
		}
		auto& sc = registry.get<ScriptComponent>(entity);
		bool hasWarning = false;
		for (const auto& entry : sc.scripts) {
			if (entry.scriptPath == "WarningEffectScript") hasWarning = true;
		}
		if (!hasWarning) {
			sc.scripts.push_back({"WarningEffectScript", "{ \"duration\": " + std::to_string(duration) + " }", std::make_shared<WarningEffectScript>(), false});
		}
	}
};

} // namespace Game
