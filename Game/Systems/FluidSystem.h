// FluidSystem.h
// 旧スライムの GPU 流体 (SPH) のシミュレーションと描画。
// 粒子をコンピュートシェーダーで動かし、点の板ポリゴンとして描く。SSFR 用のテクスチャも用意する。
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <vector>
#include <random>
#include <cmath>
#include "Matrix4x4.h"
#include "../../Engine/Renderer.h"
#include "../../Engine/PathUtils.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace Game {

// 流体の粒子 1 個 (GPU のバッファと同じ並び)
struct FluidParticle {
    // 位置と密度
    DirectX::XMFLOAT3 position;
    float density;
    // 速度と圧力
    DirectX::XMFLOAT3 velocity;
    float pressure;
    // 力 (詰め物で 16 バイトにそろえる)
    DirectX::XMFLOAT3 force;
    float pad1;
    DirectX::XMFLOAT3 restPosition; // 形を覚えるための元の位置
    float pad2;
};

// 定数バッファの 256 バイトの整列で出る警告を止める
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
// シミュレーション用の定数 (コンピュートシェーダーへ渡す)
struct alignas(256) FluidConstants {
    // 核の位置と粒子の数
    DirectX::XMFLOAT3 corePosition;
    uint32_t numParticles;
    
    // 時間の刻み・影響の半径・粒子の質量・目標の密度
    float deltaTime;
    float smoothingLength;
    float particleMass;
    float restDensity;
    
    // 圧力の強さ・粘性・重力・減衰
    float gasStiffness;
    float viscosity;
    float gravity;
    float damping;
    
    // 床の高さ・計算のパス (0: 密度, 1: 力)・モード
    float floorWorldY;
    uint32_t passType;
    uint32_t simMode; // 0: スライム球, 1: 液状化
    float pad0;
    // 塊の半径
    DirectX::XMFLOAT3 blobRadii;
    float pad1;
    DirectX::XMFLOAT3 playerInputForce; // プレイヤーから加える外力
    float pad2;
};

// 描画用の定数 (カメラの行列・時刻・中心・液状化中か・色)
struct alignas(256) FluidRenderConstants {
    // ビュー・射影・ビュー射影・それぞれの逆行列
    Engine::Matrix4x4 view;
    Engine::Matrix4x4 projection;
    Engine::Matrix4x4 viewProj;
    Engine::Matrix4x4 invProjection;
    Engine::Matrix4x4 invView;
    // カメラの位置と時刻
    DirectX::XMFLOAT3 cameraPos;
    float time;
    // 核の位置と液状化中か
    DirectX::XMFLOAT3 corePosition;
    float isLiquidated;
    // スライムの色
    DirectX::XMFLOAT3 blobColor;
    float padColor;
};

// 粒子を作り直すときの形 (スライムの塊 / 水たまり)
enum class FluidResetShape {
    Blob,
    Puddle
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// 流体の管理 (シングルトン)
class FluidSystem {
public:
    // 唯一のインスタンス
    static FluidSystem* GetInstance() {
        static FluidSystem instance;
        return &instance;
    }

    // SSFR 用の深度・厚み・なめらかにした深度のテクスチャ
    // SSFR用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> ssfrDepthTex_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ssfrThicknessTex_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ssfrSmoothedDepthTex_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ssfrDsvTex_; // 深度テスト用のバッファ
    
    // SSFR用のディスクリプタヒープ (RTV/SRV/DSV)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrRtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrSrvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssfrDsvHeap_; // 深度テスト用のヒープ
    // ディスクリプタの大きさ
    uint32_t rtvDescriptorSize_ = 0;
    uint32_t srvDescriptorSize_ = 0;
    
    // 画面サイズ（初期化時に設定）
    uint32_t screenWidth_ = 1920;
    uint32_t screenHeight_ = 1080;

    // 初期化: ルートシグネチャ・シェーダー・パイプライン・粒子と定数のバッファを作る
    bool Initialize(ID3D12Device* device);

    // スライムの形 (広く平らな裾野と小さな頭) の中に粒子を無作為に置く
    void FillLocalBlobParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii);

    // 水たまりの形 (平たい円) に粒子を置き、少し下向きの速度を付ける
    void FillLocalPuddleParticles(std::vector<FluidParticle>& out, DirectX::XMFLOAT3 radii);

    // 指定の形で粒子を作り直し、アップロード用バッファへ書く
    void RebuildParticles(FluidResetShape shape, DirectX::XMFLOAT3 radii);

    // 毎フレームのシミュレーション: 必要なら粒子を作り直し、密度と圧力 → 力と移動の2回に分けて計算する
    void Update(ID3D12GraphicsCommandList* list, float dt, const DirectX::XMFLOAT3& corePos,
                DirectX::XMFLOAT3 blobRadii, bool isLiquidated, DirectX::XMFLOAT3 inputForce = {0.0f, 0.0f, 0.0f}) {
        // 最初の更新をログに残す
        static bool firstUpdate = true;
        if (firstUpdate) {
            firstUpdate = false;
            FILE* fp = nullptr;
            fopen_s(&fp, "fluid_debug.txt", "a");
            if (fp) {
                fprintf(fp, "[FluidSystem] First Update: isLiquidated=%d\n", isLiquidated);
                fclose(fp);
            }
        }
        // 初期化前なら何もしない
        if (!isInitialized_ || !pso_ || !particleBuffer_) return;

        // 作り直しの依頼があれば、アップロード用のバッファから粒子のバッファへコピーする
        if (resetRequested_) {
            FILE* fp = nullptr;
            fopen_s(&fp, "fluid_debug.txt", "a");
            if (fp) {
                fprintf(fp, "[UpdateReset] executing: shape=%d, radii=(%.2f, %.2f, %.2f)\n", (int)resetShape_, resetRadii_.x, resetRadii_.y, resetRadii_.z);
                fclose(fp);
            }

            RebuildParticles(resetShape_, resetRadii_);
            // 2 回目以降は UAV からコピー先へ切り替える
            if (!firstFrame_) {
                auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                    particleBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
                list->ResourceBarrier(1, &toCopyDest);
            }
            list->CopyResource(particleBuffer_.Get(), particleUploadBuffer_.Get());
            auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
                particleBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &toUav);
            resetRequested_ = false;
            firstFrame_ = false;
        // 最初のフレームは初期の配置をコピーする
        } else if (firstFrame_) {
            list->CopyResource(particleBuffer_.Get(), particleUploadBuffer_.Get());
            auto transition = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->ResourceBarrier(1, &transition);
            firstFrame_ = false;
        }

        // シミュレーションの定数 (液状化中は柔らかく、スライムの形のときは硬く粘る)
        FluidConstants constants{};
        // 核の位置・粒子数・時間 (上限 0.05 秒)・各定数
        constants.corePosition = corePos;
        constants.numParticles = numParticles_;
        constants.deltaTime = dt > 0.05f ? 0.05f : dt;
        constants.smoothingLength = 0.30f;
        constants.particleMass = 0.1f;
        constants.restDensity = 150.0f;
        // スライム時: 重力ほぼゼロ。地面衝突で底が平らになり自然なドーム型になる
        constants.gravity = isLiquidated ? -2.0f : -0.5f;
        constants.floorWorldY = 0.0f;
        constants.blobRadii = blobRadii;
        constants.simMode = isLiquidated ? 1u : 0u;
        constants.playerInputForce = inputForce;

        if (isLiquidated) {
            constants.gasStiffness = 60.0f;
            constants.viscosity = 15.0f;
            constants.damping = 0.990f;
        } else {
            // ドーム型スライム: 強い圧力 + 高粘度 + 表面張力
            constants.gasStiffness = 600.0f;
            constants.viscosity = 25.0f; // 粘度を上げて重みを出す
            constants.damping = 0.985f;
        }

        // 計算パイプラインと粒子のバッファを設定する
        list->SetPipelineState(pso_.Get());
        list->SetComputeRootSignature(rootSig_.Get());
        list->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());

        // 256 個ずつのグループ
        uint32_t numGroups = (numParticles_ + 255) / 256;

        // パス 0 (密度と圧力)
        constants.passType = 0;
        memcpy(cbMapped_, &constants, sizeof(FluidConstants));
        list->SetComputeRootConstantBufferView(0, cbUploadBuffer_->GetGPUVirtualAddress());
        list->Dispatch(numGroups, 1, 1);

        // パス 0 の結果を書き終えるまで待つ
        auto barrier1 = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
        list->ResourceBarrier(1, &barrier1);

        // パス 1 (力と移動)
        constants.passType = 1;
        memcpy((uint8_t*)cbMapped_ + sizeof(FluidConstants), &constants, sizeof(FluidConstants));
        list->SetComputeRootConstantBufferView(0, cbUploadBuffer_->GetGPUVirtualAddress() + sizeof(FluidConstants));
        list->Dispatch(numGroups, 1, 1);

        // パス 1 の結果を書き終えるまで待つ
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
        list->ResourceBarrier(1, &barrier2);
    }

    // 粒子を板ポリゴンとしてメイン画面に描く
    void Draw(ID3D12GraphicsCommandList* list, Engine::Camera* camera, float time,
              DirectX::XMFLOAT3 corePos, DirectX::XMFLOAT3 blobRadii, bool isLiquidated,
              D3D12_CPU_DESCRIPTOR_HANDLE mainRtv, D3D12_CPU_DESCRIPTOR_HANDLE mainDsv) {
        (void)blobRadii; // 未使用変数の警告(C4100)を回避
        // 初期化前なら描かない
        if (!isInitialized_ || !psoRender_) return;

        // カメラの行列と逆行列を描画の定数に入れる
        FluidRenderConstants rcb{};
        DirectX::XMMATRIX viewMat = camera->View();
        DirectX::XMMATRIX projMat = camera->Proj();
        DirectX::XMMATRIX viewProjMat = DirectX::XMMatrixMultiply(viewMat, projMat);
        DirectX::XMMATRIX invProjMat = DirectX::XMMatrixInverse(nullptr, projMat);
        DirectX::XMMATRIX invViewMat = DirectX::XMMatrixInverse(nullptr, viewMat);
        
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.view), viewMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.projection), projMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.viewProj), viewProjMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.invProjection), invProjMat);
        DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&rcb.invView), invViewMat);
        
        // カメラの位置・時刻・核・液状化・色
        Engine::Vector3 camPos = camera->GetPosition();
        rcb.cameraPos = { camPos.x, camPos.y, camPos.z };
        rcb.time = time;
        rcb.corePosition = corePos;
        rcb.isLiquidated = isLiquidated ? 1.0f : 0.0f;
        rcb.blobColor = { 0.15f, 0.85f, 0.4f };
        // 定数をアップロードする
        memcpy(cbRenderMapped_, &rcb, sizeof(FluidRenderConstants));

        // 描画先をメイン画面にする
        list->OMSetRenderTargets(1, &mainRtv, FALSE, &mainDsv);

        // ビューポートとシザーの設定
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)screenWidth_, (float)screenHeight_, 0.0f, 1.0f };
        D3D12_RECT scissorRect = { 0, 0, (LONG)screenWidth_, (LONG)screenHeight_ };
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissorRect);

        // 描画のパイプラインを設定する
        list->SetPipelineState(psoRender_.Get());
        list->SetGraphicsRootSignature(rootSigRender_.Get());
        
        // 粒子のバッファを UAV から SRV へ切り替える
        auto transitionToSrv = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &transitionToSrv);
        
        // 粒子のバッファと定数を渡す
        list->SetGraphicsRootShaderResourceView(0, particleBuffer_->GetGPUVirtualAddress());
        list->SetGraphicsRootConstantBufferView(1, cbRenderUploadBuffer_->GetGPUVirtualAddress());

        // インスタンス描画 (粒子の数だけ、1 個につき 4 頂点の四角形)
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        list->DrawInstanced(4, numParticles_, 0, 0);

        // 粒子のバッファを SRV から UAV へ戻す (次フレームのコンピュート用)
        auto transitionToUav = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        
        list->ResourceBarrier(1, &transitionToUav);
    }

    // 次の更新で粒子を指定の形に作り直す
    void RequestParticleReset(FluidResetShape shape, DirectX::XMFLOAT3 radii);

    // 粒子のバッファ・粒子の数・初期化済みか
    ID3D12Resource* GetParticleBuffer() const { return particleBuffer_.Get(); }
    uint32_t GetNumParticles() const { return numParticles_; }
    bool IsInitialized() const { return isInitialized_; }

private:
    FluidSystem() = default;

    // デバイス・シミュレーションと描画のパイプライン
    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
    
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigRender_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoRender_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigComposite_; // 合成用のルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoComposite_; // 合成用のパイプライン

    // 粒子のバッファ・アップロード用バッファ・定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cbUploadBuffer_;
    FluidConstants* cbMapped_ = nullptr;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> cbRenderUploadBuffer_;
    FluidRenderConstants* cbRenderMapped_ = nullptr;

    uint32_t numParticles_ = 2048; // O(N^2) の重い計算を避けるため粒子の数を減らしている
    // 初期化済みか・最初のフレームか・作り直しの依頼
    bool isInitialized_ = false;
    bool firstFrame_ = true;
    bool resetRequested_ = false;
    FluidResetShape resetShape_ = FluidResetShape::Blob;
    DirectX::XMFLOAT3 resetRadii_ = { 1.2f, 0.85f, 1.2f };

    // SSFR (画面空間での流体描画) 用のテクスチャとビューを作る
    void CreateSSFRResources();
};

} // namespace Game
