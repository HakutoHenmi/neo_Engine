#ifndef SLIME_DECORATION
#define SLIME_DECORATION
static const uint slimeInteriorBubbles=224;
static const uint slimeEscapedBubbles=24;
static const uint slimeDecorationCount=slimeInteriorBubbles+slimeEscapedBubbles+2;

float3 SlimeHash(float id) {
    return frac(sin(float3(id*12.9898f+3.1f,id*78.233f+9.2f,id*39.346f+1.7f))*43758.5453f);
}
float SlimeDecorationScale() { return playerSlope.y>0 ? playerSlope.y : 1; }
float3 SlimeTravelForward() { return normalize(float3(playerForward.x,0,playerForward.z)+float3(0,0,1e-6f)); }
float3 SlimeFaceForward() {
    float3 forward=SlimeTravelForward(),right=float3(forward.z,0,-forward.x);
    float3 delta=cameraPosition-playerBodyCenter;
    float angle=atan2(dot(delta,right),dot(delta,forward))*saturate(playerFaceCamera);
    return forward*cos(angle)+right*sin(angle);
}
float3 SlimeFieldPosition(float3 field) {
    float3 forward=SlimeTravelForward(),right=float3(forward.z,0,-forward.x);
    float stretch=1+.55f*playerMotion;
    float width=1+playerMotion*.26f*clamp(field.z/max(playerRadii.z,.1f),-1.f,1.f);
    float3 local=right*(field.x*width/sqrt(stretch))+float3(0,field.y*width/sqrt(stretch),0)+forward*(field.z*stretch);
    local.y+=dot(local.xz,playerSlope.xz)*(1-playerAir);
    return playerBodyCenter+local;
}
float3 SlimeEyeRadii() { return float3(.145f,.285f,.105f)*SlimeDecorationScale(); }
float3 SlimeEyeGuide(uint eye) {
    float3 front=SlimeFaceForward(),right=float3(front.z,0,-front.x);
    return playerBodyCenter+right*((eye==0?-1:1)*.46f*SlimeDecorationScale())+float3(0,playerRadii.y*.48f,0);
}
float3 SlimeBubblePosition(uint id,out float radius,out float visibility) {
    float3 random=SlimeHash(float(id));
    float age=frac(time*(.10f+.035f*random.y)+random.x);
    float angle=float(id)*2.39996323f+time*.22f;
    float radial=sqrt(random.z)*.78f;
    float2 spread=float2(cos(angle),sin(angle))*radial;
    // Three uneven rising plumes, with a minority of freely dispersed cavities.
    if(id%4!=0){float cluster=float(id%3)*2.094395f;
        spread=spread*.32f+float2(cos(cluster),sin(cluster))*.33f;}
    float height=lerp(-.62f,.08f,1-playerAir)+age*(playerAir>.5f?1.42f:.76f);
    float3 field=float3(spread.x*playerRadii.x,height*playerRadii.y,spread.y*playerRadii.z);
    radius=lerp(.033f,.095f,pow(random.y,2))*SlimeDecorationScale();
    if(id%29==0)radius=.115f*SlimeDecorationScale();
    visibility=smoothstep(0,.08f,age)*(1-smoothstep(.88f,1,age));
    return SlimeFieldPosition(field);
}
// Eye/cavity geometry uses rigid world-space radii. Body deformation moves
// centers only; it never scales, shears or projects a surface texture onto them.
bool SlimeEllipsoidHit(float3 origin,float3 ray,float3 center,float3 radii,float3 forward,out float distance,out float3 normal) {
    float3 right=float3(forward.z,0,-forward.x),delta=origin-center;
    float3 local=float3(dot(delta,right),delta.y,dot(delta,forward))/radii;
    float3 direction=float3(dot(ray,right),ray.y,dot(ray,forward))/radii;
    float a=dot(direction,direction),b=dot(local,direction),c=dot(local,local)-1;
    float discriminant=b*b-a*c;distance=0;normal=0;
    if(discriminant<=0)return false;
    distance=(-b-sqrt(discriminant))/a;
    if(distance<0)return false;
    float3 gradient=(local+direction*distance)/radii;
    normal=normalize(right*gradient.x+float3(0,gradient.y,0)+forward*gradient.z);
    return true;
}
#endif
