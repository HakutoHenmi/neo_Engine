#include "../Engine/FluidTank.h"

void TestFluidSandbox(Gpu& g) {
    for(int hz : {30,60,120}) {
        float remainder=0; int emitted=0;
        for(int frame=0; frame<hz*20; ++frame)
            Engine::LimitFluidEmission(Engine::AccumulateFluidEmission(20,1.f/hz,remainder),16001,emitted);
        Require(emitted==16001,"finite emitter exceeded its cap or depends on FPS");
        Require(Engine::LimitFluidEmission(40,16001,emitted)==0,"completed emitter restarted");
    }
    g.Compile(L"Resources/shaders/GlassVS.hlsl","main","vs_5_0");
    g.Compile(L"Resources/shaders/GlassPS.hlsl","main","ps_5_0");
    puts("PASS sandbox: frame-rate independent finite emission and glass shader compilation");
    const char* entries[]={"Emit","ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","CalcForce","WriteBack"};
    for(auto entry:entries) g.Kernel(entry,true);
    constexpr UINT count=8;
    std::vector<Particle> initial(count);
    auto p=g.MakeBuffer(count,64,initial.data()), counts=g.MakeBuffer(65536,4), offsets=g.MakeBuffer(65536,4);
    auto sorted=g.MakeBuffer(count,64), indices=g.MakeBuffer(count,4), previous=g.MakeBuffer(count,16), scratch=g.MakeBuffer(count,64);
    struct Box { XMFLOAT3 min; float pad0; XMFLOAT3 max; float pad1; };
    std::vector<Box> boxes;
    for(auto panel:Engine::MakeFluidTankPanels(6,5,5,.25f)) {
        panel.center.y+=.6f;
        const auto low=panel.center-panel.size*.5f, high=panel.center+panel.size*.5f;
        boxes.push_back({{low.x,low.y,low.z},0,{high.x,high.y,high.z},0});
    }
    auto aabb=g.MakeBuffer(5,32,boxes.data());
    std::array<UINT,48> cb{};
    auto f=[&](UINT index,float value){memcpy(&cb[index],&value,4);};
    const float dt=1.f/180;
    f(0,dt); cb[2]=count; cb[3]=count; cb[20]=count; f(5,6.6f); f(7,1); f(9,-.8f); f(15,1);
    f(33,1); // Opt into retained water at emission.
    auto constants=g.Constant(192);
    auto bind=[&]() {
        g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);
        auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr);auto a=aabb.srv.Get();g.context->CSSetShaderResources(0,1,&a);
    };
    bind();g.Dispatch("Emit",1);
    auto emitted=g.Read<Particle>(p);
    for(auto particle:emitted) Require(particle.pad.x==-1 && particle.type==1,"retention flag was not stored per water particle");
    // Probe all inner walls and the floor. The last probe remains ordinary water.
    initial[0]=MakeParticle(-2.9f,2,0,1);initial[0].velocity.x=-4;
    initial[1]=MakeParticle(2.9f,2,0,1);initial[1].velocity.x=4;
    initial[2]=MakeParticle(0,2,-2.4f,1);initial[2].velocity.z=-4;
    initial[3]=MakeParticle(0,2,2.4f,1);initial[3].velocity.z=4;
    initial[4]=MakeParticle(0,.7f,0,1);
    initial[5]=MakeParticle(1,4,1,1);
    initial[6]=MakeParticle(-1,4,-1,1);
    initial[7]=MakeParticle(2,.7f,1,1);initial[7].pad.x=4.999f;
    for(UINT i=0;i<7;++i) initial[i].pad.x=-1;
    g.Unbind();g.context->UpdateSubresource(p.resource.Get(),0,nullptr,initial.data(),0,0);
    cb[2]=0;cb[32]=5;f(33,0); // Retention survives subsequent CB changes.
    for(int frame=0;frame<1100;++frame) {
        bind();g.Dispatch("ClearOriginalIndices",1);g.Dispatch("ClearGridCount",1024);
        g.Dispatch("CountParticles",1);g.Dispatch("PrefixSum",1);g.Dispatch("SortParticles",1);
        g.Dispatch("CalcForce",1);g.Dispatch("WriteBack",1);
    }
    auto result=g.Read<Particle>(p);
    for(UINT i=0;i<7;++i) {
        const auto& particle=result[i];
        Require(particle.color.w>0 && particle.pad.x==-1,"retained water expired after six seconds");
        Require(std::isfinite(particle.position.x) && std::isfinite(particle.position.y) && std::isfinite(particle.position.z),"tank produced a non-finite particle");
        Require(particle.position.x>-3 && particle.position.x<3 && particle.position.z>-2.5f && particle.position.z<2.5f && particle.position.y>=.89f,"water escaped through a tank panel");
    }
    Require(result[7].color.w==0,"normal gameplay water no longer expires");
    puts("PASS sandbox: actual emission retention, five-panel containment for 6.11 seconds, ordinary water expiry preserved");
}
