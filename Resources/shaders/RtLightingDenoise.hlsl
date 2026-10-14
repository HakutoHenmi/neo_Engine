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
bool ValidLight(float4 light){return all(isfinite(light))&&abs(light.a)>1e-6;}
float PredictedHistoryDepth(Texture2D<float4> history,int2 q,float4 center,float2 delta,int2 limit){
    float inv=1/center.a;
    float4 l=history[max(q-int2(1,0),0)],r=history[min(q+int2(1,0),limit)];
    float4 u=history[max(q-int2(0,1),0)],d=history[min(q+int2(0,1),limit)];
    float left=ValidLight(l)&&l.a>0?1/l.a:inv,right=ValidLight(r)&&r.a>0?1/r.a:inv;
    float up=ValidLight(u)&&u.a>0?1/u.a:inv,down=ValidLight(d)&&d.a>0?1/d.a:inv;
    float2 gradient=float2(abs(left-inv)<abs(right-inv)?inv-left:right-inv,abs(up-inv)<abs(down-inv)?inv-up:down-inv);
    return 1/max(1e-6,inv+dot(gradient,delta));
}
float4 FilterLight(Texture2D<float4> current,Texture2D<float4> history,uint2 p,float2 oldUV,float oldZ){
    float4 center=current[p];if(!ValidLight(center))return 0;
    float3 sum=0,lo=1e10,hi=-1e10;float weights=0;
    // Reciprocal depth predicts steep ground across neighboring half-res texels.
    float inv=1/abs(center.a);int2 limit=int2(dimensions)-1;
    float4 left=current[max(int2(p)-int2(1,0),0)],right=current[min(int2(p)+int2(1,0),limit)];
    float4 up=current[max(int2(p)-int2(0,1),0)],down=current[min(int2(p)+int2(0,1),limit)];
    float l=ValidLight(left)?abs(left.a):0,r=ValidLight(right)?abs(right.a):0;
    float u=ValidLight(up)?abs(up.a):0,d=ValidLight(down)?abs(down.a):0;
    float dx=l>0&&r>0?(abs(1/l-inv)<abs(1/r-inv)?inv-1/l:1/r-inv):0;
    float dy=u>0&&d>0?(abs(1/u-inv)<abs(1/d-inv)?inv-1/u:1/d-inv):0;
    [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
        float4 tap=current[clamp(int2(p)+int2(x,y),0,limit)];
        // Reject before arithmetic: multiplying NaN/Inf radiance by zero still
        // contaminates the entire neighborhood and later DLSS/bloom history.
        if(!ValidLight(tap)||(tap.a<0)!=(center.a<0))continue;
        float predicted=1/max(1e-6,inv+dx*x+dy*y);
        float w=exp(-abs(abs(tap.a)-predicted)/max(.08,abs(center.a)*.004))*exp(-(x*x+y*y)*.35);
        sum+=tap.rgb*w;weights+=w;
        if(w>.1){lo=min(lo,tap.rgb);hi=max(hi,tap.rgb);}
    }
    float3 spatial=weights>1e-5?sum/weights:center.rgb;
    // Fluid shape animates independently of the camera: spatial filtering only.
    if(historyValid>.5&&center.a>0&&all(oldUV>0)&&all(oldUV<1)&&oldZ>0){
        uint dw,dh;depth.GetDimensions(dw,dh);float2 ratio=float2(dw,dh)/dimensions;
        // Trace samples are source-depth pixel centers, not the centers of the
        // half-resolution texture: for a 2:1 ratio their offset is .75, not .5.
        float2 sampleOffset=(floor(ratio*.5)+.5)/ratio;
        float2 at=oldUV*dimensions-sampleOffset;int2 base=int2(floor(at));float2 f=frac(at);
        float3 prior=0;float valid=0;
        [unroll]for(int j=0;j<2;++j)[unroll]for(int i=0;i<2;++i){
            int2 q=clamp(base+int2(i,j),0,limit);float4 h=history[q];
            if(!ValidLight(h)||h.a<=0)continue;
            float predicted=PredictedHistoryDepth(history,q,h,at-float2(q),limit);
            if(abs(predicted-oldZ)>=max(.12,oldZ*.006))continue;
            float w=(i?f.x:1-f.x)*(j?f.y:1-f.y);
            // Explicit view-depth test prevents lighting leaking across objects.
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
