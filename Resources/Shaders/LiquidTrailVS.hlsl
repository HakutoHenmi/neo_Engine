struct InstanceData { row_major float4x4 world; float4 color; float4 uvScaleOffset; };
StructuredBuffer<InstanceData> instances : register(t2);
cbuffer CBFrame : register(b0) { row_major float4x4 view,proj,viewProj;float3 cameraPos;float time; };
struct VSIn {float4 pos:POSITION;float2 uv:TEXCOORD0;float3 normal:NORMAL;};
struct PSIn {float4 pos:SV_POSITION;float3 worldPos:TEXCOORD0;float3 normal:TEXCOORD1;float2 uv:TEXCOORD2;float4 color:COLOR0;};
PSIn main(VSIn v,uint id:SV_InstanceID){PSIn o;InstanceData data=instances[id];float4 wp=mul(v.pos,data.world);
    float3 scaleSquared=float3(dot(data.world[0].xyz,data.world[0].xyz),dot(data.world[1].xyz,data.world[1].xyz),dot(data.world[2].xyz,data.world[2].xyz));
    o.normal=normalize(mul(float4(v.normal/max(scaleSquared,.00001),0),data.world).xyz);
    o.worldPos=wp.xyz;o.pos=mul(mul(wp,view),proj);o.uv=v.uv;o.color=data.color;return o;}
