#include "ISystem.h"
#include <cmath>
#include <algorithm>
#include "../Engine/Input.h" // ★追加

namespace Game {

class CameraFollowSystem : public ISystem {
public:
	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying || !ctx.camera) return;

		auto view = registry.view<CameraTargetComponent, TransformComponent>();
		for (auto entity : view) {
			auto& ct = view.get<CameraTargetComponent>(entity);
			if (!ct.enabled) continue;

			// ★追加: マウスホイールによるズーム
			auto* inputIns = ::Engine::Input::GetInstance();
			if (inputIns) {
				float wheel = inputIns->GetMouseWheelDelta();
				if (std::abs(wheel) > 0.001f) {
					ct.distance -= wheel * 0.005f; // 感度調整
					ct.distance = std::clamp(ct.distance, 3.0f, 20.0f); // 範囲制限
				}
			}

			auto& tc = view.get<TransformComponent>(entity);
			DirectX::XMFLOAT3 targetPos = tc.translate;

			if (registry.all_of<PlayerInputComponent>(entity)) {
				auto& pi = registry.get<PlayerInputComponent>(entity);
				if (pi.enabled) {
					auto rot = ctx.camera->Rotation();
					
					// ★追加: ロックオン中の場合、カメラを敵の方に向ける
					if (pi.lockedEnemy != entt::null && registry.valid(pi.lockedEnemy)) {
						if (registry.all_of<TransformComponent>(pi.lockedEnemy)) {
							auto& eTc = registry.get<TransformComponent>(pi.lockedEnemy);
							// 敵の少し上（体の中央あたり）を注視点にする
							float targetY = eTc.translate.y + 1.5f;
							
							float dx = eTc.translate.x - targetPos.x;
							float dy = targetY - (targetPos.y + ct.height);
							float dz = eTc.translate.z - targetPos.z;
							
							float targetYaw = std::atan2(dx, dz);
							float horizontalDist = std::sqrt(dx * dx + dz * dz);
							float targetPitch = -std::atan2(dy, horizontalDist);
							
							// スムーズに追従
							float lerpSpeed = 10.0f * ctx.dt;
							
							// Yawの最短角度補間
							float diffYaw = targetYaw - rot.y;
							while (diffYaw < -DirectX::XM_PI) diffYaw += DirectX::XM_2PI;
							while (diffYaw > DirectX::XM_PI) diffYaw -= DirectX::XM_2PI;
							rot.y += diffYaw * lerpSpeed;
							
							rot.x += (targetPitch - rot.x) * lerpSpeed;
						}
					} else {
						// 通常のマウス操作
						rot.y += pi.cameraYaw;
						rot.x += pi.cameraPitch;
					}

					const float PITCH_LIMIT = 1.5f;
					if (rot.x > PITCH_LIMIT) rot.x = PITCH_LIMIT;
					if (rot.x < -PITCH_LIMIT) rot.x = -PITCH_LIMIT;

					ctx.camera->SetRotation(rot);
				}
			}

			auto curRot = ctx.camera->Rotation();
			float camSy = std::sin(curRot.y);
			float camCy = std::cos(curRot.y);
			float camSx = std::sin(curRot.x);
			float camCx = std::cos(curRot.x);

			DirectX::XMFLOAT3 offset = {
				-camSy * camCx * ct.distance,
				ct.height + camSx * ct.distance,
				-camCy * camCx * ct.distance
			};

			DirectX::XMFLOAT3 desiredPos = {
				targetPos.x + offset.x,
				targetPos.y + offset.y,
				targetPos.z + offset.z
			};

			DirectX::XMFLOAT3 currentPos = ctx.camera->Position();
			float t = ct.smoothSpeed * ctx.dt;
			if (t > 1.0f) t = 1.0f;

			DirectX::XMFLOAT3 newPos = {
				currentPos.x + (desiredPos.x - currentPos.x) * t,
				currentPos.y + (desiredPos.y - currentPos.y) * t,
				currentPos.z + (desiredPos.z - currentPos.z) * t
			};

			ctx.camera->SetPosition(newPos);
			break;
		}
	}
};

} // namespace Game
