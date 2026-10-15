#pragma once
#include "GameUI.h"

namespace Game::UI {
inline constexpr Rect CreditsButton{96,580,220,50};
inline constexpr Rect CreditsBack{1012,600,200,50}, CreditsPrev{96,600,190,50}, CreditsNext{300,600,190,50};
inline constexpr int CreditsPageCount = 4;

// Shared by the title and GPU layout validation; all coordinates use Canvas space.
inline void DrawCredits(Canvas& ui, int page) {
    ui.Background("CREDITS / SPECIAL THANKS");
    // Page number as a big faded numeral, heading painted over a brush stroke.
    ui.Text("0"+std::to_string(page+1),1040,92,200,{.1f,.16f,.125f,1});
    auto heading=[&](const char* text){
        const float w=ui.Width(text,34);
        ui.Brush({66,114,w+100,82},WithAlpha(Lime,.92f),-.02f);
        ui.Text(text,96,136,34,Ink);
    };
    auto row=[&](int index,const char* title,const char* detail){
        const float y=226.f+index*68.f;
        ui.Dot(104,y+14,5,index%2?Teal:Lime);
        ui.Text(title,124,y,25);ui.Text(detail,126,y+30,18,Muted);
    };
    if(page==0){
        heading("MUSIC / Kevin MacLeod (incompetech.com)");
        row(0,"TITLE  /  Call to Adventure","Kevin MacLeod - CC BY 4.0");
        row(1,"SELECT & TRAINING  /  Carefree","Kevin MacLeod - CC BY 4.0");
        row(2,"BATTLE  /  Volatile Reaction","Kevin MacLeod - CC BY 4.0");
        row(3,"VICTORY  /  Fanfare for Space","Kevin MacLeod - CC BY 4.0");
        row(4,"DEFEAT  /  Evening Fall (Harp)","Kevin MacLeod - CC BY 4.0 / creativecommons.org/licenses/by/4.0/");
    }else if(page==1){
        heading("ART & USER INTERFACE");
        row(0,"UI Pack (2.0)","Kenney / www.kenney.nl / CC0 1.0");
        row(1,"Input Prompts (1.5A)","Kenney / www.kenney.nl / CC0 1.0");
        row(2,"Ultimate Platformer Pack","Quaternius / quaternius.com / CC0 1.0");
        row(3,"Slime effects & procedural sound effects","Generated for this game / GenerateSlimeMeshes & GenerateSlimeAudio");
        row(4,"Photogrammetry scenery / Poly Haven","CC0 / polyhaven.com / sources: Resources/Credits/MeadowRefresh.md");
    }else if(page==2){
        heading("FONTS / SIL OPEN FONT LICENSE 1.1");
        row(0,"Rajdhani","Copyright (c) 2014 Indian Type Foundry");
        row(1,"M PLUS Rounded 1c","Copyright 2016 The Rounded M+ Project Authors");
        row(2,"SIL Open Font License 1.1","openfontlicense.org");
        row(3,"Font licenses","OFL.txt is included with each font in Resources/Fonts.");
    }else{
        heading("SOFTWARE / OPEN SOURCE");
        row(0,"Dear ImGui / Omar Cornut","Copyright (c) 2014-2023 / MIT License");
        row(1,"DirectXTex / Microsoft Corporation","MIT License");
        row(2,"JSON for Modern C++ / Niels Lohmann","Copyright (c) 2013-2022 / MIT License");
        row(3,"Assimp / assimp team  -  stb_truetype / Sean Barrett","Assimp: BSD license / stb_truetype: MIT or public domain");
        row(4,"EnTT  /  ufbx  /  cpp-httplib","Thanks to the projects and their contributors. See Resources/Credits.");
    }
    if(page>0)ui.Button(CreditsPrev,"PREVIOUS",false,"xbox_lb");
    if(page+1<CreditsPageCount)ui.Button(CreditsNext,"NEXT",false,"xbox_rb");
    for(int i=0;i<CreditsPageCount;++i)ui.Dot(540+float(i)*22,625,i==page?6.f:4.f,i==page?Lime:WithAlpha(Muted,.4f));
    ui.Button(CreditsBack,UsingPad()?"BACK":"BACK / ESC",false,"xbox_button_b");
}
}
