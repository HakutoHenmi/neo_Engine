#include "Obj.hlsli"
Texture2D<float4> paint : register(t0);
SamplerState smp : register(s0);
float4 main(VSOutput input) : SV_TARGET {
    float wet=paint.Sample(smp,input.uv).r;
    // Horizontal decks and vertical sides expose the original Quaternius
    // material when dry. Only the continuous ramp needs its own dry surface.
    bool ramp=abs(input.normal.y)>.1 && abs(input.normal.y)<.99;
    if(!ramp) clip(wet-.30);
    float3 n=normalize(input.normal);
    float3 v=normalize(cameraPos-input.worldpos.xyz);
    // Coverage gradient gives the dried edge a small meniscus without displaced geometry.
    float left=paint.Sample(smp,input.uv-float2(1.0/1024,0)).r;
    float right=paint.Sample(smp,input.uv+float2(1.0/1024,0)).r;
    float back=paint.Sample(smp,input.uv-float2(0,1.0/1024)).r;
    float front=paint.Sample(smp,input.uv+float2(0,1.0/1024)).r;
    float3 axis=abs(n.x)<.9?float3(1,0,0):float3(0,0,1);
    float3 tangent=normalize(axis-n*dot(axis,n));
    float3 bitangent=normalize(cross(tangent,n));
    float2 ripple=float2(cos(input.worldpos.x*2.1+input.worldpos.z*.8),sin(input.worldpos.z*1.7-input.worldpos.x*.6))*.018;
    n=normalize(n+tangent*((left-right)*.22+ripple.x)*wet+bitangent*((back-front)*.22+ripple.y)*wet);
    float3 light=normalize(float3(-.3,.85,-.4));
    float diffuse=.5+.5*saturate(dot(n,light));
    float spec=pow(saturate(dot(n,normalize(v+light))),90)*wet*1.4;
    float3 dry=float3(.33,.40,.16);
    // The body and jets use the same green absorption colour. A shallow opaque
    // film keeps the floor solid, with a raised wet edge and grazing reflection.
    float fresnel=.04+.96*pow(1-saturate(dot(n,v)),5);
    float3 ink=lerp(float3(.025,.075,.008),float3(.16,.38,.035),.35+.45*wet);
    float coverage=smoothstep(.30,.65,wet);
    float3 sky=float3(.25,.42,.64);
    return float4(lerp(dry*diffuse,lerp(ink*diffuse,sky,fresnel*.65)+spec,coverage),1);
}
