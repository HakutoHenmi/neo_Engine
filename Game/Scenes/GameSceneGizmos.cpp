// GameSceneGizmos.cpp
// GameScene のエディタ用の表示: 選択中の物の枠と当たり判定、変形ツールの形、地面の格子、ライトの印。
#include <cfloat>
// シーンの BGM
#include "../UI/SceneMusic.h"
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
// プレイヤー向けの UI (共通部品・強化カード・グラフィック設定)
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../UI/GraphicsUI.h"
// 流体の放出と時間の管理 (エンジン側)
#include "../../Engine/FluidEmission.h"
#include "../../Engine/Time/TimeManager.h"
// 缶の装備 (旧アクションゲーム)
#include "../CanLoadout.h"
#include "../ObjectTypes.h"
// 音・パス・シーン管理 (エンジン側)
#include "../../Engine/Audio.h"
#include "../../Engine/PathUtils.h"
#include "../../Engine/SceneManager.h"
// エディタとスクリプト
#include "../Editor/EditorUI.h"
#include "../Scripts/ScriptEngine.h"
#include "../Scripts/ScriptLifecycle.h"
// ゲームの処理を分担するシステム群
#include "../Systems/AudioSystem.h"
#include "../Systems/CameraFollowSystem.h"
#include "../Systems/CharacterMovementSystem.h"
#include "../Systems/CleanupSystem.h"
#include "../Systems/FluidSystem.h"
#include "../Systems/WeaponSystem.h" // 武器の管理
#include "../../Engine/Input.h"      // キーボード・マウス入力

#include "../Systems/HealthSystem.h"
#include "../Systems/MotionSystem.h" // キーフレームのモーション
#include "../Systems/PhysicsSystem.h"
#include "../Systems/PlayerInputSystem.h"
#include "../Systems/ChronoSystem.h"
#include "../Systems/PlayerActionSystem.h" // プレイヤーアクション
#include "../Systems/CombatSystem.h"       // 戦闘の当たり判定
#include "../Systems/CanAbilitySystem.h"
#include "../Systems/EnemyAISystem.h"      // 敵の AI
#include "../Systems/BossActionSystem.h"    // ボスの AI
#include "../Systems/WaveSystem.h"         // ウェーブ管理
#include "../Scripts/BossTestScript.h"      // ボスの生成に使う

#include "../Systems/ScriptSystem.h"
#include "../Systems/UISystem.h"
#include "../Systems/PostProcessSystem.h" // ポストエフェクト
// Web プロファイラ (外部ツールから値を見たり変えたりする)
#include "../../Engine/NetworkProfiler.h"
// JSON の読み書き
#include "../../externals/nlohmann/json.hpp"
#include "../../externals/imgui/imgui.h" // ImGui (エディタの画面)
#include <Windows.h> // デバッグ出力 (OutputDebugStringA) を使うため
#include <algorithm>
#include <cmath>
#include <filesystem> // 空のテクスチャ検索などのファイル操作

namespace Game {

// エディタ: 選択中のエンティティを箱の線で強調表示する
void GameScene::DrawSelectionHighlight() {
	if (!renderer_)
		return;

	// 選択中のエンティティを順に処理する
	for (auto entity : selectedEntities_) {
		// 消えているか位置が無ければ飛ばす
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity))
			continue;

		// 位置
		auto& tc = registry_.get<TransformComponent>(entity);
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};

		// ワールド行列
		Engine::Matrix4x4 mat = this->GetWorldMatrix(static_cast<int>(entity));
		DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		// モデルの基準の箱 (±1) をワールド行列で変換し、12本の辺を黄色で描く
		Engine::Vector4 hlColor = {1.0f, 0.85f, 0.0f, 1.0f};
		Engine::Vector3 v[8] = {
		    {-1.0f, -1.0f, -1.0f},
            {1.0f,  -1.0f, -1.0f},
            {1.0f,  1.0f,  -1.0f},
            {-1.0f, 1.0f,  -1.0f},
            {-1.0f, -1.0f, 1.0f },
            {1.0f,  -1.0f, 1.0f },
            {1.0f,  1.0f,  1.0f },
            {-1.0f, 1.0f,  1.0f },
		};

		// 8 つの角をワールド座標へ変換する
		for (int i = 0; i < 8; ++i) {
			DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v[i].x, v[i].y, v[i].z, 1.0f), worldMat);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&v[i]), p);
		}
		// 箱の 12 本の辺 (角の番号の組)
		int edges[][2] = {
		    {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7}
        };
		// 辺を描く
		for (auto& eg : edges)
			renderer_->DrawLine3D(v[eg[0]], v[eg[1]], hlColor, true);

		// 箱の当たり判定 (緑)
		if (registry_.all_of<BoxColliderComponent>(entity)) {
			const auto& bc = registry_.get<BoxColliderComponent>(entity);
			if (bc.enabled) {
				// 箱の半分の大きさと中心
				float hx = bc.size.x * 0.5f, hy = bc.size.y * 0.5f, hz = bc.size.z * 0.5f;
				Engine::Vector3 cp = {bc.center.x, bc.center.y, bc.center.z};
				Engine::Vector4 colColor = {0.2f, 1.0f, 0.2f, 0.8f};
				Engine::Vector3 cv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], colColor, true);
			}
		}

		// GPU メッシュコライダー (青、重なっていれば赤)
		if (registry_.all_of<GpuMeshColliderComponent>(entity)) {
			const auto& gmc = registry_.get<GpuMeshColliderComponent>(entity);
			if (gmc.enabled) {
				Engine::Vector4 gColor = gmc.isIntersecting ? Engine::Vector4{1.0f, 0.2f, 0.2f, 0.8f} : Engine::Vector4{0.2f, 0.2f, 1.0f, 0.8f};
				// モデルの基準の箱 (±1)
				float hs = 1.0f;
				Engine::Vector3 cv[8] = {
				    {-hs, -hs, -hs},
                    {hs,  -hs, -hs},
                    {hs,  hs,  -hs},
                    {-hs, hs,  -hs},
                    {-hs, -hs, hs },
                    {hs,  -hs, hs },
                    {hs,  hs,  hs },
                    {-hs, hs,  hs }
                };
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cv[i].x, cv[i].y, cv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&cv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(cv[eg[0]], cv[eg[1]], gColor, true);
			}
		}

		// 攻撃判定 (赤。有効なときは濃く)
		if (registry_.all_of<HitboxComponent>(entity)) {
			const auto& hb = registry_.get<HitboxComponent>(entity);
			if (hb.enabled) {
				// 箱の半分の大きさと中心
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = hb.isActive ? Engine::Vector4{1.0f, 0.2f, 0.2f, 1.0f} : Engine::Vector4{1.0f, 0.2f, 0.2f, 0.3f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		// 食らい判定 (緑)
		if (registry_.all_of<HurtboxComponent>(entity)) {
			const auto& hb = registry_.get<HurtboxComponent>(entity);
			if (hb.enabled) {
				// 箱の半分の大きさと中心
				float hx = hb.size.x * 0.5f, hy = hb.size.y * 0.5f, hz = hb.size.z * 0.5f;
				Engine::Vector3 cp = {hb.center.x, hb.center.y, hb.center.z};
				Engine::Vector4 hbColor = {0.2f, 1.0f, 0.5f, 0.6f};
				Engine::Vector3 hv[8] = {
				    {cp.x - hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y - hy, cp.z - hz},
                    {cp.x + hx, cp.y + hy, cp.z - hz},
                    {cp.x - hx, cp.y + hy, cp.z - hz},
				    {cp.x - hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y - hy, cp.z + hz},
                    {cp.x + hx, cp.y + hy, cp.z + hz},
                    {cp.x - hx, cp.y + hy, cp.z + hz},
				};
				// 8 つの角をワールド座標へ変換して辺を描く
				for (int i = 0; i < 8; ++i) {
					DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(hv[i].x, hv[i].y, hv[i].z, 1.0f), worldMat);
					DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&hv[i]), p);
				}
				for (auto& eg : edges)
					renderer_->DrawLine3D(hv[eg[0]], hv[eg[1]], hbColor, true);
			}
		}

		// 変形ツールの形を描く (移動は矢印、回転は円、拡大は先端の × 印)。ドラッグ中の軸は明るくする
		DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) * DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
		// ギズモのローカル座標の線を、ワールド座標に直して描く
		auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1, const Engine::Vector4& col) {
			DirectX::XMVECTOR p0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP0.x, localP0.y, localP0.z, 1.0f), gizmoMat);
			DirectX::XMVECTOR p1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(localP1.x, localP1.y, localP1.z, 1.0f), gizmoMat);
			Engine::Vector3 wp0, wp1;
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp0), p0);
			DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&wp1), p1);
			renderer_->DrawLine3D(wp0, wp1, col, true);
		};

		// 軸の長さと、矢印の先の大きさ
		const float al = 2.0f, ar = 0.3f;
		// ドラッグ中の軸 (選択中のものをドラッグしているときだけ)
		int dAxis = (EditorUI::IsGizmoDragging() && entity == selectedEntity_) ? EditorUI::GetGizmoDragAxis() : -1;
		// 軸の色 (X 赤・Y 緑・Z 青、ドラッグ中は明るく)
		auto axCol = [](int axis, int drag) -> Engine::Vector4 {
			bool a = (drag == axis);
			switch (axis) {
			case 0:
				return a ? Engine::Vector4{1, .6f, .6f, 1} : Engine::Vector4{1, .2f, .2f, 1};
			case 1:
				return a ? Engine::Vector4{.6f, 1, .6f, 1} : Engine::Vector4{.2f, 1, .2f, 1};
			case 2:
				return a ? Engine::Vector4{.6f, .6f, 1, 1} : Engine::Vector4{.2f, .2f, 1, 1};
			default:
				return {1, 1, 1, 1};
			}
		};
		// 3 軸の色
		auto cX = axCol(0, dAxis), cY = axCol(1, dAxis), cZ = axCol(2, dAxis);

		// 移動: 各軸の線と矢印の先
		if (EditorUI::GetGizmoMode() == GizmoMode::Translate) {
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, ar * .4f, 0}, cX);
			drawLocalLine({al, 0, 0}, {al - ar, -ar * .4f, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({0, al, 0}, {ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, al, 0}, {-ar * .4f, al - ar, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, 0, al}, {0, ar * .4f, al - ar}, cZ);
			drawLocalLine({0, 0, al}, {0, -ar * .4f, al - ar}, cZ);
		} else if (EditorUI::GetGizmoMode() == GizmoMode::Rotate) {
			// 回転: 各軸のまわりの円を 32 分割で描く
			const int seg = 32;
			const float rad = 1.5f;
			for (int i = 0; i < seg; ++i) {
				float a0 = (float)i / seg * DirectX::XM_2PI, a1 = (float)(i + 1) / seg * DirectX::XM_2PI;
				drawLocalLine({0, cosf(a0) * rad, sinf(a0) * rad}, {0, cosf(a1) * rad, sinf(a1) * rad}, cX);
				drawLocalLine({cosf(a0) * rad, 0, sinf(a0) * rad}, {cosf(a1) * rad, 0, sinf(a1) * rad}, cY);
				drawLocalLine({cosf(a0) * rad, sinf(a0) * rad, 0}, {cosf(a1) * rad, sinf(a1) * rad, 0}, cZ);
			}
		} else {
			// 拡大: 各軸の線と先の × 印
			float e = 0.15f;
			drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
			drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
			drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
			drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
			drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
			drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
			drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
			drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
			drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		}
	}
}

// エディタ: 地面の格子 (10 マスごとに濃く)、X 軸 (赤) と Z 軸 (青)、原点の XYZ 軸
void GameScene::DrawEditorGizmos() {
	if (!renderer_)
		return;
	// 格子の範囲 (±100) と間隔 (1)
	const float gridSize = 100.0f, step = 1.0f;
	for (float i = -gridSize; i <= gridSize; i += step) {
		// 中心の線は軸として別に描く
		if (std::fabs(i) < 0.01f)
			continue;
		// 10 ごとの線は濃く
		bool isMajor = std::fmod(std::fabs(i), 10.0f) < 0.01f;
		float alpha = isMajor ? 0.35f : 0.15f;
		Engine::Vector4 gc = {0.6f, 0.6f, 0.6f, alpha};
		// X 方向と Z 方向の線
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	// X 軸 (赤) と Z 軸 (青)
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, {0.8f, 0.2f, 0.2f, 0.7f}, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, {0.2f, 0.2f, 0.8f, 0.7f}, false);
	// 原点の XYZ 軸 (手前に描く)
	renderer_->DrawLine3D({0, 0, 0}, {1.5f, 0, 0}, {1.f, 0.2f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 1.5f, 0}, {0.2f, 1.f, 0.2f, 1.f}, true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, 1.5f}, {0.2f, 0.2f, 1.f, 1.f}, true);
}

// エディタ UI の描画
void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

// エディタ: ライトの位置と向きを線で描く (選択中は濃く)
void GameScene::DrawLightGizmos() {
	if (!renderer_)
		return;
	// 平行光源 (黄): 十字と向き
	auto dlView = registry_.view<DirectionalLightComponent, TransformComponent>();
	dlView.each([&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!dl.enabled)
			return;
		// 位置と向き (回転行列の Z 軸)
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {1.0f, 0.9f, 0.2f, alpha};
		// 向きの線 (長さ 5) と十字
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});

	// 点光源 (緑): 3軸の十字
	auto plView = registry_.view<PointLightComponent, TransformComponent>();
	plView.each([&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!pl.enabled)
			return;
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.9f, 0.2f, alpha};
		float s = 0.5f;
		// X・Y・Z の十字
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, col, true);
	});

	// スポットライト (水色): 十字と向き
	auto slView = registry_.view<SpotLightComponent, TransformComponent>();
	slView.each([&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		// 無効なら描かない
		if (!sl.enabled)
			return;
		// 位置と向き (回転行列の Z 軸)
		Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
		Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
		// 選択中なら濃く
		bool isSelected = (selectedEntities_.find(entity) != selectedEntities_.end());
		float alpha = isSelected ? 1.0f : 0.4f;

		Engine::Vector4 col = {0.2f, 0.8f, 1.0f, alpha};
		// 向きの線 (長さ 5) と十字
		renderer_->DrawLine3D(pos, {pos.x + fwd.x * 5.0f, pos.y + fwd.y * 5.0f, pos.z + fwd.z * 5.0f}, col, true);
		float s = 0.5f;
		renderer_->DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, col, true);
		renderer_->DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, col, true);
	});
}

} // namespace Game
