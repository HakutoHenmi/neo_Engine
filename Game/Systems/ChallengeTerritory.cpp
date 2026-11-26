// ChallengeTerritory.cpp
// 変形チャレンジ「スライム陣取り」(ChronoSystem の一部。規則は Chrono/ChallengeRules.h)。
//  - 専用ステージ「ひろい子ども部屋」: 戦う場所の床の上にラグ・縁・奥と左右の壁・家具を置く。ほかのチャレンジでは隠す
//  - ラグは ChallengeTerritoryCells 四方の 1 x 1 (unit) のマスに分けて、どこをぬるか (近い順・量) を決める。家具の下は陣地に数えない
//  - 見た目と陣地の割合はインクマップ (スプラトゥーンと同じ考え方): 床全体を覆う量の地図に、しぶきの形を押していく。
//    床のシェーダー (InkFloorPS) が量をしきい値で切って液体の縁を作り、量の傾きで盛り上がりとつやを出す。割合は地図の点の数で数える
//  - スライムの体液でマスを陣地にする。能力の当たり (衝撃) の形の中の床を、当たりの中心に近い順にぬる (EmitBattleImpact から呼ぶ)。
//    1 回の能力でぬれる量は上限と下限でそろえる (形だけが能力ごとに違う)。飛んだ物は通った下と落ちた所、熱いお茶は外へじわじわ広がる。歩いた跡も細く残る
#include "ChronoSystemInternal.h"
#include "CityBuilder.h"

namespace Game {
namespace {
constexpr int N=ChallengeTerritoryCells;
// 能力 1 回分の体液が終わるまでの秒 (飛んだ物・置いた物が終わるまで待つ)。終わったときに下限に足りなければ足す
float TerritorySettle(BattleAction a){
    switch(a){
    case BattleAction::Throw:return 3.2f;case BattleAction::Water:return BattleActionTime(a)+1.f;case BattleAction::Marbles:return 3.f;
    case BattleAction::Tower:return BattleTowerLife+.3f;case BattleAction::Domino:return BattleDominoStart+BattleDominoStep*float(BattleDominoCount)+.6f;
    case BattleAction::Decoy:return BattleDecoyLife+.2f;case BattleAction::Smell:return 2.f;case BattleAction::Tea:return BattleBurnTime+.2f;
    case BattleAction::Coin:return BattleCoinLife+.1f;case BattleAction::Disc:return BattleDiscLife+.1f;case BattleAction::Bounce:return BattleBounceLife+.1f;
    case BattleAction::Homing:return BattleHomingLife+.1f;case BattleAction::Fan:return 1.6f;case BattleAction::Pierce:return BattlePierceLife+.2f;case BattleAction::Sticky:return 2.2f;
    default:return BattleActionTime(a)+.6f;}
}
constexpr int W=ChallengeTerritoryInk;
// しぶきの乱数 (決まった種から。毎回の見た目は変わる)
uint32_t InkRandom(uint32_t& seed){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
float InkUnit(uint32_t& seed){return float(InkRandom(seed)&0xffffff)/float(0x1000000);}
// ラグ (無地と、縁の近くの帯) と縁・壁の色
constexpr Vec kRugLight{.93f,.89f,.80f},kRugDark{.88f,.83f,.73f},kRugEdge{.62f,.45f,.30f},kWall{.96f,.93f,.86f},kBaseboard{.78f,.66f,.52f};
}

// 専用ステージを作る (戦う場所の床ができたあと 1 回だけ)。見せるのはスライム陣取りのときだけで、ほかのチャレンジでは戦う円の目印を見せる
void ChronoSystem::ShowChallengeStage(entt::registry& r){
    const bool territory=!battle_.legacy&&battle_.challenge==ChallengeKind::Territory;
    bool built=!territoryStage_.empty();for(auto e:territoryStage_)if(!r.valid(e))built=false;
    if(territory&&!built){
        territoryStage_.clear();const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;
        auto make=[&](const char* name,const std::vector<std::array<V,2>>& list,V color){CityParts::CityBuilder builder;for(const auto& box:list)builder.Box(CityParts::CityMetal,box[0],box[1]);auto& g=builder.geometry[CityParts::CityMetal];
            auto e=r.create();r.emplace<NameComponent>(e,name);r.emplace<TransformComponent>(e).scale={1,1,1};
            auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=Engine::Renderer::GetInstance()->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;
            mr.shaderName="LowpolySurface";mr.useCubemap=false;mr.color={color.x,color.y,color.z,1};territoryStage_.push_back(e);};
        // ラグ: 無地の薄い板と、縁の近くの 1 本の帯 (マス目は見せない)。上の面は床とほぼ同じ高さ (体と足元のゲージが埋まらないように)
        {const float top=.006f,band=u*.8f,in=u*1.2f,lift=.006f;make("Chrono Territory rug",{{c+V{-h,-u*.05f,-h},c+V{h,top,h}}},kRugLight);
            make("Chrono Territory rug band",{{c+V{-h+in,top,-h+in},c+V{h-in,top+lift,-h+in+band}},{c+V{-h+in,top,h-in-band},c+V{h-in,top+lift,h-in}},
                {c+V{-h+in,top,-h+in+band},c+V{-h+in+band,top+lift,h-in-band}},{c+V{h-in-band,top,-h+in+band},c+V{h-in,top+lift,h-in-band}}},kRugDark);}
        // 体液の板: ラグのすぐ上に 1 枚 (足元のゲージより低い。uv の u が +x、v が +z。インクマップと同じ並び)
        {const float y=c.y+.018f;auto q=[&](float x,float z,float tu,float tv){Engine::VertexData v{};v.position={x,y,z,1};v.texcoord={tu,tv};v.normal={0,1,0};return v;};
            territoryInkMesh_=Engine::Renderer::GetInstance()->CreateDynamicMesh({q(c.x-h,c.z-h,0,0),q(c.x+h,c.z-h,1,0),q(c.x+h,c.z+h,1,1),q(c.x-h,c.z+h,0,1)},{0,2,1,0,3,2});}
        // ラグの縁 (少し高い木の枠)
        {const float w=u*.4f,y=u*.25f;make("Chrono Territory rug edge",{{c+V{-h-w,0,-h-w},c+V{h+w,y,-h}},{c+V{-h-w,0,h},c+V{h+w,y,h+w}},{c+V{-h-w,0,-h},c+V{-h,y,h}},{c+V{h,0,-h},c+V{h+w,y,h}}},kRugEdge);}
        // 部屋の壁 (奥と左右) と幅木
        {const float g=ChallengeTerritoryWallGap*u,wall=ChallengeTerritoryWall*u,t=u*.5f,e=h+g;
            // 壁は 1 枚ずつ作り、カメラと体の間に入ったら薄くする (広い部屋の端でカメラが壁の外へ出ても、体が見えるように)
            const std::array<std::array<V,2>,3> walls{{{c+V{-e-t,0,e},c+V{e+t,wall,e+t}},{c+V{-e-t,0,-e},c+V{-e,wall,e}},{c+V{e,0,-e},c+V{e+t,wall,e}}}};
            for(const auto& w:walls){make("Chrono Territory wall",{w},kWall);r.emplace<CameraOccluder>(territoryStage_.back(),(w[0]+w[1])*.5f,w[1]-w[0],1.f);}
            const float bb=u*.6f,in=u*.12f;make("Chrono Territory baseboard",{{c+V{-e,0,e-in},c+V{e,bb,e}},{c+V{-e,0,-e},c+V{-e+in,bb,e}},{c+V{e-in,0,-e},c+V{e,bb,e}}},kBaseboard);}
        // 家具 (飲めない置物。足元の四角は BuildTerritory が同じ計算で求める)
        for(const auto& f:ChallengeTerritoryFurniture){const auto& m=LowModelOf(f.model);const float scale=LowScaleFor(f.model,f.width*u),cs=std::cos(f.yaw),sn=std::sin(f.yaw);
            const V mid=(m.lo+m.hi)*.5f,offset{(mid.x*cs+mid.z*sn)*scale,0,(-mid.x*sn+mid.z*cs)*scale};
            territoryStage_.push_back(LowProp(r,f.model,c+V{f.x*u,0,f.z*u}-offset,scale,f.yaw,0));}}
    auto show=[&](const std::vector<entt::entity>& list,bool on){for(auto e:list)if(r.valid(e))if(auto* mr=r.try_get<MeshRendererComponent>(e))mr->enabled=on;};
    show(territoryStage_,territory);show(battleArenaDecor_,!territory);
}

// マスを作る: 全部を床にし、家具の足元の四角に真ん中が入るマスを家具にする
void ChronoSystem::BuildTerritory(){
    auto& t=battle_.territory;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;
    t.cells.assign(size_t(N*N),TerritoryCell::Floor);t.age.assign(size_t(N*N),0.f);t.owned=t.spread=0;t.furniture.clear();
    for(const auto& f:ChallengeTerritoryFurniture){const auto& m=LowModelOf(f.model);const float scale=LowScaleFor(f.model,f.width*u);const V size=(m.hi-m.lo)*scale;
        const bool turned=std::abs(std::sin(f.yaw))>.5f;t.furniture.push_back({c.x+f.x*u,c.z+f.z*u,(turned?size.z:size.x)*.5f,(turned?size.x:size.z)*.5f});}
    for(int z=0;z<N;++z)for(int x=0;x<N;++x){const float px=c.x-h+(float(x)+.5f)*u,pz=c.z-h+(float(z)+.5f)*u;
        for(const auto& f:t.furniture)if(std::abs(px-f[0])<f[2]&&std::abs(pz-f[1])<f[3]){t.cells[size_t(z*N+x)]=TerritoryCell::Furniture;break;}}
    t.open=int(std::count(t.cells.begin(),t.cells.end(),TerritoryCell::Floor));
    // インクマップ: 全部乾いた状態。数える点は、真ん中が家具の足元に入らない点
    t.ink.assign(size_t(W*W),0);t.wet.assign(size_t(W*W),0);t.anyWet=false;t.growing.clear();t.flights.clear();t.inkOpen.assign(size_t(W*W),1);t.inkCovered=0;t.inkDirty=true;t.inkSeed=0x9e3779b9u;
    for(int z=0;z<W;++z)for(int x=0;x<W;++x){const float px=c.x-h+(float(x)+.5f)*(2*h/W),pz=c.z-h+(float(z)+.5f)*(2*h/W);
        for(const auto& f:t.furniture)if(std::abs(px-f[0])<f[2]&&std::abs(pz-f[1])<f[3]){t.inkOpen[size_t(z*W+x)]=0;break;}}
    t.inkOpenCount=int(std::count(t.inkOpen.begin(),t.inkOpen.end(),uint8_t(1)));
    // 取り返すもの: ロボは隅から、扇風機は部屋の手前の真ん中 (始める所) の方を向いて置く
    t.reclaim=true;t.robot={};t.robot.at=c+V{RobotStartX*u,0,RobotStartZ*u};t.robot.dir=Unit(V{1,0,1});t.robot.turn=.5f;
    t.fan={};t.fan.at=c+V{FanX*u,0,FanZ*u};{const V to=c+V{0,0,ChallengeTerritoryStartZ*u*.6f}-t.fan.at;t.fan.face=std::atan2(to.x,to.z);}
}

// 体液を消す: 半径の中の量を 0 にする (縁はなめらかに)。半分を下回った点は陣地から外し、マスもまたぬれるように戻す
void ChronoSystem::EraseTerritoryInk(V at,float radius){
    auto& t=battle_.territory;if(t.ink.empty())return;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;const float step=2*h/W;const int per=W/N;
    const float reach=radius+step*2;
    const int x0=std::max(0,int((at.x-reach-(c.x-h))/step)),x1=std::min(W-1,int((at.x+reach-(c.x-h))/step));
    const int z0=std::max(0,int((at.z-reach-(c.z-h))/step)),z1=std::min(W-1,int((at.z+reach-(c.z-h))/step));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){auto& cell=t.ink[size_t(z*W+x)];if(!cell)continue;
        const float dx=c.x-h+(float(x)+.5f)*step-at.x,dz=c.z-h+(float(z)+.5f)*step-at.z,d=std::sqrt(dx*dx+dz*dz);if(d>reach)continue;
        const uint8_t keep=uint8_t(255*std::clamp((d-radius)/(step*2)+.5f,0.f,1.f));if(keep>=cell)continue;
        if(cell>=128&&keep<128){if(t.inkOpen[size_t(z*W+x)])--t.inkCovered;auto& owned=t.cells[size_t((z/per)*N+x/per)];if(owned==TerritoryCell::Owned)owned=TerritoryCell::Floor;}
        cell=keep;t.wet[size_t(z*W+x)]=0;t.inkDirty=true;}
}

// 扇の形に体液を消す (at から yaw の向きへ reach まで。半分の角度の cos が cone)
void ChronoSystem::EraseTerritoryCone(V at,float yaw,float reach,float cone){
    auto& t=battle_.territory;if(t.ink.empty()||reach<=0)return;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;const float step=2*h/W;const int per=W/N;
    const V f{std::sin(yaw),0,std::cos(yaw)};
    const int x0=std::max(0,int((at.x-reach-(c.x-h))/step)),x1=std::min(W-1,int((at.x+reach-(c.x-h))/step));
    const int z0=std::max(0,int((at.z-reach-(c.z-h))/step)),z1=std::min(W-1,int((at.z+reach-(c.z-h))/step));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){auto& cell=t.ink[size_t(z*W+x)];if(!cell)continue;
        const float dx=c.x-h+(float(x)+.5f)*step-at.x,dz=c.z-h+(float(z)+.5f)*step-at.z,d=std::sqrt(dx*dx+dz*dz);if(d>reach||d<.01f)continue;
        if((dx*f.x+dz*f.z)/d<cone)continue;
        if(cell>=128){if(t.inkOpen[size_t(z*W+x)])--t.inkCovered;auto& owned=t.cells[size_t((z/per)*N+x/per)];if(owned==TerritoryCell::Owned)owned=TerritoryCell::Floor;}
        cell=0;t.wet[size_t(z*W+x)]=0;t.inkDirty=true;}
}

// 扇風機の今の向き (首振りと、当てられたずれ)
float ChronoSystem::FanYaw()const{const auto& f=battle_.territory.fan;return f.face+std::sin(f.phase*6.2831853f/FanPeriod)*FanSweep+f.offset;}

// 取り返すものを進める
void ChronoSystem::TickReclaimers(float dt){
    auto& t=battle_.territory;if(!t.reclaim)return;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;
    // お掃除ロボ
    {auto& r=t.robot;const float R=u*RobotRadius;
        r.slow=std::max(0.f,r.slow-dt);r.push=r.push*std::exp(-4*dt);r.spin+=dt*(r.stop>0?1.f:6.f);
        auto blocked=[&](V p){if(std::abs(p.x-c.x)>h-R||std::abs(p.z-c.z)>h-R)return true;for(const auto& f:t.furniture)if(std::abs(p.x-f[0])<f[2]+R*.8f&&std::abs(p.z-f[1])<f[3]+R*.8f)return true;return false;};
        V move=r.push*dt;
        if(r.stop>0)r.stop-=dt;
        else{r.turn-=dt;
            if(r.turn<=0){
                // 曲がる: 前方の体液の多い向きへ (今の向きを少しひいきし、少しばらつかせる)。体液が無ければ気ままに
                const int step=W/N;float best=-1e9f;V pick=r.dir;
                for(int k=0;k<12;++k){const float a=6.2831853f*float(k)/12+InkUnit(seed)*.2f;const V d{std::sin(a),0,std::cos(a)};float score=Dot(d,r.dir)*.6f+InkUnit(seed)*1.2f;
                    for(float s=2;s<=RobotLook;s+=1){const V p=r.at+d*(s*u);if(blocked(p)){score-=s<3?100.f:0.f;break;}
                        const int ix=std::clamp(int((p.x-(c.x-h))/(2*h)*W),0,W-1),iz=std::clamp(int((p.z-(c.z-h))/(2*h)*W),0,W-1);score+=float(t.ink[size_t(iz*W+ix)])/255.f;}
                    if(score>best){best=score;pick=d;}}
                r.dir=pick;r.turn=RobotTurnEvery*(.7f+.6f*InkUnit(seed));(void)step;}
            move=move+r.dir*(u*RobotSpeed*(r.slow>0?RobotSlow:1.f)*dt);}
        const V want=r.at+move;V next=want;
        // 部屋の端と家具で止まる (止まったらすぐ曲がり直す)
        next.x=std::clamp(next.x,c.x-h+R,c.x+h-R);next.z=std::clamp(next.z,c.z-h+R,c.z+h-R);
        for(const auto& f:t.furniture){const float nx=std::clamp(next.x,f[0]-f[2],f[0]+f[2]),nz=std::clamp(next.z,f[1]-f[3],f[1]+f[3]);V d{next.x-nx,0,next.z-nz};const float len=Length(d);
            if(len<R&&len>1e-4f)next=V{nx,0,nz}+d*(R/len);else if(len<=1e-4f)next=r.at;}
        // スライムの体とは重ならない (体に当たったら押し戻されて、向きを変える)
        {const auto& body=life_.bodies[0];const float BR=DevourBodyRadius(body.mass)*1.05f+R;V d{next.x-body.at.x,0,next.z-body.at.z};const float len=Length(d);
            if(len<BR){d=len>1e-3f?d*(1/len):V{-r.dir.x,0,-r.dir.z};next=V{body.at.x,0,body.at.z}+d*BR;if(Dot(r.dir,d)<0)r.dir=Unit(r.dir-d*(2*Dot(r.dir,d)));r.turn=std::min(r.turn,.8f);}}
        next.y=c.y;if(Length(V{next.x-want.x,0,next.z-want.z})>u*.05f&&r.stop<=0)r.turn=std::min(r.turn,.15f);
        r.at=next;EraseTerritoryInk(r.at,u*RobotWipe);}
    // 首振り扇風機
    {auto& f=t.fan;f.offset*=std::exp(-.5f*dt);
        if(f.stop>0)f.stop-=dt;else{f.phase+=dt;f.clock+=dt;}
        f.spin+=dt*(f.blow>=0?40.f:f.stop>0?2.f:(f.clock>FanEvery-1?24.f:12.f));
        if(f.clock>=FanEvery&&f.blow<0){f.clock=0;f.blow=0;
            // 前に積み木の塔が立っていれば、この 1 回の風を受け止める
            f.blocked=false;const float yaw=FanYaw();const V dir{std::sin(yaw),0,std::cos(yaw)};
            for(const auto& pl:battle_.placed)if(pl.kind==BattleAction::Tower&&pl.age<pl.life){V d=pl.at-f.at;d.y=0;const float len=Length(d);if(len<u*FanReach&&len>.01f&&Dot(d*(1/len),dir)>=FanCone*.9f)f.blocked=true;}}
        if(f.blow>=0){f.blow+=dt;const float k=std::min(1.f,f.blow/FanBlowTime);if(!f.blocked)EraseTerritoryCone(f.at,FanYaw(),u*FanReach*k,FanCone);if(f.blow>FanBlowTime)f.blow=-1;}}
}

// 衝撃を受ける: お掃除ロボ (倒れない。はじかれる・止まる・遅くなる・引き寄せられる)
bool ChronoSystem::ApplyImpactToRobot(const BattleImpact& i){
    auto& r=battle_.territory.robot;const float u=battle_.unit;
    V away=i.radial||i.effect==ImpactEffect::PushOut?r.at-i.at:i.dir;away.y=0;away=Length(away)>.01f?Unit(away):V{1,0,0};
    switch(i.effect){
    case ImpactEffect::Hit:case ImpactEffect::PushOut:r.push=away*(u*RobotPush*std::clamp(i.push/4.f,.5f,2.f));r.dir=away;r.turn=.6f;return true;
    case ImpactEffect::Wall:r.push=away*(u*RobotPush*.5f);r.dir=away;r.turn=.4f;return true;
    case ImpactEffect::Pull:{V to=i.at-r.at;to.y=0;if(Length(to)>.01f){r.dir=Unit(to);r.turn=1.5f;r.push=r.dir*(u*RobotPush*.5f);}return true;}
    case ImpactEffect::Stun:case ImpactEffect::Capture:r.stop=std::max(r.stop,i.seconds>0?i.seconds:RobotStun);return true;
    case ImpactEffect::Weaken:case ImpactEffect::Burn:case ImpactEffect::Zone:r.slow=std::max(r.slow,RobotSlowTime);return true;
    default:return false;}
}

// 衝撃を受ける: 首振り扇風機 (止まる・首の向きがずれる)
bool ChronoSystem::ApplyImpactToFan(const BattleImpact& i){
    auto& f=battle_.territory.fan;
    switch(i.effect){
    case ImpactEffect::Stun:case ImpactEffect::Capture:f.stop=std::max(f.stop,i.seconds>0?std::min(i.seconds,FanStun*2):FanStun);return true;
    case ImpactEffect::Hit:case ImpactEffect::PushOut:{const float yaw=FanYaw();const V side{std::cos(yaw),0,-std::sin(yaw)};V d=f.at-i.at;d.y=0;f.offset+=(Dot(d,side)>=0?1.f:-1.f)*FanNudge;
        f.offset=std::clamp(f.offset,-FanSweep,FanSweep);return true;}
    default:return false;}
}

// 取り返すものを描く (ロボは平たい円盤、扇風機は台・柱・頭・羽根)
void ChronoSystem::DrawReclaimers(GameContext& ctx){
    const auto& t=battle_.territory;if(!t.reclaim||cube_==0||sphere_==0)return;const float u=battle_.unit;const V c=battle_.arenaAt;
    auto draw=[&](uint32_t mesh,V at,V scale,V rotate,Engine::Vector4 col){Engine::Transform tr;tr.translate=EV(at);tr.scale=EV(scale);tr.rotate=EV(rotate);
        ctx.renderer->DrawMeshInstanced(mesh,white_,tr,Engine::Vector4{std::pow(col.x,2.2f),std::pow(col.y,2.2f),std::pow(col.z,2.2f),1});};
    {const auto& r=t.robot;const float R=u*RobotRadius,yaw=std::atan2(r.dir.x,r.dir.z);const V base{r.at.x,c.y,r.at.z};
        draw(sphere_,base+V{0,R*.18f,0},{R,R*.2f,R},{0,yaw,0},{.24f,.26f,.3f,1});
        draw(sphere_,base+V{0,R*.33f,0},{R*.8f,R*.08f,R*.8f},{0,yaw,0},{.5f,.53f,.58f,1});
        // 前のバンパーと、止まっている間は赤・遅い間は黄色に光るランプ
        draw(cube_,base+r.dir*(R*.82f)+V{0,R*.18f,0},{R*.5f,R*.1f,R*.08f},{0,yaw,0},{.15f,.15f,.17f,1});
        const Engine::Vector4 lamp=r.stop>0?Engine::Vector4{1,.25f,.2f,1}:r.slow>0?Engine::Vector4{1,.85f,.2f,1}:Engine::Vector4{.3f,.85f,1,1};
        draw(sphere_,base+V{0,R*.42f,0}+r.dir*(R*.35f),{R*.12f,R*.06f,R*.12f},{0,0,0},lamp);
        // 回るブラシ
        for(int k=0;k<2;++k){const float a=r.spin*(k?-1.f:1.f);const V side{r.dir.z,0,-r.dir.x};draw(cube_,base+r.dir*(R*.55f)+side*(R*.7f*(k?-1.f:1.f))+V{0,R*.05f,0},{R*.3f,R*.02f,R*.04f},{0,a,0},{.85f,.85f,.8f,1});}}
    {const auto& f=t.fan;const float yaw=FanYaw();const V base{f.at.x,c.y,f.at.z},dir{std::sin(yaw),0,std::cos(yaw)};const float H=u*2.6f,S=u*1.1f;
        draw(sphere_,base+V{0,u*.12f,0},{u*1.1f,u*.14f,u*1.1f},{0,0,0},{.94f,.94f,.92f,1});
        draw(cube_,base+V{0,H*.5f,0},{u*.12f,H*.5f,u*.12f},{0,0,0},{.9f,.9f,.88f,1});
        const V head=base+V{0,H,0};
        draw(sphere_,head-dir*(S*.25f),{S*.45f,S*.45f,S*.3f},{0,yaw,0},{.92f,.92f,.9f,1});
        // 羽根 (首の向きの面で回る。送風の前は速くなる) と、前のガード
        for(int k=0;k<3;++k){const float a=f.spin+2.0944f*float(k);const V right{std::cos(yaw),0,-std::sin(yaw)};const V arm=(right*std::cos(a)+V{0,1,0}*std::sin(a))*(S*.45f);
            draw(sphere_,head+dir*(S*.05f)+arm,{S*.32f,S*.32f,S*.04f},{0,yaw,0},f.blocked&&f.blow>=0?Engine::Vector4{.7f,.7f,.7f,1}:Engine::Vector4{.55f,.8f,.98f,1});}
        draw(sphere_,head+dir*(S*.12f),{S*.95f,S*.95f,S*.05f},{0,yaw,0},{.85f,.86f,.88f,1});
        draw(sphere_,head+dir*(S*.16f),{S*.15f,S*.15f,S*.06f},{0,yaw,0},f.stop>0?Engine::Vector4{1,.3f,.25f,1}:Engine::Vector4{.3f,.6f,.95f,1});}
}

// 丸いしぶきを 1 つ押す: 縁は角度ごとに波打つ (3・5・9 の山)。量は大きい方を残す。半分を越えた点を数える
void ChronoSystem::StampTerritoryInk(V at,float radius,uint32_t seed){
    auto& t=battle_.territory;if(t.ink.empty()||radius<=0)return;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;const float step=2*h/W;
    const float p1=InkUnit(seed)*6.283f,p2=InkUnit(seed)*6.283f,p3=InkUnit(seed)*6.283f,a1=.08f+InkUnit(seed)*.1f,a2=.05f+InkUnit(seed)*.06f;
    const float reach=radius*(1+a1+a2+.06f)+step*2;
    const int x0=std::max(0,int((at.x-reach-(c.x-h))/step)),x1=std::min(W-1,int((at.x+reach-(c.x-h))/step));
    const int z0=std::max(0,int((at.z-reach-(c.z-h))/step)),z1=std::min(W-1,int((at.z+reach-(c.z-h))/step));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){
        const float dx=c.x-h+(float(x)+.5f)*step-at.x,dz=c.z-h+(float(z)+.5f)*step-at.z,d=std::sqrt(dx*dx+dz*dz);if(d>reach)continue;
        const float a=std::atan2(dz,dx),edge=radius*(1+a1*std::sin(3*a+p1)+a2*std::sin(5*a+p2)+.06f*std::sin(9*a+p3));
        // 縁で量が半分 (シェーダーのしきい値)。内側へ 1.5 点ぶんで満ちる
        const float v=std::clamp(.5f+(edge-d)/(step*3),0.f,1.f);const uint8_t value=uint8_t(v*255);
        auto& cell=t.ink[size_t(z*W+x)];if(value<=cell)continue;
        if(cell<128&&value>=128&&t.inkOpen[size_t(z*W+x)])++t.inkCovered;
        cell=value;t.wet[size_t(z*W+x)]=255;t.anyWet=t.inkDirty=true;}
}

// しぶきを広がるように押す: 形の種はそのままで、小さく出てから広がる (TickTerritory が毎フレーム押し直す)
void ChronoSystem::GrowTerritoryInk(V at,float radius,uint32_t seed){
    auto& t=battle_.territory;if(t.ink.empty())return;uint32_t& s=t.inkSeed;
    t.growing.push_back({at,radius,seed,0,ChallengeTerritoryGrowLow+(ChallengeTerritoryGrowHigh-ChallengeTerritoryGrowLow)*InkUnit(s)});
}

// マス 1 つ分のしぶき: 真ん中を少しずらし、大きさもばらばらにする。
// launch があれば、型ごとの所から流体の粒子のしずくを飛ばし、落ちたときにしぶきにする (近ければ飛ばさずその場で広げる)
void ChronoSystem::SplatTerritoryCell(int index,const InkLaunch* launch){
    auto& t=battle_.territory;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;
    const V center=c+V{-h+(float(index%N)+.5f)*u,0,-h+(float(index/N)+.5f)*u};
    const float j=ChallengeTerritorySplatJitter*u;
    LaunchTerritoryInk(center+V{(InkUnit(seed)*2-1)*j,0,(InkUnit(seed)*2-1)*j},launch);
}

// しずく 1 つ: at に落ちるしぶき。launch があれば、型ごとの所から流体の粒子のしずくを飛ばし、落ちたときにしぶきにする (近ければ飛ばさずその場で広げる)
void ChronoSystem::LaunchTerritoryInk(V at,const InkLaunch* launch){
    auto& t=battle_.territory;const float u=battle_.unit;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;
    const float radius=u*(ChallengeTerritorySplatLow+(ChallengeTerritorySplatHigh-ChallengeTerritorySplatLow)*InkUnit(seed));
    const uint32_t shape=InkRandom(seed);const bool drops=InkUnit(seed)<ChallengeTerritoryDropChance;
    if(!launch){LandTerritorySplat(at,radius,shape,drops);return;}
    // 型ごとに、しずくを出す所 (from)・飛び出すまでの秒 (delay)・粒子を飛ばすか (fly) を決める
    const auto& body=life_.bodies[0];const float R=DevourBodyRadius(body.mass),floor=c.y;
    V toward=at-body.at;toward.y=0;toward=Length(toward)>.01f?Unit(toward):launch->dir;
    V from{launch->origin.x,std::max(launch->origin.y,floor+u),launch->origin.z};float delay=0;bool fly=true;
    float minTime=0;
    switch(launch->style){
    case InkStyle::Lay:{
        // 転がし: 真下にそのまま広がる。一部は車輪 (体の後ろ) から、後ろと横へ巻き上げる (前の落ちる所は後ろへ折り返す)
        fly=false;if(InkUnit(seed)<InkTailShare&&launch->action!=BattleAction::Marbles){V d=at-body.at;d.y=0;const float ahead=Dot(d,launch->dir);
            if(ahead>0)at=at-launch->dir*(2*ahead);from=body.at-launch->dir*(R*.8f);from.y=floor+u*.3f;fly=true;minTime=InkCrownTime*.8f;}
        break;}
    case InkStyle::Burst:{
        // 炸裂: 当たった所 (叩くはハンマーの頭) の床すれすれから、王冠の形に山なりに跳ねる
        V center=launch->origin;if(launch->action==BattleAction::Smash)center=body.at+launch->dir*(R*InkHammerReach);
        from=center;from.y=floor+u*InkCrownHeight;minTime=InkCrownTime;break;}
    case InkStyle::Topple:{
        // 崩れる: 倒れる物の上の方から、倒れた向きへ降りかかる
        from=launch->origin;from.y=floor+u*InkToppleHeight*(.35f+.65f*InkUnit(seed));from=from-launch->dir*(u*.5f);
        V d=at-launch->origin;d.y=0;delay=Length(d)/(u*InkLineSpeed);break;}
    case InkStyle::Swing:{from=body.at+toward*(R*1.4f);from.y=floor+u*.9f;
        // 振った向きの順: 狙いの左から右へ (角度 -90〜90 度を InkSwingTime 秒で)
        const V side{launch->dir.z,0,-launch->dir.x};delay=std::clamp(.5f+.5f*Dot(toward,side),0.f,1.f)*InkSwingTime;break;}
    case InkStyle::Spray:{from=body.at+launch->dir*(R*1.3f);from.y=floor+u*.7f;V d=at-from;d.y=0;delay=Length(d)/(u*InkLineSpeed);break;}
    case InkStyle::Drip:from=launch->origin;from.y=std::max(from.y,floor+u*.5f);break;
    case InkStyle::Line:{V d=at-body.at;d.y=0;from=at-launch->dir*(u*InkLineHop*(.7f+.6f*InkUnit(seed)));from.y=floor+u*InkLineHeight*(.6f+.8f*InkUnit(seed));delay=Length(d)/(u*InkLineSpeed);break;}
    case InkStyle::Spin:{const float a=-.9f,cs=std::cos(a),sn=std::sin(a);const V lag{toward.x*cs-toward.z*sn,0,toward.x*sn+toward.z*cs};from=body.at+lag*(R*1.1f);from.y=floor+u*.7f;break;}
    case InkStyle::Pool:{fly=false;V d=at-launch->origin;d.y=0;delay=Length(d)/(u*InkPoolSpeed);
        // 色ぬり: 一部は前へ大きな塊で飛ぶ (チューブが破裂する)。熱いお茶: はじめは体の前の高い所から注ぐ
        if(launch->action==BattleAction::Paint&&InkUnit(seed)<InkBurstShare){fly=true;from=body.at+launch->dir*R;from.y=floor+u*.6f;delay=0;minTime=InkCrownTime;}
        if(launch->action==BattleAction::Tea&&InkUnit(seed)<InkPourShare){fly=true;from=body.at+launch->dir*(R*1.2f);from.y=floor+u*InkPourHeight;delay*=.5f;}
        break;}
    case InkStyle::Rain:from=at+V{0,0,0};from.y=floor+u*InkRainHeight;delay=InkUnit(seed)*InkRainTime;break;
    case InkStyle::Pulse:from=body.at+toward*R;from.y=floor+u*.7f;minTime=InkCrownTime*.8f;break;
    case InkStyle::Gather:{V out=at-launch->origin;out.y=0;out=Length(out)>.01f?Unit(out):V{1,0,0};from=at+out*(u*InkGatherReach);from.y=floor+u*InkGatherHeight;delay=InkUnit(seed)*.3f;break;}
    default:break;}
    if(launch->action==BattleAction::Ou){from=body.at+toward*R;from.y=floor+u*.5f;V d=at-body.at;d.y=0;delay=Length(d)/(u*InkWaveSpeed);minTime=InkCrownTime*.7f;}
    // 近すぎる所は飛ばさずに広げる (雨は真上から落とすので距離を見ない)
    // (王冠・崩れる・巻き上げ・注ぐは、近くても山なりに跳ねるので飛ばす)
    {V d=at-from;d.y=0;if(launch->style!=InkStyle::Rain&&minTime<=0&&launch->style!=InkStyle::Topple&&Length(d)<u*ChallengeTerritoryFlightMin)fly=false;}
    // 不ぞろいにする: 出す所・出る時刻・大きさ・飛ぶ秒をばらばらにする
    if(launch->style!=InkStyle::Lay&&launch->style!=InkStyle::Pool){
        const float a=InkUnit(seed)*6.283f,r=InkDropScatter*u*std::sqrt(InkUnit(seed));from=from+V{std::cos(a)*r,(InkUnit(seed)-.5f)*r,std::sin(a)*r};from.y=std::max(from.y,floor+u*.3f);
        delay+=InkUnit(seed)*InkDropLag;}
    TerritoryState::Flight f;f.to=at;f.radius=radius;f.seed=shape;f.drops=drops;f.delay=delay;f.particle=fly;f.from=from;
    if(!fly){if(delay<=0){LandTerritorySplat(at,radius,shape,drops);return;}f.time=0;t.flights.push_back(f);return;}
    // 流体の粒子のしずく: 決まった放物線 (p = from + v t - g t^2) で、落ちる高さは体液の板の高さ
    // (大きさは飛び出す瞬間に決める: 同じ能力の遠くのしずくが並び終わってから、散らばりを見て升より大きくする。ここでは倍率だけ)
    f.grow=InkDropSizeLow+(InkDropSizeHigh-InkDropSizeLow)*InkUnit(seed);f.count=f.grow<.85f?2:f.grow<1.15f?3:4;const V land{at.x,floor,at.z};
    // 飛ぶ秒もばらばら (同じ所へも、高く山なりに飛ぶしずくと低く速いしずくがある)
    f.time=std::clamp((ChallengeTerritoryFlightLow+Length(land-from)/(u*ChallengeTerritoryFlightSpeed))*(InkDropTimeLow+(InkDropTimeHigh-InkDropTimeLow)*InkUnit(seed)),std::max(ChallengeTerritoryFlightLow,minTime),ChallengeTerritoryFlightHigh);
    f.velocity=(land-from)*(1/f.time)+V{0,ChallengeTerritoryFlightGravity*f.time,0};
    t.flights.push_back(f);
}

// しずくの大きさの下限: 体・飛んでいるしずく (今の所と落ちる所)・これから飛ぶしずく (from と to) を囲む広さから、流体を描く升の大きさを見積もる
float ChronoSystem::TerritoryDropFloor(V from,V to)const{
    const auto& t=battle_.territory;const V body=life_.bodies[0].at;const float R=DevourBodyRadius(life_.bodies[0].mass);
    V lo=body-V{R*2.5f,R,R*2.5f},hi=body+V{R*2.5f,R*2,R*2.5f};
    auto take=[&](V p){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};};
    take(from);take(to);
    for(const auto& f:t.flights){if(!f.particle)continue;take(f.to);if(f.launched){const float k=f.age-f.delay;take(f.from+f.velocity*k-V{0,ChallengeTerritoryFlightGravity*k*k,0});}else take(f.from);}
    // 升の大きさ: 流体の描き方 (FluidVolume の FitCell) と同じく、範囲に粒の広がり (0.8 x 粒の間隔) を足して並べる。
    // 粒の間隔で決まる下限 (0.2 x 粒の間隔) があり、体が小さいとこちらが升の大きさになる。前のフレームに実際に使った升より小さくはしない
    const auto* render=Engine::Renderer::GetInstance();const float spacing=render?render->fluidSamplingScale_:1.f,last=render?render->volumePreviousVoxelSize_:0.f;
    const float wide=std::max(hi.x-lo.x,hi.z-lo.z),cell=std::max({(wide+1.6f*spacing)/InkVolumeColumns,.2f*spacing,last});
    // 粒の大きさ size のしずくは、半径 0.6 size で描かれる
    return cell*InkDropCells/.6f;
}

// 動いている物から垂れるしずく: from から velocity で落ち、床に着いた所に小さな点を残す (能力の体液の量とは別)
void ChronoSystem::DripTerritoryInk(V from,V velocity){
    auto& t=battle_.territory;if(t.ink.empty())return;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;
    const auto& body=life_.bodies[0];TerritoryState::Flight f;f.from=from;f.velocity=velocity;
    f.size=std::max({.3f,.3f*LifeScale(body.mass)*.9f,u*InkDropBody,TerritoryDropFloor(from,from)})*InkDripSize*(.8f+.4f*InkUnit(seed));f.count=2;
    // 床 (しずくが平たくなる高さ) に着く秒: y = from.y + vy t - 9 t^2
    const float drop=std::max(0.f,from.y-(c.y+.12f*f.size)),vy=velocity.y;f.time=(vy+std::sqrt(vy*vy+4*ChallengeTerritoryFlightGravity*drop))/(2*ChallengeTerritoryFlightGravity);
    if(f.time<=.02f)return;
    f.to=from+V{velocity.x,0,velocity.z}*f.time;f.to.y=c.y;
    // ラグの外には落とさない
    if(std::abs(f.to.x-c.x)>h||std::abs(f.to.z-c.z)>h)return;
    f.radius=u*InkDripSpot*(.7f+.6f*InkUnit(seed));f.seed=InkRandom(seed);f.drops=false;f.particle=true;t.flights.push_back(f);
}

// しずくが落ちた: しぶきを広げ、ときどきまわりに小さなしずくを跳ねさせる
void ChronoSystem::LandTerritorySplat(V at,float radius,uint32_t shape,bool drops){
    auto& t=battle_.territory;const float u=battle_.unit;uint32_t& seed=t.inkSeed;
    GrowTerritoryInk(at,radius,shape);
    if(drops){const int count=1+int(InkUnit(seed)*2.f);
        for(int k=0;k<count;++k){const float a=InkUnit(seed)*6.283f,d=u*(1.f+InkUnit(seed)*.7f);GrowTerritoryInk(at+V{std::cos(a)*d,0,std::sin(a)*d},u*(.12f+InkUnit(seed)*.16f),InkRandom(seed));}}
}

// 体液を丸く広げる: 真ん中が円 (少し広め) に入る床のマスを陣地にする。新しく陣地になった数を返す
int ChronoSystem::SpreadTerritory(V at,float radius){
    auto& t=battle_.territory;if(t.cells.empty())return 0;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;
    const float reach=radius+u*.35f;int added=0;
    const int x0=std::max(0,int(std::floor((at.x-reach-(c.x-h))/u))),x1=std::min(N-1,int(std::floor((at.x+reach-(c.x-h))/u)));
    const int z0=std::max(0,int(std::floor((at.z-reach-(c.z-h))/u))),z1=std::min(N-1,int(std::floor((at.z+reach-(c.z-h))/u)));
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){auto& cell=t.cells[size_t(z*N+x)];if(cell!=TerritoryCell::Floor)continue;
        const float dx=c.x-h+(float(x)+.5f)*u-at.x,dz=c.z-h+(float(z)+.5f)*u-at.z;if(dx*dx+dz*dz>reach*reach)continue;
        cell=TerritoryCell::Owned;t.age[size_t(z*N+x)]=0;++added;SplatTerritoryCell(z*N+x);}
    t.owned+=added;t.spread+=added;return added;
}

// 能力 1 回分の体液を始める (体が大きいほど量が増える。飴玉は少しだけで、下限は無い)
void ChronoSystem::BeginTerritoryUse(BattleAction action,bool candy){
    auto& b=battle_;if(b.legacy||b.challenge!=ChallengeKind::Territory)return;
    TerritoryUse use;use.action=action;use.candy=candy;use.last=life_.bodies[0].at;
    if(candy){use.budget=ChallengeTerritoryCandyMax;use.minimum=0;use.settle=3.2f;}
    else{const float k=ChallengeTerritoryScale(ChallengePowerNow())*InkAmountOf(action)*std::min(6.f,b.powerScale);use.budget=int(std::lround(ChallengeTerritoryUseMax*k/InkCellsPerDrop));use.minimum=int(std::lround(ChallengeTerritoryUseMin*k/InkCellsPerDrop));use.settle=TerritorySettle(action);
        const InkStyle style=InkStyleOf(action);
        if(style==InkStyle::Lay||style==InkStyle::Spin||style==InkStyle::Spray||style==InkStyle::Drip)use.span=std::max(InkContinuousSpan,(style==InkStyle::Drip?use.settle*.5f:BattleActionTime(action)));}
    if(candy)use.budget=std::max(1,int(std::lround(ChallengeTerritoryCandyMax/InkCellsPerDrop)));
    b.territory.uses.push_back(use);
}

// origin から近い順に、inside に入る床のマスを limit マスまで陣地にする。ぬった数を返す (last には一番遠くにぬったマスの真ん中)
// 体液は launch の型で飛ばす (無ければ origin から飛び散る炸裂)
int ChronoSystem::PaintTerritoryNearest(V origin,int limit,const std::function<bool(V)>& inside,V* last,const InkLaunch* launch){
    auto& t=battle_.territory;if(t.cells.empty()||limit<=0)return 0;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;
    std::vector<std::pair<float,int>> found;
    for(int z=0;z<N;++z)for(int x=0;x<N;++x){if(t.cells[size_t(z*N+x)]!=TerritoryCell::Floor)continue;const V p=c+V{-h+(float(x)+.5f)*u,origin.y,-h+(float(z)+.5f)*u};
        if(!inside(p))continue;const float dx=p.x-origin.x,dz=p.z-origin.z;found.push_back({dx*dx+dz*dz,z*N+x});}
    const size_t take=std::min(found.size(),size_t(limit));if(take==0)return 0;
    std::partial_sort(found.begin(),found.begin()+std::ptrdiff_t(take),found.end());
    InkLaunch burst;burst.origin=origin;if(!launch)launch=&burst;
    for(size_t k=0;k<take;++k){const int i=found[k].second;t.cells[size_t(i)]=TerritoryCell::Owned;t.age[size_t(i)]=0;SplatTerritoryCell(i,launch);}
    if(last){const int i=found[take-1].second;*last=c+V{-h+(float(i%N)+.5f)*u,origin.y,-h+(float(i/N)+.5f)*u};}
    t.owned+=int(take);t.spread+=int(take);return int(take);
}

// 衝撃の形にぬる: その能力のいちばん新しい 1 回分から体液を使う (使い切ったらぬらない)。熱いお茶 (やけど) は外へ広がる体液にする
void ChronoSystem::PaintTerritoryImpact(const BattleImpact& impact){
    auto& t=battle_.territory;if(t.cells.empty())return;
    TerritoryUse* use=nullptr;size_t index=0;
    for(size_t k=t.uses.size();k-->0;){auto& x=t.uses[k];if(x.open&&x.candy==impact.candy&&(impact.candy||x.action==impact.action)){use=&x;index=k;break;}}
    if(!use||use->used>=use->budget)return;
    if(impact.effect==ImpactEffect::Burn){const float reach=impact.reach;V dir=impact.dir;dir.y=0;dir=Length(dir)>.01f?Unit(dir):V{0,0,1};
        t.ripples.push_back({index,impact.at+dir*(reach*.5f),battle_.unit*.8f,reach*ChallengeTerritoryTeaGrow,0,BattleBurnTime});return;}
    // ぬる形は当たりより少し広くする (向きと角度はそのまま)。とても大きい当たり (一時停止) は体のまわり InkSampleReach (unit) まで
    const float u=battle_.unit;BattleImpact flat=impact;flat.flat=true;const float pad=u*ChallengeTerritoryPad;
    flat.reach=std::min(flat.reach*ChallengeTerritoryGrow+pad,u*InkSampleReach*3);flat.width=flat.width*ChallengeTerritoryGrow+pad;
    if(impact.reach>1e8f)flat.reach=u*InkSampleReach;
    InkLaunch launch;launch.style=impact.candy?InkStyle::Drip:InkStyleOf(impact.action);launch.origin=impact.at;launch.action=impact.action;launch.effect=impact.effect;
    launch.dir=impact.shape==ImpactShape::Segment?impact.to-impact.at:impact.dir;launch.dir.y=0;launch.dir=Length(launch.dir)>.01f?Unit(launch.dir):V{0,0,1};
    // 物ごと: 立っている積み木の塔 (壁) と、置いたときのおとり・塔はしずくを使わない (崩れる・弾けるときに全部使う)。
    // 投げた物が落ちた所 (しぶき) は王冠の形に跳ねる
    if(!impact.candy){
        if(impact.effect==ImpactEffect::Wall)return;
        if(impact.action==BattleAction::Guard&&impact.effect==ImpactEffect::Self)return;
        if((impact.action==BattleAction::Decoy||impact.action==BattleAction::Tower)&&impact.effect==ImpactEffect::Zone)return;
        if(launch.style==InkStyle::Drip&&impact.effect==ImpactEffect::Zone)launch.style=InkStyle::Burst;}
    use->aimed=true;use->style=launch.style;use->origin=launch.origin;use->dir=launch.dir;
    // 噴き出す・王: 体のまわりの輪に、角度をそろえて均等に落とす
    if(launch.style==InkStyle::Pulse||(impact.action==BattleAction::Ou&&!impact.candy)){
        int ring=std::max(1,int(std::lround(3.1416f*((InkRingInner*DevourBodyRadius(life_.bodies[0].mass)+u*InkRingWidth)*(InkRingInner*DevourBodyRadius(life_.bodies[0].mass)+u*InkRingWidth)
            -(InkRingInner*DevourBodyRadius(life_.bodies[0].mass))*(InkRingInner*DevourBodyRadius(life_.bodies[0].mass)))/(u*u*InkCellsPerDrop))));
        ring=std::min(ring,use->budget-use->used);if(use->span>0)ring=std::min(ring,int(use->allow)-use->used);if(ring<=0)return;
        use->used+=RingTerritory(*use,ring,launch);return;}
    // 形の広さ (unit^2) と、形を囲む四角
    const V a=flat.at;float area=0;V lo=a,hi=a;
    switch(flat.shape){
    case ImpactShape::Sphere:area=3.1416f*flat.reach*flat.reach;lo=a-V{flat.reach,0,flat.reach};hi=a+V{flat.reach,0,flat.reach};break;
    case ImpactShape::Cone:area=flat.reach*flat.reach*std::acos(std::clamp(flat.cone,-1.f,1.f));lo=a-V{flat.reach,0,flat.reach};hi=a+V{flat.reach,0,flat.reach};break;
    case ImpactShape::Segment:{const V b=flat.to;area=Length(V{b.x-a.x,0,b.z-a.z})*2*flat.width;
        lo={std::min(a.x,b.x)-flat.width,0,std::min(a.z,b.z)-flat.width};hi={std::max(a.x,b.x)+flat.width,0,std::max(a.z,b.z)+flat.width};break;}
    case ImpactShape::Strip:{const float r=std::max(flat.reach,flat.back)+flat.width;area=(flat.reach+flat.back)*2*flat.width;lo=a-V{r,0,r};hi=a+V{r,0,r};break;}
    }
    int count=std::max(1,int(std::lround(area/(u*u*InkCellsPerDrop))));
    // 毎フレーム当たる能力は、出してよい数 (時間で増える) までに抑える
    if(use->span>0)count=std::min(count,int(use->allow)-use->used);
    count=std::min(count,use->budget-use->used);if(count<=0)return;
    use->used+=SprayTerritory(*use,count,[&](V p){return BattleImpactTouches(flat,p,0);},lo,hi,launch);
}

// 体のまわりの輪に、しずくを count 個、角度をそろえて落とす (少しずつずらす)。飛ばした数を返す
int ChronoSystem::RingTerritory(TerritoryUse& use,int count,const InkLaunch& launch){
    auto& t=battle_.territory;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;int sent=0;
    const V body=life_.bodies[0].at;const float inner=InkRingInner*DevourBodyRadius(life_.bodies[0].mass),phase=InkUnit(seed)*6.283f;
    for(int k=0;k<count;++k){
        const float a=phase+6.283f*(float(k)+.5f*InkUnit(seed))/float(count),r=inner+u*InkRingWidth*std::sqrt(InkUnit(seed));
        const V p{body.x+std::cos(a)*r,c.y,body.z+std::sin(a)*r};
        if(std::abs(p.x-c.x)>h-u*.3f||std::abs(p.z-c.z)>h-u*.3f)continue;
        bool blocked=false;for(const auto& f:t.furniture)if(std::abs(p.x-f[0])<f[2]&&std::abs(p.z-f[1])<f[3]){blocked=true;break;}if(blocked)continue;
        InkLaunch l=launch;l.order=float(k)/float(count);LaunchTerritoryInk(p,&l);use.last=p;++sent;}
    return sent;
}

// しずくを count 個、inside に入る床 (ラグの中・家具の外) の、ばらばらに選んだ所へ飛ばす。飛ばした数を返す (use.last に最後の所)
int ChronoSystem::SprayTerritory(TerritoryUse& use,int count,const std::function<bool(V)>& inside,V low,V high,const InkLaunch& launch){
    auto& t=battle_.territory;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;uint32_t& seed=t.inkSeed;int sent=0;
    // 体の下には落とさない (体に隠れて見えず、体から出たしずくが体へ戻って溶け込んで見える)。転がしは体の下をぬるので除かない
    const V body=life_.bodies[0].at;const float under=launch.style==InkStyle::Lay?0.f:DevourBodyRadius(life_.bodies[0].mass)*1.1f;
    for(int k=0;k<count;++k)for(int tries=0;tries<24;++tries){
        const V p{low.x+(high.x-low.x)*InkUnit(seed),c.y,low.z+(high.z-low.z)*InkUnit(seed)};
        if(std::abs(p.x-c.x)>h-u*.3f||std::abs(p.z-c.z)>h-u*.3f||!inside(p))continue;
        {const float dx=p.x-body.x,dz=p.z-body.z;if(dx*dx+dz*dz<under*under)continue;}
        bool blocked=false;for(const auto& f:t.furniture)if(std::abs(p.x-f[0])<f[2]&&std::abs(p.z-f[1])<f[3]){blocked=true;break;}if(blocked)continue;
        LaunchTerritoryInk(p,&launch);use.last=p;++sent;break;}
    return sent;
}

// 陣地の広さをマスに直した数 (インクマップの点の数 / 1 マスの点の数)
int ChronoSystem::TerritoryCoveredCells()const{const int per=(W/N)*(W/N);return battle_.territory.inkCovered/std::max(1,per);}

// 進める: 陣地になってからの秒・1 回分の体液の終わり (下限に足りなければ最後にぬった所のまわりに足す)・外へ広がる体液・歩いた跡
void ChronoSystem::TickTerritory(GameContext&,float dt){
    auto& t=battle_.territory;if(t.cells.empty())return;
    for(size_t i=0;i<t.cells.size();++i)if(t.cells[i]==TerritoryCell::Owned)t.age[i]+=dt;
    // 投げた物・飛び道具・飴玉・転がるビー玉から、動いている間しずくを垂らす (水鉄砲の水は水そのものなので垂らさない)
    {const auto& b=battle_;uint32_t& seed=t.inkSeed;const float chance=dt/InkDripEvery;
        for(const auto& s:b.shots){if(s.age>=99)continue;
            // 水鉄砲の水は、撃った瞬間に同じ速さの流体のしずくを 1 つ出す (水の玉は描かない)
            if(s.style==2){if(s.age<=dt*1.01f)DripTerritoryInk(s.at,s.velocity);continue;}
            if(InkUnit(seed)<chance)DripTerritoryInk(s.at-V{0,s.radius*.5f,0},V{s.velocity.x*.25f,std::min(0.f,s.velocity.y*.25f),s.velocity.z*.25f});}
        for(const auto& pl:b.placed){if(pl.kind!=BattleAction::Marbles||pl.landed)continue;if(InkUnit(seed)<chance)DripTerritoryInk(pl.at+V{0,pl.size*.4f,0},V{pl.velocity.x*.2f,0,pl.velocity.z*.2f});}}
    // 飛んでいるしずく: 落ちる秒が来たらしぶきにする
    {auto* render=Engine::Renderer::GetInstance();const V rgb=life_.recipe.color.ToRGB();
        for(auto& f:t.flights){f.age+=dt;
            // 飛び出す: 種類 4 + 残る秒 / 10 は決まった放物線で飛ぶしずく。大きさを負にすると、ばらつきなし (落ちる所が計算と合う)
            if(f.particle&&!f.launched&&f.age>=f.delay){f.launched=true;const float life=f.time+.5f;
                // 能力のしずく (大きさがまだ無い) は、今の散らばりから大きさを決め、落ちる高さ (平たくなる高さ) に合わせて速さを直す
                if(f.size<=0){const auto& body=life_.bodies[0];f.size=std::max({.3f,.3f*LifeScale(body.mass)*.9f,battle_.unit*InkDropBody,TerritoryDropFloor(f.from,f.to)})*f.grow;
                    const V land{f.to.x,battle_.arenaAt.y+.12f*f.size,f.to.z};f.velocity=(land-f.from)*(1/f.time)+V{0,ChallengeTerritoryFlightGravity*f.time,0};}
                render->EmitGPUFluid(EV(f.from),EV(f.velocity*.2f),Engine::Vector4{rgb.x,rgb.y,rgb.z,1},f.count,4+life/10,false,-f.size);}
            if(f.age>=f.delay+f.time)LandTerritorySplat(f.to,f.radius,f.seed,f.drops);}}
    t.flights.erase(std::remove_if(t.flights.begin(),t.flights.end(),[](const TerritoryState::Flight& f){return f.age>=f.delay+f.time;}),t.flights.end());
    // 広がっている途中のしぶき: 始めは速く、だんだん遅く (3 乗のゆるみ) 形まで広げる
    for(auto& g:t.growing){g.age+=dt;const float k=std::min(1.f,g.age/std::max(.01f,g.life)),ease=1-(1-k)*(1-k)*(1-k);
        StampTerritoryInk(g.at,g.radius*(ChallengeTerritoryGrowStart+(1-ChallengeTerritoryGrowStart)*ease),g.seed);}
    t.growing.erase(std::remove_if(t.growing.begin(),t.growing.end(),[](const TerritoryState::Growing& g){return g.age>=g.life;}),t.growing.end());
    // ぬれたての度合いを減らす (無くなったら送り直しをやめる)
    if(t.anyWet){const int fade=std::max(1,int(std::lround(255*dt/ChallengeTerritoryWetTime)));bool any=false;
        for(auto& w:t.wet)if(w){w=uint8_t(std::max(0,int(w)-fade));any|=w>0;}
        t.anyWet=any;t.inkDirty=true;}
    for(auto& r:t.ripples){r.age+=dt;auto& use=t.uses[r.use];if(use.used>=use.budget)continue;const float k=std::min(1.f,r.age/std::max(.01f,r.life)),radius=r.from+(r.to-r.from)*k;
        const int want=std::min(use.budget,int(std::ceil(float(use.budget)*k)))-use.used;if(want<=0)continue;
        InkLaunch pool;pool.style=InkStyle::Pool;pool.origin=r.at;pool.action=use.action;pool.dir=life_.bodies[0].forward;pool.dir.y=0;pool.dir=Length(pool.dir)>.01f?Unit(pool.dir):V{0,0,1};
        use.used+=SprayTerritory(use,want,[&](V p){const float dx=p.x-r.at.x,dz=p.z-r.at.z;return dx*dx+dz*dz<=radius*radius;},r.at-V{radius,0,radius},r.at+V{radius,0,radius},pool);}
    t.ripples.erase(std::remove_if(t.ripples.begin(),t.ripples.end(),[](const TerritoryRipple& r){return r.age>r.life;}),t.ripples.end());
    // 1 回分の終わり: 下限に足りなければ、最後に落とした所のまわりに飛び散らせる。毎フレーム当たる能力は、出してよい数を時間で増やす
    for(auto& use:t.uses){if(!use.open)continue;use.age+=dt;if(use.span>0)use.allow=float(use.budget)*std::min(1.f,use.age/use.span);
        if(use.age<use.settle)continue;use.open=false;
        if(use.used<use.minimum){
            // 足りない分: 最後に当たったときと同じ所から同じ飛ばし方で、そのまわりへ (叩くならハンマーの頭から)。噴き出す・王は体のまわりの輪
            InkLaunch more;more.action=use.action;more.style=use.aimed?use.style:InkStyle::Burst;more.origin=use.aimed?use.origin:use.last;more.dir=use.dir;
            if(more.style==InkStyle::Pulse||use.action==BattleAction::Ou){use.used+=RingTerritory(use,use.minimum-use.used,more);continue;}
            const auto& body=life_.bodies[0];V center=more.origin;if(use.action==BattleAction::Smash)center=body.at+more.dir*(DevourBodyRadius(body.mass)*InkHammerReach);
            const float r=battle_.unit*2.5f+(use.action==BattleAction::Smash?DevourBodyRadius(body.mass):0.f);
            use.used+=SprayTerritory(use,use.minimum-use.used,[&](V p){const float dx=p.x-center.x,dz=p.z-center.z;return dx*dx+dz*dz<=r*r;},center-V{r,0,r},center+V{r,0,r},more);}}
    TickReclaimers(dt);
    const auto& body=life_.bodies[0];SpreadTerritory(body.at,DevourBodyRadius(body.mass)*ChallengeTerritoryTrail);
}

// 陣地を描く: インクマップが変わったら送り、ラグの上の 1 枚の板を体液のシェーダーで描く (色は体の色)
void ChronoSystem::DrawTerritory(GameContext& ctx){
    auto& t=battle_.territory;if(t.ink.empty()||territoryInkMesh_==0)return;
    if(!territoryInkPipeline_)territoryInkPipeline_=ctx.renderer->CreateShaderPipeline("InkFloor",L"Resources/shaders/InstancedObjVS.hlsl",L"Resources/shaders/InkFloorPS.hlsl");
    if(t.inkDirty||territoryInkTexture_==0){territoryInkPixels_.resize(size_t(W*W*4));for(size_t i=0;i<t.ink.size();++i){auto* px=&territoryInkPixels_[i*4];px[0]=t.ink[i];px[1]=t.wet[i];px[2]=0;px[3]=255;}
        territoryInkTexture_=ctx.renderer->UpdatePaintTexture("territory-ink",W,W,territoryInkPixels_.data());t.inkDirty=false;}
    if(territoryInkTexture_==0)return;
    const V rgb=life_.recipe.color.ToRGB();Engine::Transform tr;tr.scale={1,1,1};
    DrawReclaimers(ctx);
    ctx.renderer->DrawMeshInstanced(territoryInkMesh_,territoryInkTexture_,tr,Engine::Vector4{std::pow(rgb.x,2.2f),std::pow(rgb.y,2.2f),std::pow(rgb.z,2.2f),1},"InkFloor");
}

// 体を止める: ラグの外へ出ず、家具に入らない (家具は四角、体は丸)
void ChronoSystem::BlockTerritoryBody(V& at,float radius)const{
    const auto& t=battle_.territory;const float u=battle_.unit,h=ChallengeTerritoryHalf*u-radius*.6f;const V c=battle_.arenaAt;
    at.x=std::clamp(at.x,c.x-h,c.x+h);at.z=std::clamp(at.z,c.z-h,c.z+h);(void)u;
    const float r=radius*.8f;
    for(const auto& f:t.furniture){const float nx=std::clamp(at.x,f[0]-f[2],f[0]+f[2]),nz=std::clamp(at.z,f[1]-f[3],f[1]+f[3]);float dx=at.x-nx,dz=at.z-nz;const float len=std::sqrt(dx*dx+dz*dz);
        if(len>=r)continue;
        if(len>1e-4f){at.x=nx+dx/len*r;at.z=nz+dz/len*r;continue;}
        // 真ん中が家具の中: 一番近い辺の外へ出す
        const float left=at.x-(f[0]-f[2]),right=(f[0]+f[2])-at.x,back=at.z-(f[1]-f[3]),front=(f[1]+f[3])-at.z,m=std::min({left,right,back,front});
        if(m==left)at.x=f[0]-f[2]-r;else if(m==right)at.x=f[0]+f[2]+r;else if(m==back)at.z=f[1]-f[3]-r;else at.z=f[1]+f[3]+r;}
}

// HUD の状況: 陣地の割合 (インクマップの点で数える)
std::string ChronoSystem::TerritoryStatus()const{
    const auto& t=battle_.territory;const float percent=t.inkOpenCount>0?100.f*float(t.inkCovered)/float(t.inkOpenCount):0.f;
    char text[32];snprintf(text,sizeof(text),"%.1f",percent);return std::string("陣地 ")+text+"%";
}

// まだ陣地でない一番近いマスの真ん中 (確認用の自動操作で向かう先)
bool ChronoSystem::NearestOpenCell(V from,V& out)const{
    const auto& t=battle_.territory;if(t.cells.empty())return false;const float u=battle_.unit,h=ChallengeTerritoryHalf*u;const V c=battle_.arenaAt;float best=1e18f;
    const int per=W/N;
    for(int z=0;z<N;++z)for(int x=0;x<N;++x){if(t.cells[size_t(z*N+x)]==TerritoryCell::Furniture)continue;if(!t.ink.empty()&&t.ink[size_t((z*per+per/2)*W+x*per+per/2)]>=128)continue;
        const V p=c+V{-h+(float(x)+.5f)*u,0,-h+(float(z)+.5f)*u};const float dx=p.x-from.x,dz=p.z-from.z,d=dx*dx+dz*dz;if(d<best){best=d;out=p;}}
    return best<1e17f;
}

}
