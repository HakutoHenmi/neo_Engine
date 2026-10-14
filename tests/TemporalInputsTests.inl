// Compare production temporal inputs against independent CPU projection at
// every input pixel, including a rotating camera and subpixel jitter.
void TestTemporalInputs(Gpu& g){
    constexpr UINT w=48,h=32;
    struct Image{ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11RenderTargetView> rtv;};
    auto image=[&](DXGI_FORMAT format,const void* pixels,UINT pitch,bool target=false){Image out;D3D11_TEXTURE2D_DESC d{};
        d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=format;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(target?D3D11_BIND_RENDER_TARGET:0);
        D3D11_SUBRESOURCE_DATA initial{pixels,pitch,pitch*h};Check(g.device->CreateTexture2D(&d,pixels?&initial:nullptr,&out.texture));
        Check(g.device->CreateShaderResourceView(out.texture.Get(),nullptr,&out.srv));
        if(target)Check(g.device->CreateRenderTargetView(out.texture.Get(),nullptr,&out.rtv));return out;};
    std::vector<float> raw(w*h),fluid(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){float z=4+float(x+y)*.3f;
        raw[y*w+x]=100.f/99.9f-10.f/99.9f/z;if(x>w/3&&x<w*2/3)fluid[y*w+x]=3;}
    auto depth=image(DXGI_FORMAT_R32_FLOAT,raw.data(),w*4),water=image(DXGI_FORMAT_R32_FLOAT,fluid.data(),w*4);
    auto motion=image(DXGI_FORMAT_R32G32_FLOAT,nullptr,0,true),prepared=image(DXGI_FORMAT_R32_FLOAT,nullptr,0,true);
    XMFLOAT4 velocity{2,0,0,0};D3D11_TEXTURE3D_DESC vd{};vd.Width=vd.Height=vd.Depth=vd.MipLevels=1;vd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;vd.Usage=D3D11_USAGE_DEFAULT;vd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA vi{&velocity,16,16};ComPtr<ID3D11Texture3D> volume;ComPtr<ID3D11ShaderResourceView> velocitySrv;
    Check(g.device->CreateTexture3D(&vd,&vi,&volume));Check(g.device->CreateShaderResourceView(volume.Get(),nullptr,&velocitySrv));
    struct Constants{XMFLOAT4X4 inverse,previous,current;float nearPlane,farPlane,fluid,dt;XMFLOAT3 origin;float cell;XMFLOAT3 dimensions;float pad;} cb{};
    static_assert(sizeof(Constants)==240);cb.nearPlane=.1f;cb.farPlane=100;cb.cell=1;cb.dimensions={1,1,1};
    auto constants=g.Constant(sizeof(cb));auto projection=XMMatrixPerspectiveFovLH(1.13f,float(w)/h,.1f,100);
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"Resources/shaders/TemporalPrepare.hlsl","main","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;Check(g.device->CreateRasterizerState(&rd,&raster));
    D3D11_DEPTH_STENCIL_DESC dd{};ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dd,&ds));
    auto read=[&](Image& source,UINT components){D3D11_TEXTURE2D_DESC d;source.texture->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Check(g.device->CreateTexture2D(&d,nullptr,&staging));g.context->CopyResource(staging.Get(),source.texture.Get());
        D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<float> result(w*h*components);
        for(UINT y=0;y<h;++y)memcpy(result.data()+y*w*components,static_cast<const char*>(m.pData)+y*m.RowPitch,w*components*4);
        g.context->Unmap(staging.Get(),0);return result;};
    for(int mode=0;mode<4;++mode){XMFLOAT4X4 jittered;XMStoreFloat4x4(&jittered,projection);jittered._31+=.37f*2/w;jittered._32-=.23f*2/h;
        auto inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&jittered));auto previous=mode==0?projection:XMMatrixRotationY(.07f)*XMMatrixTranslation(.3f,0,0)*projection;
        XMStoreFloat4x4(&cb.inverse,inverse);XMStoreFloat4x4(&cb.current,projection);XMStoreFloat4x4(&cb.previous,previous);
        cb.fluid=mode>=2?1.f:0.f;cb.dt=mode==2?.02f:0.f;g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);
        auto b=constants.Get();g.context->PSSetConstantBuffers(0,1,&b);ID3D11ShaderResourceView* inputs[]={depth.srv.Get(),water.srv.Get(),velocitySrv.Get()};g.context->PSSetShaderResources(0,3,inputs);
        ID3D11RenderTargetView* targets[]={motion.rtv.Get(),prepared.rtv.Get()};g.context->OMSetRenderTargets(2,targets,nullptr);
        g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->OMSetDepthStencilState(ds.Get(),0);g.context->RSSetState(raster.Get());
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};g.context->RSSetViewports(1,&vp);g.context->IASetInputLayout(nullptr);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->Draw(3,0);g.Unbind();
        auto vectors=read(motion,2),depths=read(prepared,1);float error=0;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){UINT i=y*w+x;float z=.1f*100/(100-raw[i]*99.9f),d=raw[i];bool wet=cb.fluid>.5f&&fluid[i]>0&&fluid[i]<z;
            if(wet)d=100.f/99.9f-10.f/99.9f/fluid[i];
            auto world=XMVector4Transform(XMVectorSet(2*(float(x)+.5f)/w-1,1-2*(float(y)+.5f)/h,d,1),inverse);world=XMVectorScale(world,1/XMVectorGetW(world));
            auto now=XMVector4Transform(world,projection);if(wet)world=XMVectorSubtract(world,XMVectorSet(velocity.x*cb.dt,0,0,0));auto old=XMVector4Transform(world,previous);
            float mx=(XMVectorGetX(old)/XMVectorGetW(old)-XMVectorGetX(now)/XMVectorGetW(now))*.5f;
            float my=(XMVectorGetY(old)/XMVectorGetW(old)-XMVectorGetY(now)/XMVectorGetW(now))*-.5f;
            Require(std::isfinite(vectors[i*2])&&std::isfinite(vectors[i*2+1]),"temporal motion is non-finite");
            error=std::max(error,std::max(std::abs(mx-vectors[i*2]),std::abs(my-vectors[i*2+1])));
            Require(std::abs(depths[i]-d)<1e-6,"DLSS depth does not match the visible input surface");}
        Require(error<1e-5,"DLSS motion does not match input pixel centers / camera / fluid motion");
        printf("PASS temporal inputs mode %d: depth + motion at input pixel centers, error %.8f\n",mode,error);
    }
    // Static geometry contributes exactly the camera motion already generated
    // from depth. Moving transforms still require the actual object shader.
    struct Vertex{XMFLOAT4 position;XMFLOAT2 uv;};
    Vertex vertices[]={{{-40,-30,20,1},{.5f,.5f}},{{-40,30,20,1},{.5f,.5f}},{{40,-30,20,1},{.5f,.5f}},
        {{40,-30,20,1},{.5f,.5f}},{{-40,30,20,1},{.5f,.5f}},{{40,30,20,1},{.5f,.5f}}};
    D3D11_BUFFER_DESC vbDesc{};vbDesc.ByteWidth=sizeof(vertices);vbDesc.Usage=D3D11_USAGE_IMMUTABLE;vbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{vertices,0,0};ComPtr<ID3D11Buffer> vertexBuffer;Check(g.device->CreateBuffer(&vbDesc,&vbData,&vertexBuffer));
    std::vector<XMFLOAT4> white(w*h,XMFLOAT4{1,1,1,1});auto albedo=image(DXGI_FORMAT_R32G32B32A32_FLOAT,white.data(),w*16);
    std::fill(raw.begin(),raw.end(),100.f/99.9f-10.f/99.9f/20);
    g.context->UpdateSubresource(depth.texture.Get(),0,nullptr,raw.data(),w*4,0);
    vsCode=g.Compile(L"Resources/shaders/TemporalObject.hlsl","VSMain","vs_5_0");psCode=g.Compile(L"Resources/shaders/TemporalObject.hlsl","PSMain","ps_5_0");
    Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ComPtr<ID3D11InputLayout> layout;Check(g.device->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),&layout));
    struct Object{XMFLOAT4X4 world,previousWorld,jittered,current,previous;float nearPlane,farPlane,fluid,pad;} object{};
    XMStoreFloat4x4(&object.world,XMMatrixIdentity());XMStoreFloat4x4(&object.current,projection);object.previous=object.current;object.jittered=object.current;
    object.jittered._31+=.37f*2/w;object.jittered._32-=.23f*2/h;object.nearPlane=.1f;object.farPlane=100;
    auto objectConstants=g.Constant(sizeof(object));
    for(int moving=0;moving<2;++moving){XMStoreFloat4x4(&object.previousWorld,XMMatrixTranslation(moving?-.2f:0,0,0));g.Unbind();
        g.context->UpdateSubresource(objectConstants.Get(),0,nullptr,&object,0,0);auto b=objectConstants.Get();g.context->VSSetConstantBuffers(0,1,&b);g.context->PSSetConstantBuffers(0,1,&b);
        ID3D11ShaderResourceView* inputs[]={depth.srv.Get(),water.srv.Get(),nullptr,albedo.srv.Get()};g.context->PSSetShaderResources(0,4,inputs);
        auto target=motion.rtv.Get();float sentinel[4]={9,9,9,9};g.context->ClearRenderTargetView(target,sentinel);g.context->OMSetRenderTargets(1,&target,nullptr);
        auto vb=vertexBuffer.Get();UINT stride=sizeof(Vertex),offset=0;g.context->IASetVertexBuffers(0,1,&vb,&stride,&offset);g.context->IASetInputLayout(layout.Get());
        g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->Draw(6,0);g.Unbind();auto vectors=read(motion,2);
        float expected=moving?-.2f*XMVectorGetX(projection.r[0])/40:0;
        for(UINT i=0;i<w*h;++i)Require(std::abs(vectors[i*2]-expected)<1e-6&&std::abs(vectors[i*2+1])<1e-6,"static/moving object temporal motion is incorrect");
    }
    printf("PASS temporal objects: static motion matches depth reprojection; moved world generates object motion\n");
}
