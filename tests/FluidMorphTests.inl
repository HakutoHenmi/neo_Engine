#include "FluidScaleTests.inl"
void TestWeaponSubsteps(Gpu& g){
    struct Goal {XMFLOAT3 local;float blend;XMFLOAT3 origin;float density;XMFLOAT3 forward;float omega;XMFLOAT4 color,material,motion,shape,reserved;};
    Goal goal{{0,3,0},1,{},275,{0,0,1},32,{.4f,.8f,.1f,1},{},{},{0,0,0,-4},{12,0,0,-1}};
    Particle initial=MakeParticle(0,3,0,0);initial.density=275;initial.velocity={12,0,0};
    UINT zero=0;auto particles=g.MakeBuffer(1,64,&initial),sorted=g.MakeBuffer(1,64,&initial),indices=g.MakeBuffer(1,4,&zero),scratch=g.MakeBuffer(1,64),morph=g.MakeBuffer(16384,48);
    auto counts=g.MakeBuffer(65536,4),offsets=g.MakeBuffer(65536,4),targets=g.MakeBuffer(1,sizeof(Goal),&goal),aabb=g.MakeBuffer(128,32);
    std::array<UINT,48> cb{};auto f=[&](UINT i,float value){memcpy(&cb[i],&value,4);};cb[3]=cb[20]=1;f(0,1.f/120);f(31,4);f(35,1);f(47,1);auto constants=g.Constant(192);
    auto measure=[&](int hz,bool interpolate){Particle particle=initial;double sum=0,square=0;int measured=0;float elapsed=0;
        int frames=hz?hz:60;
        for(int frame=0;frame<frames;++frame){const float varying[]={.018f,.041f,.029f,.05f};float frameDt=hz?1.f/hz:varying[frame%4];elapsed+=frameDt;goal.origin.x=12*elapsed;
            int substeps=std::max(1,int(std::ceil(frameDt*(hz?120.f:100.f)-.0001f)));float stepDt=frameDt/substeps;f(0,stepDt);
            g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,&goal,0,0);
            for(int step=0;step<substeps;++step){f(12,interpolate?frameDt-stepDt*(step+1):0);
                g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);g.context->UpdateSubresource(sorted.resource.Get(),0,nullptr,&particle,0,0);
                auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
                ID3D11UnorderedAccessView* u[]={particles.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),nullptr,scratch.uav.Get(),morph.uav.Get()};g.context->CSSetUnorderedAccessViews(0,8,u,nullptr);
                ID3D11ShaderResourceView* s[]={aabb.srv.Get(),targets.srv.Get()};g.context->CSSetShaderResources(0,2,s);g.Dispatch("CalcForce",1);g.Dispatch("WriteBack",1);particle=g.Read<Particle>(particles)[0];
                if(frame>frames/2){sum+=particle.velocity.x;square+=double(particle.velocity.x)*particle.velocity.x;++measured;}
            }}
        float deviation=float(std::sqrt(std::max(0.,square/measured-std::pow(sum/measured,2.))));
        printf("weapon travel %dHz %s: speed=%.4f jitter=%.5f\n",hz,interpolate?"substep path":"frame-end goal",sum/measured,deviation);
        return deviation;
    };
    float staircase=measure(30,false);for(int hz:{30,60,120,0}){float smooth=measure(hz,true);Require(smooth<.01f&&smooth<staircase*.1f,"weapon substeps still chase a jumping frame-end goal");}
    puts("PASS weapon travel: 30/60/120Hz and irregular 18-50 ms frames remove speed pulses");
}
void TestFluidMorph(Gpu& g){
    for(auto entry:{"MorphKeys","MorphSort","MorphAssign","SavePrevious","ClearOriginalIndices","ClearGridCount","CountParticles","PrefixSum","SortParticles","SortParticlesVelocity","CalcDensity","CalcForce","WriteBack","CalcDeltaP","ApplyDeltaP","UpdateVelocity"})g.Kernel(entry,true);
    TestWeaponSubsteps(g);
    TestScaleInvariantDensity(g);
    struct Goal {XMFLOAT3 local;float blend;XMFLOAT3 origin;float density;XMFLOAT3 forward;float omega;XMFLOAT4 color;XMFLOAT4 material,motion,shape,reserved;};
    struct Entry {XMFLOAT3 start;UINT target;XMFLOAT3 previous;UINT key;UINT source,owner;XMFLOAT2 unused;};
    static_assert(sizeof(Goal)==128&&sizeof(Entry)==48);
    constexpr UINT count=1024;float scale=std::cbrt(float(count/2)/6600.f);
    Engine::FluidShape::Mesh mesh("Resources/Models/SlimeLife/hammer.obj");auto rest=Engine::FluidShape::Sample(nullptr,count/2),shape=Engine::FluidShape::Sample(&mesh,count/2);
    std::vector<Particle> input(count);std::vector<Goal> goals(count);
    for(UINT i=0;i<count;++i){UINT owner=i/(count/2),local=i%(count/2);float x=owner==0?-5.f:5.f;auto p=rest[local];
        input[i].position={x+p.x*scale,2+p.y*scale,p.z*scale};input[i].color={.4f,.8f,.1f,1};input[i].density=275;
        auto q=shape[local];goals[i]={{q.x*scale,q.y*scale,q.z*scale},1,{x,2,0},275,{0,float(owner),1},20,{.4f,.8f,.1f,1},{4,40,50,0},{},{},{}};}
    input[17].color.w=0;input[17].position.y=-1000;input[750].color.w=0;input[750].position.y=-1000;
    auto particles=g.MakeBuffer(count,64,input.data()),sorted=g.MakeBuffer(count,64),indices=g.MakeBuffer(count,4),previous=g.MakeBuffer(count,16),scratch=g.MakeBuffer(count,64);
    auto counts=g.MakeBuffer(65536,4),offsets=g.MakeBuffer(65536,4),morph=g.MakeBuffer(16384,48),targets=g.MakeBuffer(count,sizeof(Goal),goals.data()),aabb=g.MakeBuffer(128,32);
    std::array<UINT,48> cb{};auto f=[&](UINT i,float v){memcpy(&cb[i],&v,4);};f(0,1.f/120);cb[3]=count;f(31,4);f(35,float(count));f(47,1);f(7,15);
    auto constants=g.Constant(192);
    auto bind=[&](){g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb.data(),0,0);auto c=constants.Get();g.context->CSSetConstantBuffers(0,1,&c);
        ID3D11UnorderedAccessView* u[]={particles.uav.Get(),counts.uav.Get(),offsets.uav.Get(),sorted.uav.Get(),indices.uav.Get(),previous.uav.Get(),scratch.uav.Get(),morph.uav.Get()};g.context->CSSetUnorderedAccessViews(0,8,u,nullptr);
        ID3D11ShaderResourceView* s[]={aabb.srv.Get(),targets.srv.Get()};g.context->CSSetShaderResources(0,2,s);};
    bind();g.Dispatch("MorphKeys",128);for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);
    auto assigned=g.Read<Entry>(morph);std::vector<bool> seen(count,false);
    for(UINT i=0;i<count;++i){auto e=assigned[8192+i];Require(e.target<count&&!seen[e.target],"morph assignment not bijective");seen[e.target]=true;Require(e.owner==i/(count/2)&&e.target/(count/2)==i/(count/2),"assignment crossed owners");}
    // Retarget the second body while the first body's ordering changes: the first map must survive.
    std::swap(input[0].position,input[511].position);g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    f(7,2);bind();g.Dispatch("MorphKeys",128);
    for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}
    cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);auto partial=g.Read<Entry>(morph);
    for(UINT i=0;i<count/2;++i)Require(assigned[8192+i].target==partial[8192+i].target,"another body's transform replaced an active assignment");
    std::swap(input[0].position,input[511].position);g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    auto grid=[&](bool velocity=false){g.Dispatch("ClearOriginalIndices",count/64);g.Dispatch("ClearGridCount",1024);g.Dispatch("CountParticles",count/64);g.Dispatch("PrefixSum",1);g.Dispatch(velocity?"SortParticlesVelocity":"SortParticles",count/64);};
    for(int step=0;step<120;++step){bind();g.Dispatch("SavePrevious",count/64);grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
        for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcDeltaP",count/64);g.Dispatch("ApplyDeltaP",count/64);}grid(true);g.Dispatch("UpdateVelocity",count/64);g.Dispatch("WriteBack",count/64);}
    auto output=g.Read<Particle>(particles);auto after=g.Read<Entry>(morph);double error=0;
    for(UINT i=0;i<count;++i){Require(assigned[8192+i].target==after[8192+i].target,"spatial sorting changed assignment");auto t=goals[after[8192+i].target];auto p=output[i].position;
        Require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(output[i].velocity.z),"morph became nonfinite");Require(output[i].color.w>.9f&&p.y>-500,"inactive morph slot was not restored");
        Require(output[i].pad.x==1000.f+float(i/(count/2))&&output[i].pad.y==t.density,"render ownership or stable sample volume lost after sorting");
        float x=p.x-t.origin.x-t.local.x,y=p.y-t.origin.y-t.local.y,z=p.z-t.local.z;error+=x*x+y*y+z*z;}
    float rms=float(std::sqrt(error/count));Require(rms<.65f,"spring failed to form target with PBF enabled");
    printf("PASS point-cloud GPU: unique assignments, two owners, persistent IDs, PBF spring formation RMS=%.4f\n",rms);
    // Same particles, identical velocity impulse: only the uploaded recipe changes.
    for(UINT i=0;i<count;++i){UINT owner=i/(count/2),local=i%(count/2);float x=owner==0?-5.f:5.f;auto p=rest[local];
        goals[i]={{p.x*scale,p.y*scale,p.z*scale},0,{x,2,0},275,{0,float(owner),1},55,{.4f,.8f,.1f,1},{3,3,0,0},{},{2.55f*scale,1.8f*scale,2.55f*scale,1},{}};
        input[i]={};input[i].position={x+p.x*scale,2+p.y*scale,p.z*scale};input[i].color={.4f,.8f,.1f,1};input[i].velocity={0,i%2?8.f:-8.f,0};}
    f(7,15);g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    bind();g.Dispatch("MorphKeys",128);for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);f(7,0);
    auto variance=[&](float viscosity){for(auto& t:goals)t.material.y=viscosity;
        std::vector<XMFLOAT4> before(count);for(UINT i=0;i<count;++i)before[i]={input[i].position.x,input[i].position.y-input[i].velocity.y/30,input[i].position.z,1};
        g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);g.context->UpdateSubresource(previous.resource.Get(),0,nullptr,before.data(),0,0);
        f(0,1.f/30);bind();grid(true);g.Dispatch("UpdateVelocity",count/64);g.Dispatch("WriteBack",count/64);auto result=g.Read<Particle>(particles);double v=0;for(auto p:result)v+=p.velocity.y*p.velocity.y;return float(v/count);};
    float watery=variance(3),sticky=variance(108);printf("mixture variance water=%.4f glue=%.4f\n",watery,sticky);Require(sticky<watery*.6f,"GPU viscosity ignores mixture");
    // A free liquid particle inside the field must not be tethered to its old point.
    auto map=g.Read<Entry>(morph);UINT selected=0;float nearest=1e9f;
    for(UINT i=0;i<count/2;++i){auto t=goals[map[8192+i].target];float radius=t.local.x*t.local.x+t.local.y*t.local.y+t.local.z*t.local.z;if(radius<nearest){nearest=radius;selected=i;}}
    for(UINT i=0;i<count;++i){auto t=goals[map[8192+i].target];input[i].position={t.origin.x+t.local.x,t.origin.y+t.local.y,t.origin.z+t.local.z};input[i].velocity={};}
    input[selected].position.x+=.12f;g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);f(0,1.f/120);bind();grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
    auto freeLiquid=g.Read<Particle>(particles);Require(std::abs(freeLiquid[selected].velocity.x)<.01f,"ordinary liquid still pins particles to points");
    // In flight the underside is below the centre, not clamped to a moving floor.
    for(auto& t:goals){t.origin.y=10;t.motion.w=1;t.shape={1.8f*scale,1.8f*scale,1.8f*scale,1};}
    for(UINT i=0;i<count;++i){auto t=goals[map[8192+i].target];input[i].position={t.origin.x+t.local.x*.5f,10-.4f+t.local.y*.25f,t.local.z*.5f};}
    std::vector<Particle> zero(count);g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);g.context->UpdateSubresource(scratch.resource.Get(),0,nullptr,zero.data(),0,0);
    bind();grid();g.Dispatch("ApplyDeltaP",count/64);auto flying=g.Read<Particle>(particles);float low=1e9f;for(auto p:flying){Require(std::isfinite(p.position.y),"free liquid became nonfinite");low=std::min(low,p.position.y);}Require(low<9.8f,"jump underside is still cut flat by moving origin");
    printf("PASS soft-liquid GPU: no rest-point pinning, airborne underside %.3f, mixture velocity variance water=%.3f glue=%.3f\n",low,watery,sticky);
    // Probe actual GPU containment in fixed world directions. A new direction
    // changes the symmetric shape, independent of the slowly turning face.
    auto flowProbe=[&](float xx,float xz,float yaw){
        for(auto& t:goals){t.origin.y=10;t.motion={};t.forward.x=std::sin(yaw);t.forward.z=std::cos(yaw);t.shape={2.55f*scale,1.8f*scale,2.55f*scale,1};t.reserved={xx,xz,0,0};}
        for(UINT i=0;i<count;++i){auto t=goals[map[8192+i].target];float r=4*scale;UINT axis=i%8;float x=axis==0?r:axis==1?-r:0,z=axis==2?r:axis==3?-r:0;
            if(axis>=4){x=(axis%2==0?1.f:-1.f)*r*.7071068f;z=(axis<6?1.f:-1.f)*r*.7071068f;}input[i].position={t.origin.x+x,10,z};}
        g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);g.context->UpdateSubresource(scratch.resource.Get(),0,nullptr,zero.data(),0,0);
        bind();grid();g.Dispatch("ApplyDeltaP",count/64);return g.Read<Particle>(particles);
    };
    auto alongX=flowProbe(.18f,0,0),alongZ=flowProbe(-.18f,0,0),faceTurn=flowProbe(.18f,0,1.5707963f),round=flowProbe(0,0,0);
    Require(alongX[0].position.x>alongZ[0].position.x+.02f&&alongZ[2].position.z>alongX[2].position.z+.02f,"moving direction does not change actual GPU shape");
    Require(round[0].position.x<alongX[0].position.x&&round[0].position.x>alongZ[0].position.x,"old extension does not relax between travel directions");
    for(UINT i=0;i<count;++i){auto a=alongX[i].position,b=faceTurn[i].position;Require(std::abs(a.x-b.x)+std::abs(a.z-b.z)<.0002f,"face yaw rotates the old extended body");}
    auto centre=goals[map[8192].target].origin;
    Require(std::abs((alongX[0].position.x+alongX[1].position.x)*.5f-centre.x)<.0002f&&std::abs(alongX[2].position.x-alongX[3].position.x)<.0002f,"flow field bends its rear sideways");
    auto diagonal=flowProbe(0,.18f,0),otherDiagonal=flowProbe(0,-.18f,0);
    Require(diagonal[4].position.z>otherDiagonal[4].position.z+.02f,"GPU shape ignores diagonal travel");
    puts("PASS flow shape GPU: travel-aligned deformation, relaxed turn, face-yaw independence, symmetric rear");
    // Two isolated probes: the core turns first while the outer liquid keeps
    // moving in its old direction. SavePrevious must not carry either sideways.
    for(auto& t:goals){t.origin.y=10;t.forward.x=0;t.forward.z=1;t.reserved={};t.motion={-6,0,0,0};t.material.x=3;}
    for(auto& p:input){p={};p.position.y=-1000;}
    for(UINT i=0;i<2;++i){auto t=goals[map[8192+i].target];input[i].color={.4f,.8f,.1f,1};input[i].velocity={3,0,0};input[i].position={t.origin.x,10+t.shape.y*.43f,i==0?0:t.shape.z*.78f};}
    g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);
    // Inactive slots are restored by SavePrevious, so give them the same zero
    // motion and inspect only the live probes before testing localized forces.
    f(0,1.f/30);bind();g.Dispatch("SavePrevious",count/64);auto carried=g.Read<Particle>(particles);
    for(UINT i=0;i<2;++i)Require(carried[i].position.x==input[i].position.x&&carried[i].position.z==input[i].position.z,"outer liquid still receives rigid horizontal transport");
    g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);bind();grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
    auto led=g.Read<Particle>(particles);
    Require(led[0].velocity.x<0&&led[1].velocity.x>0,"core and outer liquid reverse together instead of following with inertia");
    printf("PASS liquid core GPU: no rigid transport, reversing core %.3f precedes outer liquid %.3f\n",led[0].velocity.x,led[1].velocity.x);
    // Sustained forward travel must keep a crown in front of the core, rather
    // than pushing the leading liquid into a thin ground-level sheet.
    auto frontCrown=[&](float speed){
        for(UINT i=0;i<count;++i){UINT owner=i/(count/2),local=i%(count/2);auto p=rest[local];float x=owner==0?-5.f:5.f;
            goals[i]={{p.x*scale,p.y*scale,p.z*scale},0,{x,0,0},275,{0,float(owner),1},59,{.4f,.8f,.1f,1},{4,40,23,0},{0,0,speed,0},{2.55f*scale,1.8f*scale,2.55f*scale,1},{-.088f,0,0,.3f*2.55f*scale}};
            input[i]={};input[i].position={x+p.x*scale,std::max(.15f,p.y*scale),p.z*scale};input[i].color={.4f,.8f,.1f,1};input[i].velocity.z=speed;}
        g.Unbind();g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);f(0,1.f/120);
        for(int frame=0;frame<240;++frame){for(auto& t:goals)t.origin.z=speed*float(frame+1)/120;
            g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);bind();g.Dispatch("SavePrevious",count/64);grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
            for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcDeltaP",count/64);g.Dispatch("ApplyDeltaP",count/64);}grid(true);g.Dispatch("UpdateVelocity",count/64);g.Dispatch("WriteBack",count/64);}
        float crown=0;UINT front=0;for(auto p:g.Read<Particle>(particles)){
            Require(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z),"forward liquid became nonfinite");
            if(p.position.z-speed*2>.2f*2.55f*scale){crown=std::max(crown,p.position.y);++front;}}
        printf("forward speed=%.2f crown=%.3f front=%u\n",speed,crown,front);return XMFLOAT2{crown,float(front)};
    };
    auto restingFront=frontCrown(0),movingFront=frontCrown(6);
    Require(movingFront.x>restingFront.x*.8f&&movingFront.y>restingFront.y*.4f,"forward crown collapses or loses its liquid compared with rest");
    auto handCloud=Engine::FluidShape::SampleEngulf(rest);
    for(UINT i=0;i<count;++i){UINT owner=i/(count/2),local=i%(count/2);float x=owner==0?-5.f:5.f;auto p=rest[local];
        goals[i]={{p.x*scale,p.y*scale,p.z*scale},0,{x,2,0},275,{0,float(owner),1},36,{.4f,.8f,.1f,1},{4,40,50,0},{},{},{}};
        input[i]={};input[i].position={x+p.x*scale,2+p.y*scale,p.z*scale};input[i].color={.4f,.8f,.1f,1};}
    g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);f(0,1.f/120);f(7,15);
    bind();g.Dispatch("MorphKeys",128);for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);f(7,0);
    float handError=0,handHeight=0;
    for(int frame=0;frame<108;++frame){float phase=float(frame+1)/108;
        for(UINT i=0;i<count;++i){auto p=Engine::FluidShape::EngulfPose(handCloud[i%(count/2)],phase,{0,.5f,5});goals[i].local={p.x*scale,p.y*scale,p.z*scale};goals[i].blend=1;}
        g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);bind();g.Dispatch("SavePrevious",count/64);grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
        for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcDeltaP",count/64);g.Dispatch("ApplyDeltaP",count/64);}grid(true);g.Dispatch("UpdateVelocity",count/64);g.Dispatch("WriteBack",count/64);
        if(frame==54){auto positions=g.Read<Particle>(particles);auto maps=g.Read<Entry>(morph);double sum=0;for(UINT i=0;i<count;++i){auto p=positions[i].position;auto t=goals[maps[8192+i].target];float x=p.x-t.origin.x-t.local.x,y=p.y-t.origin.y-t.local.y,z=p.z-t.local.z;sum+=x*x+y*y+z*z;handHeight=std::max(handHeight,(p.y-2)/scale);}handError=float(std::sqrt(sum/count))/scale;}
    }
    Require(handError<1.25f&&handHeight>2.8f,"PBF cannot form the tall two-handed engulf pose");
    for(auto p:g.Read<Particle>(particles))Require(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&p.color.w>.9f,"hand animation loses particles or becomes nonfinite");
    printf("PASS engulf hands GPU: two owners, volumetric reach/wrap/pull, height %.2f, formation RMS %.3f\n",handHeight,handError);
    // Animate the same production poses used for collision, without reassigning
    // individual particles between repeated swings.
    auto weaponMotion=[&](Game::Chrono::LifeAction form,const std::vector<Engine::FluidShape::Point>& cloud){
        using namespace Game::Chrono;
        for(UINT i=0;i<count;++i){UINT owner=i/(count/2);auto p=cloud[i%(count/2)];auto pose=LifeWeaponPose(form,{p.x,p.y,p.z},false,0);float x=(owner==0?-1.f:1.f)*(5+10*scale);
            goals[i]={{pose.x*scale,pose.y*scale,pose.z*scale},1,{x,2,0},275,{0,float(owner),1},80,{.4f,.8f,.1f,1},{4,40,50,0},{},{},{}};
            input[i]={};input[i].position={x+pose.x*scale,2+pose.y*scale,pose.z*scale};input[i].color={.4f,.8f,.1f,1};}
        g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);f(7,15);
        bind();g.Dispatch("MorphKeys",128);for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);f(7,0);
        auto assignment=g.Read<Entry>(morph);float worst=0;
        for(int swing=-1;swing<3;++swing){float duration=swing<0?1.f:LifeWeaponDuration(form);int frames=int(std::ceil(duration*120));
            for(int frame=0;frame<=frames;++frame){float age=std::min(duration,float(frame)/120);
                LifeBody body;body.action=form;body.age=age+float(swing+1);body.mass=100*scale*scale*(scale<=1?scale:1);body.weapon.active=swing>=0;body.weapon.age=age;body.weapon.previousAge=std::max(0.f,age-1.f/120);
                for(UINT i=0;i<count;++i){auto p=cloud[i%(count/2)];auto pose=LifeWeaponPose(body,{p.x,p.y,p.z},age);auto velocity=LifeWeaponVelocity(body,{p.x,p.y,p.z},1.f/120);
                    goals[i].local={pose.x*scale,pose.y*scale,pose.z*scale};goals[i].reserved={velocity.x,velocity.y,velocity.z,-1};goals[i].omega=swing<0?32.f:80.f;}
                g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);bind();g.Dispatch("SavePrevious",count/64);grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcForce",count/64);g.Dispatch("WriteBack",count/64);
                for(int iteration=0;iteration<3;++iteration){grid();g.Dispatch("CalcDensity",count/64);g.Dispatch("CalcDeltaP",count/64);g.Dispatch("ApplyDeltaP",count/64);}grid(true);g.Dispatch("UpdateVelocity",count/64);g.Dispatch("WriteBack",count/64);
                if(swing>=0&&frame%12==0){auto positions=g.Read<Particle>(particles);double error=0;
                    for(UINT i=0;i<count;++i){auto p=positions[i];auto t=goals[assignment[8192+i].target];float x=p.position.x-t.origin.x-t.local.x,y=p.position.y-t.origin.y-t.local.y,z=p.position.z-t.origin.z-t.local.z;
                        Require(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&p.color.w>.9f,"persistent weapon motion loses fluid particles");error+=x*x+y*y+z*z;}
                    worst=std::max(worst,float(std::sqrt(error/count))/scale);}
            }
        }
        printf("weapon motion form=%d scale=%.3f normalized RMS=%.3f\n",int(form),scale,worst);
        Require(worst<1.25f,"GPU fluid cannot follow the repeated production weapon motions");return worst;
    };
    auto hammerMotion=weaponMotion(Game::Chrono::LifeAction::Hammer,shape);
    Engine::FluidShape::Mesh swordMesh("Resources/Models/SlimeLife/sword.obj");auto swordCloud=Engine::FluidShape::Sample(&swordMesh,count/2);
    auto swordMotion=weaponMotion(Game::Chrono::LifeAction::Sword,swordCloud);
    float smallScale=scale;scale=Game::Chrono::LifeScale(Game::Chrono::LifeMaxMass);auto largeSwordMotion=weaponMotion(Game::Chrono::LifeAction::Sword,swordCloud);scale=smallScale;
    printf("PASS persistent weapons GPU: vertical hammer and repeated horizontal sword sweeps, max normalized RMS hammer=%.3f sword=%.3f grown_sword=%.3f\n",hammerMotion,swordMotion,largeSwordMotion);
    // 16 owners exercise both the late-game 12 children and the upper renderer slot.
    // The old owner<<30 key silently aliases owners 4, 8 and 12 with owner 0.
    g.Kernel("MorphMeasure",true);
    for(UINT i=0;i<count;++i){UINT owner=i/(count/16),local=i%(count/16);auto point=rest[local];float x=float(owner)*12;
        goals[i]={{point.x,point.y,point.z},1,{x,2,0},275,{0,float(owner),1},20,{.4f,.8f,.1f,1},{4,40,50,0},{},{},{}};
        input[i]={};input[i].position={x+point.x,2+point.y,point.z};input[i].color={.4f,.8f,.1f,1};}
    g.Unbind();g.context->UpdateSubresource(targets.resource.Get(),0,nullptr,goals.data(),0,0);g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);f(7,65535);
    bind();g.Dispatch("MorphKeys",128);for(UINT width=2;width<=8192;width*=2)for(UINT stride=width/2;stride;stride/=2){cb[1]=stride;cb[2]=width;bind();g.Dispatch("MorphSort",128);}cb[1]=cb[2]=0;bind();g.Dispatch("MorphAssign",count/64);
    auto many=g.Read<Entry>(morph);std::fill(seen.begin(),seen.end(),false);
    for(UINT i=0;i<count;++i){auto e=many[8192+i];Require(e.target<count&&!seen[e.target],"many-owner assignment is not bijective");seen[e.target]=true;
        Require(e.owner==i/(count/16)&&e.target/(count/16)==e.owner,"late-game owner aliases another body");}
    bind();g.Dispatch("MorphMeasure",1);auto measured=g.Read<Entry>(morph);
    for(UINT owner=0;owner<16;++owner)Require(measured[16000+owner].start.z==count/16,"upper-owner shape readiness missing");
    puts("PASS growth GPU: 16 independent owners, full spatial keys, unique targets and readiness for upper slots");

}
