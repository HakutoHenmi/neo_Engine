#pragma once
#include "GameUI.h"

namespace Game::UI {
inline constexpr Rect CreditsButton{1020,666,212,42};
inline constexpr Rect CreditsBack{922,576,240,52}, CreditsPrev{118,576,180,52}, CreditsNext{320,576,180,52};
inline constexpr int CreditsPageCount = 4;

// Shared by the title and GPU layout validation; all coordinates use Canvas space.
inline void DrawCredits(Canvas& ui, int page) {
    ui.Background("CREDITS / SPECIAL THANKS");
    ui.Panel({88,122,1104,430});
    auto heading=[&](const char* text){ui.Text(text,118,143,32,Lime);};
    auto row=[&](int index,const char* title,const char* detail){
        const float y=202.f+index*66.f;
        ui.Text(title,118,y,25);ui.Text(detail,118,y+29,19,Muted);
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
        row(4,"CC0 1.0 Universal","creativecommons.org/publicdomain/zero/1.0/");
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
    if(page>0)ui.Button(CreditsPrev,"PREVIOUS");
    if(page+1<CreditsPageCount)ui.Button(CreditsNext,"NEXT");
    ui.Center(std::to_string(page+1)+" / "+std::to_string(CreditsPageCount),690,589,24,Muted);
    ui.Button(CreditsBack,"BACK / ESC");
}
}
