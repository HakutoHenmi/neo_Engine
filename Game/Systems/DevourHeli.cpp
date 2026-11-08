// DevourHeli.cpp
// 吸収モード: 高い所 (机・ベッド・本棚・おもちゃ箱の上) から落ちると、体がヘリコプターの形になって
// 回転翼を回しながらゆっくり降りる (ChronoSystem の一部)。落ちている間もスティックで行き先を選べる。
// 形と規則は Game/Chrono/DevourGimmickRules.h (GimmickInsideHeli・GimmickHeliStarts)。
#include "ChronoSystemInternal.h"

namespace Game {
// 落ち始めたらヘリコプターになり、降りる速さを抑える。着地・他の動き (飛びかかり・跳ぶ・ギミック・変形) の間はやめる
void ChronoSystem::TickDevourHeli(Player& p,float dt,V wish,bool blocked){
    auto& h=devourHeli_;auto& body=life_.bodies[0];const float scale=LifeScale(body.mass);
    if(blocked||finished_){h.active=false;return;}
    if(!h.active){
        if(!Chrono::GimmickHeliStarts(body.at.y-1.25f-InkGround(body.at),scale,p.velocity.y,p.grounded))return;
        h={};h.active=true;PlayDevourSound(devourSounds_.bounce,.35f,1.6f);}
    // 着地したら元の体へ戻る (体が少しつぶれて弾む)
    if(p.grounded&&h.age>.1f){h.active=false;body.springStretch=.3f;return;}
    h.age+=dt;h.previous=h.pose;
    // 回転翼は回り始めに少しずつ速くなる。進む向きへ前に傾く
    h.pose.spin+=Chrono::GimmickHeliSpin*std::min(1.f,h.age/.4f)*dt;
    const V flat{p.velocity.x,0,p.velocity.z};const float lean=.28f*std::clamp(Length(flat)/30.f,0.f,1.f)*(Length(wish)>.1f?1.f:.4f);
    h.pose.pitch+=(lean-h.pose.pitch)*(1-std::exp(-6*dt));
    // 降りる速さの上限 (大きい体は少し速く降りる)。回転翼に合わせてわずかに上下する
    const float fall=Chrono::GimmickHeliFall*std::sqrt(scale)*(1+.12f*std::sin(h.age*9));
    if(p.velocity.y<-fall)p.velocity.y+=(-fall-p.velocity.y)*(1-std::exp(-9*dt));
}
}
