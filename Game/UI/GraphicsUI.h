#pragma once
#include "GameUI.h"
namespace Game::UI {
inline constexpr Rect GraphicsButton{1040,34,188,46},GraphicsBack{820,617,350,54};
inline constexpr Rect DlssQualityButton{780,211,190,46},DlssOffButton{988,211,190,46};
inline constexpr Rect RtOnButton{988,341,88,38},RtOffButton{1090,341,88,38};
inline constexpr Rect RtReflectionOn{988,409,88,38},RtReflectionOff{1090,409,88,38};
inline constexpr Rect RtIndirectOn{988,477,88,38},RtIndirectOff{1090,477,88,38};
inline Rect GraphicsRow(int i){return {70,166+float(i)*60,660,50};}
inline bool UpdateGraphics(Canvas& ui,Engine::Renderer& renderer){
    auto s=renderer.GetGraphicsSettings();float* values[]={&s.bloom,&s.lensFlare,&s.grading,&s.ambientOcclusion,&s.motionBlur,&s.dof};
    const float defaults[]={.16f,.035f,.55f,.5f,.2f,1};const bool quality=s.dlssQuality,shadows=s.rtShadows,reflection=s.rtReflections,indirect=s.rtIndirect;
    for(int i=0;i<6;++i)if(ui.Click(GraphicsRow(i))||Pressed(BYTE(DIK_1+i)))*values[i]=*values[i]>.001f?0:defaults[i];
    if(ui.Click({110,617,200,54}))s={.08f,0,.4f,0,0,0,1.05f};
    if(ui.Click({328,617,200,54}))s={};
    if(ui.Click({546,617,240,54}))s={.25f,.045f,.65f,.7f,.3f,1,1.05f};
    s.dlssQuality=quality;
    s.rtShadows=shadows;s.rtReflections=reflection;s.rtIndirect=indirect;
    if(ui.Click({990,551,92,42}))s.exposure=std::max(.6f,s.exposure-.1f);
    if(ui.Click({1094,551,92,42}))s.exposure=std::min(1.6f,s.exposure+.1f);
    renderer.ApplyGraphicsSettings(s);
    if(ui.Click(RtOnButton))renderer.SetRtShadows(true);
    if(ui.Click(RtOffButton))renderer.SetRtShadows(false);
    if(ui.Click(RtReflectionOn))renderer.SetRtReflections(true);
    if(ui.Click(RtReflectionOff))renderer.SetRtReflections(false);
    if(ui.Click(RtIndirectOn))renderer.SetRtIndirect(true);
    if(ui.Click(RtIndirectOff))renderer.SetRtIndirect(false);
    if(ui.Click(DlssQualityButton))renderer.SetDlssQuality(true);
    if(ui.Click(DlssOffButton))renderer.SetDlssQuality(false);
    return ui.Click(GraphicsBack)||Pressed(DIK_ESCAPE)||Pressed(DIK_G)||Pressed(DIK_F7);
}
inline void DrawGraphics(Canvas& ui,const Engine::Renderer& renderer){
    ui.Background("GRAPHICS / PAUSED");const auto& s=renderer.GetGraphicsSettings();
    float values[]={s.bloom,s.lensFlare,s.grading,s.ambientOcclusion,s.motionBlur,s.dof};
    const char* names[]={"HDR ブルーム","レンズフレア","カラーグレーディング / LUT","接地影 / GTAO","演出時モーションブラー","被写界深度 / DoF"};
    ui.Text("クリック、または 1〜6 キーで切り替え",70,119,23,Muted,JapaneseFont);
    for(int i=0;i<6;++i){auto row=GraphicsRow(i);ui.Panel(row,ui.Hover(row)?Engine::Vector4{.18f,.30f,.22f,1}:Ink);
        ui.Text(std::to_string(i+1)+"  "+names[i],row.x+18,row.y+11,23,Paper,JapaneseFont);
        ui.Text(values[i]>.001f?"ON":"OFF",row.x+568,row.y+10,27,values[i]>.001f?Lime:Muted);}
    ui.Panel({760,154,450,166});ui.Text("NVIDIA DLSS Super Resolution",780,169,27,Lime);
    ui.Button(DlssQualityButton,"ON / QUALITY",s.dlssQuality&&renderer.SupportsDlss());ui.Button(DlssOffButton,"OFF",!s.dlssQuality);
    ui.Text("変更はゲームアプリ再起動後に反映",780,269,21,Gold,JapaneseFont);
    std::string availability=renderer.DlssActive()?"現在適用中：QUALITY":"現在適用中：OFF";
    if(!renderer.SupportsDlss()){
        const auto reason=renderer.DlssStatus();availability="DLSS初期化失敗（ログを確認）";
        if(reason=="SDK DLLs missing")availability="DLSSライブラリが配置されていません";
        else if(reason.find("signature")!=std::string::npos)availability="DLSSライブラリの署名確認に失敗";
        else if(reason.find("unsupported")!=std::string::npos)availability="選択中のGPU・ドライバーはDLSS非対応";
        else if(reason.find("evaluation failed")!=std::string::npos)availability="DLSS描画に失敗（ログを確認）";
    }
    ui.Text(availability,780,296,18,Muted,JapaneseFont);
    ui.Panel({760,329,450,62});ui.Text("RT影 / 地形・岩",780,351,20,Paper,JapaneseFont);
    ui.Button(RtOnButton,"ON",s.rtShadows&&renderer.RtShadowsAvailable());ui.Button(RtOffButton,"OFF",!s.rtShadows);
    ui.Panel({760,397,450,62});ui.Text("RT反射",780,419,20,Paper,JapaneseFont);
    ui.Button(RtReflectionOn,"ON",s.rtReflections&&renderer.RtLightingAvailable());ui.Button(RtReflectionOff,"OFF",!s.rtReflections);
    ui.Panel({760,465,450,62});ui.Text("RT間接光",780,487,20,Paper,JapaneseFont);
    ui.Button(RtIndirectOn,"ON",s.rtIndirect&&renderer.RtLightingAvailable());ui.Button(RtIndirectOff,"OFF",!s.rtIndirect);
    if(!renderer.RtLightingAvailable())ui.Text("RT利用不可",780,530,16,Muted,JapaneseFont);
    ui.Panel({760,539,450,62});ui.Text("露出  "+std::to_string(int(s.exposure*100))+"%",780,561,23,Paper,JapaneseFont);
    ui.Button({990,551,92,42},"-10%");ui.Button({1094,551,92,42},"+10%");
    ui.Text("設定は自動保存されます",70,559,22,Muted,JapaneseFont);
    ui.Button({110,617,200,54},"LOW");ui.Button({328,617,200,54},"BALANCED");ui.Button({546,617,240,54},"HIGH");ui.Button(GraphicsBack,"ESC: BACK",true);
}
}
