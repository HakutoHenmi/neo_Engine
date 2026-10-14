#include "../Engine/CameraMatrices.h"
void TestReceiverDepth(Gpu& g){
    constexpr UINT count=256;
    auto code=g.Compile(L"tests/ReceiverDepthTests.hlsl","main","cs_5_0");ComPtr<ID3D11ComputeShader> shader;
    Check(g.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
    auto input=g.MakeBuffer(count,16),output=g.MakeBuffer(count,16);
    struct Constants{XMFLOAT4X4 vp,inverse;XMFLOAT3 camera;float count;} cb{};cb.count=count;
    auto constants=g.Constant(sizeof(cb));UINT legacyMisses=0,fixedMisses=0,directInverseMisses=0;float largest=0,directLargest=0;
    for(float yaw:{0.f,.5f,1.f,2.f,3.f})for(float pitch:{.003f,.04f,.2f})for(float shift:{0.f,.35f}){
        cb.camera={235.f+shift,2,-157.f+shift};XMFLOAT3 forward{std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        auto camera=XMLoadFloat3(&cb.camera);auto view=XMMatrixLookToLH(camera,XMLoadFloat3(&forward),XMVectorSet(0,1,0,0));
        auto projection=XMMatrixPerspectiveFovLH(1.13f,16.f/9,.1f,2000);auto vp=view*projection;
        XMStoreFloat4x4(&cb.vp,vp);
        std::vector<XMFLOAT4> points(count);
        for(UINT i=0;i<count;++i){float distance=40.f+510.f*i/(count-1);points[i]={cb.camera.x+forward.x*distance,-.08f,cb.camera.z+forward.z*distance,1};}
        for(int variant=0;variant<2;++variant){
        XMStoreFloat4x4(&cb.inverse,variant?Engine::InverseCameraViewProjection(view,projection):XMMatrixInverse(nullptr,vp));
        g.Unbind();g.context->UpdateSubresource(input.resource.Get(),0,nullptr,points.data(),0,0);
        g.context->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);auto b=constants.Get();g.context->CSSetConstantBuffers(0,1,&b);
        auto srv=input.srv.Get();auto uav=output.uav.Get();g.context->CSSetShaderResources(0,1,&srv);g.context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        g.context->CSSetShader(shader.Get(),nullptr,0);g.context->Dispatch(count/64,1,1);
        for(auto result:g.Read<XMFLOAT4>(output)){Require(std::isfinite(result.x),"non-finite distant receiver reconstruction");
            if(variant){largest=std::max(largest,result.x);fixedMisses+=result.y>.5;legacyMisses+=result.z>.5;}
            else{directLargest=std::max(directLargest,result.x);directInverseMisses+=result.y>.5;}}
        }
    }
    printf("Distant ground receiver: max undershoot %.4f m, fixed misses %u, legacy 30cm misses %u\n",largest,fixedMisses,legacyMisses);
    printf("Direct combined inverse: max undershoot %.4f m, misses with matched range %u\n",directLargest,directInverseMisses);
    Require(directInverseMisses>0&&legacyMisses>0&&fixedMisses==0,"primary ray range drops distant ground lighting under camera travel");
}

