// RiverSystem.cpp
// 川のメッシュ生成 (スプライン補間・地形への沿わせ・帯メッシュの作成)。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "RiverSystem.h"
#include "ObjectTypes.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/Model.h"

namespace Game {

using namespace DirectX;

// 制御点を Catmull-Rom スプラインで補間し、t (区間番号 + 区間内の割合) の位置を返す
XMVECTOR RiverSystem::InterpolateSpline(const std::vector<XMFLOAT3>& points, float t) {
    // 点が無ければ原点、1 つならその点
    if (points.empty()) return XMVectorZero();
    if (points.size() == 1) return XMLoadFloat3(&points[0]);

    // t の整数部が区間、小数部が区間内の位置。最後の区間を超えたら終端に合わせる
    int numSections = (int)points.size() - 1;
    int curSection = (int)t;
    if (curSection >= numSections) {
        curSection = numSections - 1;
        t = (float)numSections;
    }
    float localT = t - curSection;

    // 前後の点の番号 (端では端の点を繰り返す)
    int p0 = (std::max)(0, curSection - 1);
    int p1 = curSection;
    int p2 = (std::min)((int)points.size() - 1, curSection + 1);
    int p3 = (std::min)((int)points.size() - 1, curSection + 2);

    // 4 つの点を読み込んで Catmull-Rom で補間する
    XMVECTOR v0 = XMLoadFloat3(&points[p0]);
    XMVECTOR v1 = XMLoadFloat3(&points[p1]);
    XMVECTOR v2 = XMLoadFloat3(&points[p2]);
    XMVECTOR v3 = XMLoadFloat3(&points[p3]);

    return XMVectorCatmullRom(v0, v1, v2, v3, localT);
}

// 親子関係をたどってエンティティのワールド行列を求める
static Engine::Matrix4x4 GetWorldMatrixInternal(entt::registry& registry, entt::entity entity) {
    // 有効でないかトランスフォームが無ければ単位行列
    if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity)) return Engine::Matrix4x4::Identity();
    
    // 自分のローカル行列
    Engine::Matrix4x4 local = registry.get<TransformComponent>(entity).ToMatrix();
    
    // 親があれば親のワールド行列を掛ける
    if (registry.all_of<HierarchyComponent>(entity)) {
        entt::entity parentId = registry.get<HierarchyComponent>(entity).parentId;
        if (parentId != entt::null) {
            return Engine::Matrix4x4::Multiply(local, GetWorldMatrixInternal(registry, parentId));
        }
    }
    
    return local;
}

// スプラインに沿って、地形の上に左右の幅を持った水面の帯メッシュを作る
void RiverSystem::BuildRiverMesh(RiverComponent& river, Engine::Renderer* renderer, entt::registry& registry, const DirectX::XMFLOAT3& /*ownerPos*/) {
    // 描画が無いか点が 2 つ未満なら作らない
    if (!renderer || river.points.size() < 2) return;

    // 頂点と添字
    std::vector<Engine::VertexData> vertices;
    std::vector<uint32_t> indices;

    // スプライン点はワールド座標で格納済み
    const auto& worldPoints = river.points;

    // 制御点の区間ごとに 10 分割する
    const int subdivisions = 10;
    // 分割点の総数
    int totalSteps = (int)(worldPoints.size() - 1) * subdivisions;

    // 最初の位置と向き、スプラインに沿った距離
    XMVECTOR prevPos = InterpolateSpline(worldPoints, 0.0f);
    XMVECTOR dir = XMVector3Normalize(XMVectorSubtract(InterpolateSpline(worldPoints, 0.01f), prevPos));
    float distanceAlongSpline = 0.0f;

    // 分割点ごとに位置・進行方向・右方向・スプラインに沿った距離を求める
    for (int i = 0; i <= totalSteps; ++i) {
        float t = (float)i / subdivisions;
        // 分割点の位置
        XMVECTOR pos = InterpolateSpline(worldPoints, t);
        
        // 少し先の点との差を進行方向にする (最後の点は前の向きのまま)
        if (i < totalSteps) {
            XMVECTOR nextPos = InterpolateSpline(worldPoints, t + 0.01f);
            dir = XMVector3Normalize(XMVectorSubtract(nextPos, pos));
        }

        // 進行方向をXZ平面に投影して水平な右ベクトルを計算
        XMVECTOR dirXZ = XMVectorSet(XMVectorGetX(dir), 0.0f, XMVectorGetZ(dir), 0.0f);
        dirXZ = XMVector3Normalize(dirXZ);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dirXZ));

        // 前の点との距離を足していく
        if (i > 0) {
            distanceAlongSpline += XMVectorGetX(XMVector3Length(XMVectorSubtract(pos, prevPos)));
        }
        prevPos = pos;

        // --- 地形の高さを取得 ---
        // 上空 500 から真下へレイを飛ばす
        XMVECTOR rayOrig = XMVectorAdd(pos, XMVectorSet(0, 500.0f, 0, 0));
        XMVECTOR rayDir  = XMVectorSet(0, -1.0f, 0, 0);

        float worldX = XMVectorGetX(pos), worldZ = XMVectorGetZ(pos);
        float worldY = 0.0f;
        bool hitTerrain = false;
        
        float closestDist = FLT_MAX;
        // GPU メッシュコライダーへ真下にレイを飛ばし、一番近い地面の高さを使う
        auto view = registry.view<GpuMeshColliderComponent>();
        for (auto e : view) {
            auto& gmc = view.get<GpuMeshColliderComponent>(e);
            auto* model = renderer->GetModel(gmc.meshHandle);
            if (model) {
                Engine::Vector3 hp;
                float dist;
                // 親子関係を含めたワールド行列でレイを判定する
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
        // 水面は地面より 0.5 上
        pos = XMVectorSet(worldX, (hitTerrain ? worldY : XMVectorGetY(pos)) + 0.5f, worldZ, 1.0f);

        // 中心から左右へ幅の半分ずつ離した2頂点を作る (UV の v はスプラインに沿った距離)
        float halfW = river.width * 0.5f;
        XMVECTOR rightEdge = XMVectorAdd(pos, XMVectorScale(right, halfW));
        XMVECTOR leftEdge = XMVectorSubtract(pos, XMVectorScale(right, halfW));

        // 左右の頂点 (法線は上向き、UV の u は 0 と 1)
        Engine::VertexData vL{}, vR{};
        XMStoreFloat4(&vL.position, leftEdge); vL.position.w = 1.0f;
        vL.normal = { XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up) };
        vL.texcoord = { 0.0f, distanceAlongSpline * river.uvScale };

        XMStoreFloat4(&vR.position, rightEdge); vR.position.w = 1.0f;
        vR.normal = { XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up) };
        vR.texcoord = { 1.0f, distanceAlongSpline * river.uvScale };

        // 頂点を加える
        vertices.push_back(vL);
        vertices.push_back(vR);

        // 隣の分割点との間を三角形2枚でつなぐ
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
    // 作ったメッシュのハンドルを記録する
    river.meshHandle = renderer->CreateDynamicMesh(vertices, indices);
}

} // namespace Game
