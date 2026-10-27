// SlimeWeaponRules.h
// スライムの武器の変形の規則 (変形・戻る・向き・タイヤ・カメラの距離・攻撃の時間と振り・当たり・ダメージ)。
#pragma once
#include "SlimeLifeRules.h"

namespace Game::Chrono {
// 武器の形か / 攻撃を止める (形ができているかは残す)
inline bool LifeWeaponForm(LifeAction form){return LifeRequiredMass(form)>0;}
inline void LifeStopWeaponAttack(LifeBody& body){bool ready=body.weapon.ready;body.weapon={};body.weapon.ready=ready;}
inline void LifeRetargetWeapon(LifeBody& body){
    // 分裂の後に既にできた消火栓の大きさを変えても、GPU の新しい粒子の持ち主が
    // そろうまで、押している放水を止めてはいけない
    // 形ができた消火栓の放水中か
    bool flowing=body.action==LifeAction::Hydrant&&body.weapon.ready;
    body.weapon={};body.weapon.ready=flowing;if(!flowing)body.actionAge=0;
}
// スライムに戻る (取り込み中なら早く戻る)
inline void LifeReturnToSlime(LifeBody& body){
    // 取り込みからは早く、それ以外は 0.4 秒で戻る
    body.restoreDuration=body.action==LifeAction::Engulf?.12f:.4f;
    body.action=LifeAction::Restore;body.actionAge=0;body.target=0xffffffffu;body.grabs.clear();body.weapon={};
}
// 武器に変形する (大きさが足りなければ false)
inline bool LifeEnterWeapon(LifeBody& body,LifeAction form,Vec facing){
    // 大きさが足りないか、倒れているか、体力が無ければ変形できない
    if(!LifeCanMorph(body,form)||body.down||body.health<=0)return false;
    body.action=form;body.actionAge=0;body.forward=body.previousForward=facing;body.committed=false;body.weapon={};return true;
}
// 武器の向きを目標の向きへ回す (回る速さに上限がある)
inline Vec LifeWeaponFacing(Vec facing,Vec desired,float dt){
    // 今のヨーと目標の角度の差
    float yaw=std::atan2(facing.x,facing.z),delta=std::remainder(std::atan2(desired.x,desired.z)-yaw,6.2831853f);
    // 1 秒に 6 ラジアンまでで回す
    yaw+=std::clamp(delta*(1-std::exp(-14*dt)),-6*dt,6*dt);
    return {std::sin(yaw),0,std::cos(yaw)};
}
// カメラの向きを向く形か / 体の向きを更新する (タイヤはゆっくり曲がる)
inline bool LifeFacesCamera(LifeAction form){return form==LifeAction::Hammer||form==LifeAction::Plane||form==LifeAction::Hydrant||form==LifeAction::Signal;}
// カメラを向く形ならカメラの向きへ
inline void LifeUpdateFacing(LifeBody& body,Vec movement,Vec cameraForward,float dt){
    if(LifeFacesCamera(body.action))body.forward=LifeWeaponFacing(body.forward,cameraForward,dt);
    else if(Length(movement)>.05f){
        // 液体なら移動の向きをそのまま向く
        if(!LifeWeaponForm(body.action))body.forward=Unit(movement);
        else if(body.action==LifeAction::Tire){
            // タイヤは大きいほどゆっくり曲がる
            float yaw=std::atan2(body.forward.x,body.forward.z),goal=std::atan2(movement.x,movement.z);
            float rate=2.2f/(1+.12f*LifeScale(body.mass));
            yaw+=std::clamp(std::remainder(goal-yaw,6.2831853f),-rate*dt,rate*dt);body.forward={std::sin(yaw),0,std::cos(yaw)};
        // それ以外の武器は攻撃中でなければ移動の向きへ
        }else if(!body.weapon.active)body.forward=LifeWeaponFacing(body.forward,Unit(movement),dt);
    }
}
// タイヤの大きさと、点がタイヤの形の中にあるか
inline constexpr float LifeTireMajor=2.1f,LifeTireTube=.77f,LifeTireRadius=LifeTireMajor+LifeTireTube;
inline bool LifeInsideTire(Vec point){
    // タイヤの断面の中か
    float radial=std::sqrt((point.y-LifeTireRadius)*(point.y-LifeTireRadius)+point.z*point.z)-LifeTireMajor;
    return point.x*point.x+radial*radial<=LifeTireTube*LifeTireTube;
}
// タイヤを転がした角度の姿勢
inline Vec LifeTirePose(Vec point,float angle){
    // タイヤの中心のまわりに回す
    float y=point.y-LifeTireRadius,c=std::cos(angle),s=std::sin(angle);
    return {point.x,LifeTireRadius+y*c-point.z*s,y*s+point.z*c};
}
// タイヤの移動の速さ (速く、ゆっくり加速し、空中では曲がりにくい)
inline Vec LifeTireMoveVelocity(const LifeBody& body,Vec velocity,Vec wish,const SlimeRecipe& recipe,bool grounded,float dt){
    // 入力の量と最高速
    float amount=std::min(1.f,Length(wish));
    float top=recipe.Speed()*(2.1f+std::min(.5f,.08f*LifeScale(body.mass)));
    // 加速の速さ (空中は弱い)
    float rate=amount>.05f?2.4f:1.7f;rate*=grounded?1.f:.35f;
    // 前方向の目標の速度へ近づける
    float blend=1-std::exp(-rate*dt);Vec goal=body.forward*(top*amount);
    velocity.x+=(goal.x-velocity.x)*blend;velocity.z+=(goal.z-velocity.z)*blend;return velocity;
}
// タイヤの溜めと突進: 溜めきる時間・突進できる最小の溜め・溜めたときの最高の回転の速さ
inline constexpr float LifeTireChargeTime=1.1f,LifeTireMinCharge=.15f,LifeTireSpinMax=42;
// 突進の長さと速さ (溜めが強いほど長く速い)
inline float LifeTireDashDuration(float power){return .4f+.6f*power;}
inline float LifeTireDashSpeed(const LifeBody& body,const SlimeRecipe& recipe,float power){
    return recipe.Speed()*(2.1f+std::min(.5f,.08f*LifeScale(body.mass)))*(1.7f+1.8f*power);
}
// 突進が敵に与えるダメージ / バリケードや門に与えるダメージ
inline float LifeTireDashDamage(const LifeBody& body,float power){return (22+48*power)*LifeScale(body.mass);}
inline float LifeTireDashGateDamage(float power){return 45+75*power;}
// タイヤの溜めを進める。押している間はその場で回転を溜め、離したら溜めの分だけ突進を始める (始めたら true)
inline bool LifeTickTireCharge(LifeBody& body,bool held,float dt){
    auto& w=body.weapon;w.dash=std::max(0.f,w.dash-dt);
    if(body.action!=LifeAction::Tire||!w.ready){w.charging=false;w.charge=0;return false;}
    // 突進の最中は溜め直せない
    if(held&&w.dash<=0){w.charging=true;w.charge=std::min(1.f,w.charge+dt/LifeTireChargeTime);return false;}
    if(!held&&w.charging){w.charging=false;float power=w.charge;w.charge=0;
        if(power>=LifeTireMinCharge){w.dashPower=power;w.dash=LifeTireDashDuration(power);return true;}}
    return false;
}
// 溜めている間は止まり、突進中は前へ一気に進む (向きは少しだけ曲げられる)。それ以外は普通に転がる
inline Vec LifeTireChargeVelocity(LifeBody& body,Vec velocity,Vec wish,const SlimeRecipe& recipe,bool grounded,float dt){
    const auto& w=body.weapon;
    if(w.charging){float brake=1-std::exp(-9*dt);velocity.x-=velocity.x*brake;velocity.z-=velocity.z*brake;return velocity;}
    if(w.dash>0){
        if(Length(wish)>.05f){float yaw=std::atan2(body.forward.x,body.forward.z),delta=std::remainder(std::atan2(wish.x,wish.z)-yaw,6.2831853f);
            yaw+=std::clamp(delta,-1.6f*dt,1.6f*dt);body.forward={std::sin(yaw),0,std::cos(yaw)};}
        float speed=LifeTireDashSpeed(body,recipe,w.dashPower)*std::min(1.f,.35f+w.dash/.25f);
        return {body.forward.x*speed,velocity.y,body.forward.z*speed};
    }
    return LifeTireMoveVelocity(body,velocity,wish,recipe,grounded,dt);
}
// その場の回転の速さ: 溜めている間は溜めに合わせて上がり、離すと落ちる
inline float LifeTireSpinRate(const LifeBody& body,float dt){
    float goal=body.weapon.charging?LifeTireSpinMax*(.25f+.75f*body.weapon.charge):0;
    return body.tireSpin+(goal-body.tireSpin)*(1-std::exp(-(body.weapon.charging?6.f:3.f)*dt));
}
// カメラが映す対象と、それが画面に収まるカメラの距離
struct LifeCameraSubject {Vec center{};float radius=1;};
inline float LifeCameraDistance(float radius,float fovY,float aspect){
    // 対象全体を、画面の短い方の 62% の中に収める
    // 画角と比率から、対象が収まる距離
    float tangent=std::tan(fovY*.5f)*std::min(1.f,std::max(.1f,aspect));
    float halfAngle=std::atan(tangent*.62f);
    return std::max(17.f,(radius+1.5f)/std::sin(halfAngle));
}
// 武器ごとの攻撃の長さ
inline float LifeWeaponDuration(LifeAction form){
    return form==LifeAction::Hammer?.58f:form==LifeAction::Sword?.64f:.95f;
}
// 変形の完成と攻撃の時間は別。先行入力は回復の時間の間だけ残る。
// ハンマーは押し続ければ、押し直さなくても繰り返す
inline bool LifeTickWeapon(SlimeLife& life,LifeBody& body,float dt,bool pressed,bool held,bool formed){
    if(!LifeWeaponForm(body.action))return false;
    // タイヤと消火栓は攻撃の時間が無い (形ができたかだけ記録する)
    if(body.action==LifeAction::Tire||body.action==LifeAction::Hydrant||body.action==LifeAction::Signal){body.weapon.ready|=formed;return false;}
    auto& attack=body.weapon;attack.ready|=formed;
    // 先行入力は 0.24 秒残る
    attack.buffer=pressed?.24f:std::max(0.f,attack.buffer-dt);
    attack.previousAge=attack.age;
    // 攻撃中なら時間を進め、終わったら止める
    if(attack.active){
        attack.age+=dt;
        if(attack.age>=LifeWeaponDuration(body.action))attack.active=false;
    }
    // 攻撃していなくて、形ができていて、先行入力があるかハンマーを押し続けていれば攻撃を始める
    if(!attack.active&&attack.ready&&(attack.buffer>0||(body.action==LifeAction::Hammer&&held))){
        attack.active=true;attack.age=attack.previousAge=0;attack.buffer=0;
        body.attackSerial=++life.nextAttack;return true;
    }
    return false;
}
// 武器の振り (構え → 振り抜き → 戻り)
inline float LifeWeaponSwing(float age,float duration,float start,float end,float rest){
    float u=age/duration;
    // 構え → 振り抜き → 戻り を 18% と 65% で切り替える
    return u<.18f?rest+(start-rest)*LifeSmooth(u/.18f):u<.65f?start+(end-start)*LifeSmooth((u-.18f)/.47f):end+(rest-end)*LifeSmooth((u-.65f)/.35f);
}
// GPU の目標と当たりのカプセルで共有する (正規化したメッシュの座標)
inline Vec LifeWeaponPoseAt(LifeAction form,Vec point,bool attacking,float age,float fluidAge){
    // 頂点ごとのばらばらな揺れではなく、ゆっくりそろったずれと呼吸の動き。
    // 断面の拡大を逆数にして体積を保ち、シルエットを保つ
    // 呼吸と断面のゆっくりした変化
    float breath=1+.022f*std::sin(fluidAge*1.5f),cross=1+.035f*std::sin(fluidAge*1.8f-point.y*.65f);
    point.x*=cross/std::sqrt(breath);point.z/=cross*std::sqrt(breath);point.y*=breath;
    point.z+=.075f*std::sin(fluidAge*1.8f-point.y*.65f)*LifeSmooth(point.y/4);
    // 攻撃の長さ
    float duration=LifeWeaponDuration(form);
    if(form==LifeAction::Hammer){
        // 打つ軸は前と上の振りの平面にある。元のメッシュの頭は
        // 左右に向いているので、ハンマー全体を 90 度回す
        float x=point.x;point.x=-point.z;point.z=x;
        // 振りの角度と、当たった瞬間のつぶれ
        float angle=attacking?LifeWeaponSwing(age,duration,-.55f,1.05f,-.18f):-.18f;
        float squash=attacking?1-.24f*std::exp(-std::pow((age-duration*.65f)/.045f,2.f)):1;
        if(point.y>3.5f){point.y=4.7f+(point.y-4.7f)*squash;point.x/=std::sqrt(squash);point.z/=std::sqrt(squash);}
        // 柄の根元を中心に回す
        float y=point.y-.35f,c=std::cos(angle),s=std::sin(angle);
        return {point.x,.35f+y*c-point.z*s,y*s+point.z*c};
    }
    if(form==LifeAction::Sword){
        // 剣は横に振る
        float yaw=attacking?LifeWeaponSwing(age,duration,-1.1f,1.15f,-.15f):-.15f;
        float pitch=0; // 水平に 1 回薙ぐだけ。斜めや回転の連撃は無い
        float y=.6f+(point.y-.6f)*std::cos(pitch)-point.z*std::sin(pitch),z=(point.y-.6f)*std::sin(pitch)+point.z*std::cos(pitch);
        return {point.x*std::cos(yaw)+z*std::sin(yaw),y,-point.x*std::sin(yaw)+z*std::cos(yaw)};
    }
    return point;
}
inline Vec LifeWeaponPose(LifeAction form,Vec point,bool attacking,float age){return LifeWeaponPoseAt(form,point,attacking,age,0);}
inline Vec LifeWeaponPose(const LifeBody& body,Vec point,float age){return body.action==LifeAction::Tire?LifeTirePose(point,body.tireAngle):LifeWeaponPoseAt(body.action,point,body.weapon.active,age,body.age-(body.weapon.age-age));}
// 武器の点の速度 (体の速度と、振りやタイヤの回転の分)
inline Vec LifeWeaponVelocity(const LifeBody& body,Vec point,float dt){
    if(dt<=1e-5f)return body.velocity;
    // 体の前方向でローカルからワールドへ回す
    auto world=[](Vec p,Vec f){return Vec{f.z*p.x+f.x*p.z,p.y,-f.x*p.x+f.z*p.z};};
    Vec current,previous;
    // タイヤは転がる角度から、前後のフレームの位置を求める
    if(body.action==LifeAction::Tire){
        float angular=Dot(body.velocity,body.forward)/(LifeTireRadius*LifeScale(body.mass))+body.tireSpin;
        current=LifeTirePose(point,body.tireAngle);previous=LifeTirePose(point,body.tireAngle-angular*dt);
    // それ以外は攻撃の姿勢から前後のフレームの位置を求める
    }else{current=LifeWeaponPoseAt(body.action,point,body.weapon.active,body.weapon.age,body.age);
        previous=LifeWeaponPoseAt(body.action,point,body.weapon.active,body.weapon.previousAge,body.age-dt);}
    // 体の速度に、姿勢の変化による速度を足す
    return body.velocity+(world(current,body.forward)-world(previous,body.previousForward))*(LifeScale(body.mass)/dt);
}
// 攻撃の当たりを調べる時間の範囲
inline bool LifeWeaponHitInterval(const LifeBody& body,int pulse,float& from,float& to){
    // 攻撃中で最初の当たりだけ
    if(!body.weapon.active||pulse!=0)return false;
    float begin=0,end=0;
    // 武器ごとの当たりの時間の範囲
    if(body.action==LifeAction::Hammer){if(pulse)return false;begin=.19f;end=.40f;}
    else if(body.action==LifeAction::Sword){begin=.14f;end=.43f;}
    else if(body.action==LifeAction::Plane){begin=.18f;end=.72f;}
    else return false;
    // 前のフレームから今のフレームまでの範囲と重なる部分
    from=std::max(begin,body.weapon.previousAge);to=std::min(end,body.weapon.age);return to>=from;
}
// 点と線分の距離
inline float LifeSegmentDistance(Vec point,Vec a,Vec b){
    Vec line=b-a;float t=std::clamp(Dot(point-a,line)/std::max(1e-6f,Dot(line,line)),0.f,1.f);return Length(point-(a+line*t));
}
// 武器が対象に当たったか (攻撃中の時間を細かく分けて調べる)
inline bool LifeWeaponTouches(const LifeBody& body,Vec target,float targetRadius,int pulse){
    float from,to;if(!LifeWeaponHitInterval(body,pulse,from,to))return false;
    // 武器の当たりのカプセル (2 点と半径)
    Vec a,b;float radius;
    if(body.action==LifeAction::Hammer){a={-2.2f,4.8f,0};b={2.2f,4.8f,0};radius=1.2f;}
    else if(body.action==LifeAction::Sword){a={0,.9f,.5f};b={0,.9f,7.8f};radius=.7f;}
    else {a={0,.8f,-2};b={0,.8f,3.6f};radius=1.5f;}
    // 大きさの倍率と、時間を細かく分けた数
    float scale=LifeScale(body.mass);int samples=std::max(1,int(std::ceil((to-from)*180)));
    // 各時刻の体の位置と姿勢でカプセルを置き、対象に触れたか調べる
    for(int i=0;i<=samples;++i){float age=from+(to-from)*float(i)/samples;
        Vec origin=body.at-body.velocity*(body.weapon.age-age);origin.y-=1.25f;
        auto world=[&](Vec point){point=LifeWeaponPose(body,point,age)*scale;return origin+Vec{body.forward.z*point.x+body.forward.x*point.z,point.y,-body.forward.x*point.x+body.forward.z*point.z};};
        if(LifeSegmentDistance(target,world(a),world(b))<=radius*scale+targetRadius)return true;
    }
    return false;
}
// 武器のダメージ (体の大きさに比例する)
inline float LifeWeaponDamage(const LifeBody& body){
    return (body.action==LifeAction::Hammer?24.f:body.action==LifeAction::Sword?16.f:20.f)*LifeScale(body.mass);
}
}
