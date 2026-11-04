// SlimeDevour.cpp
// 吸収モード (ChronoSystem の一部)。敵はおらず、制限時間内に物を飲み込んで大きくなり、街を飲み込む。
// 吸収ボタンで全身ごと飛びかかり (溜め → 跳ぶ → 覆う → 満足)、広げた体の下の物をまとめて取り込む。
// ずっと小さい物は触れるだけで沈み込む。規則は DevourRules.h。
#include "ChronoSystemInternal.h"
#include "../Chrono/DevourAnimation.h"
#include "../Data/GameData.h"
#include <random>

namespace Game {
// 飛びかかりの体の形のアニメーション (Resources/Data/devour_animation.json)
namespace {
// 飛沫の乱数 (毎回ちがう飛び方にする)
std::mt19937& DevourRandom(){static std::mt19937 random{std::random_device{}()};return random;}
const char* kDevourAnimPath="Resources/Data/devour_animation.json";
// JSON から読む (壊れていれば理由を返す)
bool ReadDevourAnim(Chrono::DevourAnimation& out,std::string& message){
    std::ifstream file(kDevourAnimPath);if(!file){message=std::string("ファイルがありません: ")+kDevourAnimPath;return false;}
    try{nlohmann::json json;file>>json;Chrono::DevourAnimation a;
        for(const auto& name:json.at("blobs"))a.names.push_back(name.get<std::string>());
        auto vec=[](const nlohmann::json& v){auto f=Data::ReadArray<float,3>(v,"devour_animation.json");return Chrono::Vec{f[0],f[1],f[2]};};
        for(const auto& key:json.at("keys")){Chrono::DevourKey k;k.time=key.at("time").get<float>();
            for(const auto& blob:key.at("blobs")){Chrono::DevourBlob b;b.at=vec(blob.at("at"));b.size=vec(blob.at("size"));
                // 口の値は無ければ初めの値 (古いファイルも読める)
                {std::string kind=blob.value("kind",std::string("lump"));b.kind=kind=="mouth"?Chrono::DevourBlobKind::Mouth:kind=="tail"?Chrono::DevourBlobKind::Tail:kind=="amoeba"?Chrono::DevourBlobKind::Amoeba:kind=="gulp"?Chrono::DevourBlobKind::Gulp:Chrono::DevourBlobKind::Lump;}
                b.thickness=blob.value("thickness",b.thickness);b.reach=blob.value("reach",b.reach);b.curl=blob.value("curl",b.curl);
                b.wave=blob.value("wave",b.wave);b.tentacles=blob.value("tentacles",b.tentacles);b.twist=blob.value("twist",b.twist);
                k.blobs.push_back(b);}
            if(k.blobs.size()!=a.names.size())throw std::runtime_error("キーの塊の数が名前の数と違います");
            a.keys.push_back(std::move(k));}
        if(a.keys.empty())throw std::runtime_error("キーがありません");
        Chrono::DevourSortKeys(a,-1);out=std::move(a);message=std::string("読み込みました: ")+kDevourAnimPath;return true;}
    catch(const std::exception& e){message=std::string("読み込めません: ")+e.what();return false;}
}
}
Chrono::DevourAnimation& DevourAnim(){
    static Chrono::DevourAnimation anim=[]{Chrono::DevourAnimation a;std::string message;if(!ReadDevourAnim(a,message))a=Chrono::DevourDefaultAnimation();return a;}();
    return anim;
}
bool SaveDevourAnim(std::string& message){
    const auto& a=DevourAnim();nlohmann::json json;json["blobs"]=a.names;json["keys"]=nlohmann::json::array();
    for(const auto& key:a.keys){nlohmann::json k;k["time"]=key.time;k["blobs"]=nlohmann::json::array();
        for(const auto& blob:key.blobs){nlohmann::json b;b["kind"]=blob.kind==Chrono::DevourBlobKind::Mouth?"mouth":blob.kind==Chrono::DevourBlobKind::Tail?"tail":blob.kind==Chrono::DevourBlobKind::Amoeba?"amoeba":blob.kind==Chrono::DevourBlobKind::Gulp?"gulp":"lump";
            b["at"]={blob.at.x,blob.at.y,blob.at.z};b["size"]={blob.size.x,blob.size.y,blob.size.z};
            if(blob.kind!=Chrono::DevourBlobKind::Lump){b["thickness"]=blob.thickness;b["reach"]=blob.reach;b["curl"]=blob.curl;b["wave"]=blob.wave;b["tentacles"]=blob.tentacles;b["twist"]=blob.twist;}
            k["blobs"].push_back(b);}
        json["keys"].push_back(k);}
    std::ofstream file(kDevourAnimPath);if(!file){message=std::string("保存できません: ")+kDevourAnimPath;return false;}
    file<<json.dump(2);message=std::string("保存しました: ")+kDevourAnimPath;return true;
}
bool ReloadDevourAnim(std::string& message){return ReadDevourAnim(DevourAnim(),message);}
namespace {
// 体の中に見えている時間 (この間に中心へ沈みながら溶けて小さくなる) と、同時に見せる数
constexpr float kMealTime=2.4f;
constexpr size_t kMealLimit=16;
// ワールドの点を 1280x720 の画面の位置にする (後ろなら false)
bool DevourScreen(const GameContext& ctx,Vec point,float& x,float& y){
    DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(point.x,point.y,point.z,1),ctx.camera->View()*ctx.camera->Proj()));
    if(clip.w<=.1f)return false;x=(clip.x/clip.w*.5f+.5f)*1280;y=(.5f-clip.y/clip.w*.5f)*720;return true;
}
// 残り時間を「分:秒」にする
std::string DevourClockText(float seconds){int s=int(std::ceil(std::max(0.f,seconds)));char text[16];snprintf(text,sizeof(text),"%d:%02d",s/60,s%60);return text;}
// 増えた量を整数の文字列にする
std::string DevourAmount(float value){return std::to_string(int(std::round(value)));}
}
// 小物を 1 つ飲み込む: 隠して成長し、体の中に見せる。塀・バリケード・シャッターなら道を開ける。増えた量を返す
float ChronoSystem::DevourEat(entt::registry& r,size_t index){
    if(index>=life_.props.size()||!life_.props[index].active)return 0;
    auto& prop=life_.props[index];auto& body=life_.bodies[0];DevourRemoveProp(r,index);
    float gain=DevourSwallow(body.mass,devourDigest_,prop.reward);++life_.absorbed;LifeUnlockMorphItem(life_,0x80000000u|uint32_t(index));
    // 体の中に見せる (モデルか光る球。塀のようにモデルの無い物は見せない)
    if(prop.visual!=0xffffffffu||prop.orb){
        if(devourMeals_.size()>=kMealLimit)devourMeals_.erase(devourMeals_.begin());
        float bodyRadius=DevourBodyRadius(body.mass);V offset=prop.at-body.at;offset.y=0;
        float length=Length(offset);if(length>bodyRadius*.45f)offset=offset*(bodyRadius*.45f/length);
        DevourMeal meal;meal.visual=prop.visual;meal.orb=prop.orb;meal.offset=offset;meal.radius=prop.radius;meal.tint=LifeOrbColor(prop.reward);
        meal.fit=std::min(1.f,bodyRadius*.55f/std::max(.1f,prop.radius));devourMeals_.push_back(meal);}
    return gain;
}
// 物を街から消す: 隠して、塀・バリケード・シャッターなら道を開ける
void ChronoSystem::DevourRemoveProp(entt::registry& r,size_t index){
    auto& prop=life_.props[index];prop.active=false;
    if(prop.visual!=0xffffffffu){auto e=entt::entity(prop.visual);if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;}
    // 建物なら残りの部位を隠し、当たり判定を消して通れるようにする
    if(auto parts=devourBuildingParts_.find(index);parts!=devourBuildingParts_.end()){
        for(auto e:parts->second){if(!r.valid(e))continue;if(r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;else r.destroy(e);}
        devourBuildingParts_.erase(parts);}
    // 道をふさぐ物なら開ける
    for(size_t i=0;i<devourWallProps_.size();++i)if(devourWallProps_[i]==int(index))BreakCityWall(i,true);
    if(int(index)==devourBarricadeProp_)BreakCityGate(r,true,true);
    if(int(index)==cityGateProp_)BreakCityGate(r,false,true);
    // 輪の門なら壁が崩れる
    if(auto gate=devourGates_.find(index);gate!=devourGates_.end())DevourOpenRing(r,gate->second);
}
// 輪を開ける: 壁を消し、同じ壁の他の門も消して、向こうの物に手が届くようにする
void ChronoSystem::DevourOpenRing(entt::registry& r,int ring){
    if(ring<1||ring>=RingStyleCount||ringOpen_[size_t(ring)])return;
    ringOpen_[size_t(ring)]=true;devourRingOpened_=ring;
    for(auto e:ringWalls_[size_t(ring)])if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;
    for(auto& prop:life_.props)if(prop.ring==ring)prop.sealed=false;
    for(const auto& [index,gate]:devourGates_)if(gate==ring&&life_.props[index].active)DevourRemoveProp(r,index);
}
// 大きすぎる物を砕く: 物を消し、飲める大きさの欠片 (光る球) を周りへ飛ばす
void ChronoSystem::DevourShatter(entt::registry& r,GameContext& ctx,size_t index){
    // 欠片を足すと配列が動くので写しを使う
    const LifeCollectible prop=life_.props[index];DevourRemoveProp(r,index);
    float spread=prop.radius*.5f+prop.reach;V top=prop.at+V{0,prop.radius*.5f,0};
    for(int k=0;k<DevourShardCount;++k){float a=float(k)*2.399963f,d=spread*std::sqrt((float(k)+.5f)/float(DevourShardCount));
        LifeCollectible shard;shard.at=prop.at+V{std::cos(a)*d,0,std::sin(a)*d};shard.at.y=InkGround(shard.at)+1;shard.required=1;shard.reward=prop.reward*DevourShardShare/float(DevourShardCount);
        shard.orb=true;shard.radius=prop.radius*.35f;shard.shard=shard.radius*.6f;shard.shardFrom=top;shard.shardStart=life_.age;life_.props.push_back(shard);}
    if(swarmEffects_.size()<128)swarmEffects_.push_back({prop.at,0,prop.radius,2});
    LifeImpact(ctx,ImpactHammerHit,prop.at);auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,.6f*audio->GetMasterSEVolume(),.8f);
    life_.message="ドカン！ 砕けた欠片を飲み込め";life_.messageAge=1.4f;
}
// ハンマーの当たり: 覆える物はそのまま飲み込み、大きすぎる物は砕く。もっと大きい物は必要な量を知らせる
void ChronoSystem::DevourHammerHits(entt::registry& r,GameContext& ctx,const LifeBody& b,int pulse){
    float from,to;if(!LifeWeaponHitInterval(b,pulse,from,to))return;
    float mass=b.mass,range=14*LifeScale(mass),gained=0;int eaten=0;
    // 砕いて増えた欠片は同じ一撃の対象にしない
    size_t count=life_.props.size();
    for(size_t i=0;i<count;++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed)continue;float radius=prop.radius*.5f+prop.reach;
        if(LifeDistance(prop.at,b.at)>range+radius||!LifeWeaponTouches(b,prop.at,radius,pulse))continue;
        if(DevourCanCover(mass,prop.radius)){gained+=DevourEat(r,i);++eaten;}
        else if(prop.morphItem==LifeMorphItem::None&&DevourCanShatter(mass,prop.radius))DevourShatter(r,ctx,i);
        else if(life_.messageAge<=0){life_.message="硬い！ 本体 "+std::to_string(int(DevourMassToShatter(prop.radius)))+" で砕ける";life_.messageAge=1.4f;LifeImpact(ctx,ImpactDevourBounce,prop.at);}}
    if(eaten){float weight=DevourGulpWeight(eaten,gained,mass);auto* audio=Engine::Audio::GetInstance();audio->Play(lifeEngulfSound_,false,(.6f+.3f*weight)*audio->GetMasterSEVolume(),1);
        LifeImpact(ctx,ImpactDevour,b.at,{0,1,0},.8f+.7f*weight);}
}
// タイヤの溜め突進: 砕ける大きさの物にぶつかると砕く
void ChronoSystem::DevourTireSmash(entt::registry& r,GameContext& ctx,float reach){
    const auto& body=life_.bodies[0];if(body.weapon.dashPower<DevourTireSmashPower)return;
    size_t count=life_.props.size();
    for(size_t i=0;i<count;++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||prop.morphItem!=LifeMorphItem::None||!DevourCanShatter(body.mass,prop.radius))continue;
        if(LifeDistance(prop.at,body.at)<=reach+prop.radius*.5f+prop.reach)DevourShatter(r,ctx,i);}
}
// 時間切れの大きさを記録して終わる。自己ベストか最後の段階なら勝ちの演出にする
void ChronoSystem::FinishDevour(Player& p){
    auto& body=life_.bodies[0];DevourGrow(body,devourDigest_);devourDigest_=0;
    devourNewBest_=body.mass>devourBest_;if(devourNewBest_){devourBest_=body.mass;SaveSlimeLab();}
    Finish(p,devourNewBest_||devourReachedFinal_);
}
// 広げた体の下にある、覆える大きさの物をまとめて飲み込む。飲み込んだ数を返す
int ChronoSystem::DevourCapture(entt::registry& r,V landing,float& gained){
    // 途中で大きくなっても、覆った瞬間の大きさで判定する
    float mass=life_.bodies[0].mass;int count=0;
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];
        if(!prop.active||prop.sealed||!DevourCanCover(mass,prop.radius)||!DevourUnderCover(landing,prop.at,mass,prop.radius,prop.reach))continue;
        gained+=DevourEat(r,i);++count;}
    return count;
}
// ずっと小さい物は、通り道で触れただけで沈み込む (前フレームの位置からの線分で調べ、速く動いても取りこぼさない)
int ChronoSystem::DevourTouch(entt::registry& r,V from,float& gained){
    auto& body=life_.bodies[0];if(body.mass<=0||body.down)return 0;
    float mass=body.mass,scale=LifeScale(mass),reach=2.1f*life_.recipe.Spread()*scale;int count=0;
    // タイヤで転がっている間は、もっと大きい物まで通り道で吸い込む
    bool tire=body.action==LifeAction::Tire&&body.weapon.ready;float ratio=tire?(body.weapon.dash>0?DevourTireDashSinkRatio:DevourTireSinkRatio):DevourSinkRatio;
    if(tire)reach=std::max(reach,(LifeTireRadius+1)*scale);
    V travel=body.at-from;travel.y=0;float squared=Dot(travel,travel);
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||!DevourSinks(mass,prop.radius,ratio))continue;
        V delta=prop.at-from;delta.y=0;float t=squared>.00001f?std::clamp(Dot(delta,travel)/squared,0.f,1.f):0;V nearest=Lerp(from,body.at,t);
        if(LifeDistance(nearest,prop.at)>reach+prop.radius*.5f+(tire?prop.reach:0)||std::abs(nearest.y-prop.at.y)>2*scale+prop.radius)continue;
        gained+=DevourEat(r,i);++count;}
    return count;
}
// 炎などで体の一部が剥がれる: 小さくなり、剥がれた分は周りに光る球で落ちる (拾い直せる)
void ChronoSystem::DevourShedBody(Player& p,GameContext& ctx,float damage,V source){
    auto& body=life_.bodies[0];float loss=DevourShed(body.mass,damage);
    p.invincible=.7f;p.damageAge=0;p.damageSource=source;++p.stats.hitsTaken;
    V away=body.at-source;away.y=0;away=Length(away)>.01f?Unit(away):V{body.forward.z,0,-body.forward.x};
    if(loss>0){body.mass-=loss;body.maxHealth=body.health=body.mass;
        float radius=DevourBodyRadius(body.mass);
        for(int k=0;k<3;++k){float a=(float(k)-1)*.7f;V side{away.x*std::cos(a)-away.z*std::sin(a),0,away.x*std::sin(a)+away.z*std::cos(a)};
            LifeCollectible orb;orb.at=body.at+side*(radius+4+2*float(k));orb.at.y=InkGround(orb.at)+1;orb.required=1;orb.reward=loss*.27f;orb.orb=true;orb.radius=.6f;
            life_.props.push_back(orb);}
        life_.message="熱い！ 体が剥がれた -"+std::to_string(int(std::ceil(loss)))+" / 落ちた球は拾い直せる";life_.messageAge=2;}
    ++life_.revision;
    LifeImpact(ctx,ImpactPlayerHurt,body.at,away*.6f+V{0,.8f,0},std::clamp(damage/10,.8f,1.4f));
}
// 飛びかかりを始める: 正面の覆える物へ向かって跳ぶ。満足の間に押せば、次の飛びかかりへつなげる
bool ChronoSystem::StartDevourPounce(Player& p,GameContext& ctx){
    (void)ctx;auto& body=life_.bodies[0];if(body.mass<=0||body.down||finished_)return false;
    bool chain=body.action==LifeAction::Engulf&&body.actionAge>=DevourCoverEnd;
    if(body.action!=LifeAction::Idle&&!chain)return false;
    // 空中からは飛びかからない (連打で空へ上っていかないように、着地を待つ)
    if(!p.grounded)return false;
    // 目標を選び、そちらを向く
    auto pick=DevourChooseTarget(body.at,body.forward,body.mass,life_.props);
    V to=pick.at,delta=to-body.at;delta.y=0;float distance=Length(delta);V direction=distance>.01f?delta*(1/distance):body.forward;
    // 大きすぎる物には、手前で覆いかぶさって弾かれる
    if(!pick.fits&&pick.index>=0){float stop=life_.props[size_t(pick.index)].radius*.5f+DevourBodyRadius(body.mass);to=body.at+direction*std::max(0.f,distance-stop);}
    // 跳べる距離に収める
    V hop=to-body.at;hop.y=0;float range=DevourLeapRange(body.mass);if(Length(hop)>range)to=body.at+Unit(hop)*range;
    // 仮足の形は飛びかかりごとに変える
    devourPounce_={body.at,to,pick.index,pick.fits,false,false,std::uniform_real_distribution<float>(0,100)(DevourRandom())};
    // 液体から包み込みの形 (体の各点の行き先) へ移るので、最初の 1 回だけ粒子の割り当てを作り直す。
    // 続けて飛びかかるときは包み込みの形のままなので作り直さない
    body.forward=direction;body.action=LifeAction::Engulf;body.actionAge=0;body.grabs.clear();body.committed=false;body.restoreDuration=.12f;
    life_.tackleTime=0;if(!chain)++life_.revision;return true;
}
// 体と同じ流体の粒子を周りへばらまく (本体と同じ流体シミュレーションの、弾道で飛ぶ粒子。描き方も本体と同じ)。
// 飛沫は毎回ばらばら: 向き・高さ・速さ・しずくの大きさ・粒の数・飛ぶ時間をすべて乱数で決める。
// 大きめのしずくを count 個と、細かく速く飛ぶ霧のような飛沫をその半分混ぜる。
// (多すぎると体の前に粒の輪ができ、飛ぶ範囲を描く範囲に足すほど流体の升目が粗くなって体もごつごつする)
// 1 本の粒は飛ぶ間に少しずつ速さがずれてちぎれ、しずくは進む向きに伸び、地面に落ちると平たく広がって消える (シェーダー)。
// しずくは体の大きさに合わせて大きくする (大きい体は流体を描く升目が粗く、小さいままだと見えなくなる)
void ChronoSystem::DevourSplash(V at,float radius,int count,float strength){
    auto* renderer=Engine::Renderer::GetInstance();auto rgb=life_.recipe.color.ToRGB();const Engine::Vector4 color{rgb.x,rgb.y,rgb.z,1};
    auto uniform=[&](float low,float high){return std::uniform_real_distribution<float>(low,high)(DevourRandom());};
    float ground=InkGround(at),base=std::max(.3f,.3f*LifeScale(life_.bodies[0].mass));
    // 流体を描く範囲に、しずくが飛ぶ範囲を入れる (体の範囲だけだと外へ飛んだしずくが切れる)
    devourSplashes_.push_back({V{at.x,ground+5*strength,at.z},radius+7*strength+2*base,0});
    auto throwDrop=[&](float sizeScale,float speedScale,int particlesLow,int particlesHigh,float lifeLow,float lifeHigh){
        float a=uniform(0,6.2831853f);V direction{std::cos(a),0,std::sin(a)};
        // 体の表面の少し外から飛ばす (内側から出すと体の表面と溶け合って、ざらざらの皮になる)
        V from=V{at.x,ground+uniform(.3f,1.2f)*strength,at.z}+direction*(radius*uniform(1.f,1.3f));
        V velocity=direction*(uniform(4,11)*strength*speedScale)+V{0,uniform(4,11)*strength*speedScale,0};
        int particles=int(uniform(float(particlesLow),float(particlesHigh)+.99f));
        float life=uniform(lifeLow,lifeHigh);
        renderer->EmitGPUFluid(EV(from),EV(velocity*.2f),color,particles,4+life/10,false,base*sizeScale*uniform(.55f,1.15f));};
    // しずく 1 つは 1〜5 粒。粒を多く詰めると隣と溶け合って、ごつごつした塊になる
    // 大きめのしずく
    for(int i=0;i<count;++i)throwDrop(1,1,1,3,.45f,.8f);
    // 細かい霧のような飛沫 (小さく速い)
    for(int i=0;i<count/2;++i)throwDrop(.45f,1.35f,1,2,.3f,.55f);
}
// 飛びかかりを進める: 溜め → 跳ぶ → 覆う (飲み込む・弾かれる) → 満足。体の中の物の時間も進める
void ChronoSystem::TickDevourPounce(entt::registry& r,Player& p,GameContext& ctx,float dt){
    auto& body=life_.bodies[0];
    DevourDigest(body,devourDigest_,dt);devourGulp_.age+=dt;
    if(devourOpenAll_){devourOpenAll_=false;for(int z=1;z<RingStyleCount;++z)DevourOpenRing(r,z);devourRingOpened_=0;}
    // 輪が開いたら大きく知らせる
    if(devourRingOpened_){life_.message=RingZones()[size_t(devourRingOpened_)].name+"が開いた！";life_.messageAge=3;devourRingOpened_=0;
        LifeImpact(ctx,ImpactStructureBreak,body.at);auto* audio=Engine::Audio::GetInstance();audio->Play(hitSound_,false,.8f*audio->GetMasterSEVolume(),.6f);}
    for(auto& meal:devourMeals_)meal.age+=dt;
    devourMeals_.erase(std::remove_if(devourMeals_.begin(),devourMeals_.end(),[](const DevourMeal& m){return m.age>kMealTime;}),devourMeals_.end());
    // しずくが飛んでいる範囲は、しずくが消えるまで (0.6 秒) 覚えておく
    for(auto& splash:devourSplashes_)splash.age+=dt;
    devourSplashes_.erase(std::remove_if(devourSplashes_.begin(),devourSplashes_.end(),[](const DevourSplashArea& s){return s.age>.7f;}),devourSplashes_.end());
    devourClock_.comboClock=std::max(0.f,devourClock_.comboClock-dt);if(devourClock_.comboClock<=0)devourClock_.combo=0;
    if(body.action!=LifeAction::Engulf)return;
    float age=body.actionAge,next=age+dt,scale=LifeScale(body.mass);
    V leap=devourPounce_.to-devourPounce_.from;leap.y=0;float distance=Length(leap);V direction=distance>.01f?leap*(1/distance):body.forward;
    if(age<DevourLandAt)body.forward=direction;
    // 溜め: その場で踏ん張る
    if(age<DevourWindup){float brake=std::exp(-24*dt);p.velocity.x*=brake;p.velocity.z*=brake;
        if(next>=DevourWindup){auto* audio=Engine::Audio::GetInstance();audio->Play(lifeSplitSounds_[1],false,.3f*audio->GetMasterSEVolume(),1.5f);}}
    // 口を開けて飛び込む (ズバーッ) から包む (ガバッ) まで: 勢いよく飛び出し、口で包みながら目標の上で止まる。上には跳ばない
    if(next>DevourWindup&&age<DevourCoverEnd&&dt>0&&!devourPounce_.bounced){float speed=distance*(DevourLeapProgress(next)-DevourLeapProgress(age))/dt;
        p.velocity.x=direction.x*speed;p.velocity.z=direction.z*speed;
        if(next>=DevourCoverEnd)p.velocity.x=p.velocity.z=0;}
    // 覆う: 体の上の方が物の上へ届いた瞬間に、行き先で覆える物をまとめて飲み込む
    // (根元は覆う間に追いつく。壁などで行き先まで進めていなければ今の位置で調べる)
    if(!devourPounce_.covered&&next>=DevourLandAt){devourPounce_.covered=true;
        V rest=devourPounce_.to-body.at;rest.y=0;V landing=Length(rest)<=distance*(1-DevourLeapProgress(age))+.5f?devourPounce_.to:body.at;landing.y=body.at.y;
        float gained=0;int count=DevourCapture(r,landing,gained);
        const auto* target=devourPounce_.target>=0&&size_t(devourPounce_.target)<life_.props.size()?&life_.props[size_t(devourPounce_.target)]:nullptr;
        bool bounced=target&&target->active&&!devourPounce_.fits;
        auto* audio=Engine::Audio::GetInstance();float cover=DevourCoverRadius(body.mass);
        if(count){++devourClock_.combo;devourClock_.comboClock=1.4f;
            // ガバッ: 止めずに体をつぶして揺らす。飲み込む音はコンボで高くし、たくさん飲むほど大きく低くする
            float weight=DevourGulpWeight(count,gained,body.mass);devourGulp_={0,weight};
            audio->Play(lifeEngulfSound_,false,(.6f+.3f*weight)*audio->GetMasterSEVolume(),std::clamp(.9f+.07f*float(devourClock_.combo)-.2f*weight,.7f,1.8f));
            LifeImpact(ctx,ImpactDevour,body.at,{0,1,0},.8f+.7f*weight);
            life_.message=std::string(devourClock_.combo>1?"ガバッ！ "+std::to_string(devourClock_.combo)+"コンボ":"ガバッ！")+"  "+std::to_string(count)+"個 / 成長 +"+DevourAmount(gained);life_.messageAge=1.4f;}
        // 体のしずくが周りへ飛び散る (飲み込んだ数が多いほど多く)
        DevourSplash(landing,cover*.8f,count?std::min(30,16+3*count):8,std::sqrt(scale));
        // 大きすぎた: ぼよんと弾かれて少しよろける
        if(bounced){devourPounce_.bounced=true;p.velocity=direction*-(7+1.5f*scale)+V{0,5,0};p.grounded=false;body.aspectVelocity-=8;
            if(!count)devourGulp_={0,.5f};
            LifeImpact(ctx,ImpactDevourBounce,body.at,direction*-1.f+V{0,.5f,0});
            audio->Play(lifeSplitSounds_[2],false,.35f*audio->GetMasterSEVolume(),1.6f);
            if(!count){life_.message="ぼよん！ まだ大きい / 本体 "+std::to_string(int(DevourMassToCover(target->radius)))+" で飲み込める";life_.messageAge=1.8f;}}
    }
    // (包み込んでいる間に口の縁から液をこぼすと、唇がちぎれて破片が飛んでいるように見えるので出さない。
    //  飛び散るのは覆った瞬間の 1 回だけ)
    // 満足: 空洞を閉じたドームから液体の体へ戻り、ばねでぷるんと弾む
    if(next>=DevourPounceTime){body.action=LifeAction::Idle;body.actionAge=0;body.aspect=1;body.aspectVelocity=4+2*(devourGulp_.age<.7f?devourGulp_.weight:0);}
}
// 体の中に見えたまま、中心へ沈みながら溶けていく物を描く
void ChronoSystem::DrawDevourMeals(GameContext& ctx){
    const auto& body=life_.bodies[0];if(body.mass<=0)return;
    V radii=LifeRadii(body,life_.recipe);V core=body.at+body.fluidOffset+V{0,-1.25f+radii.y*.45f,0};
    for(const auto& meal:devourMeals_){float t=LifeSmooth(meal.age/kMealTime),shrink=meal.fit*(1-.85f*t);
        // ゆっくり回りながら中心へ寄る
        float a=meal.age*1.7f+meal.offset.x;V at=core+meal.offset*(1-.75f*t)+V{std::sin(a)*.2f,std::sin(a*1.3f)*.25f,std::cos(a)*.2f}*LifeScale(body.mass);
        if(!meal.orb&&DrawCityPropInTentacle(ctx,meal.visual,at,shrink))continue;
        float radius=std::max(LifeOrbRadius(1),meal.radius*.6f)*std::max(.3f,shrink)*1.6f;Engine::Transform t3;t3.translate=EV(at);t3.scale={radius,radius,radius};
        ctx.renderer->DrawMeshInstanced(sphere_,white_,t3,{meal.tint.x,meal.tint.y,meal.tint.z,1});}
}
// 吸収モードの UI: 右上の制限時間・上の大きさの段階と次の段階までのゲージ・左下の大きさとコンボ・飛びかかる目標の印。終わったら結果
void ChronoSystem::DrawDevourUI(GameContext& ctx){
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const auto& body=life_.bodies[0];
    const Engine::Vector4 hazard{.95f,.76f,.19f,1},slime{.61f,.89f,.23f,1},white{.96f,.96f,.94f,1},black{.08f,.08f,.08f,1},red{.9f,.22f,.16f,1};
    auto shadowed=[&](const std::string& text,float x,float y,float size,Engine::Vector4 color,float align=0){
        x-=ui.Width(text,size,UI::JapaneseFont)*align;ui.Text(text,x,y+size*.09f,size,{0,0,0,.85f*color.w},UI::JapaneseFont);ui.Text(text,x,y,size,color,UI::JapaneseFont);};
    // 街の物を飲み込んだ割合 (塀や落とした球は数えない)
    int total=0,eaten=0;for(const auto& prop:life_.props)if(!prop.orb&&prop.visual!=0xffffffffu){++total;if(!prop.active)++eaten;}
    char size[32];snprintf(size,sizeof(size),"%.1f m",DevourSizeMeters(body.mass));char best[32];snprintf(best,sizeof(best),"%.1f m",DevourSizeMeters(devourBest_));
    if(finished_){
        // 結果画面の文字は英字のフォントで描かれるので、英語で出す
        ui.Result(won_,devourReachedFinal_?"CITY DEVOURED":devourNewBest_?"NEW RECORD":"TIME UP",std::string("称号: ")+DevourTiers[size_t(devourClock_.tier)].name,"FINAL SIZE",size,
            std::string(devourNewBest_?"NEW BEST":"BEST ")+(devourNewBest_?"":best)+"   /   CITY "+std::to_string(total?eaten*100/total:0)+"%   /   "+std::to_string(life_.absorbed)+" ITEMS");
        return;}
    // 右上: 残り時間 (残り 30 秒で赤く脈打つ)。段階が上がった直後は足した時間を出す
    {float left=devourClock_.left;bool hurry=left<=30;float pulse=hurry?.5f+.5f*std::sin(life_.age*10):0;
        ui.Dot(1196,66,48+4*pulse,{0,0,0,.4f});ui.Dot(1196,62,46+4*pulse,hurry?red:hazard);ui.Dot(1196,62,39,black);
        ui.Center(DevourClockText(left),1196,42,30,hurry?red:white,UI::JapaneseFont);
        shadowed("のこり時間",1196,114,12,white,.5f);
        if(devourClock_.tierFlash>0)shadowed("+"+std::to_string(int(DevourTierBonus))+"秒",1196,132,18,UI::WithAlpha(slime,std::min(1.f,devourClock_.tierFlash)),.5f);}
    // 上中央: 今の大きさの段階と、次の段階までのゲージ
    {int tier=devourClock_.tier;const auto& now=DevourTiers[size_t(tier)];
        float flash=std::min(1.f,devourClock_.tierFlash);
        shadowed(now.name,640,16,22+8*flash,UI::WithAlpha(flash>0?hazard:white,1),.5f);
        if(tier<DevourFinalTier){const auto& next=DevourTiers[size_t(tier+1)];float progress=std::clamp((body.mass-now.mass)/(next.mass-now.mass),0.f,1.f);
            ui.Fill({438,54,404,12},black);ui.Fill({440,56,400*progress,8},slime);
            shadowed(std::string("次: ")+next.name+" → "+next.food,640,72,13,hazard,.5f);}
        // 次に開ける門の案内
        for(int z=1;z<RingStyleCount;++z)if(!ringOpen_[size_t(z)]){float radius=RingZones()[size_t(z)].gateRadius;
            shadowed(RingZones()[size_t(z)].name+"の門: 本体 "+DevourAmount(DevourMassToCover(radius))+" で飲める / ハンマーなら "+DevourAmount(std::max(LifeHammerMass,DevourMassToShatter(radius)))+" で砕ける",640,92,13,white,.5f);break;}}
    // 左下: 大きさ・飲み込んだ割合・コンボ
    {shadowed(std::string("大きさ ")+size,28,596,24,white);
        shadowed("街の "+std::to_string(total?eaten*100/total:0)+"% を吸収  /  "+std::to_string(life_.absorbed)+"個",28,630,14,hazard);
        // 自己ベストを超えたら色を変えて知らせる
        if(devourBest_>0)shadowed(std::string(body.mass>devourBest_?"自己ベスト更新中！":"自己ベスト ")+(body.mass>devourBest_?"":best),28,650,14,body.mass>devourBest_?slime:white);
        if(devourClock_.combo>1)shadowed(std::to_string(devourClock_.combo)+" コンボ！",28,556,26*(1+.2f*std::min(1.f,devourClock_.comboClock)),slime);}
    // 飛びかかる目標の印: 覆える物は「！」、大きすぎる物は「×」
    if(body.action==LifeAction::Idle&&!life_.lab){auto pick=DevourChooseTarget(body.at,body.forward,body.mass,life_.props);
        if(pick.index>=0){const auto& prop=life_.props[size_t(pick.index)];float x,y;
            if(DevourScreen(ctx,prop.at+V{0,prop.radius+2.5f*LifeScale(body.mass),0},x,y)&&x>20&&x<1260&&y>90&&y<560){
                float bob=4*std::sin(life_.age*8);
                if(pick.fits){ui.Dot(x,y+bob,15,black);ui.Dot(x,y+bob,13,hazard);ui.Center("!",x,y-14+bob,22,black,UI::JapaneseFont);}
                else if(DevourCanShatter(body.mass,prop.radius)&&prop.morphItem==LifeMorphItem::None&&LifeCanMorph(body,LifeAction::Hammer)){ui.Dot(x,y+bob,15,black);ui.Dot(x,y+bob,13,red);ui.Center("砕",x,y-12+bob,18,white,UI::JapaneseFont);}
                else{ui.Dot(x,y,13,UI::WithAlpha(black,.7f));ui.Center("×",x,y-12,18,red,UI::JapaneseFont);}}
            if(!pick.fits&&DevourCanShatter(body.mass,prop.radius)&&prop.morphItem==LifeMorphItem::None&&LifeCanMorph(body,LifeAction::Hammer))ui.Center("ハンマーで叩くと砕ける！",640,590,16,UI::Lime,UI::JapaneseFont);
            if(pick.fits)ui.Center(std::string(UI::UsingPad()?"LT":"右クリック")+"で飛びかかって丸のみ！ 押し続けると連続",640,590,16,UI::Lime,UI::JapaneseFont);}}
}
}
