#include "BossTestScript.h"
#include "ScriptEngine.h"
#include "../Engine/Renderer.h"
#include "../Engine/Model.h"
#include <cmath>
#include <fstream>
#include "../Engine/ThirdParty/nlohmann/json.hpp"

using json = nlohmann::json;
namespace Game {

REGISTER_SCRIPT(BossTestScript);

void BossTestScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();

	// 1. ボスコンポーネントの初期化（パラメータ設定）
	if (!registry.all_of<BossActionComponent>(entity)) {
		registry.emplace<BossActionComponent>(entity);
	}
	auto& boss = registry.get<BossActionComponent>(entity);
	
	if (boss.patterns.empty()) {
		std::ifstream file("Resources/Scripts/boss_patterns.json");
		if (file.is_open()) {
			json j;
			file >> j;
			for (const auto& p : j["patterns"]) {
				BossActionPattern pattern;
				pattern.name = p.value("name", "Unknown");
				std::string typeStr = p.value("type", "Thrust");
				if (typeStr == "Thrust") pattern.type = BossAttackType::Thrust;
				else if (typeStr == "TailSpin") pattern.type = BossAttackType::TailSpin;
				else if (typeStr == "JumpPress") pattern.type = BossAttackType::JumpPress;
				else if (typeStr == "Punch") pattern.type = BossAttackType::Punch;
				
				pattern.windUpDuration = p.value("windUpDuration", 1.0f);
				pattern.activeDuration = p.value("activeDuration", 0.5f);
				pattern.recoveryDuration = p.value("recoveryDuration", 1.5f);
				pattern.range = p.value("range", 5.0f);
				pattern.damage = p.value("damage", 30.0f);
				pattern.thrustForce = p.value("thrustForce", 0.0f);
				boss.patterns.push_back(pattern);
			}
		}
	}
	
	// 振り向く速度を下げて、アニメーションのターンと違和感がないようにする
	boss.rotationSpeed = 1.5f;

	// 初期スケールを保存しておく
	if (registry.all_of<TransformComponent>(entity)) {
		auto& bTc = registry.get<TransformComponent>(entity);
		// アニメーション再生時のスケール崩壊対策として、モデル全体のスケールを大幅に縮小（さらに小さく調整）
		bTc.scale.x *= 0.005f;
		bTc.scale.y *= 0.005f;
		bTc.scale.z *= 0.005f;
		originalScale_ = bTc.scale;
	}

	// ★追加: 物理演算コンポーネント（重力と床判定）
	if (!registry.all_of<RigidbodyComponent>(entity)) {
		auto& rb = registry.emplace<RigidbodyComponent>(entity);
		rb.useGravity = true;
		rb.isKinematic = true; // ガクガク防止のためキネマティックにする（物理挙動は自前で行うかCMSに任せる）
	}
	if (!registry.all_of<BoxColliderComponent>(entity)) {
		auto& bc = registry.emplace<BoxColliderComponent>(entity);
		// BoxColliderはスケール倍されるため、ローカル空間では2x2x2程度にしておく
		bc.size = {2.0f, 2.0f, 2.0f};
		bc.center = {0, 1.0f, 0};
	}
	if (!registry.all_of<CharacterMovementComponent>(entity)) {
		auto& cm = registry.emplace<CharacterMovementComponent>(entity);
		cm.heightOffset = 0.0f; // ボスは足元原点
		cm.gravity = 40.0f;     // 巨体なので速く落ちる
	}

	// ★追加: アニメーションコンポーネントの追加
	if (!registry.all_of<AnimatorComponent>(entity)) {
		registry.emplace<AnimatorComponent>(entity);
	}

	// 2. ヒットボックス（攻撃判定）の準備
	if (!registry.all_of<HitboxComponent>(entity)) {
		auto& hb = registry.emplace<HitboxComponent>(entity);
		hb.size = {4.0f, 4.0f, 4.0f};
		hb.center = {0, 2.0f, 2.5f}; // 前方
		hb.tag = TagType::Enemy;
		hb.isActive = false; // 攻撃中のみtrueになる
	}

	// ★追加: ボス本体にHealthComponentがない場合は追加する（WaveSystem等で生存カウントするため）
	if (!registry.all_of<HealthComponent>(entity)) {
		auto& hc = registry.emplace<HealthComponent>(entity);
		hc.hp = 250.0f;
		hc.maxHp = 250.0f;
	}

	// ★修正: ボス本体にHurtboxComponentを追加し、サイズを見た目のスケールに完全に一致させる
	if (!registry.all_of<HurtboxComponent>(entity)) {
		registry.emplace<HurtboxComponent>(entity);
	}
	
	// ★追加: ボスのMeshRendererComponentを取得して、追加のアニメーションを読み込む
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& bossMr = registry.get<MeshRendererComponent>(entity);
		if (auto* renderer = Engine::Renderer::GetInstance()) {
			renderer->LoadAdditionalAnimation(bossMr.modelHandle, "Resources/Models/Animation/Boss/Mutant Idle.fbx");
			renderer->LoadAdditionalAnimation(bossMr.modelHandle, "Resources/Models/Animation/Boss/Mutant Walking.fbx");
			renderer->LoadAdditionalAnimation(bossMr.modelHandle, "Resources/Models/Animation/Boss/Mutant Left Turn 45.fbx");
			renderer->LoadAdditionalAnimation(bossMr.modelHandle, "Resources/Models/Animation/Boss/Mutant Right Turn 45.fbx");
			renderer->LoadAdditionalAnimation(bossMr.modelHandle, "Resources/Models/Animation/Boss/Mutant Dying.fbx");
		}
	}
	auto& hr = registry.get<HurtboxComponent>(entity);
	// cube.obj はベースが 2x2x2 なので、スケール値に2をかけると見た目とピッタリ一致する
	hr.size = { originalScale_.x * 2.0f, originalScale_.y * 2.0f, originalScale_.z * 2.0f };
	hr.center = {0, 0, 0}; // ボスの中心
	hr.tag = TagType::Enemy; // ★追加: 敵として判定させる

	// 3. 弱点部位（尻尾）の生成と紐付け
	tailEntity_ = registry.create();
	
	auto& tc = registry.emplace<TransformComponent>(tailEntity_);
	tc.scale = { 0.8f, 0.8f, 2.5f }; // 長細い尻尾
	
	auto& mr = registry.emplace<MeshRendererComponent>(tailEntity_);
	mr.modelPath = "Resources/Models/cube/cube.obj";
	mr.texturePath = "Resources/Textures/white1x1.png";
	mr.color = { 0.8f, 0.2f, 0.8f, 1.0f }; // 紫色
	// ★修正: Rendererからモデルとテクスチャを正しく読み込んでハンドルを取得する
	if (auto* renderer = Engine::Renderer::GetInstance()) {
		mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
		mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
	}
	
	auto& tailHr = registry.emplace<HurtboxComponent>(tailEntity_);
	// 尻尾のHurtboxもスケールに合わせて完全に一致させる
	tailHr.size = { tc.scale.x * 2.0f, tc.scale.y * 2.0f, tc.scale.z * 2.0f };
	tailHr.tag = TagType::Enemy;
	tailHr.damageMultiplier = 1.5f; // 弱点なのでダメージ1.5倍

	// ★追加: 尻尾自身にもタグを設定しないと、自爆ダメージを受けてしまう
	registry.emplace<TagComponent>(tailEntity_).tag = TagType::Enemy;
	
	auto& part = registry.emplace<BodyPartComponent>(tailEntity_);
	part.parentEntity = entity;
	part.hp = 50.0f;      // 尻尾の耐久値
	part.maxHp = 50.0f;
	part.damageMultiplierToParent = 0.3f; // 尻尾へのダメージの30%が本体にも入る
	part.partName = "Tail";
}

void BossTestScript::Update(entt::entity entity, GameScene* scene, float /*dt*/) {
	auto& registry = scene->GetRegistry();
	if (!registry.all_of<BossActionComponent>(entity)) return;

	auto& boss = registry.get<BossActionComponent>(entity);
	auto& bTc = registry.get<TransformComponent>(entity);

	// 現在の攻撃タイプを取得
	BossAttackType attackType = BossAttackType::Thrust;
	if (boss.currentPatternIndex >= 0 && boss.currentPatternIndex < boss.patterns.size()) {
		attackType = boss.patterns[boss.currentPatternIndex].type;
	}

	// --- 状態に応じたカラー変更とスケールアニメーション ---
	if (registry.all_of<MeshRendererComponent>(entity)) {
		auto& mr = registry.get<MeshRendererComponent>(entity);
		
		bool hasAnimator = registry.all_of<AnimatorComponent>(entity);
		auto* anim = hasAnimator ? &registry.get<AnimatorComponent>(entity) : nullptr;
		if (anim) {
			anim->isPlaying = true; // ★追加: 再生状態をオンにする
			anim->drawSkeleton = false;
		}
		
		auto getAnimName = [&](const std::string& prefix, const std::string& fallback) -> std::string {
			if (auto* renderer = Engine::Renderer::GetInstance()) {
				if (auto* m = renderer->GetModel(mr.modelHandle)) {
					const auto& anims = m->GetData().animations;
					for (const auto& a : anims) {
						if ((a.name.find(prefix + "_") == 0 || a.name.find(prefix) == 0) && a.name.find("mixamo.com") != std::string::npos) {
							return a.name;
						}
					}
					// Fallback to any animation with the prefix
					for (const auto& a : anims) {
						if (a.name.find(prefix + "_") == 0 || a.name.find(prefix) == 0) {
							return a.name;
						}
					}
					// If not found, return the first animation as a desperate fallback if available
					if (!anims.empty()) return anims[0].name;
				}
			}
			return fallback;
		};

		bool stateChanged = (prevBossState_ != boss.state);
		auto playAnim = [&](const std::string& animPrefix, bool loop, float speed) {
			if (!anim) return;
			std::string targetName = getAnimName(animPrefix, animPrefix);
			
			bool forceRestart = false;
			// 攻撃開始時(WindUp)や、待機・移動への移行時はアニメーションを最初から再生し直す
			if (stateChanged && (boss.state == BossState::WindUp || boss.state == BossState::Idle || boss.state == BossState::Chase || boss.state == BossState::Stunned)) {
				forceRestart = true;
			}
			
			// If playing attack animation, we don't want it to loop
			if (animPrefix == "attack1") loop = false;

			if (!targetName.empty()) {
				if (anim->currentAnimation != targetName || forceRestart) {
					// 補間（クロスフェード）用ステートの保存
					if (anim->currentAnimation != targetName && !anim->currentAnimation.empty()) {
						anim->prevAnimation = anim->currentAnimation;
						anim->prevTime = anim->time;
						anim->prevLoop = anim->loop;
						anim->crossfadeDuration = 0.2f; // 0.2秒かけて補間
						anim->crossfadeTimer = 0.2f;
					} else if (forceRestart) {
						anim->crossfadeTimer = 0.0f; // 同じアニメーションの再スタート時は補間しない
					}

					anim->currentAnimation = targetName;
					anim->time = 0.0f;
					anim->isPlaying = true;
				}
				anim->loop = loop;
				anim->speed = speed;
			}
		};


		switch (boss.state) {
		case BossState::Idle:
			mr.color = {0.8f, 0.8f, 0.8f, 1.0f}; // グレー
			if (anim) {
				if (boss.turnDirection > 0.05f) {
					playAnim("Mutant Right Turn 45", true, 1.0f);
				} else if (boss.turnDirection < -0.05f) {
					playAnim("Mutant Left Turn 45", true, 1.0f);
				} else {
					playAnim("Mutant Idle", true, 1.0f);
				}
			} else {
				bTc.scale = originalScale_;
			}
			break;
		case BossState::Chase:
			mr.color = {1.0f, 0.5f, 0.0f, 1.0f}; // オレンジ（接近中）
			if (anim) {
				playAnim("Mutant Walking", true, 0.4f); // 速度を遅く調整
			} else {
				// 歩くようなスケール運動
				bTc.scale.y = originalScale_.y + std::abs(std::sin(boss.stateTimer * 10.0f)) * 0.2f;
			}
			break;
		case BossState::WindUp:
			mr.color = {1.0f, 0.0f, 0.0f, 1.0f}; // 赤（予備動作・危険）
			if (anim) {
				// 攻撃はループさせず、速度をさらに遅くしてタイミングを合わせる
				playAnim("attack1", false, 0.4f); // search for "attack1_..." 
			} else {
				if (attackType == BossAttackType::Thrust) {
					bTc.scale.x = originalScale_.x * 1.5f;
					bTc.scale.y = originalScale_.y * 0.5f;
					bTc.scale.z = originalScale_.z * 1.5f;
				} else if (attackType == BossAttackType::TailSpin) {
					// 尻尾なぎ払いの予備動作：身体をひねる（Y軸に少し回転）
					bTc.rotate.y += 0.05f; // ジリジリと回転を溜める
					bTc.scale.y = originalScale_.y * 0.8f;
				} else if (attackType == BossAttackType::JumpPress) {
					// プレスの予備動作：上に伸び上がる
					bTc.scale.y = originalScale_.y * 1.8f;
					bTc.scale.x = originalScale_.x * 0.7f;
					bTc.scale.z = originalScale_.z * 0.7f;
				} else if (attackType == BossAttackType::Punch) {
					// パンチ予備動作
					bTc.scale.x = originalScale_.x * 1.2f;
					bTc.scale.y = originalScale_.y * 0.9f;
					bTc.scale.z = originalScale_.z * 1.2f;
				}
			}
			break;
		case BossState::Attack:
			mr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 白（攻撃中）
			if (anim) {
				// Attackステート中もWindUpと同じアニメーションを継続(forceRestartされない)
				playAnim("attack1", false, 0.4f); // search for "attack1_..."
			} else {
				if (attackType == BossAttackType::Thrust) {
					// 突進：縦に細長く伸びて前方に飛ぶ
					bTc.scale.x = originalScale_.x * 0.8f;
					bTc.scale.y = originalScale_.y * 1.5f;
					bTc.scale.z = originalScale_.z * 0.8f;
				} else if (attackType == BossAttackType::TailSpin) {
					// 回転攻撃：超高速でY軸回転
					bTc.rotate.y += 1.5f; // 高速スピン
					bTc.scale.x = originalScale_.x * 1.2f;
					bTc.scale.z = originalScale_.z * 1.2f; // 遠心力で広がる
				} else if (attackType == BossAttackType::JumpPress) {
					// ジャンププレス：空中に飛び上がり、落下する
					// CharacterMovementComponentに任せてジャンプさせる
					if (registry.all_of<CharacterMovementComponent>(entity)) {
						auto& cm = registry.get<CharacterMovementComponent>(entity);
						auto& rb = registry.get<RigidbodyComponent>(entity);
						// タイマーが始まった直後だけジャンプの初速を与える
						if (boss.stateTimer < 0.1f && cm.isGrounded) {
							rb.velocity.y = 25.0f;
							cm.isGrounded = false;
						}
					}
					bTc.scale = originalScale_; // サイズは元に戻る
				} else if (attackType == BossAttackType::Punch) {
					// パンチ：前方に飛び出す
					bTc.scale.x = originalScale_.x * 1.5f;
					bTc.scale.y = originalScale_.y * 1.5f;
					bTc.scale.z = originalScale_.z * 1.5f;
				}
			}
			break;
		case BossState::Cooldown:
			mr.color = {0.2f, 0.2f, 0.8f, 1.0f}; // 青（硬直・隙）
			if (anim) {
				playAnim("Mutant Idle", true, 1.0f); // search for "Mutant Idle_..."
			} else {
				bTc.scale = originalScale_;
			}
			break;
		case BossState::Stunned:
		case BossState::Down:
			mr.color = {0.0f, 0.5f, 1.0f, 1.0f}; // 水色（スタン・ダウン）
			if (anim) {
				playAnim("Mutant Idle", false, 1.0f); // ダウンモーションがないのでIdle
			} else {
				// 倒れる（横に90度回転）
				bTc.rotate.x = DirectX::XM_PIDIV2;
			}
			break;
		case BossState::Dead:
			mr.color = {0.3f, 0.3f, 0.3f, 1.0f}; // 黒/グレー
			if (anim) {
				playAnim("Mutant Dying", false, 0.5f); // 速度を遅くして、倒れるまでの時間を稼ぐ
			}
			
			// 死亡アニメーションが終わる頃（速度半減なので約6秒）で消滅させる
			if (boss.stateTimer >= 6.0f) {
				if (registry.valid(tailEntity_)) {
					registry.destroy(tailEntity_);
				}
				registry.emplace_or_replace<AutoDestroyComponent>(entity).timer = 0.0f; // 次のフレームで削除
				return;
			}
			break;
		}

		// スタン/ダウンから復帰したら起き上がる
		if (boss.state != BossState::Down && boss.state != BossState::Stunned) {
			bTc.rotate.x = 0.0f;
		}
	}

	// --- 尻尾（部位）の追従処理 ---
	if (registry.valid(tailEntity_)) {
		auto& tailPart = registry.get<BodyPartComponent>(tailEntity_);
		if (tailPart.isDestroyed) {
			// 破壊されたら非表示
			if (registry.all_of<MeshRendererComponent>(tailEntity_)) {
				registry.get<MeshRendererComponent>(tailEntity_).enabled = false;
			}
		} else {
			// ボスの後ろにピッタリ追従させる
			auto& tailTc = registry.get<TransformComponent>(tailEntity_);
			tailTc.rotate = bTc.rotate;
			
			float sinY = std::sin(bTc.rotate.y);
			float cosY = std::cos(bTc.rotate.y);
			float tailScaleZ = 2.5f;
			float offsetZ = -originalScale_.z - tailScaleZ; // ボスの背中に密着させる
			
			tailTc.translate.x = bTc.translate.x + offsetZ * sinY;
			tailTc.translate.y = bTc.translate.y + 1.0f; // 地面から少し浮かす
			tailTc.translate.z = bTc.translate.z + offsetZ * cosY;
			
			// 待機中は少し尻尾を振るアニメーション
			if (boss.state == BossState::Idle || boss.state == BossState::Chase) {
				tailTc.rotate.y += std::sin(boss.stateTimer * 5.0f) * 0.5f;
			}
		}
	}

	// 尻尾が破壊されているかチェック
	bool tailDestroyed = false;
	if (registry.valid(tailEntity_) && registry.all_of<BodyPartComponent>(tailEntity_)) {
		tailDestroyed = registry.get<BodyPartComponent>(tailEntity_).isDestroyed;
	}

	// --- 攻撃ごとのHitboxサイズ調整 ---
	if (registry.all_of<HitboxComponent>(entity)) {
		auto& hb = registry.get<HitboxComponent>(entity);
		if (boss.state == BossState::Attack) {
			if (attackType == BossAttackType::Thrust) {
				// 見た目の変形（細長くなる）に完全に一致させる
				hb.size = {bTc.scale.x * 2.0f, bTc.scale.y * 2.0f, bTc.scale.z * 2.0f};
				hb.center = {0, 0, 0};
			} else if (attackType == BossAttackType::TailSpin) {
				if (tailDestroyed) {
					// 尻尾がない場合は本体の見た目に完全に一致させる
					hb.size = {bTc.scale.x * 2.0f, bTc.scale.y * 2.0f, bTc.scale.z * 2.0f};
				} else {
					// 尻尾がある場合は、尻尾の旋回半径をカバーする
					// ボススケール(3) + 尻尾距離(2) + 尻尾長(2.5) = 7.5。直径15mの正方形でカバー
					hb.size = {15.0f, bTc.scale.y * 2.0f, 15.0f}; 
				}
				hb.center = {0, 0, 0}; // 中心
			} else if (attackType == BossAttackType::JumpPress) {
				// ジャンプ中：本体の見た目に完全に一致
				hb.size = {bTc.scale.x * 2.0f, bTc.scale.y * 2.0f, bTc.scale.z * 2.0f};
				hb.center = {0, 0, 0};
			} else if (attackType == BossAttackType::Punch) {
				// パンチ：前方に判定
				hb.size = {bTc.scale.x * 3.0f, bTc.scale.y * 2.0f, bTc.scale.z * 3.0f};
				hb.center = {0, 0, 2.0f};
			}
		}
	}

	// --- 着地時の衝撃波（ダメージエリア）生成 ---
	// 前フレームがAttackで、今フレームがCooldownになった瞬間を着地と判定
	if (prevBossState_ == BossState::Attack && boss.state == BossState::Cooldown) {
		if (attackType == BossAttackType::JumpPress) {
			entt::entity shockwave = registry.create();
			registry.emplace<TagComponent>(shockwave).tag = TagType::Enemy; // 敵の攻撃として扱う
			
			auto& swTc = registry.emplace<TransformComponent>(shockwave);
			swTc.translate = bTc.translate;
			swTc.translate.y = 0.1f; // 地面すれすれ
			
			auto& swMr = registry.emplace<MeshRendererComponent>(shockwave);
			swMr.modelPath = "Resources/Models/plane.obj";
			swMr.texturePath = "Resources/Textures/ripple_normal.png"; // 波紋のノーマルマップ
			swMr.shaderName = "Distortion"; // 空間の歪みシェーダーを使う
			swMr.color = {1.0f, 1.0f, 1.0f, 1.0f};
			if (auto* renderer = Engine::Renderer::GetInstance()) {
				swMr.modelHandle = renderer->LoadObjMesh(swMr.modelPath);
				swMr.textureHandle = renderer->LoadTexture2D(swMr.texturePath);
			}
			
			// ParryDistortionComponent を流用してリングを広げるアニメーション
			auto& pd = registry.emplace<ParryDistortionComponent>(shockwave);
			pd.duration = 0.6f;
			pd.startScale = 2.0f;
			pd.endScale = 15.0f; // 15mまで広がる
			pd.isBillboard = false; // 常にカメラを向かず、地面と水平に広がるようにする
			
			// 衝撃波の当たり判定
			auto& swHb = registry.emplace<HitboxComponent>(shockwave);
			// CombatSystemで動的に拡大されるため、初期サイズはstartScaleに合わせる
			swHb.size = {pd.startScale * 2.0f, 4.0f, pd.startScale * 2.0f}; 
			swHb.center = {0, 2.0f, 0};
			swHb.damage = 45.0f;
			swHb.tag = TagType::Enemy;
			swHb.isActive = true; // 出現と同時に攻撃判定
			
			// エフェクト終了と同時にエンティティを消す
			registry.emplace<AutoDestroyComponent>(shockwave).timer = pd.duration;

			// カメラシェイク
			if (scene->GetContext().camera) {
				scene->GetContext().camera->StartShake(0.4f, 0.5f); // 激しく揺らす
			}
		}
	}

	// 状態を保存
	prevBossState_ = boss.state;
}

void BossTestScript::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	// 本体が消えたら尻尾も消す
	if (scene && scene->GetRegistry().valid(tailEntity_)) {
		scene->DestroyObject(static_cast<uint32_t>(entt::to_entity(tailEntity_)));
	}
}

} // namespace Game
