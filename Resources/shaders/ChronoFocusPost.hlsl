// Chrono-only treatment. The aiming area stays sharp; temporal blur and cool
// grading live at the periphery, with UI composited separately afterwards.
Texture2D scene : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Post : register(b0) {
    float time,noise,strength,chroma,damage,scanline,slow,pad;
};
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 original=scene.Sample(linearClamp,uv).rgb;
    float2 delta=uv-.5;
    float edge=smoothstep(.20,.62,length(delta));
    float amount=saturate(slow)*edge;
    float3 blurred=0;
    [unroll]for(int i=0;i<8;++i)
        blurred+=scene.Sample(linearClamp,saturate(uv-delta*(i/7.0)*.06*strength*amount)).rgb;
    float3 color=lerp(original,blurred/8,amount*.8);
    float luma=dot(color,float3(.2126,.7152,.0722));
    color=lerp(color,lerp(color,luma.xxx,.3)*float3(.80,1.05,1.18),amount*.6);
    color*=1-amount*.18;
    color=lerp(color,float3(.8,.08,.06),edge*damage*.22);
    return float4(color,1);
}
