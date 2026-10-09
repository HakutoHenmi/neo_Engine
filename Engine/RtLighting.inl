bool Renderer::InitRtLighting_(){
    std::ifstream source("Resources/shaders/RtLighting.cso",std::ios::binary);
    std::vector<char> shader((std::istreambuf_iterator<char>(source)),{});if(shader.empty())return false;
    CD3DX12_DESCRIPTOR_RANGE ranges[6];CD3DX12_ROOT_PARAMETER roots[11];
    roots[0].InitAsConstantBufferView(0);roots[1].InitAsShaderResourceView(0);
    const UINT registers[]={1,2,5,6,0,1};const UINT slots[]={2,3,6,7,8,9};
    for(UINT i=0;i<6;++i){ranges[i].Init(i<4?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV,i==3?32:1,registers[i]);roots[slots[i]].InitAsDescriptorTable(1,&ranges[i]);}
    roots[4].InitAsShaderResourceView(3);roots[5].InitAsShaderResourceView(4);
    roots[10].InitAsUnorderedAccessView(2);
    CD3DX12_STATIC_SAMPLER_DESC samplers[2];samplers[0].Init(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    samplers[1].Init(1,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC desc(11,roots,2,samplers);ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(dev_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&rootSigRtLighting_))))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=rootSigRtLighting_.Get();pso.CS={shader.data(),shader.size()};
    if(FAILED(dev_->CreateComputePipelineState(&pso,IID_PPV_ARGS(&psoRtLighting_))))return false;
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto create=[&](BloomTarget& target,D3D12_GPU_DESCRIPTOR_HANDLE& uav){
        target.width=WindowDX::kW/2;target.height=WindowDX::kH/2;
        auto rd=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT,target.width,target.height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target.resource))))return false;
        UINT srv=AllocateSrvIndex();target.srv=window_->SRV_GPU(srv);dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU(srv));dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU_Master(srv));
        UINT index=AllocateSrvIndex();uav=window_->SRV_GPU(index);D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=rd.Format;ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        dev_->CreateUnorderedAccessView(target.resource.Get(),nullptr,&ud,window_->SRV_CPU(index));dev_->CreateUnorderedAccessView(target.resource.Get(),nullptr,&ud,window_->SRV_CPU_Master(index));return true;
    };
    if(!create(rtReflectionTarget_,rtReflectionUav_)||!create(rtIndirectTarget_,rtIndirectUav_)){psoRtLighting_.Reset();return false;}
    for(UINT i=0;i<2;++i)if(!create(rtFilteredReflection_[i],rtFilteredReflectionUav_[i])||!create(rtFilteredIndirect_[i],rtFilteredIndirectUav_[i])){psoRtLighting_.Reset();return false;}
    CD3DX12_DESCRIPTOR_RANGE filterRanges[7];CD3DX12_ROOT_PARAMETER filterRoots[8];filterRoots[0].InitAsConstantBufferView(0);
    for(UINT i=0;i<7;++i){filterRanges[i].Init(i<5?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,i<5?i:i-5);filterRoots[i+1].InitAsDescriptorTable(1,&filterRanges[i]);}
    CD3DX12_ROOT_SIGNATURE_DESC filterDesc(8,filterRoots);ComPtr<ID3DBlob> filterBlob;
    auto filterShader=CompileShaderFromFile(L"Resources/shaders/RtLightingDenoise.hlsl","main","cs_5_0");
    if(!filterShader||FAILED(D3D12SerializeRootSignature(&filterDesc,D3D_ROOT_SIGNATURE_VERSION_1,&filterBlob,&error))||FAILED(dev_->CreateRootSignature(0,filterBlob->GetBufferPointer(),filterBlob->GetBufferSize(),IID_PPV_ARGS(&rootSigRtDenoise_)))){psoRtLighting_.Reset();return false;}
    D3D12_COMPUTE_PIPELINE_STATE_DESC filterPso{};filterPso.pRootSignature=rootSigRtDenoise_.Get();filterPso.CS={filterShader->GetBufferPointer(),filterShader->GetBufferSize()};
    if(FAILED(dev_->CreateComputePipelineState(&filterPso,IID_PPV_ARGS(&psoRtDenoise_)))){psoRtLighting_.Reset();return false;}
    auto vs=CompileShaderFromFile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),ps=CompileShaderFromFile(L"Resources/shaders/RtLightingComposite.hlsl","main","ps_5_0");
    if(!vs||!ps){psoRtLighting_.Reset();return false;}
    D3D12_GRAPHICS_PIPELINE_STATE_DESC composite{};composite.pRootSignature=rootSigPP_.Get();composite.VS={vs->GetBufferPointer(),vs->GetBufferSize()};composite.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    composite.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;composite.NumRenderTargets=1;composite.SampleDesc.Count=1;composite.SampleMask=UINT_MAX;composite.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    composite.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);composite.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);composite.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);composite.DepthStencilState.DepthEnable=FALSE;
    auto hdr=ppSceneColor_->GetDesc();D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    if(FAILED(dev_->CreateGraphicsPipelineState(&composite,IID_PPV_ARGS(&psoRtLightingComposite_)))||FAILED(dev_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtLightingRtvHeap_)))||FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&hdr,D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_PPV_ARGS(&rtLightingHdr_)))){psoRtLighting_.Reset();return false;}
    dev_->CreateRenderTargetView(rtLightingHdr_.Get(),nullptr,rtLightingRtvHeap_->GetCPUDescriptorHandleForHeapStart());return true;
}
bool Renderer::RenderRtLighting_(uint32_t frame,const std::vector<RtInstance>& instances){
    if(!psoRtLighting_||rtTriangles_.empty())return false;
    BeginFluidProfile(RtTrace);
    auto& slot=rtFrames_[frame];
    auto buffer=[&](ComPtr<ID3D12Resource>& resource,UINT64 bytes,D3D12_HEAP_TYPE type){
        if(resource&&resource->GetDesc().Width>=bytes)return true;
        auto heap=CD3DX12_HEAP_PROPERTIES(type);auto rd=CD3DX12_RESOURCE_DESC::Buffer((bytes+255)&~UINT64(255));
        return SUCCEEDED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)));
    };
    auto upload=[&](ComPtr<ID3D12Resource>& resource,const void* data,size_t bytes){
        if(!buffer(resource,bytes,D3D12_HEAP_TYPE_UPLOAD))return false;void* mapped=nullptr;D3D12_RANGE empty{0,0};
        if(FAILED(resource->Map(0,&empty,&mapped)))return false;std::memcpy(mapped,data,bytes);resource->Unmap(0,nullptr);return true;
    };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;auto rd=rtReflectionTarget_.resource->GetDesc();dev_->GetCopyableFootprints(&rd,0,1,0,&footprint,nullptr,nullptr,&total);
    if(slot.lightingPending){void* data=nullptr;D3D12_RANGE range{0,SIZE_T(total*2+16)};
        if(SUCCEEDED(slot.lightingReadback->Map(0,&range,&data))){uint32_t pixels[2]{};
            for(int target=0;target<2;++target)for(UINT y=0;y<rtReflectionTarget_.height;++y){auto* row=reinterpret_cast<float*>(static_cast<char*>(data)+target*total+y*footprint.Footprint.RowPitch);
                for(UINT x=0;x<rtReflectionTarget_.width;++x){const float* c=row+x*4;if(std::isfinite(c[0])&&std::isfinite(c[1])&&std::isfinite(c[2])&&std::abs(c[3])>0&&std::abs(c[0])+std::abs(c[1])+std::abs(c[2])>.0001f)++pixels[target];}}
            auto* counters=reinterpret_cast<uint32_t*>(static_cast<char*>(data)+total*2);rtReflectionHits_=counters[0];rtIndirectHits_=counters[1];
            D3D12_RANGE empty{0,0};slot.lightingReadback->Unmap(0,&empty);rtReflectionPixels_=pixels[0];rtIndirectPixels_=pixels[1];}
        slot.lightingPending=false;
    }
    if(slot.triangleCount!=rtTriangles_.size()){
        size_t bytes=rtTriangles_.size()*sizeof(RtTriangle);bool reuse=slot.triangles&&slot.triangles->GetDesc().Width>=bytes;
        if(!upload(slot.triangleUpload,rtTriangles_.data(),bytes)||!buffer(slot.triangles,bytes,D3D12_HEAP_TYPE_DEFAULT))return false;
        if(reuse){auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(slot.triangles.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);list_->ResourceBarrier(1,&barrier);}
        list_->CopyBufferRegion(slot.triangles.Get(),0,slot.triangleUpload.Get(),0,bytes);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(slot.triangles.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&barrier);slot.triangleCount=rtTriangles_.size();
    }
    struct Material{uint32_t offset,albedo,normal,roughness;Vector4 tint;uint32_t ground;float pad[3];};
    static_assert(sizeof(Material)==48&&sizeof(RtTriangle)==96);
    std::vector<TextureHandle> textures;std::vector<Material> materials;materials.reserve(instances.size());
    auto index=[&](TextureHandle handle){if(handle>=textures_.size())handle=0;auto found=std::find(textures.begin(),textures.end(),handle);if(found!=textures.end())return uint32_t(found-textures.begin());textures.push_back(handle);return uint32_t(textures.size()-1);};
    for(const auto& item:instances)materials.push_back({rtGeometry_[item.mesh].triangleOffset,index(item.albedo),index(item.normal),index(item.roughness),item.color,item.ground?1u:0u,{}});
    if(textures.size()>32||!upload(slot.materials,materials.data(),materials.size()*sizeof(Material)))return false;
    UINT table=AllocateDynamicSrvIndex(32);if(table==UINT32_MAX)return false;
    for(UINT i=0;i<32;++i)dev_->CopyDescriptorsSimple(1,window_->SRV_CPU(table+i),textures_[i<textures.size()?textures[i]:0].srvCpuMaster,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    bool diagnostic=wcsstr(GetCommandLineW(),L"--rt-lighting-smoke")!=nullptr;
    struct Constants{Matrix4x4 inverse;Vector3 sun;float nearPlane;Vector3 camera;float farPlane;Vector2 dimensions;float fluid,enabled;Vector3 sunColor;float reflection;Vector3 ambient;float indirect;uint32_t diagnostics;float pad[3];} cb{};
    cb.inverse=XMToM4(XMMatrixInverse(nullptr,M4ToXM(cbFrame_.viewProj)));auto direction=lightCB_.dirLights[0].direction;
    float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);cb.sun=direction*(-1.f/(std::max)(length,.001f));cb.enabled=lightCB_.dirLights[0].enabled?1.f:0.f;cb.sunColor=lightCB_.dirLights[0].color;
    cb.ambient=lightCB_.ambientColor;cb.reflection=graphicsSettings_.rtReflections?1.f:0.f;cb.indirect=graphicsSettings_.rtIndirect?1.f:0.f;
    cb.diagnostics=diagnostic?1u:0u;
    float a=cbFrame_.proj.m[2][2],b=cbFrame_.proj.m[3][2];cb.nearPlane=std::abs(b/a);cb.farPlane=std::abs(b/(a-1));cb.camera=cbFrame_.cameraPos;cb.dimensions={float(rtReflectionTarget_.width),float(rtReflectionTarget_.height)};
    cb.fluid=volumeSurfaceDepth_&&gpuFluidCoreMode_>=4&&fluidVolumeDebugMode_==0?1.f:0.f;
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return false;std::memcpy(upload_[frame].mapped+offset,&cb,sizeof(cb));
    if(!slot.lightingCounters){auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);auto counterDesc=CD3DX12_RESOURCE_DESC::Buffer(16,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&counterDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&slot.lightingCounters))))return false;}
    UINT counterIndex=diagnostic?AllocateDynamicSrvIndex(1):0;if(counterIndex==UINT32_MAX)return false;
    std::vector<ID3D12Resource*> inputs{ppSceneDepth_.Get()};if(cb.fluid>.5f)inputs.push_back(volumeSurfaceDepth_.Get());
    for(auto handle:textures){auto* resource=textures_[handle].res.Get();if(resource&&std::find(inputs.begin(),inputs.end(),resource)==inputs.end())inputs.push_back(resource);}
    if(envMapSrvGpu_.ptr)for(auto& texture:textures_)if(texture.srvGpu.ptr==envMapSrvGpu_.ptr&&std::find(inputs.begin(),inputs.end(),texture.res.Get())==inputs.end())inputs.push_back(texture.res.Get());
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(resource,from,to);list_->ResourceBarrier(1,&barrier);};
    for(auto* input:inputs)transition(input,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(rtReflectionTarget_.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(rtIndirectTarget_.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetComputeRootSignature(rootSigRtLighting_.Get());list_->SetPipelineState(psoRtLighting_.Get());ID3D12DescriptorHeap* heaps[]={srvHeap_};list_->SetDescriptorHeaps(1,heaps);
    if(diagnostic){D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;ud.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=4;ud.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;dev_->CreateUnorderedAccessView(slot.lightingCounters.Get(),nullptr,&ud,window_->SRV_CPU(counterIndex));dev_->CreateUnorderedAccessView(slot.lightingCounters.Get(),nullptr,&ud,window_->SRV_CPU_Master(counterIndex));UINT zero[4]{};list_->ClearUnorderedAccessViewUint(window_->SRV_GPU(counterIndex),window_->SRV_CPU_Master(counterIndex),slot.lightingCounters.Get(),zero,0,nullptr);auto barrier=CD3DX12_RESOURCE_BARRIER::UAV(slot.lightingCounters.Get());list_->ResourceBarrier(1,&barrier);}
    list_->SetComputeRootUnorderedAccessView(10,slot.lightingCounters->GetGPUVirtualAddress());
    list_->SetComputeRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);list_->SetComputeRootShaderResourceView(1,slot.tlas->GetGPUVirtualAddress());
    list_->SetComputeRootDescriptorTable(2,ppDepthSrvGpu_);list_->SetComputeRootDescriptorTable(3,cb.fluid>.5f?volumeSurfaceDepthSrv_:ppDepthSrvGpu_);
    list_->SetComputeRootShaderResourceView(4,slot.triangles->GetGPUVirtualAddress());list_->SetComputeRootShaderResourceView(5,slot.materials->GetGPUVirtualAddress());
    list_->SetComputeRootDescriptorTable(6,envMapSrvGpu_.ptr?envMapSrvGpu_:liquidNullCubeSrv_);list_->SetComputeRootDescriptorTable(7,window_->SRV_GPU(table));
    list_->SetComputeRootDescriptorTable(8,rtReflectionUav_);list_->SetComputeRootDescriptorTable(9,rtIndirectUav_);list_->Dispatch((rtReflectionTarget_.width+7)/8,(rtReflectionTarget_.height+7)/8,1);
    bool readback=diagnostic&&buffer(slot.lightingReadback,total*2+16,D3D12_HEAP_TYPE_READBACK);
    BloomTarget* targets[]={&rtReflectionTarget_,&rtIndirectTarget_};
    for(int i=0;i<2;++i){auto* resource=targets[i]->resource.Get();
        if(readback){transition(resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=slot.lightingReadback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;to.PlacedFootprint.Offset=UINT64(i)*total;list_->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
        else transition(resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    if(readback){transition(slot.lightingCounters.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list_->CopyBufferRegion(slot.lightingReadback.Get(),total*2,slot.lightingCounters.Get(),0,16);transition(slot.lightingCounters.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    slot.lightingPending=readback;for(auto* input:inputs)transition(input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    EndFluidProfile(RtTrace);
    BeginFluidProfile(RtDenoise);
    DenoiseRtLighting_(frame,cb.nearPlane,cb.farPlane);
    EndFluidProfile(RtDenoise);
    CompositeRtLighting_(frame,cb.nearPlane,cb.farPlane,cb.fluid>.5f);
    ++rtLightingFrames_;return true;
}
void Renderer::DenoiseRtLighting_(uint32_t frame,float nearPlane,float farPlane){
    uint32_t previous=rtHistoryIndex_,current=previous^1;
    uint32_t mask=(graphicsSettings_.rtReflections?1u:0u)|(graphicsSettings_.rtIndirect?2u:0u);
    auto delta=cbFrame_.cameraPos-previousRtCamera_;float travel=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    struct Constants{Matrix4x4 inverse,previous;Vector2 dimensions;float nearPlane,farPlane,valid,pad[3];} cb{};
    cb.inverse=XMToM4(XMMatrixInverse(nullptr,M4ToXM(cbFrame_.viewProj)));cb.previous=previousRtViewProjection_;
    cb.dimensions={float(rtReflectionTarget_.width),float(rtReflectionTarget_.height)};cb.nearPlane=nearPlane;cb.farPlane=farPlane;
    cb.valid=rtLightingHistoryValid_&&postHistoryValid_&&mask==rtHistoryMask_&&travel<5?1.f:0.f;
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return;std::memcpy(upload_[frame].mapped+offset,&cb,sizeof(cb));
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){auto b=CD3DX12_RESOURCE_BARRIER::Transition(resource,from,to);list_->ResourceBarrier(1,&b);};
    BloomTarget* inputs[]={&rtReflectionTarget_,&rtIndirectTarget_,&rtFilteredReflection_[previous],&rtFilteredIndirect_[previous]};
    for(auto* input:inputs)transition(input->resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(ppSceneDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    BloomTarget* outputs[]={&rtFilteredReflection_[current],&rtFilteredIndirect_[current]};
    for(auto* output:outputs)transition(output->resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->SetComputeRootSignature(rootSigRtDenoise_.Get());list_->SetPipelineState(psoRtDenoise_.Get());
    list_->SetComputeRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);
    for(UINT i=0;i<4;++i)list_->SetComputeRootDescriptorTable(i+1,inputs[i]->srv);
    list_->SetComputeRootDescriptorTable(5,ppDepthSrvGpu_);list_->SetComputeRootDescriptorTable(6,rtFilteredReflectionUav_[current]);list_->SetComputeRootDescriptorTable(7,rtFilteredIndirectUav_[current]);
    list_->Dispatch((rtReflectionTarget_.width+7)/8,(rtReflectionTarget_.height+7)/8,1);
    for(auto* input:inputs)transition(input->resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    for(auto* output:outputs)transition(output->resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition(ppSceneDepth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    previousRtViewProjection_=cbFrame_.viewProj;previousRtCamera_=cbFrame_.cameraPos;rtHistoryIndex_=current;rtHistoryMask_=mask;rtLightingHistoryValid_=true;
}
void Renderer::CompositeRtLighting_(uint32_t frame,float nearPlane,float farPlane,bool fluid){
    struct Post{float data[28]{};Matrix4x4 inverse{},previous{};} cb{};
    cb.data[11]=nearPlane;cb.data[12]=farPlane;cb.data[24]=fluid?1.f:0.f;cb.data[26]=graphicsSettings_.rtReflections?1.f:0.f;cb.data[27]=graphicsSettings_.rtIndirect?1.f:0.f;
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return;std::memcpy(upload_[frame].mapped+offset,&cb,sizeof(cb));
    auto toRender=CD3DX12_RESOURCE_BARRIER::Transition(rtLightingHdr_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);list_->ResourceBarrier(1,&toRender);
    list_->SetGraphicsRootSignature(rootSigPP_.Get());list_->SetPipelineState(psoRtLightingComposite_.Get());
    auto rtv=rtLightingRtvHeap_->GetCPUDescriptorHandleForHeapStart();list_->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    D3D12_VIEWPORT vp{0,0,float(sceneWidth_),float(sceneHeight_),0,1};D3D12_RECT scissor{0,0,LONG(sceneWidth_),LONG(sceneHeight_)};list_->RSSetViewports(1,&vp);list_->RSSetScissorRects(1,&scissor);
    list_->SetGraphicsRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);
    for(UINT i=1;i<10;++i)list_->SetGraphicsRootDescriptorTable(i,ppSrvGpu_);
    list_->SetGraphicsRootDescriptorTable(4,ppDepthSrvGpu_);list_->SetGraphicsRootDescriptorTable(6,fluid?volumeSurfaceDepthSrv_:ppDepthSrvGpu_);list_->SetGraphicsRootDescriptorTable(8,rtFilteredReflection_[rtHistoryIndex_].srv);list_->SetGraphicsRootDescriptorTable(9,rtFilteredIndirect_[rtHistoryIndex_].srv);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list_->DrawInstanced(3,1,0,0);
    D3D12_RESOURCE_BARRIER copies[]={CD3DX12_RESOURCE_BARRIER::Transition(rtLightingHdr_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE),CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST)};list_->ResourceBarrier(2,copies);
    list_->CopyResource(ppSceneColor_.Get(),rtLightingHdr_.Get());auto toRead=CD3DX12_RESOURCE_BARRIER::Transition(ppSceneColor_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&toRead);
    list_->RSSetViewports(1,&viewport_);list_->RSSetScissorRects(1,&scissor_);
}
