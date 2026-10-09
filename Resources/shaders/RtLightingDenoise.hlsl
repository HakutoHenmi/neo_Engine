// Filter only ray radiance, never the terrain's albedo/detail. Reproject static
// receivers, reject disocclusions and clamp history to the current neighborhood.
Texture2D<float4> currentReflection : register(t0);
Texture2D<float4> currentIndirect : register(t1);
Texture2D<float4> historyReflection : register(t2);
Texture2D<float4> historyIndirect : register(t3);
Texture2D<float> depth : register(t4);
RWTexture2D<float4> filteredReflection : register(u0);
RWTexture2D<float4> filteredIndirect : register(u1);
cbuffer Filter : register(b0){
    row_major float4x4 inverseVP,previousVP;
    float2 dimensions; float nearPlane,farPlane;
    float historyValid; float3 pad;
};
float4 FilterLight(Texture2D<float4> current,Texture2D<float4> history,uint2 p,float2 oldUV,float oldZ){
    float4 center=current[p];if(center.a==0)return 0;
    float3 sum=0,lo=1e10,hi=-1e10;float weights=0;
    // Reciprocal depth predicts steep ground across neighboring half-res texels.
    float inv=1/abs(center.a);int2 limit=int2(dimensions)-1;
    float l=abs(current[max(int2(p)-int2(1,0),0)].a),r=abs(current[min(int2(p)+int2(1,0),limit)].a);
    float u=abs(current[max(int2(p)-int2(0,1),0)].a),d=abs(current[min(int2(p)+int2(0,1),limit)].a);
    float dx=l>0&&r>0?(abs(1/l-inv)<abs(1/r-inv)?inv-1/l:1/r-inv):0;
    float dy=u>0&&d>0?(abs(1/u-inv)<abs(1/d-inv)?inv-1/u:1/d-inv):0;
    [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
        float4 tap=current[clamp(int2(p)+int2(x,y),0,limit)];
        float predicted=1/max(1e-6,inv+dx*x+dy*y);
        float w=exp(-abs(abs(tap.a)-predicted)/max(.08,abs(center.a)*.004))*exp(-(x*x+y*y)*.35);
        if(tap.a==0||(tap.a<0)!=(center.a<0))w=0;
        sum+=tap.rgb*w;weights+=w;
        if(w>.1){lo=min(lo,tap.rgb);hi=max(hi,tap.rgb);}
    }
    float3 spatial=weights>1e-5?sum/weights:center.rgb;
    // Fluid shape animates independently of the camera: spatial filtering only.
    if(historyValid>.5&&center.a>0&&all(oldUV>0)&&all(oldUV<1)&&oldZ>0){
        float2 at=oldUV*dimensions-.5;int2 base=int2(floor(at));float2 f=frac(at);
        float3 prior=0;float valid=0;
        [unroll]for(int j=0;j<2;++j)[unroll]for(int i=0;i<2;++i){
            int2 q=clamp(base+int2(i,j),0,limit);float4 h=history[q];
            float w=(i?f.x:1-f.x)*(j?f.y:1-f.y);
            // Explicit view-depth test prevents lighting leaking across objects.
            w*=h.a>0&&abs(h.a-oldZ)<max(.12,oldZ*.006)?1:0;
            prior+=h.rgb*w;valid+=w;
        }
        if(valid>.5){float3 margin=max(float3(.005,.005,.005),(hi-lo)*.1);
            spatial=lerp(spatial,clamp(prior/valid,lo-margin,hi+margin),.88*valid);}
    }
    return float4(spatial,center.a);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=uint2(dimensions)))return;uint2 p=id.xy;
    uint w,h;depth.GetDimensions(w,h);uint2 pixel=min(uint2((float2(p)+.5)/dimensions*float2(w,h)),uint2(w-1,h-1));
    float2 uv=(float2(pixel)+.5)/float2(w,h);
    float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),depth[pixel],1),inverseVP);world/=world.w;
    float4 old=mul(world,previousVP);float2 oldUV=old.xy/max(old.w,.001)*float2(.5,-.5)+.5;
    filteredReflection[p]=FilterLight(currentReflection,historyReflection,p,oldUV,old.w);
    filteredIndirect[p]=FilterLight(currentIndirect,historyIndirect,p,oldUV,old.w);
}
