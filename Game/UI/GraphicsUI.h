#pragma once
#include "GameUI.h"
namespace Game::UI {
inline constexpr Rect GraphicsButton{1046,38,190,46},GraphicsBack{984,600,226,58};
inline constexpr Rect DlssQualityButton{760,238,222,44},DlssOffButton{986,238,206,44};
inline constexpr Rect RtOnButton{1010,357,92,38},RtOffButton{1106,357,92,38};
inline constexpr Rect RtReflectionOn{1010,415,92,38},RtReflectionOff{1106,415,92,38};
inline constexpr Rect RtIndirectOn{1010,473,92,38},RtIndirectOff{1106,473,92,38};
inline constexpr Rect ExposureDown{1010,531,92,38},ExposureUp{1106,531,92,38};
inline constexpr Rect PresetLow{96,604,170,50},PresetBalanced{278,604,206,50},PresetHigh{496,604,170,50};
inline Rect GraphicsRow(int i){return {72,196+float(i)*58,612,50};}
inline bool UpdateGraphics(Canvas& ui,Engine::Renderer& renderer){
    auto s=renderer.GetGraphicsSettings();float* values[]={&s.bloom,&s.lensFlare,&s.grading,&s.ambientOcclusion,&s.motionBlur,&s.dof};
    const float defaults[]={.16f,.035f,.55f,.5f,.2f,1};const bool quality=s.dlssQuality,shadows=s.rtShadows,reflection=s.rtReflections,indirect=s.rtIndirect;
    for(int i=0;i<6;++i)if(ui.Click(GraphicsRow(i))||Pressed(BYTE(DIK_1+i)))*values[i]=*values[i]>.001f?0:defaults[i];
    if(ui.Click(PresetLow))s={.08f,0,.4f,0,0,0,1.05f};
    if(ui.Click(PresetBalanced))s={};
    if(ui.Click(PresetHigh))s={.25f,.045f,.65f,.7f,.3f,1,1.05f};
    s.dlssQuality=quality;
    s.rtShadows=shadows;s.rtReflections=reflection;s.rtIndirect=indirect;
    if(ui.Click(ExposureDown))s.exposure=std::max(.6f,s.exposure-.1f);
    if(ui.Click(ExposureUp))s.exposure=std::min(1.6f,s.exposure+.1f);
    renderer.ApplyGraphicsSettings(s);
    if(ui.Click(RtOnButton))renderer.SetRtShadows(true);
    if(ui.Click(RtOffButton))renderer.SetRtShadows(false);
    if(ui.Click(RtReflectionOn))renderer.SetRtReflections(true);
    if(ui.Click(RtReflectionOff))renderer.SetRtReflections(false);
    if(ui.Click(RtIndirectOn))renderer.SetRtIndirect(true);
    if(ui.Click(RtIndirectOff))renderer.SetRtIndirect(false);
    if(ui.Click(DlssQualityButton))renderer.SetDlssQuality(true);
    if(ui.Click(DlssOffButton))renderer.SetDlssQuality(false);
    return ui.Click(GraphicsBack)||BackPressed()||PadPressed(XINPUT_GAMEPAD_BACK)||Pressed(DIK_G)||Pressed(DIK_F7);
}
// One half of a segmented control: the active side is a lime slime pill.
inline void Segment(Canvas& ui,Rect b,const char* label,bool active,Engine::Vector4 accent=Lime){
    const bool hover=ui.Hover(b);ui.FocusFrame(b,b.h*.5f);
    if(active)ui.Rounded(b,accent,b.h*.5f);
    else if(hover)ui.Rounded(b,{.14f,.23f,.18f,1},b.h*.5f);
    ui.LabelCenter(label,b.x+b.w*.5f,b.y+(b.h-17)*.5f-1,17,active?Ink:hover?Paper:Muted,1.6f);
}
inline void SegmentTrack(Canvas& ui,Rect a,Rect b){
    ui.Rounded({a.x-4,a.y-4,b.x+b.w-a.x+8,a.h+8},{.05f,.085f,.07f,1},(a.h+8)*.5f);
}
// A toggle switch with a sliding glossy knob.
inline void Switch(Canvas& ui,float x,float y,bool on){
    ui.Rounded({x,y,62,30},on?Lime:Engine::Vector4{.16f,.24f,.2f,1},15);
    const float knob=on?x+47:x+15;
    ui.Dot(knob,y+15,11,on?Ink:Muted);ui.Dot(knob-3,y+11,3,WithAlpha(Paper,on?.35f:.25f));
}
inline void DrawGraphics(Canvas& ui,const Engine::Renderer& renderer){
    ui.Background("GRAPHICS / PAUSED");const auto& s=renderer.GetGraphicsSettings();
    ui.Brush({62,92,340,94},WithAlpha(Lime,.92f),-.025f);
    ui.Text("映像設定",96,116,40,Ink,JapaneseFont);
    if(UsingPad()){float x=ui.PadPrompt("xbox_dpad","選択",424,134,28,18,Muted,JapaneseFont);
        x=ui.PadPrompt("xbox_button_a","切り替え",x+22,134,28,18,Muted,JapaneseFont);ui.PadPrompt("xbox_button_b","戻る",x+22,134,28,18,Muted,JapaneseFont);}
    else ui.Text("クリック、または 1〜6 キーで切り替え",424,138,18,Muted,JapaneseFont);

    float values[]={s.bloom,s.lensFlare,s.grading,s.ambientOcclusion,s.motionBlur,s.dof};
    const char* names[]={"HDR ブルーム","レンズフレア","カラーグレーディング / LUT","接地影 / GTAO","演出時モーションブラー","被写界深度 / DoF"};
    for(int i=0;i<6;++i){
        const auto row=GraphicsRow(i);const bool hover=ui.Hover(row),on=values[i]>.001f;
        ui.FocusFrame(row,16);
        ui.Rounded(row,hover?Engine::Vector4{.12f,.2f,.16f,.98f}:Engine::Vector4{.07f,.115f,.09f,.94f},16);
        ui.Label("0"+std::to_string(i+1),row.x+20,row.y+17,14,WithAlpha(on?Lime:Muted,.9f),2);
        ui.Text(names[i],row.x+62,row.y+12,21,on?Paper:Muted,JapaneseFont);
        Switch(ui,row.x+row.w-84,row.y+10,on);
    }

    const Rect dlss{740,186,470,148};
    ui.Rounded(dlss,{.07f,.115f,.09f,.94f},20);
    ui.Label("NVIDIA DLSS SUPER RESOLUTION",dlss.x+22,dlss.y+20,13,Lime,2.6f);
    SegmentTrack(ui,DlssQualityButton,DlssOffButton);
    Segment(ui,DlssQualityButton,"ON / QUALITY",s.dlssQuality&&renderer.SupportsDlss());Segment(ui,DlssOffButton,"OFF",!s.dlssQuality);
    std::string availability=renderer.DlssActive()?"現在適用中：QUALITY":"現在適用中：OFF";
    if(!renderer.SupportsDlss()){
        const auto reason=renderer.DlssStatus();availability="DLSS初期化失敗（ログを確認）";
        if(reason=="SDK DLLs missing")availability="DLSSライブラリが配置されていません";
        else if(reason.find("signature")!=std::string::npos)availability="DLSSライブラリの署名確認に失敗";
        else if(reason.find("unsupported")!=std::string::npos)availability="選択中のGPU・ドライバーはDLSS非対応";
        else if(reason.find("evaluation failed")!=std::string::npos)availability="DLSS描画に失敗（ログを確認）";
    }
    ui.Text("変更はゲームアプリ再起動後に反映",dlss.x+22,dlss.y+104,16,Gold,JapaneseFont);
    ui.Text(availability,dlss.x+250,dlss.y+105,14,Muted,JapaneseFont);

    auto rtRow=[&](float y,const char* name,Rect on,Rect off,bool enabled,bool available){
        ui.Rounded({740,y,470,52},{.07f,.115f,.09f,.94f},16);
        ui.Text(name,762,y+13,20,available?Paper:Muted,JapaneseFont);
        SegmentTrack(ui,on,off);Segment(ui,on,"ON",enabled&&available,Teal);Segment(ui,off,"OFF",!enabled,Teal);
    };
    rtRow(350,"RT影 / 地形・岩",RtOnButton,RtOffButton,s.rtShadows,renderer.RtShadowsAvailable());
    rtRow(408,"RT反射",RtReflectionOn,RtReflectionOff,s.rtReflections,renderer.RtLightingAvailable());
    rtRow(466,"RT間接光",RtIndirectOn,RtIndirectOff,s.rtIndirect,renderer.RtLightingAvailable());
    if(!renderer.RtLightingAvailable())ui.Text("RT利用不可",900,358,14,Muted,JapaneseFont);
    ui.Rounded({740,524,470,52},{.07f,.115f,.09f,.94f},16);
    ui.Text("露出",762,537,20,Paper,JapaneseFont);
    ui.Text(std::to_string(int(std::round(s.exposure*100)))+"%",830,534,26,Lime);
    SegmentTrack(ui,ExposureDown,ExposureUp);Segment(ui,ExposureDown,"-10%",false);Segment(ui,ExposureUp,"+10%",false);

    ui.Text("設定は自動保存されます",98,562,16,Muted,JapaneseFont);
    ui.Button(PresetLow,"LOW");ui.Button(PresetBalanced,"BALANCED");ui.Button(PresetHigh,"HIGH");
    ui.Button(GraphicsBack,UsingPad()?"BACK":"ESC  BACK",true,"xbox_button_b");
}
}
