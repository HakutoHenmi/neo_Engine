// Included inside Engine from Renderer.cpp.
void Renderer::BindPbrTextures_(const std::vector<TextureHandle>& material) {
    if(!neutralNormal_)neutralNormal_=LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png",false);
    UINT index=AllocateDynamicSrvIndex(3);if(index==UINT32_MAX)return;
    for(UINT i=0;i<3;++i){TextureHandle handle=i<material.size()?material[i]:(i==0?neutralNormal_:0);
        if(handle>=textures_.size())handle=0;
        dev_->CopyDescriptorsSimple(1,window_->SRV_CPU(index+i),textures_[handle].srvCpuMaster,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    list_->SetGraphicsRootDescriptorTable(8,window_->SRV_GPU(index));
}
bool Renderer::InitHdrBloom_() {
    neutralNormal_=LoadTexture2D("Resources/Textures/PBR/NeutralNormal.png",false);
    if(!neutralNormal_)return false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=kBloomLevels*2;
    if(FAILED(dev_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&bloomRtvHeap_))))return false;
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const UINT increment=dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for(UINT i=0;i<kBloomLevels*2;++i){
        UINT level=i<kBloomLevels?i:i-kBloomLevels;
        bool ao=i==kBloomLevels*2-1;
        auto& target=ao?gtaoTarget_:(i<kBloomLevels?bloomDown_[level]:bloomUp_[level]);
        target.width=ao?sceneWidth_:(std::max)(1u,UINT(WindowDX::kW)>>(level+1));target.height=ao?sceneHeight_:(std::max)(1u,UINT(WindowDX::kH)>>(level+1));
        auto rd=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,target.width,target.height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear{DXGI_FORMAT_R16G16B16A16_FLOAT,{0,0,0,1}};
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&target.resource))))return false;
        target.rtv=CD3DX12_CPU_DESCRIPTOR_HANDLE(bloomRtvHeap_->GetCPUDescriptorHandleForHeapStart(),i,increment);
        dev_->CreateRenderTargetView(target.resource.Get(),nullptr,target.rtv);
        UINT index=AllocateSrvIndex();target.srv=window_->SRV_GPU(index);
        dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU(index));
        dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU_Master(index));
    }
    auto vs=CompileShaderFromFile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0");
    if(!vs)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=rootSigPP_.Get();
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.NumRenderTargets=1;pso.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pso.SampleDesc.Count=1;pso.SampleMask=UINT_MAX;
    pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);pso.DepthStencilState.DepthEnable=FALSE;
    for(int i=0;i<3;++i){auto ps=CompileShaderFromFile(i==2?L"Resources/shaders/Gtao.hlsl":L"Resources/shaders/HdrBloom.hlsl",i==2?"main":(i?"Upsample":"Downsample"),"ps_5_0");if(!ps)return false;
        pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};auto& target=i==2?psoGtao_:(i?psoBloomUp_:psoBloomDown_);
        if(FAILED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&target))))return false;
    }
    return true;
}
void Renderer::RenderGtao_(uint32_t frame) {
    float a=cbFrame_.proj.m[2][2],b=cbFrame_.proj.m[3][2];
    float constants[8]={std::abs(b/a),std::abs(b/(a-1)),cbFrame_.proj.m[0][0],cbFrame_.proj.m[1][1],
        1.f/gtaoTarget_.width,1.f/gtaoTarget_.height,volumeSurfaceDepth_&&gpuFluidCoreMode_>=4&&fluidVolumeDebugMode_==0?1.f:0.f,3.6f};
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return;
    std::memcpy(upload_[frame].mapped+offset,constants,sizeof(constants));
    list_->SetGraphicsRootSignature(rootSigPP_.Get());ID3D12DescriptorHeap* heaps[]={srvHeap_};list_->SetDescriptorHeaps(1,heaps);
    list_->SetGraphicsRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);
    for(UINT root=1;root<=6;++root)list_->SetGraphicsRootDescriptorTable(root,root==2&&constants[6]>.5f?volumeSurfaceDepthSrv_:ppDepthSrvGpu_);
    auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(gtaoTarget_.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);list_->ResourceBarrier(1,&barrier);
    list_->OMSetRenderTargets(1,&gtaoTarget_.rtv,FALSE,nullptr);list_->SetPipelineState(psoGtao_.Get());
    D3D12_VIEWPORT vp{0,0,float(gtaoTarget_.width),float(gtaoTarget_.height),0,1};
    D3D12_RECT rect{0,0,LONG(gtaoTarget_.width),LONG(gtaoTarget_.height)};
    list_->RSSetViewports(1,&vp);list_->RSSetScissorRects(1,&rect);list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list_->DrawInstanced(3,1,0,0);
    barrier=CD3DX12_RESOURCE_BARRIER::Transition(gtaoTarget_.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&barrier);
}
void Renderer::RenderHdrBloom_(uint32_t frame) {
    list_->SetGraphicsRootSignature(rootSigPP_.Get());ID3D12DescriptorHeap* heaps[]={srvHeap_};list_->SetDescriptorHeaps(1,heaps);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto pass=[&](BloomTarget& target,D3D12_GPU_DESCRIPTOR_HANDLE source,D3D12_GPU_DESCRIPTOR_HANDLE lower,UINT width,UINT height,bool first,bool up){
        float constants[4]={1.f/width,1.f/height,first?1.f:0.f,0};UINT offset=upload_[frame].Allocate(256,256);
        if(offset==UINT32_MAX)return;
        std::memcpy(upload_[frame].mapped+offset,constants,sizeof(constants));
        list_->SetGraphicsRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);list_->ResourceBarrier(1,&barrier);
        list_->OMSetRenderTargets(1,&target.rtv,FALSE,nullptr);list_->SetPipelineState(up?psoBloomUp_.Get():psoBloomDown_.Get());
        D3D12_VIEWPORT vp{0,0,float(target.width),float(target.height),0,1};D3D12_RECT rect{0,0,LONG(target.width),LONG(target.height)};
        list_->RSSetViewports(1,&vp);list_->RSSetScissorRects(1,&rect);
        for(UINT root=1;root<=6;++root)list_->SetGraphicsRootDescriptorTable(root,root==2?lower:source);
        list_->DrawInstanced(3,1,0,0);
        barrier=CD3DX12_RESOURCE_BARRIER::Transition(target.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&barrier);
    };
    for(UINT i=0;i<kBloomLevels;++i)pass(bloomDown_[i],i?bloomDown_[i-1].srv:postColorSrv_,postColorSrv_,i?bloomDown_[i-1].width:WindowDX::kW,i?bloomDown_[i-1].height:WindowDX::kH,i==0,false);
    for(int i=kBloomLevels-2;i>=0;--i){auto& lower=i==kBloomLevels-2?bloomDown_[i+1]:bloomUp_[i+1];pass(bloomUp_[i],bloomDown_[i].srv,lower.srv,lower.width,lower.height,false,true);}
}
