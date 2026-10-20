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
};
constexpr UINT kVolumeComputeConstantCount=44;
static_assert(sizeof(VolumeConstants)==208);
static_assert(kVolumeComputeConstantCount+7*2+5<=64);
static_assert(2*2+3*2+6<=64); // Two CBVs, three structured SRVs, six tables.
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
    CD3DX12_ROOT_PARAMETER draw[11];
    CD3DX12_DESCRIPTOR_RANGE drawRanges[6];
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
    return true;
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
            if(t.shape.w>.5f){if(owners&(1u<<owner))continue;owners|=1u<<owner;float stretch=1+.55f*t.blend;
                Vector3 extent{t.shape.x*stretch*1.5f,t.shape.y,t.shape.z*stretch*1.5f};include(t.origin-extent);include(t.origin+extent);
            }else {include(t.origin+Vector3{t.forward.z*t.local.x+t.forward.x*t.local.z,t.local.y,-t.forward.x*t.local.x+t.forward.z*t.local.z});}}
        for(const auto& p:fluidBodySnapshot_.offsets){auto world=p+fluidBodySnapshot_.core;if(std::isfinite(world.x)&&std::isfinite(world.y)&&std::isfinite(world.z)&&std::abs(world.x-center.x)<100&&std::abs(world.y-center.y)<100&&std::abs(world.z-center.z)<100)include(world);}
        const float shadowMargin=1.2f*fluidSamplingScale_;
        volumeShadowLow_=lo-Vector3{shadowMargin,shadowMargin,shadowMargin};volumeShadowHigh_=hi+Vector3{shadowMargin,shadowMargin,shadowMargin};
        cell=FluidVolumeDomain::StableCell(FluidVolumeDomain::FitCell(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,kVolumeWidth,kVolumeHeight,.8f*fluidSamplingScale_,.2f*fluidSamplingScale_),volumeHistoryValid_?volumePreviousVoxelSize_:0);
        origin={FluidVolumeDomain::Origin(lo.x,hi.x,cell,kVolumeWidth),FluidVolumeDomain::Origin(lo.y,hi.y,cell,kVolumeHeight),FluidVolumeDomain::Origin(lo.z,hi.z,cell,kVolumeWidth)};
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
    cb.axis=fluidMorphTargets_.empty()?.4f:-.4f; // Non-morph particles use the unscaled hash; filtering resets axis.
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
    EndFluidProfile(FluidFiltering);
    std::memcpy(fluidUpload.mapped+settingsOffset,&cb,sizeof(cb));
    volumeFrameSettings_=fluidUpload.buffer->GetGPUVirtualAddress()+settingsOffset;
    volumeDrawCount_=cb.count+(cb.playerDecoration>0.001f?224+24+2:0);
    for(auto resource:simulationReads)transition(resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    volumeHistoryIndex_=1-volumeHistoryIndex_;volumePreviousOrigin_=origin;volumeHistoryValid_=true;
}

void Renderer::DrawFluidVolume() {
    if(!volumeFrameSettings_)return;
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
    list_->SetGraphicsRootSignature(rootSigVolumeDraw_.Get());
    // Keep the world's material clock frozen, but give fluid decorations their
    // own visual time so interior and escaped bubbles keep rising in the menu.
    auto fluidCamera=cbFrame_;fluidCamera.time=fluidDecorationTime_;
    uint32_t cameraOffset=fluidUpload.Allocate(sizeof(fluidCamera),256);
    if(cameraOffset!=UINT32_MAX){
        std::memcpy(fluidUpload.mapped+cameraOffset,&fluidCamera,sizeof(fluidCamera));
        list_->SetGraphicsRootConstantBufferView(0,fluidUpload.buffer->GetGPUVirtualAddress()+cameraOffset);
    }else list_->SetGraphicsRootConstantBufferView(0,cbFrameAddr_);
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
    list_->SetPipelineState(psoVolumeRaymarch_.Get());
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3,1,0,0);
    transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    EndFluidProfile(FluidRaymarch);
    list_->OMSetRenderTargets(1,&ppRtv_,FALSE,nullptr);
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
