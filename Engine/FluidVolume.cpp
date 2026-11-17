#include "Renderer.h"
#include "FluidVolumeDomain.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <d3dx12.h>

using Microsoft::WRL::ComPtr;

namespace Engine {
namespace {
constexpr UINT kVolumeWidth = 176;
constexpr UINT kVolumeHeight = 64;
constexpr UINT kFarWaterWidth=128,kFarWaterHeight=64;
constexpr float kFarWaterCell=1.f;
// interior: x は体の中から見る量 (結果画面の「おなかの中」)、y はそれより手前を体の中の物として見せる深さ
struct FarWaterConstants {Vector3 origin;float cell;UINT dimensions[3];UINT enabled;Vector4 interior;};
static_assert(sizeof(FarWaterConstants)==48);
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
    Vector3 playerBodyCenter; float playerDecoration;
    Vector3 playerForward; float playerFaceCamera;
    Vector3 playerRadii; float playerMotion;
    Vector3 playerSlope; float playerAir;
    Vector4 playerLiquidCore; // Graphics-only: position and activation.
    Vector4 playerMorph; // Form, physical mass scale, main particle count, enabled.
    Vector4 sunDirection; // Graphics-only: light travel direction and intensity.
};
constexpr UINT kVolumeComputeConstantCount=44;
static_assert(sizeof(VolumeConstants)==224);
static_assert(kVolumeComputeConstantCount+7*2+5<=64);
static_assert(3*2+3*2+10<=64); // Three CBVs, three structured SRVs, ten tables.
static_assert(sizeof(Renderer::GPUFluidParticle) == 64);
}

bool Renderer::InitFluidVolume() {
    if (!isGPUFluidReady_) return false;
    CD3DX12_ROOT_PARAMETER compute[13];
    CD3DX12_DESCRIPTOR_RANGE computeRanges[5];
    compute[0].InitAsConstants(kVolumeComputeConstantCount, 1);
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
    CD3DX12_ROOT_PARAMETER draw[16];
    CD3DX12_DESCRIPTOR_RANGE drawRanges[10];
    draw[0].InitAsConstantBufferView(0);
    draw[1].InitAsConstantBufferView(1);
    for(UINT i=0;i<4;++i) {
        drawRanges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i);
        draw[i+2].InitAsDescriptorTable(1,&drawRanges[i],i==0?D3D12_SHADER_VISIBILITY_ALL:D3D12_SHADER_VISIBILITY_PIXEL);
    }
    draw[6].InitAsShaderResourceView(4);
    draw[7].InitAsShaderResourceView(5);
    for(UINT i=4;i<6;++i) {
        drawRanges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i+2);
        draw[i+4].InitAsDescriptorTable(1,&drawRanges[i],D3D12_SHADER_VISIBILITY_PIXEL);
    }
    draw[10].InitAsShaderResourceView(8);
    // Resolved fluid velocity: aerates fast water into white foam in RaymarchPS.
    drawRanges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,9);
    draw[11].InitAsDescriptorTable(1,&drawRanges[6],D3D12_SHADER_VISIBILITY_PIXEL);
    draw[12].InitAsConstantBufferView(2);
    for(UINT i=0;i<3;++i){
        drawRanges[7+i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,10+i);
        draw[13+i].InitAsDescriptorTable(1,&drawRanges[7+i]);
    }
    if(!root(draw,_countof(draw),rootSigVolumeDraw_)) return false;
    CD3DX12_ROOT_PARAMETER shadow[4];CD3DX12_DESCRIPTOR_RANGE shadowRanges[2];
    shadow[0].InitAsConstantBufferView(0);shadow[1].InitAsConstantBufferView(1);
    for(UINT i=0;i<2;++i){shadowRanges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i);shadow[i+2].InitAsDescriptorTable(1,&shadowRanges[i],D3D12_SHADER_VISIBILITY_PIXEL);}
    if(!root(shadow,_countof(shadow),rootSigVolumeShadow_))return false;
    auto shadowVS=CompileShaderFromFile(L"Resources/shaders/FluidShadow.hlsl","ShadowVS","vs_5_0");
    auto shadowPS=CompileShaderFromFile(L"Resources/shaders/FluidShadow.hlsl","ShadowPS","ps_5_0");
    if(!shadowVS||!shadowPS)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPso{};
    shadowPso.pRootSignature=rootSigVolumeShadow_.Get();shadowPso.VS={shadowVS->GetBufferPointer(),shadowVS->GetBufferSize()};shadowPso.PS={shadowPS->GetBufferPointer(),shadowPS->GetBufferSize()};
    shadowPso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);shadowPso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    shadowPso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);shadowPso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowPso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;shadowPso.SampleDesc.Count=1;shadowPso.SampleMask=UINT_MAX;shadowPso.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    if(FAILED(dev_->CreateGraphicsPipelineState(&shadowPso,IID_PPV_ARGS(&psoVolumeShadow_))))return false;
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
       !computePso("SplatFarWater",psoFarWaterSplat_) || !computePso("BuildOccupancy",psoVolumeOccupancy_) || !computePso("SplatTendrils",psoVolumeTendrils_)) return false;
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
    graphics.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    const UINT64 farVoxels=UINT64(kFarWaterWidth)*kFarWaterHeight*kFarWaterWidth;
    if(!buffer(farVoxels*16,farWaterAccum_)||!buffer(farVoxels*16,farWaterMomentum_))return false;
    for(UINT i=0;i<4;++i){
        UINT width=i==3?kFarWaterWidth/4:kFarWaterWidth,height=i==3?kFarWaterHeight/4:kFarWaterHeight;
        auto desc=CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R16G16B16A16_FLOAT,width,height,UINT16(width),1,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,kVolumeRead,nullptr,IID_PPV_ARGS(&farWaterTextures_[i]))))return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;srv.Texture3D.MipLevels=1;
        UINT index=AllocateSrvIndex();dev_->CreateShaderResourceView(farWaterTextures_[i].Get(),&srv,window_->SRV_CPU(index));
        dev_->CreateShaderResourceView(farWaterTextures_[i].Get(),&srv,window_->SRV_CPU_Master(index));farWaterSrv_[i]=window_->SRV_GPU(index);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=desc.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;uav.Texture3D.WSize=width;
        index=AllocateSrvIndex();dev_->CreateUnorderedAccessView(farWaterTextures_[i].Get(),nullptr,&uav,window_->SRV_CPU(index));
        dev_->CreateUnorderedAccessView(farWaterTextures_[i].Get(),nullptr,&uav,window_->SRV_CPU_Master(index));farWaterUav_[i]=window_->SRV_GPU(index);
    }
    auto depthDesc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT,sceneWidth_,sceneHeight_,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
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
    // 放水の水の画面の上の面。作れなくても流体の体積はそのまま使える (水は体積で描く)
    screenWaterReady_=InitScreenWater();
    if(!screenWaterReady_)OutputDebugStringA("[Fluid] Screen-space hose water unavailable; hose water uses the volume.\n");
    return true;
}

bool Renderer::InitScreenWater(){
    // b0 カメラ・b1 流体の設定・b2 段ごとの値 (8 個)・t0 粒・t1〜t7 の表 (それぞれ 1 つ)
    CD3DX12_ROOT_PARAMETER parameters[11];CD3DX12_DESCRIPTOR_RANGE ranges[7];
    parameters[0].InitAsConstantBufferView(0);
    parameters[1].InitAsConstantBufferView(1);
    parameters[2].InitAsConstants(8,2);
    parameters[3].InitAsShaderResourceView(0,0,D3D12_SHADER_VISIBILITY_VERTEX);
    for(UINT i=0;i<7;++i){ranges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i+1);parameters[4+i].InitAsDescriptorTable(1,&ranges[i],D3D12_SHADER_VISIBILITY_PIXEL);}
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(parameters),parameters,1,&sampler);
    ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))){if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));return false;}
    if(FAILED(dev_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&rootSigScreenWater_))))return false;
    const wchar_t* file=L"Resources/shaders/FluidScreenWater.hlsl";
    auto spriteVS=CompileShaderFromFile(file,"WaterSpriteVS","vs_5_0"),spritePS=CompileShaderFromFile(file,"WaterSpritePS","ps_5_0");
    auto smoothPS=CompileShaderFromFile(file,"WaterSmoothPS","ps_5_0"),compositePS=CompileShaderFromFile(file,"WaterCompositePS","ps_5_0");
    auto fullscreenVS=CompileShaderFromFile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0");
    if(!spriteVS||!spritePS||!smoothPS||!compositePS||!fullscreenVS)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=rootSigScreenWater_.Get();
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);pso.DepthStencilState.DepthEnable=FALSE;pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.SampleDesc.Count=1;pso.SampleMask=UINT_MAX;
    // 粒: RT0 は近さの最大 (いちばん手前の粒)、RT1 は厚みと速さの足し算
    pso.VS={spriteVS->GetBufferPointer(),spriteVS->GetBufferSize()};pso.PS={spritePS->GetBufferPointer(),spritePS->GetBufferSize()};
    pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);pso.BlendState.IndependentBlendEnable=TRUE;
    for(UINT i=0;i<2;++i){auto& target=pso.BlendState.RenderTarget[i];target.BlendEnable=TRUE;target.SrcBlend=target.SrcBlendAlpha=D3D12_BLEND_ONE;target.DestBlend=target.DestBlendAlpha=D3D12_BLEND_ONE;
        target.BlendOp=target.BlendOpAlpha=i==0?D3D12_BLEND_OP_MAX:D3D12_BLEND_OP_ADD;}
    pso.NumRenderTargets=2;pso.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT;pso.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(FAILED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&psoScreenWaterSprite_))))return false;
    // なめらかにする段: 奥行きをそのまま書く
    pso.VS={fullscreenVS->GetBufferPointer(),fullscreenVS->GetBufferSize()};pso.PS={smoothPS->GetBufferPointer(),smoothPS->GetBufferSize()};
    pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);pso.NumRenderTargets=1;pso.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT;pso.RTVFormats[1]=DXGI_FORMAT_UNKNOWN;
    if(FAILED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&psoScreenWaterSmooth_))))return false;
    // 重ねる段: 前もって掛けた色 (色 + 後ろ × (1 - α))
    pso.PS={compositePS->GetBufferPointer(),compositePS->GetBufferSize()};pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto& blend=pso.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    if(FAILED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&psoScreenWaterComposite_))))return false;
    // 画面と同じ大きさの 4 枚 (近さ・厚み・なめらかな奥行き 2 枚)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeap{};rtvHeap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtvHeap.NumDescriptors=4;
    if(FAILED(dev_->CreateDescriptorHeap(&rtvHeap,IID_PPV_ARGS(&screenWaterRtvHeap_))))return false;
    const UINT rtvStep=dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    for(UINT i=0;i<4;++i){
        const DXGI_FORMAT format=i==1?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32_FLOAT;
        auto texture=CD3DX12_RESOURCE_DESC::Tex2D(format,sceneWidth_,sceneHeight_,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear{};clear.Format=format;
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&screenWaterTargets_[i]))))return false;
        auto rtv=screenWaterRtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(i)*rtvStep;
        dev_->CreateRenderTargetView(screenWaterTargets_[i].Get(),nullptr,rtv);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        const UINT slot=AllocateSrvIndex();
        dev_->CreateShaderResourceView(screenWaterTargets_[i].Get(),&srv,window_->SRV_CPU(slot));
        dev_->CreateShaderResourceView(screenWaterTargets_[i].Get(),&srv,window_->SRV_CPU_Master(slot));
        screenWaterSrv_[i]=window_->SRV_GPU(slot);
    }
    return true;
}

// 放水の水を画面の上で描く: 粒 → 奥行きを 2 回ずつ横・縦になめらかに → 色を付けて画面へ重ねる。
// 呼ぶ前は画面の色 (ppRtv) が描き先で、場面の奥行き・体の面の奥行きは読める状態
void Renderer::DrawScreenWater(D3D12_GPU_VIRTUAL_ADDRESS camera){
    BeginFluidProfile(FluidScreenWater);
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){auto b=CD3DX12_RESOURCE_BARRIER::Transition(resource,before,after);list_->ResourceBarrier(1,&b);};
    const UINT rtvStep=dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv=[&](UINT i){auto handle=screenWaterRtvHeap_->GetCPUDescriptorHandleForHeapStart();handle.ptr+=SIZE_T(i)*rtvStep;return handle;};
    const D3D12_VIEWPORT viewport{0,0,float(sceneWidth_),float(sceneHeight_),0,1};const D3D12_RECT scissor{0,0,LONG(sceneWidth_),LONG(sceneHeight_)};
    list_->RSSetViewports(1,&viewport);list_->RSSetScissorRects(1,&scissor);
    list_->SetGraphicsRootSignature(rootSigScreenWater_.Get());
    list_->SetGraphicsRootConstantBufferView(0,camera);
    list_->SetGraphicsRootConstantBufferView(1,volumeFrameSettings_);
    list_->SetGraphicsRootShaderResourceView(3,gpuFluidBuffer_->GetGPUVirtualAddress());
    list_->SetGraphicsRootDescriptorTable(4,backdropSrv_);
    list_->SetGraphicsRootDescriptorTable(5,ppDepthSrvGpu_);
    list_->SetGraphicsRootDescriptorTable(6,envMapSrvGpu_.ptr?envMapSrvGpu_:liquidNullCubeSrv_);
    list_->SetGraphicsRootDescriptorTable(7,volumeSurfaceDepthSrv_);
    list_->SetGraphicsRootDescriptorTable(8,screenWaterSrv_[0]);
    list_->SetGraphicsRootDescriptorTable(9,screenWaterSrv_[1]);
    list_->SetGraphicsRootDescriptorTable(10,screenWaterSrv_[2]);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // outsideOnly: 体積で描いている放水では、細かい升の窓の外の粒だけを描く
    const bool pool=HydrantTerrainActive();
    struct Pass {float axis[2];float size;uint32_t first,slots;float firstSmooth,outsideOnly,unused;} pass{{0,0},1,pool?kPoolFluidStart:kPlayerFluidParticles,pool?kPoolFluidCapacity:kHydrantWaterParticles,0,HydrantVolumeWater()?1.f:0.f,0};
    static_assert(sizeof(Pass)==32);
    // 1. 粒
    transition(screenWaterTargets_[0].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(screenWaterTargets_[1].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float zero[4]={0,0,0,0};
    D3D12_CPU_DESCRIPTOR_HANDLE sprites[2]={rtv(0),rtv(1)};
    list_->ClearRenderTargetView(sprites[0],zero,0,nullptr);list_->ClearRenderTargetView(sprites[1],zero,0,nullptr);
    list_->OMSetRenderTargets(2,sprites,FALSE,nullptr);
    list_->SetGraphicsRoot32BitConstants(2,8,&pass,0);
    list_->SetPipelineState(psoScreenWaterSprite_.Get());
    list_->DrawInstanced(6,pass.slots,0,0);
    transition(screenWaterTargets_[0].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(screenWaterTargets_[1].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // 2. なめらかに (横 → 縦 を 2 回)。最初は近さ (RT0) から読み、あとは 2 と 3 を交互に使う
    list_->SetPipelineState(psoScreenWaterSmooth_.Get());
    UINT source=3;
    for(UINT step=0;step<4;++step){
        const UINT destination=source==2?3:2;
        pass.axis[0]=step%2==0?1.f:0.f;pass.axis[1]=step%2==0?0.f:1.f;pass.firstSmooth=step==0?1.f:0.f;
        list_->SetGraphicsRoot32BitConstants(2,8,&pass,0);
        list_->SetGraphicsRootDescriptorTable(10,screenWaterSrv_[source]);
        transition(screenWaterTargets_[destination].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto target=rtv(destination);list_->OMSetRenderTargets(1,&target,FALSE,nullptr);
        list_->DrawInstanced(3,1,0,0);
        transition(screenWaterTargets_[destination].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        source=destination;
    }
    // 3. 画面へ重ねる
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,nullptr);
    list_->SetGraphicsRootDescriptorTable(10,screenWaterSrv_[source]);
    list_->SetPipelineState(psoScreenWaterComposite_.Get());
    list_->DrawInstanced(3,1,0,0);
    list_->RSSetViewports(1,&viewport_);list_->RSSetScissorRects(1,&scissor_);
    EndFluidProfile(FluidScreenWater);
}

void Renderer::DrawFluidVolumeShadow(const Matrix4x4& lightVP) {
    if(!volumeFrameSettings_||!psoVolumeShadow_||(gpuFluidPhaseMask_&3u)==0)return;
    struct ShadowConstants {Matrix4x4 vp,inverse;Vector4 bounds;} cb{};
    cb.vp=lightVP;
    DirectX::XMFLOAT4X4 matrix{};std::memcpy(&matrix,&lightVP,sizeof(matrix));
    auto vp=DirectX::XMLoadFloat4x4(&matrix);DirectX::XMStoreFloat4x4(&matrix,DirectX::XMMatrixInverse(nullptr,vp));std::memcpy(&cb.inverse,&matrix,sizeof(matrix));
    cb.bounds={1,1,-1,-1};
    for(UINT i=0;i<8;++i){auto lo=volumeShadowLow_,hi=volumeShadowHigh_;auto p=DirectX::XMVectorSet(i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z,1);
        DirectX::XMFLOAT3 clip{};DirectX::XMStoreFloat3(&clip,DirectX::XMVector3TransformCoord(p,vp));
        cb.bounds.x=(std::min)(cb.bounds.x,clip.x);cb.bounds.y=(std::min)(cb.bounds.y,clip.y);cb.bounds.z=(std::max)(cb.bounds.z,clip.x);cb.bounds.w=(std::max)(cb.bounds.w,clip.y);}
    cb.bounds.x=(std::max)(-1.f,cb.bounds.x);cb.bounds.y=(std::max)(-1.f,cb.bounds.y);cb.bounds.z=(std::min)(1.f,cb.bounds.z);cb.bounds.w=(std::min)(1.f,cb.bounds.w);
    if(cb.bounds.x>=cb.bounds.z||cb.bounds.y>=cb.bounds.w)return;
    auto& ring=upload_[window_->FrameIndex()];UINT offset=ring.Allocate(sizeof(cb),256);if(offset==UINT32_MAX)return;
    std::memcpy(ring.mapped+offset,&cb,sizeof(cb));
    list_->SetGraphicsRootSignature(rootSigVolumeShadow_.Get());list_->SetPipelineState(psoVolumeShadow_.Get());
    list_->SetGraphicsRootConstantBufferView(0,ring.buffer->GetGPUVirtualAddress()+offset);list_->SetGraphicsRootConstantBufferView(1,volumeFrameSettings_);
    list_->SetGraphicsRootDescriptorTable(2,volumeSrv_[2+volumeHistoryIndex_]);list_->SetGraphicsRootDescriptorTable(3,volumeSrv_[5]);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list_->DrawInstanced(6,1,0,0);
}

void Renderer::PrepareFluidVolume() {
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
    float cell=kVoxelSize;
    volumeShadowLow_=origin;volumeShadowHigh_=origin+Vector3{kVolumeWidth*cell,kVolumeHeight*cell,kVolumeWidth*cell};
    if(!fluidMorphTargets_.empty()){
        // The former 0.56-unit cells leave a watery body only one or two voxels
        // thick. Fit a finer domain to all active bodies and the real surface.
        Vector3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        auto include=[&](Vector3 p){lo={(std::min)(lo.x,p.x),(std::min)(lo.y,p.y),(std::min)(lo.z,p.z)};hi={(std::max)(hi.x,p.x),(std::max)(hi.y,p.y),(std::max)(hi.z,p.z)};};
        uint32_t owners=0;
        for(const auto& t:fluidMorphTargets_){
            uint32_t owner=uint32_t(t.forward.y);
            if(t.shape.w>.5f){if(owners&(1u<<owner))continue;owners|=1u<<owner;float stretch=1+.55f*LiquidBlendMotion(t.blend);
                Vector3 extent{t.shape.x*stretch*1.5f,t.shape.y,t.shape.z*stretch*1.5f};include(t.origin-extent);include(t.origin+extent);
            }else {include(t.origin+Vector3{t.forward.z*t.local.x+t.forward.x*t.local.z,t.local.y,-t.forward.x*t.local.x+t.forward.z*t.local.z});}}
        for(const auto& p:fluidBodySnapshot_.offsets){auto world=p+fluidBodySnapshot_.core;if(std::isfinite(world.x)&&std::isfinite(world.y)&&std::isfinite(world.z)&&std::abs(world.x-center.x)<100&&std::abs(world.y-center.y)<100&&std::abs(world.z-center.z)<100)include(world);}
        // (ここまでがスライムの体。坂の町の放水では、升が粗くならないように範囲をこの体を含む大きさまでに抑える)
        const Vector3 bodyLow=lo,bodyHigh=hi;Vector3 extraLow{1e9f,1e9f,1e9f},extraHigh{-1e9f,-1e9f,-1e9f};
        for(const auto& s:fluidVolumeExtras_){Vector3 c{s.x,s.y,s.z},r{s.w,s.w,s.w};include(c-r);include(c+r);
            extraLow={(std::min)(extraLow.x,c.x-r.x),(std::min)(extraLow.y,c.y-r.y),(std::min)(extraLow.z,c.z-r.z)};extraHigh={(std::max)(extraHigh.x,c.x+r.x),(std::max)(extraHigh.y,c.y+r.y),(std::max)(extraHigh.z,c.z+r.z)};}
        for(const auto& arm:fluidTendrils_){
            // A box skin stores its half size in end (see SplatTendrils).
            if(arm.endRadius<0){float r=(std::max)({arm.end.x,arm.end.y,arm.end.z})*1.5f-arm.endRadius*2;Vector3 extent{r,r,r};include(arm.start-extent);include(arm.start+extent);continue;}
            float r=(std::max)(arm.radius,arm.endRadius)*2;Vector3 extent{r,r,r};
            include(arm.start-extent);include(arm.start+extent);include(arm.end-extent);include(arm.end+extent);}
        if(HydrantVolumeWater()&&extraLow.x<=extraHigh.x){
            // 坂の町の放水: 水は 200 以上の広さに散るので、全部を入れると升が粗くなる (水がかたまりに見える)。
            // 箱庭のデモに近い細かさ (升 maxCell 以下) の窓を、体を含んだまま水の真ん中へ寄せて置く。窓の外の水は画面の上の面で描く
            const float maxCell=(std::max)(.7f,.28f*fluidSamplingScale_),margin=2*.8f*fluidSamplingScale_;
            auto window=[&](float& low,float& high,float bodyMin,float bodyMax,float focus,float extent){
                if(high-low<=extent||bodyMax-bodyMin>=extent)return;
                low=std::clamp(focus-extent*.5f,bodyMax-extent,bodyMin);high=low+extent;};
            const Vector3 focus=(extraLow+extraHigh)*.5f;
            const float across=maxCell*float(kVolumeWidth-8)-margin,up=maxCell*float(kVolumeHeight-8)-margin;
            window(lo.x,hi.x,bodyLow.x,bodyHigh.x,focus.x,across);window(lo.y,hi.y,bodyLow.y,bodyHigh.y,focus.y,up);window(lo.z,hi.z,bodyLow.z,bodyHigh.z,focus.z,across);
        }
        const float shadowMargin=1.2f*fluidSamplingScale_;
        volumeShadowLow_=lo-Vector3{shadowMargin,shadowMargin,shadowMargin};volumeShadowHigh_=hi+Vector3{shadowMargin,shadowMargin,shadowMargin};
        cell=FluidVolumeDomain::StableCell(FluidVolumeDomain::FitCell(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,kVolumeWidth,kVolumeHeight,.8f*fluidSamplingScale_,.2f*fluidSamplingScale_),volumeHistoryValid_?volumePreviousVoxelSize_:0);
        origin={FluidVolumeDomain::Origin(lo.x,hi.x,cell,kVolumeWidth),FluidVolumeDomain::Origin(lo.y,hi.y,cell,kVolumeHeight),FluidVolumeDomain::Origin(lo.z,hi.z,cell,kVolumeWidth)};
    }
    if(fluidVolumeFocusActive_&&fluidMorphTargets_.empty()){
        // 箱庭など: 決めた箱に升を合わせる (スライムの体と同じ細かさで水を描く)
        const Vector3 lo=fluidVolumeFocusLow_,hi=fluidVolumeFocusHigh_;
        cell=FluidVolumeDomain::StableCell(FluidVolumeDomain::FitCell(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,kVolumeWidth,kVolumeHeight,1.f,.18f),volumeHistoryValid_?volumePreviousVoxelSize_:0);
        origin={FluidVolumeDomain::Origin(lo.x,hi.x,cell,kVolumeWidth),FluidVolumeDomain::Origin(lo.y,hi.y,cell,kVolumeHeight),FluidVolumeDomain::Origin(lo.z,hi.z,cell,kVolumeWidth)};
        volumeShadowLow_=lo-Vector3{1,1,1};volumeShadowHigh_=hi+Vector3{1,1,1};
    }
    if(std::abs(volumePreviousVoxelSize_-cell)>1e-5f)volumeHistoryValid_=false;
    volumePreviousVoxelSize_=cell;
    float move=std::abs(origin.x-volumePreviousOrigin_.x)+std::abs(origin.y-volumePreviousOrigin_.y)+std::abs(origin.z-volumePreviousOrigin_.z);
    if(move>8) volumeHistoryValid_=false;
    VolumeConstants cb{origin,cell,{kVolumeWidth,kVolumeHeight,kVolumeWidth},(std::min)(gpuFluidActiveParticleCount_,gpuFluidMaxParticles_),
        volumePreviousOrigin_,volumeHistoryValid_?1.0f:0.0f,fluidSimulatedDt_,0.25f,0,static_cast<float>(fluidVolumeDebugMode_),
        {volumeColors_[0],volumeColors_[1],volumeColors_[2]},
        {gpuFluidCorePos_.x,gpuFluidCorePos_.y-1.25f+0.8f*std::clamp(gpuFluidCoreScale_.y,0.0f,1.0f),gpuFluidCorePos_.z},
        gpuFluidCoreMode_>=4.0f ? chronoFluidOpacity_ : 0.0f,
        gpuFluidCoreForward_,slimeFaceCamera_,{},0,slimeGround_,std::clamp(gpuFluidCoreScale_.y,0.f,1.f)};
    // 体の中から見るときは、顔や泡の飾りを描かない (内側から裏返しに見える)
    if(fluidInterior_>.01f)cb.playerDecoration=0;
    // BuildShapes: -(the solver's neighbour-table size), +.5 when hose water is drawn in screen space
    // instead of the volume. Filtering passes overwrite axis with 0..2.
    cb.axis=-(float(fluidHashBuckets_)+(HydrantScreenWater()?.5f:HydrantVolumeWater()?.25f:0.f));
    // Match the simulated containment field, including mass, landing squash and slide.
    float scale=std::cbrt(std::clamp(gpuFluidCoreFlowSpeed_/100.f,.05f,2.f));
    float aspect=gpuFluidCoreScale_.x>.1f?std::clamp(gpuFluidCoreScale_.x,.55f,1.6f):1.f;
    float flatRadius=gpuFluidCoreMode_>=5.f?4.2f:2.55f,flatHeight=gpuFluidCoreMode_>=5.f?.66f:1.8f;
    float horizontal=(flatRadius+(1.8f-flatRadius)*cb.playerAir)*scale/std::sqrt(aspect);
    cb.playerRadii={horizontal,(flatHeight+(1.8f-flatHeight)*cb.playerAir)*scale*aspect,horizontal};
    cb.playerMotion=std::clamp(gpuFluidCoreScale_.z,0.f,1.f)*(1-.75f*cb.playerAir);
    if(!fluidMorphTargets_.empty()){
        const auto& t=fluidMorphTargets_[0];
        UINT mainCount=0;for(const auto& target:fluidMorphTargets_){if(target.forward.y!=0)break;++mainCount;}
        scale=std::cbrt(275.f*mainCount/(6600.f*(std::max)(t.density,.1f)));
        cb.playerMorph={t.shape.w,scale,float(mainCount),1};
        cb.playerFaceCamera=slimeFaceCamera_;cb.playerForward={t.forward.x,0,t.forward.z};cb.playerMotion=0;
        if(t.shape.w>.5f){
            cb.playerBodyCenter=t.origin;cb.playerRadii={t.shape.x,t.shape.y,t.shape.z};
            cb.playerLiquidCore={t.origin.x+t.reserved.z,t.origin.y+t.shape.y*.43f,t.origin.z+t.reserved.w,1};
        }else{
            Vector3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
            for(UINT i=0;i<mainCount;++i){const auto& target=fluidMorphTargets_[i];
                auto p=target.origin+Vector3{target.forward.z*target.local.x+target.forward.x*target.local.z,target.local.y,-target.forward.x*target.local.x+target.forward.z*target.local.z};
                lo={(std::min)(lo.x,p.x),(std::min)(lo.y,p.y),(std::min)(lo.z,p.z)};
                hi={(std::max)(hi.x,p.x),(std::max)(hi.y,p.y),(std::max)(hi.z,p.z)};
            }
            cb.playerBodyCenter=(lo+hi)*.5f;cb.playerRadii=(hi-lo)*.5f;
        }
    }
    cb.playerSlope.y=scale; // Decoration size depends on mass, never the body stretch.
    if(lightCB_.dirLights[0].enabled){
        // Hydrant mist scatters this sun into a rainbow around the antisolar point.
        const auto& sun=lightCB_.dirLights[0];
        float length=std::sqrt(sun.direction.x*sun.direction.x+sun.direction.y*sun.direction.y+sun.direction.z*sun.direction.z);
        if(length>1e-4f)cb.sunDirection={sun.direction.x/length,sun.direction.y/length,sun.direction.z/length,
            std::clamp((std::max)(sun.color.x,(std::max)(sun.color.y,sun.color.z)),0.f,4.f)};
    }
    if(gpuFluidCoreMode_>=2.0f){
        cb.colors[0].w=chronoFluidOpacity_;
        cb.colors[0].x+=(1-cb.colors[0].x)*chronoFluidFlash_;
        cb.colors[0].y+=(1-cb.colors[0].y)*chronoFluidFlash_;
        cb.colors[0].z+=(1-cb.colors[0].z)*chronoFluidFlash_;
    }
    auto& fluidUpload=upload_[window_->FrameIndex()];
    uint32_t settingsOffset=fluidUpload.Allocate(sizeof(cb),256);
    if(settingsOffset==UINT32_MAX)return;
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(resource,before,after); list_->ResourceBarrier(1,&b);
    };
    auto uavBarrier=[&]() { auto b=CD3DX12_RESOURCE_BARRIER::UAV(nullptr); list_->ResourceBarrier(1,&b); };
    ID3D12Resource* simulationReads[]={gpuFluidSortedParticlesBuffer_.Get(),gpuFluidOriginalIndicesBuffer_.Get(),gpuFluidGridCountBuffer_.Get(),gpuFluidGridOffsetBuffer_.Get(),gpuFluidBuffer_.Get()};
    for(auto resource:simulationReads) transition(resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={srvHeap_}; list_->SetDescriptorHeaps(1,heaps);
    list_->SetComputeRootSignature(rootSigVolumeCompute_.Get());
    list_->SetComputeRoot32BitConstants(0,kVolumeComputeConstantCount,&cb,0);
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
    if(!fluidTendrils_.empty()&&!fluidMorphTargets_.empty()){
        // Reuse t0 for a separate kernel; the ordinary particle SRV is restored
        // before resolving. No additional root-signature budget or particle slots.
        UINT bytes=UINT(fluidTendrils_.size()*sizeof(FluidTendril));
        UINT offset=fluidUpload.Allocate(bytes,256);
        if(offset!=UINT32_MAX){
            std::memcpy(fluidUpload.mapped+offset,fluidTendrils_.data(),bytes);
            list_->SetComputeRootShaderResourceView(1,fluidUpload.buffer->GetGPUVirtualAddress()+offset);
            UINT count=UINT(fluidTendrils_.size());list_->SetComputeRoot32BitConstant(0,count,7);
            list_->SetPipelineState(psoVolumeTendrils_.Get());list_->Dispatch((count+63)/64,1,1);uavBarrier();
            list_->SetComputeRoot32BitConstant(0,cb.count,7);
            list_->SetComputeRootShaderResourceView(1,simulationReads[0]->GetGPUVirtualAddress());
        }
    }
    transition(volumeTextures_[0].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(volumeTextures_[4].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetPipelineState(psoVolumeResolve_.Get()); list_->Dispatch(kVolumeWidth/4,kVolumeHeight/4,kVolumeWidth/4);
    transition(volumeTextures_[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    transition(volumeTextures_[4].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    EndFluidProfile(FluidSplat);
    BeginFluidProfile(FluidFiltering);
    for(UINT axis=0;axis<3;++axis) {
        UINT source=axis%2, destination=1-source;
        cb.axis=static_cast<float>(axis); list_->SetComputeRoot32BitConstants(0,kVolumeComputeConstantCount,&cb,0);
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
    // Coarse water-only volume; fine shapes and the near history stay intact.
    FarWaterConstants farSettings{};farSettings.cell=kFarWaterCell;farSettings.dimensions[0]=farSettings.dimensions[2]=kFarWaterWidth;farSettings.dimensions[1]=kFarWaterHeight;
    // (放水の水を画面の上で描いている間は、遠くの水の升は使わない)
    // (坂の町の放水では、水の範囲を細かい升に入れるので遠くの水の升は使わない)
    farSettings.enabled=(gpuFluidPhaseMask_&4u)!=0&&!HydrantTerrainActive()&&!fluidVolumeFocusActive_;farSettings.interior={fluidInterior_,fluidInteriorDepth_,0,0};
    farSettings.origin={std::floor(center.x/farSettings.cell)*farSettings.cell-kFarWaterWidth*farSettings.cell*.5f,
        std::floor((center.y-4)/(farSettings.cell*.4f))*(farSettings.cell*.4f),
        std::floor(center.z/farSettings.cell)*farSettings.cell-kFarWaterWidth*farSettings.cell*.5f};
    UINT farOffset=fluidUpload.Allocate(sizeof(farSettings),256);farWaterFrameSettings_=0;
    if(farOffset!=UINT32_MAX){std::memcpy(fluidUpload.mapped+farOffset,&farSettings,sizeof(farSettings));farWaterFrameSettings_=fluidUpload.buffer->GetGPUVirtualAddress()+farOffset;}
    if(farSettings.enabled&&farWaterFrameSettings_){
        auto coarse=cb;coarse.origin=farSettings.origin;coarse.cell=farSettings.cell;
        coarse.dimensions[0]=coarse.dimensions[2]=kFarWaterWidth;coarse.dimensions[1]=kFarWaterHeight;coarse.historyValid=0;
        list_->SetComputeRoot32BitConstants(0,kVolumeComputeConstantCount,&coarse,0);
        list_->SetComputeRootUnorderedAccessView(5,farWaterAccum_->GetGPUVirtualAddress());
        list_->SetComputeRootUnorderedAccessView(6,farWaterMomentum_->GetGPUVirtualAddress());
        list_->SetPipelineState(psoVolumeClear_.Get());list_->Dispatch(kFarWaterWidth/4,kFarWaterHeight/4,kFarWaterWidth/4);uavBarrier();
        list_->SetPipelineState(psoFarWaterSplat_.Get());list_->Dispatch(groups,1,1);uavBarrier();
        list_->SetComputeRootDescriptorTable(8,farWaterUav_[0]);list_->SetComputeRootDescriptorTable(9,farWaterUav_[2]);
        transition(farWaterTextures_[0].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(farWaterTextures_[2].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list_->SetPipelineState(psoVolumeResolve_.Get());list_->Dispatch(kFarWaterWidth/4,kFarWaterHeight/4,kFarWaterWidth/4);
        transition(farWaterTextures_[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
        transition(farWaterTextures_[2].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
        // Horizontal filtering connects ground films without thickening them.
        for(UINT pass=0;pass<2;++pass){UINT source=pass,destination=1-pass;coarse.axis=pass==0?0.f:2.f;
            list_->SetComputeRoot32BitConstants(0,kVolumeComputeConstantCount,&coarse,0);
            list_->SetComputeRootDescriptorTable(10,farWaterSrv_[source]);list_->SetComputeRootDescriptorTable(8,farWaterUav_[destination]);
            transition(farWaterTextures_[destination].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list_->SetPipelineState(psoVolumeSmooth_.Get());list_->Dispatch(kFarWaterWidth/4,kFarWaterHeight/4,kFarWaterWidth/4);
            transition(farWaterTextures_[destination].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
        }
        list_->SetComputeRootDescriptorTable(10,farWaterSrv_[0]);list_->SetComputeRootDescriptorTable(8,farWaterUav_[3]);
        transition(farWaterTextures_[3].Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list_->SetPipelineState(psoVolumeOccupancy_.Get());list_->Dispatch(kFarWaterWidth/16,kFarWaterHeight/16,kFarWaterWidth/16);
        transition(farWaterTextures_[3].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    }
    EndFluidProfile(FluidFiltering);
    // 描く段へ: axis 3 = 坂の町の放水を箱庭のデモと同じ澄んだ水で描く (速い水の白い泡をほとんど出さない)
    cb.axis=HydrantVolumeWater()?3.f:2.f;
    std::memcpy(fluidUpload.mapped+settingsOffset,&cb,sizeof(cb));
    volumeFrameSettings_=fluidUpload.buffer->GetGPUVirtualAddress()+settingsOffset;
    volumeDrawCount_=cb.count+(cb.playerDecoration>0.001f?224+24+2:0);
    for(auto resource:simulationReads)transition(resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    volumeHistoryIndex_=1-volumeHistoryIndex_;volumePreviousOrigin_=origin;volumeHistoryValid_=true;
}

void Renderer::DrawFluidVolume() {
    if(!volumeFrameSettings_||!farWaterFrameSettings_)return;
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){auto b=CD3DX12_RESOURCE_BARRIER::Transition(resource,before,after);list_->ResourceBarrier(1,&b);};
    auto& fluidUpload=upload_[window_->FrameIndex()];
    ID3D12Resource* simulationReads[]={gpuFluidSortedParticlesBuffer_.Get(),gpuFluidBuffer_.Get()};
    for(auto resource:simulationReads)transition(resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    transition(volumeShapes_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,kVolumeRead);
    const UINT historyDestination=2+volumeHistoryIndex_;
    BeginFluidProfile(FluidRaymarch);
    SnapshotSceneForRefraction(list_);
    transition(ppSceneDepth_.Get(),ppDepthState_,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ppDepthState_=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE targets[]={ppRtv_,volumeRtvHeap_->GetCPUDescriptorHandleForHeapStart()};
    list_->OMSetRenderTargets(2,targets,FALSE,nullptr);
    list_->RSSetViewports(1,&viewport_); list_->RSSetScissorRects(1,&scissor_);
    // Keep the world's material clock frozen, but give fluid decorations their
    // own visual time so interior and escaped bubbles keep rising in the menu.
    auto fluidCamera=cbFrame_;fluidCamera.time=fluidDecorationTime_;
    uint32_t cameraOffset=fluidUpload.Allocate(sizeof(fluidCamera),256);
    D3D12_GPU_VIRTUAL_ADDRESS cameraAddress=cbFrameAddr_;
    if(cameraOffset!=UINT32_MAX){
        std::memcpy(fluidUpload.mapped+cameraOffset,&fluidCamera,sizeof(fluidCamera));
        cameraAddress=fluidUpload.buffer->GetGPUVirtualAddress()+cameraOffset;
    }
    // (放水の水を描いたあと、霧と粒の段のためにもう一度つなぐ)
    auto bindVolumeDraw=[&](){
    list_->SetGraphicsRootSignature(rootSigVolumeDraw_.Get());
    list_->SetGraphicsRootConstantBufferView(0,cameraAddress);
    list_->SetGraphicsRootConstantBufferView(1,volumeFrameSettings_);
    list_->SetGraphicsRootDescriptorTable(2,volumeSrv_[historyDestination]);
    list_->SetGraphicsRootDescriptorTable(3,backdropSrv_);
    list_->SetGraphicsRootDescriptorTable(4,ppDepthSrvGpu_);
    list_->SetGraphicsRootDescriptorTable(5,envMapSrvGpu_.ptr?envMapSrvGpu_:liquidNullCubeSrv_);
    list_->SetGraphicsRootShaderResourceView(6,volumeShapes_->GetGPUVirtualAddress());
    list_->SetGraphicsRootShaderResourceView(7,gpuFluidSortedParticlesBuffer_->GetGPUVirtualAddress());
    // RaymarchPS does not reference t6; SprayPS uses it after the RTV transition.
    list_->SetGraphicsRootDescriptorTable(8,volumeSurfaceDepthSrv_);
    list_->SetGraphicsRootDescriptorTable(9,volumeSrv_[5]);
    list_->SetGraphicsRootShaderResourceView(10,gpuFluidBuffer_->GetGPUVirtualAddress());
    list_->SetGraphicsRootDescriptorTable(11,volumeSrv_[4]);
    list_->SetGraphicsRootConstantBufferView(12,farWaterFrameSettings_);
    list_->SetGraphicsRootDescriptorTable(13,farWaterSrv_[0]);
    list_->SetGraphicsRootDescriptorTable(14,farWaterSrv_[2]);
    list_->SetGraphicsRootDescriptorTable(15,farWaterSrv_[3]);
    };
    bindVolumeDraw();
    list_->SetPipelineState(psoVolumeRaymarch_.Get());
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3,1,0,0);
    transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    EndFluidProfile(FluidRaymarch);
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,nullptr);
    // 放水の水 (画面の上の面)。霧と水しぶきの粒はその上に描く
    // (体積で描くときも、細かい升の窓の外へ流れた水だけは画面の上の面で描く)
    if((HydrantScreenWater()||HydrantVolumeWater())&&fluidVolumeDebugMode_==0){DrawScreenWater(cameraAddress);bindVolumeDraw();list_->OMSetRenderTargets(1,&ppRtv_,FALSE,nullptr);}
    if(fluidVolumeDebugMode_==0) {
        BeginFluidProfile(FluidImpostors);
        list_->SetPipelineState(psoVolumeSpray_.Get());
        list_->DrawInstanced(6,volumeDrawCount_,0,0);
        EndFluidProfile(FluidImpostors);
    }
    transition(ppSceneDepth_.Get(),ppDepthState_,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ppDepthState_=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,&ppDepthDsv_);
    for(auto resource:simulationReads) transition(resource,kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(volumeShapes_.Get(),kVolumeRead,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    list_->SetGraphicsRootSignature(rootSig3D_.Get());
    list_->SetGraphicsRootConstantBufferView(0,cbFrameAddr_);
    list_->SetGraphicsRootConstantBufferView(2,cbLightAddr_);
    if(shadowSrv_.ptr) list_->SetGraphicsRootDescriptorTable(5,shadowSrv_);
}
}
