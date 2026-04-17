// RiverSystem.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "RiverSystem.h"
#include "ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Model.h"

namespace Game {

using namespace DirectX;

XMVECTOR RiverSystem::InterpolateSpline(const std::vector<XMFLOAT3>& points, float t) {
    if (points.empty()) return XMVectorZero();
    if (points.size() == 1) return XMLoadFloat3(&points[0]);

    int numSections = (int)points.size() - 1;
    int curSection = (int)t;
    if (curSection >= numSections) {
        curSection = numSections - 1;
        t = (float)numSections;
    }
    float localT = t - curSection;

    int p0 = (std::max)(0, curSection - 1);
    int p1 = curSection;
    int p2 = (std::min)((int)points.size() - 1, curSection + 1);
    int p3 = (std::min)((int)points.size() - 1, curSection + 2);

    XMVECTOR v0 = XMLoadFloat3(&points[p0]);
    XMVECTOR v1 = XMLoadFloat3(&points[p1]);
    XMVECTOR v2 = XMLoadFloat3(&points[p2]);
    XMVECTOR v3 = XMLoadFloat3(&points[p3]);

    return XMVectorCatmullRom(v0, v1, v2, v3, localT);
}

static Engine::Matrix4x4 GetWorldMatrixInternal(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) return Engine::Matrix4x4::Identity();
    
    Engine::Matrix4x4 local = registry.get<TransformComponent>(entity).ToMatrix();
    
    if (registry.all_of<HierarchyComponent>(entity)) {
        entt::entity parentId = registry.get<HierarchyComponent>(entity).parentId;
        if (parentId != entt::null) {
            return Engine::Matrix4x4::Multiply(local, GetWorldMatrixInternal(registry, parentId));
        }
    }
    
    return local;
}

void RiverSystem::BuildRiverMesh(RiverComponent& river, Engine::Renderer* renderer, entt::registry& registry, const DirectX::XMFLOAT3& /*ownerPos*/) {
    if (!renderer || river.points.size() < 2) return;

    std::vector<Engine::VertexData> vertices;
    std::vector<uint32_t> indices;

    // スプライン点はワールド座標で格納済み
    const auto& worldPoints = river.points;

    const int subdivisions = 10;
    int totalSteps = (int)(worldPoints.size() - 1) * subdivisions;

    XMVECTOR prevPos = InterpolateSpline(worldPoints, 0.0f);
    XMVECTOR dir = XMVector3Normalize(XMVectorSubtract(InterpolateSpline(worldPoints, 0.01f), prevPos));
    float distanceAlongSpline = 0.0f;

    for (int i = 0; i <= totalSteps; ++i) {
        float t = (float)i / subdivisions;
        XMVECTOR pos = InterpolateSpline(worldPoints, t);
        
        if (i < totalSteps) {
            XMVECTOR nextPos = InterpolateSpline(worldPoints, t + 0.01f);
            dir = XMVector3Normalize(XMVectorSubtract(nextPos, pos));
        }

        // 進行方向をXZ平面に投影して水平な右ベクトルを計算
        XMVECTOR dirXZ = XMVectorSet(XMVectorGetX(dir), 0.0f, XMVectorGetZ(dir), 0.0f);
        dirXZ = XMVector3Normalize(dirXZ);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dirXZ));

        if (i > 0) {
            distanceAlongSpline += XMVectorGetX(XMVector3Length(XMVectorSubtract(pos, prevPos)));
        }
        prevPos = pos;

        // --- 地形の高さを取得 ---
        XMVECTOR rayOrig = XMVectorAdd(pos, XMVectorSet(0, 500.0f, 0, 0));
        XMVECTOR rayDir  = XMVectorSet(0, -1.0f, 0, 0);

        float worldX = XMVectorGetX(pos), worldZ = XMVectorGetZ(pos);
        float worldY = 0.0f;
        bool hitTerrain = false;
        
        float closestDist = FLT_MAX;
        auto view = registry.view<GpuMeshColliderComponent>();
        for (auto e : view) {
            auto& gmc = view.get<GpuMeshColliderComponent>(e);
            auto* model = renderer->GetModel(gmc.meshHandle);
            if (model) {
                Engine::Vector3 hp;
                float dist;
                // ★修正: GetWorldMatrixInternal を使用
                if (model->RayCast(rayOrig, rayDir, GetWorldMatrixInternal(registry, e), dist, hp)) {
                    if (dist < closestDist) {
                        closestDist = dist;
                        worldY = hp.y;
                        hitTerrain = true;
                    }
                }
            }
        }
        // 地形にヒットした場合は地形の高さ+オフセット、そうでなければ元の高さ
        pos = XMVectorSet(worldX, (hitTerrain ? worldY : XMVectorGetY(pos)) + 0.5f, worldZ, 1.0f);

        float halfW = river.width * 0.5f;
        XMVECTOR rightEdge = XMVectorAdd(pos, XMVectorScale(right, halfW));
        XMVECTOR leftEdge = XMVectorSubtract(pos, XMVectorScale(right, halfW));

        Engine::VertexData vL{}, vR{};
        XMStoreFloat4(&vL.position, leftEdge); vL.position.w = 1.0f;
        vL.normal = { XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up) };
        vL.texcoord = { 0.0f, distanceAlongSpline * river.uvScale };

        XMStoreFloat4(&vR.position, rightEdge); vR.position.w = 1.0f;
        vR.normal = { XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up) };
        vR.texcoord = { 1.0f, distanceAlongSpline * river.uvScale };

        vertices.push_back(vL);
        vertices.push_back(vR);

        if (i < totalSteps) {
            uint32_t baseIdx = i * 2;
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);

            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 3);
            indices.push_back(baseIdx + 2);
        }
    }

    // 常に新規メッシュを作成（頂点数が変わるため）
    river.meshHandle = renderer->CreateDynamicMesh(vertices, indices);
}

} // namespace Game
