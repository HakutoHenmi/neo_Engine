#ifndef GPUPARTICLE_H
#define GPUPARTICLE_H

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <DirectXMath.h>
#include "../Math/Matrix4x4.h"

namespace Engine {

struct GPUParticleData {
	DirectX::XMFLOAT3 position;
	float life;
	DirectX::XMFLOAT3 velocity;
	float age;
	DirectX::XMFLOAT4 colorStart;
	DirectX::XMFLOAT4 colorEnd;
	DirectX::XMFLOAT3 scale;
	uint32_t particleType; // 0: 通常, 1: 軌跡, 2: ライティングあり
};

struct GPUParticleEmitterData {
	DirectX::XMFLOAT3 emitPos;
	float emitRate;
	DirectX::XMFLOAT3 emitVel;
	float emitLife;

	// --- 追加の機能 ---
	uint32_t emitterShape;            // 0: 点, 1: 球, 2: 箱, 3: メッシュ
	DirectX::XMFLOAT3 emitterExtents; // 半径 (x) または箱の XYZ

	DirectX::XMFLOAT4 colorStart;
	DirectX::XMFLOAT4 colorEnd;

	uint32_t fieldType;            // 0: なし, 1: 重力, 2: 風, 3: 竜巻
	DirectX::XMFLOAT3 fieldPos;    // 場の中心 (使う場合)
	DirectX::XMFLOAT4 fieldParams; // x: 強さ, y: 半径, z: 高さ, w: その他

	uint32_t particleType; // 0: 通常, 1: 軌跡, 2: ライティングあり
	DirectX::XMFLOAT3 pad;
};

class GPUParticleSystem {
public:
	GPUParticleSystem() = default;
	~GPUParticleSystem();

	bool Initialize(ID3D12Device* device, uint32_t maxParticles);
	void Update(ID3D12GraphicsCommandList* cmd, float dt, const std::vector<GPUParticleEmitterData>& emitters);
	void Draw(
	    ID3D12GraphicsCommandList* cmd, const Matrix4x4& viewProj, const DirectX::XMFLOAT3& camPos,
	    bool useBillboard = true);

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
