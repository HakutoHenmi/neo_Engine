// Render production raymarch + decoration shaders together, including actual
// density anchoring and scene/fluid depth, rather than testing ideal UV samples.
void TestSlimeDecorationPreview(Gpu& g) {
    const UINT size=64,w=512,h=384;
    auto density=g.MakeTexture(size),occupancy=g.MakeTexture(size/4);
    std::vector<HALF> solid((size/4)*(size/4)*(size/4)*4);
    for(size_t i=0;i<solid.size();i+=4)solid[i]=XMConvertFloatToHalf(1);
    g.context->UpdateSubresource(occupancy.resource.Get(),0,nullptr,solid.data(),size/4*8,size/4*size/4*8);
    D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> color,background,depth,surface;
    Check(g.device->CreateTexture2D(&td,nullptr,&color));
    std::vector<UINT> pixels(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)pixels[y*w+x]=y<h/2?0xff302921:((x/28+y/28)%2?0xff423b32:0xff302c26);
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,w*h*4};Check(g.device->CreateTexture2D(&td,&data,&background));
    td.Format=DXGI_FORMAT_R32_FLOAT;std::vector<float> depths(w*h,1);data={depths.data(),w*4,w*h*4};
    Check(g.device->CreateTexture2D(&td,&data,&depth));Check(g.device->CreateTexture2D(&td,nullptr,&surface));
    ComPtr<ID3D11RenderTargetView> colorRtv,surfaceRtv;
    Check(g.device->CreateRenderTargetView(color.Get(),nullptr,&colorRtv));Check(g.device->CreateRenderTargetView(surface.Get(),nullptr,&surfaceRtv));
    ComPtr<ID3D11ShaderResourceView> bgSrv,depthSrv,surfaceSrv;
    Check(g.device->CreateShaderResourceView(background.Get(),nullptr,&bgSrv));Check(g.device->CreateShaderResourceView(depth.Get(),nullptr,&depthSrv));Check(g.device->CreateShaderResourceView(surface.Get(),nullptr,&surfaceSrv));
    D3D11_TEXTURE2D_DESC skyDesc{};skyDesc.Width=skyDesc.Height=16;skyDesc.MipLevels=1;skyDesc.ArraySize=6;skyDesc.SampleDesc.Count=1;
    skyDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;skyDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;skyDesc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    std::array<std::vector<BYTE>,6> sky;std::array<D3D11_SUBRESOURCE_DATA,6> skyData{};
    for(UINT face=0;face<6;++face){sky[face].resize(16*16*4);
        for(UINT y=0;y<16;++y)for(UINT x=0;x<16;++x){auto pixel=sky[face].data()+(y*16+x)*4;
            float light=face==2?.8f:face==3?.12f:.25f+.3f*float(15-y)/15;
            if(face==4&&x>7&&x<12&&y>2&&y<8)light=.95f;
            pixel[0]=BYTE(light*220);pixel[1]=BYTE(light*240);pixel[2]=BYTE(light*255);pixel[3]=255;}
        skyData[face]={sky[face].data(),16*4,16*16*4};}
    ComPtr<ID3D11Texture2D> environment;ComPtr<ID3D11ShaderResourceView> environmentSrv;
    Check(g.device->CreateTexture2D(&skyDesc,skyData.data(),&environment));Check(g.device->CreateShaderResourceView(environment.Get(),nullptr,&environmentSrv));
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","RaymarchPS","ps_5_0");
    auto decorationVS=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayVS","vs_5_0"),decorationPS=g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayPS","ps_5_0");
    ComPtr<ID3D11VertexShader> vs,dvs;ComPtr<ID3D11PixelShader> ps,dps;
    Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    Check(g.device->CreateVertexShader(decorationVS->GetBufferPointer(),decorationVS->GetBufferSize(),nullptr,&dvs));Check(g.device->CreatePixelShader(decorationPS->GetBufferPointer(),decorationPS->GetBufferSize(),nullptr,&dps));
    struct Camera {XMFLOAT4X4 view,projection,vp;XMFLOAT3 position;float time;} camera{};
    camera.position={0,5.f,-17};camera.time=2;
    auto view=XMMatrixLookAtLH(XMLoadFloat3(&camera.position),XMVectorSet(0,.7f,0,1),XMVectorSet(0,1,0,0));
    auto projection=XMMatrixPerspectiveFovLH(1.13f,float(w)/h,.1f,100);
    XMStoreFloat4x4(&camera.view,view);XMStoreFloat4x4(&camera.projection,projection);XMStoreFloat4x4(&camera.vp,view*projection);
    auto cameraCb=g.Constant(sizeof(camera)),volumeCb=g.Constant(sizeof(VolumeCB));g.context->UpdateSubresource(cameraCb.Get(),0,nullptr,&camera,0,0);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&raster,&rs));
    D3D11_DEPTH_STENCIL_DESC depthState{};ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&depthState,&ds));
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=TRUE;blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> alpha;Check(g.device->CreateBlendState(&blend,&alpha));
    for(int stage=0;stage<3;++stage){
        VolumeCB cb{};cb.origin={-6.4f,-4,-6.4f};cb.cell=.2f;cb.size[0]=cb.size[1]=cb.size[2]=size;cb.iso=.25f;cb.playerDecoration=1;
        cb.colors[0]={.06f,.78f,.29f,1};cb.playerForward={0,0,-1};cb.playerSlope={0,1,0};
        cb.playerRadii=stage==0?XMFLOAT3{2.55f,1.8f,2.55f}:stage==1?XMFLOAT3{3.4f,1.f,3.4f}:XMFLOAT3{4.2f,.66f,4.2f};
        cb.playerMotion=stage>0?1.f:0;
        std::vector<HALF> samples(size*size*size*4);
        for(UINT z=0;z<size;++z)for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){
            XMFLOAT3 world{cb.origin.x+(x+.5f)*cb.cell,cb.origin.y+(y+.5f)*cb.cell,cb.origin.z+(z+.5f)*cb.cell};
            float stretch=1+.55f*cb.playerMotion,along=-world.z/stretch;
            float width=1+cb.playerMotion*.26f*std::clamp(along/cb.playerRadii.z,-1.f,1.f),flatten=std::sqrt(stretch)/width;
            float nx=world.x*flatten/cb.playerRadii.x,ny=world.y*flatten/cb.playerRadii.y,nz=along/cb.playerRadii.z;
            float extent=std::sqrt(nx*nx+ny*ny+nz*nz);
            float value=world.y>=.04f?std::max(0.f,cb.iso+(1-extent)*4):0;
            samples[((z*size+y)*size+x)*4]=XMConvertFloatToHalf(value);
        }
        g.Unbind();g.context->UpdateSubresource(density.resource.Get(),0,nullptr,samples.data(),size*8,size*size*8);
        g.context->UpdateSubresource(volumeCb.Get(),0,nullptr,&cb,0,0);
        ID3D11Buffer* constants[]={cameraCb.Get(),volumeCb.Get()};g.context->VSSetConstantBuffers(0,2,constants);g.context->PSSetConstantBuffers(0,2,constants);
        auto sampler=g.sampler.Get();g.context->VSSetSamplers(0,1,&sampler);
        ID3D11ShaderResourceView* inputs[]={density.srv.Get(),bgSrv.Get(),depthSrv.Get(),environmentSrv.Get(),nullptr,nullptr,nullptr,occupancy.srv.Get()};
        g.context->PSSetShaderResources(0,8,inputs);
        ID3D11RenderTargetView* targets[]={colorRtv.Get(),surfaceRtv.Get()};g.context->OMSetRenderTargets(2,targets,nullptr);
        g.context->OMSetDepthStencilState(ds.Get(),0);g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->RSSetState(rs.Get());
        D3D11_VIEWPORT viewport{0,0,float(w),float(h),0,1};g.context->RSSetViewports(1,&viewport);
        g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->Draw(3,0);
        g.Unbind();
        // Compare pixels outside the gel against the undecorated render: only escaped bubbles can change them.
        D3D11_TEXTURE2D_DESC baseDesc; color->GetDesc(&baseDesc);baseDesc.BindFlags=0;baseDesc.Usage=D3D11_USAGE_STAGING;baseDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> baseRead,depthRead;Check(g.device->CreateTexture2D(&baseDesc,nullptr,&baseRead));
        baseDesc.Format=DXGI_FORMAT_R32_FLOAT;Check(g.device->CreateTexture2D(&baseDesc,nullptr,&depthRead));
        g.context->CopyResource(baseRead.Get(),color.Get());g.context->CopyResource(depthRead.Get(),surface.Get());
        std::vector<BYTE> basePixels(w*h*4);std::vector<float> bodyDepth(w*h);
        D3D11_MAPPED_SUBRESOURCE baseMap{};Check(g.context->Map(baseRead.Get(),0,D3D11_MAP_READ,0,&baseMap));
        for(UINT y=0;y<h;++y)std::memcpy(basePixels.data()+y*w*4,static_cast<const BYTE*>(baseMap.pData)+y*baseMap.RowPitch,w*4);
        g.context->Unmap(baseRead.Get(),0);Check(g.context->Map(depthRead.Get(),0,D3D11_MAP_READ,0,&baseMap));
        for(UINT y=0;y<h;++y)std::memcpy(bodyDepth.data()+y*w,static_cast<const BYTE*>(baseMap.pData)+y*baseMap.RowPitch,w*sizeof(float));
        g.context->Unmap(depthRead.Get(),0);
        inputs[6]=surfaceSrv.Get();g.context->VSSetShaderResources(0,8,inputs);g.context->PSSetShaderResources(0,8,inputs);
        auto target=colorRtv.Get();g.context->OMSetRenderTargets(1,&target,nullptr);g.context->OMSetBlendState(alpha.Get(),nullptr,UINT_MAX);
        g.context->VSSetShader(dvs.Get(),nullptr,0);g.context->PSSetShader(dps.Get(),nullptr,0);g.context->DrawInstanced(6,250,0,0);g.Unbind();
        const wchar_t* names[]={L"tests/out/slime-decorations-rest.png",L"tests/out/slime-decorations-stretch.png",L"tests/out/slime-decorations-flat.png"};
        WritePng(g.device.Get(),g.context.Get(),color.Get(),names[stage]);
        D3D11_TEXTURE2D_DESC readDesc; color->GetDesc(&readDesc);readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;Check(g.device->CreateTexture2D(&readDesc,nullptr,&readback));g.context->CopyResource(readback.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(g.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        UINT leftEye=0,rightEye=0,escapedPixels=0;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto pixel=static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch+x*4;
            if(pixel[0]>210&&pixel[1]>210&&pixel[2]>70&&pixel[2]<200){if(x<w/2)++leftEye;else ++rightEye;}
            auto base=basePixels.data()+(y*w+x)*4;
            int difference=std::max({std::abs(int(pixel[0])-int(base[0])),std::abs(int(pixel[1])-int(base[1])),std::abs(int(pixel[2])-int(base[2]))});
            if(bodyDepth[y*w+x]<=0&&difference>=6)++escapedPixels;}
        g.context->Unmap(readback.Get(),0);Require(leftEye>20&&rightEye>20,"rigid eye pair vanished in the combined density/depth render");
        Require(escapedPixels>12,"escaped bubbles are not visible outside the slime at gameplay camera distance");
        printf("PASS decoration render pose %d: both eyes visible (%u/%u pixels), preview saved\n",stage,leftEye,rightEye);
        printf("PASS escaped bubbles pose %d: %u visible pixels outside the body at 17-unit camera distance\n",stage,escapedPixels);
    }
}
