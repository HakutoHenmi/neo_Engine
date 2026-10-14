// UV change per world unit on the hit triangle. This works for arbitrary
// mesh scale and skew, unlike a fixed mip level for every ray intersection.
float2 RayConeUVDensity(float3 edge1,float3 edge2,float2 uv1,float2 uv2){
    float3 normal=cross(edge1,edge2);float area2=dot(normal,normal);
    if(area2<1e-16)return 0;
    float3 a=cross(edge2,normal)/area2,b=cross(normal,edge1)/area2;
    return float2(length(a*uv1.x+b*uv2.x),length(a*uv1.y+b*uv2.y));
}
float RayConeMipLevel(float2 uvDensity,float footprint,float2 textureSize){
    return max(0,log2(max(1,max(uvDensity.x*textureSize.x,uvDensity.y*textureSize.y)*footprint)));
}
