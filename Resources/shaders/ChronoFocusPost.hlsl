// Chrono-only treatment. The aiming area stays sharp; temporal blur and cool
// grading live at the periphery, with UI composited separately afterwards.
Texture2D scene : register(t0);
Texture2D<float> sceneDepth : register(t3);
SamplerState linearClamp : register(s0);
cbuffer Post : register(b0) {
    float time,noise,strength,chroma,damage,scanline,slow,pad;
    float dofFocus,dofRange,dofStrength,dofNear;
    float dofFar,texelX,texelY,dofBossDepth;
};
float viewDepth(float2 uv){float depth=sceneDepth.SampleLevel(linearClamp,uv,0);return dofNear*dofFar/max(.0001,dofFar-depth*(dofFar-dofNear));}
float4 main(float4 position:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET {
    float3 original=scene.Sample(linearClamp,uv).rgb;
    float2 delta=uv-.5;
    float edge=smoothstep(.20,.62,length(delta));
    float amount=saturate(slow)*edge;
    float3 color=original;
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
    float luma=dot(color,float3(.2126,.7152,.0722));
    color=lerp(color,lerp(color,luma.xxx,.3)*float3(.80,1.05,1.18),amount*.6);
    color*=1-amount*.18;
    color=lerp(color,float3(.8,.08,.06),edge*damage*.22);
    return float4(color,1);
}
