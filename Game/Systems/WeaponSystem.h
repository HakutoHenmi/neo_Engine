#pragma once
#include "ISystem.h"
#include "../ObjectTypes.h"
#include "../Engine/Renderer.h"
#include "../Systems/PlayerActionSystem.h" // ★追加
#include <cmath>
#include <vector>

namespace Game {

class WeaponSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<PlayerWeaponComponent, TransformComponent, PlayerActionComponent>();
		for (auto entity : view) {
			auto& pw = view.get<PlayerWeaponComponent>(entity);
			auto& ptc = view.get<TransformComponent>(entity);
			auto& pa = view.get<PlayerActionComponent>(entity);

			// --- 武器の切り替え入力（1, 2, 3キー） ---
			if (GetAsyncKeyState('1') & 0x8000) ChangeWeapon(registry, pw, WeaponType::Greatsword);
			if (GetAsyncKeyState('2') & 0x8000) ChangeWeapon(registry, pw, WeaponType::DualBlades);
			if (GetAsyncKeyState('3') & 0x8000) ChangeWeapon(registry, pw, WeaponType::MultiBlades);

			// --- 武器エンティティの初期化 ---
			InitializeWeapons(registry, pw);

			// --- 武器のプロシージャルアニメーション（追従・攻撃動作） ---
			UpdateWeaponAnimation(registry, pw, ptc, pa);
		}
	}

	void Reset(entt::registry& registry) override {
		// 武器エンティティを一旦すべて削除
		auto view = registry.view<PlayerWeaponComponent>();
		for (auto entity : view) {
			auto& pw = registry.get<PlayerWeaponComponent>(entity);
			for (auto we : pw.weaponEntities) {
				if (registry.valid(we)) registry.destroy(we);
			}
			pw.weaponEntities.clear();
		}
	}

private:
	void ChangeWeapon(entt::registry& registry, PlayerWeaponComponent& pw, WeaponType newType) {
		if (pw.currentWeapon == newType) return;
		pw.currentWeapon = newType;
		
		// 既存の武器モデルを削除して再生成させる
		for (auto we : pw.weaponEntities) {
			if (registry.valid(we)) registry.destroy(we);
		}
		pw.weaponEntities.clear();
	}

	void InitializeWeapons(entt::registry& registry, PlayerWeaponComponent& pw) {
		if (!pw.weaponEntities.empty()) return; // 既に生成済みならスキップ

		auto* renderer = Engine::Renderer::GetInstance();

		if (pw.currentWeapon == WeaponType::Greatsword) {
			entt::entity sword = registry.create();
			auto& tc = registry.emplace<TransformComponent>(sword);
			tc.scale = { 0.6f, 3.5f, 1.2f }; // 分厚く長い
			auto& mr = registry.emplace<MeshRendererComponent>(sword);
			mr.modelPath = "Resources/Models/cube/cube.obj";
			mr.texturePath = "Resources/Textures/white1x1.png";
			mr.color = { 0.7f, 0.7f, 0.7f, 1.0f }; // 鉄の色
			if (renderer) {
				mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
				mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
			}
			auto& tag = registry.emplace<TagComponent>(sword);
			tag.tag = TagType::Player; // レイキャスト回避
			pw.weaponEntities.push_back(sword);
		}
		else if (pw.currentWeapon == WeaponType::DualBlades) {
			for (int i = 0; i < 2; ++i) {
				entt::entity blade = registry.create();
				auto& tc = registry.emplace<TransformComponent>(blade);
				tc.scale = { 0.2f, 1.5f, 0.4f }; // 短く鋭い
				auto& mr = registry.emplace<MeshRendererComponent>(blade);
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.color = { 0.4f, 0.8f, 1.0f, 1.0f }; // 水色
				if (renderer) {
					mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				}
				auto& tag = registry.emplace<TagComponent>(blade);
				tag.tag = TagType::Player; // レイキャスト回避
				pw.weaponEntities.push_back(blade);
			}
		}
		else if (pw.currentWeapon == WeaponType::MultiBlades) {
			for (int i = 0; i < 4; ++i) {
				entt::entity fragment = registry.create();
				auto& tc = registry.emplace<TransformComponent>(fragment);
				tc.scale = { 0.15f, 1.0f, 0.3f }; // 小さい破片
				auto& mr = registry.emplace<MeshRendererComponent>(fragment);
				mr.modelPath = "Resources/Models/cube/cube.obj";
				mr.texturePath = "Resources/Textures/white1x1.png";
				mr.color = { 1.0f, 0.8f, 0.2f, 1.0f }; // 黄金色
				if (renderer) {
					mr.modelHandle = renderer->LoadObjMesh(mr.modelPath);
					mr.textureHandle = renderer->LoadTexture2D(mr.texturePath);
				}
				auto& tag = registry.emplace<TagComponent>(fragment);
				tag.tag = TagType::Player; // レイキャスト回避
				pw.weaponEntities.push_back(fragment);
			}
		}
	}

	void UpdateWeaponAnimation(entt::registry& registry, PlayerWeaponComponent& pw, const TransformComponent& ptc, const PlayerActionComponent& pa) {
		if (pw.weaponEntities.empty()) return;

		float pSin = std::sin(ptc.rotate.y);
		float pCos = std::cos(ptc.rotate.y);

		if (pw.currentWeapon == WeaponType::Greatsword) {
			auto& wtc = registry.get<TransformComponent>(pw.weaponEntities[0]);
			
			if (pa.state == PlayerActionState::Idle || pa.state == PlayerActionState::Dodge || pa.state == PlayerActionState::Parry) {
				// 背中に背負う（斜め）
				wtc.translate.x = ptc.translate.x - pSin * 0.5f;
				wtc.translate.y = ptc.translate.y + 1.5f + std::sin(pa.stateTimer * 2.0f) * 0.1f;
				wtc.translate.z = ptc.translate.z - pCos * 0.5f;
				wtc.rotate = ptc.rotate;
				wtc.rotate.x = DirectX::XM_PIDIV4;
				wtc.rotate.z = DirectX::XM_PIDIV4;
			} 
			else if (pa.state == PlayerActionState::Attack1 || pa.state == PlayerActionState::Attack2 || pa.state == PlayerActionState::Attack3) {
				// 通常コンボ：縦振り
				float progress = pa.stateTimer / pa.stateDuration;
				wtc.translate.x = ptc.translate.x + pSin * 1.5f;
				wtc.translate.y = ptc.translate.y + 1.5f;
				wtc.translate.z = ptc.translate.z + pCos * 1.5f;
				wtc.rotate = ptc.rotate;
				if (progress < 0.3f) {
					wtc.rotate.x = DirectX::XM_PIDIV2 * (progress / 0.3f);
				} else {
					wtc.rotate.x = DirectX::XM_PIDIV2 - DirectX::XM_PI * ((progress - 0.3f) / 0.7f);
				}
			}
			else if (pa.state == PlayerActionState::Charging) {
				// 溜め中（構えてブルブル震える、溜めレベルで構え角度が変わる）
				float intensity = std::min(pa.chargeTimer / 1.5f, 1.0f);
				float shake = std::sin(pa.stateTimer * 60.0f) * (0.05f + 0.12f * intensity);
				wtc.translate.x = ptc.translate.x - pSin * 0.8f + pCos * shake;
				wtc.translate.y = ptc.translate.y + 1.8f + intensity * 0.5f;
				wtc.translate.z = ptc.translate.z - pCos * 0.8f - pSin * shake;
				wtc.rotate = ptc.rotate;
				// 溜めが深いほど大きく振りかぶる
				wtc.rotate.x = DirectX::XM_PIDIV2 + DirectX::XM_PIDIV4 * intensity;
			}
			else if (pa.state == PlayerActionState::ChargeAttack1) {
				// 溜め1段目：力強い縦斬り（通常より大振り）
				float progress = pa.stateTimer / pa.stateDuration;
				wtc.translate.x = ptc.translate.x + pSin * 2.0f;
				wtc.translate.y = ptc.translate.y + 2.0f;
				wtc.translate.z = ptc.translate.z + pCos * 2.0f;
				wtc.rotate = ptc.rotate;
				if (progress < 0.25f) {
					wtc.rotate.x = DirectX::XM_PI * 0.75f * (progress / 0.25f);
				} else {
					wtc.rotate.x = DirectX::XM_PI * 0.75f - DirectX::XM_PI * 1.25f * ((progress - 0.25f) / 0.75f);
				}
			}
			else if (pa.state == PlayerActionState::ChargeAttack2) {
				// 溜め2段目：大薙ぎ払い（横回転）
				float progress = pa.stateTimer / pa.stateDuration;
				float swingAngle = ptc.rotate.y - DirectX::XM_PIDIV2 + DirectX::XM_PI * 1.2f * progress;
				float radius = 2.5f;
				wtc.translate.x = ptc.translate.x + std::sin(swingAngle) * radius;
				wtc.translate.y = ptc.translate.y + 1.5f;
				wtc.translate.z = ptc.translate.z + std::cos(swingAngle) * radius;
				wtc.rotate.y = swingAngle;
				wtc.rotate.x = DirectX::XM_PIDIV2;
				wtc.rotate.z = 0;
			}
			else if (pa.state == PlayerActionState::ChargeAttack3) {
				// 溜め3段目：天墜斬（飛び上がって叩きつけ）
				float progress = pa.stateTimer / pa.stateDuration;
				float jumpHeight;
				if (progress < 0.35f) {
					// 上昇
					jumpHeight = 5.0f * (progress / 0.35f);
					wtc.rotate.x = DirectX::XM_PI * (progress / 0.35f);
				} else {
					// 急降下叩きつけ
					float fallProgress = (progress - 0.35f) / 0.65f;
					jumpHeight = 5.0f * (1.0f - fallProgress * fallProgress);
					wtc.rotate.x = DirectX::XM_PI + DirectX::XM_PI * fallProgress;
				}
				wtc.translate.x = ptc.translate.x + pSin * 1.5f;
				wtc.translate.y = ptc.translate.y + 1.5f + jumpHeight;
				wtc.translate.z = ptc.translate.z + pCos * 1.5f;
				wtc.rotate.y = ptc.rotate.y;
				wtc.rotate.z = 0;
			}
		}
		else if (pw.currentWeapon == WeaponType::DualBlades) {
			for (size_t i = 0; i < pw.weaponEntities.size(); ++i) {
				auto& wtc = registry.get<TransformComponent>(pw.weaponEntities[i]);
				float sign = (i == 0) ? -1.0f : 1.0f; // 左と右
				
				if (pa.state == PlayerActionState::Idle || pa.state == PlayerActionState::Dodge || pa.state == PlayerActionState::Parry) {
					// 左右に逆手持ちで待機
					float offsetX = sign * 1.2f;
					wtc.translate.x = ptc.translate.x + offsetX * pCos;
					wtc.translate.y = ptc.translate.y + 1.0f + std::sin(pa.stateTimer * 4.0f + i) * 0.1f;
					wtc.translate.z = ptc.translate.z - offsetX * pSin;
					wtc.rotate = ptc.rotate;
					wtc.rotate.x = DirectX::XM_PIDIV2; // 寝かせる
				}
				else if (pa.state == PlayerActionState::Attack1 || pa.state == PlayerActionState::Attack2) {
					// X字斬り
					float progress = pa.stateTimer / pa.stateDuration;
					wtc.translate.x = ptc.translate.x + pSin * 1.0f;
					wtc.translate.y = ptc.translate.y + 1.2f;
					wtc.translate.z = ptc.translate.z + pCos * 1.0f;
					
					wtc.rotate = ptc.rotate;
					// 交差するように回転
					wtc.rotate.z = sign * (DirectX::XM_PIDIV4 - DirectX::XM_PIDIV2 * progress * 3.0f); 
				}
				else if (pa.state == PlayerActionState::Attack3) {
					// 乱舞（竜巻のように回転）
					wtc.rotate.y += 0.5f; // 高速回転
					
					float radius = 2.0f;
					float angle = wtc.rotate.y;
					wtc.translate.x = ptc.translate.x + std::sin(angle) * radius;
					wtc.translate.y = ptc.translate.y + 1.5f + std::sin(pa.stateTimer * 10.0f) * 0.5f;
					wtc.translate.z = ptc.translate.z + std::cos(angle) * radius;
				}
			}
		}
		else if (pw.currentWeapon == WeaponType::MultiBlades) {
			if (pa.state == PlayerActionState::Attack3) {
				// 合体フィニッシュ：1本の巨大な剣になる
				float progress = pa.stateTimer / pa.stateDuration;
				for (size_t i = 0; i < pw.weaponEntities.size(); ++i) {
					auto& wtc = registry.get<TransformComponent>(pw.weaponEntities[i]);
					
					// 直列に繋がる
					float offset = i * 2.0f;
					float swordLength = offset;
					
					// プレイヤーの少し前から振りかぶって薙ぎ払う
					float swingAngle = ptc.rotate.y - DirectX::XM_PIDIV2 + (DirectX::XM_PI * progress);
					
					wtc.rotate.y = swingAngle;
					wtc.rotate.x = DirectX::XM_PIDIV2;
					wtc.rotate.z = 0;
					
					wtc.translate.x = ptc.translate.x + std::sin(swingAngle) * swordLength;
					wtc.translate.y = ptc.translate.y + 1.5f;
					wtc.translate.z = ptc.translate.z + std::cos(swingAngle) * swordLength;
				}
			} else {
				// 待機または通常攻撃
				for (size_t i = 0; i < pw.weaponEntities.size(); ++i) {
					auto& wtc = registry.get<TransformComponent>(pw.weaponEntities[i]);
					
					float baseAngle = (DirectX::XM_2PI / pw.weaponEntities.size()) * i;
					float hoverAngle = baseAngle + pa.stateTimer;
					
					if (pa.state == PlayerActionState::Attack1 || pa.state == PlayerActionState::Attack2) {
						// 1個ずつ射出する（ファンネル）
						int firingIndex = (int)(pa.stateTimer * 4.0f) % pw.weaponEntities.size();
						if (i == firingIndex) {
							// 射出中
							float shootDist = 8.0f * std::sin((pa.stateTimer * 4.0f - firingIndex) * DirectX::XM_PI); // 行って戻る
							wtc.translate.x = ptc.translate.x + pSin * shootDist;
							wtc.translate.y = ptc.translate.y + 1.5f;
							wtc.translate.z = ptc.translate.z + pCos * shootDist;
							wtc.rotate = ptc.rotate;
							wtc.rotate.x = DirectX::XM_PIDIV2; // 刃を向ける
							continue;
						}
					}
					
					// 後光のように背後に浮遊
					float radius = 1.5f;
					wtc.translate.x = ptc.translate.x - pSin * 1.0f + std::cos(ptc.rotate.y) * std::sin(hoverAngle) * radius;
					wtc.translate.y = ptc.translate.y + 2.0f + std::cos(hoverAngle) * radius;
					wtc.translate.z = ptc.translate.z - pCos * 1.0f - std::sin(ptc.rotate.y) * std::sin(hoverAngle) * radius;
					
					// プレイヤーの方向を向く
					wtc.rotate = ptc.rotate;
					wtc.rotate.x = DirectX::XM_PIDIV2;
				}
			}
		}
	}
};

} // namespace Game
