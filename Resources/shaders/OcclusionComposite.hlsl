// DLSS の前の遮蔽: ジッター付きの HDR シーンに掛け合わせ (合成: dest*src)、
// DLSS が AO / RT 影の接地の暗さのアンチエイリアスと安定化を行えるようにする。
#define RT_LIGHTING_COMPOSITE
#include "ChronoFocusPost.hlsl"
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float factor=occlusionFactor(uv,viewDepth(uv));
    return float4(factor,factor,factor,1);
}
