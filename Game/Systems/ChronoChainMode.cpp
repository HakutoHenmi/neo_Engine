// ChronoChainMode.cpp
// 旧来の確認用のモード: 鎖で敵を引き寄せる戦闘とアリーナ。ステージの組み立て、鎖の狙い・発射・命中、
// 肩越しのカメラ、プレイヤー・ボス・ワールドの更新。
#include "ChronoSystemInternal.h"

namespace Game {

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

} // namespace Game
