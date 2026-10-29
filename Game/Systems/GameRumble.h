// GameRumble.h
// コントローラーの振動。短い振動を重ねて受け付け、毎フレーム一番強いものをパッドへ送る。
#pragma once
#include "../../Engine/Input.h"
#include <algorithm>
#include <array>

namespace Game::Rumble {
// 1 回分の振動 (左の重いモーター・右の軽いモーター・経過時間・長さ)
struct Pulse {float low=0,high=0,age=0,duration=0;};
inline std::array<Pulse,8>& Pulses(){static std::array<Pulse,8> pulses{};return pulses;}
// 振動を足す。空きが無ければ一番弱まったものと入れ替える
inline void Play(float low,float high,float seconds){
    if(seconds<=0||(low<=0&&high<=0))return;
    auto& pulses=Pulses();Pulse* slot=&pulses[0];float weakest=2;
    for(auto& pulse:pulses){float left=pulse.duration>0?1-pulse.age/pulse.duration:0;if(left<weakest){weakest=left;slot=&pulse;}}
    *slot={std::clamp(low,0.f,1.f),std::clamp(high,0.f,1.f),0,seconds};
}
// 時間を進めてパッドへ送る。enabled が false (ポーズ中など) の間は止める
inline void Update(float dt,bool enabled){
    float low=0,high=0;
    for(auto& pulse:Pulses()){if(pulse.duration<=0)continue;pulse.age+=dt;
        if(pulse.age>=pulse.duration){pulse={};continue;}
        // 最初は強く、終わりへ向かって弱める
        float fade=1-pulse.age/pulse.duration;fade*=fade;
        low=std::max(low,pulse.low*fade);high=std::max(high,pulse.high*fade);}
    if(!enabled){low=high=0;}
    if(auto* input=Engine::Input::GetInstance())input->SetPadVibration(low,high);
}
// すぐに止める (シーンを抜けるときなど)
inline void Stop(){Pulses().fill({});if(auto* input=Engine::Input::GetInstance())input->SetPadVibration(0,0);}
}
