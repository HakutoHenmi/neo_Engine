#pragma once
#include <DirectXMath.h>
namespace Engine {
inline DirectX::XMMATRIX InverseCameraViewProjection(const DirectX::XMMATRIX& view,const DirectX::XMMATRIX& projection){
    // Inverting the combined translated perspective matrix in FP32 amplifies
    // cancellation on distant ground. Invert its well-conditioned factors.
    return DirectX::XMMatrixInverse(nullptr,projection)*DirectX::XMMatrixInverse(nullptr,view);
}
}
