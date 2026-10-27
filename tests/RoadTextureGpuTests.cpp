// Hardware-only, isolated D3D12 experiment. Does not change the shipping texture path.
// libNTC headers/binaries are supplied separately by the official SDK in build/ntc/.
#define NOMINMAX
#define NTC_STATIC 1
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <DirectXTex.h>
#include <d3dx12.h>
#include <libntc/ntc.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
static const fs::path Out = L"tests/out/road-texture";
static const char* Names[] = {"albedo", "normal", "roughness", "ao"};
static const char* Suffixes[] = {"diff", "nor_dx", "Rough", "AO"};
static const DXGI_FORMAT Formats[] = {DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_UNORM};
static void Check(HRESULT hr) { if (FAILED(hr)) { char s[80]; sprintf_s(s,"D3D/texture failure: 0x%08X", unsigned(hr)); throw std::runtime_error(s); } }
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static double Ms(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
static double Median(std::vector<double> v) { std::sort(v.begin(),v.end()); return (v[(v.size()-1)/2]+v[v.size()/2])*.5; }
static double P95(std::vector<double> v) { std::sort(v.begin(),v.end()); return v[size_t(std::ceil(v.size()*.95))-1]; }

struct Gpu {
    ComPtr<IDXGIAdapter1> adapter; ComPtr<ID3D12Device> dev; ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc; ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12QueryHeap> queries; ComPtr<ID3D12Resource> times; ComPtr<ID3D12InfoQueue> info; HANDLE event = nullptr;
    UINT64 serial=0, frequency=0; std::string name; UINT64 driver=0;
    Gpu(bool debug) {
        if(debug) { ComPtr<ID3D12Debug> d; Check(D3D12GetDebugInterface(IID_PPV_ARGS(&d))); d->EnableDebugLayer(); }
        ComPtr<IDXGIFactory6> factory; Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
        for(UINT i=0; factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
            if(!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && desc.VendorId==0x10de && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dev)))) {
                char s[256]; WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,s,sizeof(s),nullptr,nullptr); name=s; break;
            }
        }
        Require(bool(dev),"NVIDIA D3D12 hardware required; WARP is forbidden.");
        if(debug) Check(dev.As(&info));
        LARGE_INTEGER version{}; Check(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)); driver=version.QuadPart;
        D3D12_COMMAND_QUEUE_DESC q{}; Check(dev->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))); Check(queue->GetTimestampFrequency(&frequency));
        Check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
        Check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))); Check(list->Close());
        Check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); event=CreateEvent(nullptr,FALSE,FALSE,nullptr); Require(event!=nullptr,"CreateEvent failed");
        D3D12_QUERY_HEAP_DESC qh{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,4,0}; Check(dev->CreateQueryHeap(&qh,IID_PPV_ARGS(&queries)));
        times=Buffer(32,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    }
    ~Gpu() { if(event) CloseHandle(event); }
    ComPtr<ID3D12Resource> Resource(D3D12_RESOURCE_DESC desc,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> r; auto hp=CD3DX12_HEAP_PROPERTIES(heap); Check(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r))); return r;
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 size,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state) { return Resource(CD3DX12_RESOURCE_DESC::Buffer((std::max<UINT64>(size,4)+3)&~3ull),heap,heap==D3D12_HEAP_TYPE_DEFAULT && state==D3D12_RESOURCE_STATE_COPY_DEST?D3D12_RESOURCE_STATE_COMMON:state); }
    void Begin() { Check(alloc->Reset()); Check(list->Reset(alloc.Get(),nullptr)); }
    void End() { Check(list->Close()); ID3D12CommandList* p=list.Get(); queue->ExecuteCommandLists(1,&p); Check(queue->Signal(fence.Get(),++serial)); Check(fence->SetEventOnCompletion(serial,event)); Require(WaitForSingleObject(event,120000)==WAIT_OBJECT_0,"GPU fence timeout"); Check(dev->GetDeviceRemovedReason());
        if(info) { bool bad=false; for(UINT64 i=0;i<info->GetNumStoredMessages();++i) { SIZE_T size=0; info->GetMessage(i,nullptr,&size); std::vector<uint8_t> bytes(size); auto msg=reinterpret_cast<D3D12_MESSAGE*>(bytes.data()); Check(info->GetMessage(i,msg,&size)); if(msg->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) std::cerr<<msg->pDescription<<'\n'; bad|=msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR; } info->ClearStoredMessages(); Require(!bad,"D3D12 debug layer reported an error"); }
    }
    void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) { auto b=CD3DX12_RESOURCE_BARRIER::Transition(r,before,after); list->ResourceBarrier(1,&b); }
    void Stamp(UINT n) { list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,n); }
    std::array<double,3> Finish() {
        list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,4,times.Get(),0); End();
        UINT64* p=nullptr; D3D12_RANGE range{0,32}; Check(times->Map(0,&range,reinterpret_cast<void**>(&p)));
        std::array<double,3> v{}; for(int i=0;i<3;++i) v[i]=double(p[i+1]-p[i])*1000./frequency;
        D3D12_RANGE empty{0,0}; times->Unmap(0,&empty); return v;
    }
    UINT64 Bytes(ID3D12Resource* r) { auto d=r->GetDesc(); return dev->GetResourceAllocationInfo(0,1,&d).SizeInBytes; }
};
struct Material {
    std::array<ComPtr<ID3D12Resource>,4> tex;
    UINT64 finalBytes=0, peakDefaultBytes=0, uploadBytes=0, fileBytes=0;
    std::array<double,3> gpu{}; double wall=0;
};
static void WriteBuffer(ID3D12Resource* r,const void* data,size_t size,UINT64 offset=0) {
    void* p=nullptr; D3D12_RANGE empty{0,0}; Check(r->Map(0,&empty,&p)); memcpy(static_cast<char*>(p)+offset,data,size); r->Unmap(0,nullptr);
}
static ComPtr<ID3D12Resource> Upload(Gpu& g,ID3D12Resource* dest,const std::vector<D3D12_SUBRESOURCE_DATA>& data) {
    UINT64 size=GetRequiredIntermediateSize(dest,0,UINT(data.size())); auto up=g.Buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Require(UpdateSubresources(g.list.Get(),dest,up.Get(),0,0,UINT(data.size()),data.data())!=0,"UpdateSubresources failed"); return up;
}
static Material LoadImages(Gpu& g,const std::string& variant) {
    auto start=Clock::now(); Material m; std::vector<ComPtr<ID3D12Resource>> uploads;
    g.Begin(); g.Stamp(0);
    for(int i=0;i<4;++i) {
        DirectX::ScratchImage image, converted, mips; DXGI_FORMAT format; const DirectX::ScratchImage* source;
        if(variant=="rgba8") {
            fs::path path=fs::path("Resources/Textures/PolyHaven")/(std::string("asphalt_02_")+Suffixes[i]+"_2k.jpg"); m.fileBytes+=fs::file_size(path);
            Check(DirectX::LoadFromWICFile(path.c_str(),DirectX::WIC_FLAGS_NONE,nullptr,image));
            auto src=image.GetImage(0,0,0); if(src->format!=DXGI_FORMAT_R8G8B8A8_UNORM) { Check(DirectX::Convert(*src,DXGI_FORMAT_R8G8B8A8_UNORM,DirectX::TEX_FILTER_DEFAULT,0,converted)); src=converted.GetImage(0,0,0); }
            format=i==0?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
            auto mipSource=*src; mipSource.format=format; Check(DirectX::GenerateMipMaps(mipSource,DirectX::TEX_FILTER_DEFAULT,0,mips)); source=&mips;
        } else {
            auto path=Out/variant/(std::string(Names[i])+".dds"); m.fileBytes+=fs::file_size(path);
            Check(DirectX::LoadFromDDSFile(path.c_str(),DirectX::DDS_FLAGS_NONE,nullptr,image)); source=&image; format=Formats[i];
        }
        auto md=source->GetMetadata(); Require(md.width==2048 && md.height==2048 && md.mipLevels==12 && md.arraySize==1,"Unexpected road texture geometry");
        auto d=CD3DX12_RESOURCE_DESC::Tex2D(format,md.width,UINT(md.height),1,UINT16(md.mipLevels));
        m.tex[i]=g.Resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST); m.finalBytes+=g.Bytes(m.tex[i].Get());
        std::vector<D3D12_SUBRESOURCE_DATA> subs;
        for(size_t level=0;level<md.mipLevels;++level) { auto im=source->GetImage(level,0,0); subs.push_back({im->pixels,LONG_PTR(im->rowPitch),LONG_PTR(im->slicePitch)}); }
        auto up=Upload(g,m.tex[i].Get(),subs); m.uploadBytes+=up->GetDesc().Width; uploads.push_back(up);
        g.Barrier(m.tex[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    g.Stamp(1); g.Stamp(2); g.Stamp(3); m.gpu=g.Finish(); m.wall=Ms(start); m.peakDefaultBytes=m.finalBytes; return m;
}

// Native D3D12 backend: NTC file -> latent/weight upload -> DP4a inference -> BCn compute -> GPU copies.
// No CUDA context, child process, or CPU round-trip of reconstructed pixels is used here.
struct NtcLoader {
    Gpu& g; HMODULE dll=nullptr; ntc::IContext* context=nullptr;
    decltype(&ntc::DestroyContext) destroy=nullptr; decltype(&ntc::GetLastErrorMessage) error=nullptr;
    ComPtr<ID3D12RootSignature> root; std::map<const void*,ComPtr<ID3D12PipelineState>> psos;
    void Ok(ntc::Status s) { if(s!=ntc::Status::Ok) throw std::runtime_error(std::string("NTC: ")+error()); }
    NtcLoader(Gpu& gpu):g(gpu) {
        auto path=fs::absolute("build/ntc/sdk/bin/windows-x64/libntc.dll");
        dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); Require(dll!=nullptr,"Cannot load official libntc.dll");
        auto create=reinterpret_cast<decltype(&ntc::CreateContext)>(GetProcAddress(dll,"CreateContext"));
        destroy=reinterpret_cast<decltype(destroy)>(GetProcAddress(dll,"DestroyContext")); error=reinterpret_cast<decltype(error)>(GetProcAddress(dll,"GetLastErrorMessage"));
        Require(create && destroy && error,"Missing libNTC entry points");
        ntc::ContextParameters p; p.cudaDevice=ntc::DisableCudaDevice; p.graphicsApi=ntc::GraphicsAPI::D3D12; p.d3d12Device=g.dev.Get(); p.enableCooperativeVector=false;
        auto status=create(&context,p); if(status!=ntc::Status::CudaUnavailable) Ok(status); Require(context!=nullptr,"NTC context creation failed");
        // Shared layout for the SDK's inference (space1 UAV array) and BC encoding (space0 u3).
        CD3DX12_DESCRIPTOR_RANGE ranges[3]; ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,1); ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,48,0,1); ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,3);
        CD3DX12_ROOT_PARAMETER params[4]; params[0].InitAsConstantBufferView(0); for(int i=0;i<3;++i) params[i+1].InitAsDescriptorTable(1,&ranges[i]);
        CD3DX12_STATIC_SAMPLER_DESC sampler(3,D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        CD3DX12_ROOT_SIGNATURE_DESC desc(4,params,1,&sampler); ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors)); Check(g.dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    }
    ~NtcLoader() { if(context) destroy(context); if(dll) FreeLibrary(dll); }
    ID3D12PipelineState* Pipeline(const ntc::ComputePassDesc& pass) {
        auto& p=psos[pass.computeShader]; if(!p) { D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature=root.Get(); d.CS={pass.computeShader,pass.computeShaderSize}; Check(g.dev->CreateComputePipelineState(&d,IID_PPV_ARGS(&p))); } return p.Get();
    }
    Material Load(const fs::path& file=Out/"asphalt_02.ntc",bool hybrid=false) {
        auto start=Clock::now(); Material m; m.fileBytes=fs::file_size(file);
        std::vector<uint8_t> bytes(size_t(m.fileBytes)); std::ifstream f(file,std::ios::binary); Require(bool(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Read NTC failed");
        ntc::IStream* stream=nullptr; Ok(context->OpenReadOnlyMemory(bytes.data(),bytes.size(),&stream));
        struct StreamGuard { ntc::IContext* c; ntc::IStream* p; ~StreamGuard(){c->CloseMemory(p);} } sg{context,stream};
        ntc::ITextureSetMetadata* meta=nullptr; Ok(context->CreateTextureSetMetadataFromStream(stream,&meta));
        struct MetaGuard { ntc::IContext* c; ntc::ITextureSetMetadata* p; ~MetaGuard(){c->DestroyTextureSetMetadata(p);} } mg{context,meta};
        const int textureCount=hybrid?3:4;
        auto md=meta->GetDesc(); Require(md.width==2048 && md.height==2048 && md.mips==12 && meta->GetTextureCount()==textureCount,"Unexpected NTC material");
        const std::array<int,4> destination=hybrid?std::array<int,4>{0,2,3,-1}:std::array<int,4>{0,1,2,3};
        for(int i=0;i<textureCount;++i) { std::string name=meta->GetTexture(i)->GetName(); name=name.substr(0,name.find('|')); Require(name==Names[destination[i]],"NTC texture order mismatch"); }
        Require(meta->GetBestSupportedWeightType()==ntc::InferenceWeightType::GenericInt8,"Expected non-CoopVec INT8 inference");
        std::vector<ComPtr<ID3D12Resource>> keep, uploads;
        auto hold=[&](ComPtr<ID3D12Resource> r) { m.peakDefaultBytes+=g.Bytes(r.Get()); keep.push_back(r); return r; };
        g.Begin(); g.Stamp(0);
        if(hybrid) {
            // The original BC5 DDS is copied verbatim: normal is never passed through NTC.
            auto path=Out/"bcn/normal.dds"; DirectX::ScratchImage normal;
            Check(DirectX::LoadFromDDSFile(path.c_str(),DirectX::DDS_FLAGS_NONE,nullptr,normal));
            auto normalMd=normal.GetMetadata(); Require(normalMd.format==DXGI_FORMAT_BC5_UNORM && normalMd.width==2048 && normalMd.height==2048 && normalMd.mipLevels==12,"Invalid fixed BC5 normal");
            m.fileBytes+=fs::file_size(path); m.tex[1]=g.Resource(CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_BC5_UNORM,2048,2048,1,12),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST); m.finalBytes+=g.Bytes(m.tex[1].Get());
            std::vector<D3D12_SUBRESOURCE_DATA> normalSubs;
            for(int mip=0;mip<12;++mip) { auto im=normal.GetImage(mip,0,0); normalSubs.push_back({im->pixels,LONG_PTR(im->rowPitch),LONG_PTR(im->slicePitch)}); }
            uploads.push_back(Upload(g,m.tex[1].Get(),normalSubs)); g.Barrier(m.tex[1].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        auto ld=meta->GetLatentTextureDesc(); auto latent=hold(g.Resource(CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_B4G4R4A4_UNORM,ld.width,ld.height,UINT16(ld.arraySize),UINT16(ld.mipLevels)),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
        std::vector<D3D12_SUBRESOURCE_DATA> subs;
        for(int layer=0;layer<ld.arraySize;++layer) for(int mip=0;mip<ld.mipLevels;++mip) {
            ntc::LatentTextureFootprint fp; Ok(meta->GetLatentTextureFootprint(mip,layer,fp));
            Require(fp.buffer.compressionType==ntc::CompressionType::None,"Run preparation with --gdeflate off");
            Require(fp.buffer.rangeInStream.offset+fp.buffer.rangeInStream.size<=bytes.size(),"Invalid latent footprint");
            subs.push_back({bytes.data()+fp.buffer.rangeInStream.offset,LONG_PTR(fp.rowPitch),LONG_PTR(fp.rowPitch*fp.height)});
        }
        uploads.push_back(Upload(g,latent.Get(),subs)); g.Barrier(latent.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const void* weightData=nullptr; size_t weightSize=0,converted=0; Ok(meta->GetInferenceWeights(ntc::InferenceWeightType::GenericInt8,&weightData,&weightSize,&converted)); Require(!converted,"Unexpected CoopVec weight layout");
        auto weights=hold(g.Buffer(weightSize,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST)); auto wup=g.Buffer(weightSize,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); WriteBuffer(wup.Get(),weightData,weightSize); uploads.push_back(wup);
        g.list->CopyBufferRegion(weights.Get(),0,wup.Get(),0,weightSize); g.Barrier(weights.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // One raw BC7 mode buffer per mip; it is part of the NTC file, not recomputed during loading.
        std::array<ComPtr<ID3D12Resource>,12> modes;
        std::array<size_t,12> modeSizes{};
        for(int mip=0;mip<12;++mip) {
            auto fp=meta->GetTexture(0)->GetBC7ModeBufferFootprint(mip); Require(fp.compressionType==ntc::CompressionType::None && fp.rangeInStream.size>0,"Missing optimized BC7 modes");
            Require(fp.rangeInStream.offset+fp.rangeInStream.size<=bytes.size(),"Invalid BC7 mode footprint"); modeSizes[mip]=size_t(fp.rangeInStream.size);
            modes[mip]=hold(g.Buffer(fp.rangeInStream.size,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST)); auto up=g.Buffer(fp.rangeInStream.size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            WriteBuffer(up.Get(),bytes.data()+fp.rangeInStream.offset,modeSizes[mip]); uploads.push_back(up); g.list->CopyBufferRegion(modes[mip].Get(),0,up.Get(),0,fp.rangeInStream.size); g.Barrier(modes[mip].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,256,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0}; ComPtr<ID3D12DescriptorHeap> heap; Check(g.dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
        UINT inc=g.dev->GetDescriptorHandleIncrementSize(hd.Type);
        auto cpu=[&](UINT i){return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(),i,inc);};
        auto gpu=[&](UINT i){return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(),i,inc);};
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Format=DXGI_FORMAT_B4G4R4A4_UNORM; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY; srv.Texture2DArray.MipLevels=ld.mipLevels; srv.Texture2DArray.ArraySize=ld.arraySize; g.dev->CreateShaderResourceView(latent.Get(),&srv,cpu(0));
        auto rawSrv=[&](ID3D12Resource* r,size_t size,UINT index) { D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Format=DXGI_FORMAT_R32_TYPELESS; d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; d.Buffer.NumElements=UINT((size+3)/4); d.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW; g.dev->CreateShaderResourceView(r,&d,cpu(index)); };
        rawSrv(weights.Get(),weightSize,1);
        std::array<ComPtr<ID3D12Resource>,4> decoded;
        for(int i=0;i<4;++i) {
            if(hybrid && i==1) continue;
            decoded[i]=hold(g.Resource(CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,2048,2048,1,12,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            for(int mip=0;mip<12;++mip) { D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice=mip; g.dev->CreateUnorderedAccessView(decoded[i].Get(),nullptr,&d,cpu(2+mip*4+i)); }
        }
        // Constant data for each dispatch must remain distinct until GPU execution completes.
        auto cb=g.Buffer(60*4096,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); uploads.push_back(cb); UINT cbSlot=0;
        auto dispatch=[&](ntc::ComputePassDesc& pass,UINT srvIndex,UINT bcUav) {
            Require(pass.constantBufferSize<=4096,"NTC constant data exceeds slot"); WriteBuffer(cb.Get(),pass.constantBufferData,pass.constantBufferSize,cbSlot*4096ull);
            g.list->SetComputeRootSignature(root.Get()); g.list->SetPipelineState(Pipeline(pass)); g.list->SetComputeRootConstantBufferView(0,cb->GetGPUVirtualAddress()+cbSlot++*4096ull);
            ID3D12DescriptorHeap* h=heap.Get(); g.list->SetDescriptorHeaps(1,&h); g.list->SetComputeRootDescriptorTable(1,gpu(srvIndex)); g.list->SetComputeRootDescriptorTable(2,gpu(2)); g.list->SetComputeRootDescriptorTable(3,gpu(bcUav)); g.list->Dispatch(pass.dispatchWidth,pass.dispatchHeight,1);
        };
        g.Stamp(1);
        for(int mip=0;mip<12;++mip) {
            std::array<ntc::OutputTextureDesc,4> output;
            for(int i=0;i<textureCount;++i) { output[i].descriptorIndex=destination[i]; meta->GetTexture(i)->GetChannels(output[i].firstChannel,output[i].numChannels); output[i].rgbColorSpace=meta->GetTexture(i)->GetRgbColorSpace(); output[i].alphaColorSpace=meta->GetTexture(i)->GetAlphaColorSpace(); output[i].ditherScale=1.f/255.f; }
            ntc::MakeDecompressionComputePassParameters p; p.textureSetMetadata=meta; p.weightType=ntc::InferenceWeightType::GenericInt8; p.mipLevel=mip; p.firstOutputDescriptorIndex=mip*4; p.pOutputTextures=output.data(); p.numOutputTextures=textureCount;
            ntc::ComputePassDesc pass; Ok(context->MakeDecompressionComputePass(p,&pass)); dispatch(pass,0,250);
        }
        for(auto& t:decoded) if(t) g.Barrier(t.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.Stamp(2);
        UINT descriptor=50;
        for(int i=0;i<4;++i) {
            if(hybrid && i==1) continue;
            auto d=CD3DX12_RESOURCE_DESC::Tex2D(Formats[i],2048,2048,1,12); m.tex[i]=g.Resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST); m.finalBytes+=g.Bytes(m.tex[i].Get());
            std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,12> fps; UINT64 size=0; g.dev->GetCopyableFootprints(&d,0,12,0,fps.data(),nullptr,nullptr,&size);
            auto packed=hold(g.Buffer(size,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST));
            for(int mip=0;mip<12;++mip) {
                UINT w=std::max(1,2048>>mip), h=w; auto blockFmt=i<2?DXGI_FORMAT_R32G32B32A32_UINT:DXGI_FORMAT_R32G32_UINT;
                auto blocks=hold(g.Resource(CD3DX12_RESOURCE_DESC::Tex2D(blockFmt,(w+3)/4,(h+3)/4,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
                D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MostDetailedMip=mip; sd.Texture2D.MipLevels=1; g.dev->CreateShaderResourceView(decoded[i].Get(),&sd,cpu(descriptor));
                rawSrv(modes[mip].Get(),modeSizes[mip],descriptor+1);
                D3D12_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format=blockFmt; ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; g.dev->CreateUnorderedAccessView(blocks.Get(),nullptr,&ud,cpu(descriptor+2));
                ntc::MakeBlockCompressionComputePassParameters p; p.srcRect.width=w; p.srcRect.height=h; p.dstFormat=i==0?ntc::BlockCompressedFormat::BC7:i==1?ntc::BlockCompressedFormat::BC5:ntc::BlockCompressedFormat::BC4;
                if(i==0) { p.modeBufferSource=ntc::BlockCompressionModeBufferSource::TextureSet; p.modeBufferInfo.textureSet.texture=meta->GetTexture(0); p.modeBufferInfo.textureSet.mipLevel=mip; }
                ntc::ComputePassDesc pass; Ok(context->MakeBlockCompressionComputePass(p,&pass)); dispatch(pass,descriptor,descriptor+2); descriptor+=3;
                g.Barrier(blocks.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
                auto rawFp=fps[mip]; rawFp.Footprint.Format=blockFmt; rawFp.Footprint.Width=(w+3)/4; rawFp.Footprint.Height=(h+3)/4;
                auto dst=CD3DX12_TEXTURE_COPY_LOCATION(packed.Get(),rawFp), src=CD3DX12_TEXTURE_COPY_LOCATION(blocks.Get(),0); g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            }
            g.Barrier(packed.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
            for(UINT mip=0;mip<12;++mip) { auto dst=CD3DX12_TEXTURE_COPY_LOCATION(m.tex[i].Get(),mip),src=CD3DX12_TEXTURE_COPY_LOCATION(packed.Get(),fps[mip]); g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); }
            g.Barrier(m.tex[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        g.Stamp(3); m.gpu=g.Finish(); m.wall=Ms(start); m.peakDefaultBytes+=m.finalBytes; for(auto& u:uploads) m.uploadBytes+=u->GetDesc().Width; return m;
    }
};

static void SaveTextures(Gpu& g,const Material& m,const std::string& variant) {
    fs::create_directories(Out/variant);
    for(int i=0;i<4;++i) {
        auto d=m.tex[i]->GetDesc(); const UINT count=d.MipLevels; std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(count); std::vector<UINT> rows(count); std::vector<UINT64> pitches(count); UINT64 total=0;
        g.dev->GetCopyableFootprints(&d,0,count,0,fp.data(),rows.data(),pitches.data(),&total); auto readback=g.Buffer(total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        g.Begin(); g.Barrier(m.tex[i].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        for(UINT mip=0;mip<count;++mip) { auto dst=CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(),fp[mip]),src=CD3DX12_TEXTURE_COPY_LOCATION(m.tex[i].Get(),mip); g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); }
        g.Barrier(m.tex[i].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); g.End();
        DirectX::ScratchImage image; Check(image.Initialize2D(d.Format,size_t(d.Width),d.Height,1,count)); void* data=nullptr; D3D12_RANGE range{0,size_t(total)}; Check(readback->Map(0,&range,&data));
        for(UINT mip=0;mip<count;++mip) { auto im=image.GetImage(mip,0,0); for(UINT row=0;row<rows[mip];++row) memcpy(im->pixels+row*im->rowPitch,static_cast<char*>(data)+fp[mip].Offset+row*fp[mip].Footprint.RowPitch,size_t(pitches[mip])); }
        D3D12_RANGE empty{0,0}; readback->Unmap(0,&empty); auto path=Out/variant/(std::string(Names[i])+".dds"); Check(DirectX::SaveToDDSFile(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,path.c_str()));
    }
}

static DirectX::ScratchImage Pixels(const fs::path& path) {
    DirectX::ScratchImage image, decoded; Check(DirectX::LoadFromDDSFile(path.c_str(),DirectX::DDS_FLAGS_NONE,nullptr,image));
    auto src=*image.GetImage(0,0,0);
    // PSNR is measured on stored 8-bit values, without an implicit sRGB -> linear conversion.
    src.format=DirectX::MakeLinear(src.format);
    if(DirectX::IsCompressed(src.format)) Check(DirectX::Decompress(src,DXGI_FORMAT_R8G8B8A8_UNORM,decoded));
    else Check(decoded.InitializeFromImage(src));
    return decoded;
}
static void Quality(const std::vector<std::string>& variants={"bcn-readback","ntc-native"},const std::string& reportName="quality.json") {
    std::ofstream report(Out/reportName); report<<std::fixed<<std::setprecision(6)<<"[\n"; bool first=true;
    for(const std::string& variant:variants) for(int map=0;map<4;++map) {
        auto reference=Pixels(Out/"rgba8"/(std::string(Names[map])+".dds")); auto decoded=Pixels(Out/variant/(std::string(Names[map])+".dds"));
        auto a=reference.GetImage(0,0,0), b=decoded.GetImage(0,0,0); double squared=0; const int channels=map==0?3:map==1?2:1;
        std::vector<double> angles; if(map==1) angles.reserve(a->width*a->height);
        for(size_t y=0;y<a->height;++y) for(size_t x=0;x<a->width;++x) {
            auto p=a->pixels+y*a->rowPitch+x*4, q=b->pixels+y*b->rowPitch+x*4;
            for(int ch=0;ch<channels;++ch) { double d=(double(p[ch])-q[ch])/255.; squared+=d*d; }
            if(map==1) { double n[3],r[3],len=0; for(int ch=0;ch<3;++ch) { n[ch]=p[ch]/127.5-1.; len+=n[ch]*n[ch]; } r[0]=q[0]/127.5-1.; r[1]=q[1]/127.5-1.; r[2]=std::sqrt(std::max(0.,1-r[0]*r[0]-r[1]*r[1])); double rlen=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]); double dot=(n[0]*r[0]+n[1]*r[1]+n[2]*r[2])/(std::sqrt(len)*rlen); angles.push_back(std::acos(std::clamp(dot,-1.,1.))*180./3.141592653589793); }
        }
        double mse=squared/(a->width*a->height*channels); Require(mse>0 && std::isfinite(mse),"Invalid comparison image");
        Require(-10*std::log10(mse)>(variant=="bcn-readback"?35.:20.),"Road texture fidelity check failed (binding, format or color space mismatch)");
        if(!first) report<<",\n"; first=false;
        report<<"  {\"variant\": \""<<variant<<"\", \"map\": \""<<Names[map]<<"\", \"mip\": 0, \"channels\": "<<channels<<", \"psnrDb\": "<<-10*std::log10(mse);
        if(!angles.empty()) { double sum=0; for(double v:angles) sum+=v; report<<", \"normalMeanDegrees\": "<<sum/angles.size()<<", \"normalP95Degrees\": "<<P95(std::move(angles)); }
        report<<"}";
    }
    report<<"\n]\n";
}

// Identical offscreen material view for all three formats. A synthetic view, not game FPS.
static double Preview(Gpu& g,const Material& m,bool bc5,const std::string& variant) {
    constexpr UINT W=1024,H=640;
    const char* shader=R"(
Texture2D albedo:register(t0); Texture2D normalTex:register(t1); Texture2D roughness:register(t2); Texture2D ao:register(t3);
SamplerState sam:register(s0); cbuffer Params:register(b0){uint bc5;}
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD;};
V VS(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 PS(V i):SV_Target{float2 uv=float2((i.uv.x-.5)*3,1)/(.18+i.uv.y);float3 n=normalTex.Sample(sam,uv).xyz*2-1;
if(bc5)n.z=sqrt(saturate(1-dot(n.xy,n.xy)));n.y=-n.y;n=normalize(n);float r=roughness.Sample(sam,uv).r;float a=ao.Sample(sam,uv).r;
float3 l=normalize(float3(.55,.3,1)),v=normalize(float3(0,.55,.8)),h=normalize(l+v);float diffuse=saturate(dot(n,l));
float spec=pow(saturate(dot(n,h)),lerp(100,3,r))*.16;float3 color=albedo.Sample(sam,uv).rgb*(.25*a+diffuse*1.35)+spec;
return float4(pow(saturate(color),1/2.2),1);})";
    ComPtr<ID3DBlob> vs,ps,errors; Check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors)); Check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors));
    CD3DX12_DESCRIPTOR_RANGE range; range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0); CD3DX12_ROOT_PARAMETER params[2]; params[0].InitAsDescriptorTable(1,&range); params[1].InitAsConstants(1,0);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_ANISOTROPIC); sampler.MaxAnisotropy=8;
    CD3DX12_ROOT_SIGNATURE_DESC rs(2,params,1,&sampler); ComPtr<ID3DBlob> blob; Check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors)); ComPtr<ID3D12RootSignature> root; Check(g.dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()}; pd.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; pd.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT); pd.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); pd.DepthStencilState.DepthEnable=FALSE; pd.DepthStencilState.StencilEnable=FALSE; pd.SampleMask=UINT_MAX; pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets=1; pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; pd.SampleDesc.Count=1; ComPtr<ID3D12PipelineState> pipeline; Check(g.dev->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)));
    auto d=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET); auto target=g.Resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    ComPtr<ID3D12DescriptorHeap> heap,rtvHeap; D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0}; Check(g.dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))); hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0}; Check(g.dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvHeap))); g.dev->CreateRenderTargetView(target.Get(),nullptr,rtvHeap->GetCPUDescriptorHandleForHeapStart());
    UINT inc=g.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(int i=0;i<4;++i) { D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=m.tex[i]->GetDesc().Format; s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels=12; g.dev->CreateShaderResourceView(m.tex[i].Get(),&s,CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(),i,inc)); }
    std::vector<double> draws;
    for(int iteration=-3;iteration<32;++iteration) { g.Begin(); auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart(); g.list->OMSetRenderTargets(1,&rtv,FALSE,nullptr); D3D12_VIEWPORT viewport{0,0,float(W),float(H),0,1}; D3D12_RECT scissor{0,0,W,H}; g.list->RSSetViewports(1,&viewport); g.list->RSSetScissorRects(1,&scissor); g.list->SetPipelineState(pipeline.Get()); g.list->SetGraphicsRootSignature(root.Get()); auto h=heap.Get(); g.list->SetDescriptorHeaps(1,&h); g.list->SetGraphicsRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart()); g.list->SetGraphicsRoot32BitConstant(1,bc5?1:0,0); g.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g.Stamp(0); for(int draw=0;draw<(iteration<0?1000:1);++draw) g.list->DrawInstanced(3,1,0,0); g.Stamp(1); g.Stamp(2); g.Stamp(3); auto timings=g.Finish(); if(iteration>=0)draws.push_back(timings[0]); }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 size=0; g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size); auto readback=g.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST); g.Begin(); g.Barrier(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE); auto dst=CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(),fp), src=CD3DX12_TEXTURE_COPY_LOCATION(target.Get(),0); g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); g.End(); void* data=nullptr; D3D12_RANGE readRange{0,size_t(size)}; Check(readback->Map(0,&readRange,&data)); DirectX::Image image{W,H,DXGI_FORMAT_R8G8B8A8_UNORM,fp.Footprint.RowPitch,size_t(size),static_cast<uint8_t*>(data)}; Check(DirectX::SaveToWICFile(image,DirectX::WIC_FLAGS_NONE,GUID_ContainerFormatPng,(Out/(variant+"-preview.png")).c_str())); D3D12_RANGE empty{0,0}; readback->Unmap(0,&empty); return Median(draws);
}

int main(int argc,char** argv) {
    try {
        Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); bool debug=false,hybrid=false;
        for(int i=1;i<argc;++i) { const std::string arg=argv[i]; if(arg=="--debug")debug=true; else if(arg=="--hybrid-sweep")hybrid=true; else throw std::runtime_error("Unknown argument"); }
        Gpu g(debug); NtcLoader ntc(g);
        const std::vector<std::string> variants=hybrid?std::vector<std::string>{"bcn","hybrid-3","hybrid-5","hybrid-8","hybrid-12","hybrid-16"}:std::vector<std::string>{"rgba8","bcn","ntc-native"};
        const std::string reportName=std::string(hybrid?"hybrid-gpu":"gpu")+(debug?"-debug.json":".json");
        std::ofstream report(Out/reportName); report<<std::fixed<<std::setprecision(6)<<"{\n  \"adapter\": \""<<g.name<<"\",\n  \"debugLayer\": "<<(debug?"true":"false")<<",\n  \"driverVersionRaw\": "<<g.driver<<",\n  \"timestampFrequency\": "<<g.frequency<<",\n  \"warmups\": 3, \"samples\": 32,\n  \"variants\": [\n";
        int variantIndex=0;
        for(const std::string& variant:variants) {
            std::vector<double> walls,total,upload,inference,encode; Material last; double firstWall=0;
            for(int iteration=-4;iteration<32;++iteration) {
                last=Material{};
                Material m=variant=="ntc-native"?ntc.Load():variant.rfind("hybrid-",0)==0?ntc.Load(Out/variant/"material.ntc",true):LoadImages(g,variant);
                if(iteration==-4) { firstWall=m.wall; SaveTextures(g,m,variant=="bcn"?"bcn-readback":variant); }
                if(iteration>=0) { walls.push_back(m.wall); upload.push_back(m.gpu[0]); inference.push_back(m.gpu[1]); encode.push_back(m.gpu[2]); total.push_back(m.gpu[0]+m.gpu[1]+m.gpu[2]); }
                last=std::move(m);
            }
            double draw=Preview(g,last,variant!="rgba8",variant);
            if(variantIndex++) report<<",\n";
            report<<"    {\"name\": \""<<variant<<"\", \"fileBytes\": "<<last.fileBytes<<", \"finalGpuAllocationBytes\": "<<last.finalBytes<<", \"peakOwnedDefaultAllocationBytes\": "<<last.peakDefaultBytes<<", \"uploadHeapBytes\": "<<last.uploadBytes<<", \"firstLoadWallMs\": "<<firstWall<<", \"warmLoadWallMedianMs\": "<<Median(walls)<<", \"warmLoadWallP95Ms\": "<<P95(walls)<<", \"gpuTotalMedianMs\": "<<Median(total)<<", \"gpuTotalP95Ms\": "<<P95(total)<<", \"gpuUploadMedianMs\": "<<Median(upload)<<", \"gpuInferenceMedianMs\": "<<Median(inference)<<", \"gpuEncodeAndCopyMedianMs\": "<<Median(encode)<<", \"syntheticDrawMedianMs\": "<<draw<<"}";
            std::cout<<variant<<": wall "<<Median(walls)<<" ms; GPU "<<Median(total)<<" ms; final "<<last.finalBytes<<" bytes; peak "<<last.peakDefaultBytes<<" bytes\n";
        }
        report<<"\n  ]\n}\n"; report.close();
        if(hybrid) { auto qualityVariants=variants; qualityVariants[0]="bcn-readback"; Quality(qualityVariants,"hybrid-quality.json"); }
        else Quality();
        CoUninitialize(); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
