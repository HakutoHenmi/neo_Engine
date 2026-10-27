// Production kernels and combined sampling across a fitted near-volume edge.
void TestFarWater(Gpu& g){
    constexpr UINT size=64;
    std::vector<Particle> water;
    // One physical layer at the solver's rest spacing; no invented water mass.
    for(int z=0;z<=30;++z)for(int x=0;x<=198;++x){
        auto p=MakeParticle(float(x)*.2f,.22f,float(z)*.2f,1.1f);p.density=125;p.velocity={12,0,0};p.pad={.2f,.22f,1};water.push_back(p);
    }
    // A detached spray sample must not become a broad far-water surface.
    auto drop=MakeParticle(46,3,3,1.1f);water.push_back(drop);
    // A few landed splash particles far from the sheet used to grow a puddle
    // through the per-particle amplitude floor. They carry only .032 volume.
    for(int i=0;i<4;++i){
        auto p=MakeParticle(24.f+float(i%2)*.2f,.22f,15.f+float(i/2)*.2f,1.1f);p.density=125;p.pad={.2f,.22f,1};water.push_back(p);
    }
    std::sort(water.begin(),water.end(),[](const Particle& a,const Particle& b){return Hash(a.position)<Hash(b.position);});
    UINT count=UINT(water.size());std::vector<UINT> counts(65536),offsets(65536),indices(count);
    for(UINT i=0;i<count;++i){++counts[Hash(water[i].position)];indices[i]=i;}
    for(UINT i=1;i<65536;++i)offsets[i]=offsets[i-1]+counts[i-1];
    auto particles=g.MakeBuffer(count,64,water.data()),shapes=g.MakeBuffer(count,64);
    auto ids=g.MakeBuffer(count,4,indices.data()),gridCounts=g.MakeBuffer(65536,4,counts.data()),gridOffsets=g.MakeBuffer(65536,4,offsets.data());
    auto accum=g.MakeBuffer(size*size*size,16),momentum=g.MakeBuffer(size*size*size,16);
    auto settings=g.Constant(sizeof(VolumeCB));
    std::array<Texture,4> fine,coarse;for(auto& field:fine)field=g.MakeTexture(size);for(auto& field:coarse)field=g.MakeTexture(size);
    for(auto name:{"BuildShapes","ClearVolume","SplatDensity","SplatFarWater","ResolveDensity","SmoothDensity","BuildOccupancy"})g.Kernel(name);
    VolumeCB nearSettings{};nearSettings.origin={-8,-2,-8};nearSettings.cell=.25f;nearSettings.iso=.25f;
    nearSettings.size[0]=nearSettings.size[1]=nearSettings.size[2]=size;nearSettings.count=count;
    g.Unbind();g.context->UpdateSubresource(settings.Get(),0,nullptr,&nearSettings,0,0);auto settingsBuffer=settings.Get();g.context->CSSetConstantBuffers(1,1,&settingsBuffer);
    ID3D11ShaderResourceView* grid[]={particles.srv.Get(),ids.srv.Get(),gridCounts.srv.Get(),gridOffsets.srv.Get()};g.context->CSSetShaderResources(0,4,grid);
    auto shapeOutput=shapes.uav.Get();g.context->CSSetUnorderedAccessViews(2,1,&shapeOutput,nullptr);g.Dispatch("BuildShapes",(count+63)/64);
    auto built=g.Read<Shape>(shapes);
    for(UINT i=0;i<count;++i)if(water[i].pad.z>0){
        float determinant=1;for(const auto& row:built[i].r)determinant*=std::sqrt(row.x*row.x+row.y*row.y+row.z*row.z);
        float volume=built[i].info.x/determinant/(315.f/(64.f*3.14159265f));
        Require(std::abs(volume-.008f)<.0001f,"near water sheet invents mass as particles spread apart");
    }
    auto build=[&](bool distant){
        VolumeCB cb=nearSettings;if(distant){cb.origin={-8,-4,-8};cb.cell=1;}
        auto& fields=distant?coarse:fine;
        auto bind=[&](){g.Unbind();g.context->UpdateSubresource(settings.Get(),0,nullptr,&cb,0,0);auto c=settings.Get();g.context->CSSetConstantBuffers(1,1,&c);
            auto ps=particles.srv.Get();g.context->CSSetShaderResources(0,1,&ps);
            ID3D11UnorderedAccessView* u[]={accum.uav.Get(),momentum.uav.Get(),shapes.uav.Get(),fields[0].uav.Get(),fields[2].uav.Get()};g.context->CSSetUnorderedAccessViews(0,5,u,nullptr);};
        bind();g.Dispatch("ClearVolume",size/4,size/4,size/4);g.Dispatch(distant?"SplatFarWater":"SplatDensity",(count+63)/64);g.Dispatch("ResolveDensity",size/4,size/4,size/4);
        UINT passes=distant?2:3;
        for(UINT pass=0;pass<passes;++pass){cb.axis=distant?(pass==0?0.f:2.f):float(pass);bind();auto dst=fields[1-pass%2].uav.Get();g.context->CSSetUnorderedAccessViews(3,1,&dst,nullptr);auto src=fields[pass%2].srv.Get();g.context->CSSetShaderResources(4,1,&src);g.Dispatch("SmoothDensity",size/4,size/4,size/4);}
        bind();auto dst=fields[3].uav.Get();g.context->CSSetUnorderedAccessViews(3,1,&dst,nullptr);auto src=fields[distant?0:1].srv.Get();g.context->CSSetShaderResources(4,1,&src);g.Dispatch("BuildOccupancy",size/16,size/16,size/16);
    };
    build(false);build(true);
    auto field=g.Read(coarse[0]);double densityIntegral=0;float splashPeak=0;
    for(UINT z=0;z<size;++z)for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){
        const auto& cell=field[x+size*(y+size*z)];Require(cell.x==0&&cell.y==0,"coarse water contaminated slime or honey phase");
        densityIntegral+=cell.z*.4;float wx=-8+(x+.5f),wz=-8+(z+.5f);
        if(wx>21&&wx<28&&wz>12&&wz<19)splashPeak=std::max(splashPeak,cell.z);
    }
    printf("far sheet integrated water=%.3f expected=%.3f detached splash peak=%.4f\n",densityIntegral,double(water.size()-1)*.008,splashPeak);
    Require(splashPeak<nearSettings.iso,"a few landed splash particles turn into a detached far puddle");
    Require(densityIntegral<double(water.size()-1)*.008*1.35,"far sheet creates water volume when enlarging its kernels");
    std::vector<XMFLOAT4> points;for(int i=0;i<=288;++i)points.push_back({2.f+float(i)*.125f,.22f,3.3f,0});
    points.push_back({46,3,3,0});points.push_back({20,.22f,18,0});
    points.push_back({80,.22f,3,1});
    auto queries=g.MakeBuffer(UINT(points.size()),16,points.data()),results=g.MakeBuffer(UINT(points.size()),16);
    struct Includes:ID3DInclude{
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID* data,UINT* bytes)override{
            for(auto root:{"tests/","Resources/shaders/"}){std::ifstream file(std::string(root)+name,std::ios::binary|std::ios::ate);if(!file)continue;
                *bytes=UINT(file.tellg());auto contents=new char[*bytes];file.seekg(0);file.read(contents,*bytes);*data=contents;return S_OK;}return E_FAIL;}
        HRESULT __stdcall Close(LPCVOID data)override{delete[] static_cast<const char*>(data);return S_OK;}
    } includes;
    auto code=g.Compile(L"tests/FarWaterTests.hlsl","ProbeFarWater","cs_5_0",&includes);Check(g.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&g.kernels["ProbeFarWater"]));
    struct CoarseCB{XMFLOAT3 origin;float cell;UINT size[3];UINT enabled;} farSettings{{-8,-4,-8},1,{size,size,size},1};
    auto farConstants=g.Constant(sizeof(farSettings));g.context->UpdateSubresource(farConstants.Get(),0,nullptr,&farSettings,0,0);
    g.Unbind();g.context->UpdateSubresource(settings.Get(),0,nullptr,&nearSettings,0,0);auto c=settings.Get();g.context->CSSetConstantBuffers(1,1,&c);c=farConstants.Get();g.context->CSSetConstantBuffers(2,1,&c);
    auto srv=fine[1].srv.Get();g.context->CSSetShaderResources(0,1,&srv);srv=fine[2].srv.Get();g.context->CSSetShaderResources(9,1,&srv);
    ID3D11ShaderResourceView* remote[]={coarse[0].srv.Get(),coarse[2].srv.Get(),coarse[3].srv.Get(),queries.srv.Get()};g.context->CSSetShaderResources(10,4,remote);
    auto u=results.uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&u,nullptr);g.Dispatch("ProbeFarWater",(UINT(points.size())+63)/64);
    auto sampled=g.Read<XMFLOAT4>(results);float minimum=100,maxFlowError=0;UINT seam=0;
    for(size_t i=0;i<points.size()-3;++i){minimum=std::min(minimum,sampled[i].x);maxFlowError=std::max(maxFlowError,std::abs(sampled[i].w-12));
        if(!std::isfinite(sampled[i].x)||sampled[i].x<nearSettings.iso){printf("hole x=%.3f density=%.4f near=%.3f far=%.3f flow=%.3f\n",points[i].x,sampled[i].x,sampled[i].y,sampled[i].z,sampled[i].w);Require(false,"water surface has a hole at the near/far boundary or in the far sheet");}
        if(sampled[i].y>0&&sampled[i].y<1)++seam;}
    printf("sampling min=%.4f seam=%u flowError=%.4f\n",minimum,seam,maxFlowError);
    Require(seam>0&&maxFlowError<.1f,"overlap or water velocity is discontinuous");
    Require(sampled[sampled.size()-3].x==0&&sampled[sampled.size()-2].x==0,"isolated spray or empty space was filled with water");
    Require(std::abs(sampled.back().w-.008f)<.0001f&&sampled.back().x<.25f,"out-of-volume fallback renders a whole sheet kernel as one distant puddle");
    // Do not leak new slots into existing rendering tests.
    ID3D11ShaderResourceView* empty[14]{};g.context->CSSetShaderResources(0,14,empty);g.context->PSSetShaderResources(0,14,empty);g.context->VSSetShaderResources(0,14,empty);
    ID3D11Buffer* none=nullptr;g.context->CSSetConstantBuffers(2,1,&none);
    printf("PASS far water: %zu continuous samples, %u overlap samples, minimum density %.3f, velocity error %.4f; spray, empty space and other phases excluded; fallback volume %.4f\n",points.size()-3,seam,minimum,maxFlowError,sampled.back().w);
}
