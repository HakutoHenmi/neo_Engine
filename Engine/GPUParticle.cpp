#include "GPUParticle.h"
#include "Renderer.h"
#include <d3dx12.h>

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace Engine {

GPUParticleSystem::~GPUParticleSystem() {}

bool GPUParticleSystem::Initialize(ID3D12Device* device, uint32_t maxParticles) {
    if (isInitialized_) return true; // ★追加: 二重初期化を防止
    maxParticles_ = maxParticles;
    if (!CreateBuffers(device)) return false;
    if (!CreatePipelines(device)) return false;
    
    isInitialized_ = true;
    return true;
}

#include <cstdio>

static void LogFileGPU(const char* msg) {
	FILE* f = nullptr;
	fopen_s(&f, "C:\\Users\\k024g\\source\\repos\\neo_Engine\\error_log.txt", "a");
	if (f) {
		fputs(msg, f);
		fputc('\n', f);
		fclose(f);
	}
}

static Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromPath(const wchar_t* filename, const char* entrypoint, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompileFromFile(filename, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entrypoint, target,
        D3DCOMPILE_DEBUG, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) {
            OutputDebugStringA((char*)err->GetBufferPointer());
            LogFileGPU((char*)err->GetBufferPointer());
        }
        return nullptr;
    }
    return blob;
}

bool GPUParticleSystem::CreateBuffers(ID3D12Device* device) {
    auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    // Particle Pool (UAV + SRV)
    auto poolDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(GPUParticleData) * maxParticles_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &poolDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&particleBuffer_)))) return false;

    return true;
}

bool GPUParticleSystem::CreatePipelines(ID3D12Device* device) {
    // === コンピュート用 Root Signature ===
    // Root[0]: 32bit Constants (b0) - 32 DWORDs
    // Root[1]: UAV (u0) - ParticlePool
    // Root[2]: SRV (t0) - MeshVertices (Optional)
    CD3DX12_ROOT_PARAMETER cParams[3];
    cParams[0].InitAsConstants(32, 0); // b0
    cParams[1].InitAsUnorderedAccessView(0); // u0
    cParams[2].InitAsShaderResourceView(0); // t0

    CD3DX12_ROOT_SIGNATURE_DESC cSigDesc(_countof(cParams), cParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    Microsoft::WRL::ComPtr<ID3DBlob> cSig, cErr;
    if (FAILED(D3D12SerializeRootSignature(&cSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &cSig, &cErr))) {
        if (cErr) OutputDebugStringA((char*)cErr->GetBufferPointer());
        return false;
    }
    device->CreateRootSignature(0, cSig->GetBufferPointer(), cSig->GetBufferSize(), IID_PPV_ARGS(&computeRootSig_));

    auto initCS = CompileShaderFromPath(L"Resources/shaders/GPUParticleCS.hlsl", "InitCS", "cs_5_0");
    auto emitCS = CompileShaderFromPath(L"Resources/shaders/GPUParticleCS.hlsl", "EmitCS", "cs_5_0");
    auto updateCS = CompileShaderFromPath(L"Resources/shaders/GPUParticleCS.hlsl", "UpdateCS", "cs_5_0");
    if (!initCS || !emitCS || !updateCS) return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsd = {};
    cpsd.pRootSignature = computeRootSig_.Get();
    cpsd.CS = { initCS->GetBufferPointer(), initCS->GetBufferSize() };
    if (FAILED(device->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&initPSO_)))) return false;
    cpsd.CS = { emitCS->GetBufferPointer(), emitCS->GetBufferSize() };
    if (FAILED(device->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&emitPSO_)))) return false;
    cpsd.CS = { updateCS->GetBufferPointer(), updateCS->GetBufferSize() };
    if (FAILED(device->CreateComputePipelineState(&cpsd, IID_PPV_ARGS(&updatePSO_)))) return false;

    // === グラフィックス用 Root Signature ===
    // Root[0]: 32bit Constants (b0) - 20 DWORDs (ViewProj 16 + CamPos 3 + useBillboard 1)
    // Root[1]: SRV (t0) - ParticlePool (ルートSRV)
    // Root[2]: CBV (b1) - LightCB
    CD3DX12_ROOT_PARAMETER gParams[3];
    gParams[0].InitAsConstants(20, 0); // b0
    gParams[1].InitAsShaderResourceView(0); // t0 - ルートSRV
    gParams[2].InitAsConstantBufferView(1); // b1 - LightCB

    CD3DX12_ROOT_SIGNATURE_DESC gSigDesc(_countof(gParams), gParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE); // IAは使わない (頂点なし)
    Microsoft::WRL::ComPtr<ID3DBlob> gSig, gErr;
    if (FAILED(D3D12SerializeRootSignature(&gSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &gSig, &gErr))) {
        if (gErr) OutputDebugStringA((char*)gErr->GetBufferPointer());
        return false;
    }
    device->CreateRootSignature(0, gSig->GetBufferPointer(), gSig->GetBufferSize(), IID_PPV_ARGS(&graphicsRootSig_));

    auto vs = CompileShaderFromPath(L"Resources/shaders/GPUParticleVS.hlsl", "main", "vs_5_0");
    auto ps = CompileShaderFromPath(L"Resources/shaders/GPUParticlePS.hlsl", "main", "ps_5_0");
    if (!vs || !ps) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsd = {};
    gpsd.pRootSignature = graphicsRootSig_.Get();
    gpsd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    gpsd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    // Additive Blend
    gpsd.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    gpsd.BlendState.RenderTarget[0].BlendEnable = TRUE;
    gpsd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    gpsd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    gpsd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    gpsd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    gpsd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    gpsd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    gpsd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    gpsd.SampleMask = UINT_MAX;
    gpsd.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    gpsd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gpsd.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    gpsd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    gpsd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gpsd.NumRenderTargets = 1;
    gpsd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpsd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    gpsd.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&gpsd, IID_PPV_ARGS(&drawPSO_)))) {
        OutputDebugStringA("[GPUParticle] Failed to create graphics PSO\n");
        return false;
    }

    return true;
}

void GPUParticleSystem::Update(ID3D12GraphicsCommandList* cmd, float dt, const std::vector<GPUParticleEmitterData>& emitters) {
    if (!isInitialized_ || !cmd || !updatePSO_ || !emitPSO_) return;

    static float totalTime = 0.0f;
    totalTime += dt;

    // 状態遷移: 現在の状態 -> UAV
    if (currentState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(), currentState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &barrier);
        currentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    cmd->SetComputeRootSignature(computeRootSig_.Get());

    // Prepare Update Constants
    uint32_t updateConstants[32] = {};
    memcpy(&updateConstants[0], &dt, 4);
    memcpy(&updateConstants[1], &totalTime, 4);
    updateConstants[2] = maxParticles_;
    if (!emitters.empty()) {
        const auto& ed = emitters[0];
        updateConstants[24] = ed.fieldType;
        memcpy(&updateConstants[25], &ed.fieldPos, 12);
        memcpy(&updateConstants[28], &ed.fieldParams, 16);
    }
    
    // Bind Constants and UAV (needed for InitCS and UpdateCS)
    cmd->SetComputeRoot32BitConstants(0, 32, updateConstants, 0);
    cmd->SetComputeRootUnorderedAccessView(1, particleBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootShaderResourceView(2, meshVBAddr_ != 0 ? meshVBAddr_ : particleBuffer_->GetGPUVirtualAddress());

    // --- 1. InitCS (初回のみ) ---
    if (!initDone_ && initPSO_) {
        cmd->SetPipelineState(initPSO_.Get());
        cmd->Dispatch((maxParticles_ + 255) / 256, 1, 1);
        initDone_ = true;
        auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
        cmd->ResourceBarrier(1, &uavBarrier);
    }

    // --- 2. UpdateCS ---
    // 1thread = 4 particles, so dispatch maxParticles / 4 / 256
    uint32_t dispatchGroups = (maxParticles_ + (256 * 4) - 1) / (256 * 4);
    cmd->SetPipelineState(updatePSO_.Get());
    cmd->Dispatch(dispatchGroups, 1, 1);

    auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    // --- 3. EmitCS (エミッターごとに実行) ---
    cmd->SetPipelineState(emitPSO_.Get());
    
    for (const auto& ed : emitters) {
        uint32_t emitCount = (uint32_t)(ed.emitRate * dt * 10.0f);
        if (emitCount == 0 && ed.emitRate > 0) emitCount = 1;

        if (emitCount > 0) {
            uint32_t emitConstants[32] = {};
            memcpy(&emitConstants[0], &dt, 4);
            memcpy(&emitConstants[1], &totalTime, 4);
            emitConstants[2] = maxParticles_;
            emitConstants[3] = emitCount;
            memcpy(&emitConstants[4], &ed.emitPos, 12);
            memcpy(&emitConstants[7], &ed.emitRate, 4);
            memcpy(&emitConstants[8], &ed.emitVel, 12);
            memcpy(&emitConstants[11], &ed.emitLife, 4);
            
            const bool canUseMeshEmitter = (ed.emitterShape == 3 && meshVBAddr_ != 0 && meshVertexCount_ > 0);
            emitConstants[12] = canUseMeshEmitter ? 3 : ed.emitterShape;
            if (ed.emitterShape == 3 && !canUseMeshEmitter) {
                emitConstants[12] = 0;
            }
            
            // Mesh emitters pass vertex count/stride through the bit pattern of extents.xy.
            DirectX::XMFLOAT3 extents = ed.emitterExtents;
            if (canUseMeshEmitter) {
                memcpy(&extents.x, &meshVertexCount_, 4);
                memcpy(&extents.y, &meshVertexStride_, 4);
            }
            memcpy(&emitConstants[13], &extents, 12);
            
            memcpy(&emitConstants[16], &ed.colorStart, 16);
            memcpy(&emitConstants[20], &ed.colorEnd, 16);
            
            // Add particle type
            emitConstants[24] = ed.particleType;
            
            // Random offset per emitter based on position to avoid pattern
            float rOff = ed.emitPos.x * 10.0f + ed.emitPos.y;
            memcpy(&emitConstants[25], &rOff, 4); // Use fieldPos.x as random seed offset

            cmd->SetComputeRoot32BitConstants(0, 32, emitConstants, 0);
            cmd->Dispatch((emitCount + 63) / 64, 1, 1);
            
            cmd->ResourceBarrier(1, &uavBarrier);
        }
    }
}

void GPUParticleSystem::SetEmitterMesh(D3D12_GPU_VIRTUAL_ADDRESS meshVBAddr, uint32_t vertexCount, uint32_t stride) {
    meshVBAddr_ = meshVBAddr;
    meshVertexCount_ = vertexCount;
    meshVertexStride_ = stride;
}

void GPUParticleSystem::Draw(ID3D12GraphicsCommandList* cmd, const Matrix4x4& viewProj, const DirectX::XMFLOAT3& camPos, bool useBillboard) {
    if (!isInitialized_ || !cmd || !drawPSO_) {
        OutputDebugStringA("GPUParticleSystem::Draw ABORTED: Not initialized or missing PSO\n");
        return;
    }
    
    char dbgMsg[256];
    sprintf_s(dbgMsg, "GPUParticleSystem::Draw executing with %u particles!\n", maxParticles_);
    OutputDebugStringA(dbgMsg);

    // 状態遷移: UAV -> SRV (頂点シェーダーから読むので NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE)
    if (currentState_ != (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffer_.Get(), currentState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &barrier);
        currentState_ = (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    cmd->SetPipelineState(drawPSO_.Get());
    cmd->SetGraphicsRootSignature(graphicsRootSig_.Get());

    uint32_t constants[20];
    memcpy(&constants[0], &viewProj, 64);
    memcpy(&constants[16], &camPos, 12);
    constants[19] = useBillboard ? 1 : 0;
    cmd->SetGraphicsRoot32BitConstants(0, 20, constants, 0);

    // ルートSRVでバッファを直接バインド
    cmd->SetGraphicsRootShaderResourceView(1, particleBuffer_->GetGPUVirtualAddress());

    // LightCB
    cmd->SetGraphicsRootConstantBufferView(2, Renderer::GetInstance()->GetLightCBAddr());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, maxParticles_, 0, 0);
}

} // namespace Engine
