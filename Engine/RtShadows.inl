// Limited DXR 1.1 visibility for opaque scenery. Vegetation and implicit fluid
// are receivers, but are deliberately not triangle acceleration structures.
bool Renderer::InitRtShadows_(){
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    if(FAILED(dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options)))||options.RaytracingTier<D3D12_RAYTRACING_TIER_1_1)return false;
    D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
    if(FAILED(dev_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model)))||model.HighestShaderModel<D3D_SHADER_MODEL_6_5)return false;
    std::ifstream source("Resources/shaders/RtShadow.cso",std::ios::binary);
    std::vector<char> shader((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());if(shader.empty())return false;
    CD3DX12_DESCRIPTOR_RANGE ranges[3];CD3DX12_ROOT_PARAMETER roots[5];roots[0].InitAsConstantBufferView(0);roots[1].InitAsShaderResourceView(0);
    for(UINT i=0;i<3;++i){ranges[i].Init(i==2?D3D12_DESCRIPTOR_RANGE_TYPE_UAV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i==2?0:i+1);roots[i+2].InitAsDescriptorTable(1,&ranges[i]);}
    CD3DX12_ROOT_SIGNATURE_DESC rs(5,roots);ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||FAILED(dev_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&rootSigRtShadow_))))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=rootSigRtShadow_.Get();pso.CS={shader.data(),shader.size()};
    if(FAILED(dev_->CreateComputePipelineState(&pso,IID_PPV_ARGS(&psoRtShadow_))))return false;
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    rtShadowTarget_.width=WindowDX::kW/2;rtShadowTarget_.height=WindowDX::kH/2;
    auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_FLOAT,rtShadowTarget_.width,rtShadowTarget_.height,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if(FAILED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&rtShadowTarget_.resource)))){psoRtShadow_.Reset();return false;}
    UINT srv=AllocateSrvIndex();rtShadowTarget_.srv=window_->SRV_GPU(srv);dev_->CreateShaderResourceView(rtShadowTarget_.resource.Get(),nullptr,window_->SRV_CPU(srv));dev_->CreateShaderResourceView(rtShadowTarget_.resource.Get(),nullptr,window_->SRV_CPU_Master(srv));
    UINT uav=AllocateSrvIndex();rtShadowUav_=window_->SRV_GPU(uav);D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=desc.Format;ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;dev_->CreateUnorderedAccessView(rtShadowTarget_.resource.Get(),nullptr,&ud,window_->SRV_CPU(uav));dev_->CreateUnorderedAccessView(rtShadowTarget_.resource.Get(),nullptr,&ud,window_->SRV_CPU_Master(uav));
    return true;
}
bool Renderer::RenderRtShadows_(uint32_t frame){
    if(!psoRtShadow_||rtInstances_.empty())return false;
    ComPtr<ID3D12Device5> device;ComPtr<ID3D12GraphicsCommandList4> commands;
    if(FAILED(dev_->QueryInterface(IID_PPV_ARGS(&device)))||FAILED(list_->QueryInterface(IID_PPV_ARGS(&commands))))return false;
    auto buffer=[&](ComPtr<ID3D12Resource>& resource,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
        if(resource&&resource->GetDesc().Width>=bytes)return true;
        // Per-frame resources are resized only after the slot's fence has completed.
        auto heap=CD3DX12_HEAP_PROPERTIES(type);auto desc=CD3DX12_RESOURCE_DESC::Buffer((bytes+255)&~UINT64(255),type==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE);
        return SUCCEEDED(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));
    };
    auto uavBarrier=[&](ID3D12Resource* resource){auto b=CD3DX12_RESOURCE_BARRIER::UAV(resource);list_->ResourceBarrier(1,&b);};
    BeginFluidProfile(RtAcceleration);
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;instances.reserve(rtInstances_.size());
    std::vector<RtInstance> surfaces;surfaces.reserve(rtInstances_.size());
    for(const auto& item:rtInstances_){auto* mesh=GetModel(item.mesh);if(!mesh||!mesh->GetIndexCount()||!mesh->GetData().bones.empty())continue;
        auto& geometry=rtGeometry_[item.mesh];
        if(!geometry.blas){D3D12_RAYTRACING_GEOMETRY_DESC triangle{};triangle.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;triangle.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            auto vb=mesh->GetVBV();auto ib=mesh->GetIBV();triangle.Triangles.VertexBuffer={vb.BufferLocation,vb.StrideInBytes};triangle.Triangles.VertexCount=vb.SizeInBytes/vb.StrideInBytes;triangle.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
            triangle.Triangles.IndexBuffer=ib.BufferLocation;triangle.Triangles.IndexCount=mesh->GetIndexCount();triangle.Triangles.IndexFormat=ib.Format;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;build.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;build.Inputs.NumDescs=1;build.Inputs.pGeometryDescs=&triangle;build.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs,&info);
            if(!info.ResultDataMaxSizeInBytes||!buffer(geometry.blas,info.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)||!buffer(geometry.scratch,info.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
            build.DestAccelerationStructureData=geometry.blas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=geometry.scratch->GetGPUVirtualAddress();commands->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(geometry.blas.Get());
            geometry.triangleOffset=uint32_t(rtTriangles_.size());
            const auto& data=mesh->GetData();
            for(size_t i=0;i+2<data.indices.size();i+=3){RtTriangle tri{};for(int v=0;v<3;++v){const auto& vertex=data.vertices[data.indices[i+v]];tri.p[v]={vertex.position.x,vertex.position.y,vertex.position.z,vertex.texcoord.x};tri.n[v]={vertex.normal.x,vertex.normal.y,vertex.normal.z,vertex.texcoord.y};}rtTriangles_.push_back(tri);}
        }
        D3D12_RAYTRACING_INSTANCE_DESC instance{};for(UINT r=0;r<3;++r)for(UINT c=0;c<4;++c)instance.Transform[r][c]=item.world.m[c][r];
        instance.InstanceID=UINT(surfaces.size());instance.InstanceMask=1;instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;instance.AccelerationStructure=geometry.blas->GetGPUVirtualAddress();instances.push_back(instance);surfaces.push_back(item);
    }
    if(instances.empty())return false;
    auto& slot=rtFrames_[frame];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;auto targetDesc=rtShadowTarget_.resource->GetDesc();dev_->GetCopyableFootprints(&targetDesc,0,1,0,&footprint,nullptr,nullptr,&total);
    if(slot.pending){void* data=nullptr;D3D12_RANGE range{0,SIZE_T(total)};
        if(SUCCEEDED(slot.readback->Map(0,&range,&data))){uint32_t shadow=0,lit=0;for(UINT y=0;y<rtShadowTarget_.height;++y){auto* row=reinterpret_cast<float*>(static_cast<char*>(data)+footprint.Footprint.RowPitch*y);for(UINT x=0;x<rtShadowTarget_.width;++x){float v=row[x*2],z=row[x*2+1];if(std::isfinite(v)&&std::isfinite(z)&&z<1500){shadow+=v<.5f;lit+=v>.5f;}}}D3D12_RANGE written{0,0};slot.readback->Unmap(0,&written);rtShadowPixels_=shadow;rtLitPixels_=lit;}slot.pending=false;
    }
    bool rebuild=!sceneryOptimized_||!slot.tlas||slot.builtInstances.size()!=instances.size()||
        std::memcmp(slot.builtInstances.data(),instances.data(),instances.size()*sizeof(instances[0]))!=0;
    if(rebuild){
    if(!buffer(slot.instances,instances.size()*sizeof(instances[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ))return false;
    void* mapped=nullptr;D3D12_RANGE empty{0,0};if(FAILED(slot.instances->Map(0,&empty,&mapped)))return false;std::memcpy(mapped,instances.data(),instances.size()*sizeof(instances[0]));slot.instances->Unmap(0,nullptr);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;build.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;build.Inputs.NumDescs=UINT(instances.size());build.Inputs.InstanceDescs=slot.instances->GetGPUVirtualAddress();build.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs,&info);
    if(!buffer(slot.tlas,info.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)||!buffer(slot.scratch,info.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
    uavBarrier(slot.tlas.Get());uavBarrier(slot.scratch.Get());build.DestAccelerationStructureData=slot.tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=slot.scratch->GetGPUVirtualAddress();commands->BuildRaytracingAccelerationStructure(&build,0,nullptr);uavBarrier(slot.tlas.Get());
    slot.builtInstances=instances;++rtTlasBuilds_;
    }
    EndFluidProfile(RtAcceleration);
    if(!graphicsSettings_.rtShadows){rtLightingRendered_=RenderRtLighting_(frame,surfaces);return rtLightingRendered_;}
    struct Constants{Matrix4x4 inverse;Vector3 sun;float nearPlane;Vector3 camera;float farPlane;Vector2 dimensions;float fluid,enabled;} cb{};
    cb.inverse=XMToM4(XMMatrixInverse(nullptr,M4ToXM(cbFrame_.viewProj)));auto direction=lightCB_.dirLights[0].direction;float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);cb.sun=direction*(-1.f/(std::max)(length,.001f));cb.enabled=lightCB_.dirLights[0].enabled?1.f:0.f;
    float a=cbFrame_.proj.m[2][2],projectionB=cbFrame_.proj.m[3][2];cb.nearPlane=std::abs(projectionB/a);cb.farPlane=std::abs(projectionB/(a-1));cb.camera=cbFrame_.cameraPos;cb.dimensions={float(rtShadowTarget_.width),float(rtShadowTarget_.height)};cb.fluid=volumeSurfaceDepth_&&gpuFluidCoreMode_>=4&&fluidVolumeDebugMode_==0?1.f:0.f;
    UINT offset=upload_[frame].Allocate(256,256);if(offset==UINT32_MAX)return false;std::memcpy(upload_[frame].mapped+offset,&cb,sizeof(cb));
    auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){auto b=CD3DX12_RESOURCE_BARRIER::Transition(rtShadowTarget_.resource.Get(),from,to);list_->ResourceBarrier(1,&b);};
    transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // Compute reads require NON_PIXEL_SHADER_RESOURCE, whereas the scene remains a pixel input for post.
    auto opaque=CD3DX12_RESOURCE_BARRIER::Transition(ppSceneDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&opaque);
    if(cb.fluid>.5f){auto b=CD3DX12_RESOURCE_BARRIER::Transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&b);}
    list_->SetComputeRootSignature(rootSigRtShadow_.Get());list_->SetPipelineState(psoRtShadow_.Get());ID3D12DescriptorHeap* heaps[]={srvHeap_};list_->SetDescriptorHeaps(1,heaps);
    list_->SetComputeRootConstantBufferView(0,upload_[frame].buffer->GetGPUVirtualAddress()+offset);list_->SetComputeRootShaderResourceView(1,slot.tlas->GetGPUVirtualAddress());list_->SetComputeRootDescriptorTable(2,ppDepthSrvGpu_);list_->SetComputeRootDescriptorTable(3,cb.fluid>.5f?volumeSurfaceDepthSrv_:ppDepthSrvGpu_);list_->SetComputeRootDescriptorTable(4,rtShadowUav_);list_->Dispatch((rtShadowTarget_.width+7)/8,(rtShadowTarget_.height+7)/8,1);
    if(wcsstr(GetCommandLineW(),L"--rt-smoke")&&buffer(slot.readback,total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST)){transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=rtShadowTarget_.resource.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=slot.readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;list_->CopyTextureRegion(&to,0,0,0,&from,nullptr);transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);slot.pending=true;}
    else transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    opaque=CD3DX12_RESOURCE_BARRIER::Transition(ppSceneDepth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&opaque);
    if(cb.fluid>.5f){auto b=CD3DX12_RESOURCE_BARRIER::Transition(volumeSurfaceDepth_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);list_->ResourceBarrier(1,&b);}
    if(graphicsSettings_.rtShadows)++rtShadowFrames_;
    if(graphicsSettings_.rtReflections||graphicsSettings_.rtIndirect)rtLightingRendered_=RenderRtLighting_(frame,surfaces);
    return true;
}
