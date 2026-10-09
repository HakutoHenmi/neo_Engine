bool Renderer::InitTemporalInputs_(){
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;
    if(FAILED(dev_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&temporalRtvHeap_))))return false;
    BloomTarget* targets[]={&temporalMotion_,&temporalDepth_,&dlssOutput_};DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
    for(UINT i=0;i<3;++i){auto& target=*targets[i];target.width=i==2?WindowDX::kW:sceneWidth_;target.height=i==2?WindowDX::kH:sceneHeight_;
        auto rd=CD3DX12_RESOURCE_DESC::Tex2D(formats[i],target.width,target.height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|(i==2?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE));
        if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target.resource))))return false;
        target.rtv=CD3DX12_CPU_DESCRIPTOR_HANDLE(temporalRtvHeap_->GetCPUDescriptorHandleForHeapStart(),i,dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));dev_->CreateRenderTargetView(target.resource.Get(),nullptr,target.rtv);
        UINT index=AllocateSrvIndex();target.srv=window_->SRV_GPU(index);dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU(index));dev_->CreateShaderResourceView(target.resource.Get(),nullptr,window_->SRV_CPU_Master(index));
    }
    CD3DX12_DESCRIPTOR_RANGE ranges[4];CD3DX12_ROOT_PARAMETER parameters[5];parameters[0].InitAsConstantBufferView(0);
    for(UINT i=0;i<4;++i){ranges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i);parameters[i+1].InitAsDescriptorTable(1,&ranges[i],D3D12_SHADER_VISIBILITY_PIXEL);}
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC rs(5,parameters,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)))return false;
    if(FAILED(dev_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&rootSigTemporal_))))return false;
    auto vs=CompileShaderFromFile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),ps=CompileShaderFromFile(L"Resources/shaders/TemporalPrepare.hlsl","main","ps_5_0");if(!vs||!ps)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};pso.pRootSignature=rootSigTemporal_.Get();pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.NumRenderTargets=2;pso.RTVFormats[0]=formats[0];pso.RTVFormats[1]=formats[1];pso.SampleDesc.Count=1;pso.SampleMask=UINT_MAX;pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);pso.DepthStencilState.DepthEnable=FALSE;
    if(FAILED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&psoTemporalPrepare_))))return false;
    vs=CompileShaderFromFile(L"Resources/shaders/TemporalObject.hlsl","VSMain","vs_5_0");ps=CompileShaderFromFile(L"Resources/shaders/TemporalObject.hlsl","PSMain","ps_5_0");if(!vs||!ps)return false;
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pso.NumRenderTargets=1;pso.RTVFormats[1]=DXGI_FORMAT_UNKNOWN;
    D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};pso.InputLayout={layout,2};
    return SUCCEEDED(dev_->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&psoTemporalObjects_)));
}
bool Renderer::EvaluateDlss_(uint32_t frame){
    struct Prepare{Matrix4x4 inverse,previous,current;float nearPlane,farPlane,includeFluid,dt;Vector3 origin;float cell;Vector3 dimensions;float pad;} cb{};
    cb.inverse=XMToM4(XMMatrixInverse(nullptr,M4ToXM(cbFrame_.viewProj)));cb.previous=postHistoryValid_?previousTemporalViewProjection_:unjitteredViewProjection_;cb.current=unjitteredViewProjection_;
    float a=cbFrame_.proj.m[2][2],b=cbFrame_.proj.m[3][2];cb.nearPlane=std::abs(b/a);cb.farPlane=std::abs(b/(a-1));cb.includeFluid=volumeSurfaceDepth_&&gpuFluidCoreMode_>=4?1.f:0.f;
    cb.dt=rogueWorldFrozen_?0:fluidSimulatedDt_;cb.origin=volumePreviousOrigin_;cb.cell=.56f;cb.dimensions={176,64,176};
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return false;std::memcpy(upload_[frame].mapped+offset,&cb,sizeof(cb));
    auto transition=[&](ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(resource,from,to);list_->ResourceBarrier(1,&barrier);};
    transition(temporalMotion_.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);transition(temporalDepth_.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    list_->SetGraphicsRootSignature(rootSigTemporal_.Get());ID3D12DescriptorHeap* heaps[]={srvHeap_};list_->SetDescriptorHeaps(1,heaps);
    list_->SetGraphicsRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);
    list_->SetGraphicsRootDescriptorTable(1,ppDepthSrvGpu_);list_->SetGraphicsRootDescriptorTable(2,volumeSurfaceDepthSrv_);list_->SetGraphicsRootDescriptorTable(3,volumeSrv_[4]);list_->SetGraphicsRootDescriptorTable(4,textures_[0].srvGpu);
    D3D12_CPU_DESCRIPTOR_HANDLE targets[]={temporalMotion_.rtv,temporalDepth_.rtv};list_->OMSetRenderTargets(2,targets,FALSE,nullptr);list_->SetPipelineState(psoTemporalPrepare_.Get());list_->RSSetViewports(1,&viewport_);list_->RSSetScissorRects(1,&scissor_);list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list_->DrawInstanced(3,1,0,0);
    list_->OMSetRenderTargets(1,targets,FALSE,nullptr);list_->SetPipelineState(psoTemporalObjects_.Get());
    struct Object{Matrix4x4 world,previousWorld,jittered,current,previous;float nearPlane,farPlane,includeFluid,pad;} object{};
    object.jittered=cbFrame_.viewProj;object.current=cb.current;object.previous=cb.previous;object.nearPlane=cb.nearPlane;object.farPlane=cb.farPlane;object.includeFluid=cb.includeFluid;
    std::unordered_map<uint64_t,Matrix4x4> currentWorlds;
    for(const auto& job:temporalJobs_){auto* model=GetModel(job.mesh);if(!model)continue;object.world=job.world;auto last=temporalWorlds_.find(job.id);object.previousWorld=last!=temporalWorlds_.end()&&postHistoryValid_?last->second:job.world;currentWorlds[job.id]=job.world;
        UINT off=upload_[frame].Allocate(512,256);if(off==UINT32_MAX)break;std::memcpy(upload_[frame].mapped+off,&object,sizeof(object));list_->SetGraphicsRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+off);
        list_->SetGraphicsRootDescriptorTable(4,job.texture<textures_.size()?textures_[job.texture].srvGpu:textures_[0].srvGpu);auto vb=model->GetVBV();auto ib=model->GetIBV();list_->IASetVertexBuffers(0,1,&vb);list_->IASetIndexBuffer(&ib);list_->DrawIndexedInstanced(model->GetIndexCount(),1,0,0,0);
    }
    temporalWorlds_.swap(currentWorlds);
    transition(temporalMotion_.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);transition(temporalDepth_.resource.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    sl::Constants camera{};auto matrix=[](const Matrix4x4& source,sl::float4x4& target){std::memcpy(&target,&source,sizeof(source));};
    matrix(unjitteredProjection_,camera.cameraViewToClip);matrix(XMToM4(XMMatrixInverse(nullptr,M4ToXM(unjitteredProjection_))),camera.clipToCameraView);
    auto clipToPrevious=XMMatrixInverse(nullptr,M4ToXM(unjitteredViewProjection_))*M4ToXM(cb.previous);matrix(XMToM4(clipToPrevious),camera.clipToPrevClip);matrix(XMToM4(XMMatrixInverse(nullptr,clipToPrevious)),camera.prevClipToClip);
    camera.jitterOffset={temporalJitter_.x,temporalJitter_.y};camera.mvecScale={1,1};camera.cameraPos={cbFrame_.cameraPos.x,cbFrame_.cameraPos.y,cbFrame_.cameraPos.z};
    auto inverseView=XMToM4(XMMatrixInverse(nullptr,M4ToXM(cbFrame_.view)));camera.cameraRight={inverseView.m[0][0],inverseView.m[0][1],inverseView.m[0][2]};camera.cameraUp={inverseView.m[1][0],inverseView.m[1][1],inverseView.m[1][2]};camera.cameraFwd={inverseView.m[2][0],inverseView.m[2][1],inverseView.m[2][2]};
    camera.cameraNear=cb.nearPlane;camera.cameraFar=cb.farPlane;camera.cameraFOV=2*std::atan(1/unjitteredProjection_.m[1][1]);camera.cameraAspectRatio=float(sceneWidth_)/sceneHeight_;
    camera.depthInverted=sl::Boolean::eFalse;camera.cameraMotionIncluded=sl::Boolean::eTrue;camera.motionVectors3D=sl::Boolean::eFalse;
    auto delta=cbFrame_.cameraPos-previousPostCamera_;float travel=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    float alignment=camera.cameraFwd.x*previousTemporalForward_.x+camera.cameraFwd.y*previousTemporalForward_.y+camera.cameraFwd.z*previousTemporalForward_.z;
    camera.reset=!postHistoryValid_||travel>40||alignment<.7f||std::abs(camera.cameraFOV-previousTemporalFov_)>.15f?sl::Boolean::eTrue:sl::Boolean::eFalse;
    previousTemporalForward_={camera.cameraFwd.x,camera.cameraFwd.y,camera.cameraFwd.z};previousTemporalFov_=camera.cameraFOV;
    bool result=DlssRuntime::Get().Evaluate(list_,ppSceneColor_.Get(),temporalDepth_.resource.Get(),temporalMotion_.resource.Get(),dlssOutput_.resource.Get(),camera,WindowDX::kW,WindowDX::kH);
    if(result)++dlssEvaluatedFrames_;
    // Streamline does not track/restore command-list bindings in this integration.
    list_->SetDescriptorHeaps(1,heaps);return result;
}
