cbuffer CBFrame : register(b0) {row_major float4x4 view;row_major float4x4 proj;row_major float4x4 viewProj;float3 cameraPos;float time;};
struct PSIn {float4 pos:SV_POSITION;float3 worldPos:TEXCOORD0;float3 normal:TEXCOORD1;float2 uv:TEXCOORD2;float4 color:COLOR0;};
float4 main(PSIn i):SV_TARGET {
    float2 p=i.uv*2-1;float r=length(p),angle=atan2(p.y,p.x);
    int kind=(int)floor(i.color.a/2);float age=saturate(i.color.a-kind*2);
    float aa=max(.009,fwidth(r));float alpha=0,intensity=1;
    if(kind==0){
        float rim=1-smoothstep(aa,aa*3,abs(r-.92));
        float fill=(1-smoothstep(.87,.92,r))*(.1+.18*age);
        float countdown=1-smoothstep(aa,aa*3,abs(r-(.88*(1-age)+.035)));
        float ticks=pow(saturate(cos(angle*16-time*2)),12)*(1-smoothstep(.72,.8,r));
        alpha=max(rim*.65+fill,countdown*.7)+ticks*.12;intensity=1+age;
    }else if(kind==1||kind==2){
        float ringRadius=.12+age*.8;
        float ring=exp(-pow((r-ringRadius)/(.03+aa*2),2));
        float core=exp(-r*12)*(1-age);
        float rays=pow(saturate(cos(angle*(kind==1?18:11)+sin(angle*7)*2)),24);
        float shards=rays*exp(-pow((r-age*.75)/.13,2));
        alpha=(ring*.75+core+shards)*(1-age);intensity=2.5;
    }else if(kind==3){
        alpha=exp(-pow((r-(.15+age*.75))/.035,2))*(1-age)*.7;
    }else if(kind==5){
        // The quad is a 3m-radius capsule covering the committed 9.52m dash.
        float2 q=float2(p.x*3,p.y*7.76);q.y-=clamp(q.y,-4.76,4.76);
        float distance=length(q)/3;float edge=max(.015,fwidth(distance));
        alpha=(1-smoothstep(.93,.99,distance))*(.12+.15*age);
        alpha+=exp(-pow((distance-.96)/(edge*2),2))*.7;
        alpha+=(1-smoothstep(.7,.95,distance))*pow(saturate(sin(p.y*20-time*8)),12)*.15;
    }else{
        float ripples=pow(saturate(sin(r*32-age*16)),10);
        float cracks=pow(saturate(cos(angle*13+sin(r*25)*.7)),28);
        alpha=(ripples*.6+cracks*.35)*(1-smoothstep(.8,1,r))*(.5+.5*sin(age*3.14159));intensity=2;
    }
    clip(alpha-.008);return float4(i.color.rgb*intensity,saturate(alpha));
}
