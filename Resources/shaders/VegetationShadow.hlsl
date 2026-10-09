// Match the visible grass cutout in both regular and instanced shadow passes.
Texture2D albedo : register(t0);
SamplerState materialSampler : register(s0);
cbuffer Frame : register(b0) {row_major float4x4 view,proj,lightVP;float3 camera;float time;};
cbuffer Object : register(b1) {row_major float4x4 world;float4 color;};
struct Instance {row_major float4x4 world;float4 color;float4 uvScaleOffset;};
StructuredBuffer<Instance> instances : register(t2);
struct Input {float4 position:POSITION;float2 uv:TEXCOORD0;};
struct Output {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
Output VSMain(Input i){Output o;o.position=mul(mul(i.position,world),lightVP);o.uv=i.uv;return o;}
Output VSInstanced(Input i,uint id:SV_InstanceID){Output o;o.position=mul(mul(i.position,instances[id].world),lightVP);o.uv=i.uv;return o;}
void PSMain(Output i){clip(albedo.Sample(materialSampler,i.uv).a-.42f);}
