#include "../Engine/ShadowCaster.h"
#include "RtLightingDenoiseTests.inl"
void TestVegetationShadow(Gpu& g){
    for(auto shader:{"SlimeNoFaceNoDepth","EnergyBeam","EnergyCylinder","DomainGlow","LiquidTrail"})
        Require(!Engine::CastsOpaqueShadow(shader),"transparent effect casts an opaque shadow");
    for(auto shader:{"Default","HordeArmor","EnvironmentSurface","MeadowGrass","MeadowGround","Slime"})
        Require(Engine::CastsOpaqueShadow(shader),"solid caster lost its shadow");
    g.Compile(L"Resources/shaders/VegetationShadow.hlsl","VSMain","vs_5_0");
    g.Compile(L"Resources/shaders/VegetationShadow.hlsl","VSInstanced","vs_5_0");
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0");
    auto psCode=g.Compile(L"Resources/shaders/VegetationShadow.hlsl","PSMain","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));
    Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    XMFLOAT4 mask[]={{1,1,1,0},{1,1,1,.41f},{1,1,1,.43f},{1,1,1,1}};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=2;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> alpha,depth,readback;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_SUBRESOURCE_DATA initial{mask,2*16,4*16};Check(g.device->CreateTexture2D(&desc,&initial,&alpha));Check(g.device->CreateShaderResourceView(alpha.Get(),nullptr,&srv));
    desc.Width=128;desc.Height=96;desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Check(g.device->CreateTexture2D(&desc,nullptr,&depth));Check(g.device->CreateDepthStencilView(depth.Get(),nullptr,&dsv));
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(g.device->CreateTexture2D(&desc,nullptr,&readback));
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_LESS;
    ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dd,&ds));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&rd,&rs));
    g.Unbind();g.context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);g.context->OMSetRenderTargets(0,nullptr,dsv.Get());
    g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(rs.Get());auto view=srv.Get();auto sampler=g.sampler.Get();
    g.context->PSSetShaderResources(0,1,&view);g.context->PSSetSamplers(0,1,&sampler);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);
    D3D11_VIEWPORT viewport{0,0,128,96,0,1};g.context->RSSetViewports(1,&viewport);g.context->IASetInputLayout(nullptr);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.context->Draw(3,0);g.Unbind();g.context->CopyResource(readback.Get(),depth.Get());D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m));
    auto sample=[&](UINT x,UINT y){return reinterpret_cast<const float*>(static_cast<const BYTE*>(m.pData)+y*m.RowPitch)[x];};
    bool cutout=sample(32,24)==1&&sample(96,24)==1&&sample(32,72)==0&&sample(96,72)==0;
    g.context->Unmap(readback.Get(),0);Require(cutout,"vegetation shadow ignores alpha cutoff");
    printf("PASS grass depth cutout at alpha 0 / .41 / .43 / 1; transparent effect caster policy\n");
}

void TestFluidShadowCamera(Gpu& g){
    auto code=g.Compile(L"Resources/shaders/GPUFluidShadowVS.hlsl","main","vs_5_0");
    ComPtr<ID3D11VertexShader> vs;Check(g.device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs));
    const char* pixel="float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0,float type:TEXCOORD1):SV_TARGET{clip(.25-dot(uv-.5,uv-.5));return 1;}";
    ComPtr<ID3DBlob> psCode;Check(D3DCompile(pixel,strlen(pixel),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&psCode,nullptr));
    ComPtr<ID3D11PixelShader> ps;Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    Particle p{};p.density=100;p.color={1,1,1,1};auto particles=g.MakeBuffer(1,sizeof(Particle),&p);
    struct Frame{XMFLOAT4X4 view,proj,vp;XMFLOAT3 camera;float time=0;} cb{};
    auto lightView=XMMatrixLookAtLH(XMVectorSet(-5,8,-6,1),XMVectorZero(),XMVectorSet(0,1,0,0));
    auto proj=XMMatrixOrthographicLH(8,8,.1f,30);XMStoreFloat4x4(&cb.proj,proj);XMStoreFloat4x4(&cb.vp,lightView*proj);
    auto constants=g.Constant(sizeof(cb));
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=128;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32_FLOAT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;ComPtr<ID3D11RenderTargetView> rtv;
    Check(g.device->CreateTexture2D(&d,nullptr,&target));Check(g.device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(g.device->CreateTexture2D(&d,nullptr,&readback));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&rd,&rs));D3D11_DEPTH_STENCIL_DESC dd{};ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dd,&ds));
    std::vector<float> baseline;UINT changed=0,coverage=0;
    for(int angle=0;angle<5;++angle){
        g.Unbind();XMStoreFloat4x4(&cb.view,XMMatrixRotationRollPitchYaw(angle*.27f,angle*.65f,0));
        g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);auto b=constants.Get();g.context->VSSetConstantBuffers(0,1,&b);
        auto srv=particles.srv.Get();g.context->VSSetShaderResources(2,1,&srv);auto out=rtv.Get();g.context->OMSetRenderTargets(1,&out,nullptr);
        float clear[4]{};g.context->ClearRenderTargetView(out,clear);g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(rs.Get());
        D3D11_VIEWPORT viewport{0,0,128,128,0,1};g.context->RSSetViewports(1,&viewport);g.context->IASetInputLayout(nullptr);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->DrawInstanced(6,1,0,0);g.Unbind();
        g.context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m));
        std::vector<float> pixels(128*128);for(UINT y=0;y<128;++y)memcpy(pixels.data()+y*128,static_cast<const BYTE*>(m.pData)+y*m.RowPitch,128*4);g.context->Unmap(readback.Get(),0);
        if(!angle){baseline=pixels;for(float v:pixels)coverage+=v>.5f;}
        else for(size_t i=0;i<pixels.size();++i)changed+=pixels[i]!=baseline[i];
    }
    printf("Fluid shadow camera rotation: covered pixels %u, changed pixels %u\n",coverage,changed);
    Require(coverage>100,"fluid shadow disappeared");Require(!changed,"fluid shadow changes when only camera orientation changes");
}

void TestGraphicsPost(Gpu& g){
    TestRtLightingDenoise(g);
    TestVegetationShadow(g);
    TestFluidShadowCamera(g);
    for(auto path:{L"Resources/shaders/DefaultPS.hlsl",L"Resources/shaders/InstancedObjPS.hlsl",L"Resources/shaders/EnhancedTerrainPS.hlsl"})g.Compile(path,"main","ps_5_0");
    g.Compile(L"Resources/shaders/TemporalPrepare.hlsl","main","ps_5_0");
    g.Compile(L"Resources/shaders/TemporalObject.hlsl","VSMain","vs_5_0");
    g.Compile(L"Resources/shaders/TemporalObject.hlsl","PSMain","ps_5_0");
    g.Compile(L"Resources/shaders/EnvironmentSurfacePS.hlsl","main","ps_5_0");
    constexpr UINT w=128,h=96;
    struct Post {float data[28]{};XMFLOAT4X4 inverse{},previous{};} cb;
    static_assert(sizeof(Post)==240);
    cb.data[8]=8;cb.data[9]=0;cb.data[11]=.1f;cb.data[12]=100;cb.data[13]=1.f/w;cb.data[14]=1.f/h;cb.data[15]=-1000;cb.data[21]=1.05f;
    auto projection=XMMatrixPerspectiveFovLH(1.13f,float(w)/h,.1f,100);
    XMStoreFloat4x4(&cb.inverse,XMMatrixInverse(nullptr,projection));XMStoreFloat4x4(&cb.previous,projection);
    cb.data[22]=XMVectorGetX(projection.r[0]);cb.data[23]=XMVectorGetY(projection.r[1]);
    std::vector<XMFLOAT4> colors(w*h);std::vector<float> depths(w*h),fluid(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
        float z=y<h/4?20.f:(x<w/2?8.f:9.f);depths[y*w+x]=100.f/99.9f-10.f/99.9f/z;
        colors[y*w+x]={.03f+float(x)/w*.3f,.06f+float(y)/h*.35f,.08f,.99f};
        if(x>40&&x<53&&y>30&&y<43)colors[y*w+x]={8,6,3,1};
    }
    struct Image{ComPtr<ID3D11Texture2D> resource;ComPtr<ID3D11ShaderResourceView> srv;};
    auto texture=[&](UINT width,UINT height,DXGI_FORMAT format,const void* pixels,UINT pitch){Image result;
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{pixels,pitch,pitch*height};Check(g.device->CreateTexture2D(&d,&initial,&result.resource));Check(g.device->CreateShaderResourceView(result.resource.Get(),nullptr,&result.srv));return result;};
    auto scene=texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),w*16);
    auto depth=texture(w,h,DXGI_FORMAT_R32_FLOAT,depths.data(),w*4),fluidDepth=texture(w,h,DXGI_FORMAT_R32_FLOAT,fluid.data(),w*4);
    ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromFilename(L"Resources/Textures/ColorGrading/CinematicLUT.png",nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
    ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame));UINT lutW,lutH;Check(frame->GetSize(&lutW,&lutH));Require(lutW==256&&lutH==16,"LUT dimensions do not match shader");
    ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter));Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<BYTE> pixels(lutW*lutH*4);Check(converter->CopyPixels(nullptr,lutW*4,UINT(pixels.size()),pixels.data()));
    auto lut=texture(lutW,lutH,DXGI_FORMAT_R8G8B8A8_UNORM,pixels.data(),lutW*4);
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"Resources/shaders/ChronoFocusPost.hlsl","main","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    D3D11_TEXTURE2D_DESC output{};output.Width=w;output.Height=h;output.MipLevels=output.ArraySize=1;output.SampleDesc.Count=1;output.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;output.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> result,staging;ComPtr<ID3D11RenderTargetView> rtv;Check(g.device->CreateTexture2D(&output,nullptr,&result));Check(g.device->CreateRenderTargetView(result.Get(),nullptr,&rtv));
    output.BindFlags=0;output.Usage=D3D11_USAGE_STAGING;output.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(g.device->CreateTexture2D(&output,nullptr,&staging));
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&raster,&rs));
    D3D11_DEPTH_STENCIL_DESC dd{};ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dd,&ds));
    struct Target{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11RenderTargetView> rtv;UINT width,height;};
    auto createTarget=[&](UINT width,UINT height){Target target{};target.width=width;target.height=height;D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(g.device->CreateTexture2D(&desc,nullptr,&target.texture));Check(g.device->CreateShaderResourceView(target.texture.Get(),nullptr,&target.srv));Check(g.device->CreateRenderTargetView(target.texture.Get(),nullptr,&target.rtv));return target;};
    auto pixelShader=[&](const wchar_t* path,const char* entry){auto code=g.Compile(path,entry,"ps_5_0");ComPtr<ID3D11PixelShader> shader;Check(g.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));return shader;};
    auto downPs=pixelShader(L"Resources/shaders/HdrBloom.hlsl","Downsample"),upPs=pixelShader(L"Resources/shaders/HdrBloom.hlsl","Upsample"),aoPs=pixelShader(L"Resources/shaders/Gtao.hlsl","main");
    auto passConstants=g.Constant(32);
    auto renderPass=[&](Target& target,ID3D11PixelShader* shader,ID3D11ShaderResourceView* source,ID3D11ShaderResourceView* low,const float* data){
        g.Unbind();g.context->UpdateSubresource(passConstants.Get(),0,nullptr,data,0,0);auto buffer=passConstants.Get();g.context->PSSetConstantBuffers(0,1,&buffer);
        ID3D11ShaderResourceView* views[]={source,low};g.context->PSSetShaderResources(0,2,views);auto sampler=g.sampler.Get();g.context->PSSetSamplers(0,1,&sampler);
        auto outputRtv=target.rtv.Get();g.context->OMSetRenderTargets(1,&outputRtv,nullptr);g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(rs.Get());
        D3D11_VIEWPORT vp{0,0,float(target.width),float(target.height),0,1};g.context->RSSetViewports(1,&vp);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(shader,nullptr,0);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->Draw(3,0);g.Unbind();};
    std::vector<Target> down,up;for(UINT i=0;i<6;++i){down.push_back(createTarget(std::max(1u,w>>(i+1)),std::max(1u,h>>(i+1))));if(i<5)up.push_back(createTarget(std::max(1u,w>>(i+1)),std::max(1u,h>>(i+1))));}
    auto bloomPass=[&](){for(UINT i=0;i<6;++i){float data[8]={1.f/(i?down[i-1].width:w),1.f/(i?down[i-1].height:h),i==0?1.f:0.f};renderPass(down[i],downPs.Get(),i?down[i-1].srv.Get():scene.srv.Get(),nullptr,data);}
        for(int i=4;i>=0;--i){auto& low=i==4?down[i+1]:up[i+1];float data[8]={1.f/low.width,1.f/low.height};renderPass(up[i],upPs.Get(),down[i].srv.Get(),low.srv.Get(),data);}};
    bloomPass();auto ao=createTarget(w,h);
    auto aoPass=[&](bool includeFluid){float data[8]={.1f,100,cb.data[22],cb.data[23],1.f/w,1.f/h,includeFluid?1.f:0.f,3.6f};renderPass(ao,aoPs.Get(),depth.srv.Get(),fluidDepth.srv.Get(),data);};
    auto readTarget=[&](Target& target){D3D11_TEXTURE2D_DESC desc{};target.texture->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;Check(g.device->CreateTexture2D(&desc,nullptr,&readback));g.context->CopyResource(readback.Get(),target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE map{};Check(g.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map));std::vector<XMFLOAT4> data(target.width*target.height);for(UINT y=0;y<target.height;++y)std::memcpy(data.data()+y*target.width,static_cast<const BYTE*>(map.pData)+y*map.RowPitch,target.width*16);g.context->Unmap(readback.Get(),0);return data;};
    auto boundsPs=pixelShader(L"tests/ShadowFrustumTests.hlsl","main");auto bounds=createTarget(w,h);float boundsData[8]{};
    renderPass(bounds,boundsPs.Get(),nullptr,nullptr,boundsData);auto boundsPixels=readTarget(bounds);
    Require(boundsPixels[(h/2)*w].x<.01f&&boundsPixels[(h/2)*w+w/2].x>.99f,"shadow volume has a hard boundary or loses central shadows");
    float previous=0;for(UINT x=0;x<12;++x){float value=boundsPixels[(h/2)*w+x].x;Require(value>=previous&&value-previous<.16f,"shadow volume edge is discontinuous");previous=value;}
    printf("PASS shadow volume edge fades continuously; central shadow strength preserved\n");
    auto radiance=readTarget(down[0]);float brightest=0;for(auto c:radiance)brightest=std::max(brightest,c.x);Require(brightest>1,"HDR bloom clipped radiance to SDR");
    printf("PASS HDR pyramid preserves radiance above 1: %.3f\n",brightest);
    aoPass(false);auto aoPixels=readTarget(ao);Require(aoPixels[70*w+15].x>.97f,"GTAO self-occludes a flat surface");float darkest=1;for(auto c:aoPixels){Require(std::isfinite(c.x)&&c.x>=0&&c.x<=1,"invalid GTAO visibility");darkest=std::min(darkest,c.x);}Require(darkest<.95f,"GTAO failed to occlude depth contact");
    printf("PASS GTAO planar visibility %.3f contact %.3f\n",aoPixels[70*w+15].x,darkest);
    // Smooth tilted planes must stay unoccluded at native, DLSS and half-size
    // output resolutions, including slopes in both screen directions.
    for(float scale:{1.f,1.5f,.5f})for(float slope:{-.55f,.55f,1.2f}){
        std::vector<float> planeDepth(w*h);
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
            float rayY=(1-2*(float(y)+.5f)/h)/cb.data[23],rayX=(2*(float(x)+.5f)/w-1)/cb.data[22];
            float z=10/(1+slope*rayY+.25f*rayX);planeDepth[y*w+x]=100.f/99.9f-10.f/99.9f/z;
        }
        auto plane=texture(w,h,DXGI_FORMAT_R32_FLOAT,planeDepth.data(),w*4);
        auto scaledAo=createTarget(UINT(w*scale),UINT(h*scale));float planeData[8]={.1f,100,cb.data[22],cb.data[23],1.f/scaledAo.width,1.f/scaledAo.height,0,3.6f};
        renderPass(scaledAo,aoPs.Get(),plane.srv.Get(),fluidDepth.srv.Get(),planeData);
        auto planeAo=readTarget(scaledAo);float planeMin=1,planeMax=0;
        for(UINT y=6;y<scaledAo.height-6;++y)for(UINT x=6;x<scaledAo.width-6;++x){float v=planeAo[y*scaledAo.width+x].x;planeMin=std::min(planeMin,v);planeMax=std::max(planeMax,v);}
        printf("GTAO tilted-plane scale %.2f slope %.2f visibility %.3f..%.3f\n",scale,slope,planeMin,planeMax);
        Require(planeMin>.96f&&planeMax-planeMin<.02f,"GTAO creates stripes on a smooth plane");
    }
    auto constants=g.Constant(sizeof(cb));std::vector<XMFLOAT4> baseline(w*h);
    std::vector<XMFLOAT2> rtMask(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)rtMask[y*w+x]={x<w/2?0.f:1.f,y<h/4?20.f:(x<w/2?8.f:9.f)};
    auto rt=texture(w,h,DXGI_FORMAT_R32G32_FLOAT,rtMask.data(),w*8);
    // Samples from another depth surface must be rejected without a black halo.
    std::vector<XMFLOAT4> rejectedAo(w*h/4,XMFLOAT4{0,50,0,1});
    std::vector<XMFLOAT2> rejectedRt(w*h/4,XMFLOAT2{0,50});
    auto aoOtherSurface=texture(w/2,h/2,DXGI_FORMAT_R32G32B32A32_FLOAT,rejectedAo.data(),w/2*16);
    auto rtOtherSurface=texture(w/2,h/2,DXGI_FORMAT_R32G32_FLOAT,rejectedRt.data(),w/2*8);
    std::vector<XMFLOAT4> reflectionData(w*h/4),indirectData(w*h/4),wrongSurface(w*h/4);
    for(UINT y=0;y<h/2;++y)for(UINT x=0;x<w/2;++x){float z=y<h/8?20.f:(x<w/4?8.f:9.f);reflectionData[y*w/2+x]={.06f,.015f,.004f,z};indirectData[y*w/2+x]={.01f,.06f,.01f,z};wrongSurface[y*w/2+x]={.06f,.015f,.004f,-z};}
    auto reflection=texture(w/2,h/2,DXGI_FORMAT_R32G32B32A32_FLOAT,reflectionData.data(),w/2*16),indirect=texture(w/2,h/2,DXGI_FORMAT_R32G32B32A32_FLOAT,indirectData.data(),w/2*16);
    auto fluidReflection=texture(w/2,h/2,DXGI_FORMAT_R32G32B32A32_FLOAT,wrongSurface.data(),w/2*16);
    const char* names[]={"display transform","HDR bloom","LUT","GTAO","camera motion blur","DoF","lens flare","fluid depth","RT visibility composite","visibility depth rejection","RT reflection composite","RT indirect composite","RT lighting depth rejection","RT lighting material rejection"};
    for(int mode=0;mode<14;++mode){
        cb.data[10]=cb.data[16]=cb.data[17]=cb.data[18]=cb.data[19]=cb.data[20]=cb.data[24]=cb.data[25]=cb.data[26]=cb.data[27]=0;
        if(mode==1)cb.data[16]=.3f;if(mode==2)cb.data[18]=1;if(mode==3)cb.data[19]=1;
        if(mode==4)cb.data[20]=1;if(mode==5)cb.data[10]=1;if(mode==6)cb.data[17]=.1f;
        if(mode==8)cb.data[25]=.35f;
        if(mode==9){cb.data[19]=1;cb.data[25]=.35f;}
        if(mode==10||mode==12||mode==13)cb.data[26]=1;
        if(mode==11)cb.data[27]=1;
        if(mode==7){cb.data[24]=1;cb.data[19]=1;for(UINT y=32;y<64;++y)for(UINT x=40;x<88;++x)fluid[y*w+x]=5;
            g.context->UpdateSubresource(fluidDepth.resource.Get(),0,nullptr,fluid.data(),w*4,w*h*4);}
        aoPass(mode==7);
        XMStoreFloat4x4(&cb.previous,mode==4?XMMatrixTranslation(.5f,0,0)*projection:projection);
        g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);auto buffer=constants.Get();g.context->PSSetConstantBuffers(0,1,&buffer);
        ID3D11ShaderResourceView* views[]={scene.srv.Get(),lut.srv.Get(),mode==9?aoOtherSurface.srv.Get():ao.srv.Get(),depth.srv.Get(),up[0].srv.Get(),fluidDepth.srv.Get(),mode==9?rtOtherSurface.srv.Get():rt.srv.Get(),mode==12?aoOtherSurface.srv.Get():mode==13?fluidReflection.srv.Get():reflection.srv.Get(),indirect.srv.Get()};g.context->PSSetShaderResources(0,9,views);
        auto sampler=g.sampler.Get();g.context->PSSetSamplers(0,1,&sampler);auto target=rtv.Get();g.context->OMSetRenderTargets(1,&target,nullptr);g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(rs.Get());
        D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};g.context->RSSetViewports(1,&viewport);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->Draw(3,0);g.Unbind();
        g.context->CopyResource(staging.Get(),result.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));double difference=0;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto value=reinterpret_cast<const XMFLOAT4*>(static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch)[x];
            Require(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z)&&value.x>=0&&value.x<=1.001f,"post shader produced invalid output");
            if(mode==0)baseline[y*w+x]=value;else{auto base=baseline[y*w+x];difference+=std::abs(value.x-base.x)+std::abs(value.y-base.y)+std::abs(value.z-base.z);}}
        g.context->Unmap(staging.Get(),0);
        if(mode==9||mode==12||mode==13)Require(difference<.001,"rejected lighting/visibility samples create halos");
        else Require(mode==0||difference>.01,"graphics switch had no visible effect");
        printf("PASS graphics %s: finite display output, delta %.3f\n",names[mode],difference);
    }
    // A steep perspective plane used to reject every other half-resolution row.
    // Exercise the actual HDR reconstruction shader, without temporal filtering.
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){float z=400.f/(float(y)+.5f);depths[y*w+x]=1000.f/999.9f-100.f/999.9f/z;colors[y*w+x]={.1f,.1f,.1f,1};}
    for(UINT y=0;y<h/2;++y)for(UINT x=0;x<w/2;++x)reflectionData[y*w/2+x]={.08f,0,0,400.f/(2*y+1.5f)};
    g.context->UpdateSubresource(depth.resource.Get(),0,nullptr,depths.data(),w*4,w*h*4);g.context->UpdateSubresource(scene.resource.Get(),0,nullptr,colors.data(),w*16,w*h*16);
    g.context->UpdateSubresource(reflection.resource.Get(),0,nullptr,reflectionData.data(),w/2*16,w*h*4);
    cb.data[12]=1000;cb.data[24]=cb.data[27]=0;cb.data[26]=1;
    auto compositePs=pixelShader(L"Resources/shaders/RtLightingComposite.hlsl","main");g.Unbind();
    g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);auto buffer=constants.Get();g.context->PSSetConstantBuffers(0,1,&buffer);
    ID3D11ShaderResourceView* lightingViews[]={scene.srv.Get(),nullptr,nullptr,depth.srv.Get(),nullptr,fluidDepth.srv.Get(),nullptr,reflection.srv.Get(),indirect.srv.Get()};g.context->PSSetShaderResources(0,9,lightingViews);
    auto target=rtv.Get();g.context->OMSetRenderTargets(1,&target,nullptr);g.context->PSSetShader(compositePs.Get(),nullptr,0);g.context->Draw(3,0);g.Unbind();g.context->CopyResource(staging.Get(),result.Get());
    D3D11_MAPPED_SUBRESOURCE plane{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&plane));float lo=1,hi=0;
    for(UINT y=4;y<h-4;++y)for(UINT x=4;x<w-4;++x){float value=reinterpret_cast<const XMFLOAT4*>(static_cast<const BYTE*>(plane.pData)+y*plane.RowPitch)[x].x;lo=std::min(lo,value);hi=std::max(hi,value);}
    g.context->Unmap(staging.Get(),0);Require(lo>.179f&&hi<.181f,"RT reconstruction creates horizontal bands on steep perspective ground");printf("PASS RT HDR steep-plane reconstruction %.4f..%.4f\n",lo,hi);
}
