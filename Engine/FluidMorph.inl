// Included inside Engine in Renderer.cpp. The descriptor table costs one root DWORD,
// keeping the existing 48-DWORD constants and seven UAVs within the 64-DWORD limit.
void Renderer::InitFluidMorph(){
    auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("Fluid morph GPU resource creation failed");};
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto scratch=CD3DX12_RESOURCE_DESC::Buffer(16384*48,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    check(dev_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&scratch,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&fluidMorphScratch_)));
    auto upload=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);auto dummy=CD3DX12_RESOURCE_DESC::Buffer(sizeof(FluidMorphTarget));
    check(dev_->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&dummy,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&fluidMorphDummy_)));
    void* data=nullptr;check(fluidMorphDummy_->Map(0,nullptr,&data));memset(data,0,sizeof(FluidMorphTarget));fluidMorphDummy_->Unmap(0,nullptr);
    auto make=[&](const char* entry,ComPtr<ID3D12PipelineState>& pso){auto cs=CompileShaderFromFile(L"Resources/shaders/FluidSimCS.hlsl",entry,"cs_5_0");
        if(!cs)throw std::runtime_error("Slime point-cloud shader failed");D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=rootSigFluid_.Get();desc.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
        check(dev_->CreateComputePipelineState(&desc,IID_PPV_ARGS(&pso)));};
    make("MorphKeys",psoMorphKeys_);make("MorphSort",psoMorphSort_);make("MorphAssign",psoMorphAssign_);
    make("MorphMeasure",psoMorphMeasure_);
    auto read=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);auto desc=CD3DX12_RESOURCE_DESC::Buffer(kFluidMorphBodyCount*48);
    for(auto& buffer:morphReadback_)check(dev_->CreateCommittedResource(&read,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)));
    fluidMorphFrame_=nullptr;fluidMorphApplied_=~uint64_t(0);
}
void Renderer::QueueMorphMeasure(){
    if(fluidMorphTargets_.size()!=kPlayerFluidParticles)return;
    list_->SetPipelineState(psoMorphMeasure_.Get());list_->Dispatch(1,1,1);
    auto ready=CD3DX12_RESOURCE_BARRIER::UAV(fluidMorphScratch_.Get());list_->ResourceBarrier(1,&ready);
    auto copy=CD3DX12_RESOURCE_BARRIER::Transition(fluidMorphScratch_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list_->ResourceBarrier(1,&copy);
    auto frame=window_->FrameIndex();list_->CopyBufferRegion(morphReadback_[frame].Get(),0,fluidMorphScratch_.Get(),16000*48,kFluidMorphBodyCount*48);
    auto solve=CD3DX12_RESOURCE_BARRIER::Transition(fluidMorphScratch_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);list_->ResourceBarrier(1,&solve);
    morphPending_[frame]=true;morphReadbackRevision_[frame]=fluidMorphRevision_;
}
void Renderer::CollectMorphMeasure(uint32_t frame){
    if(!morphPending_[frame]||!morphReadback_[frame])return;
    // BeginFrame has already waited for this frame slot's fence; never wait here.
    D3D12_RANGE range{0,kFluidMorphBodyCount*48};void* data=nullptr;
    if(SUCCEEDED(morphReadback_[frame]->Map(0,&range,&data))){
        const auto* bytes=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<kFluidMorphBodyCount;++i){float value;memcpy(&value,bytes+i*48,sizeof(float));morphErrors_[i]=std::isfinite(value)?value:1000.f;}
        D3D12_RANGE noWrite{0,0};morphReadback_[frame]->Unmap(0,&noWrite);morphMeasuredRevision_=morphReadbackRevision_[frame];
    }
    morphPending_[frame]=false;
}
void Renderer::BindFluidMorphResources(ID3D12Resource* aabb,uint32_t offset){
    auto index=AllocateDynamicSrvIndex(3);if(index==UINT32_MAX)throw std::runtime_error("Fluid descriptor budget exhausted");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer.FirstElement=offset/32;srv.Buffer.NumElements=128;srv.Buffer.StructureByteStride=32;
    dev_->CreateShaderResourceView(aabb,&srv,window_->SRV_CPU(index));
    srv.Buffer.FirstElement=fluidMorphFrame_?fluidMorphOffset_/sizeof(FluidMorphTarget):0;srv.Buffer.NumElements=fluidMorphFrame_?kPlayerFluidParticles:1;srv.Buffer.StructureByteStride=sizeof(FluidMorphTarget);
    dev_->CreateShaderResourceView(fluidMorphFrame_?fluidMorphFrame_:fluidMorphDummy_.Get(),&srv,window_->SRV_CPU(index+1));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=16384;uav.Buffer.StructureByteStride=48;
    dev_->CreateUnorderedAccessView(fluidMorphScratch_.Get(),nullptr,&uav,window_->SRV_CPU(index+2));
    list_->SetComputeRootDescriptorTable(6,window_->SRV_GPU(index));
}
