#include "BossTestScript.h"
#include "ScriptEngine.h"
#include "../Engine/Renderer.h"
#include <cmath>

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
		// パターン1: 突進攻撃
		BossActionPattern thrust;
		thrust.name = "Thrust";
		thrust.type = BossAttackType::Thrust;
		thrust.windUpDuration = 1.0f;
		thrust.activeDuration = 0.5f;
		thrust.recoveryDuration = 1.5f;
		thrust.range = 7.0f;
		thrust.damage = 25.0f;
		thrust.thrustForce = 15.0f;
		boss.patterns.push_back(thrust);

		// パターン2: 大回転尻尾なぎ払い
		BossActionPattern tailSpin;
		tailSpin.name = "TailSpin";
		tailSpin.type = BossAttackType::TailSpin;
		tailSpin.windUpDuration = 1.2f;
		tailSpin.activeDuration = 0.6f;
		tailSpin.recoveryDuration = 2.0f;
		tailSpin.range = 5.0f;
		tailSpin.damage = 35.0f;
		tailSpin.thrustForce = 0.0f; // 回転なので前進しない
		boss.patterns.push_back(tailSpin);

		// パターン3: 飛びかかりプレス（衝撃波）
		BossActionPattern jumpPress;
		jumpPress.name = "JumpPress";
		jumpPress.type = BossAttackType::JumpPress;
		jumpPress.windUpDuration = 1.5f; // 長い予備動作
		jumpPress.activeDuration = 0.8f; // ジャンプ時間
		jumpPress.recoveryDuration = 2.5f; // 大きな隙
		jumpPress.range = 10.0f; // 遠くからでも飛んでくる
		jumpPress.damage = 40.0f;
		jumpPress.thrustForce = 12.0f; // ジャンプの前進力
		boss.patterns.push_back(jumpPress);
	}

	// 初期スケールを保存しておく
	if (registry.all_of<TransformComponent>(entity)) {
		originalScale_ = registry.get<TransformComponent>(entity).scale;
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
		hc.hp = 500.0f;
		hc.maxHp = 500.0f;
	}

	// ★追加: ボス本体のHurtboxのサイズを見た目のスケールに完全に一致させる
	if (registry.all_of<HurtboxComponent>(entity)) {
		auto& hr = registry.get<HurtboxComponent>(entity);
		// cube.obj はベースが 2x2x2 なので、スケール値に2をかけると見た目とピッタリ一致する
		hr.size = { originalScale_.x * 2.0f, originalScale_.y * 2.0f, originalScale_.z * 2.0f };
		hr.center = {0, 0, 0}; // ボスの中心
	}

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
	
	auto& hr = registry.emplace<HurtboxComponent>(tailEntity_);
	// 尻尾のHurtboxもスケールに合わせて完全に一致させる
	hr.size = { tc.scale.x * 2.0f, tc.scale.y * 2.0f, tc.scale.z * 2.0f };
	hr.tag = TagType::Enemy;
	hr.damageMultiplier = 1.5f; // 弱点なのでダメージ1.5倍

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
		
		// アニメーターがある場合はアニメーション名を設定、ない場合はプロシージャル変形
		bool hasAnimator = registry.all_of<AnimatorComponent>(entity);
		auto* anim = hasAnimator ? &registry.get<AnimatorComponent>(entity) : nullptr;
		
		switch (boss.state) {
		case BossState::Idle:
			mr.color = {0.8f, 0.8f, 0.8f, 1.0f}; // グレー
			if (anim) anim->currentAnimation = "Idle";
			else bTc.scale = originalScale_;
			break;
		case BossState::Chase:
			mr.color = {1.0f, 0.5f, 0.0f, 1.0f}; // オレンジ（接近中）
			if (anim) {
				anim->currentAnimation = "Walk";
			} else {
				// 歩くような上下運動
				bTc.translate.y = 1.0f + std::abs(std::sin(boss.stateTimer * 10.0f)) * 0.5f;
			}
			break;
		case BossState::WindUp:
			mr.color = {1.0f, 0.0f, 0.0f, 1.0f}; // 赤（予備動作・危険）
			if (anim) {
				anim->currentAnimation = "Attack_WindUp"; // 攻撃モーションの準備部分
			} else {
				if (attackType == BossAttackType::Thrust) {
					// 突進の予備動作：力を溜めるように横に広がり縦に縮む
					bTc.scale.x = originalScale_.x * 1.5f;
					bTc.scale.y = originalScale_.y * 0.5f;
					bTc.scale.z = originalScale_.z * 1.5f;
					bTc.translate.y = 1.0f;
				} else if (attackType == BossAttackType::TailSpin) {
					// 尻尾なぎ払いの予備動作：身体をひねる（Y軸に少し回転）
					bTc.rotate.y += 0.05f; // ジリジリと回転を溜める
					bTc.scale.y = originalScale_.y * 0.8f;
				} else if (attackType == BossAttackType::JumpPress) {
					// プレスの予備動作：上に伸び上がる
					bTc.scale.y = originalScale_.y * 1.8f;
					bTc.scale.x = originalScale_.x * 0.7f;
					bTc.scale.z = originalScale_.z * 0.7f;
				}
			}
			break;
		case BossState::Attack:
			mr.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 白（攻撃中）
			if (anim) {
				anim->currentAnimation = "Attack";
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
					// activeDuration中に放物線を描く
					float t = boss.stateTimer / boss.patterns[boss.currentPatternIndex].activeDuration;
					// 0->1 の時間で、sin(t*pi) によってジャンプ弧を描く
					float jumpHeight = 8.0f;
					bTc.translate.y = 1.5f + std::sin(t * DirectX::XM_PI) * jumpHeight;
					bTc.scale = originalScale_; // サイズは元に戻る
				}
			}
			break;
		case BossState::Cooldown:
			mr.color = {0.2f, 0.2f, 0.8f, 1.0f}; // 青（硬直・隙）
			if (anim) {
				anim->currentAnimation = "Idle"; // 隙はIdleなどで代用
			} else {
				bTc.scale = originalScale_;
			}
			break;
		case BossState::Stunned:
		case BossState::Down:
			mr.color = {0.0f, 0.5f, 1.0f, 1.0f}; // 水色（スタン・ダウン）
			if (anim) {
				anim->currentAnimation = "Down";
			} else {
				// 倒れる（横に90度回転）
				bTc.rotate.x = DirectX::XM_PIDIV2;
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
			swHb.damage = 30.0f;
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
		scene->GetRegistry().destroy(tailEntity_);
	}
}

} // namespace Game
