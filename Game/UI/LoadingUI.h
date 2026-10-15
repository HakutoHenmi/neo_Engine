#pragma once
#include "GameUI.h"
#include "../../Engine/LoadingScreen.h"
#include "../../Engine/SceneManager.h"
#include <array>
#include <chrono>
#include <cmath>

// Loading screen and scene transitions. Everything here shares the slime motif:
// transitions pour a dark slime over the screen and drain it away, the loading
// screen sits "inside" that slime, and the corner mark is a slime tracing a loop.
namespace Game::UI {
namespace Loading {
inline constexpr const char* Circle = "Resources/Textures/UI/slime_circle.png";
inline constexpr const char* Glow = "Resources/Textures/UI/slime_glow.png";
inline const Engine::Vector4 Slime{.022f,.045f,.035f,1}, SlimeEdge{.09f,.19f,.12f,1};

inline float Seconds() {
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
}
inline float Hash(int i) { float v=std::sin(float(i)*12.9898f+78.233f)*43758.5453f; return v-std::floor(v); }
inline float Smooth(float a,float b,float x) { float t=std::clamp((x-a)/(b-a),0.f,1.f); return t*t*(3-2*t); }
inline float EaseInOut(float t) { t=std::clamp(t,0.f,1.f); return t<.5f?4*t*t*t:1-std::pow(-2*t+2,3.f)*.5f; }
inline Engine::Vector4 Alpha(Engine::Vector4 c,float a) { c.w*=a; return c; }
inline void Dot(Canvas& ui,float x,float y,float r,Engine::Vector4 c) { ui.Image(Circle,{x-r,y-r,r*2,r*2},c); }
inline void Halo(Canvas& ui,float x,float y,float r,Engine::Vector4 c) { ui.Image(Glow,{x-r,y-r,r*2,r*2},c); }

// ---- Slime wipe ---------------------------------------------------------------
// Narrow columns with overlapping round caps form one wavy, dripping front.
// Three layers (glossy lime rim, green band, dark body) give the slime its depth.
// Cover pours down from the top; draining lets the top edge fall away (reveal).
inline void SlimeWipe(Canvas& ui,float t,bool draining) {
    constexpr int columns=28;
    const float width=1280.f/columns,radius=width*.68f,span=720+radius*2.4f;
    const float time=Seconds();
    struct Layer { float lead,cap; Engine::Vector4 color; };
    // Covering, the rim runs ahead of the body; draining, it trails behind so it stays on top.
    const float dir=draining?-1.f:1.f;
    // Lower layers use smaller caps so the glossy rim stays one continuous outline.
    const Layer layers[]={{.04f*dir,1.f,{.36f,.66f,.21f,1}},{.02f*dir,.86f,SlimeEdge},{0,.72f,Slime}};
    for(int layer=0;layer<3;++layer){
        for(int i=0;i<columns;++i){
            const float delay=.13f*(.5f+.5f*std::sin(float(i)*.55f+1.3f))+.07f*Hash(i*7+3);
            const float u=EaseInOut((t*1.06f+layers[layer].lead-delay)/.8f);
            const float wobble=std::sin(time*3.1f+float(i)*.9f)*5*(1-std::abs(u*2-1));
            const float x=float(i)*width,cx=x+width*.5f,edge=u*span-radius*1.2f+wobble;
            const auto color=layers[layer].color;
            if(!draining){
                if(edge<=-radius)continue;
                ui.Fill({x-.5f,-4,width+1,std::max(0.f,edge)+4},color);
                Dot(ui,cx,edge,radius*layers[layer].cap,color);
                // Thin strands stretch from the rim mid-pour and snap back as it lands.
                if(layer==0&&i%3==1){
                    const float strand=(30+60*Hash(i*5))*std::sin(3.14159f*std::clamp(u,0.f,1.f));
                    if(strand>2){ui.Fill({cx-3,edge,6,strand},color);Dot(ui,cx,edge+strand,6.5f,color);}
                }
            }else{
                if(edge>=720+radius)continue;
                const float top=std::max(-4.f,edge);
                ui.Fill({x-.5f,top,width+1,724-top},color);
                Dot(ui,cx,edge,radius*layers[layer].cap,color);
            }
        }
    }
}
inline void RingLine(Canvas& ui,float x,float y,float radius,float width,Engine::Vector4 color) {
    constexpr int segments=96;
    for(int i=0;i<segments;++i){
        const float a0=6.2831853f*float(i)/segments,a1=6.2831853f*float(i+1)/segments;
        ui.Line(x+std::cos(a0)*radius,y+std::sin(a0)*radius,x+std::cos(a1)*radius,y+std::sin(a1)*radius,width,color);
    }
}
// The game's loop motif: a lime ring that closes and bursts outward.
inline void LoopBurst(Canvas& ui,float t) {
    if(t<=0||t>=1)return;
    const float grow=EaseInOut(t),fade=1-Smooth(.45f,1.f,t);
    RingLine(ui,640,360,40+700*grow,3+6*(1-grow),Alpha(Lime,fade*.9f));
    RingLine(ui,640,360,20+520*grow,2,Alpha(Gold,fade*.45f));
}

// ---- Corner mark: a slime tracing an infinity loop ------------------------------
inline void LogoMark(Canvas& ui,float cx,float cy,float alpha) {
    if(alpha<=.01f)return;
    Canvas mark=ui;mark.MultiplyAlpha(alpha);
    const float time=Seconds();
    Halo(mark,cx,cy,74,Alpha(Lime,.18f+.06f*std::sin(time*2)));
    auto loop=[&](float s){return std::array<float,2>{cx+std::cos(s)*48,cy+std::sin(s*2)*20};};
    constexpr int trail=30;
    for(int k=trail;k>=0;--k){
        const float s=time*2.4f-float(k)*.075f,fade=1-float(k)/trail;
        auto p=loop(s);
        Dot(mark,p[0],p[1],1.2f+4.2f*fade,k==0?Paper:Alpha(Lime,.15f+.75f*fade));
    }
    // Squash-and-stretch body that stays on its baseline.
    const float wobble=std::sin(time*3.4f),w=21*(1+.09f*wobble),h=21*(1-.09f*wobble),base=cy+19;
    mark.Image(Circle,{cx-w,base-h*2,w*2,h*2},Engine::Vector4{.47f,.78f,.25f,1});
    mark.Image(Circle,{cx-w*.86f,base-h*1.9f,w*1.72f,h*1.72f},Engine::Vector4{.62f,.90f,.33f,1});
    Dot(mark,cx-w*.38f,base-h*1.45f,4.5f,Alpha(Paper,.75f));
    const bool blink=std::fmod(time,3.3f)>3.15f;
    const float eye=blink?1.2f:5.f;
    for(float side:{-1.f,1.f})mark.Image(Circle,{cx+side*7.5f-3,base-h-4-eye*.5f,6,eye},Ink);
}

// ---- Tutorial pages ---------------------------------------------------------------
struct Page { const char* title; std::array<const char*,4> lines; const char *key,*keyLabel,*pad,*padLabel; };
inline const std::array<Page,6>& Pages() {
    static const std::array<Page,6> pages{{
        {"軌跡を描く",{"地面を移動すると、スライムの軌跡が残ります。","止まらずに描き続けるのが基本です。","ジャンプ中は線が途切れるので注意。",""},"keyboard_w","MOVE","xbox_stick_l","MOVE"},
        {"ループで囲む",{"軌跡を交差させて囲いを作ると","囲いが青緑色に光ります。","一筆でたくさん囲うほど弾数と威力が増えます。",""},nullptr,nullptr,nullptr,nullptr},
        {"撃ち放つ",{"光っている囲いを確定すると、","囲いから弾が飛び出して周囲の敵へ殺到。","発射中も移動して次のループを描けます。",""},"mouse_left","FIRE","xbox_rt","FIRE"},
        {"回避する",{"攻撃の予告が出たら回避で切り抜けよう。","ギリギリで避けるとパーフェクト回避。","地上の回避も軌跡を描きます。",""},"mouse_right","DODGE","xbox_button_b","DODGE"},
        {"スライム進化",{"敵を倒してレベルが上がると","強化カードを1枚選べます。","同じタグを3枚集めるとシナジーが発動。",""},"keyboard_enter","CHOOSE","xbox_button_a","CHOOSE"},
        {"大蛇を倒せ",{"1000体倒すと大蛇が地中から現れます。","弾で装甲を溶かし、露出したコアを撃ち抜こう。","コアを3回破壊すれば勝利です。",""},nullptr,nullptr,nullptr,nullptr},
    }};
    return pages;
}

inline void Trail(Canvas& ui,const std::vector<std::array<float,2>>& points,float width,Engine::Vector4 color) {
    for(size_t i=1;i<points.size();++i)ui.Line(points[i-1][0],points[i-1][1],points[i][0],points[i][1],width,color);
    for(const auto& p:points)Dot(ui,p[0],p[1],width*.5f,color);
}
inline void MiniSlime(Canvas& ui,float x,float y,float r) {
    Dot(ui,x,y,r,Engine::Vector4{.55f,.86f,.30f,1});
    Dot(ui,x-r*.35f,y-r*.35f,r*.25f,Alpha(Paper,.7f));
    for(float side:{-1.f,1.f})Dot(ui,x+side*r*.33f,y-r*.05f,r*.14f,Ink);
}
// Each page is a small looping diagram inside a 400 x 300 box.
inline void Illustration(Canvas& ui,int page,float ox,float oy,float time) {
    std::vector<std::array<float,2>> path;
    if(page==0){
        const float u=std::fmod(time*.22f,1.f);
        for(int i=0;i<=60;++i){float s=float(i)/60*u;path.push_back({ox+30+340*s,oy+150+70*std::sin(s*7.5f)});}
        Trail(ui,path,10,Alpha(Lime,.85f));
        MiniSlime(ui,path.back()[0],path.back()[1]-4,16);
    }else if(page==1||page==2){
        const float cycle=page==1?3.2f:3.6f,u=std::fmod(time/cycle,1.f);
        const float draw=page==1?std::min(1.f,u/.75f):1.f;
        auto at=[&](float s){float a=-2.4f+s*7.2f,r=88-18*s;return std::array<float,2>{ox+200+std::cos(a)*r*1.25f,oy+160+std::sin(a)*r};};
        for(int i=0;i<=80;++i)path.push_back(at(float(i)/80*draw));
        const bool closed=page==2||u>.75f;
        if(closed){float pulse=page==1?1-Smooth(.75f,1.f,u)*.6f:.55f+.15f*std::sin(time*6);
            Halo(ui,ox+200,oy+160,120,Alpha(Engine::Vector4{.2f,1,.8f,1},.35f*pulse));}
        Trail(ui,path,10,closed?Engine::Vector4{.35f,1,.85f,1}:Alpha(Lime,.85f));
        if(page==1){MiniSlime(ui,path.back()[0],path.back()[1]-4,15);
            if(closed)ui.Center("LOOP!",ox+200,oy+140,30,Gold);}
        else{
            const float enemies[3][2]={{70,52},{200,40},{330,52}};
            for(auto& e:enemies)Dot(ui,ox+e[0],oy+e[1],11,Engine::Vector4{.85f,.35f,.22f,1});
            for(int k=0;k<14;++k){
                float p=std::fmod(time*.9f+float(k)/14,1.f);auto from=at(float(k)/14);const auto& e=enemies[k%3];
                float x=from[0]+(ox+e[0]-from[0])*p,y=std::max(oy+14,from[1]+(oy+e[1]-from[1])*p-std::sin(p*3.14159f)*50);
                Dot(ui,x,y,4.5f,Alpha(Engine::Vector4{.4f,1,.9f,1},1-p*.5f));
            }
        }
    }else if(page==3){
        const float u=std::fmod(time/2.6f,1.f),dash=Smooth(.48f,.62f,u);
        const float warn=u<.55f?1.f:1-Smooth(.55f,.7f,u);
        ui.Fill({ox+120,oy+90,160,120},Alpha(Gold,.12f*warn));
        for(float y:{oy+90.f,oy+210.f})ui.Fill({ox+120,y,160,3},Alpha(Gold,warn));
        for(float x:{ox+120.f,ox+277.f})ui.Fill({x,oy+90,3,120},Alpha(Gold,warn));
        const float x=ox+200+dash*140;
        for(int k=3;k>0;--k)Dot(ui,x-float(k)*28*dash,oy+150,15,Alpha(Lime,.12f*float(4-k)*dash));
        MiniSlime(ui,x,oy+150,16);
        if(u>.6f&&u<.9f)ui.Center("PERFECT DODGE",ox+200,oy+240,24,Gold);
    }else if(page==4){
        const int chosen=int(time/1.3f)%3;
        for(int i=0;i<3;++i){
            const float lift=i==chosen?12*Smooth(0,.25f,std::fmod(time,1.3f)):0.f;
            Rect card{ox+28+float(i)*122,oy+60-lift,108,170};
            ui.Panel(card,i==chosen?Lime:Engine::Vector4{.18f,.30f,.22f,1});
            ui.Panel({card.x+3,card.y+3,card.w-6,card.h-6},Ink);
            Dot(ui,card.x+54,card.y+60,22,i==0?Engine::Vector4{.2f,1,.75f,1}:i==1?Gold:Lime);
            ui.Center(i==0?"TRAIL":i==1?"SHOT":"BODY",card.x+54,card.y+108,18,Paper);
        }
        ui.Center("x3  SYNERGY",ox+200,oy+250,22,Gold);
    }else{
        for(int i=11;i>=0;--i){
            const float s=float(i)/11,x=ox+40+320*s,y=oy+170+std::sin(time*2.2f-s*5)*36;
            Dot(ui,x,y,24-12*s,Engine::Vector4{.16f,.22f,.27f,1});
            Dot(ui,x,y-6+2*s,8-4*s,Engine::Vector4{.27f,.34f,.39f,1});
        }
        const float hy=oy+170+std::sin(time*2.2f)*36;
        Dot(ui,ox+40,hy,28,Engine::Vector4{.2f,.26f,.31f,1});
        Dot(ui,ox+50,hy-8,4,Engine::Vector4{.6f,.9f,1,1});
        const float core=.6f+.4f*std::sin(time*5);
        const float cy=oy+170+std::sin(time*2.2f-2.2f)*36;
        Halo(ui,ox+180,cy,46,Alpha(Gold,.5f*core));Dot(ui,ox+180,cy,10,Gold);
        for(int k=0;k<6;++k){float p=std::fmod(time*1.2f+float(k)/6,1.f);
            Dot(ui,ox+180+(float(k)-2.5f)*40*(1-p),oy+290-(290-(cy-oy))*p,4,Alpha(Engine::Vector4{.4f,1,.9f,1},1-p*.4f));}
        ui.Bar({ox+90,oy+262,220,8},1-std::fmod(time*.2f,1.f),Gold);
    }
}

struct PagerState { int page=0; float idle=0,slide=0,shown=0,lastElapsed=0,last=0; };
inline PagerState& Pager() { static PagerState state; return state; }


// The full loading screen. alpha fades the content; the logo mark is always shown.
// Runs on the loading thread: all input and the active device come from `state`.
inline void LoadingContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha,bool interactive) {
    auto& pager=Pager();
    const float now=Seconds(),dt=std::clamp(now-pager.last,0.f,.1f);pager.last=now;
    const int count=int(Pages().size());
    // Each new load opens on the next tip.
    if(state.elapsed<pager.lastElapsed){pager.shown=0;pager.page=(pager.page+1)%count;pager.idle=0;}
    pager.lastElapsed=state.elapsed;
    if(interactive){
        int step=state.step;
        if(state.clicked){
            if(ui.HitPixel({126,296,50,64},state.clickX,state.clickY))step=-1;
            else if(ui.HitPixel({1104,296,50,64},state.clickX,state.clickY)||ui.HitPixel({190,112,900,430},state.clickX,state.clickY))step=1;
        }
        pager.idle+=dt;
        if(!step&&pager.idle>5.f)step=1;
        if(step){pager.page=(pager.page+step+count)%count;pager.slide=float(step);pager.idle=0;}
    }
    pager.slide*=std::exp(-12*dt);
    if(alpha<=.01f)return;
    Canvas c=ui;c.MultiplyAlpha(alpha);

    // Header in the same style as every other screen.
    c.SlimeMark(64,64,14);c.Text(GameTitle,90,46,22,Paper,JapaneseFont);
    c.Label(state.finished?"READY":"NOW LOADING",92,76,12,WithAlpha(Lime,.85f),2.8f);

    // Tutorial card, sliding in from the side the player flipped toward.
    const Page& page=Pages()[size_t(pager.page)];
    Canvas card=c;card.MultiplyAlpha(1-std::abs(pager.slide)*.8f);
    const float ox=190+pager.slide*90,oy=112;
    card.Rounded({ox,oy,900,430},{.06f,.1f,.08f,.97f},24);card.RoundedRing({ox,oy,900,430},WithAlpha(Paper,.08f),24);
    card.Label("HOW TO PLAY",ox+26,oy+22,13,Lime,3.2f);
    const std::string counter="0"+std::to_string(pager.page+1)+"  /  0"+std::to_string(count);
    card.Label(counter,ox+874-card.SpacedWidth(counter,13,2.4f),oy+22,13,Muted,2.4f);
    card.Text(page.title,ox+24,oy+44,34,Paper,JapaneseFont);
    card.Fill({ox+24,oy+96,400,300},{.03f,.06f,.045f,1});
    for(int i=1;i<4;++i){card.Fill({ox+24+float(i)*100,oy+96,1,300},{.06f,.11f,.08f,1});card.Fill({ox+24,oy+96+float(i)*75,400,1},{.06f,.11f,.08f,1});}
    Illustration(card,pager.page,ox+24,oy+96,now);
    for(size_t i=0;i<page.lines.size();++i)if(page.lines[i]&&*page.lines[i])
        card.Text(page.lines[i],ox+452,oy+110+float(i)*42,20,Paper,JapaneseFont);
    if(page.key){
        if(state.padMode)card.PadPrompt(page.pad,page.padLabel,ox+452,oy+260,40,24);
        else card.Prompt(page.key,page.keyLabel,ox+452,oy+260,40,24);
    }

    // Page arrows and dots.
    for(int side=0;side<2;++side){Rect b{side?1104.f:126.f,296,50,64};
        const bool hot=!state.padMode&&c.HitPixel(b,state.mouseX,state.mouseY);
        c.Panel(b,hot?Engine::Vector4{.22f,.35f,.27f,1}:Ink);c.Center(side?">":"<",b.x+25,b.y+12,36,hot?Paper:Lime);}
    for(int i=0;i<count;++i){const bool on=i==pager.page;Dot(c,640+(float(i)-(count-1)*.5f)*22,560,on?6.f:4.f,on?Lime:Engine::Vector4{.22f,.32f,.25f,1});}
    if(state.padMode){float x=c.PadPrompt("xbox_dpad","ページ送り",530,576,22,15,Muted,JapaneseFont);
        c.PadPrompt("xbox_lb","",x+14,576,22,15);c.PadPrompt("xbox_rb","",x+42,576,22,15);}
    else c.Center("← →  /  A D  /  クリックでページ送り",640,578,15,Muted,JapaneseFont);

    // Progress: smoothed so jumps in the estimate never look like stalls.
    pager.shown+=(state.progress-pager.shown)*(1-std::exp(-(state.finished?20.f:6.f)*dt));
    const float shown=state.finished?std::max(pager.shown,.995f):pager.shown;
    const bool engine=state.scene.find("+Engine")!=std::string::npos;
    const char* status=state.finished?"準備完了":engine&&state.progress<.62f?"グラフィックエンジンを準備しています":
        state.scene.rfind("Game",0)==0?"フィールドを生成しています":"読み込んでいます";
    std::string dots(state.finished?0:size_t(std::fmod(now*2.5f,4.f)),'.');
    c.Text(std::string(status)+dots,190,612,18,Paper,JapaneseFont);
    const std::string percent=std::to_string(int(std::round(shown*100)))+"%";
    c.Text(percent,1090-c.Width(percent,28),604,28,Lime);
    const Rect bar{190,646,900,10};
    c.Fill(bar,{.08f,.15f,.11f,1});
    const float fill=bar.w*std::clamp(shown,0.f,1.f);
    if(fill>0){
        c.Fill({bar.x,bar.y,fill,bar.h},Lime);
        const float sweep=std::fmod(now*.7f,1.f)*(fill+120)-60;
        const float s0=std::clamp(sweep,0.f,fill),s1=std::clamp(sweep+60,0.f,fill);
        if(s1>s0)c.Fill({bar.x+s0,bar.y,s1-s0,bar.h},Alpha(Paper,.35f));
        Halo(c,bar.x+fill,bar.y+bar.h*.5f,26,Alpha(Lime,.6f));
        Dot(c,bar.x+fill,bar.y+bar.h*.5f+std::sin(now*6)*1.5f,8,Engine::Vector4{.62f,.90f,.33f,1});
    }
}

// ---- Startup splash: the neoEngine wordmark --------------------------------------
inline float SpacedWidth(Canvas& ui,const std::string& text,float size,float spacing) {
    float width=0;for(char ch:text)width+=ui.Width(std::string(1,ch),size)+spacing;
    return text.empty()?0:width-spacing;
}
inline void SpacedText(Canvas& ui,const std::string& text,float x,float y,float size,float spacing,Engine::Vector4 color) {
    for(char ch:text){const std::string glyph(1,ch);ui.Text(glyph,x,y,size,color);x+=ui.Width(glyph,size)+spacing;}
}
inline constexpr const char* SplashTagline="REAL-TIME SLIME ENGINE  /  DIRECTX 12";

// An infinity loop is drawn around the wordmark, closes with a flash, then the
// letters rise in. A highlight keeps running along the loop while the game loads.
inline void SplashContent(Canvas& ui,const Engine::LoadingScreen::State& state,float alpha) {
    if(alpha<=.01f)return;
    Canvas c=ui;c.MultiplyAlpha(alpha);
    const float t=state.elapsed,now=Seconds(),cx=640,cy=318;
    auto at=[&](float s){const float a=1.5707963f+6.2831853f*s;return std::array<float,2>{cx+std::cos(a)*330,cy+std::sin(a)*std::cos(a)*150};};
    const float draw=EaseInOut(t/1.1f),settled=Smooth(1.1f,1.6f,t);
    constexpr int segments=160;
    for(int i=0;i<segments;++i){
        const float s0=float(i)/segments;if(s0>=draw)break;
        const float s1=std::min(float(i+1)/segments,draw);
        auto p0=at(s0),p1=at(s1);
        c.Line(p0[0],p0[1],p1[0],p1[1],5,Alpha(Lime,.3f+.7f*(1-settled)));
    }
    if(draw<1){auto head=at(draw);Halo(c,head[0],head[1],42,Alpha(Lime,.7f));Dot(c,head[0],head[1],7,Paper);}
    else for(int k=0;k<20;++k){
        float s=std::fmod(now*.3f-float(k)*.006f,1.f);if(s<0)s+=1;
        auto p=at(s);const float fade=1-float(k)/20;
        Dot(c,p[0],p[1],1+4*fade,Alpha(Paper,.85f*fade*settled));
    }
    const float flash=Smooth(1.f,1.15f,t)*(1-Smooth(1.15f,1.9f,t));
    if(flash>0){Halo(c,cx,cy,280,Alpha(Lime,.3f*flash));RingLine(c,cx,cy,60+320*Smooth(1.05f,1.9f,t),3,Alpha(Lime,flash));}

    // Wordmark: "neo" in paper, "Engine" in lime, each letter rising in turn.
    const float size=104,spacing=4,gap=18;
    const float wNeo=SpacedWidth(c,"neo",size,spacing),wEngine=SpacedWidth(c,"Engine",size,spacing);
    float x=cx-(wNeo+gap+wEngine)*.5f;
    const float y=cy-size*.62f;
    int index=0;
    auto word=[&](const std::string& text,Engine::Vector4 color){
        for(char ch:text){
            const std::string glyph(1,ch);
            const float appear=Smooth(.75f+float(index)*.06f,1.05f+float(index)*.06f,t);
            if(appear>0){Canvas letter=c;letter.MultiplyAlpha(appear);letter.Text(glyph,x,y+22*(1-appear),size,color);}
            x+=c.Width(glyph,size)+spacing;++index;
        }
    };
    word("neo",Paper);x+=gap-spacing;word("Engine",Lime);
    const float underline=EaseInOut(Smooth(1.3f,1.8f,t));
    if(underline>0)c.Fill({cx-260*underline,cy+54,520*underline,2},Alpha(Lime,.9f));
    const float tagline=Smooth(1.6f,2.1f,t);
    if(tagline>0){Canvas g=c;g.MultiplyAlpha(tagline);
        SpacedText(g,SplashTagline,cx-SpacedWidth(g,SplashTagline,17,3)*.5f,cy+72,17,3,Muted);}

    // A quiet progress line; once loaded, the splash can be skipped.
    const float barWidth=220;
    c.Fill({cx-barWidth*.5f,640,barWidth,2},Alpha(Paper,.12f));
    c.Fill({cx-barWidth*.5f,640,barWidth*std::clamp(state.progress,0.f,1.f),2},Alpha(Lime,.85f));
    if(state.finished&&t>=2.4f)c.Center(state.padMode?"PRESS ANY BUTTON":"PRESS ANY KEY",cx,654,15,Alpha(Muted,.45f+.25f*std::sin(now*4)));
}

// Drawn (fully transparent) on the main thread before the loading thread starts,
// so every texture and glyph the loading thread uses is already cached.
inline void Warmup(Canvas& ui) {
    Canvas w=ui;w.MultiplyAlpha(0);
    Dot(w,0,0,4,Lime);Halo(w,0,0,4,Lime);w.Panel({0,0,40,40},Ink);w.Bar({0,0,40,8},.5f);w.Line(0,0,10,10,2,Lime);
    w.RoundedRing({0,0,40,40},Lime);w.Grain();w.Brush({0,0,40,10},Lime);w.Splat(0,0,4,Lime);w.SlimeMark(0,0,4);
    for(int page=0;page<int(Pages().size());++page){
        const Page& p=Pages()[size_t(page)];
        Illustration(w,page,0,0,0);
        w.Text(p.title,0,0,34,Paper,JapaneseFont);
        for(const char* line:p.lines)if(line&&*line)w.Text(line,0,0,20,Paper,JapaneseFont);
        if(p.key){w.Prompt(p.key,p.keyLabel,0.f,0.f,40.f,24.f);w.PadPrompt(p.pad,p.padLabel,0.f,0.f,40.f,24.f);}
    }
    for(const char* pad:{"xbox_dpad","xbox_lb","xbox_rb"})w.PadPrompt(pad,"",0,0,22,15);
    for(const char* text:{"ページ送り","← →  /  A D  /  クリックでページ送り","準備完了","グラフィックエンジンを準備しています",
        "フィールドを生成しています","読み込んでいます",GameTitle})w.Text(text,0,0,20,Paper,JapaneseFont);
    for(const char* text:{"HOW TO PLAY NOW LOADING READY 0123456789%/<>.","PRESS ANY KEY BUTTON","neoEngine",SplashTagline})w.Text(text,0,0,20,Paper);
}

inline float ContentAlpha(const Engine::LoadingScreen::State& state) {
    // Short loads only show the slime and the mark; the tutorial appears for real waits.
    return state.startup?Smooth(0,.35f,state.elapsed):Smooth(.45f,.8f,state.elapsed);
}

inline void DrawLoadingFrame(const Engine::LoadingScreen::State& state) {
    Canvas ui(Engine::Renderer::GetInstance());
    if(state.warmup)Warmup(ui);
    ui.Fill({-200,-200,1680,1120},Slime);
    ui.Grain(.04f);
    if(state.scene=="Startup"){SplashContent(ui,state,1);return;}
    LoadingContent(ui,state,ContentAlpha(state),!state.warmup);
    LogoMark(ui,1196,646,1);
}

inline void DrawTransition(const Engine::SceneManager::Transition& transition) {
    Canvas ui(Engine::Renderer::GetInstance());
    using Phase=Engine::SceneManager::Transition::Phase;
    const float t=transition.progress;
    if(transition.phase==Phase::Cover){
        SlimeWipe(ui,t,false);
        LogoMark(ui,1196,646,Smooth(.7f,1.f,t));
        return;
    }
    // Reveal: a finished loading screen (or the splash) fades out before the slime drains away.
    const auto& last=Engine::LoadingScreen::Last();
    const bool splash=last.scene=="Startup";
    const bool content=transition.loadingShown&&(splash||ContentAlpha(last)>.5f);
    const float hold=content?.22f:0.f;
    const float drain=std::clamp((t-hold)/(1-hold),0.f,1.f);
    SlimeWipe(ui,drain,true);
    if(content){
        const float fade=1-Smooth(0,hold,t);
        if(splash)SplashContent(ui,last,fade);
        else LoadingContent(ui,last,fade*ContentAlpha(last),false);
    }
    if(!splash)LogoMark(ui,1196,646,1-Smooth(0,.35f,drain));
    LoopBurst(ui,Smooth(0,.9f,drain));
}

// Called once before the engine starts so even the startup load is covered.
inline void InstallPresentation() {
    Engine::LoadingScreen::SetDrawer(DrawLoadingFrame);
}
inline void InstallTransitions(Engine::SceneManager& scenes) {
    scenes.SetTransitionDrawer(DrawTransition);
}
}
}
