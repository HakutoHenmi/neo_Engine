// Build with the bundled DirectXTex library. Source HDR remains linear;
// cube face orientation follows Direct3D +X,-X,+Y,-Y,+Z,-Z conventions.
#include <d3d11.h>
#include <wrl/client.h>
#include "../externals/DirectXTex/DirectXTex.h"
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace DirectX;
void Check(HRESULT hr){if(FAILED(hr)){std::cerr<<"HRESULT "<<std::hex<<hr<<"\n";std::exit(1);}}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    ScratchImage source;Check(LoadFromHDRFile(argv[1],nullptr,source));
    const Image& image=*source.GetImage(0,0,0);
    if(image.format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return 3;
    constexpr size_t size=2048;ScratchImage cube;
    Check(cube.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT,size,size,1,1));
    auto pixel=[&](int x,int y,int channel){x=(x%int(image.width)+int(image.width))%int(image.width);y=std::clamp(y,0,int(image.height)-1);
        return reinterpret_cast<const float*>(image.pixels+y*image.rowPitch)[x*4+channel];};
    for(size_t face=0;face<6;++face){const Image& out=*cube.GetImage(0,face,0);
        for(size_t y=0;y<size;++y)for(size_t x=0;x<size;++x){
            float u=2*(float(x)+.5f)/size-1,v=2*(float(y)+.5f)/size-1;
            float dx=0,dy=0,dz=0;
            switch(face){case 0:dx=1;dy=-v;dz=-u;break;case 1:dx=-1;dy=-v;dz=u;break;
                case 2:dx=u;dy=1;dz=v;break;case 3:dx=u;dy=-1;dz=-v;break;
                case 4:dx=u;dy=-v;dz=1;break;default:dx=-u;dy=-v;dz=-1;}
            float length=std::sqrt(dx*dx+dy*dy+dz*dz);
            float sx=(std::atan2(dz,dx)/6.28318530718f+.5f)*float(image.width)-.5f;
            float sy=std::acos(dy/length)/3.14159265359f*float(image.height)-.5f;
            int ix=int(std::floor(sx)),iy=int(std::floor(sy));float fx=sx-ix,fy=sy-iy;
            float* dst=reinterpret_cast<float*>(out.pixels+y*out.rowPitch)+x*4;
            for(int c=0;c<3;++c)dst[c]=(pixel(ix,iy,c)*(1-fx)+pixel(ix+1,iy,c)*fx)*(1-fy)+(pixel(ix,iy+1,c)*(1-fx)+pixel(ix+1,iy+1,c)*fx)*fy;
            dst[3]=1;
        }
    }
    ScratchImage mips;Check(GenerateMipMaps(cube.GetImages(),cube.GetImageCount(),cube.GetMetadata(),TEX_FILTER_LINEAR,0,mips));
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr));
    ScratchImage compressed;Check(Compress(device.Get(),mips.GetImages(),mips.GetImageCount(),mips.GetMetadata(),DXGI_FORMAT_BC6H_UF16,TEX_COMPRESS_DEFAULT,1,compressed));
    Check(SaveToDDSFile(compressed.GetImages(),compressed.GetImageCount(),compressed.GetMetadata(),DDS_FLAGS_NONE,argv[2]));
    std::cout<<"2048x2048 x 6 faces, BC6H HDR, "<<compressed.GetMetadata().mipLevels<<" mip levels\n";
}
