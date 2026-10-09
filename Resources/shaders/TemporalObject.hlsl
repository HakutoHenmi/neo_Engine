cbuffer ObjectTemporal : register(b0){row_major float4x4 world,previousWorld,currentJittered,current,previous;float nearPlane,farPlane,includeFluid,pad;};
Texture2D<float> opaqueDepth : register(t0);
Texture2D<float> fluidDepth : register(t1);
Texture2D<float4> albedo : register(t3);
SamplerState linearClamp : register(s0);
struct VSInput{float4 position:POSITION;float2 uv:TEXCOORD0;};
struct VSOutput{float4 position:SV_POSITION;float4 now:TEXCOORD0;float4 old:TEXCOORD1;float2 uv:TEXCOORD2;};
VSOutput VSMain(VSInput input){VSOutput output;float4 p=mul(input.position,world);output.position=mul(p,currentJittered);output.now=mul(p,current);output.old=mul(mul(input.position,previousWorld),previous);output.uv=input.uv;return output;}
float2 PSMain(VSOutput input):SV_TARGET{
    int2 pixel=int2(input.position.xy);float raw=opaqueDepth.Load(int3(pixel,0));clip(.00001-abs(input.position.z-raw));
    clip(albedo.Sample(linearClamp,input.uv).a-.42);
    if(includeFluid>.5){float z=nearPlane*farPlane/max(.00001,farPlane-raw*(farPlane-nearPlane));float f=fluidDepth.Load(int3(pixel,0));if(f>0&&f<z)discard;}
    return input.old.w>.001?(input.old.xy/input.old.w-input.now.xy/input.now.w)*float2(.5,-.5):0;
}
