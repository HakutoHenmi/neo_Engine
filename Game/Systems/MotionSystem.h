// MotionSystem.h
// キーフレームのモーション (MotionComponent) を再生してトランスフォームへ反映するシステム。
// 停止中はエディタ用にモーションの軌跡とキーフレームを線で描く。
#pragma once
#include "ISystem.h"
#include "../Math/Spline.h"

namespace Game {

// モーションのシステム
class MotionSystem : public ISystem {
public:
    // 再生中なら時刻を進め、現在の時刻の姿勢を反映する
    void Update(entt::registry& registry, GameContext& ctx) override {
        // モーションとトランスフォームを持つエンティティを処理する
        auto view = registry.view<MotionComponent, TransformComponent>();
        for (auto entity : view) {
            auto& motion = view.get<MotionComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            // 再生するクリップが無ければとばす
            if (motion.clips.find(motion.activeClip) == motion.clips.end()) continue;
            auto& clip = motion.clips[motion.activeClip];

            // PLAY 中で再生中なら時刻を進める
            if (motion.isPlaying && ctx.isPlaying) {
                motion.currentTime += ctx.dt;
                // ループなら最初へ戻し、しなければ最後で止める
                if (clip.loop) {
                    motion.currentTime = fmod(motion.currentTime, clip.totalDuration);
                } else if (motion.currentTime > clip.totalDuration) {
                    motion.currentTime = clip.totalDuration;
                    motion.isPlaying = false;
                }
            }

            // 姿勢をトランスフォームへ反映する
            ApplyMotion(motion, transform);
        }
    }

    // 時刻に応じた位置 (スプライン補間)・回転と拡大 (線形補間) を求める
    void ApplyMotion(const MotionComponent& motion, TransformComponent& transform) {
        // 再生中のクリップを探す (無ければ何もしない)
        auto it = motion.clips.find(motion.activeClip);
        if (it == motion.clips.end()) return;
        const auto& clip = it->second;

        // キーフレームが 2 つ未満なら補間できない
        if (clip.keyframes.size() < 2) return;

        float t = motion.currentTime;
        // キーフレームの位置の一覧を作る
        std::vector<DirectX::XMFLOAT3> posPoints;
        for (const auto& kf : clip.keyframes) posPoints.push_back(kf.translate);

        // 時刻をキーフレームの番号の単位 (0〜数-1) に直し、スプラインで位置を求める
        float splineT = (t / clip.totalDuration) * (static_cast<float>(clip.keyframes.size()) - 1.0f);
        DirectX::XMVECTOR pos = Engine::Spline::Interpolate(posPoints, splineT);
        DirectX::XMStoreFloat3(&transform.translate, pos);

        // 今の区間の前後のキーフレームと区間内の割合
        int i = static_cast<int>(splineT);
        int next = (i + 1 < (int)clip.keyframes.size()) ? i + 1 : i;
        float localT = splineT - static_cast<float>(i);

        // 回転を線形補間する
        transform.rotate.x = clip.keyframes[i].rotate.x * (1.0f - localT) + clip.keyframes[next].rotate.x * localT;
        transform.rotate.y = clip.keyframes[i].rotate.y * (1.0f - localT) + clip.keyframes[next].rotate.y * localT;
        transform.rotate.z = clip.keyframes[i].rotate.z * (1.0f - localT) + clip.keyframes[next].rotate.z * localT;

        // 拡大を線形補間する
        transform.scale.x = clip.keyframes[i].scale.x * (1.0f - localT) + clip.keyframes[next].scale.x * localT;
        transform.scale.y = clip.keyframes[i].scale.y * (1.0f - localT) + clip.keyframes[next].scale.y * localT;
        transform.scale.z = clip.keyframes[i].scale.z * (1.0f - localT) + clip.keyframes[next].scale.z * localT;
    }

    // 停止中 (エディタ): 軌跡を黄色の線、キーフレームを十字で描く (選択中は赤)
    void Draw(entt::registry& registry, GameContext& ctx) override {
        // PLAY 中は描かない
        if (!ctx.isPlaying) {
            auto view = registry.view<MotionComponent, TransformComponent>();
            for (auto entity : view) {
                auto& motion = view.get<MotionComponent>(entity);
                auto it = motion.clips.find(motion.activeClip);
                if (it == motion.clips.end()) continue;
                const auto& clip = it->second;
                if (clip.keyframes.size() < 2) continue;

                // 親のワールド行列を取得
                DirectX::XMMATRIX parentWorld = DirectX::XMMatrixIdentity();
                if (registry.all_of<HierarchyComponent>(entity)) {
                    auto& hc = registry.get<HierarchyComponent>(entity);
                    if (hc.parentId != entt::null && registry.valid(hc.parentId)) {
                        // GameScene から取得する (GameContext の scene を使う)
                        auto worldMat = ctx.scene->GetWorldMatrix((int)hc.parentId);
                        parentWorld = DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&worldMat));
                    }
                }

                // 軌跡を 50 分割の線で描く
                const int segments = 50;
                Engine::Vector3 prevP;
                std::vector<DirectX::XMFLOAT3> posPoints;
                for (const auto& kf : clip.keyframes) posPoints.push_back(kf.translate);

                for (int i = 0; i <= segments; ++i) {
                    float t = (static_cast<float>(i) / segments) * (clip.keyframes.size() - 1);
                    DirectX::XMVECTOR pLocal = Engine::Spline::Interpolate(posPoints, t);
                    // ワールド座標に変換
                    DirectX::XMVECTOR pWorld = DirectX::XMVector3TransformCoord(pLocal, parentWorld);
                    
                    Engine::Vector3 currP;
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&currP), pWorld);
                    
                    if (i > 0) ctx.renderer->DrawLine3D(prevP, currP, {1, 1, 0, 1}, true);
                    prevP = currP;
                }

                // 各キーフレームの位置に十字を描く (選択中は赤、それ以外は水色)
                for (int i = 0; i < (int)clip.keyframes.size(); ++i) {
                    Engine::Vector4 color = (i == motion.selectedKeyframe) ? Engine::Vector4{1, 0, 0, 1} : Engine::Vector4{0, 1, 1, 1};
                    DirectX::XMVECTOR pLocal = DirectX::XMLoadFloat3(&clip.keyframes[i].translate);
                    DirectX::XMVECTOR pWorld = DirectX::XMVector3TransformCoord(pLocal, parentWorld);
                    Engine::Vector3 p;
                    DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&p), pWorld);

                    // 十字の大きさ
                    float s = 0.2f;
                    ctx.renderer->DrawLine3D({p.x - s, p.y, p.z}, {p.x + s, p.y, p.z}, color, true);
                    ctx.renderer->DrawLine3D({p.x, p.y - s, p.z}, {p.x, p.y + s, p.z}, color, true);
                    ctx.renderer->DrawLine3D({p.x, p.y, p.z - s}, {p.x, p.y, p.z + s}, color, true);
                }
            }
        }
    }

    // PLAY 開始時: 有効なモーションを最初から再生する
    void Reset(entt::registry& registry) override {
        // 有効なモーションを持つエンティティを、時刻 0 から再生し直す
        auto view = registry.view<MotionComponent>();
        for (auto entity : view) {
            auto& motion = registry.get<MotionComponent>(entity);
            if (motion.enabled) {
                motion.isPlaying = true;
                motion.currentTime = 0.0f;
            }
        }
    }
};

} // namespace Game
