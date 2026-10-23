// PlayerInputSystem.h
// 旧アクションゲームのプレイヤー入力 (移動・ジャンプ・攻撃・ロックオン・カメラ) を
// PlayerInputComponent へ書き込むシステム。
#pragma once
#include "ISystem.h"
#include <Windows.h>
#include <cmath>

namespace Game {

// キーボードとマウスの状態を読み取って入力コンポーネントへ反映する
class PlayerInputSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		// PLAY 中だけ動かす
		if (!ctx.isPlaying) return;

		// T キーでサンドバッグモードを切り替える
		bool currentT = (GetAsyncKeyState('T') & 0x8000) != 0;
		if (currentT && !prevT_) {
			ctx.isSandbagMode = !ctx.isSandbagMode;
		}
		prevT_ = currentT;

		// TAB キーでラジアルメニューを開く
		bool currentTab = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;

		auto view = registry.view<PlayerInputComponent>();
		// 各プレイヤー: 死亡していれば入力をすべて消して無効にする
		for (auto entity : view) {
			// このプレイヤーの入力コンポーネント
			auto& pi = registry.get<PlayerInputComponent>(entity);
			if (auto* hc = registry.try_get<HealthComponent>(entity)) {
				if (hc->IsDead() || hc->Hp() <= 0.0f) {
					pi.enabled = false;
					pi.moveDir = {0.0f, 0.0f};
					pi.jumpRequested = false;
					pi.attackRequested = false;
					pi.cameraYaw = 0.0f;
					pi.cameraPitch = 0.0f;
					pi.lockedEnemy = entt::null;
					pi.isRadialMenuOpen = false;
					continue;
				}
			}
			// 無効なら入力を読まない
			if (!pi.enabled) continue;

			// TAB を押している間だけラジアルメニューを開く
			pi.isRadialMenuOpen = currentTab;

			// WASD から移動方向を作り、斜めでも長さ 1 になるよう正規化する
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
			// 移動方向を記録する
			pi.moveDir = moveDir;

			// ジャンプ入力 (押した瞬間だけ)
			bool currentSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
			if (currentSpace && !prevSpace_)
				pi.jumpRequested = true;
			else
				pi.jumpRequested = false;
			prevSpace_ = currentSpace;

			// 攻撃入力: 左クリックを押している間
			pi.attackRequested = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

			// ロックオン入力: 中クリックで切り替える
			bool currentMButton = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
			if (currentMButton && !prevMButton_) {
				if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy)) {
					// ロックオン解除
					pi.lockedEnemy = entt::null;
				} else {
					// 近くの敵を探す
					entt::entity bestTarget = entt::null;
					float minDistSq = 10000.0f; // 探索範囲の2乗
					
					// プレイヤーの位置
					DirectX::XMFLOAT3 playerPos = {0, 0, 0};
					if (registry.all_of<TransformComponent>(entity)) {
						playerPos = registry.get<TransformComponent>(entity).translate;
					}

					// 生きている敵の中で一番近いもの (水平距離) を選ぶ
					auto enemies = registry.view<TagComponent, TransformComponent, HealthComponent>();
					for (auto e : enemies) {
						if (enemies.get<TagComponent>(e).tag == TagType::Enemy && !enemies.get<HealthComponent>(e).IsDead()) {
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
					// 選んだ敵をロックオンする (いなければ解除のまま)
					pi.lockedEnemy = bestTarget;
				}
			}
			// 中クリックの前フレームの状態を記録する
			prevMButton_ = currentMButton;

			// 対象が死んだり消えたりしたらロックオン解除
			if (pi.lockedEnemy != entt::null) {
				if (!registry.valid(pi.lockedEnemy) || 
					(registry.all_of<HealthComponent>(pi.lockedEnemy) && registry.get<HealthComponent>(pi.lockedEnemy).IsDead())) {
					pi.lockedEnemy = entt::null;
				}
			}

			// カメラ操作: 常にマウスの移動量で回す (右クリック不要)
			// 毎フレーム 0 に戻してから、今フレームの量を入れる
			pi.cameraYaw = 0.0f;
			pi.cameraPitch = 0.0f;
			if (ctx.input && pi.lockedEnemy == entt::null && !pi.isRadialMenuOpen) {
				// ロックオン中でなく、かつラジアルメニューを開いていない場合のみマウスで視点移動
				pi.cameraYaw = ctx.input->GetMouseDeltaX() * 0.003f;
				pi.cameraPitch = ctx.input->GetMouseDeltaY() * 0.003f;
			}
		}
	}

	// PLAY 開始時: 前フレームのキー状態を消す
	void Reset(entt::registry& /*registry*/) override {
		prevSpace_ = false;
		prevMButton_ = false;
		prevT_ = false;
	}

private:
	// 前フレームのキー状態 (押した瞬間を判定するため)
	bool prevSpace_ = false;
	bool prevMButton_ = false;
	bool prevT_ = false;
};

} // namespace Game
