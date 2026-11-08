// DevourGimmickRules.h
// 吸収モードの部屋のギミックの形と規則。エンジンに依存しない。
// どのギミックも「床の輪の中で変形キーを 1 回押す」だけで動き、あとは決まった動きを自動で進める。
// 体はその間、液体の粒子で道具の形 (ボール・水鉄砲・ドミノ・紙飛行機・流れる膜・しずく) になる。
// 形の大きさは普段の体と同じくらいの体積にする (粒子の数は決まっているので、大きすぎると表面が途切れる)
#pragma once
#include "DevourRules.h"
#include "SlimeWeaponRules.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace Game::Chrono {
enum class GimmickKind {Shoot,Domino,Bowling,Paint,Liquid,Plane};
// 体の形の番号 (LifeCloud の番号。武器の形の続き)
// (26 のヘリコプターはギミックではなく、高い所から落ちたときの形)
inline constexpr int GimmickBallShape=20,GimmickGunShape=21,GimmickDominoShape=22,GimmickPlaneShape=23,GimmickSheetShape=24,GimmickDropShape=25,GimmickHeliShape=26;
inline bool GimmickShape(int shape){return shape>=GimmickBallShape&&shape<=GimmickHeliShape;}
// 形を作る範囲 (x は左右の半分、y は 0 から、z は前後の半分)
struct GimmickBounds {float x=3,y=6,z=3;};
// ボウリングの玉: 指の穴が 3 つある球 (転がると穴が回って、回っているのが分かる)
inline constexpr float GimmickBallRadius=2.3f;
inline bool GimmickInsideBall(Vec p){
    Vec d=p-Vec{0,GimmickBallRadius,0};if(Length(d)>GimmickBallRadius)return false;
    const Vec holes[]={{-.55f,.8f,.35f},{.55f,.8f,.35f},{0,.62f,-.78f}};
    for(const auto& h:holes){Vec axis=Unit(h);float along=Dot(d,axis);if(along>GimmickBallRadius*.5f&&Length(d-axis*along)<.38f)return false;}
    return true;
}
// 水鉄砲: 胴・前の銃身・下の握り・上の水のタンク (前は +z)
inline bool GimmickInsideGun(Vec p){
    if(std::abs(p.x)<1.2f&&p.y>1.6f&&p.y<4&&p.z>-2.6f&&p.z<1.4f)return true;
    if(LifeSegmentDistance(p,{0,3.2f,1.4f},{0,3.2f,5.4f})<.75f)return true;
    if(std::abs(p.x)<.8f&&p.y>0&&p.y<1.8f&&p.z>-2.4f+.3f*(1.8f-p.y)&&p.z<-.8f+.3f*(1.8f-p.y))return true;
    return Length(p-Vec{0,5.3f,-1})<1.5f;
}
// ドミノ: 立てた 1 枚の板 (前へ倒れる)
inline constexpr float GimmickDominoHalfX=1.5f,GimmickDominoHeight=6.4f,GimmickDominoHalfZ=1.2f;
inline bool GimmickInsideDomino(Vec p){return std::abs(p.x)<GimmickDominoHalfX&&p.y>0&&p.y<GimmickDominoHeight&&std::abs(p.z)<GimmickDominoHalfZ;}
// 紙飛行機: 平たい三角の翼と下の折り目 (先が +z)
inline bool GimmickInsidePlane(Vec p){
    if(p.z<-3.5f||p.z>4.5f)return false;float span=3.8f*(4.5f-p.z)/8;
    if(p.y>1.5f&&p.y<2.7f&&std::abs(p.x)<span)return true;
    return std::abs(p.x)<.5f&&p.y>.4f&&p.y<1.6f&&p.y>1.6f-1.2f*(4.5f-p.z)/8;
}
// 流れる膜: 薄く広がった楕円 (前後に長い)。液体をつたって壁を登るときの体
inline bool GimmickInsideSheet(Vec p){float r=(p.x/3.6f)*(p.x/3.6f)+(p.z/5.4f)*(p.z/5.4f);return r<1&&p.y>=0&&p.y<.25f+.95f*std::sqrt(1-r);}
// しずく: 絵の具に飛び込むときの、先のとがった水滴
inline bool GimmickInsideDrop(Vec p){
    float h=std::sqrt(p.x*p.x+p.z*p.z);if(Length(p-Vec{0,2.1f,0})<2.1f)return true;
    return p.y>2.1f&&p.y<6&&h<2.1f*(6-p.y)/3.9f;
}
// ヘリコプター: 丸い胴・後ろへ伸びる尾・上の軸と、十字の 2 枚の回転翼 (前は +z。翼の高さより上は回る)
inline constexpr float GimmickRotorY=4.6f,GimmickRotorReach=5.f;
inline bool GimmickInsideHeli(Vec p){
    const Vec d=p-Vec{0,2,.3f};if((d.x/1.9f)*(d.x/1.9f)+(d.y/1.6f)*(d.y/1.6f)+(d.z/2.2f)*(d.z/2.2f)<1)return true;
    if(LifeSegmentDistance(p,{0,2.3f,-1.6f},{0,2.8f,-4.6f})<.45f)return true;
    if(LifeSegmentDistance(p,{0,3.3f,0},{0,GimmickRotorY,0})<.35f)return true;
    // (翼は細すぎると流体の格子より薄くなって消えるので、厚みと幅を持たせる)
    if(std::abs(p.y-GimmickRotorY)<.32f&&((std::abs(p.x)<.6f&&std::abs(p.z)<GimmickRotorReach)||(std::abs(p.z)<.6f&&std::abs(p.x)<GimmickRotorReach)))return true;
    return false;
}
inline GimmickBounds GimmickShapeBounds(int shape){
    switch(shape){case GimmickBallShape:return {GimmickBallRadius,2*GimmickBallRadius,GimmickBallRadius};case GimmickGunShape:return {1.6f,6.8f,5.6f};
        case GimmickDominoShape:return {GimmickDominoHalfX,GimmickDominoHeight,GimmickDominoHalfZ};case GimmickPlaneShape:return {3.8f,2.7f,4.5f};
        case GimmickSheetShape:return {3.6f,1.2f,5.4f};case GimmickHeliShape:return {GimmickRotorReach,GimmickRotorY+.4f,GimmickRotorReach};default:return {2.1f,6,2.1f};}
}
inline bool GimmickInside(int shape,Vec p){
    switch(shape){case GimmickBallShape:return GimmickInsideBall(p);case GimmickGunShape:return GimmickInsideGun(p);case GimmickDominoShape:return GimmickInsideDomino(p);
        case GimmickPlaneShape:return GimmickInsidePlane(p);case GimmickSheetShape:return GimmickInsideSheet(p);case GimmickHeliShape:return GimmickInsideHeli(p);default:return GimmickInsideDrop(p);}
}
// 形の姿勢: 前へ転がる (spin)・前へ倒れる (tip、下の前の角を軸に)・機首の上下 (pitch)・左右の傾き (roll)
struct GimmickPose {float spin=0,tip=0,pitch=0,roll=0,squash=0;};
inline Vec GimmickRotateX(Vec p,Vec pivot,float a){Vec d=p-pivot;float c=std::cos(a),s=std::sin(a);return pivot+Vec{d.x,d.y*c-d.z*s,d.y*s+d.z*c};}
inline Vec GimmickRotateZ(Vec p,Vec pivot,float a){Vec d=p-pivot;float c=std::cos(a),s=std::sin(a);return pivot+Vec{d.x*c-d.y*s,d.x*s+d.y*c,d.z};}
inline Vec GimmickPosePoint(int shape,Vec p,const GimmickPose& pose){
    if(shape==GimmickBallShape)return GimmickRotateX(p,{0,GimmickBallRadius,0},pose.spin);
    if(shape==GimmickDominoShape)return GimmickRotateX(p,{0,0,GimmickDominoHalfZ},pose.tip);
    if(shape==GimmickPlaneShape){p=GimmickRotateZ(p,{0,2,0},pose.roll);return GimmickRotateX(p,{0,2,0},-pose.pitch);}
    if(shape==GimmickSheetShape)return GimmickRotateX(p,{0,0,0},-pose.pitch);
    // ヘリコプター: 回転翼だけが軸の周りを回り、全体は進む向きへ少し前に傾く
    if(shape==GimmickHeliShape){if(p.y>GimmickRotorY-.4f){const float c=std::cos(pose.spin),s=std::sin(pose.spin);p={p.x*c-p.z*s,p.y,p.x*s+p.z*c};}
        p=GimmickRotateZ(p,{0,2,0},pose.roll);return GimmickRotateX(p,{0,2,0},pose.pitch);}
    if(shape==GimmickDropShape){float wide=1/std::sqrt(std::max(.2f,1-pose.squash));return {p.x*wide,p.y*(1-pose.squash),p.z*wide};}
    return p;
}

// 作動範囲の膜の高さ: 小さな輪でも体より十分高く、広い輪 (絵の具) は少し高く
inline float GimmickCurtainHeight(float radius){return std::clamp(16+radius*.2f,16.f,26.f);}

// 形の中の芯: 体の中の物を、形が崩れないように小さくしてここへ集める。center は形の厚い所の真ん中、
// thin はそこでの厚みの半分 (物の大きさの上限と、物を散らす幅に使う)
struct GimmickCore {Vec center{};float thin=1;};
inline GimmickCore GimmickShapeCore(int shape){
    switch(shape){case GimmickBallShape:return {{0,GimmickBallRadius,0},1.5f};case GimmickGunShape:return {{0,2.8f,-.6f},1.f};
        case GimmickDominoShape:return {{0,GimmickDominoHeight*.5f,0},1.f};case GimmickPlaneShape:return {{0,2.1f,.8f},.5f};
        case GimmickSheetShape:return {{0,.5f,0},.45f};case GimmickHeliShape:return {{0,2,.3f},1.3f};default:return {{0,2.1f,0},1.6f};}
}
// 物の大きさ (半分の幅) の上限と、芯から散らす幅 (形の倍率をかける前)
inline constexpr float GimmickMealSize=.7f,GimmickMealSpread=.45f;

// ---- 道をたどる (液体・紙飛行機・ボウリング) ----
inline float GimmickPathLength(const std::vector<Vec>& path){float total=0;for(size_t i=1;i<path.size();++i)total+=Length(path[i]-path[i-1]);return total;}
// 道の上の距離 s の点と、その区間の向き
inline Vec GimmickPathPoint(const std::vector<Vec>& path,float s,Vec* direction=nullptr){
    if(path.empty())return {};if(path.size()==1){if(direction)*direction={0,0,1};return path[0];}
    for(size_t i=1;i<path.size();++i){Vec a=path[i-1],b=path[i];float length=Length(b-a);
        if(s<=length||i+1==path.size()){if(direction)*direction=length>1e-4f?(b-a)*(1/length):Vec{0,0,1};return a+(b-a)*(std::clamp(s,0.f,length)/std::max(length,1e-4f));}
        s-=length;}
    return path.back();
}
// 道の向きの水平の成分 (真上へ登る区間は前後の区間の水平の向きを使う)
inline Vec GimmickPathHeading(const std::vector<Vec>& path,float s){
    Vec d;GimmickPathPoint(path,s,&d);Vec flat{d.x,0,d.z};if(Length(flat)>.05f)return Unit(flat);
    for(float k=4;k<200;k+=4){Vec ahead;GimmickPathPoint(path,s+k,&ahead);flat={ahead.x,0,ahead.z};if(Length(flat)>.05f)return Unit(flat);
        GimmickPathPoint(path,s-k,&ahead);flat={ahead.x,0,ahead.z};if(Length(flat)>.05f)return Unit(flat);}
    return {0,0,1};
}

// ---- 時間と速さ ----
// 変形にかける時間 (形へ寄せきるまで) と、元の体へ戻る時間
inline constexpr float GimmickFormTime=.35f,GimmickReleaseTime=.25f;
// 液体をつたう速さ・紙飛行機の速さ・ボウリングの玉の速さ (1 秒に進む長さ)
inline constexpr float GimmickFlowSpeed=78,GimmickPlaneSpeed=95,GimmickBallSpeed=190;
// 紙飛行機をスティックでずらせる幅 (道の左右)
inline constexpr float GimmickPlaneSteer=16;
// だるま落とし: 胴の輪の数・狙いの上下の 1 往復の時間・撃ってから当たるまでの時間・成功で出る小物の数
inline constexpr int GimmickDarumaDiscs=5;
inline constexpr float GimmickAimPeriod=1.6f,GimmickShotTime=.18f;
// 狙いの高さ (0 が一番下の輪の真ん中、1 がだるまの頭の真ん中)。上下に往復する
inline float GimmickAim(float clock){float u=std::fmod(clock/GimmickAimPeriod,1.f);return u<.5f?u*2:2-u*2;}
// 狙いの高さから当たる物を決める: 0..discs-1 は胴の輪 (下から)、discs は頭
inline int GimmickAimTarget(float aim,int discsLeft){return std::clamp(int(std::floor(aim*float(discsLeft)+.5f)),0,discsLeft);}
// ドミノが次々に倒れる間隔と、1 枚が倒れきる時間・倒れて止まる角度
inline constexpr float GimmickDominoStep=.085f,GimmickDominoFall=.2f,GimmickDominoRest=1.2f;
// ボウリング: 玉の通り道のピンは直接倒れ (玉の半径の hitShare 倍 + ピンの半径の幅)、倒れたピンは
// 当たった側の外へ向かって後ろのピンを倒す。真ん中を通せばストライク、端を通すと数本しか倒れない。
// ピンの位置は道の上の 2 次元 (x 前・z 横)、lateral は玉の横の位置
inline constexpr float GimmickPinHitShare=.6f,GimmickPinChain=1.3f;
inline std::vector<bool> GimmickPinsDown(const std::vector<Vec>& pins,float lateral,float ballRadius,float pinRadius,float spacing){
    std::vector<bool> down(pins.size());
    for(size_t i=0;i<pins.size();++i)down[i]=std::abs(pins[i].z-lateral)<ballRadius*GimmickPinHitShare+pinRadius;
    for(int round=0;round<4;++round)for(size_t i=0;i<pins.size();++i){if(!down[i])continue;const float side=pins[i].z-lateral;
        for(size_t k=0;k<pins.size();++k){if(down[k])continue;Vec d=pins[k]-pins[i];
            if(d.x>0&&Length(d)<spacing*GimmickPinChain&&d.z*side>=-.01f)down[k]=true;}}
    return down;
}
// ヘリコプター: 足元までこの高さ (体の大きさの倍率をかける前) より上で落ち始めたらなる。降りる速さの上限と回転翼の速さ
inline constexpr float GimmickHeliDrop=9,GimmickHeliFall=16,GimmickHeliSpin=20;
inline bool GimmickHeliStarts(float drop,float scale,float fallSpeed,bool grounded){return !grounded&&fallSpeed<0&&drop>GimmickHeliDrop*scale;}
// 絵の具: 塗った色と同じ色の物を飲むと増える量の倍率
inline constexpr float GimmickPaintBonus=1.5f;
inline bool GimmickSameColor(float r0,float g0,float b0,float r1,float g1,float b1){return std::abs(r0-r1)+std::abs(g0-g1)+std::abs(b0-b1)<.12f;}
}
