Texture2D<float> rawDepthPhase0 : register(t0);
Texture2D<float2> surfacePhase0 : register(t1);
Texture2D<float> rawDepthPhase1 : register(t2);
Texture2D<float2> surfacePhase1 : register(t3);
Texture2D<float> rawDepthPhase2 : register(t4);
Texture2D<float2> surfacePhase2 : register(t5);
Texture2D<float4> sceneColor : register(t6);
Texture2D<float> sceneDepth : register(t7);
TextureCube<float4> environmentMap : register(t8);
SamplerState smp : register(s0);
cbuffer CompositeSettings : register(b1) { uint phaseMask; uint3 unusedSettings; };

cbuffer CBFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float3 gCamPos; float gTime;
};

struct PSIn { float4 svpos : SV_POSITION; float2 uv : TEXCOORD0; };

float3 ViewPosition(float2 uv, float z) {
    return float3((uv * float2(2, -2) + float2(-1, 1)) /
                  float2(gProj[0][0], gProj[1][1]) * z, z);
}

float OpaqueViewDepth(float2 uv) {
    // Point depth avoids fabricating an intermediate surface at object edges.
    uint width, height;
    sceneDepth.GetDimensions(width, height);
    int2 pixel = clamp(int2(uv * float2(width, height)), int2(0, 0), int2(width, height) - 1);
    float z = sceneDepth.Load(int3(pixel, 0));
    return gProj[3][2] / (z - gProj[2][2]);
}

float2 LoadSurface(int2 pixel, uint phase) {
    if (phase == 0U) return surfacePhase0.Load(int3(pixel, 0));
    if (phase == 1U) return surfacePhase1.Load(int3(pixel, 0));
    return surfacePhase2.Load(int3(pixel, 0));
}

float2 Surface(float2 uv, uint phase) {
    uint width, height;
    surfacePhase0.GetDimensions(width, height);
    // Guide from the reconstructed surface, not the original particle sphere.
    // Raw-depth rejection here reintroduced particle-shaped holes after blur.
    float referenceDepth = 0;
    float2 coordinate = uv * float2(width, height) - 0.5f;
    int2 origin = int2(floor(coordinate));
    float2 fraction = frac(coordinate);
    float2 samples[4]; float weights[4];
    float bestWeight = -1;
    [unroll] for (int i = 0; i < 4; ++i) {
        int2 offset = int2(i & 1, i >> 1);
        samples[i] = LoadSurface(clamp(origin + offset, int2(0, 0), int2(width, height) - 1), phase);
        float2 weightXY = lerp(1.0f - fraction, fraction, float2(offset));
        weights[i] = weightXY.x * weightXY.y;
        if (samples[i].x > 0 && weights[i] > bestWeight) {
            referenceDepth = samples[i].x;
            bestWeight = weights[i];
        }
    }
    float depthSum = 0, depthWeight = 0, density = 0, densityWeight = 0;
    [unroll] for (int j = 0; j < 4; ++j) {
        if (samples[j].x <= 0) { densityWeight += weights[j]; continue; }
        if (abs(samples[j].x - referenceDepth) > 0.85f) continue;
        depthSum += samples[j].x * weights[j];
        depthWeight += weights[j];
        density += samples[j].y * weights[j];
        densityWeight += weights[j];
    }
    // Bilateral upsampling excludes other depth layers and zero-depth holes.
    return depthWeight > 0.0001f ? float2(depthSum / depthWeight, density / max(densityWeight, 0.0001f)) : float2(0, 0);
}

// Derivatives use an already filtered depth field. At discontinuities use the
// one-sided derivative on this surface, never the depth of the other body.
float DepthDelta(float2 a, float2 b, float centerDepth) {
    bool validA = a.x > 0 && abs(a.x - centerDepth) < 0.85f;
    bool validB = b.x > 0 && abs(b.x - centerDepth) < 0.85f;
    if (validA && validB) return (b.x - a.x) * 0.5f;
    if (validA) return centerDepth - a.x;
    if (validB) return b.x - centerDepth;
    return 0;
}

struct Layer {
    float depth;
    float coverage;
    float3 transmission;
    float3 radiance;
    float2 refractOffset;
};

Layer ShadeLayer(float2 uv, uint phase, float2 texel) {
    Layer layer = (Layer)0;
    layer.depth = 1.0e20f;
    layer.transmission = 1;
    if ((phaseMask & (1U << phase)) == 0) return layer;
    float2 surface = Surface(uv, phase);
    float threshold = phase == 2U ? 0.022f : 0.075f;
    if (surface.x <= 0 || surface.y < threshold) return layer;
    float opaqueDepth = OpaqueViewDepth(uv);
    if (surface.x >= opaqueDepth) return layer;
    layer.depth = surface.x;
    // Coverage is geometric; absorption is calculated separately below.
    float edgeWidth = max(fwidth(surface.y) * 1.5f, threshold * 0.2f);
    layer.coverage = smoothstep(threshold, threshold + edgeWidth, surface.y);
    // Measure slopes over a small world-space baseline. A two-pixel baseline
    // magnifies half-resolution interpolation seams in reflection/refraction.
    float normalStep = clamp(0.30f * abs(gProj[1][1]) /
                             (2.0f * texel.y * max(surface.x, 0.1f)), 3.0f, 16.0f);
    float2 dxUV = float2(texel.x * normalStep, 0);
    float2 dyUV = float2(0, texel.y * normalStep);
    float dzdx = DepthDelta(Surface(uv - dxUV, phase), Surface(uv + dxUV, phase), surface.x);
    float dzdy = DepthDelta(Surface(uv - dyUV, phase), Surface(uv + dyUV, phase), surface.x);
    float3 p = ViewPosition(uv, surface.x);
    float3 ray = p / surface.x;
    float3 tx = float3(2.0f * dxUV.x * surface.x / gProj[0][0], 0, 0) + ray * dzdx;
    float3 ty = float3(0, -2.0f * dyUV.y * surface.x / gProj[1][1], 0) + ray * dzdy;
    float3 normal = normalize(cross(tx, ty));
    float3 viewDir = normalize(-p);
    if (dot(normal, viewDir) < 0) normal = -normal;

    // Density is a local optical path proxy, not N dot V. Different particle
    // strengths are accounted for so a thin water sheet stays transparent.
    float thickness = min(surface.y * (phase == 2U ? 5.0f : 1.8f), 4.0f);
    thickness = min(thickness, max(opaqueDepth - surface.x, 0.0f));
    float3 absorption = phase == 0U ? float3(1.6f, 0.16f, 0.8f) :
                        phase == 1U ? float3(0.16f, 0.3f, 1.8f) :
                                      float3(0.22f, 0.055f, 0.035f);
    float3 tint = phase == 0U ? float3(0.025f, 0.24f, 0.07f) :
                  phase == 1U ? float3(0.28f, 0.21f, 0.025f) :
                                float3(0.025f, 0.11f, 0.13f);
    float ior = phase == 2U ? 1.333f : 1.38f;
    float f0 = (ior - 1.0f) / (ior + 1.0f);
    f0 *= f0;
    float fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(normal, viewDir)), 5.0f);
    float3 transmittance = exp(-absorption * thickness);
    layer.transmission = transmittance * (1.0f - fresnel);

    // Actual sky cubemap in world space, not a synthetic green sky derived
    // from fluid colour. A null cubemap reads black until a sky is assigned.
    float3 reflectWorld = mul(reflect(-viewDir, normal), transpose((float3x3)gView));
    float3 reflected = environmentMap.SampleLevel(smp, reflectWorld, 0).rgb;
    float3 lightDir = normalize(mul(float3(-0.3f, 0.8f, -0.5f), (float3x3)gView));
    float3 halfVector = normalize(lightDir + viewDir);
    float highlight = pow(saturate(dot(normal, halfVector)), 96.0f) * 0.8f;
    layer.radiance = reflected * fresnel + highlight * (0.15f + fresnel) +
                     tint * (1.0f - transmittance) * 0.18f;

    // Refract a finite path through the surface. Opaque depth guards below
    // stop foreground objects leaking into the displaced background sample.
    float3 refracted = refract(-viewDir, normal, 1.0f / ior);
    float3 exitPoint = p + refracted * thickness;
    float4 clip = mul(float4(exitPoint, 1), gProj);
    float2 exitUV = clip.xy / max(clip.w, 0.001f) * float2(0.5f, -0.5f) + 0.5f;
    layer.refractOffset = clamp(exitUV - uv, -texel * 32.0f, texel * 32.0f);
    return layer;
}

void SortFarBeforeNear(inout Layer a, inout Layer b) {
    if (a.depth < b.depth) { Layer saved = a; a = b; b = saved; }
}

void CompositeLayer(Layer layer, float2 texel,
                    inout float3 result, inout float3 backgroundTransmission,
                    inout float2 backgroundUV, inout float3 backgroundSample) {
    if (layer.coverage <= 0) return;
    float2 candidateUV = clamp(backgroundUV + layer.refractOffset, texel * 0.5f, 1.0f - texel * 0.5f);
    if (OpaqueViewDepth(candidateUV) < layer.depth) candidateUV = backgroundUV;
    float3 displacedBackground = sceneColor.SampleLevel(smp, candidateUV, 0).rgb;
    // Displace only the surviving opaque background contribution. Previously
    // composed fluid layers are never overwritten with a fresh scene sample.
    result += (displacedBackground - backgroundSample) * backgroundTransmission * layer.coverage;
    backgroundSample = lerp(backgroundSample, displacedBackground, layer.coverage);
    backgroundUV = lerp(backgroundUV, candidateUV, layer.coverage);
    float3 transmission = lerp(float3(1, 1, 1), layer.transmission, layer.coverage);
    result = result * transmission + layer.radiance * layer.coverage;
    backgroundTransmission *= transmission;
}

float4 main(PSIn input) : SV_TARGET {
    uint width, height;
    sceneColor.GetDimensions(width, height);
    float2 texel = 1.0f / float2(width, height);
    Layer a = ShadeLayer(input.uv, 0U, texel);
    Layer b = ShadeLayer(input.uv, 1U, texel);
    Layer c = ShadeLayer(input.uv, 2U, texel);
    SortFarBeforeNear(a, b);
    SortFarBeforeNear(b, c);
    SortFarBeforeNear(a, b);
    float3 background = sceneColor.SampleLevel(smp, input.uv, 0).rgb;
    float3 result = background;
    float3 transmission = 1;
    float2 backgroundUV = input.uv;
    CompositeLayer(a, texel, result, transmission, backgroundUV, background);
    CompositeLayer(b, texel, result, transmission, backgroundUV, background);
    CompositeLayer(c, texel, result, transmission, backgroundUV, background);
    // The shader has already composed the scene. Hardware blending is off.
    return float4(result, 1);
}
