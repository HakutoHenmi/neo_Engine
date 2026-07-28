#ifndef GPUPARTICLE_H
#define GPUPARTICLE_H

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <DirectXMath.h>
#include "Matrix4x4.h"

namespace Engine {

struct GPUParticleData {
    DirectX::XMFLOAT3 position;
    float life;
    DirectX::XMFLOAT3 velocity;
    float age;
    DirectX::XMFLOAT4 colorStart;
    DirectX::XMFLOAT4 colorEnd;
    DirectX::XMFLOAT3 scale;
    uint32_t particleType; // 0: Normal, 1: Trail, 2: Lit
};

struct GPUParticleEmitterData {
    DirectX::XMFLOAT3 emitPos;
    float emitRate;
    DirectX::XMFLOAT3 emitVel;
    float emitLife;

    // --- New features ---
    uint32_t emitterShape; // 0: Point, 1: Sphere, 2: Box, 3: Mesh
    DirectX::XMFLOAT3 emitterExtents; // Radius (x) or Box XYZ

    DirectX::XMFLOAT4 colorStart;
    DirectX::XMFLOAT4 colorEnd;

    uint32_t fieldType; // 0: None, 1: Gravity, 2: Wind, 3: Tornado
    DirectX::XMFLOAT3 fieldPos; // Center of field if applicable
    DirectX::XMFLOAT4 fieldParams; // x: Strength, y: Radius, z: Height, w: misc
    
    uint32_t particleType; // 0: Normal, 1: Trail, 2: Lit
    DirectX::XMFLOAT3 pad;
};

class GPUParticleSystem {
public:
    GPUParticleSystem() = default;
    ~GPUParticleSystem();

    bool Initialize(ID3D12Device* device, uint32_t maxParticles);
    void Update(ID3D12GraphicsCommandList* cmd, float dt, const std::vector<GPUParticleEmitterData>& emitters);
    void Draw(ID3D12GraphicsCommandList* cmd, const Matrix4x4& viewProj, const DirectX::XMFLOAT3& camPos, bool useBillboard = true);
    
    void SetEmitterMesh(D3D12_GPU_VIRTUAL_ADDRESS meshVBAddr, uint32_t vertexCount, uint32_t stride);


private:
    bool CreateBuffers(ID3D12Device* device);
    bool CreatePipelines(ID3D12Device* device);

private:
    uint32_t maxParticles_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;
    
    bool isInitialized_ = false;
    bool initDone_ = false;
    D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;
    
    D3D12_GPU_VIRTUAL_ADDRESS meshVBAddr_ = 0;
    uint32_t meshVertexCount_ = 0;
    uint32_t meshVertexStride_ = 0;
};

} // namespace Engine
#endif // GPUPARTICLE_H
