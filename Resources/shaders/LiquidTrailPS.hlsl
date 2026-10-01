cbuffer CBFrame : register(b0) {row_major float4x4 view,proj,viewProj;float3 cameraPos;float time;};
struct PSIn {float4 pos:SV_POSITION;float3 worldPos:TEXCOORD0;float3 normal:TEXCOORD1;float2 uv:TEXCOORD2;float4 color:COLOR0;};
float4 main(PSIn i):SV_TARGET{
    // Write depth as one dense liquid surface: overlapping rounded segments
    // must not alpha-stack their hidden caps into dark seams.
    if(i.color.a<.95){uint2 pixel=uint2(i.pos.xy);uint bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        clip(i.color.a-(bayer[(pixel.y%4)*4+pixel.x%4]+.5)/16);}
    float3 n=normalize(i.normal),v=normalize(cameraPos-i.worldPos),light=normalize(float3(-.4,.85,-.3));
    float wave=sin(i.worldPos.x*5+i.worldPos.z*3-time*2.8)*sin(i.worldPos.z*7-time*1.7);
    n=normalize(n+float3(wave*.065,0,cos(i.worldPos.x*4-time*2)*.045));
    float rim=pow(1-saturate(dot(n,v)),3),spec=pow(saturate(dot(n,normalize(v+light))),55);
    float3 color=i.color.rgb*(.65+.35*saturate(dot(n,light)));
    // Dense dark ink with thin wet reflections along its continuous edge.
    color+=float3(.06,.27,.22)*rim+float3(.48,.67,.60)*spec*.65;
    return float4(color,1);
}
