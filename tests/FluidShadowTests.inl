// Exercise the production light-view shader against an independent trilinear
// reference of the reconstructed surface, including gaps and silhouette edges.
void TestVolumeShadow(Gpu& g,VolumeCB cb,Texture& density,Texture& occupancy,const char* name){
    constexpr UINT width=192,height=192;
    struct Light {XMFLOAT4X4 vp,inverse;XMFLOAT4 bounds{-1,-1,1,1};} light;
    float scale=cb.playerMorph.y;
    auto view=XMMatrixLookAtLH(XMVectorSet(0,cb.playerBodyCenter.y,40*scale,1),XMVectorSet(0,cb.playerBodyCenter.y,0,1),XMVectorSet(0,1,0,0));
    auto vp=view*XMMatrixOrthographicLH(20*scale,20*scale,.1f,100*scale),inverse=XMMatrixInverse(nullptr,vp);
    XMStoreFloat4x4(&light.vp,vp);XMStoreFloat4x4(&light.inverse,inverse);
    auto lightCb=g.Constant(sizeof(light)),settings=g.Constant(sizeof(cb));
    g.Unbind();g.context->UpdateSubresource(lightCb.Get(),0,nullptr,&light,0,0);g.context->UpdateSubresource(settings.Get(),0,nullptr,&cb,0,0);
    auto vsCode=g.Compile(L"Resources/shaders/FluidShadow.hlsl","ShadowVS","vs_5_0"),psCode=g.Compile(L"Resources/shaders/FluidShadow.hlsl","ShadowPS","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(g.device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(g.device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dsv;Check(g.device->CreateTexture2D(&d,nullptr,&depth));Check(g.device->CreateDepthStencilView(depth.Get(),nullptr,&dsv));
    D3D11_DEPTH_STENCIL_DESC dsDesc{};dsDesc.DepthEnable=TRUE;dsDesc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dsDesc.DepthFunc=D3D11_COMPARISON_LESS;ComPtr<ID3D11DepthStencilState> ds;Check(g.device->CreateDepthStencilState(&dsDesc,&ds));
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;Check(g.device->CreateRasterizerState(&raster,&rs));
    g.context->OMSetDepthStencilState(ds.Get(),0);g.context->OMSetBlendState(nullptr,nullptr,UINT_MAX);g.context->RSSetState(rs.Get());D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};g.context->RSSetViewports(1,&viewport);
    ID3D11Buffer* constants[]={lightCb.Get(),settings.Get()};g.context->VSSetConstantBuffers(0,2,constants);g.context->PSSetConstantBuffers(0,2,constants);
    ID3D11ShaderResourceView* inputs[]={density.srv.Get(),occupancy.srv.Get()};g.context->PSSetShaderResources(0,2,inputs);auto sampler=g.sampler.Get();g.context->PSSetSamplers(0,1,&sampler);
    g.context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);g.context->OMSetRenderTargets(0,nullptr,dsv.Get());g.context->IASetInputLayout(nullptr);g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);g.context->VSSetShader(vs.Get(),nullptr,0);g.context->PSSetShader(ps.Get(),nullptr,0);g.context->Draw(6,0);g.Unbind();
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;Check(g.device->CreateTexture2D(&d,nullptr,&stage));g.context->CopyResource(stage.Get(),depth.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(g.context->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));std::vector<float> depths(width*height);
    for(UINT y=0;y<height;++y)memcpy(depths.data()+y*width,(BYTE*)mapped.pData+y*mapped.RowPitch,width*4);g.context->Unmap(stage.Get(),0);
    D3D11_TEXTURE3D_DESC td{};density.resource->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture3D> volumeStage;Check(g.device->CreateTexture3D(&td,nullptr,&volumeStage));g.context->CopyResource(volumeStage.Get(),density.resource.Get());
    Check(g.context->Map(volumeStage.Get(),0,D3D11_MAP_READ,0,&mapped));std::vector<float> values(td.Width*td.Height*td.Depth);
    for(UINT z=0;z<td.Depth;++z)for(UINT y=0;y<td.Height;++y){auto row=(const HALF*)((BYTE*)mapped.pData+z*mapped.DepthPitch+y*mapped.RowPitch);for(UINT x=0;x<td.Width;++x)values[(z*td.Height+y)*td.Width+x]=std::max(XMConvertHalfToFloat(row[x*4]),XMConvertHalfToFloat(row[x*4+1]));}g.context->Unmap(volumeStage.Get(),0);
    // D3D's sampler uses eight fractional interpolation bits. Match that
    // precision so a grazing 0.25003-density island is classified consistently.
    auto sample=[&](XMFLOAT3 world){float q[]={(world.x-cb.origin.x)/cb.cell-.5f,(world.y-cb.origin.y)/cb.cell-.5f,(world.z-cb.origin.z)/cb.cell-.5f};int cell[3];float f[3];for(int i=0;i<3;++i){cell[i]=int(std::floor(q[i]));f[i]=std::round((q[i]-cell[i])*256)/256;}float sum=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){int ix=std::clamp(cell[0]+x,0,int(td.Width)-1),iy=std::clamp(cell[1]+y,0,int(td.Height)-1),iz=std::clamp(cell[2]+z,0,int(td.Depth)-1);sum+=values[(iz*td.Height+iy)*td.Width+ix]*(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2]);}return sum;};
    UINT hitCount=0,mismatches=0;float maxError=0;std::vector<UINT> image(width*height,0xffffffff);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){XMFLOAT3 start;XMStoreFloat3(&start,XMVector3TransformCoord(XMVectorSet(2*(float(x)+.5f)/width-1,1-2*(float(y)+.5f)/height,0,1),inverse));float expected=-1e9f;
        for(float z=cb.origin.z+td.Depth*cb.cell;z>=cb.origin.z;z-=cb.cell*.25f){XMFLOAT3 world{start.x,start.y,z};if(world.x<cb.origin.x||world.x>cb.origin.x+td.Width*cb.cell||world.y<cb.origin.y||world.y>cb.origin.y+td.Height*cb.cell)break;if(sample(world)>=cb.iso){expected=z;break;}}
        bool hit=depths[y*width+x]<1;if(hit){++hitCount;image[y*width+x]=0xff333333;}if(hit!=(expected>-1e8f))++mismatches;
        if(hit&&expected>-1e8f){XMFLOAT3 world;XMStoreFloat3(&world,XMVector3TransformCoord(XMVectorSet(2*(float(x)+.5f)/width-1,1-2*(float(y)+.5f)/height,depths[y*width+x],1),inverse));maxError=std::max(maxError,std::abs(world.z-expected));}}
    printf("%s shadow: pixels=%u silhouette mismatch=%u max surface error=%.4f cells\n",name,hitCount,mismatches,maxError/cb.cell);
    Require(hitCount>150,"liquid shadow disappeared");Require(mismatches<30&&maxError<cb.cell*.6f,"light silhouette does not follow the reconstructed isosurface");
    if(cb.playerMorph.x==-2){d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;D3D11_SUBRESOURCE_DATA data{image.data(),width*4,width*height*4};ComPtr<ID3D11Texture2D> preview;Check(g.device->CreateTexture2D(&d,&data,&preview));WritePng(g.device.Get(),g.context.Get(),preview.Get(),L"tests/out/hammer-shadow-surface.png");}
}
