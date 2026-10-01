cbuffer CBFrame : register(b0) {row_major float4x4 view;row_major float4x4 proj;row_major float4x4 viewProj;float3 cameraPos;float time;};
struct PSIn {float4 pos:SV_POSITION;float3 worldPos:TEXCOORD0;float3 normal:TEXCOORD1;float2 uv:TEXCOORD2;float4 color:COLOR0;};
float4 main(PSIn i):SV_TARGET {
    float lightLevel=floor(i.color.a*.5);
    float opacity=saturate(i.color.a-lightLevel*2);
    opacity*=smoothstep(.8,2.8,distance(cameraPos,i.worldPos));
    clip(opacity-.001);
    // Screen-door fade preserves instancing, depth testing and the view of the slime.
    if(opacity<.999){float noise=frac(52.9829189*frac(dot(floor(i.pos.xy),float2(.06711056,.00583715))));clip(opacity-noise);}
    float3 n=normalize(i.normal),v=normalize(cameraPos-i.worldPos);
    float light=.28+.72*saturate(dot(n,normalize(float3(-.4,.8,-.35))));
    float rim=pow(1-saturate(dot(n,v)),3);
    float seams=pow(1-abs(sin(i.worldPos.y*2+i.worldPos.z*1.3)),18)*.045;
    float3 armor=i.color.rgb*light+float3(.04,.16,.22)*rim;
    armor+=float3(.08,.55,.75)*seams;
    float3 halfVector=normalize(v+normalize(float3(-.4,.8,-.35)));
    armor+=pow(saturate(dot(n,halfVector)),24)*.18;
    armor=lerp(armor,float3(1,.32,.055),saturate(lightLevel/16)*(.35+.15*sin(time*22)));
    return float4(armor,1);
}
