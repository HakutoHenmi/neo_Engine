#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../Engine/Renderer.h"
#include "../../Engine/Input.h"
#include "../../Engine/WindowDX.h"
#include <algorithm>
#include <string>
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
inline constexpr const char* Font = "Resources/Fonts/Rajdhani/Rajdhani-SemiBold.ttf";
inline constexpr const char* JapaneseFont = "Resources/Fonts/MPLUSRounded1c/MPLUSRounded1c-ExtraBold.ttf";
inline constexpr const char* GameTitle = "スライム、跡で本気出す。";
inline const Engine::Vector4 Ink{.055f,.105f,.085f,1}, Paper{.91f,.97f,.88f,1},
    Muted{.59f,.73f,.64f,1}, Lime{.72f,.94f,.35f,1}, Gold{1,.76f,.32f,1};
struct Rect { float x,y,w,h; };
inline constexpr Rect Retry{290,520,320,62}, Select{650,520,320,62},
    Resume{440,340,400,62}, Title{440,422,400,62}, Stage{290,360,700,76};

class Canvas {
    Engine::Renderer* r_;
    float s_, ox_, oy_, alpha_;
    bool pointerOverride_=false;
    float pointerX_=0,pointerY_=0;
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
    void Image(const std::string& path, Rect b, Engine::Vector4 color={1,1,1,1}) {
        Engine::Renderer::SpriteDesc d; d.x=ox_+b.x*s_;d.y=oy_+b.y*s_;d.w=b.w*s_;d.h=b.h*s_;d.color=Tint(color);
        r_->DrawSprite(r_->LoadTexture2D(path),d);
    }
    void Fill(Rect b, Engine::Vector4 color) { Image("Resources/Textures/white1x1.png",b,color); }
    void Line(float x,float y,float tx,float ty,float width,Engine::Vector4 color){
        float dx=tx-x,dy=ty-y,length=std::sqrt(dx*dx+dy*dy);if(length<.01f)return;
        Engine::Renderer::SpriteDesc d;d.x=ox_+((x+tx-length)*.5f)*s_;d.y=oy_+((y+ty-width)*.5f)*s_;
        d.w=length*s_;d.h=width*s_;d.rotationRad=std::atan2(dy,dx);d.color=Tint(color);
        r_->DrawSprite(r_->LoadTexture2D("Resources/Textures/white1x1.png"),d);
    }
    void Panel(Rect b, Engine::Vector4 color=Ink, bool primary=false) {
        Engine::Renderer::Sprite9SliceDesc d;
        d.x=ox_+b.x*s_;d.y=oy_+b.y*s_;d.w=b.w*s_;d.h=b.h*s_;
        d.left=d.right=d.top=d.bottom=(std::min)(8.f*s_,(std::min)(d.w,d.h)*.25f);d.color=Tint(color);
        r_->DrawSprite9Slice(r_->LoadTexture2D(Asset(primary?"Grey/Default/button_rectangle_depth_flat.png":"Grey/Default/button_rectangle_flat.png")),d);
    }
    void Text(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        r_->DrawString(label,ox_+x*s_,oy_+y*s_,size/64*s_,Tint(color),font);
    }
    void Center(const std::string& label,float x,float y,float size=24,Engine::Vector4 color=Paper,const char* font=Font) {
        Text(label,x-r_->MeasureTextWidth(label,size/64,font)*.5f,y,size,color,font);
    }
    bool Hover(Rect b) const {
        auto* input=Engine::Input::GetInstance(); if(!input)return false;
        float x,y;input->GetMousePos(x,y);
        if(pointerOverride_){x=pointerX_;y=pointerY_;}
        x=(x-ox_)/s_;y=(y-oy_)/s_;
        return x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;
    }
    bool Click(Rect b) const {
        if(!Hover(b))return false;
        bool clicked=Engine::Input::GetInstance()->IsMouseTrigger(0);
#ifdef USE_IMGUI
        if(ImGui::GetCurrentContext())clicked|=ImGui::IsMouseClicked(ImGuiMouseButton_Left);
#endif
        return clicked;
    }
    void Button(Rect b,const std::string& label,bool primary=false) {
        bool hover=Hover(b);
        Panel(b,primary?(hover?Engine::Vector4{.85f,1,.53f,1}:Lime):
            (hover?Engine::Vector4{.22f,.35f,.27f,1}:Engine::Vector4{.12f,.22f,.17f,1}),primary);
        Center(label,b.x+b.w*.5f,b.y+(b.h-30)*.5f,30,primary?Ink:Paper);
        if(hover) Fill({b.x+16,b.y+b.h-8,b.w-32,2},Lime);
    }
    void Prompt(const char* key,const std::string& label,float x,float y,float size=32,float textSize=22) {
        Image(std::string("Resources/Textures/UI/kenney_input-prompts_1.5/Keyboard & Mouse/Default/")+key+".png",{x,y,size,size});
        Text(label,x+size+8,y+4,textSize,Muted);
    }
    void Bar(Rect b,float ratio,Engine::Vector4 color=Lime) {
        Panel(b,{.2f,.28f,.21f,1});
        float width=(b.w-4)*std::clamp(ratio,0.f,1.f);
        if(width>0) Fill({b.x+2,b.y+2,width,b.h-4},color);
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
        arc(1,{.20f,.29f,.23f,1});arc(ratio,color);
    }
    void Background(const char* section) {
        Fill({-ox_/s_,-oy_/s_,1280+2*ox_/s_,720+2*oy_/s_},{.022f,.045f,.035f,1});
        Fill({48,48,4,30},Lime);Text(GameTitle,68,47,26,Paper,JapaneseFont);Text(section,68,80,18,Muted);
        Fill({48,656,1184,1},{.22f,.32f,.24f,1});Text("VERDANT BASIN  /  FIELD OPERATIONS",48,676,18,Muted);
    }
    void Result(bool won,const std::string& time,const std::string& detail) {
        Background("MISSION REPORT");Panel({250,130,780,475});
        Center(won?"MISSION COMPLETE":"REFORM AND RETURN",640,177,48,won?Lime:Gold);
        Center(won?"The valley is quiet again.":"Your next attempt starts here.",640,244,24,Muted);
        Center(time,640,315,38);Center(detail,640,375,24,Muted);
        Prompt("keyboard_r","RETRY",460,454);Prompt("keyboard_tab","STAGE SELECT",667,454);
        Button(Retry,"RETRY",true);Button(Select,"STAGE SELECT");
    }
    void Pause() {
        Background("EXPEDITION / PAUSED");Panel({360,168,560,416});
        Center("TAKE A BREATHER",640,210,46,Lime);
        Center("Your expedition will wait.",640,275,24,Muted);
        Button(Resume,"RESUME",true);Button(Title,"BACK TO TITLE");
        Prompt("keyboard_escape","RESUME",442,522);Prompt("keyboard_tab","TITLE",682,522);
    }
};
}
