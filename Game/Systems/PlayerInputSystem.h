#pragma once
#include "ISystem.h"
#include <Windows.h>
#include <cmath>

namespace Game {

class PlayerInputSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		auto view = registry.view<PlayerInputComponent>();
		for (auto entity : view) {
			auto& pi = registry.get<PlayerInputComponent>(entity);
			if (!pi.enabled) continue;

			DirectX::XMFLOAT2 moveDir = {0.0f, 0.0f};
			if (GetAsyncKeyState('W') & 0x8000) moveDir.y += 1.0f;
			if (GetAsyncKeyState('S') & 0x8000) moveDir.y -= 1.0f;
			if (GetAsyncKeyState('A') & 0x8000) moveDir.x -= 1.0f;
			if (GetAsyncKeyState('D') & 0x8000) moveDir.x += 1.0f;

			float len = std::sqrt(moveDir.x * moveDir.x + moveDir.y * moveDir.y);
			if (len > 0.001f) {
				moveDir.x /= len;
				moveDir.y /= len;
			}
			pi.moveDir = moveDir;

			// ジャンプ入力
			bool currentSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
			if (currentSpace && !prevSpace_)
				pi.jumpRequested = true;
			else
				pi.jumpRequested = false;
			prevSpace_ = currentSpace;

			// ★変更: 攻撃入力 → 左クリック
			pi.attackRequested = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

			// ★追加: ロックオン入力（ミドルクリックでトグル）
			bool currentMButton = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
			if (currentMButton && !prevMButton_) {
				if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy)) {
					// ロックオン解除
					pi.lockedEnemy = entt::null;
				} else {
					// 近くの敵を探す
					entt::entity bestTarget = entt::null;
					float minDistSq = 10000.0f; // 探索範囲の2乗
					
					DirectX::XMFLOAT3 playerPos = {0, 0, 0};
					if (registry.all_of<TransformComponent>(entity)) {
						playerPos = registry.get<TransformComponent>(entity).translate;
					}

					auto enemies = registry.view<TagComponent, TransformComponent, HealthComponent>();
					for (auto e : enemies) {
						if (enemies.get<TagComponent>(e).tag == TagType::Enemy && !enemies.get<HealthComponent>(e).isDead) {
							auto& eTc = enemies.get<TransformComponent>(e);
							float dx = eTc.translate.x - playerPos.x;
							float dz = eTc.translate.z - playerPos.z; // 水平距離で判定
							float distSq = dx * dx + dz * dz;
							if (distSq < minDistSq) {
								minDistSq = distSq;
								bestTarget = e;
							}
						}
					}
					pi.lockedEnemy = bestTarget;
				}
			}
			prevMButton_ = currentMButton;

			// 対象が死んだり消えたりしたらロックオン解除
			if (pi.lockedEnemy != entt::null) {
				if (!registry.valid(pi.lockedEnemy) || 
					(registry.all_of<HealthComponent>(pi.lockedEnemy) && registry.get<HealthComponent>(pi.lockedEnemy).isDead)) {
					pi.lockedEnemy = entt::null;
				}
			}

			// ★変更: カメラ操作 → 常時マウス追従（右クリック不要）
			pi.cameraYaw = 0.0f;
			pi.cameraPitch = 0.0f;
			if (ctx.input && pi.lockedEnemy == entt::null) {
				// ロックオン中でない場合のみマウスで視点移動
				pi.cameraYaw = ctx.input->GetMouseDeltaX() * 0.005f;
				pi.cameraPitch = ctx.input->GetMouseDeltaY() * 0.005f;
			}
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		prevSpace_ = false;
		prevMButton_ = false;
	}

private:
	bool prevSpace_ = false;
	bool prevMButton_ = false;
};

} // namespace Game
