// 最初から最後まで線形の FP16 の放射輝度。縮小の連鎖の前に 1 回だけしきい値をかける。
Texture2D<float4> source : register(t0);
Texture2D<float4> lowResolution : register(t1);
SamplerState linearClamp : register(s0);
cbuffer Bloom : register(b0) { float2 texel; float firstLevel; float unused; };
float3 extract(float3 c) {
    float brightness=max(c.r,max(c.g,c.b));
    float knee=clamp(brightness-.5,0,1);
    float contribution=max(brightness-1,knee*knee*.5);
    return c*contribution/max(brightness,.0001);
}
float4 Downsample(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 sum=0;
    // 元のテクセルの中心での箱型の積分で、明るい点のちらつきを防ぐ。
    [unroll] for(int y=-1;y<=2;++y) [unroll] for(int x=-1;x<=2;++x) {
        float3 c=source.SampleLevel(linearClamp,saturate(uv+(float2(x,y)-.5)*texel),0).rgb;
        sum+=firstLevel>.5?extract(c):c;
    }
    return float4(sum/16,1);
}
float4 Upsample(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 sum=0;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float weight=(x==0?2:1)*(y==0?2:1);
        sum+=lowResolution.SampleLevel(linearClamp,saturate(uv+float2(x,y)*texel),0).rgb*weight;
    }
    // 正規化したピラミッドの再構成: ミップの数が増えても強さは増えない。
    return float4(lerp(source.SampleLevel(linearClamp,uv,0).rgb,sum/16,.65),1);
}
