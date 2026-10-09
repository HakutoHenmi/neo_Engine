// The same rigid decoration geometry is exercised under very different body poses.
void TestPlayerFace(Gpu& g) {
    struct Includes : ID3DInclude {
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID,LPCVOID* data,UINT* bytes) override {
            for(auto root:{"tests/","Resources/shaders/"}){
                std::ifstream file(std::string(root)+name,std::ios::binary|std::ios::ate);if(!file)continue;
                *bytes=static_cast<UINT>(file.tellg());auto contents=new char[*bytes];file.seekg(0);file.read(contents,*bytes);*data=contents;return S_OK;
            }return E_FAIL;
        }
        HRESULT __stdcall Close(LPCVOID data) override {delete[] static_cast<const char*>(data);return S_OK;}
    } includes;
    auto code=g.Compile(L"tests/SlimeFaceTests.hlsl","TestPlayerFace","cs_5_0",&includes);
    Check(g.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&g.kernels["TestPlayerFace"]));
    struct Camera { XMFLOAT4X4 view{},proj{},vp{}; XMFLOAT3 pos{0,3,-10}; float time=2; } camera;
    auto cameraCb=g.Constant(sizeof(camera)),volumeCb=g.Constant(sizeof(VolumeCB));
    std::vector<XMFLOAT4> restingBubbles;
    for(int stage=0;stage<8;++stage){
        camera.time=stage==7?3.f:2.f;
        VolumeCB cb{};cb.playerRadii=stage==1?XMFLOAT3{3.4f,1.f,3.4f}:stage==2?XMFLOAT3{1.5f,.9f,1.5f}:
            stage==3?XMFLOAT3{1.7f,2.8f,1.7f}:stage==6?XMFLOAT3{4.2f,.66f,4.2f}:XMFLOAT3{2.55f,1.8f,2.55f};
        cb.playerForward=stage==4?XMFLOAT3{1,0,0}:XMFLOAT3{0,0,1};cb.playerMotion=stage==1||stage==4?1.f:0;
        cb.playerSlope=stage==1?XMFLOAT3{.2f,1,-.15f}:XMFLOAT3{0,stage==2?.6f:1.f,0};cb.playerAir=stage==3?1.f:0;
        cb.playerFaceCamera=stage==5?1.f:0;
        auto output=g.MakeBuffer(230,16);
        g.Unbind();g.context->UpdateSubresource(cameraCb.Get(),0,nullptr,&camera,0,0);g.context->UpdateSubresource(volumeCb.Get(),0,nullptr,&cb,0,0);
        ID3D11Buffer* constants[]={cameraCb.Get(),volumeCb.Get()};g.context->CSSetConstantBuffers(0,2,constants);
        auto uav=output.uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        g.Dispatch("TestPlayerFace",4);auto result=g.Read<XMFLOAT4>(output);
        float scale=cb.playerSlope.y;
        Require(std::abs(result[0].x-.145f*scale)<.0001f&&std::abs(result[0].y-.285f*scale)<.0001f&&std::abs(result[0].z-.105f*scale)<.0001f,"body stretch/squash changed rigid eye shape");
        float separation=XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat4(&result[1]),XMLoadFloat4(&result[2]))));
        Require(std::abs(separation-.92f*scale)<.0001f,"body pose sheared the eye pair");
        Require(result[228].w==1&&result[229].w==0&&std::abs(result[228].x-(3-.105f*scale))<.001f,"analytic eye silhouette/hit depth is incorrect");
        if(stage==0)restingBubbles=result;
        for(int i=4;i<228;++i){Require(std::isfinite(result[size_t(i)].x)&&result[size_t(i)].w>0,"bubble geometry became invalid");
            Require(std::abs(result[size_t(i)].w-restingBubbles[size_t(i)].w*scale)<.0001f,"body stretch scaled a spherical bubble");}
        if(stage==4)Require(result[3].x>.99f,"gameplay eyes do not face movement");
        if(stage==5)Require(result[3].z<-.99f,"evolution eyes do not face the camera");
        if(stage==7){float movement=XMVectorGetX(XMVector3Length(XMVectorSubtract(XMLoadFloat4(&result[4]),XMLoadFloat4(&restingBubbles[4]))));
            Require(movement>.05f,"visual-time advance did not move interior bubbles in a fixed player pose");}
    }
    g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","RaymarchPS","ps_5_0");
    g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayVS","vs_5_0");
    g.Compile(L"Resources/shaders/FluidRaymarch.hlsl","SprayPS","ps_5_0");
    puts("PASS rigid decorations: unchanged eye aspect/separation and spherical bubble radius across stretch, squash, flight and slope; mass scaling; travel/menu facing; analytic silhouette/depth; production shaders compiled");
}
