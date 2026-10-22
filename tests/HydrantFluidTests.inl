void TestHydrantFluid(Gpu& g){
    for(auto entry:{"ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","SortParticlesVelocity","CalcDensity","CalcForce","SavePrevious","CalcDeltaP","ApplyDeltaP","UpdateVelocity","WriteBack"})g.Kernel(entry,true);
    constexpr UINT count=66,groups=2;std::vector<Particle> input;
    for(int z=0;z<4;++z)for(int y=0;y<4;++y)for(int x=0;x<4;++x)input.push_back(MakeParticle(x*.12f,2+y*.12f,z*.12f,x%2?1.1f:1.2f));
    for(float type:{1.1f,1.2f}){auto p=MakeParticle(20+(type-1)*40,6,0,type);p.velocity={0,0,72};input.push_back(p);}
    auto particles=g.MakeBuffer(count,64,input.data()),counts=g.MakeBuffer(65536,4),offsets=g.MakeBuffer(65536,4),sorted=g.MakeBuffer(count,64),indices=g.MakeBuffer(count,4),previous=g.MakeBuffer(count,16),scratch=g.MakeBuffer(count,64);
    XMFLOAT4 dummy[2]{};auto aabb=g.MakeBuffer(1,32,dummy);auto constants=g.Constant(192);
    std::array<UINT,48> cb{};float dt=1.f/120;memcpy(&cb[0],&dt,4);cb[3]=cb[20]=count;
    auto bind=[&](){g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={particles.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get()};g.context->CSSetUnorderedAccessViews(0,7,u,nullptr);auto a=aabb.srv.Get();g.context->CSSetShaderResources(0,1,&a);};
    auto grid=[&](bool velocity=false){g.Dispatch("ClearOriginalIndices",groups);g.Dispatch("ClearGridCount",1024);g.Dispatch("CountParticles",groups);g.Dispatch("PrefixSum",1);g.Dispatch(velocity?"SortParticlesVelocity":"SortParticles",groups);};
    bind();g.Dispatch("SavePrevious",groups);grid();g.Dispatch("CalcDensity",groups);
    auto compression=[&](){double total=0;for(const auto& p:g.Read<Particle>(sorted))total+=std::max(0.f,p.density/125-1);return total;};
    double before=compression();
    for(int iteration=0;iteration<3;++iteration){bind();grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcDeltaP",groups);g.Dispatch("ApplyDeltaP",groups);}
    bind();grid();g.Dispatch("CalcDensity",groups);double after=compression();
    Require(before>1&&after<before*.8,"hydrant water bypasses incompressibility projection");
    g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    bind();g.Dispatch("SavePrevious",groups);grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcForce",groups);g.Dispatch("WriteBack",groups);
    auto flowing=g.Read<Particle>(particles);
    for(UINT i=64;i<count;++i)Require(flowing[i].position.z>.55f&&flowing[i].velocity.z>70&&flowing[i].velocity.y<0,"hydrant water ignores gravity or loses jet momentum");
    for(UINT i=0;i<count;++i){input[i]=MakeParticle(float(i)*2,.05f,0,i%2?1.1f:1.2f);input[i].velocity={0,-4,0};}
    g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    bind();g.Dispatch("SavePrevious",groups);grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcForce",groups);g.Dispatch("WriteBack",groups);
    for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",groups);g.Dispatch("CalcDeltaP",groups);g.Dispatch("ApplyDeltaP",groups);}
    auto grounded=g.Read<Particle>(particles);for(const auto& p:grounded)Require(std::isfinite(p.position.x)&&p.position.y>=.219f&&p.pad.z>0,"hydrant water penetrates city floor or loses contact metadata");
    printf("PASS hydrant GPU water: PBF compression %.3f -> %.3f, gravity, >70-unit/s momentum, floor contact for attack and guard phases\n",before,after);
    g.Kernel("Emit",true);
    for(float frame:{1.f/120,1.f/60,1.f/30,.05f}){
        UINT emitted=UINT(4200*frame);auto stream=g.MakeBuffer(emitted,64);
        cb={};auto f=[&](UINT slot,float value){memcpy(&cb[slot],&value,4);};
        f(0,frame);cb[2]=cb[3]=cb[20]=emitted;f(5,8);f(7,1.1f);f(10,72.f/5);f(15,.96f);f(34,1.1f);
        g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
        auto u=stream.uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&u,nullptr);g.Dispatch("Emit",(emitted+63)/64);
        auto result=g.Read<Particle>(stream);float lo=100,hi=-100,width=0;
        for(const auto& p:result){float endZ=p.position.z+p.velocity.z*frame;lo=std::min(lo,endZ);hi=std::max(hi,endZ);width=std::max(width,std::abs(p.position.x));Require(p.velocity.z>71&&p.pad.y>1,"jet loses launch speed or width metadata");}
        Require(hi-lo>72*frame*.96f&&width>1.f,"jet emission leaves a frame gap or a thin nozzle");
        Require(lo<.02f&&hi>72*frame*.98f,"integration detaches the jet from its nozzle");
    }
    // Sparse fast water must retain a bulk surface even without neighbours, at
    // both fine and coarse voxel sizes. This reproduces the disappearing jet.
    g.Kernel("BuildShapes");
    std::array<Particle,2> isolated={MakeParticle(0,5,0,1.1f),MakeParticle(3,5,0,1.2f)};
    for(auto& p:isolated){p.velocity={0,0,72};p.pad.y=1.1f;}
    UINT ids[2]={0,1};auto samples=g.MakeBuffer(2,64,isolated.data()),original=g.MakeBuffer(2,4,ids),shapes=g.MakeBuffer(2,64);
    auto settings=g.Constant(sizeof(VolumeCB));
    for(float voxel:{.2f,.6f,1.f}){
        VolumeCB v{};v.count=2;v.cell=voxel;
        g.Unbind();g.context->UpdateSubresource(settings.Get(),0,nullptr,&v,0,0);auto c=settings.Get();g.context->CSSetConstantBuffers(1,1,&c);
        ID3D11ShaderResourceView* s[]={samples.srv.Get(),original.srv.Get()};g.context->CSSetShaderResources(0,2,s);auto u=shapes.uav.Get();g.context->CSSetUnorderedAccessViews(2,1,&u,nullptr);g.Dispatch("BuildShapes",1);
        for(const auto& shape:g.Read<Shape>(shapes)){
            Require(shape.info.x>1&&shape.info.z==0&&shape.info.w==1&&shape.info.y>=voxel*1.65f,"sparse hydrant jet disappears into tiny spray");
            float r2=.75f/(1.65f*1.65f);Require(shape.info.x*std::pow(1-r2,3)>.25f,"hydrant kernel can vanish between voxel samples");
        }
    }
    puts("PASS hydrant stream: full emission slabs at 20/30/60/120Hz, wide nozzle, stable sparse surface across voxel LODs");
}
