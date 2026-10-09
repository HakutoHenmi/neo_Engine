Texture2D<float> opaqueDepth : register(t0);
Texture2D<float> fluidDepth : register(t1);
Texture3D<float4> fluidVelocity : register(t2);
SamplerState linearClamp : register(s0);
cbuffer Temporal : register(b0){
    row_major float4x4 inverseCurrent,previous,current;
    float nearPlane,farPlane,includeFluid,dt;
    float3 volumeOrigin;float cell;
    float3 dimensions;float unused;
}
struct Output {float2 motion:SV_TARGET0;float depth:SV_TARGET1;};
Output main(float4 position:SV_POSITION,float2 uv:TEXCOORD0){
    int2 pixel=int2(position.xy);float raw=opaqueDepth.Load(int3(pixel,0));
    float z=nearPlane*farPlane/max(.00001,farPlane-raw*(farPlane-nearPlane));bool fluid=false;
    if(includeFluid>.5){float f=fluidDepth.Load(int3(pixel,0));if(f>0&&f<z){z=f;raw=farPlane/(farPlane-nearPlane)-nearPlane*farPlane/((farPlane-nearPlane)*z);fluid=true;}}
    float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),raw,1),inverseCurrent);world/=world.w;
    float4 present=mul(world,current);
    if(fluid){float3 coordinate=(world.xyz-volumeOrigin)/(cell*dimensions);world.xyz-=fluidVelocity.SampleLevel(linearClamp,saturate(coordinate),0).xyz*dt;}
    float4 old=mul(world,previous);Output result;result.motion=old.w>.001?(old.xy/old.w-present.xy/present.w)*float2(.5,-.5):0;result.depth=raw;return result;
}
