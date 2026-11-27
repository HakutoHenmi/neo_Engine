// GameSceneGizmos.cpp
// GameScene のエディタ用の表示: 選択中の物の枠と当たり判定、変形ツールの形、地面の格子、ライトの印。
// Windows のヘッダーの min / max マクロを止める
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "./GameScene.h"
#include "./GameSceneConstants.h"
#include "../Editor/EditorUI.h"
#include <DirectXMath.h>
#include <array>
#include <cmath>

namespace Game {

using namespace GameSceneConstants;

namespace {

using BoxCorners = std::array<Engine::Vector3, kBoxCornerCount>;

// 箱の 12 本の辺 (角の番号の組)
constexpr int kBoxEdges[][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

// 中心と半分の大きさから、箱の 8 つの角を作る
BoxCorners MakeBoxCorners(const Engine::Vector3& center, const Engine::Vector3& half) {
	const float x0 = center.x - half.x, x1 = center.x + half.x;
	const float y0 = center.y - half.y, y1 = center.y + half.y;
	const float z0 = center.z - half.z, z1 = center.z + half.z;
	return {{
	    {x0, y0, z0},
	    {x1, y0, z0},
	    {x1, y1, z0},
	    {x0, y1, z0},
	    {x0, y0, z1},
	    {x1, y0, z1},
	    {x1, y1, z1},
	    {x0, y1, z1},
	}};
}

// 点を行列で変換する
Engine::Vector3 TransformPoint(const Engine::Vector3& point, DirectX::FXMMATRIX matrix) {
	const DirectX::XMVECTOR transformed =
	    DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(point.x, point.y, point.z, 1.0f), matrix);
	Engine::Vector3 result;
	DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&result), transformed);
	return result;
}

// ローカル座標の箱をワールド行列で変換し、12 本の辺を描く
void DrawBox(
    Engine::Renderer& renderer, const BoxCorners& localCorners, DirectX::FXMMATRIX world,
    const Engine::Vector4& color) {
	BoxCorners corners;
	for (size_t i = 0; i < corners.size(); ++i) {
		corners[i] = TransformPoint(localCorners[i], world);
	}
	for (const auto& edge : kBoxEdges) {
		renderer.DrawLine3D(corners[edge[0]], corners[edge[1]], color, true);
	}
}

// 変形ツールの軸の色 (X 赤・Y 緑・Z 青、ドラッグ中の軸は明るく)
Engine::Vector4 AxisColor(int axis, int dragAxis) {
	const float other = (axis == dragAxis) ? Gizmo::kAxisHighlight : Gizmo::kAxisDim;
	switch (axis) {
	case 0:
		return {1.0f, other, other, 1.0f};
	case 1:
		return {other, 1.0f, other, 1.0f};
	case 2:
		return {other, other, 1.0f, 1.0f};
	default:
		return {1.0f, 1.0f, 1.0f, 1.0f};
	}
}

// 色の透明度だけを差し替える
Engine::Vector4 WithAlpha(Engine::Vector4 color, float alpha) {
	color.w = alpha;
	return color;
}

// ライトの位置の十字を描く (withDepth が true なら奥行き方向の線も描く)
void DrawCross(Engine::Renderer& renderer, const Engine::Vector3& pos, const Engine::Vector4& color, bool withDepth) {
	const float s = Gizmo::kLightCrossSize;
	renderer.DrawLine3D({pos.x - s, pos.y, pos.z}, {pos.x + s, pos.y, pos.z}, color, true);
	renderer.DrawLine3D({pos.x, pos.y - s, pos.z}, {pos.x, pos.y + s, pos.z}, color, true);
	if (withDepth) {
		renderer.DrawLine3D({pos.x, pos.y, pos.z - s}, {pos.x, pos.y, pos.z + s}, color, true);
	}
}

// ライトの向き (回転行列の Z 軸) の線を描く
void DrawLightDirection(
    Engine::Renderer& renderer, const TransformComponent& tc, const Engine::Vector3& pos,
    const Engine::Vector4& color) {
	const Engine::Matrix4x4 mat = tc.GetTransform().ToMatrix();
	const Engine::Vector3 fwd = {mat.m[2][0], mat.m[2][1], mat.m[2][2]};
	const float length = Gizmo::kLightDirectionLength;
	renderer.DrawLine3D(pos, {pos.x + fwd.x * length, pos.y + fwd.y * length, pos.z + fwd.z * length}, color, true);
}

} // namespace

// エディタ: 選択中のエンティティを箱の線で強調表示する
void GameScene::DrawSelectionHighlight() {
	if (!renderer_) {
		return;
	}

	// モデルの基準の箱 (±1)
	const BoxCorners unitBox = MakeBoxCorners({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});

	for (auto entity : selectedEntities_) {
		// 消えているか位置が無ければ飛ばす
		if (!registry_.valid(entity) || !registry_.all_of<TransformComponent>(entity)) {
			continue;
		}
		const auto& tc = registry_.get<TransformComponent>(entity);

		// ワールド行列
		Engine::Matrix4x4 mat = GetWorldMatrix(static_cast<int>(entity));
		const DirectX::XMMATRIX worldMat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&mat));

		// モデルの基準の箱を黄色で描く
		DrawBox(*renderer_, unitBox, worldMat, Gizmo::kSelectionColor);

		// 箱の当たり判定 (緑)
		if (const auto* bc = registry_.try_get<BoxColliderComponent>(entity); bc && bc->enabled) {
			const Engine::Vector3 center = {bc->center.x, bc->center.y, bc->center.z};
			const Engine::Vector3 half = {bc->size.x * 0.5f, bc->size.y * 0.5f, bc->size.z * 0.5f};
			DrawBox(*renderer_, MakeBoxCorners(center, half), worldMat, Gizmo::kBoxColliderColor);
		}

		// GPU メッシュコライダー (青、重なっていれば赤)
		if (const auto* gmc = registry_.try_get<GpuMeshColliderComponent>(entity); gmc && gmc->enabled) {
			const auto& color = gmc->isIntersecting ? Gizmo::kMeshColliderHitColor : Gizmo::kMeshColliderColor;
			DrawBox(*renderer_, unitBox, worldMat, color);
		}

		DrawTransformGizmo(entity, tc);
	}
}

// 変形ツールの形を描く (移動は矢印、回転は円、拡大は先端の × 印)。ドラッグ中の軸は明るくする
void GameScene::DrawTransformGizmo(entt::entity entity, const TransformComponent& tc) {
	const DirectX::XMMATRIX gizmoMat = DirectX::XMMatrixRotationRollPitchYaw(tc.rotate.x, tc.rotate.y, tc.rotate.z) *
	                                   DirectX::XMMatrixTranslation(tc.translate.x, tc.translate.y, tc.translate.z);
	// ギズモのローカル座標の線を、ワールド座標に直して描く
	auto drawLocalLine = [&](const Engine::Vector3& localP0, const Engine::Vector3& localP1,
	                         const Engine::Vector4& col) {
		renderer_->DrawLine3D(TransformPoint(localP0, gizmoMat), TransformPoint(localP1, gizmoMat), col, true);
	};

	const float al = Gizmo::kAxisLength;
	const float ar = Gizmo::kArrowLength;
	const float spread = ar * Gizmo::kArrowSpread;
	// ドラッグ中の軸 (選択中のものをドラッグしているときだけ)
	const int dragAxis = (EditorUI::IsGizmoDragging() && entity == selectedEntity_) ? EditorUI::GetGizmoDragAxis() : -1;
	const auto cX = AxisColor(0, dragAxis);
	const auto cY = AxisColor(1, dragAxis);
	const auto cZ = AxisColor(2, dragAxis);

	switch (EditorUI::GetGizmoMode()) {
	case GizmoMode::Translate:
		// 移動: 各軸の線と矢印の先
		drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
		drawLocalLine({al, 0, 0}, {al - ar, spread, 0}, cX);
		drawLocalLine({al, 0, 0}, {al - ar, -spread, 0}, cX);
		drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
		drawLocalLine({0, al, 0}, {spread, al - ar, 0}, cY);
		drawLocalLine({0, al, 0}, {-spread, al - ar, 0}, cY);
		drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
		drawLocalLine({0, 0, al}, {0, spread, al - ar}, cZ);
		drawLocalLine({0, 0, al}, {0, -spread, al - ar}, cZ);
		break;
	case GizmoMode::Rotate: {
		// 回転: 各軸のまわりの円
		const int seg = Gizmo::kRotateRingSegments;
		const float rad = Gizmo::kRotateRingRadius;
		for (int i = 0; i < seg; ++i) {
			const float a0 = static_cast<float>(i) / seg * DirectX::XM_2PI;
			const float a1 = static_cast<float>(i + 1) / seg * DirectX::XM_2PI;
			const float c0 = std::cos(a0) * rad, s0 = std::sin(a0) * rad;
			const float c1 = std::cos(a1) * rad, s1 = std::sin(a1) * rad;
			drawLocalLine({0, c0, s0}, {0, c1, s1}, cX);
			drawLocalLine({c0, 0, s0}, {c1, 0, s1}, cY);
			drawLocalLine({c0, s0, 0}, {c1, s1, 0}, cZ);
		}
		break;
	}
	default: {
		// 拡大: 各軸の線と先の × 印
		const float e = Gizmo::kScaleMarkSize;
		drawLocalLine({0, 0, 0}, {al, 0, 0}, cX);
		drawLocalLine({al - e, -e, 0}, {al + e, e, 0}, cX);
		drawLocalLine({al + e, -e, 0}, {al - e, e, 0}, cX);
		drawLocalLine({0, 0, 0}, {0, al, 0}, cY);
		drawLocalLine({-e, al - e, 0}, {e, al + e, 0}, cY);
		drawLocalLine({e, al - e, 0}, {-e, al + e, 0}, cY);
		drawLocalLine({0, 0, 0}, {0, 0, al}, cZ);
		drawLocalLine({0, -e, al - e}, {0, e, al + e}, cZ);
		drawLocalLine({0, e, al - e}, {0, -e, al + e}, cZ);
		break;
	}
	}
}

// エディタ: 地面の格子 (10 マスごとに濃く)、X 軸 (赤) と Z 軸 (青)、原点の XYZ 軸
void GameScene::DrawEditorGizmos() {
	if (!renderer_) {
		return;
	}
	const float gridSize = Gizmo::kGridHalfSize;
	for (float i = -gridSize; i <= gridSize; i += Gizmo::kGridStep) {
		// 中心の線は軸として別に描く
		if (std::fabs(i) < Gizmo::kGridEpsilon) {
			continue;
		}
		// 一定間隔ごとの線は濃く
		const bool isMajor = std::fmod(std::fabs(i), Gizmo::kGridMajorStep) < Gizmo::kGridEpsilon;
		const float alpha = isMajor ? Gizmo::kGridMajorAlpha : Gizmo::kGridMinorAlpha;
		const Engine::Vector4 gc = {Gizmo::kGridGray, Gizmo::kGridGray, Gizmo::kGridGray, alpha};
		// X 方向と Z 方向の線
		renderer_->DrawLine3D({-gridSize, 0.0f, i}, {gridSize, 0.0f, i}, gc, false);
		renderer_->DrawLine3D({i, 0.0f, -gridSize}, {i, 0.0f, gridSize}, gc, false);
	}
	// X 軸 (赤) と Z 軸 (青)
	renderer_->DrawLine3D({-gridSize, 0.0f, 0.0f}, {gridSize, 0.0f, 0.0f}, Gizmo::kGridAxisXColor, false);
	renderer_->DrawLine3D({0.0f, 0.0f, -gridSize}, {0.0f, 0.0f, gridSize}, Gizmo::kGridAxisZColor, false);
	// 原点の XYZ 軸 (手前に描く)
	const float len = Gizmo::kOriginAxisLength;
	renderer_->DrawLine3D({0, 0, 0}, {len, 0, 0}, AxisColor(0, -1), true);
	renderer_->DrawLine3D({0, 0, 0}, {0, len, 0}, AxisColor(1, -1), true);
	renderer_->DrawLine3D({0, 0, 0}, {0, 0, len}, AxisColor(2, -1), true);
}

// エディタ UI の描画
void GameScene::DrawEditor() {
#ifdef USE_IMGUI
	EditorUI::Show(renderer_, this);
#endif
}

// エディタ: ライトの位置と向きを線で描く (選択中は濃く)
void GameScene::DrawLightGizmos() {
	if (!renderer_) {
		return;
	}
	// 選択中なら濃く、そうでなければ薄く
	auto alphaFor = [this](entt::entity entity) {
		const bool isSelected = selectedEntities_.find(entity) != selectedEntities_.end();
		return isSelected ? Gizmo::kLightSelectedAlpha : Gizmo::kLightIdleAlpha;
	};

	// 平行光源 (黄): 十字と向き
	registry_.view<DirectionalLightComponent, TransformComponent>().each(
	    [&](auto entity, const DirectionalLightComponent& dl, const TransformComponent& tc) {
		    if (!dl.enabled) {
			    return;
		    }
		    const Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		    const auto col = WithAlpha(Gizmo::kDirectionalLightColor, alphaFor(entity));
		    DrawLightDirection(*renderer_, tc, pos, col);
		    DrawCross(*renderer_, pos, col, false);
	    });

	// 点光源 (緑): 3軸の十字
	registry_.view<PointLightComponent, TransformComponent>().each(
	    [&](auto entity, const PointLightComponent& pl, const TransformComponent& tc) {
		    if (!pl.enabled) {
			    return;
		    }
		    const Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		    DrawCross(*renderer_, pos, WithAlpha(Gizmo::kPointLightColor, alphaFor(entity)), true);
	    });

	// スポットライト (水色): 十字と向き
	registry_.view<SpotLightComponent, TransformComponent>().each(
	    [&](auto entity, const SpotLightComponent& sl, const TransformComponent& tc) {
		    if (!sl.enabled) {
			    return;
		    }
		    const Engine::Vector3 pos = {tc.translate.x, tc.translate.y, tc.translate.z};
		    const auto col = WithAlpha(Gizmo::kSpotLightColor, alphaFor(entity));
		    DrawLightDirection(*renderer_, tc, pos, col);
		    DrawCross(*renderer_, pos, col, false);
	    });
}

} // namespace Game
