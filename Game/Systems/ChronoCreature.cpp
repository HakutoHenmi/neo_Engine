// ChronoCreature.cpp
// 旧来の確認用のモード: 生き物のボス (蛇と鳥) のステージ、各部位の姿勢、攻撃の衝撃、蛇の行動。
#include "ChronoSystemInternal.h"

namespace Game {

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

} // namespace Game
