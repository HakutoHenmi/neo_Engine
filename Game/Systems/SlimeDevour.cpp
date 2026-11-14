// SlimeDevour.cpp
// 吸収モード (ChronoSystem の一部)。敵はおらず、制限時間内に物を飲み込んで大きくなり、街を飲み込む。
// 吸収ボタンで全身ごと飛びかかり (溜め → 跳ぶ → 覆う → 満足)、広げた体の下の物をまとめて取り込む。
// ずっと小さい物は触れるだけで沈み込む。規則は DevourRules.h。
#include "ChronoSystemInternal.h"
#include "../Chrono/DevourAnimation.h"
#include "../Data/GameData.h"
#include "../Data/Collection.h"
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
                b.kind=Chrono::DevourBlobKindFromName(blob.value("kind",std::string("lump")));
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
        for(const auto& blob:key.blobs){nlohmann::json b;b["kind"]=Chrono::DevourBlobKindName(blob.kind);
            b["at"]={blob.at.x,blob.at.y,blob.at.z};b["size"]={blob.size.x,blob.size.y,blob.size.z};
            if(blob.kind!=Chrono::DevourBlobKind::Lump){b["thickness"]=blob.thickness;b["reach"]=blob.reach;b["curl"]=blob.curl;b["wave"]=blob.wave;b["tentacles"]=blob.tentacles;b["twist"]=blob.twist;}
            k["blobs"].push_back(b);}
        json["keys"].push_back(k);}
    std::ofstream file(kDevourAnimPath);if(!file){message=std::string("保存できません: ")+kDevourAnimPath;return false;}
    file<<json.dump(2);message=std::string("保存しました: ")+kDevourAnimPath;return true;
}
bool ReloadDevourAnim(std::string& message){return ReadDevourAnim(DevourAnim(),message);}
namespace {
// 体の中に残して見せる数 (超えたら一番小さい物から溶かす) と、これより相対的に小さくなったら溶ける大きさ
constexpr size_t kMealLimit=100;
constexpr float kMealMelt=.04f;
// 皮で包む物の数と、そのうち新しい順に包む数、飲み込むときに伸ばす腕の数
constexpr size_t kMealSkinLimit=64,kMealSkinFresh=40,kBiteArmLimit=16;
// ワールドの点を 1280x720 の画面の位置にする (後ろなら false)
bool DevourScreen(const GameContext& ctx,Vec point,float& x,float& y){
    DirectX::XMFLOAT4 clip;DirectX::XMStoreFloat4(&clip,DirectX::XMVector4Transform(DirectX::XMVectorSet(point.x,point.y,point.z,1),ctx.camera->View()*ctx.camera->Proj()));
    if(clip.w<=.1f)return false;x=(clip.x/clip.w*.5f+.5f)*1280;y=(.5f-clip.y/clip.w*.5f)*720;return true;
}
// 残り時間を「分:秒」にする
std::string DevourClockText(float seconds){int s=int(std::ceil(std::max(0.f,seconds)));char text[16];snprintf(text,sizeof(text),"%d:%02d",s/60,s%60);return text;}
// 増えた量を整数の文字列にする
std::string DevourAmount(float value){return std::to_string(int(std::round(value)));}
// 漫画の描き文字: 1 枚の画像の中の位置 (scripts/lowpoly/make_comic.py が作る)。x, y は言葉の中の位置
struct ComicPart {float u=0,v=0,w=0,h=0,x=0,y=0;};
struct ComicTable {float size=2048;std::map<std::string,std::vector<std::string>> sets;std::map<std::string,std::vector<ComicPart>> words;std::map<std::string,ComicPart> marks;};
const ComicTable& Comic(){
    static const ComicTable table=[]{ComicTable out;const auto& json=Data::Load("comic_words.json");out.size=json.at("size").get<float>();
        auto part=[](const nlohmann::json& j){ComicPart p;p.u=j.at("u").get<float>();p.v=j.at("v").get<float>();p.w=j.at("w").get<float>();p.h=j.at("h").get<float>();p.x=j.value("x",0.f);p.y=j.value("y",0.f);return p;};
        for(const auto& [name,list]:json.at("sets").items())out.sets[name]=list.get<std::vector<std::string>>();
        for(const auto& [word,glyphs]:json.at("words").items())for(const auto& g:glyphs)out.words[word].push_back(part(g));
        for(const auto& [name,rect]:json.at("lines").items())out.marks[name]=part(rect);
        return out;}();
    return table;
}
// その組の言葉を 1 つ選ぶ
const std::string& DevourWord(const char* set){const auto& list=Comic().sets.at(set);return list[size_t(std::uniform_int_distribution<int>(0,int(list.size())-1)(DevourRandom()))];}
float DevourSeed(){return std::uniform_real_distribution<float>(0,1)(DevourRandom());}
}
// ギミック (DevourGimmick.cpp) からも描き文字・乱数・画面の位置を使う
const std::string& GimmickWord(const char* set){return DevourWord(set);}
float GimmickSeed(){return DevourSeed();}
bool GimmickScreen(const GameContext& ctx,Chrono::Vec point,float& x,float& y){return DevourScreen(ctx,point,x,y);}
// 小物を 1 つ飲み込む: 隠して成長し、体の中に見せる。塀・バリケード・シャッターなら道を開ける。増えた量を返す
float ChronoSystem::DevourEat(entt::registry& r,size_t index){
    if(index>=life_.props.size()||!life_.props[index].active)return 0;
    auto& prop=life_.props[index];auto& body=life_.bodies[0];devourEatAt_=prop.at+V{0,prop.radius,0};DevourRecordEaten(r,prop.visual);DevourRemoveProp(r,index);
    // 限界に近い物は重くて遅くなる。画面では「大きくなった」と見せる (飲んだ瞬間に膨らんで光の輪、遅い間は金色に脈打つ)
    {const float heavy=DevourHeavyTime(body.mass,prop.radius);if(heavy>0){devourHeavyKick_=1;if(heavy>=devourHeavy_)devourHeavyPeak_=heavy;DevourGrowBurst(std::clamp(heavy-.5f,0.f,1.f));}
        devourHeavy_=std::max(devourHeavy_,heavy);}
    // 絵の具で塗った色と同じ色の物は多く増える
    const float paint=GimmickFoodBonus(prop);if(paint>1&&gimmickMatchCool_<=0){gimmickMatchCool_=.5f;devourPops_.push_back({prop.at+V{0,prop.radius+2,0},DevourWord("paintMatch"),0,.3f,0,DevourSeed()});}
    float gain=DevourSwallow(body.mass,devourDigest_,prop.reward*paint*DevourFoodValue(body.mass,prop.radius,CurrentDevourStage().smallFood));DevourLogEat(prop,gain);++life_.absorbed;LifeUnlockMorphItem(life_,0x80000000u|uint32_t(index));
    // 体の中に残して見せる (モデルか光る球。塀のようにモデルの無い物は見せない)。大きすぎる物は体に収まる大きさにする
    if(prop.visual!=0xffffffffu||prop.orb){
        float bodyRadius=DevourBodyRadius(body.mass);auto uniform=[](float low,float high){return std::uniform_real_distribution<float>(low,high)(DevourRandom());};
        if(devourMeals_.size()>=kMealLimit)devourMeals_.erase(std::min_element(devourMeals_.begin(),devourMeals_.end(),[](const DevourMeal& a,const DevourMeal& b){return a.radius*a.fit<b.radius*b.fit;}));
        // 飲んだ方向にくっつく。重なって見えなくならないよう、その近くで他の物と一番離れた向きを選ぶ
        V side=prop.at-body.at;side.y=0;side=Length(side)>.01f?Unit(side):body.forward;
        V dir{};float widest=-1;for(int k=0;k<24;++k){V candidate=Unit(side+V{uniform(-1.1f,1.1f),uniform(.6f,2.4f),uniform(-1.1f,1.1f)});
            float nearest=4;for(const auto& other:devourMeals_){V d=candidate-other.dir;nearest=std::min(nearest,Dot(d,d));}
            if(nearest>widest){widest=nearest;dir=candidate;}}
        DevourMeal meal;meal.visual=prop.visual;meal.orb=prop.orb;meal.dir=dir;meal.radius=prop.radius;meal.tint=LifeOrbColor(prop.reward);
        meal.fit=std::min(1.f,bodyRadius*.55f/std::max(.1f,prop.radius));
        // 塊のように積む: 前に飲んだ物に重ならない所まで外へ出す (新しい物がいつも一番外に見える)
        float size=meal.radius*meal.fit;for(;meal.lift<2.4f;meal.lift+=.1f){V at=DevourMealAt(meal);bool hit=false;
            for(const auto& other:devourMeals_){float gap=(other.radius*other.fit+size)*.8f;V d=at-DevourMealAt(other);hit|=Dot(d,d)<gap*gap;}
            if(!hit)break;}
        devourMeals_.push_back(meal);}
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
    ringOpen_[size_t(ring)]=true;devourRingOpened_=ring;DevourLogEvent("ring_open_"+std::to_string(ring));
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
    LifeImpact(ctx,ImpactHammerHit,prop.at);PlayDevourSound(devourSounds_.shatter,.75f);
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
        if(DevourCanCover(mass,prop.radius)){devourEatBy_=4;gained+=DevourEat(r,i);++eaten;}
        else if(prop.morphItem==LifeMorphItem::None&&DevourCanShatter(mass,prop.radius))DevourShatter(r,ctx,i);
        else if(life_.messageAge<=0){life_.message="硬い！ 本体 "+std::to_string(int(DevourMassToShatter(prop.radius)))+" で砕ける";life_.messageAge=1.4f;LifeImpact(ctx,ImpactDevourBounce,prop.at);PlayDevourSound(devourSounds_.hit,.5f);}}
    if(eaten){float weight=DevourGulpWeight(eaten,gained,mass);PlayDevourGulp(weight,devourClock_.combo);
        LifeImpact(ctx,ImpactDevour,b.at,{0,1,0},.8f+.7f*weight);}
}
// タイヤの溜め突進: 砕ける大きさの物にぶつかると砕く
void ChronoSystem::DevourSmash(entt::registry& r,GameContext& ctx,float reach){
    const auto& body=life_.bodies[0];
    size_t count=life_.props.size();
    for(size_t i=0;i<count;++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||prop.morphItem!=LifeMorphItem::None||!DevourCanShatter(body.mass,prop.radius))continue;
        if(LifeDistance(prop.at,body.at)<=reach+prop.radius*.5f+prop.reach)DevourShatter(r,ctx,i);}
}
// 吸収モードの音を読む
void ChronoSystem::LoadDevourSounds(){
    StopLoopSounds();auto* audio=Engine::Audio::GetInstance();
    if(devourSounds_.note!=DevourSounds::None)return;
    auto load=[&](const std::string& name){return audio->Load("Resources/Sound/Devour/"+name+".wav");};
    auto& s=devourSounds_;for(int i=0;i<4;++i)s.gulpSmall[i]=load("gulp-small-"+std::to_string(i));for(int i=0;i<2;++i)s.gulpMid[i]=load("gulp-mid-"+std::to_string(i));
    s.gulpBig=load("gulp-big");s.note=load("note");s.splat=load("splat");s.pounce=load("pounce");s.bounce=load("bounce");s.hit=load("hit");s.shatter=load("shatter");
    s.vacuumLoop=load("vacuum-loop");s.rankUp=load("rank-up");s.rankTop=load("rank-top");s.gateOpen=load("gate-open");s.tick=load("tick");s.hurry=load("hurry");s.timeUp=load("time-up");s.morph=load("morph");
}
void ChronoSystem::StopLoopSounds(){if(devourVacuumVoice_){Engine::Audio::GetInstance()->Stop(devourVacuumVoice_);devourVacuumVoice_=0;}}
void ChronoSystem::PlayDevourSound(uint32_t sound,float volume,float pitch){auto* audio=Engine::Audio::GetInstance();audio->Play(sound,false,volume*audio->GetMasterSEVolume(),pitch);}
// 同じ音が続かないよう順に使い、少しだけ高さを揺らす。音階は短い間に重ならないよう間を空ける (掃除機は続けて飲むため)
void ChronoSystem::PlayDevourGulp(float weight,int combo){
    uint32_t turn=devourGulpTurn_++;float sway=1+.03f*(float(turn%3)-1);const auto& s=devourSounds_;
    if(weight>=.6f)PlayDevourSound(s.gulpBig,.8f+.2f*weight,sway);
    else if(weight>=.3f)PlayDevourSound(s.gulpMid[turn%2],.6f+.4f*weight,sway);
    else PlayDevourSound(s.gulpSmall[turn%4],.5f+.5f*weight,sway);
    if(life_.age-devourNoteAt_<.06f)return;
    devourNoteAt_=life_.age;PlayDevourSound(s.note,.2f+.12f*weight,DevourComboPitch(combo));
}
// 残り 30 秒で知らせて BGM を速くし、残り 10 秒から秒読みする
void ChronoSystem::TickDevourTimeSounds(float before){
    float left=devourClock_.left;
    if(before>DevourHurryTime&&left<=DevourHurryTime){PlayDevourSound(devourSounds_.hurry,.7f);Engine::Audio::GetInstance()->SetBGMPitch(DevourHurryMusicPitch);}
    if(left>0&&left<=DevourCountdownTime&&std::ceil(before)!=std::ceil(left))PlayDevourSound(devourSounds_.tick,.6f,1+.05f*(DevourCountdownTime-std::ceil(left)));
    if(before>0&&left<=0)PlayDevourSound(devourSounds_.timeUp,.8f);
}
// 結果で測る本物の体の外形を求める: GPU 流体の粒子と、飲んだ物を包む皮の両方を、カメラの横方向と高さで調べる
void ChronoSystem::MeasureDevourBody(GameContext& ctx){
    const auto& body=life_.bodies[0];const V side{std::cos(yaw_),0,-std::sin(yaw_)};
    const float reach=DevourBodyRadius(body.mass);float lo=-reach,hi=reach,nearest=-reach,farthest=reach,top=body.at.y+reach*.6f;const V flat{std::sin(yaw_),0,std::cos(yaw_)};
    const auto& fluid=ctx.renderer->GetFluidBodySnapshot();
    if(fluid.offsets.size()>64){std::vector<float> across,depth,height;const size_t step=std::max<size_t>(1,fluid.offsets.size()/2048);
        for(size_t i=0;i<fluid.offsets.size();i+=step){const auto& o=fluid.offsets[i];V at{fluid.core.x+o.x,fluid.core.y+o.y,fluid.core.z+o.z};across.push_back(Dot(at-body.at,side));depth.push_back(Dot(at-body.at,flat));height.push_back(at.y);}
        // はみ出した滴を除く
        std::sort(across.begin(),across.end());std::sort(depth.begin(),depth.end());std::sort(height.begin(),height.end());const size_t trim=across.size()/50;
        lo=across[trim];hi=across[across.size()-1-trim];nearest=depth[trim];farthest=depth[depth.size()-1-trim];top=height[height.size()-1-trim];}
    for(const auto& arm:ctx.renderer->GetFluidTendrils())for(int end=0;end<2;++end){const auto& p=end?arm.end:arm.start;const float radius=end?arm.endRadius:arm.radius;
        const V offset=V{p.x,p.y,p.z}-body.at;
        // 体から遠い節 (使っていない枠など) は外形に入れない
        if(Length(offset)>reach*2.5f)continue;
        const float across=Dot(offset,side),along=Dot(offset,flat);lo=std::min(lo,across-radius);hi=std::max(hi,across+radius);nearest=std::min(nearest,along-radius);farthest=std::max(farthest,along+radius);top=std::max(top,p.y+radius);}
    // 粒子の揺れで線が震えないよう、なめらかに追う
    const float follow=resultMeasured_?.12f:1.f;resultMeasured_=true;resultOrigin_=body.at;
    resultLo_+=(lo-resultLo_)*follow;resultHi_+=(hi-resultHi_)*follow;resultNear_+=(nearest-resultNear_)*follow;resultFar_+=(farthest-resultFar_)*follow;resultTopY_+=(top-resultTopY_)*follow;
}
// 時間切れの大きさを記録して終わる。最初の目標に届いていればクリア
void ChronoSystem::FinishDevour(Player& p){
    Engine::LoadProfile::Scope profile("result","FinishDevour");
    StopLoopSounds();
    auto& body=life_.bodies[0];DevourGrow(body,devourDigest_);devourDigest_=0;
    const auto& stage=CurrentDevourStage();float& best=devourBests_[size_t(devourStage_)];bool clear=DevourRankAt(stage,body.mass)>0;
    {Engine::LoadProfile::Scope step("result","SaveDevourLog");SaveDevourLog(devourClock_.left<=0?"time_up":"all_eaten");}
    // 図鑑に足し、結果画面の「おなかの中」の升を作る
    {Engine::LoadProfile::Scope step("result","Collection::Add + BuildDevourShelf");std::vector<std::string> ids;ids.reserve(devourEaten_.size());for(const auto& eaten:devourEaten_)ids.push_back(eaten.id);BuildDevourShelf(Collection::Add(ids));}
    devourNewBest_=body.mass>best;if(devourNewBest_)best=body.mass;if(clear)++lifeClears_;
    if(devourNewBest_||clear){Engine::LoadProfile::Scope step("result","SaveSlimeLab");SaveSlimeLab();}
    {Engine::LoadProfile::Scope step("result","Finish (music)");Finish(p,clear);}
}
// 保存したステージを読む (--devour-stage=番号 があれば、そのステージを使う)
void ChronoSystem::LoadDevourStage(){
    if(devourStageLoaded_)return;devourStageLoaded_=true;
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-stage="))devourStage_=_wtoi(arg+15);
    else if(!wcsstr(GetCommandLineW(),L"--slime-life-smoke"))try{std::ifstream file(RecordPath().parent_path()/L"slime-lab-v1.json");nlohmann::json data;if(file){file>>data;devourStage_=data.value("devourStage",0);}}catch(...){}
    devourStage_=std::clamp(devourStage_,0,int(DevourStages().size())-1);
}
// 広げた体の下にある、覆える大きさの物をまとめて飲み込む。飲み込んだ数を返す
int ChronoSystem::DevourCapture(entt::registry& r,V landing,float& gained){
    // 途中で大きくなっても、覆った瞬間の大きさで判定する
    float mass=life_.bodies[0].mass;int count=0;devourEatBy_=2;
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];
        if(!prop.active||prop.sealed||!DevourCanCover(mass,prop.radius)||!DevourUnderCover(landing,prop.at,mass,prop.radius,prop.reach))continue;
        gained+=DevourEat(r,i);++count;}
    return count;
}
// ずっと小さい物は、通り道で触れただけで沈み込む (前フレームの位置からの線分で調べ、速く動いても取りこぼさない)
void ChronoSystem::DevourTouch(entt::registry& r,V from){
    auto& body=life_.bodies[0];if(body.mass<=0||body.down||devourStun_>0)return;
    float mass=body.mass,scale=LifeScale(mass),reach=2.1f*life_.recipe.Spread()*scale;
    // タイヤは幅が広いので、通り道の物をより広く吸い込む
    bool tire=body.action==LifeAction::Tire&&body.weapon.ready;
    if(tire)reach=std::max(reach,(LifeTireRadius+1)*scale);
    V travel=body.at-from;travel.y=0;float squared=Dot(travel,travel);
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||!DevourCanCover(mass,prop.radius))continue;
        V delta=prop.at-from;delta.y=0;float t=squared>.00001f?std::clamp(Dot(delta,travel)/squared,0.f,1.f):0;V nearest=Lerp(from,body.at,t);
        if(std::abs(nearest.y-prop.at.y)>2*scale+prop.radius)continue;
        float gap=LifeDistance(nearest,prop.at);
        // 飲めるのは体に触れた物だけ。少し大きい物が前に近づくと、飲む前に口を開ける
        if(gap>reach+prop.radius*.5f+(tire?prop.reach:0)){V side=prop.at-body.at;side.y=0;
            if(gap<=reach*DevourMouthReach+prop.radius*.5f&&prop.radius>=DevourBodyRadius(mass)*DevourCoverRatio*DevourMouthAhead&&Length(side)>.01f&&Dot(Unit(side),body.forward)>.4f)DevourOpenMouth(prop,false);
            continue;}
        DevourPull(r,i,DevourBiteTime(mass,prop.radius),true);}
}
// 物を体か吸い口へ引き寄せ始める: 他から選ばれないようにして、元の場所の見た目は隠す
void ChronoSystem::DevourPull(entt::registry& r,size_t index,float flight,bool toBody){
    auto& prop=life_.props[index];prop.sealed=true;prop.ring=0;
    if(prop.visual!=0xffffffffu){auto e=entt::entity(prop.visual);if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=false;}
    devourSucks_.push_back({index,prop.at,0,flight,toBody});
    if(toBody)DevourOpenMouth(prop,true);
}
// 口を開ける: 大きい物ほど大きく開ける。続けて飲むときは開いたまま次を飲む。turn なら物の方を向く
void ChronoSystem::DevourOpenMouth(const LifeCollectible& prop,bool turn){
    auto& body=life_.bodies[0];float ratio=prop.radius/(DevourBodyRadius(body.mass)*DevourCoverRatio);if(body.action!=LifeAction::Idle||ratio<DevourMouthSmallest)return;
    float scale=DevourMouthScale(body.mass,prop.radius);V side=prop.at-body.at;side.y=0;if(turn&&Length(side)>.01f)body.forward=Unit(side);
    if(devourBiteAge_<0){devourBiteAge_=DevourWindup+.04f;devourBiteScale_=scale;++life_.revision;}
    else{devourBiteAge_=std::min(devourBiteAge_,DevourLandAt);devourBiteScale_=std::max(devourBiteScale_,scale);}
}
// 飲めない物へ勢いよくぶつかる: ガンッと弾かれて止まり、体が縮み、飲んだ物が飛び出す (拾い直せる)
void ChronoSystem::DevourBump(entt::registry& r,Player& p,GameContext& ctx,V source,float impact){
    auto& body=life_.bodies[0];if(devourBumpCool_>0||body.mass<=0)return;
    const float scale=LifeScale(body.mass);
    // 1 回目は「ゴチン」と止まるだけ。少しの間にもう一度ぶつかると小さくなる
    if(devourBumpMemory_<=0){devourBumpMemory_=DevourBumpMemory;devourBumpCool_=.5f;devourStun_=.14f;V back=body.at-source;back.y=0;back=Length(back)>.01f?Unit(back):body.forward*-1.f;
        p.velocity=back*5.f*std::sqrt(scale);body.aspectVelocity-=5;const float reach=DevourBodyRadius(body.mass);
        devourPops_.push_back({body.at-back*reach+V{0,reach*.8f,0},DevourWord("bumpSoft"),0,.35f,1,DevourSeed()});LifeImpact(ctx,ImpactDevourBounce,body.at,back*.6f+V{0,.5f,0});PlayDevourSound(devourSounds_.bounce,.5f);return;}
    devourBumpMemory_=0;
    float loss=DevourBumpLoss(body.mass,impact);body.mass-=loss;body.maxHealth=body.health=body.mass;
    V away=body.at-source;away.y=0;away=Length(away)>.01f?Unit(away):body.forward*-1.f;
    // 新しく飲んだ物から順に、体の後ろへ飛び出させる
    int left=DevourBumpPops(impact),popped=0;float bodyRadius=DevourBodyRadius(body.mass);
    for(size_t m=devourMeals_.size();m-->0&&left>0;){const auto meal=devourMeals_[m];if(meal.orb||meal.visual==0xffffffffu)continue;
        for(size_t i=0;i<life_.props.size();++i){auto& prop=life_.props[i];if(prop.visual!=meal.visual||prop.active||devourBuildingParts_.count(i))continue;
            float a=(float(popped)-1.5f)*.55f;V side{away.x*std::cos(a)-away.z*std::sin(a),0,away.x*std::sin(a)+away.z*std::cos(a)};
            V at=body.at+side*(bodyRadius*(2.6f+1.6f*impact)+prop.radius+float(popped)*1.5f);at.y=InkGround(V{at.x,body.at.y,at.z})+1.2f;
            if(!DevourWalkable(at,prop.radius*.5f))at=V{body.at.x,at.y,body.at.z};
            // 体の上から飛び出し、弧を描いて落ちる。着地するまでは拾えない
            V from=body.at+V{0,bodyRadius*.9f,0};auto e=entt::entity(prop.visual);DevourFly fly;fly.index=i;fly.from=from;fly.to=at;fly.last=prop.at;fly.time=.55f+.08f*float(popped);fly.height=bodyRadius*1.8f+prop.radius*2;fly.spin=(DevourSeed()<.5f?-1.f:1.f)*(7+5*DevourSeed());
            if(r.valid(e)){if(r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=true;if(auto* t=r.try_get<TransformComponent>(e))fly.rotate=Read(t->rotate);}
            prop.at=at;prop.active=true;prop.sealed=true;devourFlies_.push_back(fly);DevourForgetEaten(prop.visual);if(life_.absorbed>0)--life_.absorbed;
            devourMeals_.erase(devourMeals_.begin()+std::ptrdiff_t(m));--left;++popped;break;}}
    // 弾かれて、しばらく動けない
    p.velocity=away*(7+7*impact)*std::sqrt(scale)+V{0,4+3*impact,0};p.grounded=false;body.aspectVelocity-=8;
    devourStun_=DevourBumpStun(impact);devourBumpCool_=DevourBumpCooldown;devourHeavy_=0;devourClock_.combo=0;++devourLog_.bounces;++life_.revision;
    // ぶつけた所に描き文字を出し、体の周りで小さくなったことを知らせる
    V toward=source-body.at;toward.y=0;toward=Length(toward)>.01f?Unit(toward):body.forward;
    // (暗い影の上に文字が出るよう、影を先に積む)
    devourPops_.push_back({body.at,"",0,impact,2,DevourSeed()});
    devourPops_.push_back({body.at+toward*bodyRadius+V{0,bodyRadius*.8f,0},DevourWord(impact>.5f?"bumpHard":"bumpSoft"),0,.5f+.5f*impact,1,DevourSeed()});
    LifeImpact(ctx,ImpactPlayerHurt,body.at,away*.6f+V{0,.8f,0},.8f+.5f*impact);PlayDevourSound(devourSounds_.hit,.7f);PlayDevourSound(devourSounds_.bounce,.6f);
}
// 飛び出した物を弧の上で動かす: 大きな弧のあと、着地で小さく跳ねて止まる
void ChronoSystem::TickDevourFlies(entt::registry& r,float dt){
    for(auto& fly:devourFlies_){fly.age=std::min(fly.time,fly.age+dt);float u=fly.age/fly.time;V at;
        if(u<.78f){float k=u/.78f;at=Lerp(fly.from,fly.to,k*.9f);at.y=fly.from.y+(fly.to.y-fly.from.y)*k+fly.height*4*k*(1-k);}
        else{float k=(u-.78f)/.22f;at=Lerp(fly.from,fly.to,.9f+.1f*k);at.y=fly.to.y+fly.height*.22f*4*k*(1-k);}
        auto& prop=life_.props[fly.index];auto e=entt::entity(prop.visual);
        if(r.valid(e))if(auto* t=r.try_get<TransformComponent>(e)){t->translate.x+=at.x-fly.last.x;t->translate.y+=at.y-fly.last.y;t->translate.z+=at.z-fly.last.z;
            t->rotate=Write(u<1?fly.rotate+V{fly.spin*fly.age,fly.spin*.6f*fly.age,0}:fly.rotate);}
        fly.last=at;if(u>=1)prop.sealed=false;}
    devourFlies_.erase(std::remove_if(devourFlies_.begin(),devourFlies_.end(),[](const DevourFly& f){return f.age>=f.time;}),devourFlies_.end());
}
// 動く物を登録する: 道は閉じた折れ線で、speed は 1 秒に進む長さ
void ChronoSystem::AddRoute(entt::entity e,std::vector<V> points,float speed,float phase){DevourMover m;m.visual=uint32_t(e);m.points=std::move(points);m.speed=speed;m.phase=phase;devourMovers_.push_back(std::move(m));}
// その場で踊る物: 左右に揺れて、拍に合わせて跳ねる
void ChronoSystem::AddDance(entt::registry& r,entt::entity e,float speed,float sway,float hop){DevourMover m;m.visual=uint32_t(e);m.speed=speed;m.sway=sway;m.hop=hop;m.phase=DevourSeed()*6.283f;const auto& t=r.get<TransformComponent>(e);m.baseY=t.translate.y;m.baseYaw=t.rotate.y;devourMovers_.push_back(std::move(m));}
// 動く物を進める。飲まれた物は止まる
void ChronoSystem::TickDevourMovers(entt::registry& r,float dt){
    for(auto& m:devourMovers_){
        if(m.index<0)for(size_t i=0;i<life_.props.size();++i)if(life_.props[i].visual==m.visual){m.index=int(i);break;}
        if(m.index<0)continue;auto& prop=life_.props[size_t(m.index)];// (飲まれた物は止まる。門の向こうの物は手が届かないだけなので動き続ける)
        if(!prop.active||(prop.sealed&&prop.ring==0)){m.speed=0;continue;}
        auto e=entt::entity(m.visual);auto* t=r.valid(e)?r.try_get<TransformComponent>(e):nullptr;if(!t||m.speed==0)continue;m.age+=dt;
        if(m.points.size()>=2){float total=0;for(size_t i=0;i<m.points.size();++i)total+=LifeDistance(m.points[i],m.points[(i+1)%m.points.size()]);
            float s=std::fmod(m.phase*total+m.age*m.speed,total);
            for(size_t i=0;i<m.points.size();++i){const V from=m.points[i],to=m.points[(i+1)%m.points.size()];const float length=LifeDistance(from,to);if(s>length){s-=length;continue;}
                V at=Lerp(from,to,s/std::max(length,.001f));at.y=prop.at.y;t->translate.x+=at.x-prop.at.x;t->translate.z+=at.z-prop.at.z;prop.at=at;
                // (車の前は +x。曲がり角は少しずつ向きを変える)
                const float want=std::atan2(-(to.z-from.z),to.x-from.x),turn=std::remainder(want-t->rotate.y,6.2831853f);t->rotate.y+=turn*(1-std::exp(-9*dt));break;}}
        else{const float beat=m.age*m.speed+m.phase;t->rotate.z=std::sin(beat)*m.sway;t->rotate.y=m.baseYaw+std::sin(beat*.5f)*m.sway*.5f;t->translate.y=m.baseY+std::abs(std::sin(beat))*m.hop;}}
}
// 飲めない物に当たると止まる。勢いが強いと弾かれる
ChronoSystem::V ChronoSystem::DevourPropBlock(entt::registry& r,Player& p,GameContext& ctx,V old,V next,float topSpeed){
    auto& body=life_.bodies[0];const float mass=body.mass,reach=DevourBodyRadius(mass)*.7f,tall=2*LifeScale(mass);
    for(const auto& prop:life_.props){if(!prop.active||prop.sealed||prop.foot<=0||prop.morphItem!=LifeMorphItem::None||DevourCanCover(mass,prop.radius))continue;
        const float base=prop.at.y-std::min(1.2f,prop.height*.5f);if(old.y>base+prop.height||old.y+tall<base)continue;
        V delta=next-prop.at;delta.y=0;float distance=Length(delta),touch=reach+prop.foot;if(distance>=touch)continue;
        V normal=distance>.001f?delta*(1/distance):body.forward*-1.f;
        next.x=prop.at.x+normal.x*touch;next.z=prop.at.z+normal.z*touch;
        float into=-Dot(p.velocity,normal);if(into<=0)continue;float impact=DevourBumpImpact(into,topSpeed);
        p.velocity=p.velocity+normal*into;
        if(impact>0&&devourBumpCool_<=0){DevourBump(r,p,ctx,prop.at,impact);break;}}
    return next;
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
    (void)ctx;auto& body=life_.bodies[0];if(body.mass<=0||body.down||finished_||devourPounceWait_>0)return false;
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
    body.forward=direction;body.action=LifeAction::Engulf;body.actionAge=0;body.grabs.clear();body.committed=false;body.restoreDuration=Data::SlimeSettings()->engulfRestoreSeconds;
    life_.tackleTime=0;devourPounceWait_=DevourPounceCooldown;++devourLog_.pounces;if(!chain)++life_.revision;return true;
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
    TickDevourLog(dt);DevourDigest(body,devourDigest_,dt);devourGulp_.age+=dt;devourPounceWait_=std::max(0.f,devourPounceWait_-dt);devourSizePulse_=std::max(0.f,devourSizePulse_-dt*4);
    // 大口は形の時間を進め、閉じ終えたら液体の体へ戻る
    if(devourBiteAge_>=0){devourBiteAge_+=dt;if(devourBiteAge_>=DevourPounceTime||body.action!=LifeAction::Idle){if(body.action==LifeAction::Idle){body.aspect=1;body.aspectVelocity=3;}devourBiteAge_=-1;}}
    for(auto& pop:devourPops_)pop.age+=dt;
    devourPops_.erase(std::remove_if(devourPops_.begin(),devourPops_.end(),[](const DevourPop& pop){return pop.age>DevourPopLife(pop.kind);}),devourPops_.end());
    TickDevourFlies(r,dt);TickDevourMovers(r,dt);TickDevourGimmicks(r,ctx,dt);TickDevourSpills(ctx,dt);TickDevourGrowth(dt);if(devour_)TickDevourTrail(p,dt);
    if(devourOpenAll_){devourOpenAll_=false;for(int z=1;z<RingStyleCount;++z)DevourOpenRing(r,z);devourRingOpened_=0;}
    // 輪が開いたら大きく知らせる
    if(devourRingOpened_){life_.message=RingZones()[size_t(devourRingOpened_)].name+"が開いた！";life_.messageAge=3;devourRingOpened_=0;
        LifeImpact(ctx,ImpactStructureBreak,body.at);PlayDevourSound(devourSounds_.gateOpen,.85f);}
    // 体の中の物: 新しい物は表面に付き、後から飲んだ物に埋もれて沈む (飲んだ順に層になる)。体が育って小さく見えるようになった物は溶ける
    {float bodyRadius=DevourBodyRadius(body.mass),cover=0;devourChew_=std::max(0.f,devourChew_-dt);devourHeavy_=std::max(0.f,devourHeavy_-dt);devourBumpCool_=std::max(0.f,devourBumpCool_-dt);devourBumpMemory_=std::max(0.f,devourBumpMemory_-dt);
        for(auto meal=devourMeals_.rbegin();meal!=devourMeals_.rend();++meal){meal->age+=dt;meal->now+=(DevourMealDepth(cover)-meal->now)*(1-std::exp(-5*dt));
            float size=meal->radius*meal->fit/bodyRadius;cover+=size*size;}
        devourMeals_.erase(std::remove_if(devourMeals_.begin(),devourMeals_.end(),[&](const DevourMeal& m){return m.age>2&&m.radius*m.fit<bodyRadius*kMealMelt;}),devourMeals_.end());}
    // しずくが飛んでいる範囲は、しずくが消えるまで (0.6 秒) 覚えておく
    for(auto& splash:devourSplashes_)splash.age+=dt;
    devourSplashes_.erase(std::remove_if(devourSplashes_.begin(),devourSplashes_.end(),[](const DevourSplashArea& s){return s.age>.7f;}),devourSplashes_.end());
    devourClock_.comboClock=std::max(0.f,devourClock_.comboClock-dt);if(devourClock_.comboClock<=0)devourClock_.combo=0;
    // (試験場の取り込みは飛びかかりではないので、ここまでの体の中の物の時間だけを進める)
    if(!devour_||body.action!=LifeAction::Engulf)return;
    float age=body.actionAge,next=age+dt,scale=LifeScale(body.mass);
    V leap=devourPounce_.to-devourPounce_.from;leap.y=0;float distance=Length(leap);V direction=distance>.01f?leap*(1/distance):body.forward;
    if(age<DevourLandAt)body.forward=direction;
    // 溜め: その場で踏ん張る
    if(age<DevourWindup){float brake=std::exp(-24*dt);p.velocity.x*=brake;p.velocity.z*=brake;
        if(next>=DevourWindup)PlayDevourSound(devourSounds_.pounce,.4f);}
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
        float cover=DevourCoverRadius(body.mass);
        if(count){++devourClock_.combo;devourClock_.comboClock=1.4f;
            // ガバッ: 止めずに体をつぶして揺らす。飲み込む音はコンボで高くし、たくさん飲むほど大きく低くする
            float weight=DevourGulpWeight(count,gained,body.mass);devourGulp_={0,weight};
            PlayDevourSound(devourSounds_.splat,.45f+.35f*weight,1.1f-.25f*weight);PlayDevourGulp(weight,devourClock_.combo);
            LifeImpact(ctx,ImpactDevour,body.at,{0,1,0},.8f+.7f*weight);
            life_.message=std::string(devourClock_.combo>1?"ガバッ！ "+std::to_string(devourClock_.combo)+"コンボ":"ガバッ！")+"  "+std::to_string(count)+"個 / 成長 +"+DevourAmount(gained);life_.messageAge=1.4f;}
        else if(!bounced)PlayDevourSound(devourSounds_.splat,.3f,1.25f);
        // 体のしずくが周りへ飛び散る (飲み込んだ数が多いほど多く)
        DevourSplash(landing,cover*.8f,count?std::min(30,16+3*count):8,std::sqrt(scale));
        // 大きすぎた: ぼよんと弾かれて少しよろける
        if(bounced){++devourLog_.bounces;devourPounce_.bounced=true;p.velocity=direction*-(7+1.5f*scale)+V{0,5,0};p.grounded=false;body.aspectVelocity-=8;
            if(!count)devourGulp_={0,.5f};
            LifeImpact(ctx,ImpactDevourBounce,body.at,direction*-1.f+V{0,.5f,0});
            PlayDevourSound(devourSounds_.bounce,.6f);DevourBump(r,p,ctx,target->at,.5f);
            if(!count){life_.message="ぼよん！ まだ大きい / 本体 "+std::to_string(int(DevourMassToCover(target->radius)))+" で飲み込める";life_.messageAge=1.8f;}}
    }
    // (包み込んでいる間に口の縁から液をこぼすと、唇がちぎれて破片が飛んでいるように見えるので出さない。
    //  飛び散るのは覆った瞬間の 1 回だけ)
    // 満足: 空洞を閉じたドームから液体の体へ戻り、ばねでぷるんと弾む
    if(next>=DevourPounceTime){body.action=LifeAction::Idle;body.actionAge=0;body.aspect=1;body.aspectVelocity=4+2*(devourGulp_.age<.7f?devourGulp_.weight:0);}
}
// 掃除機の吸い口 (形の前の端の床の近く)
ChronoSystem::V ChronoSystem::DevourVacuumMouth()const{const auto& body=life_.bodies[0];float scale=LifeScale(body.mass);V right{body.forward.z,0,-body.forward.x};return body.at+body.forward*(6.2f*scale)+right*(DevourVacuumMouthX*scale)+V{0,-1.25f+.6f*scale,0};}
// 掃除機: 押している間、吸い口の前の小さい物を順に吸い寄せ、届いたら飲み込む
void ChronoSystem::TickDevourVacuum(entt::registry& r,GameContext& ctx,float dt,bool held){
    auto& body=life_.bodies[0];V mouth=DevourVacuumMouth();float gained=0;int eaten=0;
    for(auto& suck:devourSucks_){suck.age+=dt;if(suck.age>=suck.flight){devourEatBy_=suck.toBody?0:1;gained+=DevourEat(r,suck.index);++eaten;}}
    devourSucks_.erase(std::remove_if(devourSucks_.begin(),devourSucks_.end(),[](const DevourSuck& s){return s.age>=s.flight;}),devourSucks_.end());
    if(eaten){devourClock_.combo+=eaten;devourClock_.comboClock=1.4f;float weight=DevourGulpWeight(eaten,gained,body.mass);
        PlayDevourGulp(weight,devourClock_.combo);
        Rumble::Play(.1f+.3f*weight,.2f,.06f+.08f*weight);
        // ぱくっ: 体が上へ伸びて弾み、飲んだ所に文字が出る。大きい物は画面も揺れて輪が広がり、足が少し重くなる
        if(body.aspect<1.3f)body.aspectVelocity=std::max(body.aspectVelocity,4+9*weight);
        devourSizePulse_=1;V radii=LifeRadii(body,life_.recipe);float scale=LifeScale(body.mass),side=(float(devourPops_.size()%3)-1)*radii.x*.5f;
        (void)side;(void)scale;if(devourPops_.size()<(devourSucking_?2u:8u))devourPops_.push_back({devourEatAt_,DevourWord(weight>=.6f?"gulpBig":weight>=.3f?"gulpMiddle":"gulpSmall"),0,weight,0,DevourSeed()});
        // (掃除機は続けて飲むので、体が隠れないよう揺れと輪は出さない)
        if(weight>=.3f&&!devourSucking_){LifeImpact(ctx,ImpactDevour,body.at,{0,1,0},.3f+.9f*weight);devourChew_=.25f*weight;if(weight>=.6f&&swarmEffects_.size()<128)swarmEffects_.push_back({body.at,0,radii.x*(1+weight),2});}
        if(weight>.4f)DevourSplash(body.at,LifeRadii(body,life_.recipe).x*.7f,4+int(8*weight),std::sqrt(LifeScale(body.mass))*.7f);
    }
    devourSucking_=held&&body.action==LifeAction::Vacuum&&body.weapon.ready&&!finished_;devourSuckClock_-=dt;
    // 吸っている間だけ連続音を鳴らす
    if(devourSucking_!=(devourVacuumVoice_!=0)){auto* audio=Engine::Audio::GetInstance();
        if(devourSucking_)devourVacuumVoice_=audio->Play(devourSounds_.vacuumLoop,true,.2f*audio->GetMasterSEVolume());
        else{audio->Stop(devourVacuumVoice_);devourVacuumVoice_=0;}}
    if(!devourSucking_||devourSuckClock_>0)return;
    // 一番近い物から吸い寄せる
    int best=-1;float nearest=1e9f;
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||!DevourVacuumReaches(mouth,body.forward,prop.at,body.mass,prop.radius))continue;
        float d=LifeDistance(prop.at,mouth);if(d<nearest){nearest=d;best=int(i);}}
    if(best<0)return;
    DevourPull(r,size_t(best),DevourVacuumFlight,false);devourSuckClock_=DevourVacuumInterval;
}
// バネ: 押している間は縮んで溜め、離すと向いている方へ跳ぶ。着地できる所まで距離を詰める
void ChronoSystem::TickDevourSpring(Player& p,float dt,bool held,V wish){
    auto& body=life_.bodies[0];auto& w=body.weapon;devourHopCooldown_=std::max(0.f,devourHopCooldown_-dt);
    bool spring=body.action==LifeAction::Spring;
    float goal=!spring?1:w.charging?1-.5f*w.charge:!devourHop_.active?1:devourHop_.age<devourHop_.time*.5f?1.25f:.9f;
    body.springStretch+=(goal-body.springStretch)*(1-std::exp(-14*dt));
    if(!spring||devourHop_.active)return;
    if(held&&w.ready&&p.grounded&&devourHopCooldown_<=0){w.charging=true;w.charge=std::min(1.f,w.charge+dt/DevourSpringChargeTime);return;}
    if(!w.charging)return;
    float power=std::max(DevourSpringMinPower,w.charge);w.charging=false;w.charge=0;
    StartDevourHop(power,wish);
}
// 跳び始める: 向いている方へ、着地できる所まで距離を詰めて弧を決める
void ChronoSystem::StartDevourHop(float power,V wish){
    auto& body=life_.bodies[0];
    V direction=Length(wish)>.05f?Unit(wish):body.forward;direction.y=0;body.forward=direction;
    V radii=LifeRadii(body,life_.recipe);float margin=std::clamp(std::max(radii.x,radii.z)*.7f,.5f,18.f),range=DevourSpringRange(body.mass,power);
    V to=body.at;
    if(!devour_)to=ConstrainLifeMovement(body.at,body.at+direction*range,margin);
    else for(float f=1;f>.05f;f-=.1f){V at=body.at+direction*(range*f);
        // 閉じた輪の壁は越えられない
        bool crossing=false;float a=RingDistance(body.at),b=RingDistance(at);for(int z=1;z<RingStyleCount;++z)crossing|=!ringOpen_[size_t(z)]&&(a<RingWallAt(z))!=(b<RingWallAt(z));
        if(!crossing&&DevourWalkable(at,margin)){to=at;break;}}
    to.y=InkGround(to)+1.25f;
    devourHop_={true,false,body.at,to,0,DevourSpringTime(power),DevourSpringRise(body.mass,power)};++devourLog_.hops;
    PlayDevourSound(devourSounds_.bounce,.4f+.25f*power,1.3f);
}
// 跳んでいる間の位置を進める
ChronoSystem::V ChronoSystem::DevourHopStep(float dt){
    auto& hop=devourHop_;hop.age=std::min(hop.time,hop.age+dt);float u=hop.age/hop.time;
    V at=Lerp(hop.from,hop.to,u);at.y+=hop.height*4*u*(1-u);
    if(u>=1){hop.active=false;hop.landed=true;}
    return at;
}
// 着地: 周りの小さい物を衝撃で飲み込み、体が縮んで弾む
void ChronoSystem::DevourSpringLand(entt::registry& r,GameContext& ctx){
    auto& body=life_.bodies[0];devourHop_.landed=false;devourHopCooldown_=DevourSpringCooldown;body.springStretch=.5f;
    float gained=0,shock=DevourSpringShock(body.mass),mass=body.mass;int count=0;devourEatBy_=3;
    for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];if(!prop.active||prop.sealed||!DevourCanCover(mass,prop.radius)||LifeDistance(prop.at,body.at)>shock+prop.radius*.5f)continue;
        gained+=DevourEat(r,i);++count;}
    float weight=count?DevourGulpWeight(count,gained,mass):.2f;if(count){++devourClock_.combo;devourClock_.comboClock=1.4f;life_.message="ズドン！ "+std::to_string(count)+"個 / 成長 +"+DevourAmount(gained);life_.messageAge=1.4f;}
    DevourSplash(body.at,shock*.8f,std::min(30,12+3*count),std::sqrt(LifeScale(mass)));
    LifeImpact(ctx,ImpactStompMiss,body.at,{0,1,0},.8f+.7f*weight);PlayDevourSound(devourSounds_.splat,.5f+.3f*weight,.9f);if(count)PlayDevourGulp(weight,devourClock_.combo);
}
// 引き寄せている物の位置
ChronoSystem::V ChronoSystem::DevourSuckAt(const DevourSuck& suck)const{
    const auto& body=life_.bodies[0];V radii=LifeRadii(body,life_.recipe),core=body.at+body.fluidOffset+V{0,-1.25f+radii.y*.45f,0};
    const auto& prop=life_.props[suck.index];float u=std::min(1.f,suck.age/suck.flight);
    return Lerp(suck.from,suck.toBody?core:DevourVacuumMouth(),u*u)+V{0,suck.toBody?0:prop.radius*std::sin(3.1415927f*u),0};
}
// 体の中の物の位置 (体の揺れに合わせてゆらゆら動く)
ChronoSystem::V ChronoSystem::DevourMealAt(const DevourMeal& meal)const{
    const auto& body=life_.bodies[0];V radii=LifeRadii(body,life_.recipe),core=body.at+body.fluidOffset+V{0,-1.25f+radii.y*.45f,0};float a=meal.age*.9f+meal.dir.x*5;
    // 新しい物は体の表面まで押し出す (皮が物を包むので、体の形が歪む)。低い体でも床には埋まらない
    float size=meal.radius*meal.fit,wide=radii.x*meal.now,deep=radii.z*meal.now,high=(radii.y*.8f+size*.4f)*meal.now;
    float lift=meal.lift*meal.now*DevourBodyRadius(body.mass);
    V at=core+V{meal.dir.x*(wide+lift),meal.dir.y*(high+lift),meal.dir.z*(deep+lift)}+V{std::sin(a)*.12f,std::sin(a*1.3f)*.1f,std::cos(a)*.12f}*LifeScale(body.mass);
    at.y=std::max(at.y,body.at.y-1.25f+size*.5f);return at;
}
// ギミックの形の中の物の位置: 形の芯 (姿勢と向きに合わせる) から、物ごとの向きへ少しだけ散らす
ChronoSystem::V ChronoSystem::GimmickMealAt(const DevourMeal& meal)const{
    const auto& body=life_.bodies[0];const int shape=DevourToolShape()>0?DevourToolShape():Chrono::GimmickDropShape;const float scale=LifeScale(body.mass);
    const auto core=Chrono::GimmickShapeCore(shape);V local=Chrono::GimmickPosePoint(shape,core.center+meal.dir*(core.thin*Chrono::GimmickMealSpread),DevourToolPose())*scale;
    const V f=body.forward,right{f.z,0,-f.x};return body.at+V{0,-1.25f,0}+right*local.x+V{0,local.y,0}+f*local.z;
}
// ギミックの形の中の物を縮める倍率: 形の厚みに収まる大きさまで (元より大きくはしない)
float ChronoSystem::GimmickMealFit(const DevourMeal& meal,float extent)const{
    const int shape=DevourToolShape()>0?DevourToolShape():Chrono::GimmickDropShape;
    const float limit=Chrono::GimmickShapeCore(shape).thin*Chrono::GimmickMealSize*LifeScale(life_.bodies[0].mass);
    return std::min(meal.fit,limit/std::max(.01f,extent));
}
// 大きい物の周りの液体の皮: 体の中心から物までを液体の管でつなぎ、物が内側から皮を押し広げて見えるようにする
void ChronoSystem::DevourMealSkin(std::vector<Engine::Renderer::FluidTendril>& arms)const{
    // 武器やギミックの形の間は皮を張らない (形が崩れる)。形へ移る間は皮を小さくしていく
    const auto& body=life_.bodies[0];if(body.mass<=0||LifeWeaponForm(body.action))return;const float keep=1-devourMealMorph_;if(keep<.02f)return;
    V radii=LifeRadii(body,life_.recipe),core=body.at+body.fluidOffset+V{0,-1.25f+radii.y*.45f,0};float bodyRadius=DevourBodyRadius(body.mass);
    // 飲み込んでいる物へ、体から液体の腕を伸ばして包む (腕は太く、物より先まで伸びる)
    size_t bites=0;for(const auto& suck:devourSucks_)if(suck.toBody&&bites<kBiteArmLimit){++bites;V at=DevourSuckAt(suck);float size=std::max(life_.props[suck.index].radius,bodyRadius*.22f);
        arms.push_back({EV(Lerp(core,at,.1f)),size*.9f*keep,EV(at),size*1.5f*keep});}
    // 新しい物を先に包み、残りは大きい順に包む (沈んだ物も体より背が高いと上へ突き出る)
    std::vector<const DevourMeal*> big;for(const auto& meal:devourMeals_)big.push_back(&meal);std::reverse(big.begin(),big.end());
    size_t fresh=std::min(big.size(),kMealSkinFresh);
    std::sort(big.begin()+std::ptrdiff_t(fresh),big.end(),[](const DevourMeal* a,const DevourMeal* b){return a->radius*a->fit>b->radius*b->fit;});
    if(big.size()>kMealSkinLimit)big.resize(kMealSkinLimit);
        for(const auto* meal:big){float size=meal->radius*meal->fit;V at=DevourMealAt(*meal);// 物の形に皮を張る
        auto visual=cityVisuals_.find(meal->visual);bool shaped=visual!=cityVisuals_.end()&&visual->second.half.x>0;float yaw=visual!=cityVisuals_.end()?visual->second.rotate.y:0;
        V half=shaped?visual->second.half*meal->fit:V{size,size,size}*.7f;float skin=bodyRadius*.1f+(shaped?visual->second.round*meal->fit:0);
        if(keep<1){at=Lerp(GimmickMealAt(*meal),at,keep);half=half*keep;skin*=keep;}
        arms.push_back({EV(at),yaw,EV(half),-skin});}
}
// 体の中に見えたまま、中心へ沈みながら溶けていく物を描く
void ChronoSystem::DrawDevourMeals(GameContext& ctx){
    const auto& body=life_.bodies[0];if(body.mass<=0)return;
    // 結果の「おなかの中」では、体の中の物の代わりに升の物を描く
    if(finished_&&DevourShelfDive()>0){DrawDevourShelf3D(ctx);return;}
    // 吸い口へ飛んでくる物と、吸い込む風の筋
    V mouth=DevourVacuumMouth();
    for(const auto& suck:devourSucks_){const auto& prop=life_.props[suck.index];float u=std::min(1.f,suck.age/suck.flight),shrink=suck.toBody?1+.25f*std::sin(3.1415927f*u)-.3f*u:1-.75f*u;V at=DevourSuckAt(suck);
        if(!prop.orb&&DrawCityPropInTentacle(ctx,prop.visual,at,shrink))continue;
        float radius=std::max(prop.shard,LifeOrbRadius(prop.reward))*shrink;Engine::Transform t;t.translate=EV(at);t.scale={radius,radius,radius};auto tint=LifeOrbColor(prop.reward);ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{tint.x,tint.y,tint.z,1});}
    if(devourSucking_){float range=DevourVacuumRange(body.mass);V right{body.forward.z,0,-body.forward.x};
        for(int i=0;i<14;++i){float lane=(RingNoise(i+1)*2-1)*.8f,u=std::fmod(life_.age*2.4f+RingNoise(i+40),1.f);V at=mouth+(body.forward+right*lane)*(range*(1-u))+V{0,range*.12f*RingNoise(i+80),0};
            ctx.renderer->DrawLine3D(EV(at),EV(Lerp(at,mouth,.22f)),{.75f,1,.55f,.7f});}}
    // ギミックの形になる間は、体の中の物を小さくして形の芯へ集める (見えたまま、形からははみ出さない)
    devourMealMorph_=std::clamp(devourMealMorph_+(DevourMealsMorphed()?1.f:-1.f)*std::min(ctx.dt,.05f)/Chrono::GimmickFormTime,0.f,1.f);const float morph=LifeSmooth(devourMealMorph_);
    // 武器の形の間は体の形が違うので、体の中の物は見せない
    if(LifeWeaponForm(body.action))return;
    for(const auto& meal:devourMeals_){V at=DevourMealAt(meal);auto visual=cityVisuals_.find(meal.visual);
        const float extent=visual!=cityVisuals_.end()&&visual->second.half.x>0?std::max({visual->second.half.x,visual->second.half.y,visual->second.half.z}):std::max(LifeOrbRadius(1),meal.radius*.6f);
        float fit=meal.fit;if(morph>0){at=Lerp(at,GimmickMealAt(meal),morph);fit=fit+(GimmickMealFit(meal,extent)-fit)*morph;}
        if(!meal.orb&&DrawCityPropInTentacle(ctx,meal.visual,at,fit))continue;
        float radius=std::max(LifeOrbRadius(1),meal.radius*.6f)*fit;Engine::Transform t3;t3.translate=EV(at);t3.scale={radius,radius,radius};
        ctx.renderer->DrawMeshInstanced(sphere_,white_,t3,{meal.tint.x,meal.tint.y,meal.tint.z,1});}
}
// 吸収モードの UI: 右上の制限時間・上の大きさの段階と次の段階までのゲージ・左下の大きさとコンボ・ギミックの操作の吹き出し。終わったら結果
void ChronoSystem::DrawDevourUI(GameContext& ctx){
    // ステージの紹介の間は HUD を出さない (紹介の黒帯と場所の名前だけにする)
    if(!finished_&&DevourTutorialCutscene()&&devourTutorial_.step==5)return;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);
    const auto& body=life_.bodies[0];
    const Engine::Vector4 hazard{.95f,.76f,.19f,1},slime{.61f,.89f,.23f,1},white{.96f,.96f,.94f,1},black{.08f,.08f,.08f,1},red{.9f,.22f,.16f,1};
    auto shadowed=[&](const std::string& text,float x,float y,float size,Engine::Vector4 color,float align=0){
        x-=ui.Width(text,size,UI::JapaneseFont)*align;ui.Text(text,x,y+size*.09f,size,{0,0,0,.85f*color.w},UI::JapaneseFont);ui.Text(text,x,y,size,color,UI::JapaneseFont);};
    const auto& stage=CurrentDevourStage();const int goalCount=int(stage.goals.size());
    // 飲み込んだ割合 (落とした球は数えない)
    int total=0,eaten=0;for(const auto& prop:life_.props)if(!prop.orb&&prop.visual!=0xffffffffu){++total;if(!prop.active)++eaten;}
    float cm=DevourSizeCm(stage,body.mass),bestMass=devourBests_[size_t(devourStage_)];std::string size=DevourSizeText(cm),best=DevourSizeText(DevourSizeCm(stage,bestMass));
    if(finished_){
        // 結果画面: 大きさを巻尺で見せる。届かなかったら次の目標までの差を出す
        int rank=DevourRankAt(stage,body.mass);UI::Scale::ResultView view;view.stage=stage.name;view.cm=cm;view.won=won_;view.newBest=devourNewBest_;
        view.rank=rank?DevourRankName(stage,rank):"";view.goalCm=stage.goals.front();view.timeLeft=(std::max)(0.f,devourClock_.left);view.items=life_.absorbed;view.percent=total?eaten*100/total:0;
        view.hasNext=won_&&devourStage_+1<int(DevourStages().size());view.live=true;view.inside=DevourShelfDive();
        // 体そのものを画面へ投影して端を取る (手前の面は奥より大きく映るので、中心の深さで測ると線がずれる)
        // 体とカメラが止まったら線も動かさない
        if(!resultScreenSet_||resultCameraAge_<kResultSettledAge){using namespace DirectX;const XMMATRIX viewProj=ctx.camera->View()*ctx.camera->Proj();std::vector<float> xs,ys;
            auto project=[&](V at){XMVECTOR p=XMVector3TransformCoord(XMVectorSet(at.x,at.y,at.z,1),viewProj);xs.push_back((XMVectorGetX(p)+1)*640);ys.push_back((1-XMVectorGetY(p))*360);};
            const V bodyAt=life_.bodies[0].at;const auto& fluid=ctx.renderer->GetFluidBodySnapshot();const V side{std::cos(yaw_),0,-std::sin(yaw_)};const float reach=DevourBodyRadius(life_.bodies[0].mass);
            const size_t step=std::max<size_t>(1,fluid.offsets.size()/2048);
            for(size_t i=0;i<fluid.offsets.size();i+=step){const auto& o=fluid.offsets[i];project({fluid.core.x+o.x,fluid.core.y+o.y,fluid.core.z+o.z});}
            size_t trim=xs.size()/200;
            std::sort(xs.begin(),xs.end());std::sort(ys.begin(),ys.end());
            if(xs.size()>2*trim){xs.erase(xs.end()-trim,xs.end());xs.erase(xs.begin(),xs.begin()+trim);ys.erase(ys.begin(),ys.begin()+trim);}
            for(const auto& arm:ctx.renderer->GetFluidTendrils())for(int end=0;end<2;++end){const auto& p=end?arm.end:arm.start;const float radius=end?arm.endRadius:arm.radius;const V at{p.x,p.y,p.z};
                if(Length(at-bodyAt)>reach*2.5f)continue;project(at-side*radius);project(at+side*radius);project(at+V{0,radius,0});}
            if(!xs.empty()){float left=*std::min_element(xs.begin(),xs.end()),right=*std::max_element(xs.begin(),xs.end());const float top=*std::min_element(ys.begin(),ys.end()),skin=(right-left)*.03f;left-=skin;right+=skin; // 粒子の中心より皮の分だけ外が見える
                // 粒子の揺れで線が震えないよう、なめらかに追う
                const float follow=resultScreenSet_?.2f:1.f;resultScreenSet_=true;
                resultLeft_+=(left-resultLeft_)*follow;resultRight_+=(right-resultRight_)*follow;resultTop_+=(top-resultTop_)*follow;}}
        view.bodyLeft=std::max(resultLeft_,490.f);view.bodyRight=std::min(resultRight_,1270.f);view.bodyTop=resultTop_;
        UI::Scale::Result(ui,view,finishAge_);
        DrawDevourShelfUI(ctx,ui);
        // 終わった直後は、今までの表示を薄くしながら残す
        if(finishAge_>=.35f)return;
        ui.MultiplyAlpha(1-finishAge_/.35f);}
    // 物差しの表示へ渡す内容をまとめる
    {if(devourHudCm_<=0||cm<devourHudCm_*.5f)devourHudCm_=cm;devourHudCm_+=(cm-devourHudCm_)*(1-std::exp(-8*std::min(ctx.dt,.05f)));
        UI::Scale::HudView hud;const int rank=devourRank_;hud.stage=stage.name;hud.rank=rank?DevourRankName(stage,rank):"";hud.cm=cm;hud.shownCm=devourHudCm_;hud.time=devourClock_.left;hud.pulse=devourSizePulse_;
        // ランクの知らせ: 残りの割合と、添える次の目標。チュートリアルの札と重なるので、その間は出さない
        hud.flash=devourTutorial_.step<0?std::clamp(devourClock_.flash/UI::Scale::RankFlashSeconds,0.f,1.f):0.f;
        if(rank>0)hud.flashNote=rank>=goalCount?std::string("Sランク達成！"):(rank==1?std::string("クリア！ "):std::string())+"次は "+DevourSizeText(stage.goals[size_t(rank)]);
        hud.age=life_.age;hud.combo=devourClock_.combo;hud.comboKick=devourClock_.comboClock;hud.items=life_.absorbed;hud.percent=total?eaten*100/total:0;hud.bestBroken=bestMass>0&&body.mass>bestMass;
        // 目標の旗: まだ届いていないランクを全部立てる
        for(int i=rank;i<goalCount;++i)hud.flags.push_back({stage.goals[size_t(i)],DevourRankName(stage,i+1)+" "+DevourSizeText(stage.goals[size_t(i)]),true});
        // チュートリアルの間は段差の大きさを目標にして、時計を出さない
        if(DevourTutorialGrowing()){hud.next="目標 "+DevourSizeText(DevourTutorialGoal());hud.hideTime=true;}
        else if(rank<goalCount){const float to=stage.goals[size_t(rank)];hud.next=(rank?"つぎ ランク"+DevourRankName(stage,rank+1):std::string("目標"))+" あと "+DevourSizeText(std::max(0.f,to-cm));}
        else hud.next="Sランク達成！ もっと大きく";
        // 次に飲める壁と門
        for(float gate:stage.gates)if(cm<gate){hud.flags.insert(hud.flags.begin(),{gate,"段差 "+DevourSizeText(gate),false});break;}
        for(int z=1;z<RingStyleCount;++z)if(!ringOpen_[size_t(z)]){const float gate=DevourSizeCm(stage,DevourMassToCover(RingZones()[size_t(z)].gateRadius));hud.gate=RingZones()[size_t(z)].name+"の門 "+DevourSizeText(gate)+" で飲める";break;}
        if(bestMass>0&&body.mass<=bestMass){const float mark=DevourSizeCm(stage,bestMass);hud.flags.insert(hud.flags.begin(),{mark,"BEST "+best,false});}
        // このステージの変形アイテム
        if(!devourFreeForms_){const LifeAction form=DevourItemForm(stage.item);const bool active=body.action==form,taken=LifeCanMorph(body,form);hud.item=true;hud.itemLit=active||devourItemReady_;
            hud.itemIcon=LifeActionInfo(form).icon;hud.itemName=LifeActionInfo(form).name;
            hud.itemState=active?"のこり "+std::to_string(int(std::ceil(devourMorphLeft_)))+" 秒":devourItemReady_?"で変形":taken?"使用済み":"";hud.itemKey=!active&&devourItemReady_;
            // まだ見つけていないアイテムの札は出さない
            if(hud.itemState.empty())hud.item=false;}
        UI::Scale::Hud(ui,hud);}
    // 漫画の描き文字: ドンと大きく出て、1 字ずつ回りながら飛び散る
    {const auto& comic=Comic();const char* atlas="Resources/Textures/UI/Comic/atlas.png";const float texture=comic.size;
        const auto piece=[&](const ComicPart& g,float cx,float cy,float k,float spin,float alpha){
            ui.ImagePart(atlas,{cx-g.w*k*.5f,cy-g.h*k*.5f,g.w*k,g.h*k},{g.w/texture,g.h/texture,g.u/texture,g.v/texture},spin,{1,1,1,alpha});};
        const auto word=[&](const std::string& text,float x,float y,float k,float seed,float scatter,float alpha){
            const auto found=comic.words.find(text);if(found==comic.words.end())return;const auto& glyphs=found->second;const float mid=(float(glyphs.size())-1)*.5f;
            for(size_t i=0;i<glyphs.size();++i){const float c=float(i)-mid,fi=float(i),turn=std::sin(seed*9+fi*2.3f);
                piece(glyphs[i],x+glyphs[i].x*k+(c*150+turn*70)*k*scatter,y+glyphs[i].y*k+(-170*scatter+420*scatter*scatter)*k,k*(1-.2f*scatter),turn*2.2f*scatter,alpha);}};
        for(const auto& pop:devourPops_){float x,y;if(!DevourScreen(ctx,pop.at,x,y))continue;const float life=DevourPopLife(pop.kind),u=pop.age/life;
            const float punch=1+.35f*std::sin(std::min(1.f,u*5)*3.1415927f),scatter=std::max(0.f,(u-.45f)/.55f),alpha=1-scatter*scatter*scatter;
            if(pop.kind==1){const auto& lines=comic.marks.at(pop.weight>.75f?"heavyLines":"lightLines");piece(lines,x,y,(.28f+.16f*pop.weight)*std::min(1.f,u*8)*(1+.25f*scatter),pop.seed*6.283f,alpha*(1-scatter));
                word(pop.text,x,y,(.26f+.16f*pop.weight)*(1+.5f*(punch-1)),pop.seed,scatter,alpha);}
            else if(pop.kind==2){
                // 小さくなった印: 体の周りが暗く沈み、青い下矢印が体の周りから下へ落ちる
                const float reach=DevourBodyRadius(body.mass);const V middle=body.at+V{0,reach*.4f,0};float cx,cy,tx,ty;if(!DevourScreen(ctx,middle,cx,cy)||!DevourScreen(ctx,middle+V{0,reach,0},tx,ty))continue;
                const float r=std::max(60.f,std::abs(cy-ty)*1.5f),fade=std::min(1.f,u*8)*(1-u*u);
                ui.Halo(cx,cy,r*2.3f,{.02f,.05f,.18f,.72f*fade});ui.Halo(cx,cy,r*1.5f,{.01f,.03f,.12f,.5f*fade});
                const auto& arrow=comic.marks.at("arrow");
                for(int i=0;i<6;++i){const float a=pop.seed*6.283f+float(i)*1.0472f,t=std::clamp((u-float(i%3)*.08f)/.72f,0.f,1.f),show=std::min(1.f,t*6)*(1-t*t*t);
                    piece(arrow,cx+std::cos(a)*r*1.05f,cy+std::sin(a)*r*.5f-r*.45f+t*r*1.15f,.26f+.08f*float(i%2),0,show);}}
            else{const float k=(.5f+.4f*pop.weight)*punch;if(pop.weight>=.6f)piece(comic.marks.at("lightLines"),x,y,.42f*std::min(1.f,u*8),pop.seed*6.283f,alpha*(1-scatter));
                word(pop.text,x,y-30*u,k,pop.seed,scatter,alpha);}}}
    // ギミックの狙いの印
    DrawGimmickMarks(ui,ctx);
    // その場で押せる操作 (ギミックの作動範囲の中): 体の上に吹き出しで出す。小さすぎるときは薄くして、何 cm から使えるかを添える
    {UI::Scale::Prompt prompt;float x,y;
        if(!life_.lab&&devourTutorial_.step!=1&&GimmickNearPrompt(prompt)&&DevourScreen(ctx,body.at+V{0,DevourBodyRadius(body.mass)*.9f+1,0},x,y)&&y>90)
            UI::Scale::PromptBubble(ui,prompt,x,std::min(y,540.f),prompt.locked?0.f:std::sin(life_.age*6));}
}
}
