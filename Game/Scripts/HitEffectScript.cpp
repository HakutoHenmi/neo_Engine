#include "HitEffectScript.h"
#include "GameScene.h"
#include "../ObjectTypes.h"
#include "ScriptEngine.h"
#include <random>
#include <cmath>

namespace Game {

void HitEffectScript::Start(entt::entity entity, GameScene* scene) {
	auto& registry = scene->GetRegistry();
	
	DirectX::XMFLOAT3 pos = {0, 0, 0};
	if (registry.all_of<TransformComponent>(entity)) {
		pos = registry.get<TransformComponent>(entity).translate;
	}

	std::mt19937 mt(std::random_device{}());
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
	std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

	Engine::Vector3 ePos = {pos.x, pos.y + 0.5f, pos.z};
	Engine::Vector3 eDir = {attackDirX_, 1.0f, attackDirZ_};
	
	// 攻撃の強さや種類（近接・遠距離・液体スプラッター）に応じて色と放出量を変える
	Engine::Vector4 eColor = { 0.0f, 0.8f, 1.0f, 1.0f }; // デフォルト水色
	int emitCount = 400;
	float speedMult = 1.0f;
	
	if (isExplosion_ || isExplosionHit_) {
		emitCount = 1200;
		eColor = { 1.0f, 0.2f, 1.0f, 1.0f }; // 爆発は紫/ピンク系
		speedMult = 3.0f;
	} else if (isLiquidSplatter_) {
		emitCount = 500; // パーティクル数を調整
		eColor = (colorDist(mt) > 0.5f) ? Engine::Vector4{ 0.2f, 0.8f, 1.0f, 1.0f } : Engine::Vector4{ 0.8f, 0.9f, 1.0f, 1.0f }; // 水色と白波の色
		
		// 敵に当たった後、後ろに突き抜けるのではなく、手前や周囲に跳ね返るようにベクトルを反転・拡散
		eDir.x = -attackDirX_ * 0.5f; 
		eDir.z = -attackDirZ_ * 0.5f;
		eDir.y = 1.0f; // 少し上へ跳ねる
		
		speedMult = 2.5f; // 跳ね返りの勢い
	} else if (isMelee_) {
		emitCount = 800; // 近接は激しく飛び散る
		eColor = { 1.0f, 0.0f, 1.0f, 1.0f }; // 紫色っぽく
		speedMult = 2.0f;
	} else {
		emitCount = 200; // 遠距離ヒット時は少しだけ
		eColor = { 0.0f, 1.0f, 1.0f, 1.0f }; // シアン
		speedMult = 1.5f;
	}
	
	eDir.x *= speedMult;
	eDir.y *= speedMult;
	eDir.z *= speedMult;

	if (scene && scene->GetRenderer()) {
		scene->GetRenderer()->EmitGPUFluid(ePos, eDir, eColor, emitCount, 1.0f); // 1.0f は水しぶき(引力に引かれない)
	}
}

void HitEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	
	auto& registry = scene->GetRegistry();



	// 破片の独自物理・バウンド・破裂処理
	float gravity = 25.0f;
	std::vector<SlimeFragmentData> newTrails;
	for (auto& fd : fragments_) {
		if (fd.entity == entt::null && !fd.isLiquidParticle) continue;



		if (fd.isTentacle) {
			fd.life -= dt;
			if (fd.life <= 0.0f || !registry.valid(fd.entity)) {
				if (scene && registry.valid(fd.entity)) {
					scene->DestroyObject(static_cast<uint32_t>(fd.entity));
				}
				fd.entity = entt::null;
				continue;
			}
			auto& tc = registry.get<TransformComponent>(fd.entity);
			
			fd.angle += fd.angularSpeed * dt;
			// 螺旋の中心が進む
			fd.cx += fd.axisX * fd.speedAlongAxis * dt;
			fd.cy += fd.axisY * fd.speedAlongAxis * dt;
			fd.cz += fd.axisZ * fd.speedAlongAxis * dt;
			
			// リング状に一気に広がる
			if (fd.radius < 4.5f) {
				fd.radius += dt * 15.0f;
				if (fd.radius > 4.5f) fd.radius = 4.5f;
			}
			
			// 回転平面の基底ベクトルを計算
			Engine::Vector3 axis = { fd.axisX, fd.axisY, fd.axisZ };
			Engine::Vector3 up = { 0, 1, 0 };
			if (std::abs(axis.y) > 0.9f) up = { 1, 0, 0 };
			
			Engine::Vector3 u = {
				up.y * axis.z - up.z * axis.y,
				up.z * axis.x - up.x * axis.z,
				up.x * axis.y - up.y * axis.x
			};
			float ulen = std::sqrt(u.x*u.x + u.y*u.y + u.z*u.z);
			if (ulen > 0.001f) { u.x /= ulen; u.y /= ulen; u.z /= ulen; }
			
			Engine::Vector3 v = {
				axis.y * u.z - axis.z * u.y,
				axis.z * u.x - axis.x * u.z,
				axis.x * u.y - axis.y * u.x
			};
			
			float c_val = std::cos(fd.angle) * fd.radius;
			float s_val = std::sin(fd.angle) * fd.radius;
			
			tc.translate.x = fd.cx + u.x * c_val + v.x * s_val;
			tc.translate.y = fd.cy + u.y * c_val + v.y * s_val;
			tc.translate.z = fd.cz + u.z * c_val + v.z * s_val;
			
			// 破裂（ポップ）アニメーション：消える直前で急激に膨らんで透明になる
			if (fd.life < 0.15f) {
				float pop = 1.0f - (fd.life / 0.15f);
				float popScale = fd.baseScale * (1.0f + pop * 3.0f);
				tc.scale = { popScale, popScale, popScale };
				if (registry.all_of<MeshRendererComponent>(fd.entity)) {
					auto& mr = registry.get<MeshRendererComponent>(fd.entity);
					mr.color.w = 0.99f * (1.0f - pop);
				}
			} else {
				// ★残像の生成（星形スライムメッシュを置く）
				fd.vx += dt; // タイマーとして利用
				if (fd.vx > 0.04f) { // 秒間25個
					fd.vx = 0.0f;
					auto trail = scene->CreateEntity("SlimeTentacleTrail");
					auto& ttc = registry.get<TransformComponent>(trail);
					ttc.translate = tc.translate;
					ttc.scale = tc.scale;
					
					auto& tmr = registry.emplace<MeshRendererComponent>(trail);
					auto& parent_mr = registry.get<MeshRendererComponent>(fd.entity);
					tmr.color = parent_mr.color;
					tmr.modelPath = parent_mr.modelPath;
					tmr.texturePath = parent_mr.texturePath;
					tmr.shaderName = parent_mr.shaderName;
					tmr.modelHandle = parent_mr.modelHandle;
					tmr.textureHandle = parent_mr.textureHandle;
					
					auto& thb = registry.emplace<HitboxComponent>(trail);
					thb.isActive = false;
					thb.isProjectile = true; // これにより星形メッシュに置換される

					SlimeFragmentData td;
					td.entity = trail;
					td.isTentacleTrail = true;
					td.maxLife = 0.3f; // 残像が消えるまでの時間を少し短く
					td.life = td.maxLife;
					td.baseScale = tc.scale.x;
					newTrails.push_back(td);
				}
			}
			continue;
		}

		if (fd.isTentacleTrail) {
			fd.life -= dt;
			if (fd.life <= 0.0f || !registry.valid(fd.entity)) {
				if (scene && registry.valid(fd.entity)) {
					scene->DestroyObject(static_cast<uint32_t>(fd.entity));
				}
				fd.entity = entt::null;
				continue;
			}
			auto& tc = registry.get<TransformComponent>(fd.entity);
			float s = fd.baseScale * (fd.life / fd.maxLife); // 徐々に小さくなる
			tc.scale = { s, s, s };
			tc.rotate.y += 2.0f * dt; // 少し回転させる
			continue;
		}

		if (fd.isExplosionSpike) {
			fd.life -= dt;
			if (fd.life <= 0.0f || !registry.valid(fd.entity)) {
				if (scene && registry.valid(fd.entity)) {
					scene->DestroyObject(static_cast<uint32_t>(fd.entity));
				}
				fd.entity = entt::null;
				continue;
			}
			auto& tc = registry.get<TransformComponent>(fd.entity);
			
			// 高速で直進
			tc.translate.x += fd.vx * dt;
			tc.translate.y += fd.vy * dt;
			tc.translate.z += fd.vz * dt;
			
			// 残像生成 (少し細くする)
			auto trail = scene->CreateEntity("ExplosionSpikeTrail");
			auto& ttc = registry.get<TransformComponent>(trail);
			ttc.translate = tc.translate;
			ttc.scale = { tc.scale.x * 0.8f, tc.scale.y * 0.8f, tc.scale.z * 0.8f };
			
			if (registry.all_of<MeshRendererComponent>(fd.entity)) {
				auto& tmr = registry.emplace<MeshRendererComponent>(trail);
				auto& parent_mr = registry.get<MeshRendererComponent>(fd.entity);
				tmr.color = parent_mr.color;
				tmr.modelPath = parent_mr.modelPath;
				tmr.texturePath = parent_mr.texturePath;
				tmr.shaderName = parent_mr.shaderName;
				tmr.modelHandle = parent_mr.modelHandle;
				tmr.textureHandle = parent_mr.textureHandle;
			}
			
			SlimeFragmentData td;
			td.entity = trail;
			td.isTentacleTrail = true; // 残像として登録（縮んで消える処理に乗せる）
			td.maxLife = 0.2f; // 一瞬で消える
			td.life = td.maxLife;
			td.baseScale = ttc.scale.x;
			newTrails.push_back(td);
			
			continue;
		}

		if (registry.valid(fd.entity) && registry.all_of<TransformComponent>(fd.entity)) {
			auto& tc = registry.get<TransformComponent>(fd.entity);
			
			fd.life -= dt;
			if (fd.life <= 0.0f) {
				// 寿命が尽きたらエンティティを破棄
				if (scene) {
					scene->DestroyObject(static_cast<uint32_t>(fd.entity));
				}
				fd.entity = entt::null;
				continue;
			}

			// 重力適用
			fd.vy -= gravity * dt;
			
			// 座標更新
			tc.translate.x += fd.vx * dt;
			tc.translate.y += fd.vy * dt;
			tc.translate.z += fd.vz * dt;
			
			// 回転
			tc.rotate.x += 5.0f * dt;
			tc.rotate.y += 5.0f * dt;
			tc.rotate.z += 5.0f * dt;

			// 地面（y=0.2f付近）でバウンド
			if (tc.translate.y < 0.2f) {
				tc.translate.y = 0.2f;
				if (fd.vy < -2.0f) {
					fd.vy = -fd.vy * 0.4f; // 弾性（少し跳ねる）
					fd.vx *= 0.7f; // 摩擦
					fd.vz *= 0.7f;
				} else {
					fd.vy = 0.0f;
					fd.vx *= 0.8f;
					fd.vz *= 0.8f;
				}
			}

			// 破裂（ポップ）アニメーション：消える直前の0.15秒で急激に膨らんで透明になる
			if (fd.life < 0.15f) {
				float pop = 1.0f - (fd.life / 0.15f); // 0.0 -> 1.0
				float popScale = fd.baseScale * (1.0f + pop * 3.0f); // 4倍に膨らむ
				tc.scale = { popScale, popScale, popScale };

				if (registry.all_of<MeshRendererComponent>(fd.entity)) {
					auto& mr = registry.get<MeshRendererComponent>(fd.entity);
					mr.color.w = 1.0f - pop; // フェードアウト
				}
			}
		}
	}

	for (const auto& td : newTrails) {
		fragments_.push_back(td);
	}

	if (timer_ >= duration_) {
		// スクリプト削除前に、残っているすべての破片を強制的に削除する
		if (scene) {
			for (auto& fd : fragments_) {
				if (fd.entity != entt::null && registry.valid(fd.entity)) {
					scene->DestroyObject(static_cast<uint32_t>(fd.entity));
				}
			}
		}
		fragments_.clear();

		if (scene) {
			scene->DestroyObject(static_cast<uint32_t>(entity));
		}
	}
}

void HitEffectScript::OnDestroy(entt::entity /*entity*/, GameScene* scene) {
	if (scene) {
		auto& registry = scene->GetRegistry();
		for (auto& fd : fragments_) {
			if (fd.entity != entt::null && registry.valid(fd.entity)) {
				scene->DestroyObject(static_cast<uint32_t>(fd.entity));
			}
		}
	}
	fragments_.clear();
}

std::string HitEffectScript::SerializeParameters() {
	if (isExplosion_) return "isExplosion=1";
	if (isExplosionHit_) return "isExplosionHit=1";
	return isMelee_ ? "isMelee=1" : "{}";
}

void HitEffectScript::DeserializeParameters(const std::string& data) {
	if (data.find("isExplosion=1") != std::string::npos) {
		isExplosion_ = true;
	} else if (data.find("isExplosionHit=1") != std::string::npos) {
		isExplosionHit_ = true;
	} else if (data.find("isLiquidSplatter=1") != std::string::npos) {
		isLiquidSplatter_ = true; // ★追加
		size_t posX = data.find("dirX=");
		if (posX != std::string::npos) {
			size_t comma = data.find(',', posX);
			attackDirX_ = std::stof(data.substr(posX + 5, comma - (posX + 5)));
		}
		size_t posZ = data.find("dirZ=");
		if (posZ != std::string::npos) {
			size_t comma = data.find(',', posZ);
			attackDirZ_ = std::stof(data.substr(posZ + 5, comma - (posZ + 5)));
		}
	} else if (data.find("isMelee=1") != std::string::npos) {
		isMelee_ = true;
		
		size_t posX = data.find("dirX=");
		if (posX != std::string::npos) {
			size_t comma = data.find(',', posX);
			attackDirX_ = std::stof(data.substr(posX + 5, comma - (posX + 5)));
		}
		size_t posZ = data.find("dirZ=");
		if (posZ != std::string::npos) {
			size_t comma = data.find(',', posZ);
			attackDirZ_ = std::stof(data.substr(posZ + 5, comma - (posZ + 5)));
		}
	}
}

REGISTER_SCRIPT(HitEffectScript);

} // namespace Game
