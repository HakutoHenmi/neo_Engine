// Chrono-only treatment. The aiming area stays sharp; temporal blur and cool
// grading live at the periphery, with UI composited separately afterwards.
Texture2D scene : register(t0);
Texture2D<float> sceneDepth : register(t3);
Texture2D colorLut : register(t1);
Texture2D<float4> gtao : register(t2);
Texture2D<float4> hdrBloom : register(t4);
Texture2D<float> fluidSurfaceDepth : register(t5);
Texture2D<float2> rtVisibility : register(t6);
Texture2D<float4> rtReflections : register(t7);
Texture2D<float4> rtIndirectLight : register(t8);
SamplerState linearClamp : register(s0);
cbuffer Post : register(b0) {
    float time,noise,strength,chroma,damage,scanline,slow,pad;
    float dofFocus,dofRange,dofStrength,dofNear;
    float dofFar,texelX,texelY,dofBossDepth;
    float bloom,lensFlare,grading,ambientOcclusion;
    float motionBlur,exposure,projectionX,projectionY;
    float useFluidDepth,rtStrength,rtReflection,rtIndirect;
    row_major float4x4 inverseViewProjection,previousViewProjection;
};
float viewDepth(float2 uv){
    uint w,h;sceneDepth.GetDimensions(w,h);int2 at=clamp(int2(uv*float2(w,h)),int2(0,0),int2(w-1,h-1));
    float depth=sceneDepth.Load(int3(at,0));
    float z=dofNear*dofFar/max(.0001,dofFar-depth*(dofFar-dofNear));
    if(useFluidDepth>.5){float fluid=fluidSurfaceDepth.Load(int3(at,0));if(fluid>0)z=min(z,fluid);}return z;}
float3 viewPosition(float2 uv,float z){return float3((uv*float2(2,-2)+float2(-1,1))/float2(projectionX,projectionY)*z,z);}
float ambientShadow(float2 uv,float z){
    if(z>dofFar*.95)return 1;float sum=0,weights=0;
    uint w,h;gtao.GetDimensions(w,h);int2 center=int2(uv*float2(w,h));
    // Bilateral spatial reconstruction keeps foreground AO off distant surfaces.
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        int2 at=clamp(center+int2(x,y),int2(0,0),int2(w-1,h-1));float2 sample=gtao.Load(int3(at,0)).rg;
        float weight=exp(-abs(sample.y-z)/max(.08,z*.005))*(x==0?2:1)*(y==0?2:1);
        sum+=sample.x*weight;weights+=weight;
    }
    return lerp(1,weights>.0001?saturate(sum/weights):1,ambientOcclusion);
}
float3 highLight(float2 at){float3 c=scene.SampleLevel(linearClamp,saturate(at),0).rgb;
    float l=dot(c,float3(.2126,.7152,.0722));return c*saturate((l-1)/max(l,.0001));}
float3 lutGrade(float3 c){float3 p=saturate(c)*15;float low=floor(p.b),high=min(15,low+1);
    float2 a=float2(low*16+p.r+.5,p.g+.5)/float2(256,16),b=float2(high*16+p.r+.5,p.g+.5)/float2(256,16);
    return lerp(colorLut.SampleLevel(linearClamp,a,0).rgb,colorLut.SampleLevel(linearClamp,b,0).rgb,frac(p.b));}
float3 rayLighting(Texture2D<float4> light,float2 uv,float z){
    uint w,h;light.GetDimensions(w,h);int2 center=int2(uv*float2(w,h));float3 sum=0;float weights=0;
    float raw=dofFar/(dofFar-dofNear)-dofNear*dofFar/(dofFar-dofNear)/max(z,.001);
    uint dw,dh;sceneDepth.GetDimensions(dw,dh);int2 pixel=clamp(int2(uv*float2(dw,dh)),0,int2(dw,dh)-1);
    bool fluid=useFluidDepth>.5&&fluidSurfaceDepth.Load(int3(pixel,0))>0&&raw<sceneDepth.Load(int3(pixel,0))-.0000001;
    // Reciprocal view depth is affine on a projected plane. Predict each tap's
    // depth on that plane so steep distant ground does not alternate lit rows.
    float centerInverse=1/max(z,.001);
    float left=1/viewDepth(uv-float2(1.0/dw,0)),right=1/viewDepth(uv+float2(1.0/dw,0));
    float up=1/viewDepth(uv-float2(0,1.0/dh)),down=1/viewDepth(uv+float2(0,1.0/dh));
    float2 gradient=float2(abs(left-centerInverse)<abs(right-centerInverse)?centerInverse-left:right-centerInverse,abs(up-centerInverse)<abs(down-centerInverse)?centerInverse-up:down-centerInverse);
    [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
        int2 at=clamp(center+int2(x,y),0,int2(w,h)-1);float4 tap=light.Load(int3(at,0));
        float2 tapUV=(floor((float2(at)+.5)/float2(w,h)*float2(dw,dh))+.5)/float2(dw,dh);
        float expected=1/max(.000001,centerInverse+dot(gradient,(tapUV-uv)*float2(dw,dh)));
        float weight=exp(-abs(abs(tap.a)-expected)/max(.06,z*.003))*exp(-float(x*x+y*y)*.3);
        weight*=tap.a!=0&&((tap.a<0)==fluid)?1:0;sum+=tap.rgb*weight;weights+=weight;
    }
    return weights>.0001?sum/weights:0;
}
#ifndef RT_LIGHTING_COMPOSITE
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 original=scene.Sample(linearClamp,uv).rgb;
    float2 delta=uv-.5;
    float edge=smoothstep(.20,.62,length(delta));
    float amount=saturate(slow)*edge;
    float3 color=original;
    float centerZ=viewDepth(uv);
    [branch]if(motionBlur>.001&&centerZ<dofFar*.95){
        float raw=dofFar/(dofFar-dofNear)-dofNear*dofFar/(dofFar-dofNear)/max(centerZ,.001);
        float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),raw,1),inverseViewProjection);world/=world.w;
        float4 previous=mul(world,previousViewProjection);
        if(previous.w>.001){float2 old=previous.xy/previous.w*float2(.5,-.5)+.5;
            float2 velocity=(uv-old)*motionBlur*.5;
            float pixels=length(velocity/float2(texelX,texelY));velocity*=min(1,12/max(pixels,.001));
            if(pixels>.5){float3 sum=color;float weight=1;
                [unroll]for(int sample=1;sample<6;++sample){float2 at=saturate(uv-velocity*(sample/5.0));float depth=viewDepth(at);
                    float w=saturate(1-abs(depth-centerZ)/max(.5,centerZ*.03));sum+=scene.SampleLevel(linearClamp,at,0).rgb*w;weight+=w;}color=sum/weight;}}
    }
    // Normal gameplay skips the eight temporal-blur samples entirely.
    [branch]if(amount>.001){float3 blurred=0;
        [unroll]for(int i=0;i<8;++i)blurred+=scene.Sample(linearClamp,saturate(uv-delta*(i/7.0)*.06*strength*amount)).rgb;
        color=lerp(original,blurred/8,amount*.8);
    }
    [branch]if(dofStrength>.001){
        float z=viewDepth(uv);
        float coc=saturate((z-dofFocus-dofRange)/100)*dofStrength;
        // Preserve the boss's depth band and bright attack/trail colors.
        coc*=smoothstep(45,85,abs(z-dofBossDepth));
        float bright=max(original.r,max(original.g,original.b));
        float vivid=bright-min(original.r,min(original.g,original.b));
        coc*=1-smoothstep(.55,.85,bright)*smoothstep(.25,.55,vivid);
        [branch]if(coc>.03){
            const float2 taps[8]={float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),float2(.707,.707),float2(-.707,.707),float2(.707,-.707),float2(-.707,-.707)};
            float2 radius=float2(texelX,texelY)*coc*4;
            float3 sum=color;float weight=1;
            [unroll]for(int sampleIndex=0;sampleIndex<8;++sampleIndex){float2 at=saturate(uv+taps[sampleIndex]*radius);
                float sampleZ=viewDepth(at);float w=smoothstep(dofFocus+dofRange-8,dofFocus+dofRange,sampleZ);
                // Keep separate depth surfaces from bleeding into silhouette edges.
                w*=saturate(1-abs(sampleZ-z)/max(8,z*.03));
                sum+=scene.SampleLevel(linearClamp,at,0).rgb*w;weight+=w;}
            color=lerp(color,sum/weight,coc);
        }
    }
    [branch]if(rtStrength>.001&&centerZ<dofFar*.95){float visibility=0,weights=0;
        uint w,h;rtVisibility.GetDimensions(w,h);int2 center=int2(uv*float2(w,h));
        [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){int2 at=clamp(center+int2(x,y),int2(0,0),int2(w-1,h-1));float2 tap=rtVisibility.Load(int3(at,0));
            float weight=exp(-abs(tap.y-centerZ)/max(.15,centerZ*.008));visibility+=tap.x*weight;weights+=weight;}
        color*=lerp(1,weights>.0001?saturate(visibility/weights):1,rtStrength);
    }
    [branch]if(ambientOcclusion>.001)color*=ambientShadow(uv,centerZ);
    [branch]if(rtReflection>.001&&centerZ<dofFar*.95)color+=rayLighting(rtReflections,uv,centerZ)*rtReflection;
    [branch]if(rtIndirect>.001&&centerZ<dofFar*.95)color+=rayLighting(rtIndirectLight,uv,centerZ)*rtIndirect;
    color=max(0,color);
    [branch]if(bloom>.001)color+=hdrBloom.SampleLevel(linearClamp,uv,0).rgb*bloom;
    [branch]if(lensFlare>.001){float3 flare=0;
        // Bright-source ghosts; dark areas contribute no flare.
        [unroll]for(int ghost=1;ghost<=3;++ghost)flare+=highLight(.5-(uv-.5)*(ghost*.65));
        color+=flare/3*lensFlare*pow(saturate(1-length(uv-.5)),2);
    }
    float luma=dot(color,float3(.2126,.7152,.0722));
    color=lerp(color,lerp(color,luma.xxx,.3)*float3(.80,1.05,1.18),amount*.6);
    color*=1-amount*.18;
    color=lerp(color,float3(.8,.08,.06),edge*damage*.22);
    // Scene lighting is linear (sRGB albedo sampling); the UNORM display target needs explicit encoding.
    color=max(0,color*exposure);
    color=saturate((color*(2.51*color+.03))/(color*(2.43*color+.59)+.14));
    color=lerp(color*12.92,1.055*pow(color,1/2.4)-.055,step(.0031308,color));
    [branch]if(grading>.001)color=lerp(color,lutGrade(color),grading);
    return float4(color,1);
}
#endif
