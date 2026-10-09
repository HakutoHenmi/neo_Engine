// GTAO cosine-weighted horizon integration (Jimenez et al., 2016).
// Independent implementation; four slices with six samples per side.
Texture2D<float> depthTexture : register(t0);
Texture2D<float> fluidDepth : register(t1);
SamplerState linearClamp : register(s0);
cbuffer Gtao : register(b0) { float nearPlane,farPlane,projX,projY;float2 texel;float includeFluid,radius; };
// Reconstruct the position of the depth sample, not the output pixel. DLSS
// input and post-processing targets can have different resolutions.
float2 depthUv(float2 uv) {
    uint w,h;depthTexture.GetDimensions(w,h);
    int2 pixel=clamp(int2(uv*float2(w,h)),int2(0,0),int2(w-1,h-1));
    return (float2(pixel)+.5)/float2(w,h);
}
float depthAt(float2 uv) {
    uint w,h;depthTexture.GetDimensions(w,h);int2 pixel=clamp(int2(uv*float2(w,h)),int2(0,0),int2(w-1,h-1));
    float raw=depthTexture.Load(int3(pixel,0));float z=nearPlane*farPlane/max(.00001,farPlane-raw*(farPlane-nearPlane));
    if(includeFluid>.5){float f=fluidDepth.Load(int3(pixel,0));if(f>0)z=min(z,f);}return z;
}
float3 positionAt(float2 uv,float z){return float3((uv*float2(2,-2)+float2(-1,1))/float2(projX,projY)*z,z);}
float arc(float h,float n){return (cos(n)+2*h*sin(n)-cos(2*h-n))*.25;}
float integrateHorizon(float2 horizon,float n){
    float positive=acos(clamp(horizon.x,-1,1)),negative=-acos(clamp(horizon.y,-1,1));
    positive=n+clamp(positive-n,-1.570796327,1.570796327);
    negative=n+clamp(negative-n,-1.570796327,1.570796327);
    return arc(positive,n)+arc(negative,n);
}
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    uv=depthUv(uv);
    uint w,h;depthTexture.GetDimensions(w,h);float2 depthTexel=1/float2(w,h);
    float z=depthAt(uv);if(z>farPlane*.95)return float4(1,min(z,65000),0,1);
    float3 p=positionAt(uv,z),v=normalize(-p);
    float2 dx=float2(depthTexel.x,0),dy=float2(0,depthTexel.y);
    float zl=depthAt(uv-dx),zr=depthAt(uv+dx),zt=depthAt(uv-dy),zb=depthAt(uv+dy);
    // Select the less discontinuous neighbour at silhouettes.
    float3 x=abs(zl-z)<abs(zr-z)?p-positionAt(uv-dx,zl):positionAt(uv+dx,zr)-p;
    float3 y=abs(zt-z)<abs(zb-z)?p-positionAt(uv-dy,zt):positionAt(uv+dy,zb)-p;
    float3 crossNormal=cross(x,y);float3 normal=dot(crossNormal,crossNormal)>.0000001?normalize(crossNormal):v;
    if(dot(normal,v)<0)normal=-normal;
    float visibility=0,unoccludedVisibility=0;
    [unroll] for(int slice=0;slice<4;++slice){
        float angle=(slice+.5)*.785398163;float2 screen=float2(cos(angle),sin(angle));
        // uvRadius below applies projection scaling, so the view-space slice
        // direction is screen itself (do not divide by projection twice).
        float3 axis=normalize(float3(screen.x,-screen.y,0));
        float3 tangent=normalize(axis-v*dot(axis,v));float3 binormal=normalize(cross(v,tangent));
        float3 projected=normal-binormal*dot(normal,binormal);float weight=length(projected);
        float n=atan2(dot(projected,tangent),dot(projected,v));
        float2 baseHorizon=cos(float2(n+1.570796327,n-1.570796327));float2 horizon=baseHorizon;
        float2 uvRadius=screen*float2(projX,projY)*radius/(2*max(z,.01));
        [unroll] for(int step=1;step<=6;++step)[unroll] for(int side=0;side<2;++side){
            float sign=side==0?1:-1;float t=step/6.0;t*=t;
            float2 at=uv+sign*uvRadius*t;if(any(at<depthTexel*.5)||any(at>1-depthTexel*.5))continue;
            at=depthUv(at);
            float sampleDepth=depthAt(at);if(sampleDepth>farPlane*.95)continue;
            float3 delta=positionAt(at,sampleDepth)-p;float distance=length(delta);
            if(distance<.001)continue;
            // Coplanar/below-surface samples cannot occlude the hemisphere.
            // A small world-space tolerance also covers depth quantization.
            if(dot(delta,normal)<=.01)continue;
            float falloff=1-smoothstep(radius*.3,radius,distance);
            // Texel rounding can move a sample sideways from this slice.
            // Integrate its projection into the slice, otherwise a tilted
            // unoccluded plane spuriously raises the horizon.
            float3 sliceDelta=delta-binormal*dot(delta,binormal);
            float sliceDistance=length(sliceDelta);if(sliceDistance<.001)continue;
            float candidate=dot(sliceDelta/sliceDistance,v)-.02;
            if(side==0)horizon.x=max(horizon.x,lerp(baseHorizon.x,candidate,falloff));
            else horizon.y=max(horizon.y,lerp(baseHorizon.y,candidate,falloff));
        }
        visibility+=weight*integrateHorizon(horizon,n);
        unoccludedVisibility+=weight*integrateHorizon(baseHorizon,n);
    }
    return float4(saturate(visibility/max(unoccludedVisibility,.00001)),min(z,65000),0,1);
}
