#include "Obj.hlsli"
float4 main(VSOutput i) : SV_TARGET {
    float facing=abs(dot(normalize(i.normal),normalize(cameraPos-i.worldpos.xyz)));
    float flow=.5+.5*sin(i.uv.y*65-time*32+i.uv.x*12);
    float spiral=pow(.5+.5*sin(i.uv.x*18.8496-i.uv.y*48+time*19),7);
    float edge=smoothstep(.02,.65,facing);
    float3 tint=lerp(color.rgb,color.rgb*.45+.55,pow(facing,5)*.65);
    // Preserve a dense liquid column even at grazing angles. Only the coloured
    // skin varies with the view; the pressure core must never become hollow.
    return float4(tint*(.85+flow*.12+spiral*.35),color.a*(.82+.18*edge));
}
