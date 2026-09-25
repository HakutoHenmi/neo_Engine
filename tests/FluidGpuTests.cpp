// Headless SM5 regression tests. Executes the production HLSL on D3D11 WARP;
// tests numerical kernels independently of game content and the D3D12 renderer.
#define NOMINMAX
#include "../Engine/FluidEmission.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <wrl.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;
void Check(HRESULT h) { if(FAILED(h)) { char msg[64]; sprintf_s(msg,"HRESULT 0x%08x",(unsigned)h); throw std::runtime_error(msg); } }
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Particle { XMFLOAT3 position; float density; XMFLOAT3 velocity; float pressure; XMFLOAT4 color; float type; XMFLOAT3 pad; };
struct Shape { XMFLOAT4 r[3]; XMFLOAT4 info; };
struct VolumeCB { XMFLOAT3 origin; float cell; UINT size[3],count; XMFLOAT3 previous; float history; float dt,iso,axis,debug; XMFLOAT4 colors[3]; };
static_assert(sizeof(Particle)==64 && sizeof(Shape)==64 && sizeof(VolumeCB)==112);
struct Buffer { ComPtr<ID3D11Buffer> resource; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
struct Texture { ComPtr<ID3D11Texture3D> resource; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; UINT size; };
class Gpu {
public:
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11SamplerState> sampler;
    std::map<std::string,ComPtr<ID3D11ComputeShader>> kernels;
    Gpu() {
        D3D_FEATURE_LEVEL feature;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context));
        D3D11_SAMPLER_DESC desc{}; desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; desc.MaxLOD=D3D11_FLOAT32_MAX;
        Check(device->CreateSamplerState(&desc,&sampler));
        auto s=sampler.Get(); context->CSSetSamplers(0,1,&s); context->PSSetSamplers(0,1,&s);
    }
    ComPtr<ID3DBlob> Compile(const wchar_t* file,const char* entry,const char* target) {
        ComPtr<ID3DBlob> code,error;
        HRESULT h=D3DCompileFromFile(file,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(h) && error) fprintf(stderr,"%s\n",(const char*)error->GetBufferPointer());
        Check(h); return code;
    }
    void Kernel(const char* entry,bool simulation=false) {
        auto code=Compile(simulation?L"Resources/shaders/FluidSimCS.hlsl":L"Resources/shaders/FluidVolumeCS.hlsl",entry,"cs_5_0");
        Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&kernels[entry]));
    }
    Buffer MakeBuffer(UINT count,UINT stride,const void* data=nullptr) {
        Buffer result;
        D3D11_BUFFER_DESC d{}; d.ByteWidth=count*stride; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride=stride;
        D3D11_SUBRESOURCE_DATA initial{data,0,0};
        Check(device->CreateBuffer(&d,data?&initial:nullptr,&result.resource));
        Check(device->CreateShaderResourceView(result.resource.Get(),nullptr,&result.srv));
        Check(device->CreateUnorderedAccessView(result.resource.Get(),nullptr,&result.uav));
        return result;
    }
    Texture MakeTexture(UINT size) {
        Texture t; t.size=size;
        D3D11_TEXTURE3D_DESC d{}; d.Width=d.Height=d.Depth=size; d.MipLevels=1; d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        Check(device->CreateTexture3D(&d,nullptr,&t.resource));
        Check(device->CreateShaderResourceView(t.resource.Get(),nullptr,&t.srv));
        Check(device->CreateUnorderedAccessView(t.resource.Get(),nullptr,&t.uav)); return t;
    }
    ComPtr<ID3D11Buffer> Constant(UINT bytes) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth=(bytes+15)&~15U; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> b; Check(device->CreateBuffer(&d,nullptr,&b)); return b;
    }
    void Unbind() {
        ID3D11UnorderedAccessView* uavs[8]{}; context->CSSetUnorderedAccessViews(0,8,uavs,nullptr);
        ID3D11ShaderResourceView* srvs[8]{}; context->CSSetShaderResources(0,8,srvs); context->PSSetShaderResources(0,8,srvs); context->VSSetShaderResources(0,8,srvs);
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void Dispatch(const char* name,UINT x,UINT y=1,UINT z=1) { context->CSSetShader(kernels.at(name).Get(),nullptr,0); context->Dispatch(x,y,z); }
    template<class T> std::vector<T> Read(Buffer& b) {
        Unbind(); D3D11_BUFFER_DESC d; b.resource->GetDesc(&d);
        d.BindFlags=d.MiscFlags=d.StructureByteStride=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging; Check(device->CreateBuffer(&d,nullptr,&staging));
        context->CopyResource(staging.Get(),b.resource.Get()); D3D11_MAPPED_SUBRESOURCE m{};
        Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
        std::vector<T> result(d.ByteWidth/sizeof(T)); memcpy(result.data(),m.pData,d.ByteWidth); context->Unmap(staging.Get(),0); return result;
    }
    std::vector<XMFLOAT4> Read(Texture& t) {
        Unbind(); D3D11_TEXTURE3D_DESC d; t.resource->GetDesc(&d);
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture3D> staging; Check(device->CreateTexture3D(&d,nullptr,&staging));
        context->CopyResource(staging.Get(),t.resource.Get()); D3D11_MAPPED_SUBRESOURCE m{};
        Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
        std::vector<XMFLOAT4> result(t.size*t.size*t.size);
        for(UINT z=0;z<t.size;++z) for(UINT y=0;y<t.size;++y) {
            auto row=(const HALF*)((const char*)m.pData+z*m.DepthPitch+y*m.RowPitch);
            for(UINT x=0;x<t.size;++x) result[(z*t.size+y)*t.size+x]=XMFLOAT4(XMConvertHalfToFloat(row[x*4]),XMConvertHalfToFloat(row[x*4+1]),XMConvertHalfToFloat(row[x*4+2]),XMConvertHalfToFloat(row[x*4+3]));
        }
        context->Unmap(staging.Get(),0); return result;
    }
};
UINT Hash(const XMFLOAT3& p) {
    return ((UINT)(int)floor(p.x/.4f)*73856093U ^ (UINT)(int)floor(p.y/.4f)*19349663U ^ (UINT)(int)floor(p.z/.4f)*83492791U)&65535U;
}
Particle MakeParticle(float x,float y,float z,float type) { return {{x,y,z},type==1?125.0f:275.0f,{0,0,0},0,{type==0?0.05f:0.1f,0.8f,type==0?0.05f:1.0f,1},type,{0,0,0}}; }

void TestPbf(Gpu& g) {
    const char* entries[]={"ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","SortParticlesVelocity","CalcDensity","CalcForce","SavePrevious","CalcDeltaP","ApplyDeltaP","UpdateVelocity","WriteBack"};
    for(auto e:entries) g.Kernel(e,true);
    std::vector<Particle> particles;
    for(int z=0;z<5;++z) for(int y=0;y<5;++y) for(int x=0;x<5;++x) particles.push_back(MakeParticle(x*.12f,2+y*.12f,z*.12f,1));
    particles.push_back(MakeParticle(4,2,0,1));
    UINT count=(UINT)particles.size(), groups=(count+63)/64;
    auto p=g.MakeBuffer(count,64,particles.data()), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4), sorted=g.MakeBuffer(count,64), indices=g.MakeBuffer(count,4), previous=g.MakeBuffer(count,16), scratch=g.MakeBuffer(count,64);
    XMFLOAT4 dummy[2]{}; auto aabb=g.MakeBuffer(1,32,dummy);
    std::array<UINT,48> cb{}; float dt=1.0f/300; memcpy(&cb[0],&dt,4); cb[3]=count; cb[20]=count;
    auto constants=g.Constant(192); g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    auto bind=[&]() {
        g.Unbind(); auto c=constants.Get(); g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr); auto a=aabb.srv.Get(); g.context->CSSetShaderResources(0,1,&a);
    };
    auto grid=[&](bool prepareVelocity=false) { g.Dispatch("ClearOriginalIndices",groups); g.Dispatch("ClearGridCount",1024); g.Dispatch("CountParticles",groups); g.Dispatch("PrefixSum",1); g.Dispatch(prepareVelocity?"SortParticlesVelocity":"SortParticles",groups); };
    bind(); g.Dispatch("SavePrevious",groups); grid(); g.Dispatch("CalcDensity",groups);
    auto initial=g.Read<Particle>(sorted);
    double before=0; bool isolated=false;
    for(auto& v:initial) { before+=std::max(0.0f,v.density/125-1); if(v.position.x>3) { isolated=true; Require(std::abs(v.density-24.4794f)<.01f && v.pressure==0,"isolated particle is incorrectly pressurized"); } }
    Require(isolated,"isolated particle lost");
    for(int i=0;i<3;++i) { bind(); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcDeltaP",groups); g.Dispatch("ApplyDeltaP",groups); }
    bind(); grid(true); g.Dispatch("CalcDensity",groups);
    auto after=g.Read<Particle>(sorted); double error=0;
    for(auto& v:after) { Require(std::isfinite(v.position.x)&&v.position.y>=.2f,"non-finite or penetrating PBF particle"); error+=std::max(0.0f,v.density/125-1); }
    Require(error<before*.6,"PBF density constraints did not converge");
    bind(); g.Dispatch("UpdateVelocity",groups); g.Dispatch("WriteBack",groups);
    auto result=g.Read<Particle>(p);
    for(auto& v:result) Require(std::isfinite(v.velocity.x) && XMVectorGetX(XMVector3Length(XMLoadFloat3(&v.velocity)))<=30.01f,"velocity violated CFL bound");
    printf("PASS PBF: mean compression %.4f -> %.4f; self density 24.4794; bounded finite velocity\n",before/count,error/count);
    // One prediction step touching both the global floor and an AABB top.
    for(UINT i=0;i<count;++i) particles[i]=MakeParticle(i*4.0f,.1f,0,1);
    particles[1].pad.x=4.999f;
    g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,particles.data(),0,0);
    cb[32]=1; g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    bind(); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
    auto contact=g.Read<Particle>(p);
    Require(std::abs(contact[0].pad.x-dt)<1e-5f && contact[0].pad.z>0,"contact age advanced twice or support metadata missing");
    Require(contact[1].position.y<-500 && contact[1].color.w==0,"expired water survived its lifetime");
    puts("PASS contact: one age increment for floor + AABB, support metadata, five-second expiry");
    for(auto& particle:particles) particle=Particle{};
    particles[0]=MakeParticle(0,2,0,0); particles[0].pad={0,1,0};
    particles[1]=MakeParticle(.5f,.5f,0,0); particles[1].pad={.5f,-.5f,0};
    auto setFloat=[&](UINT index,float value){memcpy(&cb[index],&value,4);};
    cb[32]=0; setFloat(17,1); setFloat(19,80);
    setFloat(24,1); setFloat(25,1); setFloat(26,1); setFloat(30,1);
    g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,particles.data(),0,0);
    g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    bind(); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
    auto dome=g.Read<Particle>(p);
    Require(dome[0].velocity.y>=0 && std::abs(dome[0].velocity.x)<1e-4f,
            "player crown is pushed sideways or collapses");
    puts("PASS crown response: upper particle is supported without lateral drift");
    auto predict=[&]() {
        g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,particles.data(),0,0);
        g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
        bind(); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
        return g.Read<Particle>(p);
    };
    setFloat(30,-1);
    auto reversed=predict();
    for(UINT i=0;i<2;++i) {
        Require(std::abs(reversed[i].position.x-dome[i].position.x)<1e-6f &&
                std::abs(reversed[i].position.y-dome[i].position.y)<1e-6f &&
                std::abs(reversed[i].position.z-dome[i].position.z)<1e-6f,
                "heading reversal moved player rest identities");
    }
    // The same airborne body in a uniformly translating frame must have
    // identical deformation acceleration (including vertical jumps).
    setFloat(17,5);
    for(UINT i=0;i<2;++i) particles[i].position.y+=4;
    auto still=predict();
    setFloat(21,4); setFloat(22,6);
    for(UINT i=0;i<2;++i) particles[i].velocity={4,6,0};
    auto moving=predict();
    for(UINT i=0;i<2;++i) {
        Require(std::abs(moving[i].velocity.x-still[i].velocity.x-4)<1e-4f &&
                std::abs(moving[i].velocity.y-still[i].velocity.y-6)<1e-4f,
                "moving controller is damped against world velocity");
    }
    puts("PASS player motion: heading reversal invariance and translating/jumping frame damping");

}

void TestLiquefyRetention(Gpu& g) {
    for(const char* entry:{"ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum",
                           "SortParticles","CalcDensity","CalcForce","WriteBack"}) g.Kernel(entry,true);
    // The real action locks the core and begins with up to 14 m/s of momentum.
    Particle initial=MakeParticle(.5f,.25f,0,0);
    initial.velocity={0,0,14};
    auto p=g.MakeBuffer(1,64,&initial), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4);
    auto sorted=g.MakeBuffer(1,64), indices=g.MakeBuffer(1,4), previous=g.MakeBuffer(1,16), scratch=g.MakeBuffer(1,64);
    XMFLOAT4 dummy[2]{}; auto aabb=g.MakeBuffer(1,32,dummy);
    std::array<UINT,48> cb{};
    auto setFloat=[&](UINT index,float value){memcpy(&cb[index],&value,4);};
    const float dt=1.0f/120.0f;
    setFloat(0,dt); cb[3]=1; cb[20]=1;
    setFloat(17,.5f); setFloat(19,1);
    setFloat(24,4); setFloat(25,.1f); setFloat(26,4);
    setFloat(30,1); setFloat(31,1);
    auto constants=g.Constant(192);
    auto bind=[&]() {
        g.Unbind(); auto c=constants.Get(); g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),
                                         indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr); auto a=aabb.srv.Get(); g.context->CSSetShaderResources(0,1,&a);
    };
    g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    bind();
    g.Dispatch("ClearOriginalIndices",1); g.Dispatch("ClearGridCount",1024);
    g.Dispatch("CountParticles",1); g.Dispatch("PrefixSum",1); g.Dispatch("SortParticles",1);
    // One particle has no neighbours, so its grid does not affect subsequent
    // force steps. This isolates the liquefy controller from density tuning.
    for(UINT step=0;step<180;++step) {
        setFloat(27,14.0f*std::exp(-8.0f*step*dt));
        g.Unbind(); g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
        bind(); g.Dispatch("CalcDensity",1); g.Dispatch("CalcForce",1); g.Dispatch("WriteBack",1);
    }
    auto puddle=g.Read<Particle>(p);
    float drift=std::hypot(puddle[0].position.x,puddle[0].position.z);
    float speed=std::hypot(puddle[0].velocity.x,puddle[0].velocity.z);
    Require(drift<3.25f && speed<.5f,"liquefied player slid away from its anchored core");
    printf("PASS liquefy retention: drift %.3f, planar speed %.3f after 1.5 s\n",drift,speed);
}

void TestReversalSymmetry(Gpu& g) {
    for(const char* entry:{"ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum",
                           "SortParticles","CalcDensity","CalcForce","WriteBack"}) g.Kernel(entry,true);
    Particle initial[2]={MakeParticle(-1.5f,1.0f,0,0),MakeParticle(1.5f,1.0f,0,0)};
    initial[0].velocity={15,0,0}; initial[1].velocity=initial[0].velocity;
    auto p=g.MakeBuffer(2,64,initial), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4);
    auto sorted=g.MakeBuffer(2,64), indices=g.MakeBuffer(2,4), previous=g.MakeBuffer(2,16), scratch=g.MakeBuffer(2,64);
    XMFLOAT4 dummy[2]{}; auto aabb=g.MakeBuffer(1,32,dummy);
    std::array<UINT,48> cb{};
    auto setFloat=[&](UINT index,float value){memcpy(&cb[index],&value,4);};
    setFloat(0,1.0f/120.0f); cb[3]=2; cb[20]=2;
    setFloat(17,1.0f); setFloat(19,32.0f);
    setFloat(24,1.2f); setFloat(25,.9f); setFloat(26,1.2f);
    auto constants=g.Constant(192);
    auto step=[&](float coreVelocity) {
        setFloat(21,coreVelocity);
        g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,initial,0,0);
        g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
        auto c=constants.Get(); g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),
                                         indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr); auto a=aabb.srv.Get(); g.context->CSSetShaderResources(0,1,&a);
        g.Dispatch("ClearOriginalIndices",1); g.Dispatch("ClearGridCount",1024);
        g.Dispatch("CountParticles",1); g.Dispatch("PrefixSum",1); g.Dispatch("SortParticles",1);
        g.Dispatch("CalcDensity",1); g.Dispatch("CalcForce",1); g.Dispatch("WriteBack",1);
        return g.Read<Particle>(p);
    };
    auto still=step(0), reversed=step(-15);
    float left=reversed[0].velocity.x-still[0].velocity.x;
    float right=reversed[1].velocity.x-still[1].velocity.x;
    Require(std::abs(left-right)<.001f,"direction reversal applies different follow on opposite sides");
    printf("PASS reversal symmetry: left %.4f, right %.4f velocity response\n",left,right);
}

void TestStreamEmission(Gpu& g) {
    g.Kernel("Emit",true);
    for(UINT batch: {80U,160U}) {
        UINT count=batch*2, groups=(count+63)/64;
        std::vector<Particle> empty(count);
        auto p=g.MakeBuffer(count,64,empty.data()), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4);
        auto sorted=g.MakeBuffer(count,64), indices=g.MakeBuffer(count,4);
        std::array<UINT,48> cb{};
        auto setFloat=[&](UINT i,float value){memcpy(&cb[i],&value,4);};
        setFloat(0,1.0f/60); cb[2]=batch; cb[3]=count; cb[20]=count;
        setFloat(5,8); setFloat(7,1); setFloat(9,-5); setFloat(15,1);
        auto constants=g.Constant(192);
        auto bind=[&]() {
            g.Unbind(); g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
            auto c=constants.Get(); g.context->CSSetConstantBuffers(0,1,&c);
            ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get()};
            g.context->CSSetUnorderedAccessViews(0,5,u,nullptr);
        };
        bind(); g.Dispatch("Emit",(batch+63)/64);
        auto first=g.Read<Particle>(p);
        for(UINT i=0;i<batch;++i) {
            Require(first[i].velocity.x==0 && first[i].velocity.z==0 && first[i].velocity.y==-25,"stream injected radial velocity");
            first[i].position.y-=25.0f/60;
        }
        g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,first.data(),0,0);
        cb[1]=batch; bind(); g.Dispatch("Emit",(batch+63)/64);
        g.Dispatch("ClearOriginalIndices",groups); g.Dispatch("ClearGridCount",1024);
        g.Dispatch("CountParticles",groups); g.Dispatch("PrefixSum",1); g.Dispatch("SortParticles",groups); g.Dispatch("CalcDensity",groups);
        auto result=g.Read<Particle>(sorted); float peak=0;
        for(auto particle:result) peak=std::max(peak,particle.density);
        Require(peak<125.0f*1.1f,"stream nozzle created excessive compression");
        printf("PASS stream emission: %u/frame, two consecutive slabs, peak density %.3f, no radial velocity\n",batch,peak);
    }
}

std::vector<Particle> TestSettledPlayer(Gpu& g) {
    const UINT count=3000, groups=(count+63)/64;
    std::vector<Particle> initial(count);
    auto p=g.MakeBuffer(count,64,initial.data()), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4);
    auto sorted=g.MakeBuffer(count,64), indices=g.MakeBuffer(count,4), previous=g.MakeBuffer(count,16), scratch=g.MakeBuffer(count,64);
    XMFLOAT4 dummy[2]{}; auto aabb=g.MakeBuffer(1,32,dummy);
    std::array<UINT,48> cb{};
    auto f=[&](UINT i,float value){memcpy(&cb[i],&value,4);};
    f(0,1.0f/300); cb[2]=count; cb[3]=count; cb[20]=count;
    f(15,1); f(17,.55f); f(19,32); f(24,1.2f); f(25,.8f); f(26,1.2f); f(30,1);
    auto constants=g.Constant(192);
    g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    auto bind=[&]() {
        g.Unbind(); auto c=constants.Get(); g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr); auto a=aabb.srv.Get(); g.context->CSSetShaderResources(0,1,&a);
    };
    auto grid=[&](bool prepareVelocity=false) { g.Dispatch("ClearOriginalIndices",groups); g.Dispatch("ClearGridCount",1024); g.Dispatch("CountParticles",groups); g.Dispatch("PrefixSum",1); g.Dispatch(prepareVelocity?"SortParticlesVelocity":"SortParticles",groups); };
    bind(); g.Dispatch("Emit",groups); initial=g.Read<Particle>(p);
    // Start from a diffuse mound; player particles no longer carry rest targets.
    for(auto& particle:initial) {
        particle.color={.1f,.8f,.1f,1};
        particle.position={particle.position.x*.85f,.2f+std::abs(particle.position.y)*.9f,particle.position.z*.85f};
        particle.velocity={0,0,0};
    }
    g.Unbind(); g.context->UpdateSubresource(p.resource.Get(),0,nullptr,initial.data(),0,0);
    bind();
    for(UINT step=0;step<300;++step) {
        g.Dispatch("SavePrevious",groups); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
        for(UINT iteration=0;iteration<3;++iteration) { grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcDeltaP",groups); g.Dispatch("ApplyDeltaP",groups); }
        grid(true); g.Dispatch("UpdateVelocity",groups); g.Dispatch("WriteBack",groups);
    }
    grid(); g.Dispatch("CalcDensity",groups);
    auto result=g.Read<Particle>(sorted);
    float centerTop=0,shoulderTop=0,minX=100,maxX=-100;
    std::vector<float> centerHeights, shoulderHeights;
    for(auto particle:result) {
        Require(std::isfinite(particle.position.y) && particle.position.y>=.199f,"settled player penetrated ground");
        float radial=std::sqrt(particle.position.x*particle.position.x+particle.position.z*particle.position.z);
        if(radial<.7f) { centerTop=std::max(centerTop,particle.position.y); centerHeights.push_back(particle.position.y); }
        if(radial>1.0f && radial<1.4f) { shoulderTop=std::max(shoulderTop,particle.position.y); shoulderHeights.push_back(particle.position.y); }
        minX=std::min(minX,particle.position.x); maxX=std::max(maxX,particle.position.x);
    }
    printf("PLAYER settled: width %.3f, center top %.3f, shoulder top %.3f\n",maxX-minX,centerTop,shoulderTop); fflush(stdout);
    auto percentile=[](std::vector<float>& heights) {
        Require(!heights.empty(),"settled player has an empty crown or shoulder");
        const size_t index=static_cast<size_t>(0.9f*(heights.size()-1));
        std::nth_element(heights.begin(),heights.begin()+index,heights.end());
        return heights[index];
    };
    const float crown90=percentile(centerHeights), shoulder90=percentile(shoulderHeights);
    printf("PLAYER bulk crown: center %.3f, shoulder %.3f\n",crown90,shoulder90); fflush(stdout);
    Require(maxX-minX>2.5f,"settled player collapsed into the core");
    Require(crown90>shoulder90+.15f && centerTop>shoulderTop+.15f && centerTop>1.2f,
            "player developed a depressed crown");
    const float motionSpeed=15.0f;
    const UINT motionSteps=90;
    f(21,motionSpeed);
    for(UINT step=0;step<motionSteps;++step) {
        f(16,motionSpeed*(step+1)/300.0f);
        g.Unbind(); g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
        bind(); g.Dispatch("SavePrevious",groups); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
        for(UINT iteration=0;iteration<3;++iteration) { grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcDeltaP",groups); g.Dispatch("ApplyDeltaP",groups); }
        grid(true); g.Dispatch("UpdateVelocity",groups); g.Dispatch("WriteBack",groups);
    }
    auto moved=g.Read<Particle>(p);
    double meanX=0;
    for(const auto& particle:moved) meanX+=particle.position.x;
    meanX/=count;
    const float coreX=motionSpeed*motionSteps/300.0f;
    printf("PLAYER motion: core %.3f, fluid center %.3f, lag %.3f\n",coreX,meanX,coreX-meanX); fflush(stdout);
    Require(coreX-meanX<1.75f,"player fluid separated too far from a fast moving core");
    f(21,0.0f);
    g.Unbind(); g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
    bind();
    for(UINT step=0;step<240;++step) {
        g.Dispatch("SavePrevious",groups); grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcForce",groups); g.Dispatch("WriteBack",groups);
        for(UINT iteration=0;iteration<3;++iteration) { grid(); g.Dispatch("CalcDensity",groups); g.Dispatch("CalcDeltaP",groups); g.Dispatch("ApplyDeltaP",groups); }
        grid(true); g.Dispatch("UpdateVelocity",groups); g.Dispatch("WriteBack",groups);
    }
    auto recovered=g.Read<Particle>(p);
    double recoveredX=0, recoveredVx=0;
    for(const auto& particle:recovered) { recoveredX+=particle.position.x; recoveredVx+=particle.velocity.x; }
    recoveredX/=count;
    recoveredVx/=count;
    printf("PLAYER recovery: core %.3f, fluid center %.3f, mean vx %.3f\n",coreX,recoveredX,recoveredVx); fflush(stdout);
    Require(std::abs(coreX-recoveredX)<0.7f,"player fluid did not regroup after stopping");
    return result;
}

void WritePng(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,const wchar_t* path) {
    D3D11_TEXTURE2D_DESC d; source->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> stage; Check(device->CreateTexture2D(&d,nullptr,&stage)); context->CopyResource(stage.Get(),source);
    D3D11_MAPPED_SUBRESOURCE m{}; Check(context->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));
    ComPtr<IWICImagingFactory> factory; Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream; Check(factory->CreateStream(&stream)); Check(stream->InitializeFromFilename(path,GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder; Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)); Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; Check(encoder->CreateNewFrame(&frame,nullptr)); Check(frame->Initialize(nullptr)); Check(frame->SetSize(d.Width,d.Height));
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA; Check(frame->SetPixelFormat(&format));
    Check(frame->WritePixels(d.Height,m.RowPitch,m.RowPitch*d.Height,(BYTE*)m.pData)); Check(frame->Commit()); Check(encoder->Commit()); context->Unmap(stage.Get(),0);
}

void TestVolume(Gpu& g, int waterLayers=2, float waterOffset=0, bool draw=true, float spacing=.2f, bool expandedSlime=false, const std::vector<Particle>* player=nullptr, float voxelSize=.25f) {
    const UINT size=48;
    std::vector<Particle> p;
    int extent=static_cast<int>(2.0f/spacing);
    for(int z=-extent;z<=extent;++z) for(int x=-extent;x<=extent;++x) for(int y=0;y<waterLayers;++y) {
        auto particle=MakeParticle(x*spacing,.25f+y*.2f+waterOffset,z*spacing,1);
        if(spacing>=.4f) particle.density=24.4794f;
        p.push_back(particle);
    }
    float slimeStep=expandedSlime ? .25f : .15f;
    for(int z=-5;z<=5 && !player;++z) for(int y=-5;y<=5;++y) for(int x=-5;x<=5;++x) if(x*x+y*y+z*z<26) {
        auto particle=MakeParticle(x*slimeStep,1.5f+y*slimeStep,z*slimeStep,0);
        if(expandedSlime) particle.density=64.0f;
        p.push_back(particle);
    }
    if(player) p.insert(p.end(),player->begin(),player->end());
    p.push_back(MakeParticle(4,1,0,1));
    std::sort(p.begin(),p.end(),[](const Particle& a,const Particle& b){return Hash(a.position)<Hash(b.position);});
    std::vector<UINT> counts(65536),offsets(65536),indices(p.size());
    UINT sparseIndex=0;
    for(UINT i=0;i<p.size();++i) { ++counts[Hash(p[i].position)]; indices[i]=i; if(p[i].type==1 && p[i].position.x>3) sparseIndex=i; }
    for(UINT i=1;i<65536;++i) offsets[i]=offsets[i-1]+counts[i-1];
    auto particles=g.MakeBuffer((UINT)p.size(),64,p.data()), original=g.MakeBuffer((UINT)p.size(),4,indices.data()), gridCount=g.MakeBuffer(65536,4,counts.data()), gridOffset=g.MakeBuffer(65536,4,offsets.data());
    auto accum=g.MakeBuffer(size*size*size,16),momentum=g.MakeBuffer(size*size*size,16),shapes=g.MakeBuffer((UINT)p.size(),64);
    Texture textures[]={g.MakeTexture(size),g.MakeTexture(size),g.MakeTexture(size),g.MakeTexture(size),g.MakeTexture(size),g.MakeTexture(size/4)};
    const char* entries[]={"ClearVolume","BuildShapes","SplatDensity","ResolveDensity","SmoothDensity","TemporalDensity","BuildOccupancy"};
    for(auto entry:entries) g.Kernel(entry);
    VolumeCB cb{{-6,-2,-6},voxelSize,{size,size,size},(UINT)p.size(),{-6,-2,-6},0,1.0f/60,.25f,0,0,{{.05f,.8f,.05f,1},{1,.9f,.1f,1},{.1f,.65f,1,1}}};
    auto constants=g.Constant(sizeof(cb));
    auto bind=[&](UINT input,UINT output) {
        g.Unbind(); g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0); auto c=constants.Get(); g.context->CSSetConstantBuffers(1,1,&c);
        ID3D11ShaderResourceView* s[]={particles.srv.Get(),original.srv.Get(),gridCount.srv.Get(),gridOffset.srv.Get(),textures[input].srv.Get(),textures[2].srv.Get(),textures[4].srv.Get()};
        // Resolve writes velocity; binding it as SRV at the same time is invalid.
        if(output==0 && input==1) s[6]=nullptr;
        g.context->CSSetShaderResources(0,7,s);
        ID3D11UnorderedAccessView* u[]={accum.uav.Get(),momentum.uav.Get(),shapes.uav.Get(),textures[output].uav.Get(),nullptr};
        g.context->CSSetUnorderedAccessViews(0,5,u,nullptr);
    };
    bind(1,0); g.Dispatch("ClearVolume",size/4,size/4,size/4); g.Dispatch("BuildShapes",(cb.count+63)/64); g.Dispatch("SplatDensity",(cb.count+63)/64);
    auto shapeData=g.Read<Shape>(shapes);
    Require(shapeData[sparseIndex].info.x==0 && shapeData[sparseIndex].info.z>.04f && shapeData[sparseIndex].info.z<.05f,"isolated water did not become a small spray");
    for(UINT i=0;i<p.size();++i) if(p[i].type==1 && p[i].position.y<=.4f)
        Require(shapeData[i].info.x>0 && shapeData[i].info.z==0,"contact film was classified as spray");
    auto resolve=[&]() { bind(1,0); auto velocity=textures[4].uav.Get(); g.context->CSSetUnorderedAccessViews(4,1,&velocity,nullptr); g.Dispatch("ResolveDensity",size/4,size/4,size/4); };
    resolve();
    auto raw=g.Read(textures[0]); double rawSum=0; for(auto v:raw) rawSum+=v.x+v.z;
    for(UINT axis=0;axis<3;++axis) { cb.axis=(float)axis; bind(axis%2,1-axis%2); g.Dispatch("SmoothDensity",size/4,size/4,size/4); }
    bind(1,3); g.Dispatch("TemporalDensity",size/4,size/4,size/4);
    auto density=g.Read(textures[3]); double smoothSum=0; float maxGreen=0,maxWater=0;
    for(auto v:density) { Require(std::isfinite(v.x)&&v.x>=0&&v.z>=0,"invalid density"); Require(v.y==0,"material phases contaminated"); smoothSum+=v.x+v.z; maxGreen=std::max(maxGreen,v.x); maxWater=std::max(maxWater,v.z); }
    Require(maxGreen>cb.iso && maxWater>cb.iso,"a bulk phase disappeared");
    if(expandedSlime) {
        float outerDensity=0;
        for(UINT z=23;z<=24;++z) for(UINT y=13;y<=14;++y)
            outerDensity=std::max(outerDensity,density[(z*size+y)*size+27].x);
        Require(outerDensity>cb.iso,"expanded player lost its outer body");
    }
    float minimumColumnPeak=1000;
    const UINT columnBegin=static_cast<UINT>(std::ceil((6.0f-.45f)/voxelSize));
    const UINT columnEnd=static_cast<UINT>(std::floor((6.0f+.45f)/voxelSize))+1;
    for(UINT z=columnBegin;z<columnEnd;++z) for(UINT x=columnBegin;x<columnEnd;++x) {
        float peak=0;
        for(UINT y=0;y<size;++y) peak=std::max(peak,density[(z*size+y)*size+x].z);
        minimumColumnPeak=std::min(minimumColumnPeak,peak);
    }
    Require(minimumColumnPeak>cb.iso*1.15f,"thin sheet has holes or insufficient isosurface margin");
    Require(std::abs(rawSum-smoothSum)<rawSum*.005,"spatial filter changed total density mass");
    bind(3,5); g.Dispatch("BuildOccupancy",size/16,size/16,size/16);
    auto occupancy=g.Read(textures[5]);
    for(UINT z=0;z<size;++z) for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
        auto v=density[(z*size+y)*size+x]; auto o=occupancy[((z/4)*(size/4)+y/4)*(size/4)+x/4];
        Require(o.x>=v.x && o.z>=v.z,"empty-space acceleration is not conservative");
    }
    printf("PASS volume (%d layers, spacing %.2f, cell %.2f, y offset %.4f, expanded slime %d): water peak %.3f, slime peak %.3f; filter mass error %.5f%%; separate spray; conservative occupancy\n",waterLayers,spacing,voxelSize,waterOffset,expandedSlime,maxWater,maxGreen,100*std::abs(rawSum-smoothSum)/rawSum);
    printf("  minimum interior column peak %.3f (iso %.2f)\n",minimumColumnPeak,cb.iso);

    // Offscreen render using the production perspective raymarch shader.
    if(draw) {
    const UINT w=480,h=320;
    D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=td.ArraySize=1; td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> color,background,depth,surface;
    Check(g.device->CreateTexture2D(&td,nullptr,&color));
    std::vector<UINT> checker(w*h); for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) checker[y*w+x]=((x/24+y/24)%2)?0xffb0a080:0xff806040;
    D3D11_SUBRESOURCE_DATA data{checker.data(),w*4,w*h*4}; Check(g.device->CreateTexture2D(&td,&data,&background));
    td.Format=DXGI_FORMAT_R32_FLOAT; std::vector<float> depths(w*h,1); data={depths.data(),w*4,w*h*4}; Check(g.device->CreateTexture2D(&td,&data,&depth)); Check(g.device->CreateTexture2D(&td,nullptr,&surface));
    ComPtr<ID3D11RenderTargetView> colorRtv,depthRtv;
    Check(g.device->CreateRenderTargetView(color.Get(),nullptr,&colorRtv)); Check(g.device->CreateRenderTargetView(surface.Get(),nullptr,&depthRtv));
    ComPtr<ID3D11ShaderResourceView> bgSrv,depthSrv;
    Check(g.device->CreateShaderResourceView(background.Get(),nullptr,&bgSrv)); Check(g.device->CreateShaderResourceView(depth.Get(),nullptr,&depthSrv));
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","RaymarchPS","ps_5_0");
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)); Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    struct Camera { XMFLOAT4X4 view,proj,vp; XMFLOAT3 pos; float time; } camera{};
    camera.pos={4,3,-6};
    auto view=XMMatrixLookAtLH(XMLoadFloat3(&camera.pos),XMVectorSet(0,.5f,0,1),XMVectorSet(0,1,0,0));
    auto proj=XMMatrixPerspectiveFovLH(XM_PIDIV4,(float)w/h,.1f,100);
    XMStoreFloat4x4(&camera.view,view); XMStoreFloat4x4(&camera.proj,proj); XMStoreFloat4x4(&camera.vp,view*proj);
    auto cameraCb=g.Constant(sizeof(camera)); g.context->UpdateSubresource(cameraCb.Get(),0,nullptr,&camera,0,0);
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs; Check(g.device->CreateRasterizerState(&rd,&rs));
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=FALSE; ComPtr<ID3D11DepthStencilState> ds; Check(g.device->CreateDepthStencilState(&dd,&ds));
    auto render=[&](float mode,const wchar_t* name) {
        g.Unbind(); cb.debug=mode; g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);
        ID3D11Buffer* c[]={cameraCb.Get(),constants.Get()}; g.context->VSSetConstantBuffers(0,2,c); g.context->PSSetConstantBuffers(0,2,c);
        ID3D11ShaderResourceView* s[]={textures[3].srv.Get(),bgSrv.Get(),depthSrv.Get(),nullptr,nullptr,nullptr,nullptr,textures[5].srv.Get()}; g.context->PSSetShaderResources(0,8,s);
        ID3D11RenderTargetView* targets[]={colorRtv.Get(),depthRtv.Get()}; g.context->OMSetRenderTargets(2,targets,nullptr);
        g.context->OMSetDepthStencilState(ds.Get(),0); g.context->RSSetState(rs.Get()); D3D11_VIEWPORT vp{0,0,(float)w,(float)h,0,1}; g.context->RSSetViewports(1,&vp);
        g.context->VSSetShader(vs.Get(),nullptr,0); g.context->PSSetShader(ps.Get(),nullptr,0); g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g.context->Draw(3,0);
        g.Unbind(); WritePng(g.device.Get(),g.context.Get(),color.Get(),name);
    };
    render(0,L"tests/out/fluid-material.png"); render(2,L"tests/out/fluid-normals.png"); render(3,L"tests/out/fluid-thickness.png");
    auto visiblePixels=[&]() {
        D3D11_TEXTURE2D_DESC desc; surface->GetDesc(&desc); desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage; Check(g.device->CreateTexture2D(&desc,nullptr,&stage)); g.context->CopyResource(stage.Get(),surface.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(g.context->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));
        UINT visible=0;
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) {
            float z=((float*)((char*)mapped.pData+y*mapped.RowPitch))[x];
            Require(std::isfinite(z) && z>=0,"invalid reconstructed view depth");
            if(z>0) ++visible;
        }
        g.context->Unmap(stage.Get(),0); return visible;
    };
    Require(visiblePixels()>1000,"raymarch did not render fluid surfaces");
    // Put an opaque plane on the near clip plane. No liquid may leak through.
    std::fill(depths.begin(),depths.end(),0.0f);
    g.context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),w*4,w*h*4);
    render(0,L"tests/out/fluid-occluded.png");
    Require(visiblePixels()==0,"fluid ignored opaque scene depth");
    puts("PASS raymarch: finite surface depths, visible bulk, opaque foreground occlusion");
    // Move the near grid away: live bulk must still render via the distant LOD.
    cb.origin={50,-2,50};
    g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);
    auto farVsCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayVS","vs_5_0");
    auto farPsCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayPS","ps_5_0");
    ComPtr<ID3D11VertexShader> farVs; ComPtr<ID3D11PixelShader> farPs;
    Check(g.device->CreateVertexShader(farVsCode->GetBufferPointer(),farVsCode->GetBufferSize(),nullptr,&farVs));
    Check(g.device->CreatePixelShader(farPsCode->GetBufferPointer(),farPsCode->GetBufferSize(),nullptr,&farPs));
    auto farDraw=[&](float opaqueDepth) {
        g.Unbind(); std::fill(depths.begin(),depths.end(),opaqueDepth);
        g.context->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),w*4,w*h*4);
        float clear[4]={}; g.context->ClearRenderTargetView(colorRtv.Get(),clear);
        auto target=colorRtv.Get(); g.context->OMSetRenderTargets(1,&target,nullptr);
        ID3D11ShaderResourceView* inputs[]={nullptr,bgSrv.Get(),depthSrv.Get(),nullptr,shapes.srv.Get(),particles.srv.Get(),nullptr};
        g.context->VSSetShaderResources(0,7,inputs); g.context->PSSetShaderResources(0,7,inputs);
        g.context->VSSetShader(farVs.Get(),nullptr,0); g.context->PSSetShader(farPs.Get(),nullptr,0);
        g.context->DrawInstanced(6,cb.count,0,0); g.Unbind();
        D3D11_TEXTURE2D_DESC desc; color->GetDesc(&desc); desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage; Check(g.device->CreateTexture2D(&desc,nullptr,&stage)); g.context->CopyResource(stage.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; Check(g.context->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));
        UINT pixels=0;
        for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) if(((BYTE*)mapped.pData)[y*mapped.RowPitch+x*4+3]) ++pixels;
        g.context->Unmap(stage.Get(),0); return pixels;
    };
    Require(farDraw(1)>1000,"bulk disappeared outside the near volume");
    WritePng(g.device.Get(),g.context.Get(),color.Get(),L"tests/out/fluid-distant.png");
    Require(farDraw(0)==0,"distant bulk ignored opaque foreground");
    cb.origin={-6,-2,-6};
    puts("PASS distant LOD: live bulk outside near grid, opaque foreground occlusion");
    }
    // A current zero field must not resurrect yesterday's fluid.
    g.context->CopyResource(textures[2].resource.Get(),textures[3].resource.Get());
    bind(1,0); g.Dispatch("ClearVolume",size/4,size/4,size/4); resolve();
    cb.history=1; bind(0,3); g.Dispatch("TemporalDensity",size/4,size/4,size/4);
    auto vanished=g.Read(textures[3]); for(auto v:vanished) Require(v.x==0&&v.y==0&&v.z==0,"temporal ghost after clear");
    printf("PASS temporal: zero-support rejection\n");
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::strcmp(argv[1],"--liquefy-only")==0) {
            Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); Gpu gpu;
            TestLiquefyRetention(gpu); return 0;
        }
        if(argc>1 && std::strcmp(argv[1],"--reversal-only")==0) {
            Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); Gpu gpu;
            TestReversalSymmetry(gpu); return 0;
        }
        if(argc>1 && std::strcmp(argv[1],"--volume-only")==0) {
            Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); Gpu gpu;
            TestVolume(gpu);
            TestVolume(gpu,1,0,true,.4f,false,nullptr,.4f);
            TestVolume(gpu,1,0,true,.4f,false,nullptr,.56f);
            return 0;
        }
        for(int hz: {30,60,120}) {
            float remainder=0; int emitted=0;
            for(int frame=0;frame<hz;++frame) emitted+=Engine::AccumulateFluidEmission(40,1.0f/hz,remainder);
            Require(std::abs(emitted-2400)<=1,"emission depends on render FPS");
        }
        Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); Gpu gpu; TestPbf(gpu); TestLiquefyRetention(gpu); TestReversalSymmetry(gpu); TestStreamEmission(gpu); TestVolume(gpu);
        TestVolume(gpu,1,0,false,.4f);
        TestVolume(gpu,1,0,false,.6f);
        TestVolume(gpu,1,0,true,.6f,true);
        for(int offset=0;offset<4;++offset) TestVolume(gpu,1,offset*.0625f,false);
        auto player=TestSettledPlayer(gpu);
        TestVolume(gpu,1,0,true,.4f,false,&player);
        puts("ALL FLUID GPU TESTS PASSED (WARP, production HLSL)"); return 0;
    }
    catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
