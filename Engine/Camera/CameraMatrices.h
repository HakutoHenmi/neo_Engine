#pragma once
#include <DirectXMath.h>
namespace Engine {
inline DirectX::XMMATRIX
    InverseCameraViewProjection(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection) {
	// 平行移動込みの透視投影行列をまとめて FP32 で逆行列にすると、遠くの地面で
	// 桁落ちが大きくなる。条件の良い個々の行列を逆にしてから掛け合わせる。
	return DirectX::XMMatrixInverse(nullptr, projection) * DirectX::XMMatrixInverse(nullptr, view);
}
} // namespace Engine
