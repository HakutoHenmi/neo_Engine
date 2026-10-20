// Reconstruct and compose an actual grown hammer, including the particle LOD.
void TestHammerSurface(Gpu& g,bool grown=false,Game::Chrono::LifeAction form=Game::Chrono::LifeAction::Hammer,float massOverride=0){
    using namespace Game::Chrono;
    constexpr UINT columns=176,rows=64,count=6600,w=384,h=384;
    const char* name=grown?"grown slime":form==LifeAction::Hammer?"hammer":form==LifeAction::Sword?"sword":"plane";
    Engine::FluidShape::Mesh mesh(form==LifeAction::Sword?"Resources/Models/SlimeLife/sword.obj":form==LifeAction::Plane?"Resources/Models/SlimeLife/plane.obj":"Resources/Models/SlimeLife/hammer.obj");auto cloud=Engine::FluidShape::Sample(grown?nullptr:&mesh,count);
    float mass=grown?600.f:form==LifeAction::Hammer?232.f:form==LifeAction::Sword?304.f:496.f;
    if(massOverride>0)mass=massOverride;
    std::vector<Particle> input(count);XMFLOAT3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};float scale=LifeScale(mass);
    for(UINT i=0;i<count;++i){auto p=cloud[i];auto q=grown?Vec{p.x,p.y,p.z}*scale:LifeWeaponPose(form,{p.x,p.y,p.z},false,0)*scale;
        input[i]=MakeParticle(q.x,q.y,q.z,0);input[i].density=LifeDensity(mass,count,count);input[i].pad={1000,input[i].density,grown?1.f:-2.f};input[i].color={.08f,.16f,.9f,1};
        lo={std::min(lo.x,q.x),std::min(lo.y,q.y),std::min(lo.z,q.z)};hi={std::max(hi.x,q.x),std::max(hi.y,q.y),std::max(hi.z,q.z)};}
    auto unsorted=g.MakeBuffer(count,64,input.data());
    std::sort(input.begin(),input.end(),[](const Particle& a,const Particle& b){return Hash(a.position)<Hash(b.position);});
    std::vector<UINT> counts(65536),offsets(65536),indices(count);for(UINT i=0;i<count;++i){++counts[Hash(input[i].position)];indices[i]=i;}
    for(UINT i=1;i<65536;++i)offsets[i]=offsets[i-1]+counts[i-1];
    auto particles=g.MakeBuffer(count,64,input.data()),original=g.MakeBuffer(count,4,indices.data()),gridCount=g.MakeBuffer(65536,4,counts.data()),gridOffset=g.MakeBuffer(65536,4,offsets.data());
    auto accum=g.MakeBuffer(columns*rows*columns,16),momentum=g.MakeBuffer(columns*rows*columns,16),shapes=g.MakeBuffer(count,64),results=g.MakeBuffer(count,16);
    auto texture=[&](UINT width,UINT height){Texture t;t.size=width;D3D11_TEXTURE3D_DESC desc{};desc.Width=desc.Depth=width;desc.Height=height;desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        Check(g.device->CreateTexture3D(&desc,nullptr,&t.resource));Check(g.device->CreateShaderResourceView(t.resource.Get(),nullptr,&t.srv));Check(g.device->CreateUnorderedAccessView(t.resource.Get(),nullptr,&t.uav));return t;};
    Texture fields[]={texture(columns,rows),texture(columns,rows),texture(columns,rows),texture(columns/4,rows/4)};
    VolumeCB cb{};cb.cell=Engine::FluidVolumeDomain::StableCell(Engine::FluidVolumeDomain::FitCell(hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,columns,rows,.8f*scale,.2f*scale),0);
    Require(Engine::FluidVolumeDomain::StableCell(cb.cell*.97f,cb.cell)==cb.cell,"small motion resizes the fluid grid every frame");
    Require(Engine::FluidVolumeDomain::StableCell(cb.cell*1.1f,cb.cell)>=cb.cell*1.1f,"stable grid fails to grow around a moving weapon");
    for(float step:{-.51f,.12f,.49f,.51f}){float offset=step*cb.cell;
        float base=Engine::FluidVolumeDomain::Origin(lo.x,hi.x,cb.cell,columns),moved=Engine::FluidVolumeDomain::Origin(lo.x+offset,hi.x+offset,cb.cell,columns);
        Require(std::abs(moved-base-offset)<.00002f,"moving body changes volume sampling phase in voxel-size jumps");}
    cb.origin={Engine::FluidVolumeDomain::Origin(lo.x,hi.x,cb.cell,columns),Engine::FluidVolumeDomain::Origin(lo.y,hi.y,cb.cell,rows),Engine::FluidVolumeDomain::Origin(lo.z,hi.z,cb.cell,columns)};
    cb.size[0]=cb.size[2]=columns;cb.size[1]=rows;cb.count=count;cb.iso=.25f;cb.dt=1.f/60;cb.colors[0]={.08f,.16f,.9f,1};cb.axis=-.4f*scale;
    cb.playerMorph={grown?1.f:-float(form),scale,float(count),1};cb.playerSlope.y=scale;cb.playerFaceCamera=0;
    cb.playerBodyCenter={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};cb.playerRadii={(hi.x-lo.x)*.5f,(hi.y-lo.y)*.5f,(hi.z-lo.z)*.5f};
    if(grown){cb.playerBodyCenter={0,0,0};cb.playerRadii={2.55f*scale,1.8f*scale,2.55f*scale};cb.playerLiquidCore={0,.77f*scale,0,1};}
    auto settings=g.Constant(sizeof(cb));
    for(auto entry:{"ClearVolume","BuildShapes","SplatDensity","ResolveDensity","SmoothDensity","BuildOccupancy"})g.Kernel(entry);
    auto bind=[&](UINT source,UINT destination){g.Unbind();g.context->UpdateSubresource(settings.Get(),0,nullptr,&cb,0,0);auto c=settings.Get();g.context->CSSetConstantBuffers(1,1,&c);
        ID3D11ShaderResourceView* srvs[]={particles.srv.Get(),original.srv.Get(),gridCount.srv.Get(),gridOffset.srv.Get(),fields[source].srv.Get()};g.context->CSSetShaderResources(0,5,srvs);
        ID3D11UnorderedAccessView* uavs[]={accum.uav.Get(),momentum.uav.Get(),shapes.uav.Get(),fields[destination].uav.Get(),nullptr};g.context->CSSetUnorderedAccessViews(0,5,uavs,nullptr);};
    bind(1,0);g.Dispatch("ClearVolume",columns/4,rows/4,columns/4);g.Dispatch("BuildShapes",(count+63)/64);g.Dispatch("SplatDensity",(count+63)/64);
    UINT worstCells=0;for(auto shape:g.Read<Shape>(shapes)){auto p=XMFLOAT3{shape.r[0].w,shape.r[1].w,shape.r[2].w};float center[]={p.x,p.y,p.z},origin[]={cb.origin.x,cb.origin.y,cb.origin.z};
        for(int axis=0;axis<3;++axis){float loCell=std::floor((center[axis]-shape.info.y-origin[axis])/cb.cell-.5f),hiCell=std::ceil((center[axis]+shape.info.y-origin[axis])/cb.cell-.5f);worstCells=std::max(worstCells,UINT(hiCell-loCell+1));}}
    printf("%s mass=%.0f scale=%.3f: splat bound=%u cells/axis\n",name,mass,scale,worstCells);Require(worstCells<=11,"growth makes reconstruction work per particle unbounded");
    if(grown){
        auto before=g.Read<Shape>(shapes);
        for(UINT i=0;i<count;++i)input[i].density*=i%2?.4f:1.6f;
        g.context->UpdateSubresource(particles.resource.Get(),0,nullptr,input.data(),0,0);bind(1,0);g.Dispatch("BuildShapes",(count+63)/64);auto after=g.Read<Shape>(shapes);
        float anisotropy=1;for(UINT i=0;i<count;++i){
            Require(std::abs(before[i].info.x-after[i].info.x)<.00001f,"physical density fluctuations pulse the grown skin");
            float loAxis=1e9f,hiAxis=0;for(int j=0;j<3;++j){auto r=after[i].r[j];float length=std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z);loAxis=std::min(loAxis,length);hiAxis=std::max(hiAxis,length);}anisotropy=std::max(anisotropy,hiAxis/loAxis);
        }
        printf("grown surface: maximum kernel anisotropy=%.3f, sample amplitude unchanged by density noise\n",anisotropy);
        Require(anisotropy<1.15f,"grown skin retains rapidly rotating narrow kernels");
    }
    bind(1,0);auto velocity=fields[2].uav.Get();g.context->CSSetUnorderedAccessViews(4,1,&velocity,nullptr);g.Dispatch("ResolveDensity",columns/4,rows/4,columns/4);
    for(UINT axis=0;axis<3;++axis){cb.axis=float(axis);bind(axis%2,1-axis%2);g.Dispatch("SmoothDensity",columns/4,rows/4,columns/4);}bind(1,3);g.Dispatch("BuildOccupancy",columns/16,rows/16,columns/16);
    TestVolumeShadow(g,cb,fields[1],fields[3],name);
    struct Camera {XMFLOAT4X4 view,projection,vp;XMFLOAT3 position;float time;} camera{};auto cameraCb=g.Constant(sizeof(camera));g.context->UpdateSubresource(cameraCb.Get(),0,nullptr,&camera,0,0);
    struct Includes:ID3DInclude {
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID* data,UINT* bytes)override{
            for(auto root:{"tests/","Resources/shaders/"}){std::ifstream file(std::string(root)+name,std::ios::binary|std::ios::ate);if(!file)continue;
                *bytes=static_cast<UINT>(file.tellg());auto contents=new char[*bytes];file.seekg(0);file.read(contents,*bytes);*data=contents;return S_OK;}return E_FAIL;}
        HRESULT __stdcall Close(LPCVOID data)override{delete[] static_cast<const char*>(data);return S_OK;}
    } includes;
    auto probe=g.Compile(L"tests/FluidSurfaceTests.hlsl","SurfaceCoverage","cs_5_0",&includes);Check(g.device->CreateComputeShader(probe->GetBufferPointer(),probe->GetBufferSize(),nullptr,&g.kernels["SurfaceCoverage"]));
    auto decorationProbe=g.Compile(L"tests/FluidSurfaceTests.hlsl","MorphDecorations","cs_5_0",&includes);Check(g.device->CreateComputeShader(decorationProbe->GetBufferPointer(),decorationProbe->GetBufferSize(),nullptr,&g.kernels["MorphDecorations"]));
    g.Unbind();ID3D11Buffer* constants[]={cameraCb.Get(),settings.Get()};g.context->CSSetConstantBuffers(0,2,constants);ID3D11ShaderResourceView* geometry[]={shapes.srv.Get(),particles.srv.Get()};g.context->CSSetShaderResources(4,2,geometry);
    auto uav=results.uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);g.Dispatch("SurfaceCoverage",(count+63)/64);
    UINT fallback=0;float minimumWeight=1;for(auto result:g.Read<XMFLOAT4>(results)){if(result.y>0)++fallback;minimumWeight=std::min(minimumWeight,result.x);}
    printf("hammer surface: cell=%.4f min coverage=%.4f fallback kernels=%u/%u\n",cb.cell,minimumWeight,fallback,count);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> color,background,depth,surface;Check(g.device->CreateTexture2D(&desc,nullptr,&color));std::vector<UINT> backdrop(w*h,0xffc9c5bf);D3D11_SUBRESOURCE_DATA data{backdrop.data(),w*4,w*h*4};Check(g.device->CreateTexture2D(&desc,&data,&background));
    desc.Format=DXGI_FORMAT_R32_FLOAT;std::vector<float> depths(w*h,1);data={depths.data(),w*4,w*h*4};Check(g.device->CreateTexture2D(&desc,&data,&depth));Check(g.device->CreateTexture2D(&desc,nullptr,&surface));
    ComPtr<ID3D11RenderTargetView> colorRtv,surfaceRtv;ComPtr<ID3D11ShaderResourceView> bgSrv,depthSrv,surfaceSrv;
    Check(g.device->CreateRenderTargetView(color.Get(),nullptr,&colorRtv));Check(g.device->CreateRenderTargetView(surface.Get(),nullptr,&surfaceRtv));Check(g.device->CreateShaderResourceView(background.Get(),nullptr,&bgSrv));Check(g.device->CreateShaderResourceView(depth.Get(),nullptr,&depthSrv));Check(g.device->CreateShaderResourceView(surface.Get(),nullptr,&surfaceSrv));
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","RaymarchPS","ps_5_0"),farVsCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayVS","vs_5_0"),farPsCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayPS","ps_5_0");
    ComPtr<ID3D11VertexShader> vs,farVs;ComPtr<ID3D11PixelShader> ps,farPs;
    ComPtr<ID3D11VertexShader> bubbleVs,eyeVs;auto bubbleCode=g.Compile(L"tests/FluidSurfaceTests.hlsl","BubbleVS","vs_5_0",&includes),eyeCode=g.Compile(L"tests/FluidSurfaceTests.hlsl","EyeVS","vs_5_0",&includes);
    Check(g.device->CreateVertexShader(bubbleCode->GetBufferPointer(),bubbleCode->GetBufferSize(),nullptr,&bubbleVs));Check(g.device->CreateVertexShader(eyeCode->GetBufferPointer(),eyeCode->GetBufferSize(),nullptr,&eyeVs));
    Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));Check(g.device->CreateVertexShader(farVsCode->GetBufferPointer(),farVsCode->GetBufferSize(),nullptr,&farVs));Check(g.device->CreatePixelShader(farPsCode->GetBufferPointer(),farPsCode->GetBufferSize(),nullptr,&farPs));
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&raster,&rs));D3D11_DEPTH_STENCIL_DESC dsDesc{};ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dsDesc,&ds));
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=TRUE;blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;ComPtr<ID3D11BlendState> alpha;Check(g.device->CreateBlendState(&blend,&alpha));
    auto read=[&](){g.Unbind();D3D11_TEXTURE2D_DESC d; color->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;Check(g.device->CreateTexture2D(&d,nullptr,&stage));g.context->CopyResource(stage.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));std::vector<BYTE> pixels(w*h*4);for(UINT y=0;y<h;++y)std::memcpy(pixels.data()+y*w*4,static_cast<const BYTE*>(m.pData)+y*m.RowPitch,w*4);g.context->Unmap(stage.Get(),0);return pixels;};
    auto decorationSampler=g.sampler.Get();g.context->VSSetSamplers(0,1,&decorationSampler);
    UINT changedPixels=0;bool farColor=false;
    for(int pose=0;pose<4;++pose){
        camera.position=pose==0?XMFLOAT3{0,4,20}:pose==1?XMFLOAT3{6,11,18}:pose==2?XMFLOAT3{0,1,14}:XMFLOAT3{0,4,-20};
        if(form==LifeAction::Sword&&!grown)camera.position=pose==0?XMFLOAT3{0,5,40}:pose==1?XMFLOAT3{10,16,35}:pose==2?XMFLOAT3{0,2,35}:XMFLOAT3{0,5,-35};
        float framing=massOverride>0?scale/LifeScale(grown?600.f:form==LifeAction::Hammer?232.f:form==LifeAction::Sword?304.f:496.f):1;
        camera.position={camera.position.x*framing,camera.position.y*framing,camera.position.z*framing};
        auto view=XMMatrixLookAtLH(XMLoadFloat3(&camera.position),XMVectorSet(0,4*framing,0,1),XMVectorSet(0,1,0,0)),projection=XMMatrixPerspectiveFovLH(.9f,float(w)/h,.1f,200);XMStoreFloat4x4(&camera.view,view);XMStoreFloat4x4(&camera.projection,projection);XMStoreFloat4x4(&camera.vp,view*projection);
        if(pose==3){cb.origin.x+=100;cb.colors[0]={.04f,.9f,.07f,1};}
        g.Unbind();g.context->UpdateSubresource(cameraCb.Get(),0,nullptr,&camera,0,0);g.context->UpdateSubresource(settings.Get(),0,nullptr,&cb,0,0);g.context->VSSetConstantBuffers(0,2,constants);g.context->PSSetConstantBuffers(0,2,constants);
        ID3D11ShaderResourceView* inputs[]={fields[1].srv.Get(),bgSrv.Get(),depthSrv.Get(),nullptr,shapes.srv.Get(),particles.srv.Get(),nullptr,fields[3].srv.Get()};g.context->PSSetShaderResources(0,8,inputs);
        ID3D11RenderTargetView* targets[]={colorRtv.Get(),surfaceRtv.Get()};g.context->OMSetRenderTargets(2,targets,nullptr);g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(rs.Get());g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};g.context->RSSetViewports(1,&viewport);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->Draw(3,0);
        auto base=read();inputs[6]=surfaceSrv.Get();g.context->VSSetShaderResources(0,8,inputs);g.context->PSSetShaderResources(0,8,inputs);auto target=colorRtv.Get();g.context->OMSetRenderTargets(1,&target,nullptr);g.context->OMSetBlendState(alpha.Get(),nullptr,UINT_MAX);g.context->VSSetShader(farVs.Get(),nullptr,0);g.context->PSSetShader(farPs.Get(),nullptr,0);g.context->DrawInstanced(6,count,0,0);auto composed=read();
        const wchar_t* images[]={L"tests/out/hammer-surface-front.png",L"tests/out/hammer-surface-above.png",L"tests/out/hammer-surface-below.png",L"tests/out/hammer-surface-far-green.png"};if(!grown)WritePng(g.device.Get(),g.context.Get(),color.Get(),images[pose]);
        double green=0,blue=0;UINT visible=0,bodyPixels=0;for(UINT i=0;i<w*h;++i){int difference=0;
            for(UINT channel=0;channel<3;++channel)difference=std::max(difference,std::abs(int(base[i*4+channel])-int(composed[i*4+channel])));
            if(std::abs(int(composed[i*4])-191)>10||std::abs(int(composed[i*4+1])-197)>10||std::abs(int(composed[i*4+2])-201)>10)++bodyPixels;
            if(difference>2){if(pose<3)++changedPixels;else {green+=composed[i*4+1];blue+=composed[i*4+2];++visible;}}}
        Require(bodyPixels>1000,"hammer surface disappeared from the composed render");
        if(pose==3)farColor=visible>1000&&green>blue*1.4;
        if(pose<3){
            cb.playerDecoration=1;g.context->UpdateSubresource(settings.Get(),0,nullptr,&cb,0,0);
            g.context->CSSetConstantBuffers(0,2,constants);auto density=fields[1].srv.Get(),stable=unsorted.srv.Get();g.context->CSSetShaderResources(0,1,&density);g.context->CSSetShaderResources(8,1,&stable);
            g.context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);g.Dispatch("MorphDecorations",4);auto decorations=g.Read<XMFLOAT4>(results);
            UINT head=0,handle=0;for(UINT i=0;i<224;++i)if(decorations[i].w>.01f){if(decorations[i].y>(lo.y+hi.y)*.5f)++head;else ++handle;}
            printf("%s decorations view=%d: upper=%u lower=%u eyes=%.2f/%.2f\n",name,pose,head,handle,decorations[248].w,decorations[249].w);
            Require(head>15&&handle>5,"interior bubbles do not fill both upper and lower liquid");
            Require(decorations[248].w>.9f&&decorations[249].w>.9f,"grown or transformed eyes are buried/missing");
            auto drawDecorations=[&](UINT first,UINT number){g.Unbind();g.context->VSSetShaderResources(0,8,inputs);g.context->VSSetShaderResources(8,1,&stable);g.context->PSSetShaderResources(0,8,inputs);
                g.context->OMSetRenderTargets(1,&target,nullptr);g.context->VSSetShader(first==248?eyeVs.Get():bubbleVs.Get(),nullptr,0);g.context->DrawInstanced(6,number,0,0);return read();};
            auto bubbles=drawDecorations(0,224);auto decorated=drawDecorations(248,2);
            UINT cavityPixels=0;std::array<UINT,2> eyePixels{};
            XMFLOAT2 eyeScreens[2];for(int eye=0;eye<2;++eye){auto p=decorations[size_t(248+eye)];XMFLOAT3 clip;XMStoreFloat3(&clip,XMVector3TransformCoord(XMVectorSet(p.x,p.y,p.z,1),view*projection));eyeScreens[eye]={(clip.x+1)*w*.5f,(1-clip.y)*h*.5f};}
            for(UINT i=0;i<w*h;++i){int difference=0;for(UINT c=0;c<3;++c)difference=std::max(difference,std::abs(int(bubbles[i*4+c])-int(composed[i*4+c])));
                if(difference>2)++cavityPixels;
                auto distance=[&](int eye){return std::pow(float(i%w)-eyeScreens[eye].x,2.f)+std::pow(float(i/w)-eyeScreens[eye].y,2.f);};
                if(decorated[i*4]>180&&decorated[i*4+1]>180&&decorated[i*4+2]<200&&std::abs(int(decorated[i*4])-int(bubbles[i*4]))>8)++eyePixels[distance(0)<distance(1)?0:1];}
            printf("rendered cavities=%u eye pixels=%u/%u\n",cavityPixels,eyePixels[0],eyePixels[1]);
            Require(cavityPixels>30&&eyePixels[0]+eyePixels[1]>16&&(pose!=0||(eyePixels[0]>8&&eyePixels[1]>8)),"bubbles or front-facing eyes disappeared from the composed image");
            if(form==LifeAction::Plane&&!grown)Require(decorations[248].z>hi.z-scale*.7f&&decorations[249].z>hi.z-scale*.7f,"plane eyes are on the tail instead of the nose");
            const wchar_t* hammerImages[]={L"tests/out/hammer-liquid-front.png",L"tests/out/hammer-liquid-above.png",L"tests/out/hammer-liquid-below.png"};
            const wchar_t* grownImages[]={L"tests/out/grown-liquid-front.png",L"tests/out/grown-liquid-above.png",L"tests/out/grown-liquid-below.png"};
            const wchar_t* swordImages[]={L"tests/out/sword-liquid-front.png",L"tests/out/sword-liquid-above.png",L"tests/out/sword-liquid-below.png"};
            const wchar_t* planeImages[]={L"tests/out/plane-liquid-front.png",L"tests/out/plane-liquid-above.png",L"tests/out/plane-liquid-below.png"};
            WritePng(g.device.Get(),g.context.Get(),color.Get(),grown?grownImages[pose]:form==LifeAction::Sword?swordImages[pose]:form==LifeAction::Plane?planeImages[pose]:hammerImages[pose]);
            cb.playerDecoration=0;
        }
    }
    printf("hammer render: overlapping LOD pixels=%u far current tint=%d\n",changedPixels,farColor);
    Require(fallback==0&&minimumWeight>.999f&&changedPixels==0,"fitted hammer surface switches to overlapping particle LODs");Require(farColor,"distant fluid ignores the current material tint");
    puts("PASS hammer surface: full reconstructed coverage, no particle overlay from three views, current tint shared by distant LOD");
}
