// The same liquid lattice must have the same density, pressure projection and
// viscosity in particle coordinates, even when world-space density < self density.
void TestScaleInvariantDensity(Gpu& g){
    for(auto e:{"ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","CalcDensity","CalcDeltaP","UpdateVelocity"})if(!g.kernels.count(e))g.Kernel(e,true);
    struct Goal {XMFLOAT3 local;float blend;XMFLOAT3 origin;float density;XMFLOAT3 forward;float omega;XMFLOAT4 color,material,motion,shape,reserved;};
    constexpr UINT count=343,groups=(count+63)/64;
    std::vector<Particle> input(count);std::vector<UINT> mapping(16384*12);
    auto particles=g.MakeBuffer(count,64),sorted=g.MakeBuffer(count,64),indices=g.MakeBuffer(count,4),previous=g.MakeBuffer(count,16),scratch=g.MakeBuffer(count,64);
    auto counts=g.MakeBuffer(65536,4),offsets=g.MakeBuffer(65536,4),morph=g.MakeBuffer(16384,48,mapping.data()),targets=g.MakeBuffer(1,sizeof(Goal)),aabb=g.MakeBuffer(128,32);
    std::array<UINT,48> cb{};auto f=[&](UINT i,float value){memcpy(&cb[i],&value,4);};cb[3]=cb[20]=count;f(0,1.f/120);f(31,4);f(35,float(count));f(47,1);auto constants=g.Constant(192);
    std::vector<float> baselineDensity;std::vector<XMFLOAT3> baselineDelta,baselineVelocity;
    for(float scale:{1.f,2.44949f,3.464102f,8.f,16.f}){
        Goal goal{};goal.density=275/(scale*scale*scale);goal.material.y=40;
        UINT i=0;for(int z=0;z<7;++z)for(int y=0;y<7;++y)for(int x=0;x<7;++x){auto& p=input[i];p=MakeParticle(float(x)*.14f*scale,(10+float(y)*.14f)*scale,float(z)*.14f*scale,0);p.velocity={float(int(i%3)-1)*scale,0,0};++i;}
        auto bind=[&](){g.Unbind();auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
            ID3D11UnorderedAccessView* u[]={particles.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get(),morph.uav.Get()};g.context->CSSetUnorderedAccessViews(0,8,u,nullptr);
            ID3D11ShaderResourceView* s[]={aabb.srv.Get(),targets.srv.Get()};g.context->CSSetShaderResources(0,2,s);};
        g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,&goal,0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
        bind();g.Dispatch("ClearOriginalIndices",groups);g.Dispatch("ClearGridCount",1024);g.Dispatch("CountParticles",groups);g.Dispatch("PrefixSum",1);g.Dispatch("SortParticles",groups);g.Dispatch("CalcDensity",groups);
        auto density=g.Read<Particle>(sorted);auto ids=g.Read<UINT>(indices);bind();g.Dispatch("CalcDeltaP",groups);auto delta=g.Read<Particle>(scratch);bind();g.Dispatch("UpdateVelocity",groups);auto velocities=g.Read<Particle>(scratch);
        std::vector<float> values(count);std::vector<XMFLOAT3> displacements(count),speeds(count);
        for(UINT j=0;j<count;++j){UINT id=ids[j];Require(id<count,"scaled grid lost a particle");values[id]=density[j].density;displacements[id]={delta[j].position.x/scale,delta[j].position.y/scale,delta[j].position.z/scale};speeds[id]={velocities[j].velocity.x/scale,velocities[j].velocity.y/scale,velocities[j].velocity.z/scale};}
        if(scale==1){baselineDensity=values;baselineDelta=displacements;baselineVelocity=speeds;}
        float densityError=0,deltaError=0,speedError=0;for(UINT j=0;j<count;++j){
            densityError=std::max(densityError,std::abs(values[j]-baselineDensity[j])/275);
            deltaError=std::max(deltaError,XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat3(&displacements[j]),XMLoadFloat3(&baselineDelta[j])))));
            speedError=std::max(speedError,XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat3(&speeds[j]),XMLoadFloat3(&baselineVelocity[j])))));
            Require(std::isfinite(values[j])&&values[j]>=24,"grown lattice has invalid density");}
        printf("growth scale %.3f: world rest=%.4f density error=%.6f projection error=%.6f viscosity error=%.6f\n",scale,goal.density,densityError,deltaError,speedError);
        Require(densityError<.001f&&deltaError<.001f&&speedError<.001f,"growth changes normalized liquid physics or loses neighbours");
    }
    // Thirteen differently sized bodies overlap deliberately. Hashing must
    // preserve each body's neighbours without mixing foreign density.
    std::vector<Goal> goals(count);std::vector<float> spacing(count);
    for(UINT i=0;i<count;++i){UINT owner=i%13,n=i/13;float s=owner==0?3.464102f:.8f+float(owner)*.08f;spacing[i]=s;
        input[i]=MakeParticle(float(n%3)*.14f*s,(10+float(n/3%3)*.14f)*s,float(n/9)*.14f*s,0);goals[i].density=275/(s*s*s);
        mapping[(8192+i)*12+3]=i;mapping[(8192+i)*12+9]=owner;}
    g.Unbind();targets=g.MakeBuffer(count,sizeof(Goal),goals.data());g.context->UpdateSubresource(morph.resource.Get(),0,nullptr,mapping.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);ID3D11UnorderedAccessView* u[]={particles.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get(),morph.uav.Get()};g.context->CSSetUnorderedAccessViews(0,8,u,nullptr);
    ID3D11ShaderResourceView* srvs[]={aabb.srv.Get(),targets.srv.Get()};g.context->CSSetShaderResources(0,2,srvs);g.Dispatch("ClearOriginalIndices",groups);g.Dispatch("ClearGridCount",1024);g.Dispatch("CountParticles",groups);g.Dispatch("PrefixSum",1);g.Dispatch("SortParticles",groups);g.Dispatch("CalcDensity",groups);
    auto measured=g.Read<Particle>(sorted);auto ids=g.Read<UINT>(indices);float mixedError=0;
    for(UINT i=0;i<count;++i){UINT source=ids[i];double expected=0;for(UINT j=source%13;j<count;j+=13){auto a=input[source].position,b=input[j].position;double x=(a.x-b.x)/spacing[source],y=(a.y-b.y)/spacing[source],z=(a.z-b.z)/spacing[source],w=std::max(0.,.16-x*x-y*y-z*z);expected+=315./(64*3.141592653589793*.000262144)*w*w*w;}
        mixedError=std::max(mixedError,float(std::abs(measured[i].density-expected)/275));}
    printf("13 overlapping owner scales: relative density error=%.6f\n",mixedError);Require(mixedError<.001f,"different-size bodies lose neighbours or share pressure");
    puts("PASS growth physics: scale-invariant pressure and viscosity through 16x size, compact hash retains all neighbours");
}
