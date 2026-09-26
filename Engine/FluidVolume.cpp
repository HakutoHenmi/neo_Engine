#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>
#include <d3dx12.h>

using Microsoft::WRL::ComPtr;

namespace Engine {
namespace {
constexpr UINT kVolumeWidth = 176;
constexpr UINT kVolumeHeight = 64;
constexpr float kVoxelSize = 0.56f;
constexpr float kVolumeHalfWidth = kVolumeWidth * kVoxelSize * 0.5f;
constexpr D3D12_RESOURCE_STATES kVolumeRead = static_cast<D3D12_RESOURCE_STATES>(
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
// Exact layout of VolumeSettings (b1), NOT a 256-byte padded CBFrame.
struct VolumeConstants {
    Vector3 origin; float cell;
    UINT dimensions[3]; UINT count;
    Vector3 previousOrigin; float historyValid;
    float dt, iso, axis, debug;
    Vector4 colors[3];
};
static_assert(sizeof(VolumeConstants) == 28 * sizeof(UINT));
static_assert(sizeof(Renderer::GPUFluidParticle) == 64);
}

bool Renderer::InitFluidVolume() {
    if (!isGPUFluidReady_) return false;
    CD3DX12_ROOT_PARAMETER compute[13];
    CD3DX12_DESCRIPTOR_RANGE computeRanges[5];
    compute[0].InitAsConstants(28, 1);
    for (UINT i=0; i<4; ++i) compute[i+1].InitAsShaderResourceView(i);
    for (UINT i=0; i<3; ++i) compute[i+5].InitAsUnorderedAccessView(i);
    for (UINT i=0; i<5; ++i) {
        computeRanges[i].Init(i<2 ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i<2 ? i+3 : i+2);
        compute[i+8].InitAsDescriptorTable(1,&computeRanges[i]);
    }
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    auto root = [&](CD3DX12_ROOT_PARAMETER* parameters, UINT count, ComPtr<ID3D12RootSignature>& result) {
        CD3DX12_ROOT_SIGNATURE_DESC desc(count,parameters,1,&sampler);
        ComPtr<ID3DBlob> blob,error;
        if (FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))) {
            if(error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return false;
        }
        return SUCCEEDED(dev_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&result)));
    };
    if(!root(compute,_countof(compute),rootSigVolumeCompute_)) return false;
    CD3DX12_ROOT_PARAMETER draw[10];
    CD3DX12_DESCRIPTOR_RANGE drawRanges[6];
    draw[0].InitAsConstantBufferView(0);
    draw[1].InitAsConstants(28,1);
    for(UINT i=0;i<4;++i) {
        drawRanges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i);
        draw[i+2].InitAsDescriptorTable(1,&drawRanges[i],D3D12_SHADER_VISIBILITY_PIXEL);
    }
    draw[6].InitAsShaderResourceView(4);
    draw[7].InitAsShaderResourceView(5);
    for(UINT i=4;i<6;++i) {
        drawRanges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i+2);
        draw[i+4].InitAsDescriptorTable(1,&drawRanges[i],D3D12_SHADER_VISIBILITY_PIXEL);
    }
    if(!root(draw,_countof(draw),rootSigVolumeDraw_)) return false;
    auto computePso = [&](const char* entry,ComPtr<ID3D12PipelineState>& pso) {
        auto cs=CompileShaderFromFile(L"Resources/shaders/FluidVolumeCS.hlsl",entry,"cs_5_0");
        if(!cs) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=rootSigVolumeCompute_.Get();
        desc.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
        return SUCCEEDED(dev_->CreateComputePipelineState(&desc,IID_PPV_ARGS(&pso)));
    };
    if(!computePso("ClearVolume",psoVolumeClear_) || !computePso("BuildShapes",psoVolumeShapes_) ||
       !computePso("SplatDensity",psoVolumeSplat_) || !computePso("ResolveDensity",psoVolumeResolve_) ||
       !computePso("SmoothDensity",psoVolumeSmooth_) || !computePso("TemporalDensity",psoVolumeTemporal_) ||
       !computePso("BuildOccupancy",psoVolumeOccupancy_)) return false;
    auto vs=CompileShaderFromFile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0");
    auto ps=CompileShaderFromFile(L"Resources/shaders/FluidRaymarch.hlsl","RaymarchPS","ps_5_0");
    auto sprayVS=CompileShaderFromFile(L"Resources/shaders/FluidRaymarch.hlsl","SprayVS","vs_5_0");
    auto sprayPS=CompileShaderFromFile(L"Resources/shaders/FluidRaymarch.hlsl","SprayPS","ps_5_0");
    if(!vs || !ps || !sprayVS || !sprayPS) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature=rootSigVolumeDraw_.Get();
    graphics.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    graphics.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    graphics.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    graphics.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    graphics.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    graphics.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    graphics.DepthStencilState.DepthEnable=FALSE;
    graphics.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    graphics.SampleDesc.Count=1;
    graphics.SampleMask=UINT_MAX;
    graphics.NumRenderTargets=2;
    graphics.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics.RTVFormats[1]=DXGI_FORMAT_R32_FLOAT;
    if(FAILED(dev_->CreateGraphicsPipelineState(&graphics,IID_PPV_ARGS(&psoVolumeRaymarch_)))) return false;
    graphics.NumRenderTargets=1;
    graphics.RTVFormats[1]=DXGI_FORMAT_UNKNOWN;
    graphics.VS={sprayVS->GetBufferPointer(),sprayVS->GetBufferSize()};
    graphics.PS={sprayPS->GetBufferPointer(),sprayPS->GetBufferSize()};
    graphics.BlendState.RenderTarget[0].BlendEnable=TRUE;
    graphics.BlendState.RenderTarget[0].SrcBlend=D3D12_BLEND_SRC_ALPHA;
    graphics.BlendState.RenderTarget[0].DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    if(FAILED(dev_->CreateGraphicsPipelineState(&graphics,IID_PPV_ARGS(&psoVolumeSpray_)))) return false;
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    auto buffer=[&](UINT64 bytes,ComPtr<ID3D12Resource>& target) {
        auto desc=CD3DX12_RESOURCE_DESC::Buffer(bytes,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        return SUCCEEDED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&target)));
    };
    const UINT64 voxels=UINT64(kVolumeWidth)*kVolumeHeight*kVolumeWidth;
    if(!buffer(voxels*16,volumeAccum_) || !buffer(voxels*16,volumeMomentum_) ||
       !buffer(UINT64(gpuFluidMaxParticles_)*64,volumeShapes_)) return false;
    for(UINT i=0;i<6;++i) {
        const UINT width=i==5 ? kVolumeWidth/4 : kVolumeWidth;
        const UINT height=i==5 ? kVolumeHeight/4 : kVolumeHeight;
        auto desc=CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R16G16B16A16_FLOAT,width,height,static_cast<UINT16>(width),1,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,kVolumeRead,nullptr,IID_PPV_ARGS(&volumeTextures_[i])))) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format=desc.Format; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; srv.Texture3D.MipLevels=1;
        UINT index=AllocateSrvIndex();
        dev_->CreateShaderResourceView(volumeTextures_[i].Get(),&srv,window_->SRV_CPU(index));
        dev_->CreateShaderResourceView(volumeTextures_[i].Get(),&srv,window_->SRV_CPU_Master(index));
        volumeSrv_[i]=window_->SRV_GPU(index);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format=desc.Format; uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D; uav.Texture3D.WSize=width;
        index=AllocateSrvIndex();
        dev_->CreateUnorderedAccessView(volumeTextures_[i].Get(),nullptr,&uav,window_->SRV_CPU(index));
        dev_->CreateUnorderedAccessView(volumeTextures_[i].Get(),nullptr,&uav,window_->SRV_CPU_Master(index));
        volumeUav_[i]=window_->SRV_GPU(index);
    }
    auto depthDesc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT,WindowDX::kW,WindowDX::kH,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&depthDesc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&volumeSurfaceDepth_)))) return false;
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeap{};
    rtvHeap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtvHeap.NumDescriptors=1;
    if(FAILED(dev_->CreateDescriptorHeap(&rtvHeap,IID_PPV_ARGS(&volumeRtvHeap_)))) return false;
    dev_->CreateRenderTargetView(volumeSurfaceDepth_.Get(),nullptr,volumeRtvHeap_->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Format=DXGI_FORMAT_R32_FLOAT; depthSrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Texture2D.MipLevels=1; depthSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    UINT index=AllocateSrvIndex();
    dev_->CreateShaderResourceView(volumeSurfaceDepth_.Get(),&depthSrv,window_->SRV_CPU(index));
    dev_->CreateShaderResourceView(volumeSurfaceDepth_.Get(),&depthSrv,window_->SRV_CPU_Master(index));
    volumeSurfaceDepthSrv_=window_->SRV_GPU(index);
    volumeHistoryValid_=false;
    return true;
}

void Renderer::DrawFluidVolume() {
    if(!fluidVolumeReady_ || !cbFrameAddr_ || !gpuFluidActiveParticleCount_) return;
    // Player-local domain, snapped to voxel boundaries. Covers y=-8..27.84
    // at ground level; never put the entire grid below the collision floor.
    Vector3 center=gpuFluidCoreAttraction_>0 ? gpuFluidCorePos_ : cbFrame_.playerPos;
    if(!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return;
    Vector3 origin={std::floor(center.x/kVoxelSize)*kVoxelSize-kVolumeHalfWidth,
                    std::floor(center.y/kVoxelSize)*kVoxelSize-8,
                    std::floor(center.z/kVoxelSize)*kVoxelSize-kVolumeHalfWidth};
    if (gpuFluidCoreMode_ >= 2.0f && gpuFluidTetherBlend_ > 0.01f) {
        const float low = (std::min)(center.y, center.y + (gpuFluidTetherTip_.y - center.y) * gpuFluidTetherBlend_);
        origin.y = std::floor(low / kVoxelSize) * kVoxelSize - 4;
    }
    float move=std::abs(origin.x-volumePreviousOrigin_.x)+std::abs(origin.y-volumePreviousOrigin_.y)+std::abs(origin.z-volumePreviousOrigin_.z);
    if(move>8) volumeHistoryValid_=false;
    VolumeConstants cb{origin,kVoxelSize,{kVolumeWidth,kVolumeHeight,kVolumeWidth},(std::min)(gpuFluidActiveParticleCount_,gpuFluidMaxParticles_),
        volumePreviousOrigin_,volumeHistoryValid_?1.0f:0.0f,fluidSimulatedDt_,0.25f,0,static_cast<float>(fluidVolumeDebugMode_),{volumeColors_[0],volumeColors_[1],volumeColors_[2]}};
    if(gpuFluidCoreMode_>=2.0f){
        cb.colors[0].w=chronoFluidOpacity_;
        cb.colors[0].x+=(1-cb.colors[0].x)*chronoFluidFlash_;
        cb.colors[0].y+=(1-cb.colors[0].y)*chronoFluidFlash_;
        cb.colors[0].z+=(1-cb.colors[0].z)*chronoFluidFlash_;
    }
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(resource,before,after); list_->ResourceBarrier(1,&b);
    };
    auto uavBarrier=[&]() { auto b=CD3DX12_RESOURCE_BARRIER::UAV(nullptr); list_->ResourceBarrier(1,&b); };
    ID3D12Resource* simulationReads[]={gpuFluidSortedParticlesBuffer_.Get(),gpuFluidOriginalIndicesBuffer_.Get(),gpuFluidGridCountBuffer_.Get(),gpuFluidGridOffsetBuffer_.Get()};
    for(auto resource:simulationReads) transition(resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={srvHeap_}; list_->SetDescriptorHeaps(1,heaps);
    list_->SetComputeRootSignature(rootSigVolumeCompute_.Get());
    list_->SetComputeRoot32BitConstants(0,28,&cb,0);
    for(UINT i=0;i<4;++i) list_->SetComputeRootShaderResourceView(i+1,simulationReads[i]->GetGPUVirtualAddress());
    list_->SetComputeRootUnorderedAccessView(5,volumeAccum_->GetGPUVirtualAddress());
    list_->SetComputeRootUnorderedAccessView(6,volumeMomentum_->GetGPUVirtualAddress());
    list_->SetComputeRootUnorderedAccessView(7,volumeShapes_->GetGPUVirtualAddress());
    list_->SetComputeRootDescriptorTable(8,volumeUav_[0]);
    list_->SetComputeRootDescriptorTable(9,volumeUav_[4]);
    list_->SetComputeRootDescriptorTable(10,volumeSrv_[0]);
    list_->SetComputeRootDescriptorTable(11,volumeSrv_[2+volumeHistoryIndex_]);
    list_->SetComputeRootDescriptorTable(12,volumeSrv_[4]);
    const UINT groups=(cb.count+63)/64;
    BeginFluidProfile(FluidShapes);
    list_->SetPipelineState(psoVolumeClear_.Get()); list_->Dispatch(kVolumeWidth/4,kVolumeHeight/4,kVolumeWidth/4); uavBarrier();
    list_->SetPipelineState(psoVolumeShapes_.Get()); list_->Dispatch(groups,1,1); uavBarrier();
    EndFluidProfile(FluidShapes);
    BeginFluidProfile(FluidSplat);
    list_->SetPipelineState(psoVolumeSplat_.Get()); list_->Dispatch(groups,1,1); uavBarrier();
    transition(volumeTextures_[0].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(volumeTextures_[4].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetPipelineState(psoVolumeResolve_.Get()); list_->Dispatch(kVolumeWidth/4,kVolumeHeight/4,kVolumeWidth/4);
    transition(volumeTextures_[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    transition(volumeTextures_[4].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    EndFluidProfile(FluidSplat);
    BeginFluidProfile(FluidFiltering);
    for(UINT axis=0;axis<3;++axis) {
        UINT source=axis%2, destination=1-source;
        cb.axis=static_cast<float>(axis); list_->SetComputeRoot32BitConstants(0,28,&cb,0);
        list_->SetComputeRootDescriptorTable(10,volumeSrv_[source]);
        list_->SetComputeRootDescriptorTable(8,volumeUav_[destination]);
        transition(volumeTextures_[destination].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list_->SetPipelineState(psoVolumeSmooth_.Get()); list_->Dispatch(kVolumeWidth/4,kVolumeHeight/4,kVolumeWidth/4);
        transition(volumeTextures_[destination].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    }
    UINT historyDestination=2+(1-volumeHistoryIndex_);
    list_->SetComputeRootDescriptorTable(10,volumeSrv_[1]);
    list_->SetComputeRootDescriptorTable(8,volumeUav_[historyDestination]);
    transition(volumeTextures_[historyDestination].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetPipelineState(psoVolumeTemporal_.Get()); list_->Dispatch(kVolumeWidth/4,kVolumeHeight/4,kVolumeWidth/4);
    transition(volumeTextures_[historyDestination].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    list_->SetComputeRootDescriptorTable(10,volumeSrv_[historyDestination]);
    list_->SetComputeRootDescriptorTable(8,volumeUav_[5]);
    transition(volumeTextures_[5].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetPipelineState(psoVolumeOccupancy_.Get()); list_->Dispatch(kVolumeWidth/16,kVolumeHeight/16,kVolumeWidth/16);
    transition(volumeTextures_[5].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    transition(volumeShapes_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    EndFluidProfile(FluidFiltering);
    BeginFluidProfile(FluidRaymarch);
    SnapshotSceneForRefraction(list_);
    transition(ppSceneDepth_.Get(),ppDepthState_,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ppDepthState_=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE targets[]={ppRtv_,volumeRtvHeap_->GetCPUDescriptorHandleForHeapStart()};
    list_->OMSetRenderTargets(2,targets,FALSE,nullptr);
    list_->RSSetViewports(1,&viewport_); list_->RSSetScissorRects(1,&scissor_);
    list_->SetGraphicsRootSignature(rootSigVolumeDraw_.Get());
    list_->SetGraphicsRootConstantBufferView(0,cbFrameAddr_);
    list_->SetGraphicsRoot32BitConstants(1,28,&cb,0);
    list_->SetGraphicsRootDescriptorTable(2,volumeSrv_[historyDestination]);
    list_->SetGraphicsRootDescriptorTable(3,backdropSrv_);
    list_->SetGraphicsRootDescriptorTable(4,ppDepthSrvGpu_);
    list_->SetGraphicsRootDescriptorTable(5,envMapSrvGpu_.ptr?envMapSrvGpu_:liquidNullCubeSrv_);
    list_->SetGraphicsRootShaderResourceView(6,volumeShapes_->GetGPUVirtualAddress());
    list_->SetGraphicsRootShaderResourceView(7,gpuFluidSortedParticlesBuffer_->GetGPUVirtualAddress());
    // RaymarchPS does not reference t6; SprayPS uses it after the RTV transition.
    list_->SetGraphicsRootDescriptorTable(8,volumeSurfaceDepthSrv_);
    list_->SetGraphicsRootDescriptorTable(9,volumeSrv_[5]);
    list_->SetPipelineState(psoVolumeRaymarch_.Get());
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3,1,0,0);
    transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    EndFluidProfile(FluidRaymarch);
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,nullptr);
    if(fluidVolumeDebugMode_==0) {
        BeginFluidProfile(FluidImpostors);
        list_->SetPipelineState(psoVolumeSpray_.Get()); list_->DrawInstanced(6,cb.count,0,0);
        EndFluidProfile(FluidImpostors);
    }
    transition(ppSceneDepth_.Get(),ppDepthState_,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ppDepthState_=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,&ppDepthDsv_);
    for(auto resource:simulationReads) transition(resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(volumeShapes_.Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    volumeHistoryIndex_=1-volumeHistoryIndex_;
    volumePreviousOrigin_=origin;
    volumeHistoryValid_=true;
    list_->SetGraphicsRootSignature(rootSig3D_.Get());
    list_->SetGraphicsRootConstantBufferView(0,cbFrameAddr_);
    list_->SetGraphicsRootConstantBufferView(2,cbLightAddr_);
    if(shadowSrv_.ptr) list_->SetGraphicsRootDescriptorTable(5,shadowSrv_);
}
}
