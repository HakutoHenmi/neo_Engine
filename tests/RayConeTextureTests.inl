void TestRayConeTextures(Gpu& g){
    constexpr UINT size=256,w=64,h=32;std::vector<XMFLOAT4> checker(size*size);
    for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){float v=((x/16+y/16)%2)?1.f:0.f;checker[y*size+x]={v,v,v,1};}
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.MipLevels=9;d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> material;ComPtr<ID3D11ShaderResourceView> srv;Check(g.device->CreateTexture2D(&d,nullptr,&material));Check(g.device->CreateShaderResourceView(material.Get(),nullptr,&srv));
    g.context->UpdateSubresource(material.Get(),0,nullptr,checker.data(),size*16,0);g.context->GenerateMips(srv.Get());
    d.Width=w;d.Height=h;d.MipLevels=1;d.MiscFlags=0;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output,staging;ComPtr<ID3D11RenderTargetView> rtv;Check(g.device->CreateTexture2D(&d,nullptr,&output));Check(g.device->CreateRenderTargetView(output.Get(),nullptr,&rtv));
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(g.device->CreateTexture2D(&d,nullptr,&staging));
    auto vsCode=g.Compile(L"Resources/shaders/FluidCompositeVS.hlsl","main","vs_5_0"),psCode=g.Compile(L"tests/RayConeTextureTests.hlsl","main","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;Check(g.device->CreateRasterizerState(&rd,&raster));D3D11_DEPTH_STENCIL_DESC dd{};ComPtr<ID3D11DepthStencilState> depthState;Check(g.device->CreateDepthStencilState(&dd,&depthState));
    float cb[4]={0,1,.08f,0};auto constants=g.Constant(16);
    auto render=[&](){g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);auto b=constants.Get();g.context->PSSetConstantBuffers(0,1,&b);
        auto input=srv.Get();g.context->PSSetShaderResources(0,1,&input);auto sampler=g.sampler.Get();g.context->PSSetSamplers(0,1,&sampler);auto target=rtv.Get();g.context->OMSetRenderTargets(1,&target,nullptr);
        g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->OMSetDepthStencilState(depthState.Get(),0);g.context->RSSetState(raster.Get());D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};g.context->RSSetViewports(1,&vp);
        g.context->IASetInputLayout(nullptr);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->Draw(3,0);g.Unbind();g.context->CopyResource(staging.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<XMFLOAT4> pixels(w*h);
        for(UINT y=0;y<h;++y)memcpy(pixels.data()+y*w,static_cast<const char*>(m.pData)+y*m.RowPitch,w*16);g.context->Unmap(staging.Get(),0);return pixels;};
    auto nearPixels=render();double detail=0;for(auto p:nearPixels)detail+=(p.x-.5)*(p.x-.5);detail/=nearPixels.size();Require(detail>.08,"RT footprint filtering erased nearby material detail");
    cb[1]=.03f;auto grazing=render();cb[0]=.019f;auto shifted=render();double adaptive=0,fixed=0;
    for(size_t i=0;i<grazing.size();++i){Require(std::isfinite(shifted[i].x),"non-finite ray cone sample");adaptive+=std::abs(shifted[i].x-grazing[i].x);fixed+=std::abs(shifted[i].y-grazing[i].y);}
    adaptive/=grazing.size();fixed/=grazing.size();Require(adaptive<.001&&fixed>.15,"ray texture minification still flickers under a small camera shift");
    printf("PASS RT material grazing angle: adaptive delta %.6f, fixed mip delta %.6f, near detail variance %.4f\n",adaptive,fixed,detail);
}
