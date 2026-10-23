// CreatureMotion.h
// 生き物のボス (蛇 ⇔ 鳥) の形の計算。背骨・装甲・翼・頭の各部位の位置と大きさを、時間と変形の割合から求める。
#pragma once
#include "ChronoRules.h"
#include "CreatureCombat.h"
#include <array>
#include <cassert>
#include <vector>

namespace Game::Chrono {
// 大きさの倍率・変形にかかる時間・背骨と部位の数
inline constexpr float CreatureScale=6.5f;
inline constexpr float CreatureMorphSeconds=8.f;
inline constexpr int CreatureSpineCount=26,CreaturePartCount=326;
// 部位の役割 (背骨・装甲・翼・羽・頭の各部)
enum class CreatureRole { Spine, DorsalArmor, FlankArmor, VentralArmor, WingSpar, PrimaryFeather,
    CovertFeather, LeadingFeather, Skull, Jaw, Eye, Brow, Beak, Horn, Cheek, Crest };
// 部位の形 (使うメッシュの種類)
enum class CreatureShape { Link, Armor, Spar, Primary, Covert, Beak, Horn, Orb };
// 翼の部位か / 頭の部位か
inline bool CreatureWing(CreatureRole role){return role==CreatureRole::WingSpar||role==CreatureRole::PrimaryFeather||
    role==CreatureRole::CovertFeather||role==CreatureRole::LeadingFeather;}
inline bool CreatureHead(CreatureRole role){return role>=CreatureRole::Skull;}
// ボスの時間で評価するので、ヒットストップや集中は全部位に同じように効く
inline float Ease(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
// 部位 1 つ (位置・大きさ・回転・材質・役割・形・番号)
struct CreaturePiece { Vec position{},size{1,1,1},rotation{}; int material=0;
    // 役割と形
    CreatureRole role=CreatureRole::Spine;CreatureShape shape=CreatureShape::Link;
    // 背骨の節・左右・層・翼の長さの番号
    int segment=-1,side=0,layer=-1,span=-1;
};
// 全部位の姿勢 (背骨・翼の根元と先・頭蓋の番号、核と頭の位置)
struct CreaturePose {
    // 部位の一覧と数
    std::array<CreaturePiece,384> pieces{};
    int count=0;
    // 背骨の部位の番号
    std::array<int,CreatureSpineCount> spineIndex{};
    // 翼の根元と先端の部位の番号
    std::array<int,2> wingRoot{{-1,-1}},wingTip{{-1,-1}};
    // 頭蓋の部位の番号
    int skullIndex=-1;
    // 核と頭の位置
    Vec core{},head{};
    // 部位を追加して番号を返す
    int Add(Vec at,Vec size,Vec rotation,int material,CreatureRole role,CreatureShape shape,
        int segment=-1,int side=0,int layer=-1,int span=-1){
        // 部位の上限を超えないか確かめる
        assert(count<static_cast<int>(pieces.size()));
        int index=count++;
        pieces[index]={at,size,rotation,material,role,shape,segment,side,layer,span};
        return index;
    }
};
// 背骨 i 番目の位置 (蛇の形と鳥の形を bird で補間する)
inline Vec CreatureSpine(int i,float age,float bird){
    // 首 (0) から尾 (1) までの割合
    float u=static_cast<float>(i)/25;
    // 持ち上げたコブラの首が、床の上に休む太いとぐろにつながる
    // 蛇: 回転しながらとぐろを巻き、首を持ち上げる
    float a=u*5.4f+age*.32f;
    float radius=3.6f*std::max(.3f,1-u*.78f);
    float groundedBody=radius*.82f*.85f+.06f;
    Vec snake{std::sin(a)*std::min(11.f,u*32),groundedBody+7*std::exp(-u*9),9+u*23+std::sin(a)*3};
    snake.x+=std::sin(age*1.5f-u*7)*.65f;
    // 鳥: 首 (0〜6) と胴 (7〜25) をそれぞれ伸ばし、ゆっくり揺らす
    Vec flight;
    if(i<7){float v=static_cast<float>(i)/6;
        flight={std::sin(age*.6f)*.8f,23-v*6,7+v*8};
    }else{float v=static_cast<float>(i-6)/19;
        flight={std::sin(age*1.7f-v*6)*v*3.2f,17-v*8,15+v*17};}
    flight.y+=std::sin(age*2)*.45f;
    // 蛇と鳥を補間する
    return Lerp(snake,flight,bird);
}
// OBJ の断面は直接拡大するので、高さは piece.size.y の半分ではない
inline float CreatureVerticalExtent(const CreaturePiece& piece){
    // 形ごとの幅と高さの割合
    float x=piece.shape==CreatureShape::Horn?.7f:piece.shape==CreatureShape::Spar?.85f:1.f;
    float y=piece.shape==CreatureShape::Link?.94f:piece.shape==CreatureShape::Armor?.58f:
        piece.shape==CreatureShape::Primary?.26f:piece.shape==CreatureShape::Covert?.35f:
        piece.shape==CreatureShape::Horn?.72f:piece.shape==CreatureShape::Beak?1.02f:.5f;
    // 回転による高さの増え方
    float pitch=std::abs(std::sin(piece.rotation.x)),roll=std::abs(std::sin(piece.rotation.z));
    return piece.size.y*y*std::abs(std::cos(piece.rotation.x))*std::abs(std::cos(piece.rotation.z))+
        piece.size.x*x*roll+piece.size.z*.5f*pitch;
}
// 時間 age と変形の割合 form から全部位の姿勢を求める
inline CreaturePose EvaluateCreature(float age,float form){
    // 鳥の割合と、変形の途中の散らばり
    CreaturePose out;float bird=Ease(form),scatter=std::sin(bird*3.14159265f);
    // 背骨と、その上と横の装甲
    for(int i=0;i<26;++i){
        // 背骨の位置と、前後の背骨から求めた向き
        float u=static_cast<float>(i)/25;Vec at=CreatureSpine(i,age,bird);
        Vec tangent=CreatureSpine(std::min(25,i+1),age,bird)-CreatureSpine(std::max(0,i-1),age,bird);
        Vec rotation{-std::atan2(tangent.y,std::sqrt(tangent.x*tangent.x+tangent.z*tangent.z)),std::atan2(tangent.x,tangent.z),0};
        // 首のあたりを太くする (鳥のときは胸)
        float v=(u-.24f)*9;
        float taper=std::max(.3f,1-u*.78f),thickness=(3.6f+bird*3.2f*std::exp(-v*v))*taper;
        // 背骨の部位
        out.spineIndex[i]=out.Add(at,{thickness,thickness*.82f,1.9f},rotation,0,
            CreatureRole::Spine,CreatureShape::Link,i);
        // 左右と背の装甲 (変形の途中は散らばる)
        Vec right{std::cos(rotation.y),0,-std::sin(rotation.y)};
        for(int side=-1;side<=1;++side){
            Vec local=right*(side*thickness*.36f)+Vec{0,thickness*(side==0?.43f:.16f),0};
            float phase=i*.73f+side*1.7f;
            Vec burst{std::cos(phase)*3,std::sin(phase*.7f)*2+2,std::sin(phase)*3};
            out.Add(at+local+burst*scatter,{thickness*.67f,.42f,2.8f},
                rotation+Vec{scatter*.7f,scatter*std::sin(phase),side*.55f},2,
                side==0?CreatureRole::DorsalArmor:CreatureRole::FlankArmor,CreatureShape::Armor,i,side);
        }
    }
    // 左右の翼: 骨と 3 層の羽
    Vec shoulder=CreatureSpine(6,age,bird);
    for(int side:{-1,1}){
        // 肩 → 肘 → 手首 → 指。先の関節ほど振り上げに遅れる
        // 肩・肘・手首・指の関節 (羽ばたきで動く)
        Vec joints[]={shoulder+Vec{side*1.6f,0,0},
            shoulder+Vec{side*8.f,3+std::sin(age*1.65f)*3,-1.5f},
            shoulder+Vec{side*15.f,1+std::sin(age*1.65f-.45f)*6,3},
            shoulder+Vec{side*21.f,-1+std::sin(age*1.65f-.8f)*9,7}};
        // 関節の間に翼の骨を置く (蛇のときは背骨に沿って小さく)
        for(int j=0;j<3;++j){
            Vec d=joints[j+1]-joints[j];
            Vec rot{-std::atan2(d.y,std::sqrt(d.x*d.x+d.z*d.z)),std::atan2(d.x,d.z),0};
            int index=out.Add(Lerp(CreatureSpine(8+j*5,age,0),(joints[j]+joints[j+1])*.5f,bird),
                Lerp(Vec{.55f,.4f,1.35f},Vec{2.8f-j*.55f,1.8f-j*.4f,Length(d)+1},bird),rot*bird,0,
                CreatureRole::WingSpar,CreatureShape::Spar,-1,side,-1,j);
            if(j==0)out.wingRoot[side<0?0:1]=index;
        }
        // 1 列の櫛ではなく、曲がった骨を 3 層の羽がずらして包む:
        // 長い風切り羽・短い雨覆・持ち上がった前縁
        // 18 枚 x 3 層の羽を翼の骨に沿って並べる
        for(int layer=0;layer<3;++layer)for(int i=0;i<18;++i){
            float u=(static_cast<float>(i)+layer*.22f)/18;
            // 翼の骨の上の位置
            float along=u*3;int joint=std::min(2,static_cast<int>(along));
            Vec root=Lerp(joints[joint],joints[joint+1],along-joint);
            // 先の方ほど外へ広げる
            float fan=Ease((u-.55f)/.45f);
            Vec direction=Unit(Vec{side*(.12f+fan*1.7f),-.7f+std::sin(age*1.65f-u)*.35f,1.1f-fan*.45f});
            float length=(layer==0?8.5f:layer==1?5.4f:3.1f)*(1-.22f*u)+std::sin(u*3.14159f)*1.4f;
            Vec flying=root+Vec{0,layer*.55f,-layer*.65f}+direction*(length*.40f);
            // 蛇のときは背骨に沿ってたたむ
            Vec folded=CreatureSpine(7+i,age,0)+Vec{side*(.18f+layer*.12f),-.2f+layer*.14f,0};
            float phase=i*1.8f+side+layer;
            Vec burst{side*(2+u*5),3+std::sin(phase)*2,std::cos(phase)*4};
            Vec rotation{-std::atan2(direction.y,std::sqrt(direction.x*direction.x+direction.z*direction.z)),
                std::atan2(direction.x,direction.z),side*(-.2f+u*.35f)};
            // たたんだ位置と広げた位置を補間する
            int index=out.Add(Lerp(folded,flying,bird)+burst*scatter,
                Lerp(Vec{.38f,.16f,1.05f},Vec{layer==0?1.85f:2.2f,.24f,length},bird),rotation*bird,
                layer==0?2:layer==1?2:1,
                layer==0?CreatureRole::PrimaryFeather:CreatureRole::CovertFeather,
                layer==0?CreatureShape::Primary:CreatureShape::Covert,-1,side,layer,i);
            if(layer==0&&i==17)out.wingTip[side<0?0:1]=index;
        }
    }
    // 頭 (頭蓋・顎・目・眉)
    Vec head=CreatureSpine(0,age,bird);out.head=head;
    // 頭蓋と顎
    out.skullIndex=out.Add(head+Vec{0,.35f,-1},Lerp(Vec{3.7f,2.6f,4},Vec{3,2.8f,3.2f},bird),{},2,CreatureRole::Skull,CreatureShape::Beak);
    out.Add(head+Vec{0,-.6f,-2},Lerp(Vec{3,.65f,2.5f},Vec{1.7f,1.1f,3.3f},bird),{.12f,0,0},1,CreatureRole::Jaw,CreatureShape::Beak);
    // 左右の目と眉
    for(int side:{-1,1}){
        out.Add(head+Vec{side*1.22f,.65f,-2.1f},{.42f,.38f,.55f},{},3,CreatureRole::Eye,CreatureShape::Orb,-1,side);
        out.Add(head+Vec{side*1.1f,1,-1.8f},{1.2f,.35f,1.7f},{0,side*.3f,side*.18f},2,CreatureRole::Brow,CreatureShape::Armor,-1,side);
    }
    // 2 段目の装甲が各椎骨に重なる。淡い背の稜線は見やすく保ち、
    // 細く暗い腹が S 字の隙間を残す
    // 2 段目の装甲 (背骨ごとに左右と腹)
    for(int i=0;i<CreatureSpineCount;++i){
        float u=float(i)/25.f, v=(u-.24f)*9.f;
        float thickness=(3.6f+bird*3.2f*std::exp(-v*v))*std::max(.3f,1-u*.78f);
        Vec at=CreatureSpine(i,age,bird),next=CreatureSpine(std::min(25,i+1),age,bird);
        Vec tangent=next-at;float yaw=std::atan2(tangent.x,tangent.z);
        Vec right{std::cos(yaw),0,-std::sin(yaw)};
        for(int side:{-1,0,1}){
            Vec local=side?right*(side*thickness*.6f)+Vec{0,-thickness*.12f,.65f}:Vec{0,-thickness*.43f,.55f};
            out.Add(at+local+Vec{side*.8f,1.2f,std::sin(i*.9f+side)*.8f}*scatter,
                {thickness*(side?.42f:.56f),side?.3f:.26f,2.6f},
                {0,yaw,side*.68f},side?2:0,
                side?CreatureRole::FlankArmor:CreatureRole::VentralArmor,CreatureShape::Armor,i,side,1);
        }
    }
    // 別の前縁で櫛のシルエットを崩し、層になった翼にする
    // 翼の前縁の羽 (8 枚)
    for(int side:{-1,1})for(int i=0;i<8;++i){
        float u=float(i)/7.f;Vec root=shoulder+Vec{side*(2+u*18),2+std::sin(age*1.65f-u*.7f)*(3+u*5),u*6};
        Vec folded=CreatureSpine(7+i*2,age,0)+Vec{side*.32f,.25f,0};
        Vec flying=root+Vec{side*1.2f,.6f,-.8f};
        out.Add(Lerp(folded,flying,bird)+Vec{side*3.f,2.f,0.f}*scatter,
            Lerp(Vec{.36f,.15f,1.1f},Vec{2.1f-u*.7f,.38f,4.8f-u*1.4f},bird),
            {0,side*.4f,side*.15f},2,CreatureRole::LeadingFeather,CreatureShape::Covert,-1,side,3,i);
    }
    // くちばし・角・頬・とさか
    out.Add(head+Vec{0,-.05f,-3.45f},Lerp(Vec{2.3f,.8f,2.1f},Vec{1.5f,1.1f,3.2f},bird),{},2,CreatureRole::Beak,CreatureShape::Beak);
    out.Add(head+Vec{0,-.75f,-3.1f},{1.4f,.3f,2.2f},{.15f,0,0},0,CreatureRole::Beak,CreatureShape::Beak,-1,0,1);
    // 左右の角・頬・とさか
    for(int side:{-1,1}){
        out.Add(head+Vec{side*1.7f,1.7f,.35f},{.55f,.7f,2.8f},{-.45f,side*.4f,side*.32f},2,CreatureRole::Horn,CreatureShape::Horn,-1,side);
        out.Add(head+Vec{side*1.7f,-.2f,-.8f},{1.15f,.7f,2.3f},{0,side*.24f,side*.18f},2,CreatureRole::Cheek,CreatureShape::Armor,-1,side);
        out.Add(head+Vec{side*.55f,1.65f,.8f},{.5f,.45f,2.2f},{-.4f,side*.12f,side*.1f},2,CreatureRole::Crest,CreatureShape::Horn,-1,side);
    }
    // 部位の数を確かめ、核の位置を決める
    assert(out.count==CreaturePartCount);
    out.core=shoulder+Vec{0,-.2f,-2.3f};
    // カメラではなく、戦闘の正面を中心にワールド空間で大きくする
    auto expand=[](Vec v){return Vec{v.x*CreatureScale,v.y*CreatureScale,9+(v.z-9)*CreatureScale};};
    // 変形の途中は部位が菱形の模様を描いて飛び回り、最後にワールドの大きさへ広げる
    for(int i=0;i<out.count;++i){
        auto& piece=out.pieces[i];
        // 体の構造に沿った波で、各翼と装甲の並びの元を保つ
        int tier=piece.segment>=0?piece.segment/4:CreatureWing(piece.role)?7+std::max(0,piece.layer):11;
        int slot=i%32;
        float order=piece.segment>=0?1.f-float(piece.segment)/25.f:
            CreatureWing(piece.role)?.25f+float(std::max(0,piece.span))/17.f*.55f:1.f;
        // 各部位は 0.32 秒で移動し、波は変わらない 8 秒の周期を埋める
        // 出発と到着の時刻 (節の順に遅らせる)
        float depart=.075f+order*.13f;
        float arrive=.625f+order*.265f;
        // 模様へ移る量 (出発から到着の間だけ 1)
        float spread=AttackEase((form-depart)/.04f)*(1-AttackEase((form-arrive)/.04f));
        // 菱形の上の位置
        float edge=static_cast<float>(slot%8)/8;
        constexpr Vec corners[]={{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{1,0,0}};
        Vec diamond=Lerp(corners[slot/8],corners[slot/8+1],edge);
        // 模様を回す角度 (段ごとに逆回り)
        float pulse=AttackEase((form-.29f-tier*.007f)/.04f)+AttackEase((form-.44f-tier*.007f)/.04f)+AttackEase((form-.56f-tier*.005f)/.035f);
        float turn=(tier%2==0?1.f:-1.f)*(pulse*1.570796f+(form-.5f)*.5f)+tier*.18f;
        // 模様の半径と位置
        float radius=22.f+tier*3.2f;
        Vec pattern{(diamond.x*std::cos(turn)-diamond.z*std::sin(turn))*radius,
            10.f+tier*5.5f+(slot%2==0?.8f:-.8f),
            16.f+(diamond.x*std::sin(turn)+diamond.z*std::cos(turn))*radius*.65f};
        // 弧を描いて飛ぶ
        Vec arc{std::sin(slot*2.4f)*4,5,std::cos(slot*2.4f)*4};
        piece.position=Lerp(piece.position,pattern,spread)+arc*(4*spread*(1-spread));
        Vec patternRotation{.35f+form*1.2f,turn+slot/8*1.570796f,(tier%2==0?1.f:-1.f)*.6f};
        piece.rotation=Lerp(piece.rotation,patternRotation,spread);
        piece.size=piece.size*(1+spread*.25f);
        // ワールドの大きさへ広げる
        piece.position=expand(piece.position);piece.size=piece.size*CreatureScale;
    }
    out.head=expand(out.head);out.core=expand(out.core);
    return out;
}
// 決まった横の位置を与えると、蛇は攻撃の前にそこへ向かうと決められる
inline Vec CreatureCruisePoint(Vec player,Vec boss,int step,bool ink){
    // 回り込む側 (左右を交互に)
    float side=step%2==0?1.f:-1.f;
    // インクのモード: プレイヤーから 185 離れた、ボスと反対寄りの地点を 12 方向から選ぶ
    if(ink){
        Vec away=Unit(Vec{boss.x-player.x,0,boss.z-player.z});
        if(Length(away)<.1f)away={0,0,1};
        Vec flank{away.z,0,-away.x},best{};float bestScore=-1e9f;
        for(int i=0;i<12;++i){
            float angle=i*6.283185f/12.f;
            Vec direction=away*std::cos(angle)+flank*std::sin(angle);
            Vec at{std::clamp(player.x+direction.x*185.f,-300.f,300.f),0,
                std::clamp(player.z+direction.z*185.f,-305.f,345.f)};
            Vec delta=at-Vec{player.x,0,player.z};float distance=Length(delta);
            float score=-std::abs(distance-185.f)+Dot(Unit(delta),away)*24.f+Dot(Unit(delta),flank)*side*5.f;
            if(score>bestScore){bestScore=score;best=at;}
        }
        return best;
    }
    // 旧モード: プレイヤーの横前の地点
    return {std::clamp(player.x+side*(step%4<2?85.f:65.f),-200.f,200.f),0,
        std::clamp(player.z+48.f+(step%3-1)*18.f,-220.f,220.f)};
}
// 回り込む地点へなめらかに向かう速度
inline Vec CreatureCruiseVelocity(Vec current,Vec delta,float dt,float speed){
    // 目標までの距離に応じた速さで、速度をなめらかに変える
    float distance=Length(delta);
    Vec desired=distance<1.f?Vec{}:Unit(delta)*std::min(speed,distance*2.2f);
    return Lerp(current,desired,1-std::exp(-6.f*std::max(0.f,dt)));
}
// 胴の節は尾から頭へ順に出発し、翼の部位は翼の長さに沿って続く
inline Vec ReformPart(Vec from,Vec to,const CreaturePiece& piece,int index,float progress){
    // 出発の遅れ (胴は尾から、翼は根元から)
    float delay=piece.segment>=0?(1.f-float(piece.segment)/25.f)*.16f:
        CreatureWing(piece.role)?.035f+float(std::max(0,piece.span))/17.f*.14f:.17f;
    // 遅れを除いた進み具合
    float t=std::clamp((progress-delay)/(.96f-delay),0.f,1.f);
    float phase=index*2.399963f;
    // 散らばる向きと飛ぶ弧
    Vec scatter{std::cos(phase)*(10+index%7),12+float(index%11),std::sin(phase)*(10+index%7)};
    float depart=Ease(t/.24f),travel=AttackEase((t-.18f)/.82f);
    Vec start=from+scatter*depart;
    Vec arc{std::sin(phase)*18,22+float(index%13),std::cos(phase)*18};
    return Lerp(start,to,travel)+arc*(4*travel*(1-travel));
}
// 1 回の攻撃で、各跳びが最短の届く距離に収まるだけの羽を出す。
// 道は発射の時点で固定し、
// 危険な到着の直後の後も使えるので、外したチェインをやり直せる
inline std::vector<Vec> FeatherRoute(Vec player,Vec core){
    // プレイヤーの位置 (アリーナの範囲に収める)
    Vec start{std::clamp(player.x,-328.f,328.f),3,std::clamp(player.z,-338.f,378.f)};
    // 7 ずつの間隔で最低 5 本
    int count=std::max(5,static_cast<int>(std::ceil(Length(core-start)/7.f)));
    std::vector<Vec> route(static_cast<size_t>(count));
    // プレイヤーと核の間に左右に揺らしながら並べる
    for(int i=0;i<count;++i){float u=static_cast<float>(i+1)/(count+1);
        route[i]=Lerp(start,core,u)+Vec{std::sin(u*6.283185f)*1.5f,0,0};}
    return route;
}
}
