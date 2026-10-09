void TestRtLightingDenoise(Gpu& g){
    constexpr UINT w=48,h=32;
    struct Image{ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;};
    auto image=[&](DXGI_FORMAT format,const void* pixels,UINT pitch,bool output=false){Image result;D3D11_TEXTURE2D_DESC d{};
        d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output?D3D11_BIND_UNORDERED_ACCESS:0);
        D3D11_SUBRESOURCE_DATA initial{pixels,pitch,pitch*h};Check(g.device->CreateTexture2D(&d,pixels?&initial:nullptr,&result.tex));Check(g.device->CreateShaderResourceView(result.tex.Get(),nullptr,&result.srv));
        if(output)Check(g.device->CreateUnorderedAccessView(result.tex.Get(),nullptr,&result.uav));return result;};
    std::vector<XMFLOAT4> noise(w*h),history(w*h);std::vector<float> raw(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){float v=((x+y)%2)?.5f:.1f;noise[y*w+x]={v,v,v,20};history[y*w+x]={.3f,.3f,.3f,20};raw[y*w+x]=100.f/99.9f-10.f/99.9f/20;}
    auto current=image(DXGI_FORMAT_R32G32B32A32_FLOAT,noise.data(),w*16),previous=image(DXGI_FORMAT_R32G32B32A32_FLOAT,history.data(),w*16);
    auto depth=image(DXGI_FORMAT_R32_FLOAT,raw.data(),w*4),output=image(DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,0,true),other=image(DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,0,true);
    struct Constants{XMFLOAT4X4 inverse,previous;XMFLOAT2 dimensions;float nearPlane,farPlane,valid,pad[3];} cb{};
    auto projection=XMMatrixPerspectiveFovLH(1.13f,float(w)/h,.1f,100);XMStoreFloat4x4(&cb.inverse,XMMatrixInverse(nullptr,projection));XMStoreFloat4x4(&cb.previous,projection);
    cb.dimensions={float(w),float(h)};cb.nearPlane=.1f;cb.farPlane=100;auto constants=g.Constant(sizeof(cb));
    auto code=g.Compile(L"Resources/shaders/RtLightingDenoise.hlsl","main","cs_5_0");ComPtr<ID3D11ComputeShader> shader;Check(g.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
    auto run=[&](){g.Unbind();g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);auto b=constants.Get();g.context->CSSetConstantBuffers(0,1,&b);
        ID3D11ShaderResourceView* inputs[]={current.srv.Get(),current.srv.Get(),previous.srv.Get(),previous.srv.Get(),depth.srv.Get()};ID3D11UnorderedAccessView* targets[]={output.uav.Get(),other.uav.Get()};
        g.context->CSSetShaderResources(0,5,inputs);g.context->CSSetUnorderedAccessViews(0,2,targets,nullptr);g.context->CSSetShader(shader.Get(),nullptr,0);g.context->Dispatch(w/8,h/8,1);g.Unbind();
        D3D11_TEXTURE2D_DESC d;output.tex->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Check(g.device->CreateTexture2D(&d,nullptr,&staging));g.context->CopyResource(staging.Get(),output.tex.Get());D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<XMFLOAT4> result(w*h);for(UINT y=0;y<h;++y)memcpy(result.data()+y*w,static_cast<const char*>(m.pData)+y*m.RowPitch,w*16);g.context->Unmap(staging.Get(),0);return result;};
    auto error=[&](const std::vector<XMFLOAT4>& data){double sum=0;for(UINT y=3;y<h-3;++y)for(UINT x=3;x<w-3;++x){float v=data[y*w+x].x-.3f;sum+=v*v;}return sum;};
    auto spatial=run();cb.valid=1;auto temporal=run();double variance=error(temporal)/error(noise);
    Require(variance<.01,"RT denoiser failed to suppress changing radiance noise");
    for(auto& p:history)p={20,20,20,100};g.context->UpdateSubresource(previous.tex.Get(),0,nullptr,history.data(),w*16,0);auto rejected=run();
    for(size_t i=0;i<rejected.size();++i)Require(std::abs(rejected[i].x-spatial[i].x)<1e-5,"RT history leaked across disocclusion");
    for(auto& p:history)p.w=20;g.context->UpdateSubresource(previous.tex.Get(),0,nullptr,history.data(),w*16,0);auto clamped=run();
    for(auto p:clamped)Require(std::isfinite(p.x)&&p.x<.55,"RT history was not clamped after lighting changes");
    for(auto& p:noise)p.w=-20;g.context->UpdateSubresource(current.tex.Get(),0,nullptr,noise.data(),w*16,0);auto fluid=run();
    for(size_t i=0;i<fluid.size();++i)Require(std::abs(fluid[i].x-spatial[i].x)<1e-5,"RT history trails behind animated fluid");
    // A reprojected surface moves one pixel. Old radiance must follow that pixel.
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){noise[y*w+x]={x%2?0.f:1.f,0,0,20};history[y*w+x]={.1f+float(x)/w*.7f,0,0,20};}
    g.context->UpdateSubresource(current.tex.Get(),0,nullptr,noise.data(),w*16,0);g.context->UpdateSubresource(previous.tex.Get(),0,nullptr,history.data(),w*16,0);
    auto stationary=run();XMStoreFloat4x4(&cb.previous,XMMatrixTranslation(40.f/(w*XMVectorGetX(projection.r[0])),0,0)*projection);auto moving=run();
    float difference=moving[16*w+24].x-stationary[16*w+24].x;Require(difference>.005f&&difference<.03f,"RT history did not reproject during camera motion");
    printf("RT radiance denoiser: noise variance ratio %.6f, disocclusion/clamp/fluid PASS, moving reprojection %.6f\n",variance,difference);
}
