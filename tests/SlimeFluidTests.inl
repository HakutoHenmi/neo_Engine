// Read actual GPU particle positions; no rendered silhouette participates.
void TestSlimeFluid(Gpu& g) {
    for(auto name:{"SavePrevious","ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","SortParticlesVelocity","CalcDensity","CalcForce","WriteBack","CalcDeltaP","ApplyDeltaP","UpdateVelocity"})g.Kernel(name,true);
    const UINT count=6600,groups=(count+63)/64;
    std::vector<Particle> empty(count);for(auto& v:empty)v.position.y=-1000;
    auto p=g.MakeBuffer(count,64,empty.data()),counts=g.MakeBuffer(65536,4),offsets=g.MakeBuffer(65536,4);
    auto sorted=g.MakeBuffer(count,64),indices=g.MakeBuffer(count,4),previous=g.MakeBuffer(count,16),scratch=g.MakeBuffer(count,64);
    XMFLOAT4 dummy[2]{};auto aabb=g.MakeBuffer(1,32,dummy);
    std::array<UINT,48> cb{};auto f=[&](UINT i,float v){memcpy(&cb[i],&v,4);};
    f(0,1.f/120);cb[3]=count;f(19,32);f(27,100);f(31,4);f(33,1);f(34,count);f(35,count);f(17,1.43f);
    auto constants=g.Constant(192);
    auto bind=[&](){g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={p.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};
        g.context->CSSetUnorderedAccessViews(0,7,u,nullptr);auto a=aabb.srv.Get();g.context->CSSetShaderResources(0,1,&a);};
    auto grid=[&](bool velocity=false){g.Dispatch("ClearOriginalIndices",groups);g.Dispatch("ClearGridCount",1024);g.Dispatch("CountParticles",groups);g.Dispatch("PrefixSum",1);g.Dispatch(velocity?"SortParticlesVelocity":"SortParticles",groups);};
    float fullTop=0;
    for(int stage=0;stage<7;++stage){
        bool slide=stage==1;float mass=stage==2?20.f:100.f;
        f(31,slide?5.f:4.f);f(17,stage==3?5.f:1.43f);f(24,stage==3?1.4f:stage==4?.6f:1.f);f(25,stage==3?1.f:0.f);f(26,stage>=5?1.f:0.f);f(30,stage==6?-1.f:1.f);f(27,mass);f(34,count*mass/100);
        for(int step=0;step<90;++step){
            bind();g.Dispatch("SavePrevious",groups);grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcForce",groups);g.Dispatch("WriteBack",groups);
            for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcDeltaP",groups);g.Dispatch("ApplyDeltaP",groups);}
            grid(true);g.Dispatch("UpdateVelocity",groups);g.Dispatch("WriteBack",groups);
        }
        auto result=g.Read<Particle>(p);float top=0,bottom=100,width=0,center=0,shoulder=0;UINT live=0,frontCount=0,backCount=0;float frontWidth=0,backWidth=0;
        for(auto v:result){if(v.color.w<=0)continue;++live;
            Require(std::isfinite(v.position.x)&&std::isfinite(v.velocity.y),"slime fluid became non-finite");
            Require(v.position.y>=.199f,"slime fluid penetrated floor");
            float along=v.position.z*(stage==6?-1.f:1.f);
            if(along>.8f){frontWidth+=v.position.x*v.position.x;++frontCount;}if(along<-.8f){backWidth+=v.position.x*v.position.x;++backCount;}
            float radial=std::hypot(v.position.x,v.position.z);bottom=std::min(bottom,v.position.y);width=std::max(width,radial);top=std::max(top,v.position.y);
            if(radial<.6f)center=std::max(center,v.position.y);if(radial>2.f)shoulder=std::max(shoulder,v.position.y);
        }
        Require(live==UINT(count*mass/100),"slime particle count does not match mass");
        Require(top>.35f&&top<(stage==3?9.f:2.8f)&&width<5.f,"slime fluid collapsed or escaped containment");
        if(stage==0){fullTop=top;Require(center>shoulder+.15f,"slime crown is depressed");}
        if(stage==1)Require(top<fullTop*.65f,"slide does not flatten actual fluid");
        if(stage==2)Require(top<fullTop*.8f,"spent mass does not shrink actual fluid");
        if(stage==3)Require(bottom<3.5f&&top-bottom>3.f,"airborne fluid retains a rigid flat base");
        if(stage==4)Require(top<fullTop*.85f,"landing does not squash fluid");
        if(stage>=5){float front=std::sqrt(frontWidth/std::max(1U,frontCount)),back=std::sqrt(backWidth/std::max(1U,backCount));printf("MOTION front %.3f rear %.3f\n",front,back);Require(front>back*1.12f,"moving fluid does not have a broader advancing front");}
        printf("PASS slime GPU stage %d: particles %u, height %.3f, radius %.3f, crown %.3f, shoulder %.3f\n",stage,live,top,width,center,shoulder);fflush(stdout);
    }
}
