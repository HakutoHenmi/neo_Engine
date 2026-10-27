// Builds one mip level from the previous one with a 2x2 box filter.
// Color maps are averaged in linear space and written back as sRGB values.
Texture2D<float4> source : register(t0);
RWTexture2D<float4> destination : register(u0);
cbuffer MipConstants : register(b0) {
    uint2 sourceSize;
    uint2 destinationSize;
    uint srgb;
};

float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 ToSrgb(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }

float4 Load(uint2 at) {
    float4 value = source[min(at, sourceSize - 1)];
    if (srgb != 0) value.rgb = ToLinear(value.rgb);
    return value;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= destinationSize)) return;
    const uint2 at = id.xy * 2;
    float4 value = (Load(at) + Load(at + uint2(1, 0)) + Load(at + uint2(0, 1)) + Load(at + uint2(1, 1))) * 0.25;
    if (srgb != 0) value.rgb = ToSrgb(saturate(value.rgb));
    destination[id.xy] = value;
}
