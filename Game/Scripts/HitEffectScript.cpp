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

	if (isExplosion_) {
		// --- 案1: トゲトゲ大爆発専用エフェクト ---
		// 全方位に多数の触手（直線的に伸びるトゲ）を一斉に放射する
		int spikeCount = 30; // 30本のトゲ
		for (int i = 0; i < spikeCount; ++i) {
			auto tentacle = scene->CreateEntity("ExplosionSpike");
			auto& tc = registry.get<TransformComponent>(tentacle);
			tc.translate = {pos.x, pos.y + 1.0f, pos.z}; // 少し上から
			tc.scale = { 0.4f, 0.4f, 0.4f }; // 先端部分
			
			auto& mr = registry.emplace<MeshRendererComponent>(tentacle);
			// ランダムな紫色・水色
			if (colorDist(mt) > 0.5f) mr.color = { 0.0f, 1.0f, 1.0f, 0.99f }; 
			else mr.color = { 1.0f, 0.0f, 1.0f, 0.99f }; 
			
			mr.modelPath = "Resources/Models/cube/cube.obj";
			mr.texturePath = "Resources/Models/cube/white1x1.png";
			mr.shaderName = "SlimeNoFaceNoDepth"; 
			if (scene->GetRenderer()) {
				mr.modelHandle = scene->GetRenderer()->LoadObjMesh(mr.modelPath);
				mr.textureHandle = scene->GetRenderer()->LoadTexture2D(mr.texturePath);
			}

			// 球面上にランダムな方向を計算
			float u = dist(mt);
			float theta = colorDist(mt) * 2.0f * 3.14159f;
			float r = std::sqrt(1.0f - u * u);
			float dirX = r * std::cos(theta);
			float dirY = r * std::sin(theta) + 0.5f; // やや上向きに補正
			float dirZ = u;
			
			float len = std::sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ);
			dirX /= len; dirY /= len; dirZ /= len;

			float speed = 25.0f + std::abs(dist(mt)) * 10.0f; // 高速で飛び出す

			SlimeFragmentData fd;
			fd.entity = tentacle;
			fd.isTentacle = false; // らせん軌道ではない
			fd.isTentacleTrail = false; // ★修正: 残像として消滅するだけの処理を回避
			fd.isExplosionSpike = true; // ★追加: 爆発トゲとしての更新を行う
			fd.vx = dirX * speed;
			fd.vy = dirY * speed;
			fd.vz = dirZ * speed;
			fd.maxLife = 0.3f + std::abs(dist(mt)) * 0.2f; // 短い寿命（一瞬で伸びる）
			fd.life = fd.maxLife;
			fd.baseScale = tc.scale.x;
			fragments_.push_back(fd);
		}
	} else if (isExplosionHit_) {
		// --- 爆発ヒット時の専用エフェクト ---
		// 敵の体からスライムのトゲが複数本突き刺さって弾ける演出
		int spikeCount = 6; 
		for (int i = 0; i < spikeCount; ++i) {
			auto spike = scene->CreateEntity("ExplosionHitSpike");
			auto& tc = registry.get<TransformComponent>(spike);
			tc.translate = pos; 
			tc.scale = { 0.4f, 0.4f, 0.4f }; 
			
			auto& mr = registry.emplace<MeshRendererComponent>(spike);
			if (colorDist(mt) > 0.5f) mr.color = { 0.0f, 1.0f, 1.0f, 0.99f }; 
			else mr.color = { 1.0f, 0.0f, 1.0f, 0.99f }; 
			
			mr.modelPath = "Resources/Models/cube/cube.obj";
			mr.texturePath = "Resources/Models/cube/white1x1.png";
			mr.shaderName = "SlimeNoFaceNoDepth"; 
			if (scene->GetRenderer()) {
				mr.modelHandle = scene->GetRenderer()->LoadObjMesh(mr.modelPath);
				mr.textureHandle = scene->GetRenderer()->LoadTexture2D(mr.texturePath);
			}

			float dirX = dist(mt);
			float dirY = dist(mt);
			float dirZ = dist(mt);
			float len = std::sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ);
			if (len > 0.001f) { dirX /= len; dirY /= len; dirZ /= len; }

			float speed = 12.0f + std::abs(dist(mt)) * 6.0f;

			SlimeFragmentData fd;
			fd.entity = spike;
			fd.isTentacle = false; 
			fd.isTentacleTrail = false; 
			fd.isExplosionSpike = true; // 残像付きの直進トゲ
			fd.vx = dirX * speed;
			fd.vy = dirY * speed;
			fd.vz = dirZ * speed;
			fd.maxLife = 0.2f + std::abs(dist(mt)) * 0.1f;
			fd.life = fd.maxLife;
			fd.baseScale = tc.scale.x;
			fragments_.push_back(fd);
		}
	} else if (isMelee_) {
		// --- リング波状の粘液の触手エフェクト ---
		int tentacleCount = 12; // たくさん並べる

		// 攻撃方向が指定されている場合はその方向を向き（法線）とする
		float ax = attackDirX_;
		float az = attackDirZ_;
		if (std::abs(ax) < 0.001f && std::abs(az) < 0.001f) {
			float ringYaw = colorDist(mt) * 2.0f * 3.14159f;
			ax = std::cos(ringYaw);
			az = std::sin(ringYaw);
		} else {
			float len = std::sqrt(ax*ax + az*az);
			ax /= len;
			az /= len;
		}

		for (int i = 0; i < tentacleCount; ++i) {
			auto tentacle = scene->CreateEntity("SlimeTentacle");
			auto& tc = registry.get<TransformComponent>(tentacle);
			tc.translate = pos;
			tc.scale = { 0.5f, 0.5f, 0.5f }; // ヘッド部分
			
			auto& mr = registry.emplace<MeshRendererComponent>(tentacle);
			Engine::Vector4 color = (i % 2 == 0) ? Engine::Vector4{0.0f, 1.0f, 1.0f, 0.99f} : Engine::Vector4{1.0f, 0.0f, 1.0f, 0.99f};
			mr.color = { color.x, color.y, color.z, color.w };
			mr.modelPath = "Resources/Models/cube/cube.obj";
			mr.texturePath = "Resources/Models/cube/white1x1.png";
			mr.shaderName = "SlimeNoFaceNoDepth"; 
			if (scene->GetRenderer()) {
				mr.modelHandle = scene->GetRenderer()->LoadObjMesh(mr.modelPath);
				mr.textureHandle = scene->GetRenderer()->LoadTexture2D(mr.texturePath);
			}

			auto& hb = registry.emplace<HitboxComponent>(tentacle);
			hb.isActive = false;
			hb.isProjectile = true; // これによりGameSceneで星形に置換される

			SlimeFragmentData fd;
			fd.entity = tentacle;
			fd.isTentacle = true;
			fd.vx = 0.0f; // 残像生成タイマーとして流用
			fd.cx = pos.x;
			fd.cy = pos.y; 
			fd.cz = pos.z;
			
			// 円周上に配置
			fd.angle = ((float)i / tentacleCount) * 2.0f * 3.14159f; 
			
			// 回転速度 (緩やかに波打つ)
			fd.angularSpeed = 2.0f; 
			
			// 初期半径
			fd.radius = 0.5f; 
			
			// 90度立てた軸を設定
			fd.axisX = ax;
			fd.axisY = 0.0f;
			fd.axisZ = az;
			
			// 波状のバリエーション用
			float wave = std::sin(fd.angle * 3.0f);

			// 自分の方や敵の後ろに進まないように、軸方向への進行速度を0にする
			fd.speedAlongAxis = 0.0f; 
			
			// 全体が消えるまでの寿命
			fd.maxLife = 0.4f + std::abs(wave) * 0.1f;
			fd.life = fd.maxLife;
			fd.baseScale = tc.scale.x;
			fragments_.push_back(fd);
		}
	} else {
		// --- 遠距離用: 直線的に飛び散る破片と飛沫 ---
		int fragmentCount = 18; 
		for (int i = 0; i < fragmentCount; ++i) {
			auto frag = scene->CreateEntity("SlimeFragment");
			auto& tc = registry.get<TransformComponent>(frag);
			tc.translate = pos;
			float s = 0.15f + std::abs(dist(mt)) * 0.15f;
			tc.scale = { s, s, s };
			tc.rotate = { dist(mt) * 3.14f, dist(mt) * 3.14f, dist(mt) * 3.14f };

			auto& mr = registry.emplace<MeshRendererComponent>(frag);
			if (colorDist(mt) > 0.5f) mr.color = { 0.0f, 1.0f, 1.0f, 0.99f }; 
			else mr.color = { 1.0f, 0.0f, 1.0f, 0.99f }; 
			
			mr.modelPath = "Resources/Models/cube/cube.obj";
			mr.texturePath = "Resources/Models/cube/white1x1.png";
			mr.shaderName = "SlimeNoFaceNoDepth"; 
			if (scene->GetRenderer()) {
				mr.modelHandle = scene->GetRenderer()->LoadObjMesh(mr.modelPath);
				mr.textureHandle = scene->GetRenderer()->LoadTexture2D(mr.texturePath);
			}

			float speed = 12.0f + std::abs(dist(mt)) * 8.0f;
			Engine::Vector3 dir = { dist(mt), std::abs(dist(mt)) + 0.4f, dist(mt) };
			float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
			
			auto& pe = registry.emplace<ParticleEmitterComponent>(frag);
			pe.emitter.params.name = "FragTrail";
			pe.emitter.params.emitRate = 30; 
			pe.emitter.params.lifeTime = 0.2f; 
			pe.emitter.params.startColor = { mr.color.x, mr.color.y, mr.color.z, 0.8f };
			pe.emitter.params.endColor = { mr.color.x, mr.color.y, mr.color.z, 0.0f };
			pe.emitter.params.startSize = { s * 0.5f, s * 0.5f, s * 0.5f }; 
			pe.emitter.params.endSize = { 0.0f, 0.0f, 0.0f };
			pe.emitter.params.useBillboard = false; 
			pe.emitter.params.isAdditive = false; 
			pe.emitter.params.texturePath = "Resources/Textures/white1x1.png"; 

			SlimeFragmentData fd;
			fd.entity = frag;
			fd.vx = (dir.x / len) * speed;
			fd.vy = (dir.y / len) * speed;
			fd.vz = (dir.z / len) * speed;
			fd.maxLife = 0.6f + std::abs(dist(mt)) * 0.4f;
			fd.life = fd.maxLife;
			fd.baseScale = s;
			fragments_.push_back(fd); 

			auto& hb = registry.emplace<HitboxComponent>(frag);
			hb.isActive = false;
			hb.isProjectile = true;
		}

		int burstCount = 40; 
		for (int i = 0; i < burstCount; ++i) {
			entt::entity bubble = scene->CreateEntity("SlimeBubble");
			auto& tc = registry.get<TransformComponent>(bubble);
			tc.translate = pos;
			float s = 0.05f + std::abs(dist(mt)) * 0.05f; 
			tc.scale = { s, s, s };
			tc.rotate = { dist(mt)*3.14f, dist(mt)*3.14f, dist(mt)*3.14f };

			auto& mr = registry.emplace<MeshRendererComponent>(bubble);
			mr.color = { 0.0f, 1.0f, 1.0f, 0.99f }; 

			Engine::Vector3 dir = { dist(mt), dist(mt), dist(mt) };
			float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
			if (len < 0.001f) { dir.y = 1.0f; len = 1.0f; }
			float speed = 15.0f + std::abs(dist(mt)) * 15.0f; 

			SlimeFragmentData fd;
			fd.entity = bubble;
			fd.vx = (dir.x / len) * speed;
			fd.vy = (dir.y / len) * speed;
			fd.vz = (dir.z / len) * speed;
			fd.maxLife = 0.3f + std::abs(dist(mt)) * 0.2f; 
			fd.life = fd.maxLife;
			fd.baseScale = s;
			fragments_.push_back(fd); 

			auto& hb = registry.emplace<HitboxComponent>(bubble);
			hb.isActive = false;
			hb.isProjectile = true;
		}
	}


}

void HitEffectScript::Update(entt::entity entity, GameScene* scene, float dt) {
	timer_ += dt;
	
	auto& registry = scene->GetRegistry();



	// 破片の独自物理・バウンド・破裂処理
	float gravity = 25.0f;
	std::vector<SlimeFragmentData> newTrails;
	for (auto& fd : fragments_) {
		if (fd.entity == entt::null) continue;

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
