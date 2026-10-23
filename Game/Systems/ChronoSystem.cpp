// ChronoSystem.cpp
// 現在のゲームの中心のシステムの実装。旧来の確認用のモード (鎖で敵を引き寄せる戦闘・生き物のボス・アリーナ) と、
// 共通の処理 (当たり判定・カメラ・ダメージ・終了・描画)。本編の処理は末尾で読み込む .inl にある。
#include "../../Engine/FluidMorphShape.h"
#include "../Chrono/SlimeTentacleRules.h"
#include "../../Engine/FluidMorphShape.h"
#include <map>
#include "../UI/SceneMusic.h"
#include "ChronoSystem.h"
#include "GameControls.h"
#include <set>
#include <unordered_map>
#include "../UI/GameUI.h"
#include "../UI/RogueliteUI.h"
#include "../Chrono/CreatureMotion.h"
#include "../Chrono/MeadowTerrain.h"
#include "../Scenes/GameScene.h"
#include "../../Engine/SceneManager.h"
#include "../../Engine/Model.h"
#include "../../Engine/Audio.h"
#include "../../externals/nlohmann/json.hpp"
#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <limits>
#include <cstdio>

namespace Game {
// Chrono のゲームの規則の型 (Vec など) をそのまま使う
using namespace Chrono;
// このファイルの中だけで使う補助の関数
namespace {
// XMFLOAT3 と Vec の変換・前方向・キーが押されているか
Vec Read(const DirectX::XMFLOAT3& p){return {p.x,p.y,p.z};}
DirectX::XMFLOAT3 Write(Vec p){return {p.x,p.y,p.z};}
Engine::Vector3 EV(Vec p){return {p.x,p.y,p.z};}
Vec Forward(float yaw,float pitch){return {std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};}
bool Down(int key){return (GetAsyncKeyState(key)&0x8000)!=0;}
// 旧モードで使うモデルのパス
constexpr const char* grass="Resources/Models/Quaternius/Modular Platforms/Single Cube/FBX/Cube_Grass_Single.fbx";
constexpr const char* horn="Resources/Models/Quaternius/Enemies/FBX/Enemy.fbx";
constexpr const char* ball="Resources/Models/player_ball/ball.obj";
// 最高記録の保存先 (%LOCALAPPDATA%/LiquidTime/records-v1.json)
std::filesystem::path RecordPath(){
    wchar_t buffer[32768]{};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    return n>0 && n<32768 ? std::filesystem::path(buffer)/L"LiquidTime"/L"records-v1.json" : std::filesystem::path{};
}
// 秒を「分:秒.百分の一秒」の文字列にする
std::string TimeText(float seconds){char b[32];int s=static_cast<int>(seconds);std::snprintf(b,sizeof(b),"%02d:%02d.%02d",s/60,s%60,static_cast<int>(seconds*100)%100);return b;}
}

// 名前付きのメッシュのエンティティを作る (当たり判定の箱とカメラの遮蔽も付ける)
entt::entity ChronoSystem::Mesh(entt::registry& r,const std::string& name,const std::string& path,V center,V size){
    // 名前に「Chrono 」を付けたエンティティを作り、モデルを読み込む
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name);
    auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelPath=path;
    mr.modelHandle=Engine::Renderer::GetInstance()->LoadObjMesh(path);mr.useCubemap=false;
    // 中心の位置に置く
    auto& t=r.emplace<TransformComponent>(e);t.translate=Write(center);
    auto* model=Engine::Renderer::GetInstance()->GetModel(mr.modelHandle);
    // モデルの頂点の範囲を求め、指定の大きさに合うよう拡大して中心をそろえる
    if(model && !model->GetData().vertices.empty()){
        V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:model->GetData().vertices){
            lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
            hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
        }
        V scale{size.x/std::max(hi.x-lo.x,0.001f),size.y/std::max(hi.y-lo.y,0.001f),size.z/std::max(hi.z-lo.z,0.001f)};
        V mid=(lo+hi)*0.5f;t.scale=Write(scale);
        t.translate=Write(center-V{mid.x*scale.x,mid.y*scale.y,mid.z*scale.z});
    }
    // 草のブロックと自然物はカメラを遮る物として登録する
    if(path==grass || path.find("/Nature/")!=std::string::npos)
        r.emplace<CameraOccluder>(e,center,size,1.0f);
    return e;
}
// 見えない当たり判定の箱を作る
void ChronoSystem::Block(entt::registry& r,const std::string& name,V center,V size){
    // 見た目のメッシュを置く (床より下ならカメラの遮蔽は不要)
    auto mesh=Mesh(r,name,grass,center,size);
    if(center.y+size.y*.5f<=.01f)r.remove<CameraOccluder>(mesh);
    // 当たり判定の箱のエンティティを別に作る
    auto e=r.create();r.emplace<NameComponent>(e,"Chrono "+name+" collision");
    r.emplace<TransformComponent>(e).translate=Write(center);
    r.emplace<BoxColliderComponent>(e).size=Write(size);
    r.emplace<TagComponent>(e).tag=TagType::Wall;r.emplace<Solid>(e);
}
// 旧モードのステージを作る (足場・階段・敵・ボス)
void ChronoSystem::Build(entt::registry& r){
    // 谷・段丘・城の足場
    Block(r,"Valley",{0,-4,12},{110,8,130});
    Block(r,"West terrace",{-16,2,2},{14,4,16});
    Block(r,"East terrace",{16,4,15},{14,8,16});
    Block(r,"Upper terrace",{-10,6,29},{16,12,14});
    Block(r,"Citadel",{0,9,49},{28,18,24});
    // 広い段差は、敵が残っていなくても戻る道になる
    // 36 段の階段 (0.5 ずつ高くなる)
    for(int i=0;i<36;++i){float y=static_cast<float>(i+1)*0.5f;
        Block(r,"Stair "+std::to_string(i),{31,y*0.5f,-2+static_cast<float>(i)*1.35f},{8,y,1.42f});}
    Block(r,"Citadel landing",{24,9,49},{22,18,10});
    // 1 回だけ使える近道で、常設の階段と踊り場を補う
    // 足場は各崖の手前から近づき、後ろの踊り場が次の動きの失敗を受け止める
    // 復帰用の足場 (下段・中段・上段)
    Block(r,"Recovery lower",{-25,2.5f,17},{9,5,9});
    Block(r,"Recovery middle",{-23,5,29},{9,10,9});
    Block(r,"Recovery upper",{-19,7.5f,40},{9,15,9});
    // 周りの山の岩 8 つ
    for(int i=0;i<8;++i){float a=static_cast<float>(i)*0.785398f;
        Mesh(r,"Mountain "+std::to_string(i),"Resources/Models/Quaternius/Nature/FBX/RockPlatforms_Large.fbx",
            {std::sin(a)*66,12+static_cast<float>(i%3)*4,16+std::cos(a)*76},{26,38+static_cast<float>(i%3)*9,26});}
    // 木を 6 本置く
    const V trees[]={{-21,4,-1},{20,8,19},{-15,12,30},{-28,0,23},{30,0,-12},{-26,0,-20}};
    for(int i=0;i<6;++i)Mesh(r,"Tree "+std::to_string(i),"Resources/Models/Quaternius/Nature/FBX/Tree.fbx",trees[i]+V{0,4,0},{4,8,4});
    // 鎖で引き寄せられる足場の位置
    const V anchors[]={{-16,6,0},{16,10,12},{-10,14,27},{0,20,39},{24,2,-8},
        {-25,9,10.5f},{-23,14,22},{-19,19,34},{-8,22,35}};
    // 足場を置き、狙いの対象として登録する (5 番目以降は色を変える)
    for(int i=0;i<9;++i){
        auto e=Mesh(r,"Anchor "+std::to_string(i),ball,anchors[i],{1.4f,1.8f,1.4f});
        auto& target=r.emplace<Target>(e);target.kind=Kind::Anchor;target.radius=1.1f;target.reward=0;
        target.offset=anchors[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Anchor>(e);r.get<MeshRendererComponent>(e).color={0.1f,1.2f,1.4f,1};
        if(i>=5)r.get<MeshRendererComponent>(e).color={.2f,1.4f,.6f,1};
    }
    // 角のある跳ねる敵の位置
    const V enemies[]={{-5,1.4f,-19},{4,1.4f,-15},{0,1.4f,-7},{-6,1.4f,2},{5,1.4f,6},{0,1.4f,15},
        {-16,5.4f,5},{16,9.4f,16},{-10,13.4f,29}};
    // 敵を置き、狙いの対象・跳ねる動き・敵のタグを付ける
    for(int i=0;i<9;++i){
        auto e=Mesh(r,"Horned hopper "+std::to_string(i),horn,enemies[i],{2.4f,2.8f,2.4f});
        auto& target=r.emplace<Target>(e);target.radius=1.4f;target.hp=42;target.reward=18;
        target.offset=enemies[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Hopper>(e).timer=static_cast<float>(i%4)*0.18f;
        r.emplace<TagComponent>(e).tag=TagType::Enemy;
    }
    // ボスを城の上に置き、ボスと狙いの対象を付ける
    if(r.valid(boss_)){
        auto& t=r.get<TransformComponent>(boss_);t.translate={0,18,49};t.rotate={0,3.14159f,0};
        r.emplace_or_replace<Boss>(boss_);
        auto& bt=r.emplace_or_replace<Target>(boss_);bt.kind=Kind::Boss;bt.radius=3;bt.hp=360;bt.offset={0,4,0};bt.reward=0;
        // アニメーションがあれば、最初のクリップをループで再生する
        if(auto* a=r.try_get<AnimatorComponent>(boss_)){
            auto* m=Engine::Renderer::GetInstance()->GetModel(r.get<MeshRendererComponent>(boss_).modelHandle);
            if(m && !m->GetData().animations.empty()){
                const auto& data=m->GetData();const auto& clip=data.animations.front();
                a->currentAnimation=clip.name;a->time=0;a->isPlaying=true;a->loop=true;
                // 動く FBX の単位は、読み込んだ静止の範囲と違うことがある。
                // 評価した姿勢を 10 単位の城の守護者に合わせる
                // 最初のフレームの骨の姿勢を計算する
                std::vector<Engine::Matrix4x4> palette(data.bones.size(),Engine::Matrix4x4::Identity());
                m->UpdateSkeleton(data.rootNode,Engine::Matrix4x4::Identity(),clip,0,nullptr,0,0,palette,nullptr);
                V lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
                // スキンを適用した頂点の範囲を求める
                for(const auto& v:data.vertices){
                    using namespace DirectX;XMVECTOR point=XMLoadFloat4(&v.position),skin=XMVectorZero();float weight=0;
                    for(int j=0;j<4;++j)if(v.boneWeights[j]>0.0001f&&v.boneIndices[j]<palette.size()&&v.boneIndices[j]<128){
                        auto matrix=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&palette[v.boneIndices[j]]));
                        skin=XMVectorAdd(skin,XMVectorScale(XMVector4Transform(point,matrix),v.boneWeights[j]));weight+=v.boneWeights[j];
                    }
                    XMFLOAT3 at;XMStoreFloat3(&at,weight>0.001f?XMVectorScale(skin,1/weight):point);
                    lo={std::min(lo.x,at.x),std::min(lo.y,at.y),std::min(lo.z,at.z)};
                    hi={std::max(hi.x,at.x),std::max(hi.y,at.y),std::max(hi.z,at.z)};
                }
                // 高さ 10 になるよう拡大し、足元を城の上 (y=18) にそろえる
                if(hi.y>lo.y){float size=10/(hi.y-lo.y);V mid=(lo+hi)*0.5f;
                    t.scale={size,size,size};t.translate={mid.x*size,18-lo.y*size,49+mid.z*size};
                    bt.offset=V{0,23,49}-Read(t.translate);
                }
            }
        }
    }
    // 浮いた球ではなく、守護者の胴に合わせた見えない狙いの範囲
    // 弱点の範囲 (普段は見えない)
    weakpoint_=Mesh(r,"Weakpoint",ball,{0,23,49},{6,6,6});
    auto& w=r.emplace<Target>(weakpoint_);w.kind=Kind::Weakpoint;w.radius=3;w.reward=0;w.active=false;
    w.offset=V{0,23,49}-Read(r.get<TransformComponent>(weakpoint_).translate);
    r.get<MeshRendererComponent>(weakpoint_).enabled=false;
    // アリーナや生き物のモードなら、そのステージを作る
    if(arena_||creature_)BuildArena(r);
    if(creature_)BuildCreature(r);
}
// 生き物のボスのステージを作る
void ChronoSystem::BuildCreature(entt::registry& r){
    // 翼と引いたカメラのために、広く平らなアリーナにする
    // 前回作った「Chrono 」のエンティティを消す
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>()){
        const auto& name=r.get<NameComponent>(e).name;
        if(e!=weakpoint_&&name.rfind("Chrono ",0)==0)remove.push_back(e);
    }
    for(auto e:remove)r.destroy(e);
    // 広い床と 4 方向の壁
    Block(r,"Creature floor",{0,-2,0},{700,4,800});
    Block(r,"Creature north",{0,2,400},{700,4,2});Block(r,"Creature south",{0,2,-400},{700,4,2});
    Block(r,"Creature west",{-350,2,0},{2,4,800});Block(r,"Creature east",{350,2,0},{2,4,800});
    // ボスに生き物のボスのコンポーネントを付け、元のモデルは隠す
    auto& creature=r.emplace_or_replace<CreatureBoss>(boss_);
    if(inkMode_)creature.offset={0,0,125};
    r.get<MeshRendererComponent>(boss_).enabled=false;
    if(auto* a=r.try_get<AnimatorComponent>(boss_))a->isPlaying=false;
    // 最初の姿勢から各部位のエンティティを作る (形ごとのモデル)
    const auto pose=EvaluateCreature(0,0);
    for(int i=0;i<pose.count;++i){const auto& piece=pose.pieces[i];
        const char* models[]={"Resources/Models/Chrono/link.obj","Resources/Models/Chrono/armor.obj",
            "Resources/Models/Chrono/spar.obj","Resources/Models/Chrono/primary.obj",
            "Resources/Models/Chrono/covert.obj","Resources/Models/Chrono/beak.obj",
            "Resources/Models/Chrono/horn.obj"};
        auto shape=static_cast<int>(piece.shape);
        auto e=Mesh(r,"Creature part "+std::to_string(i),shape<7?models[shape]:ball,{0,0,0},{1,1,1});
        // 部位の番号・節・役割と、元の大きさと位置を記録する
        auto& t=r.get<TransformComponent>(e);auto& part=r.emplace<CreaturePart>(e);
        part.index=i;part.segment=piece.segment;part.role=piece.role;
        part.unitScale=Read(t.scale);part.unitOffset=Read(t.translate);
        r.emplace<CameraOccluder>(e);
    }
    // 弱点の核を表示する
    auto& core=r.get<MeshRendererComponent>(weakpoint_);core.enabled=true;core.color={.2f,.65f,.75f,1};
    r.get<Target>(weakpoint_).radius=3.0f;
    r.get<Target>(boss_).radius=3.0f;
    // プレイヤーをボスの前に置く
    r.get<TransformComponent>(player_).translate={0,1.3f,-12};
    r.get<Player>(player_).recoveryPoint={0,1.3f,-12};
    // 姿勢を反映する。インクのモードならインクのステージを作る
    PoseCreature(r);
    if(inkMode_){BuildInk(r);return;}
    // プレイヤーからボスへの道に、引き寄せの中継の羽を置く (最初は隠す)
    auto route=FeatherRoute({0,1,-12},Center(r,boss_));
    for(int i=0;i<std::min(5,static_cast<int>(route.size()));++i){
        auto e=Mesh(r,"Serpent relay "+std::to_string(i),"Resources/Models/Chrono/feather.obj",route[i],{2.5f,1,3});
        auto& target=r.emplace<Target>(e);target.kind=Kind::Anchor;target.radius=1.8f;target.reward=0;
        r.emplace<Anchor>(e).reusable=false;
        r.emplace<SerpentRelay>(e,route[i],static_cast<float>(i+1)/(route.size()+1));
        r.get<MeshRendererComponent>(e).color={.3f,1.3f,.65f,1};
        target.active=false;r.get<MeshRendererComponent>(e).enabled=false;
    }
}
// 生き物のボス (蛇と鳥) の各部位の姿勢を、時間と形の変化から求めて反映する
void ChronoSystem::PoseCreature(entt::registry& r){
    // 生き物の今の姿勢を計算する
    auto& creature=r.get<CreatureBoss>(boss_);auto pose=EvaluateCreature(creature.age,creature.form);
    if(inkMode_){
        // 生き物全体を獲物の方へ向ける。外側の通路には小さな飛行のシルエットが収まる
        // 鳥になるほど小さくし、向きに合わせて核の周りに回す
        float scale=1-.62f*Ease(creature.form),sn=std::sin(creature.heading),cs=std::cos(creature.heading);
        auto rotate=[&](V point){V d=(point-pose.core)*scale;return pose.core+V{d.x*cs+d.z*sn,d.y,-d.x*sn+d.z*cs};};
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];piece.position=rotate(piece.position);piece.size=piece.size*scale;piece.rotation.y+=creature.heading;
            // 扇状の羽の予告では翼を持ち上げる
            if(creature.stage==CreatureStage::Opening&&creature.sweepPhase==1&&CreatureWing(piece.role))
                piece.position.y+=std::sin(std::min(1.f,creature.sweepTime/1.15f)*1.570796f)*5;
        }
        // 急降下中は翼を体へたたむ
        if(creature.stage==CreatureStage::Dive){
            float fold=creature.sweepPhase==1?AttackEase(creature.sweepTime/1.15f)*.78f:
                creature.sweepPhase==2?.78f:.78f*(1-AttackEase(creature.sweepTime/.55f));
            for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];if(!CreatureWing(piece.role))continue;
                V root=pose.pieces[pose.wingRoot[piece.side<0?0:1]].position,d=piece.position-root;
                piece.position=Lerp(piece.position,root+V{d.x*.25f,d.y*.8f,d.z*.72f},fold);
                piece.rotation.z+=fold*(piece.side<0?-.35f:.35f);
            }
        }
        // 頭も同じように回す
        pose.head=rotate(pose.head);
    }
    // 移動は固い鎖を平行移動するのではなく、体に沿って伝わる
    float locomotion=std::clamp(Length(creature.moveVelocity)/30.f,0.f,1.f);
    if(locomotion>0){
        // 体の横向きと、尾ほど遅れる横揺れ
        V side{std::cos(creature.heading),0,-std::sin(creature.heading)};
        auto sway=[&](float u){return std::sin(creature.age*5.5f-u*5.f)*locomotion*(1.8f-u*.7f);};
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];
            if(piece.segment>=0){float u=float(piece.segment)/25.f;
                piece.position=piece.position+side*sway(u);
                piece.rotation.z+=std::cos(creature.age*5.5f-u*5.f)*locomotion*.04f;}
            else if(CreatureWing(piece.role)){float span=float(std::max(0,piece.span))/17.f;
                piece.position=piece.position+side*sway(.24f)+V{0,std::sin(creature.age*5.f-span*2.f)*locomotion*span*1.2f,0};}
            else piece.position=piece.position+side*sway(0);
        }
        pose.head=pose.head+side*sway(0);pose.core=pose.core+side*sway(.24f);
    }
    // 全部位をボスの位置へずらす
    for(int i=0;i<pose.count;++i)pose.pieces[i].position=pose.pieces[i].position+creature.offset;
    pose.head=pose.head+creature.offset;pose.core=pose.core+creature.offset;
    // 集合と下降の間は、各部位が元の位置から新しい形へ飛んで集まる
    bool reforming=inkMode_&&(creature.stage==CreatureStage::Assemble||creature.stage==CreatureStage::Descend);
    if(reforming){
        bool bird=creature.stage==CreatureStage::Assemble;
        // 変形の最初に、今の各部位の位置・回転・大きさと、行き先と向きを記録する
        if(!creature.morphActive){
            creature.morphActive=true;creature.moveVelocity={};creature.morphSource=pose;
            creature.morphSource.core=Center(r,boss_);creature.morphSource.head=creature.attention;
            for(auto e:r.view<CreaturePart,TransformComponent>()){
                const auto& part=r.get<CreaturePart>(e);const auto& t=r.get<TransformComponent>(e);
                auto& source=creature.morphSource.pieces[part.index];source.position=Read(t.translate);source.rotation=Read(t.rotate);
                source.size={t.scale.x/part.unitScale.x,t.scale.y/part.unitScale.y,t.scale.z/part.unitScale.z};
            }
            V player=Read(r.get<TransformComponent>(player_).translate);
            V radial=creature.morphSource.core-V{0,0,20};creature.flightAngle=std::atan2(radial.x,radial.z);
            if(bird)creature.morphDestination=SlimeFlightPoint(creature.flightAngle);
            else{V perch=CreatureCruisePoint(player,creature.morphSource.core,creature.cruiseStep++,true);
                creature.morphDestination={perch.x,EvaluateCreature(creature.age,0).core.y,perch.z};}
            V toward=player-creature.morphDestination;creature.morphHeading=std::atan2(-toward.x,-toward.z);
        }
        // 変形先の形 (鳥なら小さく) を行き先の向きへ回す
        auto target=EvaluateCreature(creature.age,bird?1.f:0.f);
        float scale=bird?.38f:1.f,sn=std::sin(creature.morphHeading),cs=std::cos(creature.morphHeading);
        auto destination=[&](V point){V d=(point-target.core)*scale;return creature.morphDestination+V{d.x*cs+d.z*sn,d.y,-d.x*sn+d.z*cs};};
        // 変形の進み具合で各部位を移動させ、大きさと回転を補間する
        float progress=std::clamp(creature.timer/CreatureMorphSeconds,0.f,1.f);
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];const auto& source=creature.morphSource.pieces[i];
            piece.position=ReformPart(source.position,destination(target.pieces[i].position),piece,i,progress);
            float blend=AttackEase(std::clamp((progress-.2f)/.76f,0.f,1.f));
            piece.size=Lerp(source.size,target.pieces[i].size*scale,blend);
            piece.rotation=Lerp(source.rotation,target.pieces[i].rotation+V{0,creature.morphHeading,0},blend);
            piece.rotation.z+=std::sin(progress*3.14159265f)*std::sin(i*2.4f)*3;
        }
        // 核と頭も同じように移動させる
        pose.core=ReformPart(creature.morphSource.core,creature.morphDestination,pose.pieces[pose.spineIndex[6]],0,progress);
        pose.head=ReformPart(creature.morphSource.head,destination(target.head),pose.pieces[pose.skullIndex],1,progress);
    }else creature.morphActive=false;
    // 蛇の攻撃中: 頭を攻撃の位置へ寄せ、首を波打たせる
    if(creature.stage==CreatureStage::Snake&&creature.sweepPhase>0){
        float phase=creature.sweepPhase==1?AttackEase(creature.sweepTime/WarningTime(creature.attack)):creature.sweepPhase==4?1-AttackEase(creature.sweepTime/.8f):1.f;
        if(creature.attack==CreatureAttack::HeadTail){
            phase=creature.sweepPhase==2?AttackEase(creature.sweepTime/ActiveTime(creature.attack)):
                creature.sweepPhase==5?1-AttackEase(creature.sweepTime/TailWarningTime):0.f;
        }
        V head=creature.sweepCenter+AttackHead(creature.attack,creature.sweepPhase,creature.sweepTime);
        V shift=(head-pose.head)*phase;
        // 首 (前の 12 節) を頭へ引っぱり、波を足す
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];if(piece.segment<0||piece.segment>=12)continue;
            float along=static_cast<float>(piece.segment)/12;
            float wave=std::sin(creature.sweepTime*6-along*4)*std::sin(along*3.14159f)*phase;
            piece.position=piece.position+shift*((1-along)*(1-along))+V{wave*2,0,0};}
        // 頭の部位を頭と同じだけずらす
        for(int i=0;i<pose.count;++i)if(CreatureHead(pose.pieces[i].role))pose.pieces[i].position=pose.pieces[i].position+shift;
        pose.head=pose.head+shift;
        // 頭と尾の攻撃では、尾 (17〜25 節) を薙ぎ払いの位置へ動かす
        if(creature.attack==CreatureAttack::HeadTail&&(creature.sweepPhase==3||creature.sweepPhase>=5)){
            float tailBlend=creature.sweepPhase==5?AttackEase(creature.sweepTime/TailWarningTime):
                creature.sweepPhase==6?1.f:1-AttackEase(creature.sweepTime/1.5f);
            float travel=creature.sweepPhase==6?AttackEase(creature.sweepTime/TailActiveTime):
                creature.sweepPhase==5?0.f:1.f;
            for(int segment=17;segment<26;++segment){
                float u=float(segment-17)/8.f,weight=u*u*tailBlend;
                V target=creature.sweepCenter+V{-20+42*travel-(1-u)*25,3+u*2,(1-u)*25};
                for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];if(piece.segment!=segment)continue;
                    piece.position=Lerp(piece.position,target+V{0,static_cast<float>(piece.layer+1)*.2f,0},weight);
                    piece.rotation.y+=weight*1.2f;}
            }
        }
        // 核 (弱点) を攻撃の後の反撃の位置へ寄せる
        V counter=creature.attack==CreatureAttack::Sweep?V{12,7,-1}:creature.attack==CreatureAttack::Charge?V{7,7,-10}:V{10,7,0};
        pose.core=Lerp(pose.core,creature.sweepCenter+counter,phase);
    }
    // 注目する位置は頭
    creature.attention=pose.head;
    // 旧モードの翼の攻撃: 左の翼を振り下ろし、先端を攻撃の位置へ合わせる
    if(!inkMode_&&creature.stage==CreatureStage::Opening&&creature.sweepPhase>0){
        float w=creature.sweepPhase==1?AttackEase(creature.sweepTime/1.2f):creature.sweepPhase==2?1-2*AttackEase(creature.sweepTime/ActiveTime(CreatureAttack::Wing)):-1;
        if(creature.sweepPhase>=3)w*=1-AttackEase(creature.sweepTime/3.f);
        V root=pose.pieces[pose.wingRoot[0]].position;
        float travel=creature.sweepPhase==1?0.f:creature.sweepPhase==2?AttackEase(creature.sweepTime/ActiveTime(CreatureAttack::Wing)):1.f;
        V targetTip=creature.sweepCenter+V{-13+28*travel,0,0};
        float contactBlend=creature.sweepPhase==1?AttackEase(creature.sweepTime/1.2f):creature.sweepPhase>=3?1-AttackEase(creature.sweepTime/3.f):1.f;
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];if(!CreatureWing(piece.role)||piece.side!=-1)continue;
            V d=piece.position-root;float angle=w*.65f;
            piece.position=root+V{d.x*std::cos(angle)-d.y*std::sin(angle),d.x*std::sin(angle)+d.y*std::cos(angle),d.z};
            piece.rotation.z+=angle;}
        V rotatedTip=pose.pieces[pose.wingTip[0]].position;
        V correction=(targetTip-rotatedTip)*contactBlend;
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];if(!CreatureWing(piece.role)||piece.side!=-1)continue;
            float weight=std::clamp(Length(piece.position-root)/std::max(1.f,Length(rotatedTip-root)),0.f,1.f);
            piece.position=piece.position+correction*weight;}
        creature.attention=pose.pieces[pose.wingTip[0]].position;
    }
    // 被弾の反動: 節ごとに遅れて上下に揺らす
    if(creature.recoil>0)for(int i=0;i<pose.count;++i){const auto& piece=pose.pieces[i];float lag=(piece.segment>=0?piece.segment:std::max(0,piece.span))*.016f;
        float age=.6f-creature.recoil-lag;if(age>0)pose.pieces[i].position.y+=std::sin(age*22)*std::exp(-age*8)*1.5f;}
    if(inkMode_||creature.stage==CreatureStage::Snake){
        // 実際のメッシュの断面で、胴と尾の先を床に付ける。
        // 外接球を使うと細い尾が地面から浮いてしまった
        for(int i=0;i<pose.count;++i){auto& piece=pose.pieces[i];float clearance=CreatureVerticalExtent(piece);
            piece.position.y=std::max(piece.position.y,InkBossSupport(piece.position,clearance)+clearance+.08f);}
        pose.core.y=std::max(pose.core.y,InkBossSupport(pose.core,3.f)+3.3f);
        pose.head.y=std::max(pose.head.y,InkBossSupport(pose.head,7.f)+7.3f);
        creature.attention=pose.head;
    }
    // 背骨の節から当たり判定の箱を作る (攻撃中の首は除く)
    creatureSolids_.clear();
    for(int i=0;i<CreatureSpineCount;++i){if(creature.stage==CreatureStage::Snake&&creature.sweepPhase>0&&i<12)continue;const auto& body=pose.pieces[pose.spineIndex[i]];
        V half{body.size.x*.36f,body.size.y*.36f,.65f*CreatureScale};
        creatureSolids_.push_back({body.position-half,body.position+half});}
    // 材質ごとの色 (濃い・灰・白・赤)
    const DirectX::XMFLOAT4 colors[]={{.09f,.13f,.17f,1},{.27f,.34f,.39f,1},{.93f,.95f,.88f,1},{1.8f,.07f,.025f,1}};
    // 各部位のエンティティへ大きさ・位置・回転・色・カメラの遮蔽を反映する
    for(auto e:r.view<CreaturePart,TransformComponent,MeshRendererComponent>()){
        const auto& part=r.get<CreaturePart>(e);const auto& piece=pose.pieces[part.index];auto& t=r.get<TransformComponent>(e);
        t.scale={part.unitScale.x*piece.size.x,part.unitScale.y*piece.size.y,part.unitScale.z*piece.size.z};
        t.translate=Write(piece.position);t.rotate=Write(piece.rotation);
        auto color=colors[piece.material];
        // 翼は鳥になるほど本来の色に近づける
        if(CreatureWing(piece.role)){float reveal=Ease(creature.form);
            color.x=.10f+(color.x-.10f)*reveal;
            color.y=.14f+(color.y-.14f)*reveal;
            color.z=.18f+(color.z-.18f)*reveal;}
        r.get<MeshRendererComponent>(e).color=color;
        auto& o=r.get<CameraOccluder>(e);o.center=piece.position;
        float extent=std::max({piece.size.x,piece.size.y,piece.size.z});o.size={extent,extent,extent};
    }
    // 被弾の光を反映する
    if(inkMode_)FlashBoss(r);
    // 論理上のボスと見える核は、同じ評価した位置を使う
    auto& bossTransform=r.get<TransformComponent>(boss_);bossTransform.translate=Write(pose.core);
    r.get<Target>(boss_).offset={};
    r.get<TransformComponent>(weakpoint_).translate=Write(pose.core);
    r.get<TransformComponent>(weakpoint_).scale={5.f,5.f,5.f};
    r.get<Target>(weakpoint_).offset={};
    // 弱点の核の色 (出ているときは光る)
    auto& core=r.get<MeshRendererComponent>(weakpoint_);core.enabled=true;
    core.color=r.get<Target>(weakpoint_).active?DirectX::XMFLOAT4{2,1.2f,.15f,1}:DirectX::XMFLOAT4{.12f,.55f,.65f,1};
}
// ボスの攻撃の衝撃 (粒子・画面の揺れ・土ぼこり)
void ChronoSystem::BossImpact(entt::registry& r,GameContext& ctx,V at,V direction,float power){
    // 衝撃の時刻・位置・強さを記録する
    auto& c=r.get<CreatureBoss>(boss_);c.impactAge=0;c.impactPoint=at;c.impactPower=power;
    // プレイヤーとの水平距離が近いほど画面を強く揺らす
    V player=Read(r.get<TransformComponent>(player_).translate)-at;player.y=0;
    float proximity=1.f/(1.f+Length(player)/80.f);
    ctx.camera->StartImpactShake(.19f+.06f*power,std::min(.35f,.31f*power*proximity),Write(direction),10.f+2.f*power);
    // 土ぼこりの粒を 28 個、周りへ飛ばす
    for(int i=0;i<28&&droplets_.size()<96;++i){
        float angle=i*2.399963f,speed=(11.f+(i%5)*2.5f)*power;
        V velocity{std::cos(angle)*speed,3.f+(i%4)*1.5f,std::sin(angle)*speed};
        droplets_.push_back({at+V{0,.2f,0},velocity,0,true});
    }
    // 衝撃の音
    auto* audio=Engine::Audio::GetInstance();uint32_t sound=inkMode_?slimePressureSound_:coreSound_;
    if(sound!=0xffffffff)audio->Play(sound,false,(.24f+.12f*proximity)*audio->GetMasterSEVolume(),.66f);
}
// ボスの攻撃の衝撃の輪を描く
void ChronoSystem::DrawBossImpact(GameContext& ctx,const CreatureBoss& c){
    // 0.55 秒で消える
    if(c.impactAge>=.55f)return;
    float fade=1.f-c.impactAge/.55f;
    V at=c.impactPoint+V{0,.15f,0};
    // 広がる 2 重の輪 (2 本目は少し遅れて広がる)
    for(int ring=0;ring<2;++ring){
        float age=std::max(0.f,c.impactAge-ring*.075f);
        float radius=(2.f+age*(39.f+ring*13.f))*c.impactPower;
        Engine::Vector4 color{(ring?1.2f:1.8f)*fade,(ring?.47f:.82f)*fade,.12f*fade,1};
        // 36 本の線で円を描く
        for(int i=0;i<36;++i){float a=i*6.283185f/36.f,b=(i+1)*6.283185f/36.f;
            ctx.renderer->DrawLine3D(EV(at+V{std::cos(a)*radius,0,std::sin(a)*radius}),
                EV(at+V{std::cos(b)*radius,0,std::sin(b)*radius}),color);}
    }
}
// 蛇のボスの更新: 横に回り込み、予告してから攻撃し、足場を出す
void ChronoSystem::UpdateCreature(entt::registry& r,Player& p,GameContext& ctx){
    // コンボ中は世界の時間を遅くする
    auto& c=r.get<CreatureBoss>(boss_);auto& b=r.get<Boss>(boss_);float dt=ctx.dt*p.worldScale;
    c.age+=dt;c.timer+=dt;b.clock+=dt;
    c.impactAge+=ctx.dt;
    c.recoil=std::max(0.f,c.recoil-dt);
    // 回り込む側を選んで到着し、攻撃の予告の間そこに留まる。毎フレーム計画し直すと、
    // 生き物全体が意図なくプレイヤーの周りを滑ってしまった
    bool cruising=c.stage==CreatureStage::Snake&&(c.sweepPhase==0||(inkMode_&&c.sweepPhase==4));
    // 回り込む地点を決め、そこへ向かう (一定時間か近づきすぎたら選び直す)
    if(cruising){
        c.roam+=dt;
        V player=Read(r.get<TransformComponent>(player_).translate),boss=Center(r,boss_);
        if(!c.cruiseReady||c.roam>=2.8f||(inkMode_&&c.roam>.45f&&Length(player-boss)<165.f)){
            c.cruiseGoal=CreatureCruisePoint(player,boss,c.cruiseStep++,inkMode_);
            c.cruiseReady=true;c.roam=0;
        }
        // 今の核の位置から目標までの差で速度を決めて動く
        V base=EvaluateCreature(c.age,c.form).core;
        V delta=c.cruiseGoal-V{base.x+c.offset.x,0,base.z+c.offset.z};
        c.moveVelocity=CreatureCruiseVelocity(c.moveVelocity,delta,dt,inkMode_?45.f:40.f);
        V step=c.moveVelocity*dt;
        if(Length(step)>Length(delta))step=delta;
        c.offset=c.offset+step;
    }else{c.moveVelocity={};c.cruiseReady=false;}
    // インクのモードでは、回り込み中はプレイヤーの方を向く
    if(inkMode_&&cruising){V toward=Read(r.get<TransformComponent>(player_).translate)-Center(r,boss_);
        c.heading+=std::remainder(std::atan2(-toward.x,-toward.z)-c.heading,6.283185f)*(1-std::exp(-2*dt));}
    // 段階を切り替える
    auto transition=[&](CreatureStage stage){c.stage=stage;c.timer=0;};
    // 蛇の段階: 一定時間ごとに攻撃を選び、予告 → 攻撃 → 隙 → 戻り を繰り返す
    if(c.stage==CreatureStage::Snake){c.form=0;b.phase=0;c.sweepTime+=dt;
        if(c.sweepPhase==0&&c.sweepTime>(inkMode_?3.5f:2.f)){
            // 攻撃の選び方: 4 回に 1 回は頭と尾、それ以外は順番 (後半はプレイヤーの状況で選ぶ)
            auto previous=c.attack;
            c.attack=c.attackCount%4==0?CreatureAttack::HeadTail:
                static_cast<CreatureAttack>((c.attackCount-1)%4);
            if(c.attackCount>=4&&c.attackCount%4!=0){auto at=Read(r.get<TransformComponent>(player_).translate);
                c.attack=at.y>5?CreatureAttack::Slam:Length(at-Center(r,boss_))>35?CreatureAttack::Charge:CreatureAttack::Sweep;
                if(c.attack==previous)c.attack=static_cast<CreatureAttack>((static_cast<int>(previous)+1)%3);}
            // 確認用の入力が攻撃を指定していればそれを使う
            if(auto* control=r.try_get<ControlFrame>(player_);control&&control->attackPattern>=0)c.attack=static_cast<CreatureAttack>(std::min(4,control->attackPattern));
            ++c.attackCount;
            // 攻撃の中心はプレイヤーの今の位置
            c.sweepCenter=Read(r.get<TransformComponent>(player_).translate);c.sweepCenter.y=inkMode_?InkGround(c.sweepCenter):0;
            c.sweepPhase=1;c.sweepTime=0;c.sweepHit=false;b.countered=false;
            // これらの鱗は、予告した薙ぎ払いの上を通る安定した逃げ道になる
            int index=0;for(auto e:r.view<SerpentRelay,TransformComponent>()){
                if(e==p.target||e==p.bufferedTarget||e==p.preview)continue;
                float side=c.attack==CreatureAttack::Sweep?0.f:c.attack==CreatureAttack::Charge?8.f:12.f;
                V at=c.sweepCenter+V{side,7.f+index*2.f,4.f+index*3.f};
                r.get<TransformComponent>(e).translate=Write(at);++index;
                r.get<Target>(e).active=true;r.get<MeshRendererComponent>(e).enabled=true;
            }
        // 段階 1: 予告
        }else if(c.sweepPhase==1){b.phase=1;
            if(c.sweepTime>=WarningTime(c.attack)){c.sweepPhase=2;c.sweepTime=0;}
        // 段階 2: 攻撃。プレイヤー (回避の直後は回避前の位置も) に当たったらダメージ
        }else if(c.sweepPhase==2){b.phase=2;
            float now=std::min(c.sweepTime,ActiveTime(c.attack)),before=std::max(0.f,now-dt);
            V at=Read(r.get<TransformComponent>(player_).translate)-c.sweepCenter;
            bool contact=AttackTouches(c.attack,at,before,now);
            if(inkMode_){const auto& ink=r.get<InkPlayer>(player_);
                if(ink.dodgeAge<=.12f)contact|=AttackTouches(c.attack,ink.dodgeOrigin-c.sweepCenter,before,now);}
            if(!c.sweepHit&&contact&&(inkMode_||p.invincible<=0)){
                c.sweepHit=true;p.damageReason=c.attack==CreatureAttack::Sweep?"HIT - SERPENT SWEEP":c.attack==CreatureAttack::Charge?"HIT - SERPENT CHARGE":c.attack==CreatureAttack::HeadTail?"HIT - SERPENT HEAD":"HIT - HEAD SLAM";Damage(r,p,22,ctx,c.sweepCenter);
            }
            // 叩きつけ・頭と尾・突進は、攻撃の終わりに衝撃を出す
            if(before<ActiveTime(c.attack)&&now>=ActiveTime(c.attack)&&
                (c.attack==CreatureAttack::Slam||c.attack==CreatureAttack::HeadTail||c.attack==CreatureAttack::Charge)){
                V direction=c.attack==CreatureAttack::Slam?V{.25f,1.f,.1f}:V{0,.55f,-1.f};
                BossImpact(r,ctx,c.sweepCenter,direction,c.attack==CreatureAttack::Slam?1.15f:1.f);
            }
            // 攻撃が終わったら隙 (頭と尾なら尾の予告) へ
            if(c.sweepTime>=ActiveTime(c.attack)){
                c.sweepPhase=c.attack==CreatureAttack::HeadTail?5:3;c.sweepTime=0;b.phase=3;}
        // 段階 5: 尾の予告
        }else if(c.sweepPhase==5){b.phase=1;
            if(c.sweepTime>=TailWarningTime){c.sweepPhase=6;c.sweepTime=0;c.sweepHit=false;}
        // 段階 6: 尾の薙ぎ払い
        }else if(c.sweepPhase==6){b.phase=2;
            V at=Read(r.get<TransformComponent>(player_).translate)-c.sweepCenter;
            bool contact=TailTouches(at,std::max(0.f,c.sweepTime-dt),c.sweepTime);
            if(inkMode_){const auto& ink=r.get<InkPlayer>(player_);
                if(ink.dodgeAge<=.12f)contact|=TailTouches(ink.dodgeOrigin-c.sweepCenter,std::max(0.f,c.sweepTime-dt),c.sweepTime);}
            if(!c.sweepHit&&contact){c.sweepHit=true;p.damageReason="HIT - SERPENT TAIL";Damage(r,p,18,ctx,c.sweepCenter);}
            if(c.sweepTime>=TailActiveTime){BossImpact(r,ctx,c.sweepCenter,{1,.35f,0},.68f);
                c.sweepPhase=3;c.sweepTime=0;b.phase=3;}
        // 段階 3: 攻撃の後の隙 (弱点が出る)
        }else if(c.sweepPhase==3){b.phase=3;
            if(c.sweepTime>=(inkMode_?1.5f:3.f)){c.sweepPhase=4;c.sweepTime=0;}
        // 段階 4: 戻り。蛇だけのモードでなければ鳥へ変形する
        }else if(c.sweepPhase==4){b.phase=3;
            if(c.sweepTime>=.8f){c.sweepPhase=0;c.sweepTime=0;
                if(!snakeOnly_)transition(CreatureStage::Assemble);}
        }
    // 集合: 蛇から鳥へ変形する
    }else if(c.stage==CreatureStage::Assemble){c.form=std::min(1.f,c.timer/CreatureMorphSeconds);b.phase=0;
        if(c.timer>=CreatureMorphSeconds){c.form=1;transition(CreatureStage::Volley);b.phase=1;b.clock=0;}
    // 連射: 引き寄せの道になる羽を出す
    }else if(c.stage==CreatureStage::Volley){
        b.phase=1;
        if(c.timer>1.2f){
            // 前の攻撃を消してから、新しく届く道を出す
            std::vector<entt::entity> old;for(auto e:r.view<Feather>())old.push_back(e);for(auto e:old)r.destroy(e);
            // 波の番号を進め、反撃の機会を数える
            ++b.wave;++p.stats.opportunities;b.waveBreaks=0;b.countered=false;
            V player=Read(r.get<TransformComponent>(player_).translate);
            auto route=FeatherRoute(player,Center(r,boss_));
            // 道の各点へ向かう羽を、ボスの前から出す
            for(int lane=0;lane<1;++lane)for(int i=0;i<static_cast<int>(route.size());++i){
                if(lane==1&&i%2==1)continue;
                V start=Center(r,boss_)+V{(i%2==0?-1.f:1.f)*(5+i),2,2};
                auto e=Mesh(r,"Flight feather "+std::to_string(i),"Resources/Models/Chrono/feather.obj",start,{lane==1?1.6f:2.8f,.7f,3.4f});
                auto& target=r.emplace<Target>(e);target.kind=Kind::Projectile;target.radius=lane==1?1.15f:1.65f;
                target.hp=1;target.reward=8;target.wave=b.wave;target.active=false;
                auto& q=r.emplace<Projectile>(e);q.lifetime=lane==1?8.f:44.f;
                auto& f=r.emplace<Feather>(e);f.fast=lane==1;f.start=start;f.destination=route[i];f.destination.x+=(lane==0?7.f:-4.f)*std::sin(3.14159f*(i+1)/(route.size()+1));f.delay=.18f*i/std::max(1,static_cast<int>(route.size())-1);
                r.get<MeshRendererComponent>(e).color={2.f,.2f,.04f,1};
            }
            c.wingDone=false;c.wingClock=0;transition(CreatureStage::Opening);b.phase=2;
        }
    // 扇状の羽 (旧モード): プレイヤーが高い所にいれば翼で薙ぎ払う
    }else if(c.stage==CreatureStage::Opening){
        b.phase=2;
        // チェインのスローで、道を使い切るまで空からの脅威を先延ばしにしてはいけない
        c.wingClock+=ctx.dt;
        if(c.sweepPhase==0&&c.wingClock>(c.wingDone?2.5f:.8f)&&Read(r.get<TransformComponent>(player_).translate).y>10){c.attack=CreatureAttack::Wing;c.sweepPhase=1;c.sweepTime=0;c.sweepHit=false;c.wingCounter=false;
            c.sweepCenter=Read(r.get<TransformComponent>(player_).translate);}
        if(c.sweepPhase>0){c.sweepTime+=ctx.dt;
            if(c.sweepPhase==1&&c.sweepTime>=1.2f){c.sweepPhase=2;c.sweepTime=0;}
            else if(c.sweepPhase==2){V at=Read(r.get<TransformComponent>(player_).translate)-c.sweepCenter;
                if(!c.sweepHit&&p.invincible<=0&&AttackTouches(CreatureAttack::Wing,at,std::max(0.f,c.sweepTime-ctx.dt),c.sweepTime)){
                    c.sweepHit=true;p.damageReason="HIT - WING SWEEP";Damage(r,p,22,ctx,c.sweepCenter);}
                if(c.sweepTime>=ActiveTime(CreatureAttack::Wing)){c.sweepPhase=3;c.sweepTime=0;c.wingCounter=!c.sweepHit;c.wingDone=true;}}
            else if(c.sweepPhase==3&&c.sweepTime>=3){c.sweepPhase=0;c.sweepTime=0;c.wingClock=0;c.wingCounter=false;}
        }
        // 反撃されたか時間切れで下降へ
        if(b.countered||c.timer>40){c.sweepPhase=0;c.wingCounter=false;c.wingClock=0;transition(CreatureStage::Descend);b.phase=3;}
    // 下降: 鳥から蛇へ戻る
    }else{
        b.phase=3;c.form=1-std::min(1.f,c.timer/CreatureMorphSeconds);
        if(c.timer>=CreatureMorphSeconds){c.form=0;transition(CreatureStage::Snake);}
    }
    // 弱点が出る条件 (翼の後で羽を 3 つ壊したか、蛇の攻撃を避けた後の隙)
    r.get<Target>(weakpoint_).active=!b.countered&&((c.stage==CreatureStage::Opening&&c.sweepPhase!=1&&c.sweepPhase!=2&&((b.waveBreaks>=3&&c.wingDone)||c.wingCounter))||
        (c.stage==CreatureStage::Snake&&c.sweepPhase==3&&!c.sweepHit));
    // 姿勢を反映し、使っていない中継の羽を隠す
    PoseCreature(r);
    for(auto e:r.view<SerpentRelay,TransformComponent>()){
        bool live=c.stage==CreatureStage::Snake&&c.sweepPhase>=1&&c.sweepPhase!=4;
        if(!live&&e!=p.target){r.get<Target>(e).active=false;r.get<MeshRendererComponent>(e).enabled=false;}
    }
}
// アリーナのステージを作る
void ChronoSystem::BuildArena(entt::registry& r){
    // 前回の「Chrono 」のエンティティを消す (弱点とボスは残す)
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(e!=weakpoint_&&e!=boss_&&
        r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    // 床・4 方向の壁・柱・段
    Block(r,"Arena floor",{0,-2,0},{64,4,64});
    Block(r,"Arena north",{0,3,32},{64,6,2});Block(r,"Arena south",{0,3,-32},{64,6,2});
    Block(r,"Arena west",{-32,3,0},{2,6,64});Block(r,"Arena east",{32,3,0},{2,6,64});
    Block(r,"Arena pillar",{18,3,5},{3,6,3});Block(r,"Arena step",{-18,.25f,5},{5,.5f,5});
    // 跳ねる敵 3 体
    const V enemies[]={{-7,1.4f,-4},{7,1.4f,-2},{0,1.4f,2}};
    for(int i=0;i<3;++i){auto e=Mesh(r,"Arena hopper "+std::to_string(i),horn,enemies[i],{2.4f,2.8f,2.4f});
        auto& tg=r.emplace<Target>(e);tg.radius=1.4f;tg.hp=42;tg.reward=18;
        tg.offset=enemies[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Hopper>(e).timer=i*.3f;r.emplace<TagComponent>(e).tag=TagType::Enemy;}
    // 繰り返し使える足場 4 つ
    const V anchors[]={{-12,3,-9},{12,3,-9},{-12,3,13},{12,3,13}};
    for(int i=0;i<4;++i){auto e=Mesh(r,"Arena anchor "+std::to_string(i),ball,anchors[i],{1.4f,1.8f,1.4f});
        auto& tg=r.emplace<Target>(e);tg.kind=Kind::Anchor;tg.radius=1.1f;tg.reward=0;
        tg.offset=anchors[i]-Read(r.get<TransformComponent>(e).translate);
        r.emplace<Anchor>(e).reusable=true;r.get<MeshRendererComponent>(e).color={.15f,1.2f,.7f,1};}
    // ボスを奥へ置き、戦闘中にする
    if(r.valid(boss_)){
        V shift=V{0,5,10}-Center(r,boss_);auto& t=r.get<TransformComponent>(boss_);t.translate=Write(Read(t.translate)+shift);
        auto& b=r.get<Boss>(boss_);b.engaged=true;
        r.get<Target>(weakpoint_).offset=Center(r,boss_)-Read(r.get<TransformComponent>(weakpoint_).translate);
    }
    // プレイヤーを手前に置き、自動の狙いにする
    r.get<TransformComponent>(player_).translate={0,1.3f,-12};
    auto& p=r.get<Player>(player_);p.recoveryPoint={0,1.3f,-12};p.automatic=true;
    pitch_=.28f;
}
// PLAY 開始時の初期化: モードを決め、前回のエンティティを消し、プレイヤー・音・ステージ・記録を用意する
void ChronoSystem::Reset(entt::registry& r){
    // 旧来の確認用のテストも動かせるようにする。通常のプレイは常に新しいシューターを使う
    // コマンドラインの引数で、インクのモードとスライムの生活モードを決める
    inkMode_=!wcsstr(GetCommandLineW(),L"--chrono-smoke")&&!wcsstr(GetCommandLineW(),L"--creature-smoke")&&!wcsstr(GetCommandLineW(),L"--snake-smoke");
    slimeLife_=inkMode_&&(!wcsstr(GetCommandLineW(),L"-smoke")||wcsstr(GetCommandLineW(),L"--slime-life-smoke"))&&!wcsstr(GetCommandLineW(),L"--legacy-domain");
    // スライムの変形のターゲットを消す
    Engine::Renderer::GetInstance()->SetFluidMorphTargets({},0);
    if(inkMode_){creature_=true;arena_=false;inkShots_.clear();inkSurfaces_.clear();inkMeshes_.clear();inkDirty_=true;}
    // 前回作った「Chrono 」のエンティティを消す
    std::vector<entt::entity> remove;
    for(auto e:r.view<NameComponent>())if(r.get<NameComponent>(e).name.rfind("Chrono ",0)==0)remove.push_back(e);
    for(auto e:remove)r.destroy(e);
    // 名前でプレイヤーとボスを探す
    player_=boss_=entt::null;
    for(auto e:r.view<NameComponent>()){
        const auto& n=r.get<NameComponent>(e).name;if(n=="Player")player_=e;if(n=="Boss")boss_=e;
    }
    // 撃破の演出・捕獲・終了・カメラ・ムービーの状態を初期化する
    bossDefeat_=defeatBurst_=false;defeatAge_=bossFlash_=0;bossDebris_.clear();
    candidateCapture_=captureShown_=0;captureScanClock_=0;captureAge_=10;
    initialized_=r.valid(player_);finished_=won_=false;finishAge_=0;sparks_.clear();creatureSolids_.clear();
    prevL_=Down(VK_LBUTTON);prevM_=Down(VK_MBUTTON);prevSpace_=Controls::Jump();prevShift_=Down(VK_SHIFT);
    yaw_=0;pitch_=0.12f;zoom_=17;shoulder_=1.8f;chainCameraHold_=0;fov_=1.0472f;
    cameraReady_=false;zoomVelocity_=0;cameraBoom_=-1;composition_={};viewPitch_=pitch_;postStrength_=0;droplets_.clear();manualCameraHold_=0;
    openingPending_=true;cinematicActive_=false;cinematicBoss_=false;cinematicAge_=0;
    // プレイヤーがいなければここまで
    if(!initialized_)return;
    // 戦闘の BGM を流す
    Music::Play(Music::Battle);
    // ボスの生き物のコンポーネントを外し、元のモデルを表示する
    if(r.valid(boss_)){r.remove<CreatureBoss>(boss_);r.get<MeshRendererComponent>(boss_).enabled=true;}
    // プレイヤーの状態を作り直し、位置・体力・入力を初期化する
    r.emplace_or_replace<Player>(player_);
    auto& pt=r.get<TransformComponent>(player_);pt.translate={0,1.3f,-27};pt.scale={1,1,1};
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(100);hp.SetMaxHp(200);hp.SetDead(false);
    auto& pi=r.get<PlayerInputComponent>(player_);pi.selectedCan=CanType::None;pi.lockedEnemy=entt::null;pi.isRadialMenuOpen=false;
    if(auto* box=r.try_get<BoxColliderComponent>(player_))box->enabled=false;
    // 白テクスチャ・球のメッシュ・音を読み込む
    auto* renderer=Engine::Renderer::GetInstance();white_=renderer->LoadTexture2D("Resources/Textures/white1x1.png");
    sphere_=renderer->LoadObjMesh(ball);
    if(hitSound_==0xffffffff)hitSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/suittikirikae.mp3");
    if(coreSound_==0xffffffff)coreSound_=Engine::Audio::GetInstance()->Load("Resources/Sound/kettei.mp3");
    // ポストエフェクトを既定に戻してから、ステージを作る
    renderer->SetPostEffect("Default");renderer->SetPostProcessParams({});
    Build(r);CacheSolids(r);
    if(inkMode_)renderer->SetPostEffect("ChronoFocus");
    // 最高記録を読み込む
    bestTime_=0;bestChain_=0;newTime_=newChain_=false;
    try{std::ifstream file(RecordPath());if(file){nlohmann::json j;file>>j;bestTime_=j.value("time",0.0f);bestChain_=j.value("chain",0);}}catch(...){}
    renderer->SetAmbientColor({0.65f,0.68f,0.75f});
}
// 当たり判定に使う箱の一覧を作る
void ChronoSystem::CacheSolids(entt::registry& r){
    solids_.clear();
    // 固い物の箱を ワールド座標の AABB として集める
    for(auto e:r.view<Solid,BoxColliderComponent,TransformComponent>()){
        const auto& b=r.get<BoxColliderComponent>(e);const auto& t=r.get<TransformComponent>(e);
        V c=Read(t.translate)+Read(b.center),h=Read(b.size)*0.5f;solids_.push_back({c-h,c+h});
    }
}
// 箱をずらしながら動かしたとき、最初に当たる箱を求める
bool ChronoSystem::Sweep(V p,V d,V half,float& fraction,V& normal)const{
    // 地形の箱と生き物の箱の中で一番早く当たるものを選ぶ
    bool found=false;fraction=1;for(const auto& box:solids_){float f;V n;
        if(Chrono::Sweep(p,d,box,half,f,n)&&f<fraction){fraction=f;normal=n;found=true;}}
    for(const auto& box:creatureSolids_){float f;V n;
        if(Chrono::Sweep(p,d,box,half,f,n)&&f<fraction){fraction=f;normal=n;found=true;}}
    return found;
}
// 2 点の間が空いているか
bool ChronoSystem::Clear(V a,V b,float radius)const{float f;V n;return !Sweep(a,b-a,{radius,radius,radius},f,n);}
// 壁に沿って動かす (接地も判定する)
ChronoSystem::V ChronoSystem::Move(V p,V d,V half,bool& grounded,V* velocity)const{
    grounded=false;
    // 最大 3 回、当たった壁に沿って残りの移動を滑らせる
    for(int i=0;i<3 && Length(d)>1e-5f;++i){float f;V n;
        if(!Sweep(p,d,half,f,n)){p=p+d;break;}
        p=p+d*std::max(0.0f,f-0.001f);if(n.y>0.5f)grounded=true;
        if(Length(n)<0.5f)break;
        d=d*(1-f);d=d-n*Dot(d,n);
        if(velocity)*velocity=*velocity-n*std::min(0.0f,Dot(*velocity,n));
    }
    // 少し下に何かあれば接地とする
    float f;V n;if(Sweep(p,{0,-0.06f,0},half,f,n)&&n.y>0.5f)grounded=true;
    return p;
}
// エンティティの中心
ChronoSystem::V ChronoSystem::Center(entt::registry& r,entt::entity e)const{
    return Read(r.get<TransformComponent>(e).translate)+r.get<Target>(e).offset;
}
// 鎖で狙う対象を選ぶ
entt::entity ChronoSystem::Choose(entt::registry& r,Player& p,GameContext& ctx,bool manual){
    // プレイヤーの位置・前方向・カメラの位置
    V origin=Read(r.get<TransformComponent>(player_).translate),forward=Forward(yaw_,pitch_);
    V cam=Read(ctx.camera->Position());float score=-1e9f,nearestRay=1e9f;entt::entity best=entt::null;
    // 有効な対象 (ボス本体以外) を調べる
    for(auto e:r.view<Target,TransformComponent>()){
        const auto& t=r.get<Target>(e);if(!t.active || t.kind==Kind::Boss)continue;
        V c=Center(r,e),delta=c-origin;float distance=Length(delta);
        // 届かない・遮られる対象はとばす
        if(distance>Reach(p.mass)+t.radius || !Reachable(r,p,e,origin))continue;
        // 手動の狙い: 照準のレイに当たる一番近い対象
        if(manual){float hit;
            if(RaySphere(cam,forward,c,t.radius,100,hit)&&hit<nearestRay){nearestRay=hit;best=e;}
        }else{
            // 前方の半球に限り、思わぬ後ろや地面への吸い付きを防ぐ
            float alignment=Dot(Unit(c-cam),forward);if(alignment<0.55f)continue;
            // 自動の狙い: 正面に近く、近く、移動の向きに合う対象ほど高い点
            V motion=Unit(p.velocity);float s=alignment*35-distance*0.06f+Dot(Unit(delta),motion)*0.6f;
            if(t.kind==Kind::Anchor)s-=1.2f;if(t.kind==Kind::Weakpoint)s+=6;
            if(e==p.preview)s+=2.0f; // ヒステリシス: 近くの跳ねる敵に狙いを横取りされないようにする
            if(s>score){score=s;best=e;}
        }
    }
    return best;
}
// 対象へ届くか
bool ChronoSystem::Reachable(entt::registry& r,const Player& p,entt::entity e,V origin,V* lift) const{
    // 体の大きさに合わせた半径で、対象の手前 (足場なら上) まで動けるか調べる
    const auto& tg=r.get<Target>(e);V c=Center(r,e);
    float radius=std::max(.55f,p.collisionScale*.9f);
    V end=tg.kind==Kind::Anchor?c+V{0,1.7f,0}:c-Unit(c-origin)*(tg.radius+radius+.15f);
    float f;V n;V half{radius,.8f+p.collisionScale*.4f,radius};
    if(lift)*lift=origin;
    if(!Clear(origin,c,.15f))return false;
    if(!Sweep(origin,end-origin,half,f,n))return true;
    // 地上にいれば、少し持ち上げてから動けるかも調べる
    V raised=origin+V{0,.56f,0};
    if(p.grounded&&!Sweep(origin,raised-origin,half,f,n)&&!Sweep(raised,end-raised,half,f,n)){
        if(lift)*lift=raised;return true;
    }
    return false;
}
// 鎖を撃つ
void ChronoSystem::Shoot(entt::registry& r,Player& p,GameContext& ctx){
    // 先行入力の対象を撃つ対象にする
    V origin=Read(r.get<TransformComponent>(player_).translate);p.shotManual=p.bufferedManual;p.shotTimed=p.timedInput;
    p.target=p.bufferedTarget;p.bufferedTarget=entt::null;p.buffer=0;
    // 撃つ時点で届かなくなっていたら失敗にする
    if(r.valid(p.target)&&(!r.get<Target>(p.target).active||!Reachable(r,p,p.target,origin)||
        Length(Center(r,p.target)-origin)>Reach(p.mass)+r.get<Target>(p.target).radius)){
        p.target=entt::null;p.failure=ChainFailure::Obstructed;p.failureTime=.6f;return;
    }
    p.failure=r.valid(p.target)?ChainFailure::None:Failure(r,p,ctx);
    p.failureTime=p.failure==ChainFailure::None?0:0.8f;
    if(!p.shotManual&&p.automatic&&!r.valid(p.target))return;
    // 鎖の始点・手の位置・届く距離
    p.shotStart=origin;p.hand=origin;p.shotDistance=0;p.shotLimit=Reach(p.mass);
    // 手動なら照準の先 (壁があれば壁まで) へ、対象があればその中心へ
    if(p.shotManual){
        V cam=Read(ctx.camera->Position()),dir=Forward(yaw_,pitch_);
        V aim=cam+dir*100;float wall;V n;if(Sweep(cam,dir*100,{},wall,n))aim=cam+dir*(100*wall);
        if(r.valid(p.target))aim=Center(r,p.target);
        p.shotDirection=Unit(aim-origin);
    }else if(r.valid(p.target))p.shotDirection=Unit(Center(r,p.target)-origin);
    // 対象が無ければ前へ短く伸ばす
    else {p.shotDirection=Forward(yaw_,0);p.shotLimit=3.3f;}
    // 鎖を伸ばし始める
    p.action=Action::Extending;p.timer=0;p.buffer=0;
}
// プレイヤーへのダメージ (完璧な回避の判定を含む)
void ChronoSystem::Damage(entt::registry& r,Player& p,float amount,GameContext& ctx,V source,float perfectWindow){
    // スライムの生活モードなら専用の被弾へ
    if(slimeLife_){
        DamageSlimeLife(r,p,amount,ctx,source);return;
    }
    // インクのモード: 完璧な回避の時間内なら避けて、時間を遅くする
    if(inkMode_&&!finished_){auto& ink=r.get<InkPlayer>(player_);
        if(ink.TryPerfectDodge(perfectWindow)){PerfectRogueLoop(r,p,ctx);p.invincible=std::max(p.invincible,SlimePerfectInvincibility);p.worldScale=.15f;
            ctx.camera->StartImpactShake(.10f,.08f,{0,1,0},12);return;}}
    // 無敵中・終了後は受けない
    if(p.invincible>0||finished_)return;
    // インクのモードでは量に合わせてダメージを増やし、描いている線を短くする
    if(inkMode_)amount*=SlimeMaximumMass/100.f*r.get<InkPlayer>(player_).rogue.Mitigation();
    if(inkMode_)r.get<InkPlayer>(player_).domainPath.Damage();
    // 量を減らし、無敵時間を付け、チェインを切る
    p.chainDrive=1;p.lastChainHit=10;p.mass=std::max(0.0f,p.mass-amount);p.invincible=0.65f;++p.stats.hitsTaken;p.flow.Break();
    p.damageSource=source;p.damageAge=0;
    p.buffer=0;p.bufferedTarget=entt::null;
    // 鎖の途中なら回復の姿勢へ
    if(p.action!=Action::Collapsed){p.action=Action::Recovery;p.timer=0.12f;p.target=entt::null;}
    ctx.camera->StartImpactShake(.16f,.18f,Write(Unit(Read(r.get<TransformComponent>(player_).translate)-source)),10);
    r.get<HealthComponent>(player_).SetHp(p.mass);
    if(p.mass<=0)Finish(p,false);
}
// チェインが途切れた理由
ChainFailure ChronoSystem::Failure(entt::registry& r,Player& p,GameContext& ctx){
    // 照準の正面の対象から、届かない理由を探す
    V origin=Read(r.get<TransformComponent>(player_).translate),cam=Read(ctx.camera->Position());
    V forward=Forward(yaw_,pitch_);float best=-1;ChainFailure reason=ChainFailure::NoTarget;
    for(auto e:r.view<Target,TransformComponent>()){
        const auto& tg=r.get<Target>(e);if(!tg.active||tg.kind==Kind::Boss)continue;
        V c=Center(r,e);float alignment=Dot(Unit(c-cam),forward),hit;
        if(p.aiming&&!RaySphere(cam,forward,c,tg.radius,100,hit))continue;
        if(alignment<0.55f||alignment<=best)continue;best=alignment;
        reason=Length(c-origin)>Reach(p.mass)+tg.radius?ChainFailure::OutOfRange:
            !Reachable(r,p,e,origin)?ChainFailure::Obstructed:ChainFailure::None;
    }
    return reason;
}
// 命中や失敗の反応 (粒子と音)
void ChronoSystem::Feedback(Player& p,V at,bool heavy){
    // ヒットストップを入れ、水滴を飛ばす
    p.hitStop=std::max(p.hitStop,heavy?0.08f:0.025f);
    for(int i=0;i<(heavy?18:10)&&droplets_.size()<96;++i){
        float angle=i*2.39996f;float speed=heavy?8.0f:5.0f;
        droplets_.push_back({at,{std::cos(angle)*speed,2.0f+(i%4)*1.2f,std::sin(angle)*speed},0});
    }
    // コンボが続くほど高い音にする
    auto* audio=Engine::Audio::GetInstance();
    audio->Play(heavy?coreSound_:hitSound_,false,0.3f*audio->GetMasterSEVolume(),
        std::min(1.8f,1.0f+0.065f*p.flow.combo));
}
// 鎖が対象に届いたとき (引き寄せ・攻撃・つかみ)
void ChronoSystem::Impact(entt::registry& r,Player& p,entt::entity e,GameContext& ctx,bool grabbed){
    if(!r.valid(e)||!r.all_of<Target>(e))return;
    auto& t=r.get<Target>(e);if(!t.active)return;
    // つかんで届いたらチェインの勢いを強める
    if(grabbed&&t.kind!=Kind::Boss){p.chainDrive=(p.shotTimed||p.automatic)?std::min(1.5f,p.chainDrive+.15f):std::min(1.15f,p.chainDrive+.05f);p.lastChainHit=0;}
    if(t.kind==Kind::Anchor){
        // 到着したときだけ消費する。中断や遮られた引き寄せはやり直せる
        if(grabbed){
            t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
            if(auto* a=r.try_get<Anchor>(e))a->cooldown=1.2f; // 1 回だけの中継の使用も記録する。再出現するのは繰り返し使える足場だけ
            if(sparks_.size()<64)sparks_.push_back({Center(r,e),Read(r.get<TransformComponent>(player_).translate),0});
        }
        return;
    }
    // 弱点: 隙のときだけ反撃が通り、ボスに大ダメージ
    if(t.kind==Kind::Weakpoint){
        auto& b=r.get<Boss>(boss_);
        bool snakeOpening=creature_&&r.get<CreatureBoss>(boss_).stage==CreatureStage::Snake&&
            r.get<CreatureBoss>(boss_).sweepPhase==3&&!r.get<CreatureBoss>(boss_).sweepHit;
        bool wingOpening=creature_&&r.get<CreatureBoss>(boss_).wingCounter;
        if(b.countered||(!snakeOpening&&!wingOpening&&b.waveBreaks<3))return;
        b.countered=true;t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
        auto& bt=r.get<Target>(boss_);bt.hp-=120;++p.stats.counters;
        p.instability=std::max(0.0f,p.instability-55);p.mass=std::min(200.0f,p.mass+25);
        p.flow.grace=2;ctx.camera->StartImpactShake(.20f,p.aiming?.06f:.22f,Write(Unit(Read(r.get<TransformComponent>(player_).translate)-Center(r,e))),11);
        if(creature_)r.get<CreatureBoss>(boss_).recoil=.6f;
        Feedback(p,Center(r,e),true);
        b.phase=3;b.clock=0;
        if(bt.hp<=0)Finish(p,true);
        return;
    }
    if(creature_&&t.kind==Kind::Boss)return; // 露出した核だけがダメージを受ける
    // 通常の命中: つかんだときと体の量で強さが変わる
    const float damage=(grabbed?46.0f:24.0f)*(0.8f+p.mass*0.002f);
    t.hp-=t.kind==Kind::Boss ? damage*0.18f : damage;
    if(t.kind==Kind::Boss){if(t.hp<=0)Finish(p,true);return;}
    // 倒したかどうかをコンボに記録する
    bool defeated=t.hp<=0;p.flow.Hit(defeated);
    Feedback(p,Center(r,e),false);
    ctx.camera->StartImpactShake(.07f,p.aiming?.015f:.045f,Write(Unit(Read(r.get<TransformComponent>(player_).translate)-Center(r,e))),18);
    if(!defeated)return;
    // 倒したら隠し、量を吸収する
    V c=Center(r,e);t.active=false;r.get<MeshRendererComponent>(e).enabled=false;
    p.mass=std::min(200.0f,p.mass+t.reward);p.stats.absorbed+=t.reward;
    if(!p.grounded){++p.stats.airKills;p.stats.maxAirKills=std::max(p.stats.maxAirKills,p.stats.airKills);}
    if(creature_&&r.all_of<Feather>(e))p.instability=std::max(0.f,p.instability-12.f);
    // 羽を壊したら、その波の壊した数を数える
    if(t.kind==Kind::Projectile){++p.stats.projectiles;auto& b=r.get<Boss>(boss_);if(t.wave==b.wave)++b.waveBreaks;}
    if(sparks_.size()<64)sparks_.push_back({c,Read(r.get<TransformComponent>(player_).translate),0});
}

// 旧モードのカメラ (肩越し・ロックオン・床への持ち上げ)
void ChronoSystem::Camera(entt::registry& r,Player& p,GameContext& ctx,float dt){
    // 生き物のボス戦では手持ちの揺れを付ける (狙っている間は付けない)
    ctx.camera->SetHandheld(creature_&&!p.aiming?.012f:0.f);
    // 手動でカメラを動かした直後は自動の補助を止める
    manualCameraHold_=std::max(0.0f,manualCameraHold_-dt);
    bool combat=r.valid(boss_)&&r.get<Boss>(boss_).engaged;
    bool assist=combat&&!p.aiming&&manualCameraHold_<=0;
    // 戦闘中・狙っていない・手動の操作が無いときに自動の補助を強める
    cameraAssist_+=(static_cast<float>(assist)-cameraAssist_)*(1-std::exp(-3*dt));
    // 長い鎖で移動している間は少しの間広い構図にする
    bool chaining=p.shotLimit>4 && (p.action==Action::Extending||p.action==Action::Pulling||p.action==Action::Retracting);
    if(chaining)chainCameraHold_=0.35f;else chainCameraHold_=std::max(0.0f,chainCameraHold_-dt);
    bool wide=chainCameraHold_>0;
    // 体の大きさに合わせてカメラの距離を決める
    float scale=std::max(1.0f,BodyScale(p.mass));
    float desiredZoom=(p.aiming?(wide?16.0f:12.0f):(creature_?21.0f:combat?25.0f:wide?22.0f:19.0f))*scale;
    if(creature_&&!p.aiming){auto& c=r.get<CreatureBoss>(boss_);
        if(c.sweepPhase==0&&!chaining)desiredZoom+=3.f*scale;}

    // 肩越しのずれ (狙っている間は大きく)
    float desiredShoulder=(p.aiming?4.6f:(wide?4.2f:1.8f))*scale;
    float blend=1-std::exp(-9*dt);
    // 距離は速度を持たせてなめらかに変える
    float wanted=std::clamp((desiredZoom-zoom_)*5.f,-12.f,12.f);
    zoomVelocity_+=std::clamp(wanted-zoomVelocity_,-32.f*dt,32.f*dt);
    zoom_+=zoomVelocity_*dt;shoulder_+=(desiredShoulder-shoulder_)*blend;
    // 追従の位置を更新する (大きく離れたらすぐ合わせる)
    V pos=Read(r.get<TransformComponent>(player_).translate);
    if(!cameraReady_||Length(pos-cameraFollow_)>300){cameraFollow_=pos;cameraReady_=true;composition_={};}
    if(dt>0){float oldY=cameraFollow_.y;cameraFollow_=Follow(cameraFollow_,pos,dt,2.2f*scale);
        if(!p.aiming)cameraFollow_.y=std::clamp(oldY+(pos.y-oldY)*(1-std::exp(-4*dt)),pos.y-5*scale,pos.y+5*scale);}
    // 戦闘中はボス (攻撃中は注目する位置) の方へ構図をずらし、自動でボスの方を向く
    V aimOffset{};
    if(combat&&!p.aiming){
        V interest=Center(r,boss_);
        if(creature_){const auto& c=r.get<CreatureBoss>(boss_);if(c.sweepPhase==1||c.sweepPhase==2)interest=c.attention;}
        V delta=interest-(pos+V{0,2.8f*scale,0});
        aimOffset=Unit(delta)*std::min(creature_?3.0f:6.0f,Length(delta)*.30f);
        if(creature_)aimOffset.y=std::clamp(aimOffset.y,-1.f,1.f);
        if(assist&&Length(V{delta.x,0,delta.z})>4){
            float error=std::remainder(std::atan2(delta.x,delta.z)-yaw_,6.283185f);
            yaw_+=std::clamp(error,-.8f,.8f)*(1-std::exp(-2*dt))*cameraAssist_;
        }
    }
    // 鎖の移動中は対象の方へ構図をずらす
    if(chaining&&!p.aiming&&r.valid(p.target))aimOffset=Lerp(aimOffset,Unit(Center(r,p.target)-pos)*3.f,.45f);
    composition_=Lerp(composition_,aimOffset,1-std::exp(-7*dt));
    V focus=cameraFollow_+V{0,2.8f*scale,0}+composition_;
    // 自動の補助ではピッチも目標へ近づける
    float goalPitch=pitch_;
    if(assist){float desiredPitch=creature_?-.12f:.28f;
        if(creature_){auto& c=r.get<CreatureBoss>(boss_);V d=c.attention-pos;
            if(c.sweepPhase==1||c.sweepPhase==2)desiredPitch=std::clamp(-std::atan2(d.y,std::max(10.f,Length(V{d.x,0,d.z})))*.45f,p.grounded?.10f:-.30f,.18f);}
        goalPitch=pitch_+(desiredPitch-pitch_)*cameraAssist_;}
    // 手動の狙いは照準の真下のまま。自動の構図は
    // プレイヤーの選ぶ向きを変えず、対象とカメラが影響し合うのを防ぐ
    viewPitch_=p.aiming?pitch_:viewPitch_+(goalPitch-viewPitch_)*(1-std::exp(-9*dt));
    // カメラの右方向・回り込みの向き・置きたい位置
    V right{std::cos(yaw_),0,-std::sin(yaw_)};
    V orbit=Forward(yaw_,viewPitch_);
    V desired=focus-orbit*zoom_+right*shoulder_;
    // 地形の箱だけでカメラの当たりを調べる関数
    auto cameraSweep=[&](V at,V delta,V half,float& fraction,V& normal){
        bool found=false;fraction=1;
        for(const auto& box:solids_){float f;V n;
            if(Chrono::Sweep(at,delta,box,half,f,n)&&f<fraction){found=true;fraction=f;normal=n;}}
        return found;
    };
    // 空中では回り込みをそのまま保つ。地面の高さでは、ブームを縮めて
    // プレイヤーの体に入るのではなく、カメラを床の上へ持ち上げる
    float f;V n;
    if(desired.y<focus.y){
        V above{desired.x,focus.y,desired.z};float drop=focus.y-desired.y;
        if(cameraSweep(above,{0,-drop,0},{0.35f,0.35f,0.35f},f,n)&&n.y>0.5f)desired.y=above.y-drop*f+0.05f;
    }
    // 壁があればその手前まで寄せる
    if(cameraSweep(focus,desired-focus,{0.35f,0.35f,0.35f},f,n))desired=focus+(desired-focus)*std::max(0.0f,f-0.02f);
    // 近すぎるときは後ろの上へ持ち上げた位置を試す
    if(Length(desired-pos)<5.0f*scale){
        V raised=focus-Forward(yaw_,0)*(8.0f*scale)+right*shoulder_+V{0,3*scale,0};
        if(!cameraSweep(focus,raised-focus,{0.35f,0.35f,0.35f},f,n))desired=raised;
    }
    if(creature_)desired.y=std::max(1.5f,desired.y);
    // ブームの長さは縮めるのはすぐ、伸ばすのはゆっくり
    float safeBoom=Length(desired-focus);
    cameraBoom_=cameraBoom_<0?safeBoom:std::min(safeBoom,cameraBoom_+18.f*dt);
    desired=focus+Unit(desired-focus)*cameraBoom_;
    // 床で止めた上向きの回り込みは、体と腕の根元のために縦の余裕が要る。
    // 照準のレイは変えずに、画角を広げる
    // 体がちょうど収まる画角を求める
    V up{std::sin(yaw_)*std::sin(viewPitch_),std::cos(viewPitch_),std::cos(yaw_)*std::sin(viewPitch_)};
    V body=pos+V{0,0.7f*scale,0}-desired;
    float depth=std::max(0.1f,Dot(body,orbit));
    float bodyAngle=std::abs(std::atan2(Dot(body,up),depth));
    float required=2*(bodyAngle+std::atan2(1.8f*scale,Length(body)));
    // 鎖の移動中は対象も収まる画角にする
    if(chaining&&r.valid(p.target)){
        V next=Center(r,p.target)-desired;float z=std::max(0.1f,Dot(next,orbit));
        float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.f/9;
        float halfAngle=std::max(std::abs(std::atan2(Dot(next,up),z)),
            std::atan(std::abs(Dot(next,right))/z/aspect));
        required=std::max(required,2*(halfAngle+0.13f));
    }
    // 旧モードの戦闘中はボスも収まる画角にする
    if(combat&&!p.aiming&&!creature_){
        V boss=Center(r,boss_)-desired;float z=Dot(boss,orbit);
        if(z>1){float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.f/9;
            required=std::max(required,2*(std::max(std::abs(std::atan2(Dot(boss,up),z)),
                std::atan(std::abs(Dot(boss,right))/z/aspect))+std::atan2(creature_?18.0f:6.0f,Length(boss))));}

    }
    // 生き物のボス戦では画角を固定し、チェインの勢いで広げる
    if(creature_)required=1.13446f+(p.chainDrive-1)*.349066f;
    float desiredFov=std::clamp(required,creature_?1.13446f:wide?1.2217f:1.0472f,creature_?1.39626f:1.7453f);
    fov_+=(desiredFov-fov_)*(1-std::exp(-(desiredFov>fov_?18.0f:4.0f)*dt));
    // 射影・回転・位置をカメラへ反映し、近いほどプレイヤーを半透明にする
    float aspect=ctx.viewportSize.y>0?ctx.viewportSize.x/ctx.viewportSize.y:16.0f/9.0f;
    ctx.camera->SetProjection(fov_,aspect,0.1f,10000.0f);
    ctx.camera->SetRotation(viewPitch_,yaw_,0);ctx.camera->SetPosition(Write(desired));
    p.cameraOpacity=std::clamp((Length(desired-pos)-2.5f*scale)/(4.0f*scale),0.18f,1.0f);
    // 跳ねる敵もカメラを遮る物として登録する
    for(auto e:r.view<Hopper,Target,TransformComponent>()){
        auto& o=r.get_or_emplace<CameraOccluder>(e);o.center=Center(r,e);o.size={2.4f,2.8f,2.4f};
    }
    // 体やボスを隠している遮蔽物を半透明にする
    if(dt>0)for(auto e:r.view<CameraOccluder>()){
        auto& o=r.get<CameraOccluder>(e);float fraction;V normal;
        bool hidesBody=Chrono::Sweep(desired,pos+V{0,scale,0}-desired,
            {o.center-o.size*0.5f,o.center+o.size*0.5f},{0.3f,0.3f,0.3f},fraction,normal);
        bool hidesBoss=combat&&e!=boss_&&!r.all_of<CreaturePart>(e)&&Chrono::Sweep(desired,Center(r,boss_)-desired,
            {o.center-o.size*.5f,o.center+o.size*.5f},{.5f,.5f,.5f},fraction,normal);
        bool close=r.all_of<Hopper>(e)&&Length(o.center-desired)<Length(o.size)*.6f+2;
        o.opacity+=((hidesBody||hidesBoss||close?0.18f:1.0f)-o.opacity)*(1-std::exp(-12*dt));
    }
}
// 旧モードのプレイヤーの更新 (入力・鎖・移動・成長)
void ChronoSystem::UpdatePlayer(entt::registry& r,Player& p,GameContext& ctx){
    const float dt=ctx.dt;
    // 確認用の入力 (ControlFrame) があればそれを使う
    const auto* control=r.try_get<ControlFrame>(player_);
    // F4 で蛇だけのモードを切り替える
    bool snakeKey=control?control->snake:Down(VK_F4);
    if(snakeKey&&!prevSnake_){snakeOnly_=!snakeOnly_;creature_=true;arena_=false;prevSnake_=true;Reset(r);return;}
    prevSnake_=snakeKey;
    // 左クリック: 撃つ、中クリック: 自動の狙い、Space: ジャンプ、Shift: 回避
    bool left=control?control->attack:Down(VK_LBUTTON),middle=control?control->toggle:Down(VK_MBUTTON);
    bool jump=control?control->jump:Down(VK_SPACE),dodge=control?control->dodge:Down(VK_SHIFT);
    if(middle&&!prevM_)p.automatic=!p.automatic;
    bool attackPressed=left&&!prevL_;
    if(!left){p.buffer=0;p.bufferedTarget=entt::null;p.holdCancelled=false;}
    if(jump&&!prevSpace_)p.jumpBuffer=0.14f;
    bool dodgePressed=dodge&&!prevShift_;
    prevL_=left;prevM_=middle;prevSpace_=jump;prevShift_=dodge;
    // 右クリックで狙う
    p.aiming=control?control->aim:Down(VK_RBUTTON);
    // カメラの向き (マウスの移動量)
    if(control){yaw_=control->yaw;pitch_=control->pitch;if(control->cameraInput)manualCameraHold_=1.5f;}
    else if(ctx.input){float sens=p.aiming?0.0018f:0.003f;
        if(std::abs(ctx.input->GetMouseDeltaX())+std::abs(ctx.input->GetMouseDeltaY())>0.01f)manualCameraHold_=1.5f;
        yaw_+=ctx.input->GetMouseDeltaX()*sens;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*sens,-1.35f,1.3f);}
    Camera(r,p,ctx,0);
    // 撃つ入力: タイミング良く押したかを記録し、狙いの対象を先行入力にする
    if(attackPressed){p.timedInput=p.lastChainHit>=.07f&&p.lastChainHit<=.32f;p.buffer=.3f;p.bufferedManual=p.aiming;
        p.bufferedTarget=(p.aiming||p.automatic)?Choose(r,p,ctx,p.aiming):entt::null;}
    // WASD をカメラ基準の移動方向にする
    V input{static_cast<float>(Down('D'))-static_cast<float>(Down('A')),0,static_cast<float>(Down('W'))-static_cast<float>(Down('S'))};
    if(control)input=control->move;
    input=Unit(input);V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
    // 回避の入力は先行入力にする
    if(dodgePressed){p.holdCancelled=true;p.buffer=0;p.bufferedTarget=entt::null;p.dodgeBuffer=.2f;
        p.bufferedDodge=Length(wish)>.1f?wish:Forward(yaw_,0);}
    // チェインの勢いは時間とともに戻る
    p.lastChainHit+=dt;if(p.lastChainHit>1.2f)p.chainDrive=std::max(1.f,p.chainDrive-dt);
    // コンボの流れと時間の倍率
    p.flow.Tick(dt);p.stats.seconds+=dt;
    p.aimScale=AimTimeScale(p.aimScale,p.aiming,dt);
    p.worldScale=WorldTimeScale(p.flow.scale,p.aimScale);
    // 各タイマーを減らす
    p.buffer=std::max(0.0f,p.buffer-dt);p.jumpBuffer=std::max(0.0f,p.jumpBuffer-dt);
    p.invincible=std::max(0.0f,p.invincible-dt);p.cooldown=std::max(0.0f,p.cooldown-dt);
    p.failureTime=std::max(0.0f,p.failureTime-dt);p.damageAge+=dt;p.landingAge+=dt;
    // 撃てない状態で押したら理由を表示する
    if(p.buffer>0&&!r.valid(p.target)&&p.action!=Action::Free&&p.action!=Action::Extending&&p.action!=Action::Pulling){
        p.failure=p.action==Action::Collapsed?ChainFailure::Unstable:ChainFailure::Recovering;p.failureTime=0.3f;
    }
    // 衝撃で止まっている間も、入力の読み取りと先行入力・カメラの移動は続ける。
    // タイマーは実時間を使い、キャラクターと弾は 2〜4 フレーム止まる
    if(p.hitStop>0){p.hitStop=std::max(0.0f,p.hitStop-dt);Camera(r,p,ctx,dt);return;}
    // 体の量に合わせて当たり判定を大きくする (空間があるときだけ)
    auto& t=r.get<TransformComponent>(player_);V pos=Read(t.translate);
    float desiredScale=BodyScale(p.mass);
    if(desiredScale!=p.collisionScale){
        V adjusted=pos;
        if(p.grounded)adjusted.y+=(desiredScale-p.collisionScale)*0.4f+0.002f;
        float contact;V normal;float wide=std::max(0.55f,desiredScale*0.9f);
        if(desiredScale<p.collisionScale||!Sweep(adjusted,{0,0.0001f,0},{wide,0.8f+desiredScale*0.4f,wide},contact,normal)){
            pos=adjusted;p.collisionScale=desiredScale;
        }
    }
    // 当たり判定の大きさ
    float scale=p.collisionScale,radius=std::max(0.55f,scale*0.9f);V half{radius,0.8f+scale*0.4f,radius};
    // 接地中はコヨーテタイム (落ちた直後もジャンプできる時間) を戻す
    if(p.grounded){p.coyote=0.1f;p.stats.airKills=0;}else p.coyote=std::max(0.0f,p.coyote-dt);
    // 回避を始める
    if(p.dodgeBuffer>0&&p.cooldown==0&&p.action!=Action::Collapsed){
        p.chainDrive=1;p.lastChainHit=10;p.action=Action::Dodge;p.timer=0.19f;p.cooldown=0.55f;p.invincible=0.15f;p.target=entt::null;
        p.dodgeDirection=p.bufferedDodge;p.buffer=0;p.dodgeBuffer=0;p.bufferedTarget=entt::null;
        p.failureTime=0;p.hand=pos;p.velocity.y=std::max(p.velocity.y,0.0f);p.airHang=.2f;
    }
    p.dodgeBuffer=std::max(0.0f,p.dodgeBuffer-dt);
    if(p.action==Action::Dodge||p.action==Action::Collapsed){p.buffer=0;p.bufferedTarget=entt::null;}
    // 自動の狙いで押し続けている間は、次の対象を続けて撃つ
    if(left&&p.automatic&&!p.aiming&&!p.holdCancelled&&p.action==Action::Free&&p.dodgeBuffer<=0&&p.cooldown<=0){
        p.bufferedTarget=Choose(r,p,ctx,false);p.bufferedManual=false;p.timedInput=false;
        if(r.valid(p.bufferedTarget))p.buffer=.1f;
    }
    // 自由に動ける状態なら鎖を撃つ
    if(p.action==Action::Free && p.buffer>0)Shoot(r,p,ctx);
    if(p.action==Action::Extending){
        // 自動の舵取りは手が進んでいる間だけ。手動の射撃は
        // 本物のレイで、外れれば戻り、体は動かさない
        if(!p.shotManual&&r.valid(p.target)&&r.get<Target>(p.target).active)p.shotDirection=Unit(Center(r,p.target)-p.hand);
        // 手を進める (自動は速い)。壁と対象のどちらに先に当たるかを調べる
        float step=std::min((p.automatic&&!p.shotManual?160.f:110.f)*dt,p.shotLimit-p.shotDistance);
        float nearest=step;entt::entity hit=entt::null;V dir=p.shotDirection;
        float wf;V wn;bool wall=Sweep(p.hand,dir*step,{0.08f,0.08f,0.08f},wf,wn);
        if(wall)nearest=step*wf;
        for(auto e:r.view<Target,TransformComponent>()){
            const auto& tg=r.get<Target>(e);if(!tg.active)continue;
            // 露出した胴がこの命中を受ける。重なっている通常のボスの
            // 範囲に、手動で狙った反撃を奪わせてはいけない
            if(tg.kind==Kind::Boss&&r.valid(weakpoint_)&&r.get<Target>(weakpoint_).active)continue;
            if(!p.shotManual && p.target!=entt::null && e!=p.target)continue;
            float distance;if(RaySphere(p.hand,dir,Center(r,e),tg.radius,nearest,distance)) {nearest=distance;hit=e;}
        }
        // 手を進める
        p.hand=p.hand+dir*nearest;p.shotDistance+=nearest;
        // 対象に当たったとき: 短い鎖なら攻撃だけ、長い鎖なら引き寄せを始める
        if(hit!=entt::null){
            p.target=hit;auto& tg=r.get<Target>(hit);
            if(p.shotLimit<4){Impact(r,p,hit,ctx,false);p.action=Action::Recovery;p.timer=0.10f;}
            else {
                V c=Center(r,hit);p.endpoint=tg.kind==Kind::Anchor ? c+V{0,1.7f,0} : c-Unit(c-pos)*(tg.radius+radius+0.15f);
                if(!Reachable(r,p,hit,pos,&p.pullWaypoint)){p.action=Action::Retracting;p.timer=0.06f;p.failure=ChainFailure::Obstructed;p.failureTime=.8f;}
                else {
                    p.pullStep=Length(p.pullWaypoint-pos)>.01f;
                    p.action=Action::Pulling;p.timer=0;p.pullCost=StrainCost(p.mass,Length(p.endpoint-pos));
                    p.instability+=p.pullCost;if(p.shotManual)++p.stats.manual;else ++p.stats.automatic;
                    if(Overload(p.instability,p.mass)){p.action=Action::Collapsed;p.timer=0.6f;++p.stats.collapses;p.flow.Break();}
                }
            }
        // 壁に当たったか届かなかったら戻す
        }else if(wall||p.shotDistance>=p.shotLimit-0.001f){
            p.target=entt::null;p.action=Action::Retracting;p.timer=0.1f;
            if(wall||p.failure==ChainFailure::None)p.failure=wall?ChainFailure::Obstructed:ChainFailure::Missed;
            p.failureTime=.8f;
        }
    }
    // 引き寄せ: 対象の手前 (足場なら上) へ向かって体を動かす
    if(p.action==Action::Pulling){
        p.timer+=dt;
        // 対象が無くなったか時間切れなら止める
        if(!r.valid(p.target)||!r.get<Target>(p.target).active||p.timer>0.65f){
            p.action=Action::Free;p.target=entt::null;p.failure=ChainFailure::OutOfRange;p.failureTime=.8f;
        }
        else {
            auto& tg=r.get<Target>(p.target);V c=Center(r,p.target);
            V end=tg.kind==Kind::Anchor?c+V{0,1.7f,0}:c-Unit(c-pos)*(tg.radius+radius+0.15f);
            bool stepping=p.pullStep;
            if(stepping)end=p.pullWaypoint;
            // 距離に応じた速さで近づく (途中で壁に当たったら止める)
            V delta=end-pos;float dist=Length(delta);float step=std::min(dist,PullSpeed(dist)*(1+(p.chainDrive-1)*std::clamp(dist/3.f,0.f,1.f))*dt);
            float f;V n;
            if(Sweep(pos,Unit(delta)*step,half,f,n)){
                pos=pos+Unit(delta)*(step*std::max(0.0f,f-0.005f));p.action=Action::Recovery;p.timer=0.1f;p.target=entt::null;
                p.failure=ChainFailure::Obstructed;p.failureTime=.8f;
            }else{
                pos=pos+Unit(delta)*step;p.velocity={0,2.0f,0};p.grounded=false;p.hand=c;
                // 着いたら命中の処理をして、少し浮かせる
                if(dist<=step+0.08f){
                    if(stepping){p.pullStep=false;}
                    else {
                    t.translate=Write(pos);Impact(r,p,p.target,ctx,true);
                    p.action=Action::Recovery;p.timer=p.automatic?.025f:.055f;p.velocity={0,4,0};p.airHang=0.28f;
                    }
                }
            }
        }
    }else{
        // 回復・戻り・回避・崩れの時間を進める
        if(p.action==Action::Recovery||p.action==Action::Retracting||p.action==Action::Dodge||p.action==Action::Collapsed){
            p.timer-=dt;
            if(p.action==Action::Retracting)p.hand=Lerp(p.hand,pos,std::min(1.0f,dt*25));
            if(p.timer<=0){p.action=Action::Free;p.target=entt::null;}
        }
        // 移動の速度を目標へ近づける (地上は速く反応する)
        V desired=wish*15;float response=1-std::exp(-(p.grounded?35.0f:13.0f)*dt);
        p.velocity.x+=(desired.x-p.velocity.x)*response;p.velocity.z+=(desired.z-p.velocity.z)*response;
        if(p.action==Action::Dodge){p.velocity.x=p.dodgeDirection.x*32;p.velocity.z=p.dodgeDirection.z*32;}
        if(p.action==Action::Collapsed){p.velocity.x*=0.5f;p.velocity.z*=0.5f;}
        // ジャンプ
        if(p.jumpBuffer>0&&p.coyote>0&&p.action!=Action::Collapsed){p.velocity.y=13;p.grounded=false;p.coyote=0;p.jumpBuffer=0;}
        p.airHang=std::max(0.0f,p.airHang-dt);
        // 重力 (落下の速さには上限がある)
        p.velocity.y=std::max(-42.0f,p.velocity.y-Gravity(p.velocity.y,p.airHang>0)*dt);
        bool wasGrounded=p.grounded;float fallingSpeed=-p.velocity.y;
        // 半単位のブロックの階段は、チェインが終わっても歩ける
        bool stepped=false;
        V horizontal{p.velocity.x*dt,0,p.velocity.z*dt};
        float stepFraction;V stepNormal;
        if(p.grounded && Length(horizontal)>0.001f &&
           Sweep(pos,horizontal,half,stepFraction,stepNormal) && std::abs(stepNormal.y)<0.1f){
            V raised=pos+V{0,0.56f,0};
            if(!Sweep(pos,{0,0.56f,0},half,stepFraction,stepNormal) &&
               !Sweep(raised,horizontal,half,stepFraction,stepNormal) &&
               Sweep(raised+horizontal,{0,-0.62f,0},half,stepFraction,stepNormal) && stepNormal.y>0.5f){
                pos=raised+horizontal+V{0,-0.62f*stepFraction+0.001f,0};
                p.velocity.y=0;stepped=true;
            }
        }
        if(!stepped)pos=Move(pos,p.velocity*dt,half,p.grounded,&p.velocity);
        // 着地したらつぶれの演出と水滴
        if(p.grounded&&!wasGrounded){
            p.landingAge=0;p.landingStrength=std::clamp(fallingSpeed/24,0.2f,1.0f);
            for(int i=0;i<8&&droplets_.size()<96;++i){float a=i*0.785398f;
                droplets_.push_back({pos-V{0,half.y-.15f,0},{std::cos(a)*4,2,std::sin(a)*4},0});}
        }
        if(p.grounded){
            // 体の幅全体を支える場所だけを保存する
            for(const auto& b:solids_)if(pos.x>b.min.x+radius&&pos.x<b.max.x-radius&&pos.z>b.min.z+radius&&pos.z<b.max.z-radius&&std::abs(pos.y-half.y-b.max.y)<.12f)
                p.recoveryPoint=pos+V{0,.3f,0};
        }
        // 接地中は不安定さが減っていく
        if(p.grounded&&p.action!=Action::Collapsed)p.instability=std::max(0.0f,p.instability-32*dt);
    }
    // 落ちたら復帰位置へ戻し、量を減らす
    if(pos.y<-15){pos=p.recoveryPoint;p.velocity={};p.action=Action::Free;p.target=entt::null;
        p.mass=std::max(1.0f,p.mass-12);p.flow.Break();p.failure=ChainFailure::Fell;p.failureTime=1.4f;cameraReady_=false;}
    // 位置と向きを反映する
    t.translate=Write(pos);
    if(Length(wish)>0.1f)t.rotate.y=std::atan2(wish.x,wish.z);
    // 見た目の大きさは量に従い、当たり判定の成長は収まる所でだけ反映する
    t.scale={p.collisionScale,p.collisionScale,p.collisionScale};
    // 着地のつぶれ・空中の伸び・回避や崩れの形
    float squash=LandingSquash(p.landingAge,p.landingStrength);
    if(!p.grounded&&p.action!=Action::Pulling)squash+=std::min(.18f,std::abs(p.velocity.y)*.009f);
    t.scale.y*=squash;t.scale.x/=std::sqrt(squash);t.scale.z/=std::sqrt(squash);
    if(p.action==Action::Dodge){t.scale.x*=1.15f;t.scale.y*=0.65f;t.scale.z*=1.3f;}
    if(p.action==Action::Collapsed){t.scale.x*=1.4f;t.scale.y*=0.35f;t.scale.z*=1.4f;}
    p.stats.maxMass=std::max(p.stats.maxMass,p.mass);
    // 体力の表示を量に合わせる
    auto& hp=r.get<HealthComponent>(player_);hp.SetHp(p.mass);hp.SetMaxHp(200);hp.SetDead(p.mass<=0);
    Camera(r,p,ctx,dt);
    // 照準の先の候補 (届かない対象も表示用に探す)
    p.preview=(p.action==Action::Extending||p.action==Action::Pulling)?p.target:
        (p.aiming||p.automatic)?Choose(r,p,ctx,p.aiming):entt::null;
    p.blockedPreview=entt::null;
    if(!r.valid(p.preview)&&(p.aiming||p.automatic)){
        float best=.55f;V forward=Forward(yaw_,pitch_),cam=Read(ctx.camera->Position());
        for(auto e:r.view<Target,TransformComponent>()){
            const auto& tg=r.get<Target>(e);if(!tg.active||tg.kind==Kind::Boss)continue;
            V c=Center(r,e);float alignment=Dot(Unit(c-cam),forward),hit;
            if(p.aiming&&!RaySphere(cam,forward,c,tg.radius,100,hit))continue;
            if(alignment>best&&Length(c-pos)<=Reach(p.mass)+tg.radius&&!Reachable(r,p,e,pos)){
                best=alignment;p.blockedPreview=e;}
        }
    }
}

// 旧モードのボスの更新
void ChronoSystem::UpdateBoss(entt::registry& r,Player& p,GameContext& ctx){
    if(!r.valid(boss_))return;
    if(creature_){UpdateCreature(r,p,ctx);return;}
    // プレイヤーが前へ進んだら戦闘を始める
    auto& b=r.get<Boss>(boss_);V player=Read(r.get<TransformComponent>(player_).translate);
    if(!b.engaged){if(player.z>3)b.engaged=true;else return;}
    const float dt=ctx.dt*p.worldScale;b.clock+=dt;
    // アリーナ: 予告 → 突進か薙ぎ払い → 隙 を繰り返す
    if(arena_){
        V c=Center(r,boss_);
        if(b.phase==0&&b.clock>1.4f){b.phase=1;b.clock=0;b.countered=false;b.attackHit=false;
            ++b.wave;++p.stats.opportunities;b.attackDirection=Unit(V{player.x-c.x,0,player.z-c.z});}
        bool charge=b.wave%2==0;
        if(b.phase==1&&b.clock>(charge?1.1f:.85f)){b.phase=2;b.clock=0;}
        if(b.phase==2){
            V before=c;
            if(charge){bool grounded;V next=Move(c,b.attackDirection*(24*dt),{3,4.8f,3},grounded);
                auto& t=r.get<TransformComponent>(boss_);t.translate=Write(Read(t.translate)+next-c);c=next;}
            float hit=0;V delta=c-before;
            bool contact=charge?(Length(delta)<.001f?Length(player-c)<4.4f:
                RaySphere(before,Unit(delta),player,4.4f,Length(delta),hit)):
                Length(player-(c+V{0,-2,0}))<7&&Dot(Unit(V{player.x-c.x,0,player.z-c.z}),b.attackDirection)>.1f;
            if(contact&&!b.attackHit){if(p.invincible<=0)p.damageReason=charge?"HIT - BOSS CHARGE":"HIT - BOSS SWEEP";
                Damage(r,p,charge?22.f:18.f,ctx,c);b.attackHit=true;}
            if(b.clock>(charge?.65f:.22f)){b.phase=3;b.clock=0;b.waveBreaks=3;}
        }
        r.get<Target>(weakpoint_).active=b.phase==3&&!b.countered;
        r.get<Target>(weakpoint_).offset=c-Read(r.get<TransformComponent>(weakpoint_).translate);
        if(b.phase==3&&b.clock>2.6f){b.phase=0;b.clock=0;}
        return;
    }
    // 通常: 引き寄せの道になる球を出し、その後で危険な弾を撃つ
    if(b.phase==0 && b.clock>1.4f){b.phase=1;b.clock=0;b.emitted=0;b.waveBreaks=0;b.countered=false;++b.wave;++p.stats.opportunities;}
    // 球 (壊せる) か危険な弾を作る
    auto spawn=[&](V pos,V velocity,bool breakable){
        auto e=Mesh(r,breakable?"Chain orb":"Hazard",ball,pos,breakable?V{2,2,2}:V{1,1,1});
        auto& target=r.emplace<Target>(e);target.kind=Kind::Projectile;target.radius=breakable?1.25f:0.65f;
        target.hp=1;target.wave=b.wave;target.reward=8;target.active=breakable;
        target.offset=pos-Read(r.get<TransformComponent>(e).translate);
        auto& projectile=r.emplace<Projectile>(e);projectile.velocity=velocity;projectile.breakable=breakable;projectile.lifetime=20;
        r.get<MeshRendererComponent>(e).color=breakable?DirectX::XMFLOAT4{0.15f,0.9f,1.8f,1}:DirectX::XMFLOAT4{2,0.12f,0.35f,1};
    };
    if(b.phase==1){
        const V route[]={{0,3,18},{-2,7,22},{1,12,26},{3,17,29},{1,22,32},{-1,26,37},{0,28,43}};
        while(b.emitted<7 && b.clock>=static_cast<float>(b.emitted)*0.08f){
            // 共通の漂いで、階段の形を保ったまま近づく
            spawn(route[b.emitted],{0,0,-0.45f},true);++b.emitted;
        }
        if(b.clock>2.0f){
            for(int i=-1;i<=1;++i){V start{static_cast<float>(i)*9,23,40};spawn(start,Unit(player-start)*12,false);}
            b.phase=2;b.clock=0;
        }
    }
    // 球を 3 つ壊したら弱点が出る
    bool exposed=(b.phase==1||b.phase==2) && b.waveBreaks>=3 && !b.countered;
    r.get<Target>(weakpoint_).active=exposed;
    r.get<Target>(weakpoint_).offset=Center(r,boss_)-Read(r.get<TransformComponent>(weakpoint_).translate);
    r.get<MeshRendererComponent>(weakpoint_).enabled=false;
    // 時間が経ったら次の波へ (残った弾は消す)
    if(b.phase==2&&b.clock>14){b.phase=3;b.clock=0;}
    if(b.phase==3&&b.clock>2){
        b.phase=0;b.clock=0;
        for(auto e:r.view<Projectile,Target>()){r.get<Projectile>(e).lifetime=0;r.get<Target>(e).active=false;}
    }
}
// 旧モードのワールドの更新 (敵・羽・足場)
void ChronoSystem::UpdateWorld(entt::registry& r,Player& p,GameContext& ctx){
    // ボスを更新し、世界の時間 (遅くなる) と プレイヤーの位置を用意する
    UpdateBoss(r,p,ctx);float dt=ctx.dt*p.worldScale;V player=Read(r.get<TransformComponent>(player_).translate);
    // 繰り返し使える足場は、待ち時間が過ぎたら再び出す
    for(auto e:r.view<Anchor,Target,MeshRendererComponent>()){
        auto& a=r.get<Anchor>(e);if(a.reusable&&a.cooldown>0){a.cooldown=std::max(0.0f,a.cooldown-dt);
            if(a.cooldown==0){r.get<Target>(e).active=true;r.get<MeshRendererComponent>(e).enabled=true;}}
    }
    // 跳ねる敵: 地上で近くにプレイヤーがいれば跳びかかる
    for(auto e:r.view<Hopper,Target,TransformComponent>()){
        auto& tg=r.get<Target>(e);if(!tg.active)continue;
        auto& h=r.get<Hopper>(e);auto& t=r.get<TransformComponent>(e);V c=Center(r,e),delta=player-c;
        h.timer-=dt;
        if(h.grounded&&h.timer<=0 && Length(delta)<26){
            V dir=Unit(V{delta.x,0,delta.z});h.velocity=dir*6;h.velocity.y=8;h.timer=0.9f;h.grounded=false;
            t.rotate.y=std::atan2(dir.x,dir.z);
        }
        // 重力で落とし、壁に沿って動かす
        h.velocity.y-=24*dt;c=Move(c,h.velocity*dt,{1.1f,1.4f,1.1f},h.grounded,&h.velocity);
        if(h.grounded){h.velocity.x=0;h.velocity.z=0;}
        t.translate=Write(c-tg.offset);
        // プレイヤーに触れたらダメージ (引き寄せ中のその敵は除く)
        if(Length(c-player)<2.2f && !(p.action==Action::Pulling&&p.target==e)&&p.invincible<=0){p.damageReason="HIT - HOPPER";Damage(r,p,12,ctx,c);}
    }
    // 生きている跳ねる敵を、当たり判定を考えて左右対称に引き離す
    std::vector<entt::entity> enemies;
    for(auto e:r.view<Hopper,Target>())if(r.get<Target>(e).active)enemies.push_back(e);
    for(size_t i=0;i<enemies.size();++i)for(size_t j=i+1;j<enemies.size();++j){
        auto a=enemies[i],b=enemies[j];V ca=Center(r,a),cb=Center(r,b),d=ca-cb;
        if(std::abs(d.y)>2.5f)continue;d.y=0;float n=Length(d);if(n>=2.5f)continue;
        V push=(n<0.001f?V{1,0,0}:d*(1/n))*((2.5f-n)*0.5f);bool ground;
        r.get<TransformComponent>(a).translate=Write(Move(ca,push,{1.1f,1.4f,1.1f},ground)-r.get<Target>(a).offset);
        r.get<TransformComponent>(b).translate=Write(Move(cb,push*-1,{1.1f,1.4f,1.1f},ground)-r.get<Target>(b).offset);
    }
    // 弾と羽の更新 (寿命が切れたもの・当たったものは消す)
    std::vector<entt::entity> expired;
    for(auto e:r.view<Projectile,Target,TransformComponent>()){
        auto& q=r.get<Projectile>(e);auto& tg=r.get<Target>(e);auto& t=r.get<TransformComponent>(e);
        // 羽: 始点から目的地へ 0.45 秒で飛び、着いたら引き寄せの対象になる
        if(auto* feather=r.try_get<Feather>(e)){
            if(r.get<CreatureBoss>(boss_).stage!=CreatureStage::Opening&&e!=p.target){expired.push_back(e);continue;}
            q.lifetime-=ctx.dt;
            if(q.lifetime<=0||(feather->age>=feather->delay+.45f&&!tg.active)){expired.push_back(e);continue;}
            V before=Read(t.translate);feather->age+=ctx.dt;
            float u=Ease((feather->age-feather->delay)/.45f);
            V after=Lerp(feather->start,feather->destination,u);t.translate=Write(after);
            t.rotate.y+=dt*1.4f;
            if(feather->age>=feather->delay+.45f){tg.active=true;
                if(!feather->readySound){feather->readySound=true;if(feather->delay<.001f)Engine::Audio::GetInstance()->Play(hitSound_,false,.12f*Engine::Audio::GetInstance()->GetMasterSEVolume(),1.6f);}
            }
            // 着いた羽は水色、飛んでいる羽は赤。遠い羽は隠す
            r.get<MeshRendererComponent>(e).color=tg.active?DirectX::XMFLOAT4{.1f,1.2f,1.6f,1}:DirectX::XMFLOAT4{2,.15f,.02f,1};
            r.get<MeshRendererComponent>(e).enabled=!tg.active||e==p.target||Length(after-player)<Reach(p.mass)*1.6f;
            t.rotate.x=tg.active?0.f:1.2f;
            // 飛んでいる羽は危険。着いた後は安定したチェインの対象になる
            float hit;V delta=after-before;
            if(!tg.active&&Length(delta)>.001f&&RaySphere(before,Unit(delta),player,1.7f,Length(delta),hit)){
                if(p.invincible<=0)p.damageReason="HIT - FEATHER VOLLEY";
                Damage(r,p,14,ctx,before);
            }
            continue;
        }
        // 通常の弾: まっすぐ進み、プレイヤーに当たったらダメージ、壁に当たったら消える
        q.lifetime-=dt;if(q.lifetime<=0 || (q.breakable&&!tg.active)){expired.push_back(e);continue;}
        V before=Center(r,e),after=before+q.velocity*dt;
        float hit;bool contact=RaySphere(before,Unit(after-before),player,tg.radius+0.9f,Length(after-before),hit);
        if(contact && !(p.action==Action::Pulling&&p.target==e)){
            if(p.invincible<=0)p.damageReason=q.breakable?"HIT - ORB":"HIT - RED PROJECTILE";
            Damage(r,p,q.breakable?10.0f:16.0f,ctx,before);expired.push_back(e);continue;
        }
        if(!Clear(before,after,0.15f)){expired.push_back(e);continue;}
        t.translate=Write(after-tg.offset);
    }
    // 消す弾をまとめて破棄する
    for(auto e:expired)r.destroy(e);
}
// 演出: 火花と水滴を進め、時間の遅さや被弾に応じてポスト効果をかける
void ChronoSystem::Presentation(entt::registry& r,Player& p,GameContext& ctx){
    // 火花はプレイヤーへ吸い込まれ、0.35 秒で消える
    V player=Read(r.get<TransformComponent>(player_).translate);
    for(auto& spark:sparks_){spark.age+=ctx.dt;spark.to=player;}
    sparks_.erase(std::remove_if(sparks_.begin(),sparks_.end(),[](const Spark& s){return s.age>0.35f;}),sparks_.end());
    // 水滴は重力で落ち、0.45 秒で消える
    for(auto& d:droplets_){d.age+=ctx.dt;d.velocity.y-=16*ctx.dt;d.position=d.position+d.velocity*ctx.dt;}
    droplets_.erase(std::remove_if(droplets_.begin(),droplets_.end(),[](const Droplet& d){return d.age>.45f;}),droplets_.end());
    // 時間が遅いほど強いポスト効果をかける
    float intensity=finished_?0:std::clamp((1-p.worldScale)/.8f,0.f,1.f);
    postStrength_+=(intensity-postStrength_)*(1-std::exp(-8*ctx.dt));
    Engine::Renderer::PostProcessParams params{};
    params.time=ctx.renderer->GetPostProcessParams().time;params.san=postStrength_;
    // 引き寄せ中は歪みを強め、被弾直後は周辺を暗くする
    params.distortion=p.action==Action::Pulling?1.0f:0.65f;
    params.vignette=p.damageAge<.35f?1-p.damageAge/.35f:0;
    // インクのモードでは、プレイヤーとボスの深度を被写界深度に渡す
    if(inkMode_&&!finished_){
        using namespace DirectX;XMFLOAT3 playerView,bossView;V boss=Center(r,boss_);
        XMStoreFloat3(&playerView,XMVector3TransformCoord(XMVectorSet(player.x,player.y,player.z,1),ctx.camera->View()));
        XMStoreFloat3(&bossView,XMVector3TransformCoord(XMVectorSet(boss.x,boss.y,boss.z,1),ctx.camera->View()));
        params.dofFocus=std::max(8.f,playerView.z);params.dofRange=32;params.dofStrength=0;params.dofBossDepth=bossView.z;
    }
    ctx.renderer->SetPostProcessParams(params);ctx.renderer->SetPostEffect("ChronoFocus");
}
// ゲームを終える (勝ちなら最高記録を保存する)
void ChronoSystem::Finish(Player& p,bool win){
    if(finished_)return;
    // 勝ち負けの曲を流し、プレイヤーの状態を止める
    Music::Play(win ? Music::Victory : Music::Defeat);
    finished_=true;won_=win;finishAge_=0;p.target=entt::null;p.preview=entt::null;
    p.action=Action::Free;p.velocity={};p.flow.remaining=0;p.flow.scale=1;p.aimScale=1;p.worldScale=1;p.aiming=false;
    p.hitStop=0;p.damageAge=1;
    // 通常のプレイで勝ったときだけ記録を更新して保存する
    if(!win||diagnostic_||arena_||creature_)return;
    newTime_=bestTime_==0||p.stats.seconds<bestTime_;newChain_=p.flow.maxCombo>bestChain_;
    if(newTime_)bestTime_=p.stats.seconds;if(newChain_)bestChain_=p.flow.maxCombo;
    try{auto path=RecordPath();if(!path.empty()){
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);file<<nlohmann::json{{"time",bestTime_},{"chain",bestChain_}}.dump(2);
    }}catch(...){OutputDebugStringA("Chrono: record could not be saved.\n");}
}
// 毎フレームの更新: 終了後は結果画面の操作、インクのモードなら UpdateInk、旧モードなら各更新
void ChronoSystem::Update(entt::registry& r,GameContext& ctx){
    // 初期化前・プレイヤーがいないときは何もしない
    if(!initialized_||!r.valid(player_))return;
    // スライムの生活モードで確認用のやり直しの指示があれば初期化する
    if(slimeLife_){const auto* control=r.try_get<ControlFrame>(player_);if(control&&control->restart){Reset(r);return;}}
    if(finished_){
        auto& p=r.get<Player>(player_);
        // ボスを倒した後は、結果の後ろで散らばった残骸の周りをカメラが回り続ける
        if(inkMode_&&bossDefeat_)UpdateBossDefeat(r,p,ctx);
        else{
            if(inkMode_){
                auto& ink=r.get<InkPlayer>(player_);
                ink.monoAge=-1;ink.phase=SlimePhase::Roaming;ink.charge=ink.spent=0;
                InkCamera(r,p,ctx);
            }
            Presentation(r,p,ctx);
        }
        finishAge_+=ctx.dt;Engine::WindowDX::SetCursorVisible(true);
        // 結果画面: R / Enter でやり直し、TAB でセレクトへ
        if(finishAge_>0.2f&&ctx.input){
            UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
            if(ctx.useOverrideMouse)ui.SetPointer(ctx.overrideMouseX,ctx.overrideMouseY);
            if(UI::Pressed(DIK_R)||UI::Pressed(DIK_RETURN)||UI::PadPressed(XINPUT_GAMEPAD_X)||ui.Click(UI::Retry)){
                if(inkMode_||arena_||creature_){Reset(r);return;}
                Engine::SceneParameters params;params.stagePath=Data::MainStagePath();
                Engine::SceneManager::GetInstance()->RequestChange("Game",params);
            }
            if(UI::Pressed(DIK_TAB)||UI::PadPressed(XINPUT_GAMEPAD_Y)||ui.Click(UI::Select))Engine::SceneManager::GetInstance()->RequestChange("Select");
        }
        return;
    }
    // インクのモードの更新
    if(inkMode_){UpdateInk(r,r.get<Player>(player_),ctx);return;}
    const auto* control=r.try_get<ControlFrame>(player_);
    // 旧モード: F3 で生き物、F2 でアリーナに切り替える
    bool creatureKey=control?control->creature:Down(VK_F3);
    if(creatureKey&&!prevCreature_){snakeOnly_=false;creature_=!creature_;arena_=false;prevCreature_=true;Reset(r);return;}
    prevCreature_=creatureKey;
    bool arenaKey=control?control->arena:Down(VK_F2);
    if(arenaKey&&!prevArena_){snakeOnly_=false;creature_=false;arena_=!arena_;prevArena_=true;Reset(r);return;}
    prevArena_=arenaKey;
    // 旧モード: F6 で画面の揺れの強さを切り替え、プレイヤー・ワールド・演出を更新する
    auto& p=r.get<Player>(player_);
    diagnostic_=r.all_of<ControlFrame>(player_);
    bool shakeKey=!control&&Down(VK_F6);if(shakeKey&&!prevShake_)shakeSetting_=(shakeSetting_+1)%3;prevShake_=shakeKey;
    ctx.camera->SetShakeStrength(shakeSetting_*.5f);
    UpdatePlayer(r,p,ctx);if(!finished_&&p.hitStop<=0)UpdateWorld(r,p,ctx);
    Presentation(r,p,ctx);
    // 敵の時間の速さを共有の戦闘の流れへ渡す
    if(ctx.combatFlow)ctx.combatFlow->SetChronoEnemyScale(p.hitStop>0?0:p.worldScale);
}
// 3D の描画 (インクのモードなら DrawInk)
void ChronoSystem::Draw(entt::registry& r,GameContext& ctx){
    if(!initialized_||!ctx.isPlaying||!r.valid(player_)||!ctx.renderer)return;
    if(inkMode_){DrawInk(r,ctx);return;}
    // 球を描く関数
    auto sphere=[&](V at,float size,Engine::Vector4 color){
        Engine::Transform transform;transform.translate=EV(at);transform.scale={size,size,size};
        ctx.renderer->DrawMesh(sphere_,white_,transform,color,"Default",0.3f,false);
    };
    // 腕はプレイヤーの SPH の体積の一部 (GameScene::SetGPUFluidTether)
    // 火花と水滴を球で描く
    for(const auto& s:sparks_){float a=std::clamp(s.age/0.35f,0.0f,1.0f);
        V at=Lerp(s.from,s.to,a*a);sphere(at,0.2f*(1-a)+0.08f,{0.2f,1.4f,1.6f,1});}
    for(const auto& d:droplets_)sphere(d.position,(d.dust?.8f:.18f)*(1-d.age/.45f)+.035f,d.dust?Engine::Vector4{.65f,.48f,.26f,1}:Engine::Vector4{.18f,1.1f,.6f,1});
    // 生き物のボス: 衝撃の輪と、攻撃の予告の線 (叩きつけは円、突進は矢印、翼は箱、薙ぎ払いは矩形)
    if(creature_&&r.valid(boss_)){
        const auto& c=r.get<CreatureBoss>(boss_);
        DrawBossImpact(ctx,c);
        if((c.stage==CreatureStage::Snake||c.stage==CreatureStage::Opening)&&(c.sweepPhase==1||c.sweepPhase==2)){
            auto color=c.sweepPhase==1?Engine::Vector4{1,.65f,.05f,1}:Engine::Vector4{2,.1f,.02f,1};
            V origin=c.sweepCenter+V{0,.12f,0};
            if(c.attack==CreatureAttack::Slam){
                for(int i=0;i<48;++i){float a=i*6.283185f/48,aa=(i+1)*6.283185f/48;
                    ctx.renderer->DrawLine3D(EV(origin+V{std::cos(a)*8,0,std::sin(a)*8}),EV(origin+V{std::cos(aa)*8,0,std::sin(aa)*8}),color);}
                for(float x:{-8.f,8.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,0}),EV(origin+V{x,32,0}),color);
            }else if(c.attack==CreatureAttack::Charge){
                for(float x:{-4.f,4.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,-20}),EV(origin+V{x,0,26}),color);
                ctx.renderer->DrawLine3D(EV(origin+V{0,0,20}),EV(origin+V{0,0,-16}),color);
                for(float x:{-3.f,3.f})ctx.renderer->DrawLine3D(EV(origin+V{0,0,-16}),EV(origin+V{x,0,-11}),color);
            }else if(c.attack==CreatureAttack::Wing){
                for(float y:{-6.f,6.f})for(float z:{-7.f,7.f})ctx.renderer->DrawLine3D(EV(origin+V{-16,y,z}),EV(origin+V{18,y,z}),color);
                for(float x:{-16.f,18.f})for(float z:{-7.f,7.f})ctx.renderer->DrawLine3D(EV(origin+V{x,-6,z}),EV(origin+V{x,6,z}),color);
            }else{
            for(float z:{-5.f,5.f})ctx.renderer->DrawLine3D(EV(origin+V{-15,0,z}),EV(origin+V{15,0,z}),color);
            for(float x:{-15.f,15.f})ctx.renderer->DrawLine3D(EV(origin+V{x,0,-5}),EV(origin+V{x,0,5}),color);
            float edge=c.sweepPhase==1?-12.f:-12.f+24.f*AttackEase(c.sweepTime/ActiveTime(c.attack));
            ctx.renderer->DrawLine3D(EV(origin+V{edge,0,-5}),EV(origin+V{edge,0,5}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{-10,0,0}),EV(origin+V{10,0,0}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{10,0,0}),EV(origin+V{7,0,-2}),color);
            ctx.renderer->DrawLine3D(EV(origin+V{10,0,0}),EV(origin+V{7,0,2}),color);
            }
        }
    }
    // アリーナ: 突進の道か薙ぎ払いの扇を描く
    if(arena_&&r.valid(boss_)){
        const auto& b=r.get<Boss>(boss_);V c=Center(r,boss_)+V{0,-4.9f,0};
        if(b.phase==1||b.phase==2){
            Engine::Vector4 color=b.phase==1?Engine::Vector4{1,.65f,.08f,1}:Engine::Vector4{1,.1f,.1f,1};
            V side{b.attackDirection.z,0,-b.attackDirection.x};
            if(b.wave%2==0){for(float sign:{-1.f,1.f})ctx.renderer->DrawLine3D(EV(c+side*(sign*4.4f)),
                EV(c+side*(sign*4.4f)+b.attackDirection*19),color);
                ctx.renderer->DrawLine3D(EV(c+b.attackDirection*19-side*4.4f),EV(c+b.attackDirection*19+side*4.4f),color);
            }else for(int i=0;i<32;++i){float a=-1.47f+i*2.94f/32,aa=-1.47f+(i+1)*2.94f/32;
                ctx.renderer->DrawLine3D(EV(c+(b.attackDirection*std::cos(a)+side*std::sin(a))*7),
                    EV(c+(b.attackDirection*std::cos(aa)+side*std::sin(aa))*7),color);}
        }
    }
    // 飛んでいる羽の軌跡の線
    for(auto e:r.view<Feather,Target,TransformComponent>()){
        const auto& feather=r.get<Feather>(e);if(r.get<Target>(e).active||feather.age<feather.delay)continue;
        V at=Read(r.get<TransformComponent>(e).translate);
        ctx.renderer->DrawLine3D(EV(at),EV(at-Unit(feather.destination-feather.start)*5.f),{2,.2f,.02f,1});
    }
    // 弾はシアンと赤のシルエットのまま。選んだ対象だけに
    // HUD の枠を付け、戦場に重なる道の線は描かない
    // 弱点が出ているときは、核の周りに輪を描く
    if(r.valid(weakpoint_)&&r.get<Target>(weakpoint_).active){
        V c=Center(r,weakpoint_);float radius=2.5f+.2f*std::sin(r.get<Player>(player_).stats.seconds*8);
        for(int i=0;i<32;++i){float a=i*6.283185f/32,b=(i+1)*6.283185f/32;
            ctx.renderer->DrawLine3D(EV(c+V{std::cos(a)*radius,std::sin(a)*radius,0}),
                EV(c+V{std::cos(b)*radius,std::sin(b)*radius,0}),{1,.75f,.15f,1});}
    }
}
// UI の描画 (インクのモードなら DrawInkUI)
void ChronoSystem::DrawUI(entt::registry& r,GameContext& ctx){
    if(!initialized_||!r.valid(player_)||!ctx.renderer)return;
    if(inkMode_){DrawInkUI(r,ctx);return;}
    // 画面の大きさに合わせた倍率と、矩形と文字を描く関数
    const auto& p=r.get<Player>(player_);auto* render=ctx.renderer;
    float w=ctx.viewportSize.x>0?ctx.viewportSize.x:1280,h=ctx.viewportSize.y>0?ctx.viewportSize.y:720;
    float s=std::min(w/1280,h/720);
    auto rect=[&](float x,float y,float width,float height,Engine::Vector4 color){
        if(height>=40 && width<1280){ UI::Canvas ui(render,w,h);ui.Panel({x,y,width,height},UI::Ink);return; }
        Engine::Renderer::SpriteDesc d;d.x=x*s;d.y=y*s;d.w=width*s;d.h=height*s;d.color=color;d.layer=0;render->DrawSprite(white_,d);};
    auto text=[&](const std::string& label,float x,float y,float scale,Engine::Vector4 color=Engine::Vector4{0.9f,0.98f,1,1}){
        render->DrawString(label,x*s,y*s,scale*s,color,UI::Font);};
    const Engine::Vector4 bg=UI::Ink,cyan=UI::Lime,gold=UI::Gold;
    // 終了後の結果画面 (ランク・時間・チェイン・反撃・統計・最高記録・ボタン)
    if(finished_){
        rect(0,0,1280,720,{0.02f,0.03f,0.06f,0.86f});rect(250,82,780,530,bg);
        text(won_?"CHRONO CHAIN  /  CLEAR":"REFORM AND TRY AGAIN",290,106,0.65f);
        text(won_?p.stats.Rank(p.flow.maxCombo):"RETRY",840,160,1.7f,gold);
        text("CLEAR TIME     "+TimeText(p.stats.seconds)+(newTime_?"  NEW BEST":""),290,194,0.43f);
        text("MAX CHAIN      "+std::to_string(p.flow.maxCombo)+(newChain_?"  NEW BEST":""),290,239,0.43f);
        text("COUNTERS       "+std::to_string(p.stats.counters),290,284,0.43f);
        text("AIR KILLS  "+std::to_string(p.stats.maxAirKills)+"    ORBS BROKEN  "+std::to_string(p.stats.projectiles),290,347,0.32f);
        text("MAX MASS  "+std::to_string(static_cast<int>(p.stats.maxMass))+"    HITS  "+std::to_string(p.stats.hitsTaken)+"    COLLAPSES  "+std::to_string(p.stats.collapses),290,382,0.32f);
        text("MANUAL  "+std::to_string(p.stats.manual)+"    AUTO  "+std::to_string(p.stats.automatic)+"    SLOW  "+TimeText(p.flow.slowSeconds),290,417,0.32f);
        text("BEST  "+TimeText(bestTime_)+"    CHAIN  "+std::to_string(bestChain_),290,455,0.31f,cyan);
        rect(290,520,320,62,{0.07f,0.35f,0.48f,1});rect(650,520,320,62,{0.14f,0.17f,0.26f,1});
        UI::Canvas ui(render,w,h);ui.Button(UI::Retry,"RETRY",true);ui.Button(UI::Select,"STAGE SELECT");
        ui.Prompt("keyboard_r","RETRY","xbox_button_x","RETRY",310,475);ui.Prompt("keyboard_tab","STAGE SELECT","xbox_button_y","STAGE SELECT",710,475);
        return;
    }
    // 下の HUD: 量・負担・狙いのモード・時間の倍率・チェイン
    rect(422,610,436,76,bg);
    text("MASS "+std::to_string(static_cast<int>(p.mass)),438,621,.24f,cyan);
    rect(438,648,188,7,{.1f,.17f,.22f,1});rect(438,648,188*p.mass/200,7,cyan);
    text(p.instability>75?"STRAIN - LAND TO STABILIZE":"STRAIN",651,621,.22f,p.instability>75?gold:cyan);
    rect(651,648,190,7,{.16f,.15f,.13f,1});rect(651,648,190*p.instability/100,7,gold);
    text(p.aiming?"RMB FOCUS / MANUAL":p.automatic?(p.holdCancelled?"RELEASE LMB TO RESUME":"AUTO CHAIN / HOLD LMB"):"MANUAL / HOLD RMB",438,665,.20f);
    if(p.worldScale<.99f)text("TIME "+std::to_string(static_cast<int>(p.worldScale*100))+"%",737,665,.20f,cyan);
    if(p.flow.combo>0){text(std::to_string(p.flow.combo)+" CHAIN",582,542,.42f,gold);
        rect(580,579,125*p.flow.grace/2,3,gold);}
    // ボスの HUD: 指示・羽の壊した数・体力・反撃の数。画面外ならボスの方向を示す
    if(r.valid(boss_)){
        auto& boss=r.get<Boss>(boss_);auto& bt=r.get<Target>(boss_);
        if(boss.engaged){
            float hudX=creature_?-365.f:0.f;
            rect(380+hudX,8,520,55,bg);
            std::string instruction=boss.phase==0?"APPROACH - GET READY":boss.phase==3?(boss.countered?"COUNTER LANDED - REFORM":"WAVE ENDED - REGROUP"):
                boss.waveBreaks<3?"BREAK ORBS  "+std::to_string(std::min(3,boss.waveBreaks))+" / 3":"BOSS EXPOSED - GRAB THE BODY";
            if(arena_)instruction=boss.phase==1?(boss.wave%2==0?"CHARGE INCOMING - DODGE SIDEWAYS":"SWEEP INCOMING - LEAVE THE ARC"):
                boss.phase==2?"ATTACK ACTIVE":boss.phase==3?"OPENING - COUNTER OR LAND":"ARENA - WATCH THE BOSS";
            if(creature_){const auto& c=r.get<CreatureBoss>(boss_);
                instruction=c.stage==CreatureStage::Snake?(c.sweepPhase==1?"SWEEP INCOMING - BACKSTEP OR CHAIN UP":c.sweepPhase==2?"SWEEP ACTIVE - STAY ABOVE THE JAW":c.sweepPhase==3?(c.sweepHit?"RECOVER - NEXT SWEEP SOON":"CORE OPEN - COUNTER NOW"):"SERPENT - WATCH THE HEAD"):
                    c.stage==CreatureStage::Assemble?"MORPHING - REPOSITION / RELAYS CLOSED":
                    c.stage==CreatureStage::Volley?"FEATHER VOLLEY - GET READY":
                    c.stage==CreatureStage::Descend?"REFORMING - LAND TO RECOVER":
                    boss.waveBreaks<3?"CHAIN FEATHERS  "+std::to_string(boss.waveBreaks)+" / 3":"CORE OPEN - CHAIN UP AND STRIKE";
                if(c.sweepPhase==1)instruction=c.attack==CreatureAttack::Sweep?"SWEEP - BACKSTEP OR CHAIN UP":c.attack==CreatureAttack::Charge?"CHARGE - DODGE SIDEWAYS":c.attack==CreatureAttack::Slam?"HEAD SLAM - LEAVE THE COLUMN":"WING SWEEP - SWITCH ROUTE OR DROP";
                if(c.sweepPhase==2)instruction=c.attack==CreatureAttack::Sweep?"SWEEP ACTIVE":c.attack==CreatureAttack::Charge?"CHARGE ACTIVE":c.attack==CreatureAttack::Slam?"SLAM ACTIVE":"WING ACTIVE - DROP OR EVADE";
                if(c.wingCounter)instruction="WING RECOVERY - CORE OPEN";
            }
            text(instruction,408+hudX,15,.27f,gold);
            if(!arena_)for(int i=0;i<3;++i)rect(410.0f+hudX+static_cast<float>(i)*154,36,143,3,boss.waveBreaks>i?cyan:Engine::Vector4{.17f,.23f,.29f,1});
            rect(410+hudX,44,451,4,{0.1f,0.12f,0.2f,1});rect(410+hudX,44,451*std::max(0.0f,bt.hp)/360,4,{0.95f,0.23f,0.45f,1});
            text("COUNTERS "+std::to_string(p.stats.counters)+" / 3",410+hudX,51,.14f);
            V c=Center(r,boss_);if(creature_){auto& creature=r.get<CreatureBoss>(boss_);if(creature.sweepPhase==1||creature.sweepPhase==2)c=creature.attention;}using namespace DirectX;
            XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
            if(clip.w<=0||std::abs(clip.x)>clip.w*.85f||std::abs(clip.y)>clip.w*.75f){
                V d=c-Read(ctx.camera->Position());float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
                float len=std::max(.01f,std::sqrt(x*x+y*y));x/=len;y/=len;
                float sx=std::clamp(640+x*490,110.f,1090.f),sy=std::clamp(360+y*245,95.f,575.f);
                text(boss.phase==1?"! BOSS ATTACK !":"BOSS",sx-40,sy,.26f,gold);
                for(int i=0;i<7;++i){float q=i-3.f;rect(sx+x*(22-std::abs(q)*3)-y*q*3,sy+y*(22-std::abs(q)*3)+x*q*3,4,4,gold);}
            }
        }else text("CHAIN UP TO THE CITADEL",494,34,0.3f,gold);
    }
    // 操作の案内と各種の表示
    if(p.stats.seconds<12)text("WASD Move  SPACE Jump  SHIFT Dodge  LMB Grab  RMB Aim  MMB Auto",270,698,.20f);
    text("F2 ARENA / F3 BIRD / F4 SNAKE",creature_?980.f:24.f,22,.20f,cyan);
    text("F6 SHAKE "+std::to_string(shakeSetting_*50)+"%",creature_?980.f:24.f,42,.18f,cyan);
    if(creature_)text(p.lastChainHit<.32f?"TAP ON IMPACT - BUILD SPEED":"CYAN: CHAIN   RED: DANGER   GREEN: RELAY",410,568,.20f,cyan);
    if(creature_)text("CHAIN SPEED "+std::to_string(static_cast<int>(p.chainDrive*100))+"%",24,85,.22f,gold);
    text(p.action==Action::Collapsed?"REFORMING - DODGE LOCKED":p.cooldown>0?"DODGE RECHARGING":"SHIFT - CANCEL / DODGE",487,594,.18f,cyan);
    if(p.landingAge<.6f)text("LANDED - STABILIZING",515,495,.25f,cyan);
    if(p.instability>75){float pulse=.5f+.5f*std::sin(p.stats.seconds*10);
        rect(600,460,80,3,{1,.45f,.1f,pulse});}
    if(p.action==Action::Collapsed)text("UNSTABLE!  REFORMING",480,430,0.47f,{1,0.25f,0.25f,1});
    // 照準と、狙っている対象の枠 (負担が大きすぎるなら赤)
    if(p.aiming||p.automatic){
        auto color=r.valid(p.preview)?cyan:Engine::Vector4{0.65f,0.7f,0.75f,1};
        rect(631,359,7,2,color);rect(643,359,7,2,color);rect(639,351,2,7,color);rect(639,363,2,7,color);
        if(r.valid(p.preview)&&r.get<Target>(p.preview).active){
            V c=Center(r,p.preview);float cost=StrainCost(p.mass,Length(c-Read(r.get<TransformComponent>(player_).translate)));
            bool unsafe=p.instability+cost>=100;auto col=unsafe?Engine::Vector4{1,0.25f,0.15f,1}:cyan;
            using namespace DirectX;auto v=XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj());
            XMFLOAT4 clip;XMStoreFloat4(&clip,v);
            if(clip.w>0){float x=(clip.x/clip.w*0.5f+0.5f)*w/s,y=(-clip.y/clip.w*0.5f+0.5f)*h/s;
                rect(x-20,y-20,40,2,col);rect(x-20,y+18,40,2,col);rect(x-20,y-20,2,40,col);rect(x+18,y-20,2,40,col);}
            text(unsafe?"OVERLOAD RISK":p.action==Action::Pulling?"PULLING":r.get<Target>(p.preview).kind==Kind::Anchor?"SAFE RELAY":r.all_of<Feather>(p.preview)?(r.get<Feather>(p.preview).fast?"SHORTCUT - EXPIRES SOON":"STABLE FEATHER"):"GRAB",590,392,0.25f,col);
        }
    }
    // 鎖が失敗した理由を表示する
    if(p.failureTime>0){
        const char* reason="";
        switch(p.failure){
        case ChainFailure::NoTarget:reason="NO TARGET - AIM AT AN ORB";break;
        case ChainFailure::OutOfRange:reason="OUT OF REACH - MOVE CLOSER";break;
        case ChainFailure::Obstructed:reason="BLOCKED - CHANGE ANGLE";break;
        case ChainFailure::Recovering:reason=p.buffer>0?"QUEUED - RELEASE LMB TO CANCEL":"RECOVERING - SHIFT TO DODGE";break;
        case ChainFailure::Unstable:reason="UNSTABLE - WAIT TO REFORM";break;
        case ChainFailure::Missed:reason="MISSED - AIM AT THE TARGET";break;
        case ChainFailure::Fell:reason="FALL - RETURNED TO LAST LANDING";break;
        default:break;
        }
        text(reason,505,419,.22f,gold);
    }
    // 遮られている対象に印を付ける
    if(r.valid(p.blockedPreview)){
        V c=Center(r,p.blockedPreview);using namespace DirectX;XMFLOAT4 clip;
        XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(c.x,c.y,c.z,1),ctx.camera->View()*ctx.camera->Proj()));
        if(clip.w>0){float x=(clip.x/clip.w*.5f+.5f)*w/s,y=(-clip.y/clip.w*.5f+.5f)*h/s;
            for(int i=0;i<7;++i){float q=i*5.f-15;rect(x+q,y+q,3,3,gold);rect(x+q,y-q,3,3,gold);}
            text("BLOCKED",x-28,y+23,.18f,gold);}
    }
    // 被弾の方向を赤い矢印で示す
    if(p.damageAge<.6f){
        V d=Unit(p.damageSource-Read(r.get<TransformComponent>(player_).translate));
        float x=Dot(d,V{std::cos(yaw_),0,-std::sin(yaw_)}),y=-Dot(d,Forward(yaw_,0));
        float len=std::sqrt(x*x+y*y);if(len>.01f){x/=len;y/=len;}
        auto red=Engine::Vector4{1,.18f,.12f,1-p.damageAge/.6f};
        for(int i=0;i<7;++i){float q=i-3.f;
            rect(640+x*(79-std::abs(q)*3)-y*q*3,360+y*(79-std::abs(q)*3)+x*q*3,4,4,red);}
        text(p.damageReason,534,469,.25f,red);
    }
}
} // namespace Game
// 本編の処理 (インク・ムービー・輪の戦闘・スライムの生活・消火栓・街)
#include "ChronoInk.inl"
#include "ChronoCinematic.inl"
#include "SlimeAssault.inl"

#include "SlimeLife.inl"
#include "SlimeHydrant.inl"
#include "CityStage.inl"
