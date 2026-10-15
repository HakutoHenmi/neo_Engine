#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#ifdef USE_IMGUI
#include "../../externals/imgui/imgui.h"
#endif

// All player-facing screens use the same 1280 x 720 design space.
// Queue this UI in Draw(), before Renderer::EndFrame flushes sprites/text.
namespace Game::UI {
inline bool Pressed(BYTE key) {
    auto* input=Engine::Input::GetInstance();
    if(input && input->Trigger(key))return true;
#ifdef USE_IMGUI
    if(ImGui::GetCurrentContext()) {
        ImGuiKey mapped=key==DIK_TAB?ImGuiKey_Tab:key==DIK_R?ImGuiKey_R:
            key==DIK_RETURN?ImGuiKey_Enter:key==DIK_ESCAPE?ImGuiKey_Escape:ImGuiKey_None;
        if(mapped!=ImGuiKey_None)return ImGui::IsKeyPressed(mapped,false);
    }
#endif
    return false;
}
// ===== Gamepad =====
inline bool UsingPad() {
    auto* input=Engine::Input::GetInstance();
    return input && input->UsingGamepad();
}
inline bool PadPressed(uint32_t button) {
    auto* input=Engine::Input::GetInstance();
    return input && input->PadTrigger(button);
}
// Back / cancel: Escape (or the given key) on keyboard, B on the gamepad.
inline bool BackPressed(BYTE key=DIK_ESCAPE) { return Pressed(key)||PadPressed(XINPUT_GAMEPAD_B); }

inline constexpr const char* Font = "Resources/Fonts/Rajdhani/Rajdhani-SemiBold.ttf";
inline constexpr const char* JapaneseFont = "Resources/Fonts/MPLUSRounded1c/MPLUSRounded1c-ExtraBold.ttf";
inline constexpr const char* GameTitle = "スライム、跡で本気出す。";
// Field-kit palette: mossy night, glossy lime slime, toxic teal, amber warnings, cream ink.
inline const Engine::Vector4 Night{.032f,.052f,.043f,1}, Ink{.045f,.078f,.063f,1}, Moss{.085f,.135f,.105f,1},
    Paper{.94f,.96f,.86f,1}, Muted{.56f,.67f,.58f,1}, Lime{.74f,.95f,.33f,1}, Teal{.3f,.94f,.78f,1},
    Gold{1,.72f,.3f,1}, Coral{1,.43f,.35f,1};
inline Engine::Vector4 WithAlpha(Engine::Vector4 c,float a) { c.w*=a; return c; }
inline Engine::Vector4 Mix(Engine::Vector4 a,Engine::Vector4 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}; }
inline float Seconds() {
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
}
inline float Noise(float i) { float v=std::sin(i*12.9898f+78.233f)*43758.5453f; return v-std::floor(v); }
inline float SeedOf(const std::string& text) { float seed=0; for(char ch:text)seed=seed*1.37f+float(static_cast<unsigned char>(ch)); return std::fmod(seed,97.f); }
namespace Art {
inline constexpr const char* White="Resources/Textures/white1x1.png";
inline constexpr const char* Round="Resources/Textures/UI/ui_round.png";
inline constexpr const char* RoundRing="Resources/Textures/UI/ui_round_ring.png";
inline constexpr const char* Brush="Resources/Textures/UI/ui_brush.png";
inline constexpr const char* Splat="Resources/Textures/UI/ui_splat.png";
inline constexpr const char* Grain="Resources/Textures/UI/ui_grain.png";
inline constexpr const char* Fade="Resources/Textures/UI/ui_fade.png";
inline constexpr const char* Circle="Resources/Textures/UI/slime_circle.png";
inline constexpr const char* Glow="Resources/Textures/UI/slime_glow.png";
}
struct Rect { float x,y,w,h; };
// Shared screen layouts (also used by the scenes' click handling).
inline constexpr Rect Retry{96,548,320,66}, Select{436,556,268,50},
    Resume{96,420,340,66}, Title{96,502,268,50}, Stage{96,486,470,76}, TitleStart{96,470,430,74};

// Gamepad focus navigation shared by every Canvas.
// Rects passed to Click()/Track() during a frame become the next frame's focus targets,
// so each screen gets D-pad / left-stick navigation without its own menu state.
struct Navigator {
    struct Item { Rect rect; bool primary=false; };
    uint64_t frame=~0ull;
    std::vector<Item> current,previous;
    Rect focus{}; bool hasFocus=false,activate=false;
    int heldDirection=-1; float repeat=0;
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now();
    static bool Same(const Rect& a,const Rect& b) {
        return std::abs(a.x-b.x)<.5f&&std::abs(a.y-b.y)<.5f&&std::abs(a.w-b.w)<.5f&&std::abs(a.h-b.h)<.5f;
    }
    static Item* Find(std::vector<Item>& items,const Rect& b) {
        for(auto& item:items)if(Same(item.rect,b))return &item;
        return nullptr;
    }
    void Register(const Rect& b,bool primary=false) {
        Begin();
        if(auto* item=Find(current,b)){item->primary|=primary;return;}
        current.push_back({b,primary});
    }
    void Move(float dx,float dy) {
        const float fx=focus.x+focus.w*.5f,fy=focus.y+focus.h*.5f;
        float best=1e9f;const Item* next=nullptr;
        for(const auto& item:previous){
            if(Same(item.rect,focus))continue;
            float vx=item.rect.x+item.rect.w*.5f-fx,vy=item.rect.y+item.rect.h*.5f-fy;
            float along=vx*dx+vy*dy;if(along<=1)continue;
            float score=along+std::abs(vx*dy-vy*dx)*2;
            if(score<best){best=score;next=&item;}
        }
        if(next)focus=next->rect;
    }
    // Runs once per input frame, before the first Canvas query of that frame.
    void Begin() {
        auto* input=Engine::Input::GetInstance();
        if(!input||input->FrameIndex()==frame)return;
        // Targets from an older frame (e.g. before gameplay) never carry into a newly opened menu.
        const bool consecutive=input->FrameIndex()==frame+1;
        frame=input->FrameIndex();
        previous.swap(current);current.clear();activate=false;
        if(!consecutive){previous.clear();hasFocus=false;}
        auto now=std::chrono::steady_clock::now();
        float dt=std::clamp(std::chrono::duration<float>(now-last).count(),0.f,.1f);last=now;
        if(previous.empty()||!input->UsingGamepad()){heldDirection=-1;return;}
        if(!hasFocus||!Find(previous,focus)){
            const Item* first=&previous.front();
            for(const auto& item:previous)if(item.primary){first=&item;break;}
            focus=first->rect;hasFocus=true;
        }
        using E=Engine::Input;
        const uint32_t up=XINPUT_GAMEPAD_DPAD_UP|E::kPadLsUp,down=XINPUT_GAMEPAD_DPAD_DOWN|E::kPadLsDown,
            left=XINPUT_GAMEPAD_DPAD_LEFT|E::kPadLsLeft,right=XINPUT_GAMEPAD_DPAD_RIGHT|E::kPadLsRight;
        int direction=input->PadDown(up)?0:input->PadDown(down)?1:input->PadDown(left)?2:input->PadDown(right)?3:-1;
        bool step=false;
        if(direction!=heldDirection){heldDirection=direction;repeat=.38f;step=direction>=0;}
        else if(direction>=0&&(repeat-=dt)<=0){repeat=.11f;step=true;}
        if(step){const float dirs[4][2]={{0,-1},{0,1},{-1,0},{1,0}};Move(dirs[direction][0],dirs[direction][1]);}
        activate=input->PadTrigger(XINPUT_GAMEPAD_A);
    }
};
inline Navigator& Nav() { static Navigator nav; return nav; }

class Canvas {
    Engine::Renderer* r_;
    float s_, ox_, oy_, alpha_;
    bool pointerOverride_=false;
    float pointerX_=0,pointerY_=0;
    void Sprite(const char* path,Rect b,Engine::Vector4 color,Engine::Vector4 uv={1,1,0,0},float rotation=0) {
        Engine::Renderer::SpriteDesc d; d.x=ox_+b.x*s_;d.y=oy_+b.y*s_;d.w=b.w*s_;d.h=b.h*s_;
        d.color=Tint(color);d.uvScaleOffset=uv;d.rotationRad=rotation;
        r_->DrawSprite(r_->LoadTexture2D(path),d);
    }
    // 3 x 3 slices: a fixed texture corner mapped to any on-screen corner radius.
    void Sliced(const char* path,Rect b,Engine::Vector4 color,float radius) {
        constexpr float corner=30.f/96.f;
        const float r=(std::max)(.5f,(std::min)({radius,b.w*.5f,b.h*.5f}));
        const float xs[4]={b.x,b.x+r,b.x+b.w-r,b.x+b.w},ys[4]={b.y,b.y+r,b.y+b.h-r,b.y+b.h};
        const float us[4]={0,corner,1-corner,1};
        for(int row=0;row<3;++row)for(int col=0;col<3;++col){
            const float w=xs[col+1]-xs[col],h=ys[row+1]-ys[row];if(w<=.01f||h<=.01f)continue;
            Sprite(path,{xs[col],ys[row],w,h},color,{us[col+1]-us[col],us[row+1]-us[row],us[col],us[row]});
        }
    }
public:
    Canvas(Engine::Renderer* r, float w=Engine::WindowDX::kW, float h=Engine::WindowDX::kH, float alpha=1)
        : r_(r), s_((std::min)(float(Engine::WindowDX::kW)/1280,float(Engine::WindowDX::kH)/720)),
          ox_((Engine::WindowDX::kW-1280*s_)*.5f), oy_((Engine::WindowDX::kH-720*s_)*.5f), alpha_(alpha) {(void)w;(void)h;}
    static std::string Asset(const char* name) { return std::string("Resources/Textures/UI/kenney_ui-pack/PNG/")+name; }
    // EditorUI already maps the Game-panel pointer into the full final texture.
    // Sprite/text positions use that same texture; ImGui scales it exactly once.
    void SetPointer(float x,float y) { pointerOverride_=true;pointerX_=x;pointerY_=y; }
    Engine::Vector4 Tint(Engine::Vector4 c) const { c.w*=alpha_; return c; }
    void MultiplyAlpha(float alpha) { alpha_*=std::clamp(alpha,0.f,1.f); }
    void Image(const std::string& path, Rect b, Engine::Vector4 color={1,1,1,1}) { Sprite(path.c_str(),b,color); }
    void ImageRot(const std::string& path,Rect b,float rotation,Engine::Vector4 color={1,1,1,1}) { Sprite(path.c_str(),b,color,{1,1,0,0},rotation); }
    void Fill(Rect b, Engine::Vector4 color) { Sprite(Art::White,b,color); }
    void Line(float x,float y,float tx,float ty,float width,Engine::Vector4 color){
        float dx=tx-x,dy=ty-y,length=std::sqrt(dx*dx+dy*dy);if(length<.01f)return;
        Sprite(Art::White,{(x+tx-length)*.5f,(y+ty-width)*.5f,length,width},color,{1,1,0,0},std::atan2(dy,dx));
    }

    // ---- Shapes -------------------------------------------------------------------
    void Rounded(Rect b,Engine::Vector4 color,float radius=14) { Sliced(Art::Round,b,color,radius); }
    void RoundedRing(Rect b,Engine::Vector4 color,float radius=14) { Sliced(Art::RoundRing,b,color,radius); }
    void Dot(float x,float y,float r,Engine::Vector4 color) { Sprite(Art::Circle,{x-r,y-r,r*2,r*2},color); }
    void Halo(float x,float y,float r,Engine::Vector4 color) { Sprite(Art::Glow,{x-r,y-r,r*2,r*2},color); }
    // A dry-brush stroke; slightly rotated strokes read as hand-painted.
    void Brush(Rect b,Engine::Vector4 color,float rotation=0,bool flip=false) {
        Sprite(Art::Brush,b,color,flip?Engine::Vector4{-1,1,1,0}:Engine::Vector4{1,1,0,0},rotation);
    }
    void Splat(float x,float y,float r,Engine::Vector4 color,float rotation=0) { Sprite(Art::Splat,{x-r,y-r,r*2,r*2},color,{1,1,0,0},rotation); }
    // Slime drips hanging from an edge; they breathe slowly so screens never feel frozen.
    void Drips(float x,float y,float w,Engine::Vector4 color,float seed=0,float scale=1) {
        const int count=(std::max)(1,int(w/70));const float t=Seconds();
        for(int i=0;i<count;++i){
            const float n=Noise(seed+float(i)*3.1f),dx=x+w*(float(i)+.25f+.5f*n)/float(count);
            const float length=(8+20*Noise(seed+float(i)*7.7f))*scale*(.88f+.12f*std::sin(t*1.6f+float(i)*1.9f+seed));
            const float width=(5+3*n)*scale;
            Dot(dx,y-1,width*1.15f,color);
            const int steps=(std::max)(2,int(length/2.5f));
            for(int k=1;k<=steps;++k){const float f=float(k)/float(steps);
                Dot(dx+std::sin(t*1.3f+seed+f*2)*.6f,y+length*f,width*(.55f-.22f*f),color);}
            Dot(dx,y+length+width*.15f,width*.72f,color);
        }
    }
    // Readable over the 3D scene without a box: a soft dark pool.
    void Shade(Rect b,float strength=.55f) { Halo(b.x+b.w*.5f,b.y+b.h*.5f,(std::max)(b.w,b.h)*.75f,WithAlpha(Night,strength)); }
    void Grain(float amount=.05f) {
        const float w=1280+2*ox_/s_,h=720+2*oy_/s_;
        Sprite(Art::Grain,{-ox_/s_,-oy_/s_,w,h},WithAlpha(Paper,amount),{w*s_/128,h*s_/128,0,0});
    }
    // Small slime brand mark: glossy body, two eyes, a blink every few seconds.
    void SlimeMark(float x,float y,float r,float alpha=1,bool sleepy=false) {
        const float t=Seconds(),wob=std::sin(t*3.2f)*.06f;
        Dot(x,y,r,WithAlpha({.47f,.78f,.25f,1},alpha));
        Sprite(Art::Circle,{x-r*(1+wob),y-r*(1-wob)-r*.08f,r*2*(1+wob),r*2*(1-wob)},WithAlpha({.62f,.9f,.33f,1},alpha));
        Dot(x-r*.38f,y-r*.42f,r*.22f,WithAlpha(Paper,.75f*alpha));
        if(sleepy){
            for(float side:{-1.f,1.f})Line(x+side*r*.33f-r*.13f,y+r*.02f,x+side*r*.33f+r*.13f,y+r*.02f,(std::max)(2.f,r*.06f),WithAlpha(Ink,alpha));
            for(int k=0;k<3;++k){const float f=std::fmod(t*.35f+float(k)/3,1.f);
                Text("z",x+r*.7f+f*r*.5f,y-r*1.1f-f*r*.9f,r*(.25f+.2f*f),WithAlpha(Paper,alpha*(1-f)*.8f));}
            return;
        }
        const float eye=std::fmod(t,3.4f)>3.25f?r*.06f:r*.24f;
        for(float side:{-1.f,1.f})Sprite(Art::Circle,{x+side*r*.33f-r*.1f,y-eye*.5f,r*.2f,eye},WithAlpha(Ink,alpha));
    }

    // ---- Type -----------------------------------------------------------------------
    void Text(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        r_->DrawString(label,ox_+x*s_,oy_+y*s_,size/64*s_,Tint(color),font);
    }
    void Center(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        Text(label,x-r_->MeasureTextWidth(label,size/64,font)*.5f,y,size,color,font);
    }
    // Text with a soft offset shadow for use over the game view.
    void Bold(const std::string& label,float x,float y,float size,Engine::Vector4 color=Paper,const char* font=Font) {
        Text(label,x+size*.04f,y+size*.06f,size,{0,0,0,.55f*color.w},font);Text(label,x,y,size,color,font);
    }
    float Width(const std::string& label,float size=24,const char* font=Font) const {
        return r_->MeasureTextWidth(label,size/64,font);
    }
    // Letter-spaced small caps: the "label" voice of every screen.
    float SpacedWidth(const std::string& text,float size,float spacing) const {
        float width=0;for(char ch:text)width+=Width(std::string(1,ch),size)+spacing;
        return text.empty()?0:width-spacing;
    }
    float Spaced(const std::string& text,float x,float y,float size,float spacing,Engine::Vector4 color) {
        for(char ch:text){const std::string glyph(1,ch);Text(glyph,x,y,size,color);x+=Width(glyph,size)+spacing;}
        return x;
    }
    void Label(const std::string& text,float x,float y,float size=14,Engine::Vector4 color=Muted,float spacing=2.4f) { Spaced(text,x,y,size,spacing,color); }
    void LabelCenter(const std::string& text,float x,float y,float size=14,Engine::Vector4 color=Muted,float spacing=2.4f) {
        Spaced(text,x-SpacedWidth(text,size,spacing)*.5f,y,size,spacing,color);
    }

    // ---- Interaction ------------------------------------------------------------------
    // Gamepad focus (always false while the keyboard / mouse is the active device).
    bool Focused(Rect b) const {
        auto& nav=Nav();nav.Begin();
        return UsingPad()&&nav.hasFocus&&Navigator::Same(nav.focus,b);
    }
    // Ring around the gamepad-focused rect (for focus targets that are not Buttons).
    void FocusFrame(Rect b,float radius=14) {
        if(!Focused(b))return;
        RoundedRing({b.x-5,b.y-5,b.w+10,b.h+10},WithAlpha(Paper,.9f),radius+5);
    }
    // Registers a rect as a focus target without making it clickable (e.g. info cards).
    void Track(Rect b,bool primary=false) const { Nav().Register(b,primary); }
    // Hit test for a point in WindowDX::kW x kH pixels (e.g. a queued click).
    bool HitPixel(Rect b,float x,float y) const {
        x=(x-ox_)/s_;y=(y-oy_)/s_;
        return x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;
    }
    bool Hover(Rect b) const {
        auto* input=Engine::Input::GetInstance(); if(!input)return false;
        if(input->UsingGamepad())return Focused(b);
        float x,y;input->GetMousePos(x,y);
        if(pointerOverride_){x=pointerX_;y=pointerY_;}
        x=(x-ox_)/s_;y=(y-oy_)/s_;
        bool inside=x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;
        // Keep the pad focus where the mouse last was, so switching devices feels continuous.
        if(inside){auto& nav=Nav();nav.focus=b;nav.hasFocus=true;}
        return inside;
    }
    bool Click(Rect b) const {
        Track(b);
        if(UsingPad()){
            if(!Nav().activate||!Focused(b))return false;
            Engine::Input::GetInstance()->LatchPad();
            return true;
        }
        if(!Hover(b))return false;
        bool clicked=Engine::Input::GetInstance()->IsMouseTrigger(0);
#ifdef USE_IMGUI
        if(ImGui::GetCurrentContext())clicked|=ImGui::IsMouseClicked(ImGuiMouseButton_Left);
#endif
        return clicked;
    }
    // Xbox Series glyph from the Kenney input prompts pack.
    // Release copies only files listed in scripts/SlimeResources.json: add new glyphs there too.
    void PadGlyph(const char* pad,float x,float y,float size) {
        Image(std::string("Resources/Textures/UI/kenney_input-prompts_1.5/Xbox Series/Default/")+pad+".png",{x,y,size,size});
    }
    // Glyph followed by a label; returns the x just after the label so prompts can be chained.
    float PadPrompt(const char* pad,const std::string& label,float x,float y,float size=26,float textSize=18,
        Engine::Vector4 color=Muted,const char* font=Font) {
        PadGlyph(pad,x,y,size);
        Text(label,x+size+6,y+(size-textSize)*.5f-1,textSize,color,font);
        return x+size+6+Width(label,textSize,font);
    }
    // Slime buttons: primary ones are glossy lime blobs that drip; secondary ones are
    // dark pills with a teal rim. Focus / hover lifts the button and lights a halo.
    // While the gamepad is active the button shows its shortcut glyph (padGlyph),
    // or the A glyph when it is focused. focusable=false keeps it out of pad navigation.
    void Button(Rect b,const std::string& label,bool primary=false,const char* padGlyph=nullptr,bool focusable=true) {
        if(focusable)Track(b,primary);
        const bool pad=UsingPad(),hover=focusable||!pad?Hover(b):false;
        const float lift=hover?2.5f:0.f,radius=b.h*.5f;
        const Rect r{b.x,b.y-lift,b.w,b.h};
        if(hover)Halo(r.x+r.w*.5f,r.y+r.h*.5f,r.w*.62f,WithAlpha(primary?Lime:Teal,.2f));
        if(primary){
            Drips(r.x+radius,r.y+r.h-4,r.w-radius*2,hover?Mix(Lime,Paper,.25f):Lime,SeedOf(label),.9f);
            Rounded(r,hover?Mix(Lime,Paper,.25f):Lime,radius);
            Rounded({r.x+radius*.9f,r.y+5,r.w-radius*1.8f,r.h*.16f},WithAlpha(Paper,.2f),r.h*.08f);
        }else{
            Rounded(r,hover?Engine::Vector4{.13f,.22f,.17f,.96f}:Engine::Vector4{.07f,.12f,.095f,.92f},radius);
            RoundedRing(r,hover?WithAlpha(Teal,.85f):WithAlpha(Paper,.16f),radius);
        }
        if(hover&&pad)RoundedRing({r.x-6,r.y-6,r.w+12,r.h+12},WithAlpha(Paper,.9f),radius+6);
        const float size=(std::min)(26.f,b.h*.42f),spacing=size*.09f;
        const char* glyph=pad?(padGlyph?padGlyph:hover?"xbox_button_a":nullptr):nullptr;
        const float glyphSize=glyph?(std::min)(b.h-16,34.f):0.f,gap=glyph?10.f:0.f;
        const float total=glyphSize+gap+SpacedWidth(label,size,spacing);
        float x=r.x+(r.w-total)*.5f;
        if(glyph){PadGlyph(glyph,x,r.y+(r.h-glyphSize)*.5f,glyphSize);x+=glyphSize+gap;}
        Spaced(label,x,r.y+(r.h-size)*.5f-1,size,spacing,primary?Ink:Paper);
    }
    void Prompt(const char* key,const std::string& label,float x,float y,float size=32,float textSize=22) {
        Image(std::string("Resources/Textures/UI/kenney_input-prompts_1.5/Keyboard & Mouse/Default/")+key+".png",{x,y,size,size});
        Text(label,x+size+8,y+(size-textSize)*.5f-1,textSize,Muted);
    }
    // Shows the Xbox glyph / label while the gamepad is active. A null pad glyph hides the prompt.
    void Prompt(const char* key,const std::string& label,const char* pad,const std::string& padLabel,float x,float y,float size=32,float textSize=22) {
        if(!UsingPad()){Prompt(key,label,x,y,size,textSize);return;}
        if(!pad)return;
        PadPrompt(pad,padLabel,x,y,size,textSize);
    }
    // Panels are soft rounded slabs; primary ones carry a glossy top edge.
    void Panel(Rect b, Engine::Vector4 color=Moss, bool primary=false) {
        Rounded(b,color,(std::min)(16.f,(std::min)(b.w,b.h)*.3f));
        if(primary)Rounded({b.x+8,b.y+4,b.w-16,(std::min)(10.f,b.h*.2f)},WithAlpha(Paper,.12f),5);
    }
    // A slime tube gauge.
    void Bar(Rect b,float ratio,Engine::Vector4 color=Lime) {
        Rounded(b,{.06f,.1f,.08f,.92f},b.h*.5f);
        const float width=b.w*std::clamp(ratio,0.f,1.f);
        if(width>1){Rounded({b.x,b.y,(std::max)(width,b.h),b.h},color,b.h*.5f);
            Rounded({b.x+b.h*.3f,b.y+b.h*.18f,(std::max)(0.f,width-b.h*.6f),b.h*.22f},WithAlpha(Paper,.35f),b.h*.11f);}
    }
    void Ring(float x,float y,float radius,float ratio,Engine::Vector4 color=Lime,float width=5) {
        constexpr int segments=72;
        auto arc=[&](float amount,Engine::Vector4 tint){
            float sweep=6.2831853f*std::clamp(amount,0.f,1.f);
            for(int i=0;i<segments;++i){float from=6.2831853f*float(i)/segments;if(from>=sweep)break;
                float to=(std::min)(6.2831853f*float(i+1)/segments,sweep);
                Line(x+std::sin(from)*radius,y-std::cos(from)*radius,
                    x+std::sin(to)*radius,y-std::cos(to)*radius,width,tint);}
        };
        arc(1,{.12f,.19f,.15f,.9f});arc(ratio,color);
    }

    // ---- Screens -------------------------------------------------------------------------
    // Mossy night with two soft light pools, a faint painted streak and film grain.
    void Background(const char* section) {
        const float t=Seconds();
        Fill({-ox_/s_,-oy_/s_,1280+2*ox_/s_,720+2*oy_/s_},Night);
        Halo(150+20*std::sin(t*.21f),90,760,WithAlpha(Lime,.075f));
        Halo(1190,720+20*std::cos(t*.17f),640,WithAlpha(Teal,.06f));
        Brush({-120,610,980,150},WithAlpha(Moss,.55f),-.05f);
        Grain(.045f);
        SlimeMark(64,64,14);
        Text(GameTitle,90,46,22,Paper,JapaneseFont);
        Label(section,92,76,12,WithAlpha(Lime,.85f),2.8f);
        const std::string footer="VERDANT BASIN  /  FIELD KIT";
        Label(footer,1232-SpacedWidth(footer,11,2.4f),690,11,WithAlpha(Muted,.6f));
    }
    void Result(bool won,const std::string& time,const std::string& detail) {
        Background("MISSION REPORT");
        const auto accent=won?Lime:Gold;const float t=Seconds();
        Splat(1010,330,250,WithAlpha(accent,.06f),.4f);
        SlimeMark(1010,330,92+3*std::sin(t*2));
        Brush({52,150,820,140},WithAlpha(accent,.92f),-.025f);
        Text(won?"MISSION COMPLETE":"REFORM & RETURN",96,184,70,Ink);
        Text(won?"谷に、静けさが戻った。":"形を整えて、もう一度。",100,300,26,Paper,JapaneseFont);
        Label("CLEAR TIME",102,358,13,WithAlpha(accent,.9f));
        Text(time,98,374,58,Paper);
        Text(detail,102,446,20,Muted);
        Button(Retry,"RETRY",true,"xbox_button_x");Button(Select,"STAGE SELECT",false,"xbox_button_y");
        Prompt("keyboard_r","RETRY","xbox_button_x","RETRY",102,632,26,18);
        Prompt("keyboard_tab","STAGE SELECT","xbox_button_y","STAGE SELECT",250,632,26,18);
    }
    void Pause() {
        Background("EXPEDITION / PAUSED");
        Brush({60,226,560,118},WithAlpha(Lime,.9f),-.03f);
        Text("TAKE A BREATHER",96,250,58,Ink);
        Text("探索はここで待っている。",100,360,24,Paper,JapaneseFont);
        Splat(960,380,240,WithAlpha(Lime,.05f),1.1f);
        Halo(960,500,170,WithAlpha(Night,.6f));
        SlimeMark(960,400,100,1,true);
        Button(Resume,"RESUME",true,"xbox_button_b");Button(Title,"BACK TO TITLE");
        Prompt("keyboard_escape","RESUME","xbox_button_b","RESUME",102,592,26,18);
        Prompt("keyboard_tab","TITLE","xbox_button_a","SELECT",250,592,26,18);
    }
};
}
