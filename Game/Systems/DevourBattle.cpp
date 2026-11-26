// DevourBattle.cpp
// 変形チャレンジ (吸収のあとの第 2 フェーズ。ChronoSystem の一部)。規則は Chrono/ChallengeRules.h
//  - 成長 (吸収) とチャレンジでステージの制限時間を分け合う。長押しか残り 30 秒でチャレンジへ移り、残り時間で挑む
//  - 準備フェーズで吸収した物は「袋」に入る。物のスロット (ランクで 1〜4 個) には袋から引いた物が入り、使い切ると袋から引き直す。
//    引く確率は、その物を吸収した数に比例する (吸収した数の BattleBagPower 乗)。飴玉は固定のスロット (無限)
//    (チャレンジ (旧来の戦闘でないとき) はスロットを使わない。始まる前に、集めた物から変形武器を 3 本作って使う: WeaponCraft.cpp)
//  - 物ごとに能力がある (collection.json の action。省略は battle の分類の動き)。投げる以外は変形する
//  - 物を使っても体は縮まない。大きさは威力 (BattlePower) になる
//  - 時間が来たら終わり。スコアは自己ベストとして残し、次のステージへはいつでも進める。結果画面の L / B でチャレンジだけやり直せる
//  - 旧来の敵と戦う動き (敵・ボス・HP) は --devour-battle-legacy のときだけ (能力の当たりを確かめる相手として残す)
// 操作: WASD / 左スティックで移動、マウス / 右スティックで向き、左クリック / RT で使う、1〜5 / 十字キー / LB・RB でスロットを選ぶ
// 規則は Chrono/DevourBattleRules.h、物の能力は collection.json の battle / action、能力の形は Chrono/DevourGimmickRules.h
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "CityBuilder.h"
#include <random>

namespace Game {
namespace {
bool HasArg(const wchar_t* flag){return wcsstr(GetCommandLineW(),flag)!=nullptr;}
std::string ArgText(const wchar_t* flag){const wchar_t* at=wcsstr(GetCommandLineW(),flag);if(!at)return {};at+=wcslen(flag);std::string out;while(*at&&*at!=L' '&&*at!=L'"')out+=char(*at++);return out;}
std::mt19937& BattleRandom(){static std::mt19937 random{12345u};return random;}
float BU(float low,float high){return std::uniform_real_distribution<float>(low,high)(BattleRandom());}
// 図鑑の id から能力 (建物なら building=true)
BattleAction ActionOfId(const std::string& id,bool& building){
    const int index=Collection::IndexOf(id);building=false;if(index<0)return BattleAction::Throw;
    const auto& entry=Collection::Entries()[size_t(index)];BattleKindOf(entry.battle,building);
    return BattleActionOf(entry.action,entry.battle);
}
// 図鑑の名前と絵 (無ければ id)
std::string NameOfId(const std::string& id){const int index=Collection::IndexOf(id);return index<0?id:Collection::Entries()[size_t(index)].name;}
std::string ImageOfId(const std::string& id){const int index=Collection::IndexOf(id);return index<0?std::string():Collection::ImagePath(Collection::Entries()[size_t(index)]);}
// 能力の色 (仮の見た目と HUD)
Engine::Vector4 ActionColor(BattleAction a){
    static constexpr Engine::Vector4 colors[]={{.95f,.62f,.2f,1},{.35f,.7f,1,1},{.95f,.32f,.28f,1},{.65f,.65f,.72f,1},{.45f,.9f,.4f,1},{.9f,.8f,.35f,1},{.3f,.75f,1,1},
        {.55f,.4f,.85f,1},{.95f,.5f,.3f,1},{.8f,.85f,.95f,1},{1,1,1,1},{1,.6f,.85f,1},{.75f,.3f,.3f,1},{.3f,.6f,.95f,1},
        // 将棋の駒 (歩・香車・桂馬・銀・金・角・飛車・王): 駒の木の色から、強い駒ほど金色に
        {.93f,.78f,.48f,1},{.93f,.72f,.4f,1},{.93f,.68f,.36f,1},{.85f,.85f,.9f,1},{1,.82f,.3f,1},{.95f,.55f,.3f,1},{.95f,.4f,.3f,1},{1,.9f,.35f,1},
        // 置く・まく物 (ビー玉・積み木の塔・ドミノ・絵の具・ぬいぐるみ)
        {.55f,.85f,1,1},{.95f,.75f,.3f,1},{.95f,.95f,.9f,1},{.9f,.4f,.75f,1},{.85f,.65f,.45f,1},
        // 飛び道具 (十円玉・輪・ゴムボール・紙飛行機・トランプ・鉛筆)
        {.85f,.55f,.3f,1},{.9f,.5f,.4f,1},{.95f,.35f,.35f,1},{.95f,.95f,1,1},{.9f,.3f,.35f,1},{.4f,.75f,.4f,1},
        // 止める・弱らせる (リモコン・まんが・かご・ノート・消しゴム・湯のみ) と自分を強くする (だるま・招き猫・急須・ティッシュ・靴下)
        {.5f,.5f,.6f,1},{.95f,.6f,.3f,1},{.8f,.65f,.4f,1},{.95f,.95f,.85f,1},{.6f,.7f,1,1},{.6f,.8f,.4f,1},{.9f,.25f,.2f,1},{1,.9f,.5f,1},{.5f,.75f,.45f,1},{1,1,1,1},{.6f,.75f,.4f,1}};
    return colors[std::min(int(a),int(BattleAction::Count)-1)];
}
// 突進する能力か (轢く・転がる・飛ぶ・ラジコン)
bool DashAction(BattleAction a){return a==BattleAction::Guard||a==BattleAction::Ride||a==BattleAction::Roll||a==BattleAction::Fly||a==BattleAction::Rc||a==BattleAction::Kyo||a==BattleAction::Kei;}
// 桂馬は跳んでいる間 (最初の 0.42 秒) だけ進む
constexpr float kKeiJump=.42f;
}

// 戦闘を使うか: 従来の進行を確かめる自動テストと計測では、時間切れからそのまま結果画面へ進む
bool ChronoSystem::BattleEnabled(){
    if(HasArg(L"--devour-battle")||QuickChallenge().active)return true;
    return !HasArg(L"--slime-life-smoke")&&!HasArg(L"--load-profile-auto");
}

// 吸収が終わった体と袋を覚え、建物を戻して戦闘の始まりの演出へ
void ChronoSystem::StartDevourBattle(entt::registry& r,int rank){
    auto& b=battle_;auto& body=life_.bodies[0];
    // 準備フェーズの終わりにまだ体に入りきっていない成長は、ここで体に足してから大きさを覚える
    body.mass=std::min(LifeMaxMass,body.mass+std::max(0.f,body.pendingGrowth));body.pendingGrowth=0;
    b.startAt=body.at;b.startForward=body.forward;b.startMass=body.mass;b.unit=BattleUnitAt(body.mass);b.rank=rank;
    // チャレンジ: ステージを始めるときに決めたもの。時間は成長の残り (やり直しも同じ時間)
    b.challenge=challenge_;b.legacy=HasArg(L"--devour-battle-legacy");b.challengeTime=b.legacy?BattleTime:ChallengeTimeFor(devourClock_.left);
    // 敵が落とす物の候補: ステージの物 (建物は除く) の見た目と能力
    b.dropPool.clear();
    for(const auto& prop:life_.props){if(prop.visual==0xffffffffu||prop.orb||!cityVisuals_.count(prop.visual))continue;auto e=entt::entity(prop.visual);if(!r.valid(e))continue;
        const auto* name=r.try_get<NameComponent>(e);if(!name)continue;const std::string id=Collection::KeyOfName(name->name);if(id.empty())continue;
        bool building=false;const BattleAction action=ActionOfId(id,building);if(building)continue;b.dropPool.push_back({prop.visual,id,action,prop.radius});}
    // 戦う場所: マップの外 (+x の側) に置いた専用の広い平らなステージへ飛ばす
    BuildBattleArena(r);b.startAt=b.arenaAt+V{0,1.25f,0};b.startForward={0,0,1};
    // 吸収した物を種類ごとに袋へ入れる (建物は使えない。戦闘で街に戻す)
    b.startBag.clear();
    for(const auto& eaten:devourEaten_){bool building=false;const BattleAction action=ActionOfId(eaten.id,building);if(building)continue;
        auto bag=std::find_if(b.startBag.begin(),b.startBag.end(),[&](const BattleBag& x){return x.id==eaten.id;});
        if(bag==b.startBag.end()){b.startBag.push_back({eaten.id,action,0,{}});bag=b.startBag.end()-1;}
        bag->items.push_back({eaten.visual,eaten.id,eaten.gain>0?eaten.gain:DevourReward(eaten.radius),eaten.radius});}
    // 小さい物 (collection.json の bundle) は bundle 個ずつ束にして 1 回分にする。縮む量は束の合計 (1 回の上限はある)。引く重みは束の数
    for(auto& bag:b.startBag){const int index=Collection::IndexOf(bag.id);const int bundle=index<0?1:Collection::Entries()[size_t(index)].bundle;
        if(bundle>1){std::vector<BattleItem> bundled;for(size_t i=0;i<bag.items.size();i+=size_t(bundle)){BattleItem item=bag.items[i];
                for(size_t k=i+1;k<std::min(bag.items.size(),i+size_t(bundle));++k){item.gain+=bag.items[k].gain;item.radius=std::max(item.radius,bag.items[k].radius);}bundled.push_back(item);}
            bag.items=std::move(bundled);}
        bag.absorbed=int(bag.items.size());}
    // 物のスロットの数はランクで決まる (デモプレイでは最大)
    b.slotCount=HasArg(L"--devour-battle")||QuickChallenge().active?BattleSlotMax:BattleSlotsForRank(rank);
    // 変形武器は新しいチャレンジごとに作り直す (やり直しでは同じ武器のまま)
    b.crafted=false;
    // 部屋: 残すのは家具 (机・こたつ・ベッドなど、吸収できない物) だけ。吸収しきれなかった小物は片付ける
    // (家具は吸収の対象の一覧 life_.props に入っていないので、一覧に残っている物を全部消せばよい。積み木の壁も道が開く)
    if(CurrentDevourStage().map=="room"){for(size_t i=0;i<life_.props.size();++i)if(life_.props[i].active)DevourRemoveProp(r,i);
        // 部屋のギミック (だるま落としなど) の看板・床の輪・部品も隠し、看板の土台の坂と作動範囲の光の膜も消す (戦闘では使わない)
        auto hide=[&](uint32_t visual){if(visual==0xffffffffu)return;auto e=entt::entity(visual);if(r.valid(e))if(auto* mr=r.try_get<MeshRendererComponent>(e))mr->enabled=false;};
        for(const auto& g:gimmicks_){hide(g.sign);hide(g.ring);hide(g.extra);for(uint32_t part:g.parts)hide(part);for(uint32_t reward:g.rewards)hide(reward);}
        gimmickSigns_.clear();}
    // 吸収で飲んだ建物を街に戻す (見た目と通れなさ。もう飲めない)
    b.buildings.clear();
    for(size_t i=0;i<life_.props.size();++i){auto& prop=life_.props[i];if(prop.visual==0xffffffffu)continue;auto e=entt::entity(prop.visual);if(!r.valid(e))continue;
        const auto* name=r.try_get<NameComponent>(e);if(!name)continue;bool building=false;ActionOfId(Collection::KeyOfName(name->name),building);if(!building)continue;
        if(!prop.active)if(auto* mr=r.try_get<MeshRendererComponent>(e))mr->enabled=true;
        prop.active=false;b.buildings.push_back(i);}
    BeginBattleFight(r);
}

// 専用の広いステージを作る: マップの外に平らな床と、円の縁に並べた柱 (仮の見た目)。やり直しでは作り直さない
void ChronoSystem::BuildBattleArena(entt::registry& r){
    auto& b=battle_;const auto& stage=CurrentDevourStage();b.arenaRadius=battle_.unit*BattleArenaUnits;
    // マップの範囲 (吸収の対象の物の位置) の +x の端から、半径の数倍離す
    float maxX=-1e9f,minZ=1e9f,maxZ=-1e9f;for(const auto& prop:life_.props){maxX=std::max(maxX,prop.at.x);minZ=std::min(minZ,prop.at.z);maxZ=std::max(maxZ,prop.at.z);}
    if(maxX<-1e8f){maxX=life_.bodies[0].at.x;minZ=maxZ=life_.bodies[0].at.z;}
    b.arenaAt={maxX+b.arenaRadius*BattleArenaGap,0,(minZ+maxZ)*.5f};
    if(r.valid(battleArenaEntity_))return;
    battleArenaDecor_.clear();
    const float R=b.arenaRadius,floorHalf=R*1.6f,pillar=battle_.unit*.9f,height=battle_.unit*3;
    auto make=[&](const char* name,const std::vector<std::array<V,2>>& list,V color){CityParts::CityBuilder builder;for(const auto& box:list)builder.Box(CityParts::CityMetal,box[0],box[1]);auto& g=builder.geometry[CityParts::CityMetal];
        auto e=r.create();r.emplace<NameComponent>(e,name);r.emplace<TransformComponent>(e).scale={1,1,1};
        auto& mr=r.emplace<MeshRendererComponent>(e);mr.modelHandle=Engine::Renderer::GetInstance()->CreateDynamicMesh(g.vertices,g.indices);mr.textureHandle=white_;mr.extraTextureHandles=lowMaps_;
        mr.shaderName="LowpolySurface";mr.useCubemap=false;mr.color={color.x,color.y,color.z,1};return e;};
    const V c=b.arenaAt;
    battleArenaEntity_=make("Chrono Battle floor",{{V{c.x-floorHalf,-4,c.z-floorHalf},V{c.x+floorHalf,0,c.z+floorHalf}}},stage.battleColor);
    // 戦う円の目印: 床より少し明るい円の帯 (正方形の板を並べる) と、縁の柱
    {std::vector<std::array<V,2>> ring;const int count=64;const float w=R*2*3.1416f/count*.55f;
        for(int i=0;i<count;++i){const float a=6.2831853f*float(i)/count;const V at=c+V{std::cos(a),0,std::sin(a)}*R;ring.push_back({V{at.x-w,0,at.z-w},V{at.x+w,.3f,at.z+w}});}
        battleArenaDecor_.push_back(make("Chrono Battle ring",ring,{.98f,.85f,.35f}));}
    {std::vector<std::array<V,2>> pillars;const int count=16;
        for(int i=0;i<count;++i){const float a=6.2831853f*(float(i)+.5f)/count;const V at=c+V{std::cos(a),0,std::sin(a)}*(R+pillar*2);pillars.push_back({V{at.x-pillar,0,at.z-pillar},V{at.x+pillar,height,at.z+pillar}});}
        battleArenaDecor_.push_back(make("Chrono Battle pillars",pillars,{.95f,.95f,.92f}));}
}

// 戦闘の中身を始めから (やり直しもここ)
void ChronoSystem::BeginBattleFight(entt::registry& r){
    auto& b=battle_;auto& body=life_.bodies[0];
    body.at=b.startAt;body.forward=b.startForward;body.mass=b.startMass;body.velocity={};body.air=0;
    // スライム陣取りは専用ステージの手前の真ん中から始める
    if(!b.legacy&&b.challenge==ChallengeKind::Territory)body.at=ArenaPoint(0,ChallengeTerritoryStartZ)+V{0,ChallengeBodyGround,0};
    // 準備フェーズの取り込み中に時間切れになると、触手 (grabs) が元のマップの物へ伸びたまま残る。行動と体のひずみも全部戻す
    body.action=LifeAction::Idle;body.grabs.clear();body.target=0xffffffffu;body.committed=false;body.actionAge=0;body.pendingGrowth=0;
    body.liquidForward=body.previousForward=body.forward;body.engulfPoint=body.grabOrigin=body.at;
    body.flowShape={};body.fluidOffset={};body.coreLead={};body.coreVelocity={};body.aspect=1;body.aspectVelocity=0;body.motion=0;
    // 飛びかかりの途中だった場合も止める (元のマップの目標へ体を引っぱり続けないように)
    devourPounce_={};devourPounceWait_=0;devourBiteAge_=-1;
    // 吸い込み中だった物 (devourSucks_) は、元のマップの位置から体へ液体の腕を伸ばして描くので、残ると準備のステージから体が伸びて見える。
    // 戦闘では吸い込みを進めないので全部やめる。水鉄砲の水も消す
    devourSucks_.clear();devourSucking_=false;devourSuckClock_=0;gimmickJets_.clear();
    // 遠くへ飛ぶので、体の粒子を新しい位置に置き直し、カメラもすぐ追いつかせる
    if(auto* t=r.try_get<TransformComponent>(player_))t->translate=Write(body.at);cameraReady_=false;lifeReseed_=true;
    b.bag=b.startBag;for(auto& slot:b.slots)slot={};
    // 確認用: --devour-battle-item=id の物は、袋の中で 40 個に増やす (ステージに少ししか無い物の能力も続けて試せるように)
    {const std::string forced=ArgText(L"--devour-battle-item=");if(!forced.empty())for(auto& bag:b.bag)if(bag.id==forced&&!bag.items.empty())while(bag.items.size()<40)bag.items.push_back(bag.items.front());}
    b.maxHp=b.hp=BattleMaxHp(CurrentDevourStage(),body.mass);
    b.time=b.challengeTime;if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-battle-time="))b.time=std::max(10.f,float(_wtof(arg+21)));
    b.phase=BattleStep::Intro;b.intro=0;b.clock=0;b.overAge=0;b.spawnClock=0;b.cooldown=0;b.guard=0;b.hurtFlash=0;
    b.score=b.combo=0;b.comboClock=0;b.timeBonus=0;b.newBest=false;b.matrix={};b.matrix.active=HasArg(L"--challenge-matrix");b.impactMemo.clear();b.tally={};
    b.impactCheck=HasArg(L"--challenge-impact-check");b.checkNext=0;b.checkClock=.5f;if(b.impactCheck)b.time=float(int(BattleAction::Count))*BattleImpactCheckStep+7;{auto best=challengeBests_.find(ChallengeBestKey(CurrentDevourStage().id,b.challenge));b.best=best==challengeBests_.end()?0:best->second;}
    b.kills=0;b.hudTotal=0;b.regen=b.fluff=b.dropBoost=0;b.toolShape=0;b.toolAge=b.toolLeft=0;b.abilityLeft=0;b.abilityHitAt=-1;b.velocity={};b.bossSpawned=b.bossDown=b.won=false;b.previousFire=true;
    b.enemies.clear();b.shots.clear();b.fx.clear();b.drops.clear();b.placed.clear();b.pops.clear();b.pickups=b.spent=b.dropped=0;b.message.clear();b.messageAge=0;
    // 物のスロットを袋から埋める (最初はルーレットなし)。最初は物の入った一番前のスロットを選ぶ (無ければ飴玉)
    ShowChallengeStage(r);BuildChallenge();
    for(int i=0;i<b.slotCount;++i)FillBattleSlot(i,false);
    b.selected=b.slotCount;for(int i=0;i<b.slotCount;++i)if(!b.slots[size_t(i)].items.empty()){b.selected=i;break;}
    // 変形武器: 回数と待ちを戻す。まだ作っていなければ (新しいチャレンジ) 作る画面を開く
    b.weapon.cool=0;b.weaponShape=0;b.weaponForm=0;b.powerScale=1;
    if(!b.legacy&&!b.crafted)OpenWeaponCraft();
    if(WeaponsActive())b.selected=0;
    ++b.tries;lifeHitStop_=0;
    V facing=body.forward;facing.y=0;if(Length(facing)>.01f)yaw_=std::atan2(facing.x,facing.z);pitch_=.22f;
    Music::Play(Music::Battle);
}

// 結果画面から戦闘だけやり直す
void ChronoSystem::RetryDevourBattle(entt::registry& r){
    for(auto e:resultHidden_)if(r.valid(e)&&r.all_of<MeshRendererComponent>(e))r.get<MeshRendererComponent>(e).enabled=true;
    resultHidden_.clear();
    finished_=won_=false;finishAge_=0;resultCameraAge_=0;resultMeasured_=false;resultScreenSet_=false;
    BeginBattleFight(r);
}

// 袋から次の物の種類を引く: 吸収した数の BattleBagPower 乗に比例。残りの無い種類は引かない。引けなければ -1
// (確認用: --devour-battle-item=id なら、その物が残っている限りそれを引く)
int ChronoSystem::DrawFromBattleBag(){
    auto& b=battle_;static const std::string forced=ArgText(L"--devour-battle-item=");
    if(!forced.empty())for(size_t i=0;i<b.bag.size();++i)if(b.bag[i].id==forced&&!b.bag[i].items.empty())return int(i);
    float total=0;for(const auto& bag:b.bag)if(!bag.items.empty())total+=std::pow(float(std::max(1,bag.absorbed)),BattleBagPower);
    if(total<=0)return -1;
    float pick=BU(0,total);
    for(size_t i=0;i<b.bag.size();++i){if(b.bag[i].items.empty())continue;pick-=std::pow(float(std::max(1,b.bag[i].absorbed)),BattleBagPower);if(pick<=0)return int(i);}
    for(size_t i=b.bag.size();i-->0;)if(!b.bag[i].items.empty())return int(i);
    return -1;
}

// スロットを埋める: roulette なら引く演出 (BattleRouletteTime 秒) のあとで引く。入るのは上限 BattleSlotCapacity 個まで
void ChronoSystem::FillBattleSlot(int index,bool roulette){
    auto& b=battle_;auto& slot=b.slots[size_t(index)];slot.items.clear();
    if(roulette){slot.roulette=0;return;}
    slot.roulette=-1;const int pick=DrawFromBattleBag();if(pick<0){slot.id.clear();return;}
    auto& bag=b.bag[size_t(pick)];slot.id=bag.id;slot.action=bag.action;
    for(int k=0;k<BattleSlotCapacity&&!bag.items.empty();++k){slot.items.push_back(bag.items.back());bag.items.pop_back();}
}

// チャレンジの得点: コンボの倍率をかけて足し、その場所に文字を浮かべる。時間が終わったあとは数えない
int ChronoSystem::AddChallengeScore(int points,V at,const std::string& label){
    auto& b=battle_;if(points<=0||b.phase!=BattleStep::Fight)return 0;
    b.combo=b.comboClock>0?b.combo+1:1;b.comboClock=ChallengeComboWindow;
    const int added=int(std::round(float(points)*ChallengeComboScale(b.combo)));b.score+=added;
    b.pops.push_back({at,(label.empty()?std::string():label+" ")+"+"+std::to_string(added),0,{1,.85f,.3f,1}});
    return added;
}

// 敵にダメージ (倒したら消える演出。ボスを倒したら勝ち)
void ChronoSystem::HurtBattleEnemy(GameContext& ctx,BattleEnemy& enemy,float damage,V push){
    auto& b=battle_;if(enemy.hp<=0)return;
    // 消しゴムで弱った敵は、受けるダメージが増える
    if(enemy.weak>0)damage*=BattleWeakTake;
    enemy.hp-=damage;enemy.flash=.15f;enemy.knock=enemy.knock+push*(enemy.boss?.25f:1.f);
    if(enemy.hp<=0){++b.kills;AddChallengeScore(enemy.boss?500:10,enemy.at+V{0,enemy.radius,0},"");
        // 倒したら物を落とす (ボスは倒した時点で勝ちなので落とさない)
        if(!enemy.boss&&float(b.dropped)<float(b.spent)*BattleDropRefill&&BU(0,1)<BattleDropChance*(b.dropBoost>0?2.f:1.f)){SpawnBattleDrops(enemy.at,1);++b.dropped;}b.fx.push_back({enemy.at,enemy.radius*1.1f,0,.2f,{1,.9f,.6f,.8f}});
        LifeImpact(ctx,enemy.boss?ImpactStructureBreak:ImpactKill,enemy.at,{0,1,0},enemy.boss?1.f:.5f);PlayDevourSound(enemy.boss?devourSounds_.shatter:devourSounds_.hit,enemy.boss?1.f:.5f);
        if(enemy.boss){b.bossDown=true;b.won=true;b.phase=BattleStep::Over;b.overAge=0;b.message="ボスを倒した！ 勝利！";b.messageAge=3;}}
}

// ---- 衝撃 (能力の当たり) ----
// 衝撃を受ける相手の一覧: 旧来の戦闘の敵 (生きているもの)。チャレンジの物もここに並べる
std::vector<ChronoSystem::BattleTarget> ChronoSystem::CollectBattleTargets()const{
    std::vector<BattleTarget> out;out.reserve(battle_.enemies.size());
    for(const auto& e:battle_.enemies)if(e.hp>0)out.push_back({BattleTargetKind::Enemy,e.id,e.at,e.radius,e.boss,e.stun>0});
    // 積み木 (硬い積み木は重い相手)
    for(const auto& blk:battle_.blocks)if(blk.alive)out.push_back({BattleTargetKind::Block,blk.id,blk.at,blk.size*.6f,blk.type==BlockType::Hard,false});
    // スライム陣取り: お掃除ロボと首振り扇風機 (扇風機は重い相手)
    if(!battle_.legacy&&battle_.challenge==ChallengeKind::Territory&&battle_.territory.reclaim){const auto& t=battle_.territory;const float u=battle_.unit;
        out.push_back({BattleTargetKind::Robot,ChallengeRobotId,t.robot.at+V{0,u*.3f,0},u*RobotRadius,false,t.robot.stop>0});
        out.push_back({BattleTargetKind::Fan,ChallengeFanId,t.fan.at+V{0,u*1.6f,0},u*1.2f,true,t.fan.stop>0});}
    return out;
}

bool ChronoSystem::NearestBattleTarget(V from,float maxDistance,BattleTarget& out,const std::vector<uint32_t>* exclude)const{
    float best=maxDistance;bool found=false;
    for(const auto& t:CollectBattleTargets()){if(exclude&&std::find(exclude->begin(),exclude->end(),t.id)!=exclude->end())continue;
        const float d=Length(t.at-from);if(d<best){best=d;out=t;found=true;}}
    return found;
}

// 衝撃を出す: 形に入った相手それぞれに効き目を与える。数え方 (確認用) も残す
int ChronoSystem::EmitBattleImpact(GameContext& ctx,const BattleImpact& impact,std::vector<BattleTarget>* touched,const std::vector<uint32_t>* exclude){
    auto& b=battle_;const size_t a=std::min(size_t(impact.action),size_t(BattleAction::Count)-1);++b.tally.emitted[a];++b.tally.effects[a][size_t(impact.effect)];
    // スライム陣取り: 衝撃の形に床をぬる (相手がいなくても)
    if(!b.legacy&&b.challenge==ChallengeKind::Territory)PaintTerritoryImpact(impact);
    int count=0;
    for(const auto& t:CollectBattleTargets()){
        if(impact.limit>0&&count>=impact.limit)break;
        if(impact.only!=0&&t.id!=impact.only)continue;
        if(exclude&&std::find(exclude->begin(),exclude->end(),t.id)!=exclude->end())continue;
        if((impact.spareBig&&t.big)||(impact.onlyFree&&t.stopped))continue;
        if(!BattleImpactTouches(impact,t.at,t.radius))continue;
        // 同じ衝撃は、同じ相手に rehit 秒たつまで当てない
        BattleImpactMemo* memo=nullptr;
        if(impact.source!=0&&impact.rehit>0){for(auto& m:b.impactMemo)if(m.source==impact.source&&m.target==t.id)memo=&m;if(memo&&memo->left>0)continue;}
        bool hit=false;
        switch(t.kind){
        case BattleTargetKind::Enemy:{auto e=std::find_if(b.enemies.begin(),b.enemies.end(),[&](const BattleEnemy& x){return x.id==t.id;});if(e!=b.enemies.end())hit=ApplyImpactToEnemy(ctx,*e,impact);break;}
        case BattleTargetKind::Block:{const size_t k=size_t(t.id-ChallengeBlockIdBase);if(k<b.blocks.size())hit=ApplyImpactToBlock(ctx,b.blocks[k],impact);break;}
        case BattleTargetKind::Robot:hit=ApplyImpactToRobot(impact);break;
        case BattleTargetKind::Fan:hit=ApplyImpactToFan(impact);break;
        }
        if(!hit)continue;
        if(impact.source!=0&&impact.rehit>0){if(memo)memo->left=impact.rehit;else b.impactMemo.push_back({impact.source,t.id,impact.rehit});}
        ++count;if(touched)touched->push_back(t);
        if(impact.label)b.pops.push_back({t.at+V{0,t.radius,0},impact.label,0,ActionColor(impact.action)});
    }
    if(count>0)++b.tally.touched[a];
    return count;
}

// 旧来の戦闘の敵が衝撃を受ける (効き目ごとの動き)。効いたら true
bool ChronoSystem::ApplyImpactToEnemy(GameContext& ctx,BattleEnemy& e,const BattleImpact& i){
    if(e.hp<=0)return false;
    const float unit=battle_.unit,big=e.boss?i.bigScale:1.f;
    V d=e.at-i.at;d.y=0;const float dist=Length(d);V flatDir{i.dir.x,0,i.dir.z};flatDir=Length(flatDir)>.001f?Unit(flatDir):V{0,0,1};
    const V radial=dist>.01f?d*(1/dist):flatDir;const V push=(i.radial?radial:flatDir)*(unit*i.push);
    switch(i.effect){
    case ImpactEffect::Hit:HurtBattleEnemy(ctx,e,i.damage,push);return true;
    case ImpactEffect::Pull:{const float strength=i.pullCap>0?unit*std::min(dist,i.pullCap)*i.push:unit*i.push;e.knock=e.knock-radial*(strength*big);if(i.damage>0)HurtBattleEnemy(ctx,e,i.damage,V{});return true;}
    case ImpactEffect::PushOut:{const float limit=i.reach+e.radius*.5f;if(dist<limit&&dist>.01f)e.at=e.at+d*((limit-dist)/dist*i.push);return true;}
    case ImpactEffect::Wall:{V at=e.at;at.y=i.at.y;const V ab=i.to-i.at;const float t=std::clamp(Dot(at-i.at,ab)/std::max(1e-4f,Dot(ab,ab)),0.f,1.f);V out=at-(i.at+ab*t);out.y=0;
        const float len=Length(out),gap=i.width+e.radius;if(len<gap){e.at=e.at+(len>.01f?out*(1/len):flatDir)*(gap-len);}return true;}
    case ImpactEffect::Stun:e.stun=std::max(e.stun,i.seconds*big);if(i.damage>0)HurtBattleEnemy(ctx,e,i.damage,push);return true;
    case ImpactEffect::Weaken:e.weak=i.seconds;HurtBattleEnemy(ctx,e,i.damage,push);return true;
    case ImpactEffect::Burn:e.burn=i.seconds;e.burnDps=i.damage;return true;
    case ImpactEffect::Capture:e.stun=std::max(e.stun,i.seconds);return true;
    default:return false;}
}

// 全部の能力の衝撃の数を書く (--challenge-impact-check)。一度も衝撃を出さなかった能力は missing に並べる
void ChronoSystem::WriteImpactCheck()const{
    const auto& b=battle_;std::filesystem::create_directories("tests/out");std::ofstream out("tests/out/challenge-impacts.txt");
    static constexpr const char* keys[]={"throw","ride","smash","guard","heal","heli","water","roll","spin","sword","fly","spring","vacuum","rc","fu","kyo","kei","gin","kin","kaku","hi","ou","marbles","tower","domino","paint","decoy","coin","disc","bounce","homing","fan","pierce","pause","read","cage","sticky","erase","tea","roly","beckon","brew","fluff","smell"};
    static_assert(std::size(keys)==size_t(BattleAction::Count));
    std::string missing;int ok=0;
    out<<"legacy="<<b.legacy<<" step="<<BattleImpactCheckStep<<'\n';
    for(size_t a=0;a<size_t(BattleAction::Count);++a){out<<keys[a]<<" emitted="<<b.tally.emitted[a]<<" touched="<<b.tally.touched[a]<<" effects=";
        bool first=true;for(size_t k=0;k<size_t(ImpactEffect::Count);++k)if(b.tally.effects[a][k]){out<<(first?"":",")<<ImpactEffectName(ImpactEffect(k))<<':'<<b.tally.effects[a][k];first=false;}
        out<<'\n';if(b.tally.emitted[a]>0)++ok;else missing+=std::string(missing.empty()?"":",")+keys[a];}
    out<<"summary emitted="<<ok<<"/"<<int(BattleAction::Count)<<" missing="<<(missing.empty()?"none":missing)<<'\n';
}

// 敵が倒れた所から、ステージの物をランダムに飛び散らせる
void ChronoSystem::SpawnBattleDrops(V at,int count){
    auto& b=battle_;const float unit=battle_.unit;if(count<=0||b.dropPool.empty())return;
    for(int i=0;i<count;++i){const auto& pick=b.dropPool[size_t(BU(0,float(b.dropPool.size())-.001f))];
        BattleDrop d;d.visual=pick.visual;d.id=pick.id;d.action=pick.action;d.itemRadius=pick.radius;
        d.radius=std::clamp(pick.radius,unit*.25f,unit*.6f);d.shrink=d.radius/std::max(.01f,pick.radius);
        const float a=BU(0,6.2831853f);d.at=at+V{0,unit*.5f,0};d.velocity=V{std::cos(a),0,std::sin(a)}*(unit*BU(3,7))+V{0,unit*BU(6,10),0};d.spin=BU(-4,4);
        b.drops.push_back(d);}
}

// 落ちた物を吸収した: 袋に足す (体の大きさは変わらない)
void ChronoSystem::AbsorbBattleDrop(const BattleDrop& d){
    auto& b=battle_;auto& body=life_.bodies[0];
    auto bag=std::find_if(b.bag.begin(),b.bag.end(),[&](const BattleBag& x){return x.id==d.id;});
    if(bag==b.bag.end()){b.bag.push_back({d.id,d.action,1,{}});bag=b.bag.end()-1;}
    bag->items.push_back({d.visual,d.id,0,d.itemRadius});
    ++b.pickups;PlayDevourGulp(.4f,b.pickups);
    b.pops.push_back({body.at+V{0,DevourBodyRadius(body.mass)*1.2f,0},"+1 "+NameOfId(d.id),0,ActionColor(d.action)});
}

// 飴玉を投げる (固定のスロット。何も持っていなくても使える弱い弾)
void ChronoSystem::UseBattleCandy(){
    auto& b=battle_;auto& body=life_.bodies[0];const float R=DevourBodyRadius(body.mass),speed=battle_.unit*14;
    const V dir=Forward(yaw_,0);
    BattleShot shot;shot.at=body.at+dir*(R*.8f)+V{0,R*.4f,0};shot.velocity=dir*speed+V{0,speed*.12f,0};shot.style=1;
    shot.radius=battle_.unit*.18f;shot.damage=BattleCandyDamage*BattlePower(CurrentDevourStage(),body.mass);shot.by=BattleAction::Throw;b.shots.push_back(shot);
    BeginTerritoryUse(BattleAction::Throw,true);
    b.cooldown=BattleCandyWait(0);PlayDevourSound(devourSounds_.pounce,.35f,1.5f);
}

// 物のスロットを使う: 1 つ取り出して能力を始める (体は縮まない)。空になったら袋から引き直す
void ChronoSystem::UseBattleSlot(GameContext& ctx,int index){
    auto& b=battle_;auto& slot=b.slots[size_t(index)];
    if(slot.items.empty()){b.message=slot.roulette>=0?"次の物を選んでいる…":"このスロットは空っぽ";b.messageAge=1.f;return;}
    const BattleItem item=slot.items.back();slot.items.pop_back();++b.spent;
    StartBattleAbility(ctx,slot.action,item);
    b.cooldown=BattleActionWait(slot.action);
    // (スライム陣取りは続けて使えるよう短く)
    if(!b.legacy&&b.challenge==ChallengeKind::Territory)b.cooldown=ChallengeTerritoryWait(b.cooldown);
    if(slot.items.empty())FillBattleSlot(index,true);
}

// 能力を始める: 体の形を変え (投げる以外)、能力の時間と強さを決める。当たりは TickBattleAbility で進める
void ChronoSystem::StartBattleAbility(GameContext& ctx,BattleAction action,const BattleItem& item){
    auto& b=battle_;auto& body=life_.bodies[0];const auto& stage=CurrentDevourStage();
    const float R=DevourBodyRadius(body.mass);const V aim=Forward(yaw_,0);body.forward=aim;
    // 体を形に変える (同じ形ならそのまま続ける)
    auto setTool=[&](int shape,float seconds){if(b.toolShape!=shape){b.toolShape=shape;b.toolAge=0;b.toolPose=b.toolPrevious={};}b.toolLeft=seconds;};
    b.ability=action;b.abilityAge=0;b.abilityLeft=BattleActionTime(action);b.abilityPower=action==BattleAction::Heal?BattlePower(stage,body.mass):ChallengePowerNow();b.abilityItem=BattleItemScale(b.unit,item.radius);
    // (変形武器の連携・重ねがけ・威力の強化)
    b.abilityPower*=b.powerScale;
    b.abilityTick=0;b.abilityHit=false;b.abilityHitAt=-1;b.dashDir=aim;b.abilitySource=b.impactSerial++;
    BeginTerritoryUse(action,false);
    // 衝撃を作る (能力・効き目・形。中心は体、向きは狙っている向き)
    auto make=[&](ImpactEffect effect,ImpactShape shape){BattleImpact i;i.action=action;i.effect=effect;i.shape=shape;i.at=body.at;i.dir=aim;return i;};
    // 自分に効く能力・場所に置く能力も、体のまわり (置いた所) に衝撃を出して知らせる (チャレンジの物が反応できるように)
    auto self=[&](float reach){BattleImpact i=make(ImpactEffect::Self,ImpactShape::Sphere);i.reach=reach;EmitBattleImpact(ctx,i);};
    auto zone=[&](V at,float reach){BattleImpact i=make(ImpactEffect::Zone,ImpactShape::Sphere);i.at=at;i.reach=reach;EmitBattleImpact(ctx,i);};
    switch(action){
    case BattleAction::Throw:{const float speed=battle_.unit*11;
        BattleShot shot;shot.at=body.at+aim*(R*.8f)+V{0,R*.4f,0};shot.velocity=aim*speed+V{0,speed*.12f,0};shot.visual=item.visual;shot.push=3;
        shot.radius=std::max(item.radius,battle_.unit*.2f);shot.damage=BattleThrowDamage*b.abilityPower*b.abilityItem;shot.by=action;
        // (チャレンジ: 大きい物ほど当たる数が多く、床に落ちても転がって当たり続ける)
        if(!b.legacy)shot.hits=ChallengeThrowHits(b.abilityItem);b.shots.push_back(shot);
        b.abilityLeft=0;PlayDevourSound(devourSounds_.pounce,.6f,1.1f);break;}
    case BattleAction::Ride:case BattleAction::Rc:setTool(BattleCarShape,b.abilityLeft+.25f);PlayDevourSound(devourSounds_.pounce,.8f,.8f);LifeImpact(ctx,ImpactTireLaunch,body.at,aim,1);break;
    case BattleAction::Roll:setTool(GimmickBallShape,b.abilityLeft+.2f);PlayDevourSound(devourSounds_.pounce,.8f,.6f);LifeImpact(ctx,ImpactTireLaunch,body.at,aim,1);break;
    case BattleAction::Fly:setTool(GimmickPlaneShape,b.abilityLeft+.2f);PlayDevourSound(devourSounds_.pounce,.7f,1.3f);break;
    case BattleAction::Smash:setTool(BattleHammerShape,b.abilityLeft);b.abilityHitAt=BattleSmashWindup+BattleSmashSwing;PlayDevourSound(devourSounds_.morph,.5f,1.2f);break;
    case BattleAction::Sword:setTool(BattleSwordShape,b.abilityLeft);b.abilityHitAt=.16f;PlayDevourSound(devourSounds_.morph,.5f,1.4f);break;
    case BattleAction::Spring:setTool(BattleSpringShape,b.abilityLeft);b.abilityHitAt=.65f;PlayDevourSound(devourSounds_.morph,.5f,.9f);break;
    case BattleAction::Guard:setTool(BattleShieldShape,BattleGuardTime);b.guard=BattleGuardTime;self(R*1.2f);PlayDevourSound(devourSounds_.morph,.6f);
        // (チャレンジ: 盾を構えて前へ短く突進する盾タックル。旧来の戦闘は、その場で守るだけ)
        if(!b.legacy){b.abilityLeft=ChallengeTackleTime;LifeImpact(ctx,ImpactTireLaunch,body.at,aim,.7f);}else b.abilityLeft=0;
        break;
    case BattleAction::Heal:setTool(BattleHeartShape,b.abilityLeft);b.hp=std::min(b.maxHp,b.hp+b.maxHp*BattleHealShare);self(R*1.2f);PlayDevourSound(devourSounds_.rankUp,.6f);break;
    case BattleAction::Heli:setTool(GimmickHeliShape,b.abilityLeft);PlayDevourSound(devourSounds_.morph,.5f,1.3f);break;
    case BattleAction::Water:setTool(GimmickGunShape,b.abilityLeft);PlayDevourSound(devourSounds_.morph,.5f,1.1f);break;
    case BattleAction::Spin:setTool(GimmickKomaShape,b.abilityLeft);PlayDevourSound(devourSounds_.morph,.5f,1.6f);break;
    case BattleAction::Vacuum:setTool(BattleVacuumShape,b.abilityLeft);PlayDevourSound(devourSounds_.vacuumLoop,.4f);break;
    // 将棋の駒: 駒の形になり、駒の動きで当てる (王は形でいる 3 秒間ずっと無敵)
    case BattleAction::Fu:case BattleAction::Gin:case BattleAction::Kin:case BattleAction::Kaku:case BattleAction::Hi:case BattleAction::Ou:
        setTool(BattleShogiShape,b.abilityLeft+.15f);b.abilityHitAt=action==BattleAction::Ou?.05f:.12f;PlayDevourSound(devourSounds_.morph,.5f,1.5f);break;
    case BattleAction::Kyo:setTool(BattleShogiShape,b.abilityLeft+.15f);PlayDevourSound(devourSounds_.pounce,.8f,1.2f);LifeImpact(ctx,ImpactTireLaunch,body.at,aim,1);break;
    // 置く・まく物: 置いた物は TickBattlePlaced で進める
    case BattleAction::Marbles:
        // ビー玉: 前の扇の範囲へ、ばらばらに転がるようにまく
        for(int i=0;i<BattleMarbleCount;++i){BattlePlaced m;m.kind=action;m.visual=item.visual;m.itemRadius=item.radius;m.size=battle_.unit*.28f;m.radius=battle_.unit*.45f;
            const V dir=Forward(yaw_+BU(-.7f,.7f),0);const float speed=battle_.unit*BU(5,11);m.at=body.at+dir*(R*.8f)+V{0,R*.3f,0};m.velocity=dir*speed+V{0,battle_.unit*BU(2,5),0};
            m.life=BattleMarbleLife;m.power=b.abilityPower;b.placed.push_back(m);}
        zone(body.at+aim*(R*3),R*3);PlayDevourSound(devourSounds_.note,.6f,1.6f);break;
    case BattleAction::Tower:{
        // 積み木の塔: 前に横長の壁を建てる (敵は通れない)
        // (チャレンジ: 前に物があれば、その手前に建てる。倒れる向きは、物の多い側 (前か後ろ))
        const float size=std::max(R*.55f,battle_.unit*.5f);float ahead=R*BattleTowerAhead;V fall=aim;
        if(!b.legacy){const V side{aim.z,0,-aim.x};int front=0,back=0;
            for(const auto& t:CollectBattleTargets()){V d=t.at-body.at;d.y=0;const float along=Dot(d,aim),across=std::abs(Dot(d,side));if(across>R*BattleTowerHalf+t.radius)continue;
                if(along>0&&along<ahead+size+t.radius)ahead=std::max(R*.9f,std::min(ahead,along-t.radius-size*.6f-R*ChallengeTowerGap));
                if(along>-R*2&&along<R*6)(along>=ahead?front:back)+=1;}
            if(back>front)fall=aim*-1.f;}
        BattlePlaced t;t.kind=action;t.visual=item.visual;t.itemRadius=item.radius;t.at=body.at+aim*ahead;t.at.y=InkGround(t.at);t.dir=fall;
        t.radius=R*BattleTowerHalf;t.size=size;t.life=BattleTowerLife;t.power=b.abilityPower;b.placed.push_back(t);zone(t.at,t.radius);
        PlayDevourSound(devourSounds_.morph,.6f,.8f);LifeImpact(ctx,ImpactStomp,t.at,{0,1,0},.4f);break;}
    case BattleAction::Domino:
        // ドミノ: ドミノの形で前へ倒れ込みながら、前へ一列に並べたドミノを順に倒す
        setTool(GimmickDominoShape,.9f);
        for(int i=0;i<BattleDominoCount;++i){BattlePlaced d;d.kind=action;d.visual=item.visual;d.itemRadius=item.radius;d.index=i;d.dir=aim;
            d.at=body.at+aim*(R*(1.6f+BattleDominoGap*float(i)));d.at.y=InkGround(d.at);d.size=std::max(R*1.4f,battle_.unit*.9f);d.radius=R*.9f;
            d.life=BattleDominoStart+BattleDominoStep*float(i)+1.2f;d.power=b.abilityPower;b.placed.push_back(d);}
        PlayDevourSound(devourSounds_.morph,.5f,1.3f);break;
    case BattleAction::Paint:{
        // 絵の具: 足元の床をまるく塗る (色は毎回変える)
        static const Engine::Vector4 paints[]={{.95f,.3f,.35f,1},{.3f,.55f,.95f,1},{.98f,.85f,.25f,1},{.4f,.85f,.4f,1},{.8f,.45f,.9f,1}};static int turn=0;
        BattlePlaced c;c.kind=action;c.at=body.at;c.at.y=InkGround(c.at);c.radius=R*BattlePaintRadius;c.life=BattlePaintLife;c.color=paints[(turn++)%5];b.placed.push_back(c);zone(c.at,c.radius);
        PlayDevourSound(devourSounds_.splat,.6f);break;}
    case BattleAction::Decoy:{
        // ぬいぐるみ: 少し前に置く。敵はそちらへ向かい、最後に弾けてまわりを吹き飛ばす
        BattlePlaced d;d.kind=action;d.visual=item.visual;d.itemRadius=item.radius;d.at=body.at+aim*(R*BattleDecoyAhead);d.at.y=InkGround(d.at);
        d.size=std::max(R*1.1f,battle_.unit*.8f);d.radius=R*BattleDecoyReach;d.life=BattleDecoyLife;d.power=b.abilityPower;b.placed.push_back(d);zone(d.at,d.radius);
        PlayDevourSound(devourSounds_.pounce,.6f,.9f);break;}
    // 飛び道具: 投げた物の飛び方を変える (UpdateDevourBattle の飛んでいる物の更新)
    case BattleAction::Coin:case BattleAction::Disc:case BattleAction::Bounce:case BattleAction::Homing:case BattleAction::Fan:case BattleAction::Pierce:{
        const float u=battle_.unit,power=b.abilityPower*b.abilityItem;
        auto launch=[&](V dir,float speed,float lift){BattleShot s;s.mode=action;s.by=action;s.visual=item.visual;s.at=body.at+dir*(R*.9f)+V{0,R*.45f,0};s.velocity=dir*speed+V{0,lift,0};s.push=3;return s;};
        const V dir=Forward(yaw_,0);
        if(action==BattleAction::Coin){auto s=launch(dir,u*16,0);s.size=u*.8f;s.radius=u*.4f;s.damage=BattleCoinDamage*power;s.hits=BattleCoinHops;s.life=BattleCoinLife;b.shots.push_back(s);}
        else if(action==BattleAction::Disc){auto s=launch(dir,u*13,0);s.at.y=InkGround(s.at)+u*.3f;s.size=u*1.5f;s.radius=u*.7f;s.damage=BattleDiscDamage*power;s.hits=BattleDiscHits;s.life=BattleDiscLife;s.push=5;b.shots.push_back(s);}
        else if(action==BattleAction::Bounce){auto s=launch(dir,u*10,u*6);s.size=u*.8f;s.radius=u*.45f;s.damage=BattleBounceDamage*power;s.hits=BattleBounceHits;s.life=BattleBounceLife;s.push=5;b.shots.push_back(s);}
        else if(action==BattleAction::Homing){auto s=launch(dir,u*9,0);s.size=u*1.6f;s.radius=u*.5f;s.damage=BattleHomingDamage*power;s.life=BattleHomingLife;s.push=4;b.shots.push_back(s);}
        else if(action==BattleAction::Fan){for(int i=0;i<BattleFanCards;++i){const float a=(float(i)/float(BattleFanCards-1)-.5f)*.9f;auto s=launch(Forward(yaw_+a,0),u*15,0);
            s.size=u*1.f;s.radius=u*.45f;s.damage=BattleFanDamage*power;s.life=1.4f;b.shots.push_back(s);}}
        else{auto s=launch(dir,u*18,0);s.size=u*2.4f;s.radius=u*.45f;s.damage=BattlePierceDamage*power;s.hits=99;s.life=BattlePierceLife;s.push=4;b.shots.push_back(s);}
        PlayDevourSound(devourSounds_.pounce,.55f,action==BattleAction::Coin?1.8f:action==BattleAction::Pierce?1.4f:1.15f);break;}
    // 止める・弱らせる
    case BattleAction::Pause:
        // リモコン: 敵が全部しばらく止まる (ボスは短い)
        {BattleImpact i=make(ImpactEffect::Stun,ImpactShape::Sphere);i.reach=1e9f;i.flat=false;i.seconds=BattlePauseTime;i.bigScale=BattlePauseBoss/BattlePauseTime;EmitBattleImpact(ctx,i);}
        b.message="一時停止！";b.messageAge=1.5f;PlayDevourSound(devourSounds_.note,.7f,.6f);LifeImpact(ctx,ImpactSignalPulse,body.at,{0,1,0},1);break;
    case BattleAction::Read:{
        // まんが: 前に落とすと、まわりの敵 (ボス以外) が座り込んで読みふける
        const V at=body.at+aim*(R*1.5f);
        if(b.legacy){BattleImpact i=make(ImpactEffect::Stun,ImpactShape::Sphere);i.at=at;i.reach=R*BattleReadReach;i.seconds=BattleReadTime;i.spareBig=true;i.label="読みふける…";EmitBattleImpact(ctx,i);}
        // (チャレンジ: 重い本を広げて落とし、範囲の中の物を押しつぶす)
        else{BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Sphere);i.at=at;i.reach=R*BattleReadReach;i.damage=ChallengeBookDamage*b.abilityPower;i.push=2;i.radial=true;EmitBattleImpact(ctx,i);
            LifeImpact(ctx,ImpactStomp,at,{0,1,0},.6f);PlayDevourSound(devourSounds_.splat,.7f,.7f);}
        BattlePlaced m;m.kind=action;m.visual=item.visual;m.itemRadius=item.radius;m.at=at;m.at.y=InkGround(at);m.size=R*.8f;m.life=BattleReadTime;b.placed.push_back(m);
        PlayDevourSound(devourSounds_.note,.6f,1.1f);break;}
    case BattleAction::Cage:{
        // かご: 前の近い敵 1 体 (ボス以外) を閉じ込める
        // (選ぶのは前の近い相手 1 つ。大きい相手 (ボス・重い物) は選ばない)
        bool found=false;BattleTarget target;float best=R*BattleCageReach;
        for(const auto& t:CollectBattleTargets()){if(t.big)continue;V d=t.at-body.at;d.y=0;const float dist=Length(d);if(dist<best&&(dist<R*1.5f||Dot(d*(1/std::max(.01f,dist)),aim)>.3f)){best=dist;target=t;found=true;}}
        BattleImpact i=make(ImpactEffect::Capture,ImpactShape::Sphere);i.at=found?target.at:body.at+aim*(R*2);i.reach=found?0.f:R;i.seconds=BattleCageTime;i.only=found?target.id:0;i.limit=1;i.label="閉じ込めた！";
        if(found&&EmitBattleImpact(ctx,i)>0){BattlePlaced c;c.kind=action;c.visual=item.visual;c.itemRadius=item.radius;c.target=target.id;c.at=target.at;c.size=target.radius*2.6f;c.life=BattleCageTime;b.placed.push_back(c);PlayDevourSound(devourSounds_.hit,.6f,.7f);}
        else{if(!found)EmitBattleImpact(ctx,i);b.message="閉じ込める相手がいない";b.messageAge=1.f;}
        break;}
    case BattleAction::Sticky:{
        // ノート: 紙を飛ばして、当たった敵に貼り付けて止める
        const float u=battle_.unit;BattleShot s;s.mode=action;s.by=action;s.visual=item.visual;s.at=body.at+aim*(R*.9f)+V{0,R*.45f,0};s.velocity=aim*(u*14);
        s.size=u*1.2f;s.radius=u*.5f;s.damage=BattleStickyDamage*b.abilityPower;s.life=2;s.push=1;b.shots.push_back(s);PlayDevourSound(devourSounds_.pounce,.5f,1.4f);break;}
    case BattleAction::Erase:setTool(BattleEraserShape,b.abilityLeft);b.abilityHitAt=.2f;PlayDevourSound(devourSounds_.morph,.5f,1.2f);break;
    case BattleAction::Tea:{
        // 湯のみ: 前の扇の範囲へ熱いお茶をまく。かかった敵はやけどでしばらく削られる
        {BattleImpact i=make(ImpactEffect::Burn,ImpactShape::Cone);i.reach=R*BattleTeaReach;i.cone=.35f;i.seconds=BattleBurnTime;i.damage=BattleBurnDps*b.abilityPower;i.label="あちっ！";EmitBattleImpact(ctx,i);}
        for(int k=0;k<14;++k){const V d=Forward(yaw_+BU(-.55f,.55f),0);b.fx.push_back({body.at+d*(R*BU(1,BattleTeaReach))+V{0,R*.3f,0},R*.22f,0,.4f,{.65f,.75f,.35f,1}});}
        PlayDevourSound(devourSounds_.splat,.6f,1.2f);break;}
    // 自分を強くする
    case BattleAction::Roly:setTool(BattleDarumaShape,b.abilityLeft);self(R*1.2f);b.message="起き上がりこぼし！ 殴った敵にはね返す";b.messageAge=1.5f;PlayDevourSound(devourSounds_.morph,.6f,.7f);break;
    case BattleAction::Beckon:{
        // 招き猫: まわりの敵を前の 1 か所へ招き寄せる。しばらく倒した敵が落とす物が 2 倍
        const V spot=body.at+aim*(R*3);
        {BattleImpact i=make(ImpactEffect::Pull,ImpactShape::Sphere);i.at=spot;i.reach=R*BattleBeckonReach;i.push=2;i.pullCap=R*4;i.bigScale=.25f;EmitBattleImpact(ctx,i);}
        b.dropBoost=BattleBeckonTime;b.message="招き猫: 物を落としやすくなった！";b.messageAge=2;
        BattlePlaced m;m.kind=action;m.visual=item.visual;m.itemRadius=item.radius;m.at=spot;m.at.y=InkGround(spot);m.size=R*1.2f;m.life=1.5f;b.placed.push_back(m);
        PlayDevourSound(devourSounds_.rankUp,.6f,1.3f);break;}
    case BattleAction::Brew:b.regen=BattleBrewTime;self(R*1.2f);b.message="お茶でひと息… 少しずつ回復";b.messageAge=1.5f;PlayDevourSound(devourSounds_.rankUp,.5f,.9f);break;
    case BattleAction::Fluff:b.fluff=BattleFluffTime;self(R*1.6f);b.message="ふわふわ… 攻撃が当たりにくい";b.messageAge=1.5f;PlayDevourSound(devourSounds_.morph,.4f,1.6f);break;
    case BattleAction::Smell:{
        // 靴下: 足元ににおいの雲を置く。敵は雲の中に入れない
        BattlePlaced c;c.kind=action;c.at=body.at;c.at.y=InkGround(c.at);c.radius=R*BattleSmellRadius;c.life=BattleSmellLife;c.visual=item.visual;c.itemRadius=item.radius;b.placed.push_back(c);zone(c.at,c.radius);
        PlayDevourSound(devourSounds_.splat,.5f,.7f);break;}
    case BattleAction::Kei:{
        // 桂馬: 斜め前へ跳ぶ。左右は近い敵のいる側 (いなければ右)
        const V right{aim.z,0,-aim.x};float side=1;BattleTarget nearest;if(NearestBattleTarget(body.at,1e9f,nearest))side=Dot(nearest.at-body.at,right)<0?-1.f:1.f;
        b.dashDir=Unit(aim*2+right*side);b.abilityHitAt=kKeiJump+.02f;setTool(BattleShogiShape,b.abilityLeft+.15f);PlayDevourSound(devourSounds_.pounce,.7f,1.5f);break;}
    default:break;}
    // どのチャレンジでも同じ、自分に効く能力 (回復で時間が増えるなど)
    ApplyChallengeSelf(action);
}

// 能力を進める: 突進の当たり・回り続ける当たり・水を撃つ・吸い込む・ためた一撃 (叩く・剣・ばね)。当たりは全部衝撃で出す
void ChronoSystem::TickBattleAbility(GameContext& ctx,float dt){
    auto& b=battle_;auto& body=life_.bodies[0];
    if(b.abilityLeft<=0)return;
    b.abilityAge+=dt;b.abilityLeft-=dt;
    const float R=DevourBodyRadius(body.mass),unit=battle_.unit,power=b.abilityPower*b.abilityItem;
    auto make=[&](ImpactEffect effect,ImpactShape shape){BattleImpact i;i.action=b.ability;i.effect=effect;i.shape=shape;i.at=body.at;i.dir=body.forward;return i;};
    auto around=[&](float reach,float damage,float push){BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Sphere);i.reach=reach;i.damage=damage;i.push=push;i.radial=true;EmitBattleImpact(ctx,i);};
    auto front=[&](float reach,float cone,float damage,float push){BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Cone);i.reach=reach;i.cone=cone;i.damage=damage;i.push=push;i.radial=true;EmitBattleImpact(ctx,i);};
    switch(b.ability){
    case BattleAction::Ride:case BattleAction::Rc:case BattleAction::Fly:case BattleAction::Roll:case BattleAction::Kyo:{
        // 突進: 触れた相手に当てる。転がるは同じ相手に何度も当たる (0.35 秒ごと)。香車は止まらずに突き抜ける
        const float damage=(b.ability==BattleAction::Roll?BattleRollDamage:b.ability==BattleAction::Fly?BattleFlyDamage:b.ability==BattleAction::Rc?BattleRcDamage:b.ability==BattleAction::Kyo?BattleKyoDamage:BattleRideDamage)*b.abilityPower;
        BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Sphere);i.reach=R;i.dir=b.dashDir;i.damage=damage;i.push=5;i.source=b.abilitySource;i.rehit=b.ability==BattleAction::Roll?.35f:99.f;EmitBattleImpact(ctx,i);
        break;}
    case BattleAction::Guard:{
        // 盾タックル (チャレンジ): 盾の前に触れた物に強く当てて押し出す (同じ物には 1 回)
        if(b.legacy)break;BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Cone);i.dir=b.dashDir;i.reach=R*ChallengeTackleReach;i.cone=.2f;i.damage=ChallengeTackleDamage*b.abilityPower;i.push=6;i.radial=true;
        i.source=b.abilitySource;i.rehit=99;EmitBattleImpact(ctx,i);break;}
    case BattleAction::Heli:case BattleAction::Spin:
        // 回る翼・こま: 0.2 秒ごとにまわりへ当てて弾き飛ばす
        b.abilityTick-=dt;if(b.abilityTick<=0){b.abilityTick=.2f;
            if(b.ability==BattleAction::Heli)around(R*BattleHeliReach,BattleHeliDamage*power,2);else around(R*BattleSpinReach,BattleSpinDamage*power,5);}
        break;
    case BattleAction::Water:
        // 水鉄砲: 前へ水の粒を撃ち続ける (当たると強く押し流す)
        b.abilityTick-=dt;if(b.abilityTick<=0&&b.abilityAge>.2f){b.abilityTick=.07f;const V dir=Forward(yaw_+BU(-.06f,.06f),0);const float speed=unit*18;
            BattleShot shot;shot.at=body.at+dir*(R*1.2f)+V{0,R*.5f,0};shot.velocity=dir*speed+V{0,speed*.05f,0};shot.style=2;shot.by=BattleAction::Water;shot.radius=unit*.22f;shot.damage=BattleWaterDamage*power;shot.push=5;b.shots.push_back(shot);}
        break;
    case BattleAction::Vacuum:{
        // 掃除機: 前の相手を吸い寄せて少しずつ削り、落ちた物も吸い込む
        const float reach=R*BattleVacuumReach;
        {BattleImpact i=make(ImpactEffect::Pull,ImpactShape::Cone);i.reach=reach;i.cone=.3f;i.push=6*dt;i.bigScale=.2f;EmitBattleImpact(ctx,i);}
        for(auto& d:b.drops){if(d.suck>=0)continue;V to=d.at-body.at;to.y=0;if(Length(to)<reach*1.4f){d.suck=0;d.suckFrom=d.at;}}
        b.abilityTick-=dt;if(b.abilityTick<=0){b.abilityTick=.2f;front(reach,.3f,BattleVacuumDamage*power,-1);}
        break;}
    case BattleAction::Fu:case BattleAction::Kei:case BattleAction::Gin:case BattleAction::Kin:case BattleAction::Kaku:case BattleAction::Hi:case BattleAction::Ou:{
        // 将棋の駒: 駒の動ける向きへ一度に当てる。突き (歩・銀・金) は近く、ビーム (角・飛車) はステージの端まで届く
        if(b.abilityHit||b.abilityAge<b.abilityHitAt)break;b.abilityHit=true;
        const V f=Forward(yaw_,0),rt{f.z,0,-f.x};const float power2=b.abilityPower;
        // 向き dir へ長さ len・太さ width の線の上に当て、線に沿って光の粒を並べる
        auto line=[&](V dir,float len,float width,float damage,float push){dir=Unit(dir);const V from=body.at;
            BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Segment);i.to=from+dir*len;i.dir=dir;i.width=width;i.damage=damage;i.push=push;EmitBattleImpact(ctx,i);
            const int dots=std::clamp(int(len/(R*.7f)),2,40);for(int k=1;k<=dots;++k)b.fx.push_back({from+dir*(len*float(k)/float(dots))+V{0,R*.4f,0},R*.28f,0,.3f,ActionColor(b.ability)});};
        const float thrust=R*BattleThrustLength,beam=b.arenaRadius>0?b.arenaRadius*2:R*40;const V back=f*-1.f,left=rt*-1.f;
        switch(b.ability){
        case BattleAction::Fu:line(f,thrust*1.2f,R*BattleThrustWidth,BattleFuDamage*power2,7);LifeImpact(ctx,ImpactBladeHit,body.at+f*R,f,1);break;
        case BattleAction::Gin:for(const V& d:{f,f+rt,f+left,back+rt,back+left})line(d,thrust,R*BattleThrustWidth,BattleGinDamage*power2,5);LifeImpact(ctx,ImpactBladeHit,body.at,f,1);break;
        case BattleAction::Kin:for(const V& d:{f,f+rt,f+left,rt,left,back})line(d,thrust,R*BattleThrustWidth,BattleKinDamage*power2,5);LifeImpact(ctx,ImpactBladeHit,body.at,f,1);break;
        case BattleAction::Kaku:for(const V& d:{f+rt,f+left,back+rt,back+left})line(d,beam,R*BattleBeamWidth,BattleKakuDamage*power2,4);LifeImpact(ctx,ImpactStructureHit,body.at,f,1);break;
        case BattleAction::Hi:for(const V& d:{f,rt,left,back})line(d,beam,R*BattleBeamWidth,BattleHiDamage*power2,4);LifeImpact(ctx,ImpactStructureHit,body.at,f,1);break;
        case BattleAction::Kei:around(R*BattleKeiReach,BattleKeiDamage*power2,6);LifeImpact(ctx,ImpactStomp,body.at,{0,1,0},1);break;
        case BattleAction::Ou:around(R*BattleOuReach,BattleOuDamage*power2,9);LifeImpact(ctx,ImpactStructureBreak,body.at,{0,1,0},1);
            for(int k=0;k<16;++k){const float a=6.2831853f*float(k)/16;b.fx.push_back({body.at+V{std::cos(a),0,std::sin(a)}*(R*BattleOuReach)+V{0,R*.4f,0},R*.4f,0,.4f,ActionColor(b.ability)});}break;
        default:break;}
        PlayDevourSound(devourSounds_.hit,.8f,b.ability==BattleAction::Ou?.7f:1.2f);
        break;}
    case BattleAction::Erase:
        // 消しゴム: こすった前の相手を弱らせる (受ける当たりが増える)
        if(!b.abilityHit&&b.abilityAge>=b.abilityHitAt){b.abilityHit=true;
            BattleImpact i=make(ImpactEffect::Weaken,ImpactShape::Cone);i.reach=R*BattleEraseReach;i.cone=-.1f;i.seconds=BattleWeakTime;i.damage=BattleEraseDamage*b.abilityPower;i.push=2;i.label="うすくなった";EmitBattleImpact(ctx,i);
            PlayDevourSound(devourSounds_.hit,.5f,1.5f);}
        break;
    case BattleAction::Smash:case BattleAction::Sword:case BattleAction::Spring:
        // ためた一撃: 振り下ろした・振り抜いた・着地した瞬間に 1 回当てる
        if(!b.abilityHit&&b.abilityHitAt>=0&&b.abilityAge>=b.abilityHitAt){b.abilityHit=true;
            if(b.ability==BattleAction::Smash){front(R*BattleSmashReach,-.2f,BattleSmashDamage*power,4);LifeImpact(ctx,ImpactHammerHit,body.at+body.forward*R,body.forward,1);PlayDevourSound(devourSounds_.hit,.8f,.8f);}
            else if(b.ability==BattleAction::Sword){front(R*BattleSwordReach,-.15f,BattleSwordDamage*power,3);LifeImpact(ctx,ImpactBladeHit,body.at+body.forward*R,body.forward,1);PlayDevourSound(devourSounds_.hit,.7f,1.4f);}
            else{around(R*BattleSpringReach,BattleSpringDamage*power,7);LifeImpact(ctx,ImpactStomp,body.at,{0,1,0},1);PlayDevourSound(devourSounds_.bounce,.9f,.8f);}}
        break;
    default:break;}
}

// 置いた物を進める: ビー玉の罠・積み木の塔の壁と崩れ・ドミノが順に倒れる・においの雲・ぬいぐるみが弾ける (絵の具の床は敵の移動で見る)
void ChronoSystem::TickBattlePlaced(GameContext& ctx,float dt){
    auto& b=battle_;const float unit=battle_.unit;
    for(auto& pl:b.placed){pl.age+=dt;
        auto make=[&](ImpactEffect effect,ImpactShape shape){BattleImpact i;i.action=pl.kind;i.effect=effect;i.shape=shape;i.at=pl.at;i.dir=pl.dir;return i;};
        switch(pl.kind){
        case BattleAction::Marbles:
            // 転がって止まる。止まっていない相手が踏んだら転ばせて消える
            if(!pl.landed){pl.velocity.y-=unit*30*dt;pl.at=pl.at+pl.velocity*dt;const float floor=InkGround(pl.at)+pl.size*.5f;
                if(pl.at.y<=floor){pl.at.y=floor;pl.velocity.y=0;pl.velocity.x*=std::exp(-3*dt);pl.velocity.z*=std::exp(-3*dt);if(Length(pl.velocity)<unit*.5f)pl.landed=true;}}
            if(!pl.done){BattleImpact i=make(ImpactEffect::Stun,ImpactShape::Sphere);i.reach=pl.radius;i.seconds=BattleMarbleStun;i.damage=BattleMarbleDamage*pl.power;i.spareBig=i.onlyFree=true;i.limit=1;i.label="すべった！";
                if(EmitBattleImpact(ctx,i)>0)pl.done=true;}
            break;
        case BattleAction::Tower:{
            // 壁: 相手は塔を通り抜けられない。時間が来たら前へ崩れて当てる
            const V side{pl.dir.z,0,-pl.dir.x};
            {BattleImpact i=make(ImpactEffect::Wall,ImpactShape::Segment);i.at=pl.at-side*pl.radius;i.to=pl.at+side*pl.radius;i.width=pl.size*.6f;EmitBattleImpact(ctx,i);}
            if(!pl.done&&pl.age>=pl.life-.3f){pl.done=true;
                BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Strip);i.width=pl.radius;i.back=pl.size;i.reach=pl.size+unit*4;i.damage=BattleTowerDamage*pl.power;i.push=6;EmitBattleImpact(ctx,i);
                LifeImpact(ctx,ImpactStructureBreak,pl.at,pl.dir,.8f);PlayDevourSound(devourSounds_.shatter,.8f,1.1f);}
            break;}
        case BattleAction::Domino:{
            // 1 枚ずつ順に倒れ、倒れた所に当てる
            const float fall=BattleDominoStart+BattleDominoStep*float(pl.index);
            if(!pl.done&&pl.age>=fall+.12f){pl.done=true;
                BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Sphere);i.at=pl.at+pl.dir*(pl.size*.6f);i.reach=pl.radius;i.damage=BattleDominoDamage*pl.power;i.push=4;EmitBattleImpact(ctx,i);
                if(pl.index%3==2)PlayDevourSound(devourSounds_.hit,.4f,1.6f);}
            break;}
        case BattleAction::Cage:{
            // かご: 閉じ込めた敵の上に乗る。敵が倒れたら消える
            auto found=std::find_if(b.enemies.begin(),b.enemies.end(),[&](const BattleEnemy& e){return e.id==pl.target;});
            if(found==b.enemies.end()||found->hp<=0)pl.age=pl.life+1;else{pl.at=found->at;pl.at.y=InkGround(pl.at);found->stun=std::max(found->stun,.1f);}
            break;}
        case BattleAction::Smell:{
            // においの雲: 中にいる相手を外へ押し出す
            BattleImpact i=make(ImpactEffect::PushOut,ImpactShape::Sphere);i.reach=pl.radius;i.push=std::min(1.f,dt*8);EmitBattleImpact(ctx,i);
            break;}
        case BattleAction::Decoy:
            if(!pl.done&&pl.age>=pl.life-.05f){pl.done=true;
                BattleImpact i=make(ImpactEffect::Hit,ImpactShape::Sphere);i.reach=pl.radius;i.damage=BattleDecoyDamage*pl.power;i.push=7;i.radial=true;EmitBattleImpact(ctx,i);
                for(int k=0;k<14;++k){const float a=6.2831853f*float(k)/14;b.fx.push_back({pl.at+V{std::cos(a),0,std::sin(a)}*pl.radius+V{0,pl.size*.4f,0},pl.size*.25f,0,.35f,{1,.95f,.9f,1}});}
                LifeImpact(ctx,ImpactStructureBreak,pl.at,{0,1,0},.8f);PlayDevourSound(devourSounds_.shatter,.8f,.9f);}
            break;
        default:break;}}
    b.placed.erase(std::remove_if(b.placed.begin(),b.placed.end(),[](const BattlePlaced& pl){return pl.age>pl.life||(pl.kind==BattleAction::Marbles&&pl.done);}),b.placed.end());
}

// ステージの物のモデルを好きな向きと大きさで描く (height は描く大きさ (高さか幅の大きい方)、yaw は向き、tilt は前へ倒す角度)。
// 大きさはモデルの高さと幅から決める (物の半径はギミックの当たりに合わせて大きくしてある物があり、半径からだと小さく描かれる)
void ChronoSystem::DrawBattleProp(GameContext& ctx,uint32_t visual,V at,float height,float yaw,float tilt){
    auto found=cityVisuals_.find(visual);if(found==cityVisuals_.end())return;const auto& v=found->second;const float big=std::max(v.height,2*(std::max(v.half.x,v.half.z)+v.round)),shrink=height/std::max(.01f,big);
    Engine::Transform t;t.scale=EV(v.scale*shrink);t.rotate=EV(V{v.rotate.x+tilt,yaw,v.rotate.z});t.translate=EV(at-v.center*shrink+V{0,v.height*shrink*.5f,0});
    ctx.renderer->DrawMeshInstanced(v.mesh,v.texture,t,v.color,v.shader,v.extra);
}

// 戦闘の毎フレームの更新
void ChronoSystem::UpdateDevourBattle(entt::registry& r,Player& p,GameContext& ctx){
    auto& b=battle_;auto& body=life_.bodies[0];auto& ink=r.get<InkPlayer>(player_);const auto& stage=CurrentDevourStage();
    const float dt=std::clamp(ctx.dt,0.f,.05f);b.frameDt=dt;
    if(lifeHitStop_>0){lifeHitStop_-=dt;ctx.renderer->SetRogueWorldFrozen(true);InkCamera(r,p,ctx);return;}
    ctx.renderer->SetRogueWorldFrozen(false);Engine::WindowDX::SetCursorVisible(b.phase==BattleStep::Craft);
    // 変形武器を作る画面 (決めたら始まりの演出へ)
    if(b.phase==BattleStep::Craft)UpdateWeaponCraft(ctx,dt);
    CraftScreenOpen()=b.phase==BattleStep::Craft;
    life_.age+=dt;b.messageAge=std::max(0.f,b.messageAge-dt);b.hurtFlash=std::max(0.f,b.hurtFlash-dt);
    const bool autoPlay=HasArg(L"--devour-battle-auto");
    const float R=DevourBodyRadius(body.mass),unit=battle_.unit;
    // カメラ
    if(ctx.input&&b.phase!=BattleStep::Over&&b.phase!=BattleStep::Craft){float y,x;Controls::Look(dt,y,x);yaw_+=ctx.input->GetMouseDeltaX()*.0025f+y;pitch_=std::clamp(pitch_+ctx.input->GetMouseDeltaY()*.0025f+x,-.7f,.85f);}
    // 始まりの演出 (敵はまだ出ない)
    if(b.phase==BattleStep::Intro){b.intro+=dt;if(b.intro>=(b.legacy?BattleIntro:ChallengeIntro)){b.phase=BattleStep::Fight;b.message=b.legacy?"生き残って、現れるボスを倒せ！":ChallengeOf(b.challenge).goal;b.messageAge=2.5f;}}
    b.comboClock=std::max(0.f,b.comboClock-dt);if(b.comboClock<=0)b.combo=0;
    for(auto& m:b.impactMemo)m.left-=dt;b.impactMemo.erase(std::remove_if(b.impactMemo.begin(),b.impactMemo.end(),[](const BattleImpactMemo& m){return m.left<=0;}),b.impactMemo.end());
    // スロットの引き直し: 空いたスロットは袋に物があればルーレットを回し、止まったら引く
    // (変形武器を使うときは、スロットは使わない)
    if(!WeaponsActive())for(int i=0;i<b.slotCount;++i){auto& slot=b.slots[size_t(i)];
        if(slot.roulette>=0){slot.roulette+=dt;if(slot.roulette>=BattleRouletteTime){FillBattleSlot(i,false);if(!slot.items.empty())PlayDevourSound(devourSounds_.note,.5f,1.2f);}}
        else if(slot.items.empty()&&std::any_of(b.bag.begin(),b.bag.end(),[](const BattleBag& x){return !x.items.empty();}))FillBattleSlot(i,true);}
    // 移動 (突進の間は向きに進む)
    V move{};
    if(b.phase==BattleStep::Fight){
        // (リモコンで時計が止まっている間は、チャレンジの時間が進まない)
        if(b.timeStop>0)b.timeStop-=dt;else b.clock+=dt;b.cooldown=std::max(0.f,b.cooldown-dt);b.guard=std::max(0.f,b.guard-dt);
        // 自分を強くする効き目: 急須は 0.5 秒ごとに回復、ティッシュと招き猫は時間が来たら切れる
        b.fluff=std::max(0.f,b.fluff-dt);b.dropBoost=std::max(0.f,b.dropBoost-dt);b.snack=std::max(0.f,b.snack-dt);
        if(b.regen>0){b.regen-=dt;b.regenTick-=dt;if(b.regenTick<=0){b.regenTick=.5f;const float add=b.maxHp*BattleBrewShare*.5f;b.hp=std::min(b.maxHp,b.hp+add);}}
        V input{};Controls::Move(input.x,input.z);
        if(autoPlay){input={std::sin(life_.age*.7f),0,.3f*std::cos(life_.age*.5f)};
            // 確認用の自動操作: 近くに敵がいなければ、落ちた物を拾いに行く (向きはカメラ基準に直す)
            float enemyNear=1e9f;for(const auto& e:b.enemies)enemyNear=std::min(enemyNear,Length(e.at-body.at));
            const BattleDrop* drop=nullptr;float dropNear=1e9f;for(const auto& d:b.drops)if(d.suck<0){float len=Length(d.at-body.at);if(len<dropNear){dropNear=len;drop=&d;}}
            if(drop&&enemyNear>R*4){V to=drop->at-body.at;to.y=0;to=Unit(to);input={to.x*std::cos(yaw_)-to.z*std::sin(yaw_),0,to.x*std::sin(yaw_)+to.z*std::cos(yaw_)};}
            // スライム陣取り: まだ陣地でない一番近いマスへ向かう
            V open;if(!b.legacy&&b.challenge==ChallengeKind::Territory&&NearestOpenCell(body.at,open)){V to=open-body.at;to.y=0;to=Unit(to);input={to.x*std::cos(yaw_)-to.z*std::sin(yaw_),0,to.x*std::sin(yaw_)+to.z*std::cos(yaw_)};}}
        // 確認用 (--challenge-matrix): 全部の能力 x チャレンジの実測。歩く向きも実測が決める
        if(b.matrix.active)TickChallengeMatrix(ctx,dt,input);
        V wish{input.x*std::cos(yaw_)+input.z*std::sin(yaw_),0,-input.x*std::sin(yaw_)+input.z*std::cos(yaw_)};
        // 速さは普段の移動と同じ (配合と大きさで決まり、加速と減速もある)
        float walk=life_.recipe.Speed()*LifeTravelScale(body)*DevourSpeedScale(body.mass);
        // 絵の具の床の上では速い
        for(const auto& pl:b.placed)if(pl.kind==BattleAction::Paint){V d=body.at-pl.at;d.y=0;if(Length(d)<pl.radius){walk*=BattlePaintBoost;break;}}
        const bool dashing=b.abilityLeft>0&&DashAction(b.ability)&&!(b.ability==BattleAction::Kei&&b.abilityAge>kKeiJump);
        if(dashing){b.velocity=b.dashDir*(walk*BattleDashBoost(b.ability));body.forward=b.dashDir;}
        else b.velocity=LifeMoveVelocity(b.velocity,wish,life_.recipe,true,dt,walk/std::max(.01f,life_.recipe.Speed())*(b.toolShape==GimmickKomaShape?.7f:1.f));
        // 狙って使う形 (盾・ハンマー・剣・水鉄砲・掃除機) の間は、狙っている向き (カメラ) を向く
        const int t=b.toolShape;
        if(!dashing&&(t==BattleShieldShape||t==BattleHammerShape||t==BattleSwordShape||t==GimmickGunShape||t==BattleVacuumShape||t==BattleShogiShape||t==BattleEraserShape))body.forward=Forward(yaw_,0);
        else if(!dashing&&Length(wish)>.1f)body.forward=Unit(wish);
        // スロットの選び替え: 1〜5 キー (最後は飴玉)・十字キー・LB / RB
        // (変形武器は 1 本だけで、回数の制限もないので選ばない)
        const int slots=WeaponsActive()?0:b.slotCount,choices=slots+1;
        if(ctx.input){const uint8_t keys[]={DIK_1,DIK_2,DIK_3,DIK_4,DIK_5};for(int i=0;i<choices&&i<5;++i)if(ctx.input->Trigger(keys[i]))b.selected=i;}
        if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_UP))b.selected=0;if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT))b.selected=std::min(1,choices-1);
        if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_DOWN))b.selected=std::min(2,choices-1);if(UI::PadPressed(XINPUT_GAMEPAD_DPAD_LEFT))b.selected=std::min(3,choices-1);
        if(UI::PadPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER))b.selected=(b.selected+1)%choices;if(UI::PadPressed(XINPUT_GAMEPAD_LEFT_SHOULDER))b.selected=(b.selected+choices-1)%choices;
        // 使う (押し続けると待ち時間ごとに使う)
        bool fire=Controls::MorphAttack();
        if(autoPlay){
            // 確認用の自動操作: 一番近い敵を向き、2 秒ごとに使うスロットを替える (空なら飴玉)
            BattleTarget nearest;const bool found=NearestBattleTarget(body.at,1e9f,nearest);
            if(found){V d=nearest.at-body.at;yaw_=std::atan2(d.x,d.z);}
            fire=found||!b.legacy;b.selected=int(b.clock/2)%std::max(1,slots);
            if(!WeaponsActive()&&b.slots[size_t(b.selected)].items.empty())b.selected=b.slotCount;}
        // 確認用 (--challenge-impact-check): 袋は使わず、全部の能力を順に 1 回ずつ使う (一番近い相手がいればそちらを向く)
        if(b.impactCheck){fire=false;b.checkClock-=dt;
            if(b.checkClock<=0&&b.checkNext<int(BattleAction::Count)){b.checkClock=BattleImpactCheckStep;
                BattleTarget nearest;if(NearestBattleTarget(body.at,1e9f,nearest)){V d=nearest.at-body.at;yaw_=std::atan2(d.x,d.z);}
                BattleItem item;item.radius=b.unit*.5f;++b.spent;b.abilityLeft=0;StartBattleAbility(ctx,BattleAction(b.checkNext++),item);}}
        if(b.matrix.active)fire=false;
        // (能力の最中 (突進・回転など) は次を使わない。飴玉はいつでも投げられる)
        // (変形武器: 押している間、連射の間隔ごとに撃つ)
        if(fire&&WeaponsActive())UseBattleWeapon(ctx);
        else if(fire&&b.cooldown<=0){if(b.selected>=slots)UseBattleCandy();else if(b.abilityLeft<=0||BattleActionLiquid(b.ability)||(!b.legacy&&b.challenge==ChallengeKind::Territory&&b.abilityAge>=ChallengeTerritoryChain))UseBattleSlot(ctx,b.selected);}
        if(WeaponsActive())TickBattleWeapons(ctx,dt);
        b.previousFire=fire;
        // チャレンジ: 時間が来たら終わり (スコアを見せて結果画面へ)
        if((!b.legacy||b.impactCheck)&&!b.matrix.active&&b.clock>=b.time){b.won=true;b.phase=BattleStep::Over;b.overAge=0;b.message.clear();b.messageAge=0;PlayDevourSound(devourSounds_.rankTop,.8f);}
        // 敵を出す (旧来の戦闘だけ): 時間とともに間隔が短くなる。最後の 30 秒にボス
        b.spawnClock-=dt;
        if(b.legacy&&b.spawnClock<=0&&int(b.enemies.size())<BattleEnemyLimit){b.spawnClock=BattleSpawnInterval(b.clock,b.time);
            BattleEnemy e;e.id=b.nextId++;const float angle=BU(0,6.2831853f),dist=std::max(R*3.f,unit*9.f);e.at=body.at+V{std::cos(angle)*dist,0,std::sin(angle)*dist};
            // (広いステージの円の外に出たら、円の縁へ寄せる)
            if(b.arenaRadius>0){V o=e.at-b.arenaAt;o.y=0;const float len=Length(o),limit=b.arenaRadius*.95f;if(len>limit)e.at=b.arenaAt+o*(limit/len);}
            e.radius=BattleEnemyRadius(b.unit,false);e.speed=BattleEnemySpeed(b.unit,false)*BU(.85f,1.15f);e.hp=e.maxHp=BattleEnemyHp(b.clock);b.enemies.push_back(e);}
        if(b.legacy&&!b.bossSpawned&&b.clock>=BattleBossAt(b.time)){b.bossSpawned=true;BattleEnemy e;e.id=b.nextId++;e.boss=true;const float angle=yaw_;const float dist=std::max(R*4.f,unit*12.f);
            e.at=body.at+V{std::sin(angle)*dist,0,std::cos(angle)*dist};if(b.arenaRadius>0){V o=e.at-b.arenaAt;o.y=0;const float len=Length(o),limit=b.arenaRadius*.9f;if(len>limit)e.at=b.arenaAt+o*(limit/len);}
            e.radius=BattleEnemyRadius(b.unit,true);e.speed=BattleEnemySpeed(b.unit,true);e.hp=e.maxHp=BattleBossHp;b.enemies.push_back(e);
            b.message="ボスが来た！ 倒せばクリア";b.messageAge=3;LifeImpact(ctx,ImpactGuardianStomp,e.at,{0,1,0},1);}
        // 時間いっぱい耐えたら勝ち
        // (クリアはボスを倒したときだけ。時間が過ぎても、ボスを倒すか HP が 0 になるまで続く)
    }
    else b.velocity=LifeMoveVelocity(b.velocity,{},life_.recipe,true,dt);
    move=b.velocity*dt;
    TickBattleAbility(ctx,dt);TickBattlePlaced(ctx,dt);TickChallenge(ctx,dt);
    // 形の時間と姿勢: 能力ごとに動かす。時間が来たら液体へ戻る (守っている間は盾のまま)
    if(b.toolShape>0){b.toolAge+=dt;b.toolLeft-=dt;b.toolPrevious=b.toolPose;auto& pose=b.toolPose;const float a=b.toolAge;
        switch(b.toolShape){
        case BattleHammerShape:{const float t=std::clamp((a-BattleSmashWindup)/BattleSmashSwing,0.f,1.f);pose.pitch=-1.1f+2.6f*t*t;break;}
        case BattleHeartShape:pose.spin+=dt*4;break;
        case GimmickHeliShape:pose.spin+=dt*28;pose.pitch=.15f;break;
        case GimmickKomaShape:pose.spin+=dt*24;pose.roll=.08f*std::sin(a*9);break;
        case GimmickBallShape:pose.spin+=dt*Length(b.velocity)/std::max(.1f,R*.6f);break;
        case GimmickPlaneShape:pose.roll=.25f*std::sin(a*5);pose.pitch=-.1f;break;
        case BattleEraserShape:pose.spin=1.2f*std::sin(a*28);break;
        case BattleDarumaShape:pose.roll=.25f*std::sin(a*7)*std::exp(-a*.3f);break;
        case GimmickDominoShape:pose.tip=1.4f*std::clamp((a-.15f)/.4f,0.f,1.f);break;
        case BattleSwordShape:{const float t=std::clamp(a/.22f,0.f,1.f);pose.spin=-1.3f+2.6f*(1-(1-t)*(1-t));break;}
        case BattleSpringShape:pose.squash=a<.45f?.5f*a/.45f:a<.65f?.5f-.9f*(a-.45f)/.2f:std::max(0.f,-.4f+(a-.65f)*1.6f);pose.squash=std::clamp(pose.squash,-.4f,.5f);break;
        default:break;}
        if(b.toolLeft<=0&&!(b.toolShape==BattleShieldShape&&b.guard>0)){
            // 守っている間に別の形を使ったら、終わったあと盾に戻る
            if(b.guard>0){b.toolShape=BattleShieldShape;b.toolAge=0;b.toolLeft=b.guard;b.toolPose=b.toolPrevious={};}else b.toolShape=0;}}
    // 体を動かす: 固い箱は Move で、建物と動かない大きな物は足元の丸で止める
    {const float half=std::max(.5f,R*.3f);bool grounded=false;V at=Move(body.at,move,{half,.5f,half},grounded);
        for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];const bool building=std::find(b.buildings.begin(),b.buildings.end(),i)!=b.buildings.end();
            if(!building&&!(prop.active&&prop.foot>0))continue;
            const float footR=prop.foot>0?prop.foot:prop.radius*.7f;V d=at-prop.at;d.y=0;const float reach=footR+R*.6f,len=Length(d);
            if(len<reach&&len>.001f)at=at+d*((reach-len)/len);}
        // 積み木は通れない (下の面が体より低いもの。体当たりや能力で壊して進む)
        for(const auto& blk:b.blocks){if(!blk.alive||blk.at.y-blk.size*.5f>at.y+R*.3f)continue;V d=at-blk.at;d.y=0;const float reach=blk.size*.6f+R*.6f,len=Length(d);
            if(len<reach&&len>.001f)at=at+d*((reach-len)/len);}
        // 戦う場所の円から出ない
        // スライム陣取り: ラグの外と家具には入れない
        if(!b.legacy&&b.challenge==ChallengeKind::Territory)BlockTerritoryBody(at,R);
        else if(b.arenaRadius>0){V d=at-b.arenaAt;d.y=0;const float limit=std::max(1.f,b.arenaRadius-R*.6f),len=Length(d);if(len>limit)at=b.arenaAt+V{d.x*(limit/len),0,d.z*(limit/len)};}
        at.y=InkGround(at)+1.25f;const V moved=(at-body.at)*(1/std::max(dt,1e-4f));body.at=at;
        // 液体の体の動き (中心の遅れ・進む向きへの伸び・核の速さ) を普段の移動と同じように進める。
        // 進めないと粒子の追いつける速さが上がらず、体の後ろが置いていかれて三日月の形になる
        LifeTickMotion(body,life_.recipe,moved,true,false,dt,body.forward);}
    // 敵: 体へ向かい、触れたら攻撃する
    // ぬいぐるみが置いてあれば、敵はそちらへ向かう (ボス以外)
    const BattlePlaced* decoy=nullptr;for(const auto& pl:b.placed)if(pl.kind==BattleAction::Decoy&&!pl.done)decoy=&pl;
    for(auto& e:b.enemies){if(e.hp<=0)continue;e.flash=std::max(0.f,e.flash-dt);e.hitCool=std::max(0.f,e.hitCool-dt);e.stun=std::max(0.f,e.stun-dt);
        V d=body.at-e.at;d.y=0;const float len=Length(d);
        // 移動: 転んでいる間は止まる。絵の具の床の上は遅い
        float speed=e.stun>0?0.f:e.speed;
        for(const auto& pl:b.placed)if(pl.kind==BattleAction::Paint){V p2=e.at-pl.at;p2.y=0;if(Length(p2)<pl.radius){speed*=BattlePaintSlow;break;}}
        if(decoy&&!e.boss){V g=decoy->at-e.at;g.y=0;const float gl=Length(g);if(b.phase==BattleStep::Fight&&gl>.01f)e.at=e.at+g*(std::min(gl-(decoy->size*.6f+e.radius),speed*dt)/gl);}
        else if(b.phase==BattleStep::Fight&&len>.01f)e.at=e.at+d*(std::min(len-(R+e.radius)*.9f,speed*dt)/len);
        e.at=e.at+e.knock*dt;e.knock=e.knock*std::exp(-6*dt);
        if(b.arenaRadius>0){V a=e.at-b.arenaAt;a.y=0;const float out=Length(a),limit=b.arenaRadius;if(out>limit)e.at=b.arenaAt+V{a.x*(limit/out),0,a.z*(limit/out)};}
        e.at.y=InkGround(e.at)+e.radius;
        // 弱り (消しゴム) とやけど (湯のみ): やけどは 0.5 秒ごとに削る
        e.weak=std::max(0.f,e.weak-dt);
        if(e.burn>0){e.burn-=dt;e.burnTick-=dt;if(e.burnTick<=0){e.burnTick=.5f;HurtBattleEnemy(ctx,e,e.burnDps*.5f,V{});}}
        if(e.hp<=0)continue;
        if(b.phase==BattleStep::Fight&&e.stun<=0&&len<R+e.radius+unit*.2f&&e.hitCool<=0){e.hitCool=BattleEnemyHitCooldown;
            // だるま: 殴ってきた敵にはね返す
            if(b.toolShape==BattleDarumaShape){HurtBattleEnemy(ctx,e,BattleRolyReflect*b.abilityPower,Unit(d*-1.f)*unit*6);b.pops.push_back({body.at+V{0,R*1.2f,0},"はね返した！",0,{1,.5f,.4f,1}});}
            // 変形している間は無敵 (全部の能力を確かめる間も、やられないようにする)
            if(b.impactCheck)continue;
            if(b.toolShape>0){b.fx.push_back({body.at+Unit(d*-1.f)*R,R*.3f,0,.15f,{.8f,.9f,1,1}});continue;}
            // ティッシュの雲の間は攻撃をかわしやすい
            if(b.fluff>0&&BU(0,1)<BattleFluffDodge){b.pops.push_back({body.at+V{0,R*1.2f,0},"かわした！",0,{.7f,1,.9f,1}});continue;}
            float damage=e.boss?BattleBossDamage:BattleEnemyDamage;if(b.guard>0)damage*=BattleGuardTake;b.hp-=damage;b.hurtFlash=.25f;
            LifeImpact(ctx,ImpactPlayerHurt,body.at,Unit(d*-1.f+V{0,.01f,0}),e.boss?1.f:.5f);
            if(b.hp<=0){b.hp=0;b.won=false;b.phase=BattleStep::Over;b.overAge=0;b.message="やられた…";b.messageAge=3;}}}
    // 敵どうしが重ならないよう少し離す
    for(size_t i=0;i<b.enemies.size();++i)for(size_t j=i+1;j<b.enemies.size();++j){auto& a=b.enemies[i];auto& c=b.enemies[j];V d=a.at-c.at;d.y=0;const float len=Length(d),gap=(a.radius+c.radius)*.9f;
        if(len<gap&&len>.001f){V push=d*((gap-len)/len*.5f);a.at=a.at+push;c.at=c.at-push;}}
    // 投げた物・飴玉・水: 飛んで、敵に当たったら消える
    for(auto& s:b.shots){
        // (変形武器の弾は性質ごとに進める)
        if(s.fused){TickFusedShot(ctx,s,dt);continue;}
        s.age+=dt;
        if(s.mode!=BattleAction::Throw){
            // 飛び道具: 飛び方ごとに進め、当たり方も変える
            s.hitCool=std::max(0.f,s.hitCool-dt);s.spin+=dt*12;const float speed=Length(V{s.velocity.x,0,s.velocity.z});
            if(s.mode==BattleAction::Homing){
                // 紙飛行機: 一番近い敵へ向きを少しずつ曲げる
                BattleTarget target;const bool found=NearestBattleTarget(s.at,1e9f,target);
                if(found&&speed>.01f){const float now=std::atan2(s.velocity.x,s.velocity.z);V to=target.at-s.at;const float want=std::atan2(to.x,to.z);
                    const float turn=std::clamp(std::remainder(want-now,6.2831853f),-BattleHomingTurn*dt,BattleHomingTurn*dt);const V d=Forward(now+turn,0);s.velocity={d.x*speed,0,d.z*speed};}
                s.at.y+=((InkGround(s.at)+R*.6f)-s.at.y)*std::min(1.f,dt*4);}
            if(s.mode==BattleAction::Bounce){s.velocity.y-=unit*20*dt;}
            s.at=s.at+s.velocity*dt;
            // 飛んだ跡: 後ろから見ると細い物 (鉛筆・紙飛行機・輪・十円玉) も道すじが分かるよう、色の粒を少しの間残す
            if(s.mode!=BattleAction::Bounce&&s.mode!=BattleAction::Fan&&s.mode!=BattleAction::Sticky&&std::fmod(s.age,.03f)<dt)b.fx.push_back({s.at,s.radius*.45f,0,.28f,ActionColor(s.mode)});
            const float floor=InkGround(s.at)+s.radius;
            if(s.mode==BattleAction::Bounce&&s.at.y<floor){s.at.y=floor;s.velocity.y=std::max(std::abs(s.velocity.y)*.82f,unit*5);}
            if(s.mode==BattleAction::Disc)s.at.y=InkGround(s.at)+unit*.3f;
            // 輪とボールはステージの縁で跳ね返る (跳ね返ったら同じ敵にまた当たれる)
            if((s.mode==BattleAction::Disc||s.mode==BattleAction::Bounce)&&b.arenaRadius>0){V o=s.at-b.arenaAt;o.y=0;const float len=Length(o);
                if(len>b.arenaRadius-s.radius&&len>.01f){const V n=o*(1/len);const float into=Dot(s.velocity,n);if(into>0){s.velocity=s.velocity-n*(2*into);s.hit.clear();}s.at=b.arenaAt+n*(b.arenaRadius-s.radius)+V{0,s.at.y-b.arenaAt.y,0};}}
            // 当たり: 一番近い 1 つに衝撃を出す (同じ相手には続けて当てない)。当たったあとの動きは飛び方ごと
            if(s.hits>0&&s.hitCool<=0){
                BattleImpact i;i.action=s.by;i.effect=s.mode==BattleAction::Sticky?ImpactEffect::Stun:ImpactEffect::Hit;i.shape=ImpactShape::Sphere;i.at=s.at;i.reach=s.radius;i.flat=s.mode==BattleAction::Disc;
                i.dir=V{s.velocity.x,0,s.velocity.z};i.damage=s.damage;i.push=s.push;i.limit=1;
                if(s.mode==BattleAction::Sticky){i.seconds=BattleStickyTime;i.bigScale=.4f;i.label="ぺたっ";}
                std::vector<BattleTarget> touched;
                if(EmitBattleImpact(ctx,i,&touched,&s.hit)>0){const BattleTarget& e=touched.front();--s.hits;s.hit.push_back(e.id);
                    b.fx.push_back({s.at,s.radius*1.4f,0,.18f,ActionColor(s.mode)});
                    if(s.mode==BattleAction::Coin){
                        // 十円玉: 次に近い相手へ跳ね移る (いなければ消える)
                        BattleTarget next;
                        if(s.hits>0&&NearestBattleTarget(s.at,R*9,next,&s.hit)){V to=next.at-s.at;to.y=0;s.velocity=Unit(to)*std::max(speed,unit*16);PlayDevourSound(devourSounds_.note,.4f,1.9f);}else s.hits=0;}
                    else if(s.mode==BattleAction::Bounce){V away=s.at-e.at;away.y=0;away=Length(away)>.01f?Unit(away):V{0,0,1};s.velocity=V{away.x*speed,unit*7,away.z*speed};s.hitCool=.15f;}
                    else if(s.mode==BattleAction::Disc){s.hitCool=.08f;}
                    else if(s.mode==BattleAction::Homing||s.mode==BattleAction::Fan||s.mode==BattleAction::Sticky){s.hits=0;}}}
            if(s.hits<=0||s.age>s.life||(s.mode!=BattleAction::Bounce&&s.mode!=BattleAction::Disc&&s.mode!=BattleAction::Homing&&s.at.y<InkGround(s.at)))s.age=99;
            continue;}
        s.velocity.y-=unit*(s.style==2?2.f:6.f)*dt;s.at=s.at+s.velocity*dt;
        {BattleImpact i;i.action=s.by;i.candy=s.style==1;i.shape=ImpactShape::Sphere;i.at=s.at;i.reach=s.radius;i.flat=false;i.dir=V{s.velocity.x,0,s.velocity.z};i.damage=s.damage;i.push=s.push;i.limit=1;
            std::vector<BattleTarget> touched;
            if(EmitBattleImpact(ctx,i,&touched,&s.hit)>0){--s.hits;s.hit.push_back(touched.front().id);s.velocity.x*=ChallengeThrowSlow;s.velocity.z*=ChallengeThrowSlow;if(s.hits<=0)s.age=99;
                if(s.style!=2)b.fx.push_back({s.at,s.radius*1.5f,0,.2f,s.style==1?Engine::Vector4{1,.6f,.85f,.7f}:Engine::Vector4{1,.8f,.4f,.7f}});}}
        // 投げた物 (飴玉と水以外) は、当たれる数が残っていれば床を転がる (チャレンジ)
        const float floorY=InkGround(s.at)+s.radius*.5f;
        if(s.style==0&&s.hits>0&&!b.legacy&&s.at.y<floorY){s.at.y=floorY;s.velocity.y=0;s.velocity.x*=std::exp(-ChallengeThrowRoll*dt);s.velocity.z*=std::exp(-ChallengeThrowRoll*dt);
            if(Length(s.velocity)<unit*1.2f)s.age=99;}
        else if(s.at.y<InkGround(s.at)||(s.style==2&&s.age>.8f))s.age=99;}
    // (変形武器の弾が分かれてできた弾を足す。進めている間に足すと並びが壊れるので、あとで足す)
    b.shots.insert(b.shots.end(),b.newShots.begin(),b.newShots.end());b.newShots.clear();
    // 落ちた物: 飛んで跳ね、近づくと吸い寄せられて吸い込まれる (体より大きくても吸収できる)
    for(auto& d:b.drops){d.age+=dt;
        if(d.suck>=0){d.suck+=dt;continue;}
        d.velocity.y-=unit*30*dt;d.at=d.at+d.velocity*dt;const float floor=InkGround(d.at)+d.radius*.6f;
        if(d.at.y<floor){d.at.y=floor;if(d.velocity.y<0)d.velocity.y=-d.velocity.y*.3f;d.velocity.x*=std::exp(-6*dt);d.velocity.z*=std::exp(-6*dt);d.spin*=std::exp(-4*dt);}
        if(b.phase==BattleStep::Fight){V to=body.at-d.at;to.y=0;if(Length(to)<R*BattleDropMagnet+d.radius){d.suck=0;d.suckFrom=d.at;}}}
    for(auto& d:b.drops)if(d.suck>=BattleDropSuck){AbsorbBattleDrop(d);d.age=1e9f;}
    b.drops.erase(std::remove_if(b.drops.begin(),b.drops.end(),[](const BattleDrop& d){return (d.age>BattleDropLife&&d.suck<0)||d.age>1e8f;}),b.drops.end());
    for(auto& pop:b.pops)pop.age+=dt;
    b.pops.erase(std::remove_if(b.pops.begin(),b.pops.end(),[](const BattlePop& pop){return pop.age>1;}),b.pops.end());
    // スライム陣取り: 飛んだ物が落ちた・消えた所に体液のしぶき
    if(!b.legacy&&b.challenge==ChallengeKind::Territory)for(const auto& s:b.shots)if(s.age>(s.mode==BattleAction::Throw?3.f:s.life)||s.age>=99){
        BattleImpact i;i.action=s.by;i.candy=s.style==1;i.effect=ImpactEffect::Zone;i.at=s.at;i.reach=unit*(s.style==1?ChallengeTerritoryCandySplash:ChallengeTerritorySplash);PaintTerritoryImpact(i);}
    b.shots.erase(std::remove_if(b.shots.begin(),b.shots.end(),[](const BattleShot& s){return s.age>(s.mode==BattleAction::Throw?3.f:s.life)||s.age>=99;}),b.shots.end());
    b.enemies.erase(std::remove_if(b.enemies.begin(),b.enemies.end(),[](const BattleEnemy& e){return e.hp<=0;}),b.enemies.end());
    for(auto& f:b.fx)f.age+=dt;b.fx.erase(std::remove_if(b.fx.begin(),b.fx.end(),[](const BattleFx& f){return f.age>f.life;}),b.fx.end());
    // 終わり: 少し見せてから結果画面へ
    if(b.phase==BattleStep::Over){b.overAge+=dt;
        if(b.overAge>=2.5f){
            if(autoPlay){std::filesystem::create_directories("tests/out");std::ofstream log("tests/out/devour-battle.txt",std::ios::app);int left=0;for(const auto& x:b.bag)left+=int(x.items.size());for(const auto& s:b.slots)left+=int(s.items.size());
                log<<"stage="<<stage.id<<" challenge="<<ChallengeKindId(b.challenge)<<" legacy="<<b.legacy<<" time="<<b.time<<" score="<<b.score<<" rank="<<b.rank<<" won="<<b.won<<" boss_down="<<b.bossDown<<" clock="<<b.clock<<" kills="<<b.kills<<" pickups="<<b.pickups<<" spent="<<b.spent<<" pattern="<<b.blockPattern<<" territory="<<TerritoryCoveredCells()<<"/"<<b.territory.open<<" broken="<<b.broken[0]<<"/"<<b.broken[1]<<"/"<<b.broken[2]<<" cm="<<DevourSizeCm(stage,body.mass)<<" items_left="<<left;if(b.crafted)log<<" weapon="<<b.weapon.name<<" shots="<<CraftShots(b.weapon.points[0])<<" power="<<CraftPower(b.weapon.points[1])<<" interval="<<CraftInterval(b.weapon.points[2],b.weapon.lv[size_t(FuseTrait::Heavy)]);log<<'\n';}
            if(b.impactCheck)WriteImpactCheck();
            // 自己ベストのスコアを残す
            if(!b.legacy&&!b.impactCheck){int& best=challengeBests_[ChallengeBestKey(stage.id,b.challenge)];if(b.score>best){best=b.score;b.newBest=true;SaveSlimeLab();}}
            // 結果画面は吸収の結果 (大きさのランク) を見せるので、体を吸収が終わったときの大きさに戻す
            b.finalCm=DevourSizeCm(stage,body.mass);body.mass=b.startMass;
            // 次のステージへはいつでも進める。結果の「クリア」は大きさの目標に届いたか
            b.phase=BattleStep::None;Finish(p,b.legacy?b.won:DevourRankAt(stage,b.startMass)>0);}}
    // 体の位置と見た目を反映する
    auto& transform=r.get<TransformComponent>(player_);transform.translate=Write(body.at);const float size=LifeScale(body.mass);transform.scale={size,size,size};transform.rotate.y=std::atan2(body.forward.x,body.forward.z);
    ink.fluidDirection=body.forward;ink.groundHeight=InkGround(body.at);ink.groundSlope={};ink.airBlend=0;ink.fluidAspect=body.aspect;ink.fluidMotion=std::min(1.f,Length(body.velocity)/std::max(1.f,BattleMoveSpeed(battle_.unit,DevourBodyRadius(body.mass))));ink.swimming=false;ink.phase=SlimePhase::Roaming;
    p.worldScale=p.aimScale=1;p.aiming=false;p.action=Action::Free;
    InkCamera(r,p,ctx);Presentation(r,p,ctx);SyncSlimeBodies(ctx);
}

// 仮の見た目: 敵は色の付いた球 (ボスは大きい紫)、投げた物はその物のモデル (無ければ球)、飴玉はピンク、水は水色の粒
void ChronoSystem::DrawDevourBattle(GameContext& ctx){
    if(!BattleActive()||finished_)return;const auto& b=battle_;const auto& body=life_.bodies[0];
    DrawBattleFloorGauge(ctx);DrawChallenge(ctx);
    auto orb=[&](V at,float radius,Engine::Vector4 color){Engine::Transform t;t.translate=EV(at);t.scale={radius,radius,radius};ctx.renderer->DrawMeshInstanced(sphere_,white_,t,color);};
    // スライム陣取り: 仮の玉 (見た目の無い物の代わりの玉・効果の粒・水の玉) は描かない (体液のしずくが代わりになる)
    const bool territory=!b.legacy&&b.challenge==ChallengeKind::Territory;
    auto stub=[&](V at,float radius,Engine::Vector4 color){if(!territory)orb(at,radius,color);};
    for(const auto& e:b.enemies){Engine::Vector4 color=e.boss?Engine::Vector4{.55f,.25f,.75f,1}:Engine::Vector4{.85f,.22f,.2f,1};
        // 状態の色: 弱っている (消しゴム) は薄い色、やけど (湯のみ) は橙、止まっている (リモコン・まんが・ノート・ビー玉) は灰色
        if(e.weak>0)color={color.x*.5f+.45f,color.y*.5f+.45f,color.z*.5f+.5f,1};
        if(e.burn>0&&std::fmod(e.burn*6,1.f)<.5f)color={1,.55f,.15f,1};
        if(e.stun>0)color={color.x*.55f+.2f,color.y*.55f+.2f,color.z*.55f+.25f,1};
        if(e.flash>0)color={1,1,1,1};
        orb(e.at,e.radius,color);
        // 目 (向きが分かるように)
        V d=body.at-e.at;d.y=0;d=Length(d)>.01f?Unit(d):V{0,0,1};const V side{d.z,0,-d.x};
        for(int k=-1;k<=1;k+=2)orb(e.at+d*(e.radius*.8f)+side*(e.radius*.35f*float(k))+V{0,e.radius*.25f,0},e.radius*.18f,{1,1,1,1});}
    // (F4) 変形武器の形の間は、材料の物を体の中のそれぞれの所に浮かべる (どれを混ぜた武器か分かるように)。形になるにつれて大きくなり、ゆっくり回る
    if(WeaponsActive()&&b.weaponShape>0&&b.weaponForm>0)if(const auto* g=WeaponShapeOf(b.weaponShape)){
        const float scale=LifeScale(body.mass),grow=std::clamp(b.weaponAge/.3f,0.f,1.f)*std::clamp(b.weaponForm/.15f,0.f,1.f);V f=body.forward;f.y=0;f=Length(f)>.01f?Unit(f):V{0,0,1};const V right{f.z,0,-f.x};
        for(int i=0;i<std::min(g->anchors,b.weapon.filled);++i){const V local=WeaponShapePose(b.weaponShape,g->anchor[size_t(i)],b.weaponPose);const float size=2*g->anchorFit[size_t(i)]*scale*grow;if(size<.05f)continue;
            const V at=body.at+V{0,-1.25f,0}+right*(local.x*scale)+V{0,local.y*scale,0}+f*(local.z*scale);
            DrawBattleProp(ctx,b.weapon.items[size_t(i)].visual,at-V{0,size*.5f,0},size,std::atan2(f.x,f.z)+life_.age*.6f*(i%2?-1.f:1.f),0);}}
    for(const auto& s:b.shots){
        if(s.style==1){
            // 飴玉 (スライム陣取りではアメのモデル。回りながら飛ぶ)
            if(territory){const auto& m=LowModelOf("candy");Engine::Transform t;const float scale=LowScaleFor("candy",s.radius*3.2f);t.scale={scale,scale,scale};t.rotate={s.age*9,s.age*6,0};
                t.translate=EV(s.at-V{0,(m.lo.y+m.hi.y)*.5f*scale,0});ctx.renderer->DrawMeshInstanced(m.mesh,lowPalette_,t,{1,1,1,1},"LowpolySurface",lowMaps_);}
            else orb(s.at,s.radius,{1,.55f,.8f,1});}
        else if(s.style==2)stub(s.at,s.radius,{.45f,.8f,1,1});
        else if(s.mode!=BattleAction::Throw){
            // 飛び道具: 進む向きを向いて飛ぶ。十円玉と輪とボールは回る
            // (十円玉とトランプは寝かせたモデルなので、起こして面を見せる。鉛筆はモデルの長い向き (x) を進む向きへ向ける)
            float yaw=std::atan2(s.velocity.x,s.velocity.z),tilt=0;
            if(s.mode==BattleAction::Coin||s.mode==BattleAction::Disc||s.mode==BattleAction::Bounce)yaw+=s.spin;
            if(s.mode==BattleAction::Coin||s.mode==BattleAction::Fan||s.mode==BattleAction::Sticky)tilt=1.2f;
            if(s.mode==BattleAction::Bounce)tilt=s.spin*.7f;
            if(s.mode==BattleAction::Pierce)yaw-=1.5707963f;
            if(cityVisuals_.count(s.visual))DrawBattleProp(ctx,s.visual,s.at-V{0,s.size*.5f,0},s.size,yaw,tilt);else stub(s.at,s.radius,ActionColor(s.mode));}
        else if(!DrawCityPropInTentacle(ctx,s.visual,s.at,1.f))stub(s.at,s.radius,{.95f,.62f,.2f,1});}
    for(const auto& d:b.drops){
        // 吸い込まれている途中は体の中心へ飛びながら小さくなる。消える前の 3 秒は点滅する
        V at=d.at;float shrink=d.shrink;
        if(d.suck>=0){const float t=std::clamp(d.suck/BattleDropSuck,0.f,1.f),e=t*t;at=Lerp(d.suckFrom,body.at+V{0,DevourBodyRadius(body.mass)*.3f,0},e);shrink*=1-.7f*e;}
        else if(d.age>BattleDropLife-3&&std::fmod(d.age*8,2.f)<1)continue;
        else at.y+=std::sin(d.age*4+d.spin)*d.radius*.15f;
        if(!DrawCityPropInTentacle(ctx,d.visual,at,shrink))stub(at,d.radius*(shrink/std::max(.001f,d.shrink)),ActionColor(d.action));}
    // 置いた物
    for(const auto& pl:b.placed){
        switch(pl.kind){
        case BattleAction::Marbles:{if(!cityVisuals_.count(pl.visual))stub(pl.at,pl.size,{.55f,.85f,1,1});else DrawBattleProp(ctx,pl.visual,pl.at-V{0,pl.size*.5f,0},pl.size*1.4f,pl.age*3,0);break;}
        case BattleAction::Tower:{
            // 横に 5 列・縦に 3 段の積み木 (崩れる最後の 0.3 秒は前へ倒れる)
            const V side{pl.dir.z,0,-pl.dir.x};const float yaw=std::atan2(pl.dir.x,pl.dir.z),fall=std::clamp((pl.age-(pl.life-.3f))/.3f,0.f,1.f);
            const float cube=pl.size,rise=std::min(1.f,pl.age/.25f);
            for(int row=0;row<3;++row)for(int col=0;col<5;++col){V at=pl.at+side*(pl.radius*(float(col)/2.f-1))+V{0,cube*1.1f*float(row)*rise,0}+pl.dir*(cube*1.4f*float(row)*fall);
                if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,at,cube,yaw+.2f*float((row+col)%3-1),fall*1.2f);else stub(at+V{0,cube*.5f,0},cube*.5f,{.95f,.75f,.3f,1});}
            break;}
        case BattleAction::Domino:{
            const float fall=std::clamp((pl.age-(BattleDominoStart+BattleDominoStep*float(pl.index)))/.15f,0.f,1.f);
            const float rise=std::min(1.f,pl.age/.15f);if(rise<1&&pl.index>int(pl.age/.02f))break;
            if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,pl.at,pl.size,std::atan2(pl.dir.x,pl.dir.z),1.45f*fall*fall);else stub(pl.at+V{0,pl.size*.5f,0},pl.size*.4f,{.95f,.95f,.9f,1});
            break;}
        case BattleAction::Paint:{
            // 塗った床: 平たくつぶした球 (最後の 1 秒で小さくなる)。スライム陣取りでは体液の水たまりが床そのものなので描かない
            if(!b.legacy&&b.challenge==ChallengeKind::Territory)break;
            const float fade=std::clamp(pl.life-pl.age,0.f,1.f),grow=std::min(1.f,pl.age/.2f);Engine::Transform t;t.translate=EV(pl.at+V{0,.15f,0});
            const float r2=pl.radius*grow*(.6f+.4f*fade);t.scale={r2,.12f,r2};ctx.renderer->DrawMeshInstanced(sphere_,white_,t,pl.color);break;}
        case BattleAction::Read:if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,pl.at,pl.size,0,0);break;
        case BattleAction::Beckon:if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,pl.at,pl.size*std::min(1.f,pl.age*4),3.14159f+std::sin(pl.age*12)*.3f,0);break;
        case BattleAction::Cage:if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,pl.at,pl.size,pl.age*.5f,3.14159f);else stub(pl.at+V{0,pl.size*.5f,0},pl.size*.5f,{.8f,.65f,.4f,1});break;
        case BattleAction::Smell:{
            // においの雲: 地面に広がる緑がかった雲と、ゆらゆら立ちのぼる粒
            const float fade=std::clamp(pl.life-pl.age,0.f,1.f),grow=std::min(1.f,pl.age/.3f);Engine::Transform t;t.translate=EV(pl.at+V{0,.2f,0});
            // (スライム陣取りでは地面の雲は描かず、立ちのぼる粒だけ。床は体液の色のまま見せる)
            const float r2=pl.radius*grow*(.6f+.4f*fade);t.scale={r2,.15f,r2};if(b.legacy||b.challenge!=ChallengeKind::Territory)ctx.renderer->DrawMeshInstanced(sphere_,white_,t,{.65f,.75f,.45f,1});
            for(int k=0;k<6;++k){const float a=float(k)*1.05f+pl.age*.6f,h=std::fmod(pl.age*.6f+float(k)*.17f,1.f);orb(pl.at+V{std::cos(a)*pl.radius*.5f,h*pl.radius*.5f,std::sin(a)*pl.radius*.5f},pl.radius*.08f*(1-h),{.7f,.8f,.5f,1});}
            break;}
        case BattleAction::Decoy:{
            // ぬいぐるみ: 跳ねて敵を呼ぶ。弾ける直前はふくらむ
            const float hop=std::abs(std::sin(pl.age*6))*pl.size*.25f,swell=1+.4f*std::clamp((pl.age-(pl.life-.6f))/.6f,0.f,1.f);
            if(cityVisuals_.count(pl.visual))DrawBattleProp(ctx,pl.visual,pl.at+V{0,hop,0},pl.size*swell*1.4f,pl.age*2,0);else orb(pl.at+V{0,pl.size*.6f+hop,0},pl.size*.6f*swell,{.85f,.65f,.45f,1});break;}
        default:break;}}
    // ティッシュの雲: 体のまわりをふわふわ回る白い粒
    if(b.fluff>0){const float R=DevourBodyRadius(body.mass);for(int k=0;k<8;++k){const float a=float(k)*.785f+life_.age*1.5f;orb(body.at+V{std::cos(a)*R*1.3f,R*(.3f+.3f*std::sin(life_.age*3+float(k))),std::sin(a)*R*1.3f},R*.25f,{1,1,1,1});}}
    for(const auto& f:b.fx){const float t=f.age/f.life;Engine::Vector4 c=f.color;c.w*=1-t;stub(f.at,f.radius*(.6f+.6f*t),c);}
}

// 戦闘の HUD (チリツモの巻尺のデザイン。Docs/ui_designs/design1_tape.png):
//  - 下: 巻尺の「おなかメーター」。1 目盛りが物 1 回分で、種類の色が並ぶ。左の端が限界、赤い線が今の体。上の細い帯が HP
//  - 体のまわり: 足元の巻尺の輪 (残りの分が種類の色、使った分は灰色、次に使う分は赤い点線) と、頭の上の「あと N」の札
//  - 右下: スロットのステッカー (図鑑の絵・残りの数)。左上: ボスまでの時間とランク、小ささと効き目
// 残りの物を 1 回分ずつ並べる: スロットの物 (スロットの順) のあとに、袋の物を種類の順で。種類はステッカー・床のゲージ・巻尺の色になる
// (種類: 0 投げる・飛び道具 / 1 変形 / 2 置く / 3 止める / 4 回復・守る)。nextCell は次に使う 1 回分
const Engine::Vector4 BattleGroupColor[]={{.94f,.47f,.16f,1},{.43f,.35f,.78f,1},{.24f,.55f,.86f,1},{.51f,.51f,.59f,1},{.35f,.71f,.31f,1}};
void ChronoSystem::BattleHudCells(std::vector<int>& cells,int& nextCell)const{
    const auto& b=battle_;cells.clear();nextCell=-1;
    auto group=[](BattleAction a){
        switch(a){case BattleAction::Marbles:case BattleAction::Tower:case BattleAction::Paint:case BattleAction::Decoy:case BattleAction::Smell:return 2;
            case BattleAction::Pause:case BattleAction::Read:case BattleAction::Cage:case BattleAction::Sticky:case BattleAction::Tea:return 3;
            case BattleAction::Guard:case BattleAction::Heal:case BattleAction::Beckon:case BattleAction::Brew:case BattleAction::Fluff:return 4;
            case BattleAction::Throw:case BattleAction::Water:case BattleAction::Coin:case BattleAction::Disc:case BattleAction::Bounce:case BattleAction::Homing:case BattleAction::Fan:case BattleAction::Pierce:return 0;
            default:return 1;}};
    // (変形武器: 武器ごとの残りの回数。色は土台の技)
    if(WeaponsActive())return;
    for(int i=0;i<b.slotCount;++i){const auto& slot=b.slots[size_t(i)];for(size_t k=0;k<slot.items.size();++k){if(i==b.selected&&k+1==slot.items.size())nextCell=int(cells.size());cells.push_back(group(slot.action));}}
    std::vector<int> rest;for(const auto& bag:b.bag)for(size_t k=0;k<bag.items.size();++k)rest.push_back(group(bag.action));std::stable_sort(rest.begin(),rest.end());cells.insert(cells.end(),rest.begin(),rest.end());
}

// 足元の床に描く円いゲージ (地面に貼った板なので、上を通るスライムや敵の下に隠れる)。
// 内側 R*1.3〜外側 R*1.8 の帯を、物 1 回分ずつの区切りで色分けする。スライムの正面から始まり、体と一緒に回る
void ChronoSystem::DrawBattleFloorGauge(GameContext& ctx){
    const auto& b=battle_;const auto& body=life_.bodies[0];if(plane_==0||b.phase==BattleStep::None||WeaponsActive()||b.phase==BattleStep::Craft)return;
    std::vector<int> cells;int nextCell=-1;BattleHudCells(cells,nextCell);
    const int left=int(cells.size()),total=std::max(10,std::max(b.hudTotal,int(cells.size())));
    // 大きさは見えている体の幅から決める (DevourBodyRadius より体の見た目の方が広い。止まって地面にいるときの形で測ると、動いても輪が揺れない)
    LifeBody rest=body;rest.aspect=1;rest.air=0;const V radii=LifeRadii(rest,life_.recipe);
    const float R=std::max(radii.x,radii.z)*1.15f,inner=R*1.12f,outer=R*1.5f,mid=(inner+outer)*.5f,lift=.02f+R*.01f;V center=body.at;center.y=InkGround(center);
    V facing=body.forward;facing.y=0;const float front=Length(facing)>.01f?std::atan2(facing.z,facing.x):1.5707963f;
    const int steps=72;const float da=6.2831853f/float(steps);
    // 板 (plane_ は 2x2 の両面の XZ 板)。angle は板の x を向ける向き (xz 平面の角度)、len と wide は長さと幅
    auto quad=[&](V at,float angle,float len,float wide,float y,Engine::Vector4 color){Engine::Transform t;t.translate=EV(V{at.x,center.y+y,at.z});
        t.rotate=EV(V{0,-angle,0});t.scale={len*.5f,1,wide*.5f};
        // UI の色は sRGB で書いてあるので、3D の色 (リニア) に直してから描く (そのままだと白っぽくなる)
        const Engine::Vector4 linear{std::pow(color.x,2.2f),std::pow(color.y,2.2f),std::pow(color.z,2.2f),color.w};ctx.renderer->DrawMeshInstanced(plane_,white_,t,linear);};
    // 使った分は消す: 帯は正面から「残りの数 / いちばん多かった数」の角度までだけ描く (減るほど輪が欠けていく)
    if(left<=0)return;
    const float pad=R*.06f,blink=std::fmod(life_.age*4,1.f),reach=6.2831853f*float(left)/float(total);
    // 1 区間 (k 番目) のうち残りの範囲に入る部分を描く。最後の区間は途中で切る
    auto arc=[&](int k,float grow,float wide,float y,Engine::Vector4 c){const float a0=da*float(k),a1=std::min(da*float(k+1),reach);if(a1<=a0)return;
        const float half=(a1-a0)*.5f,a=front+a0+half;const V d{std::cos(a),0,std::sin(a)};quad(center+d*mid,a+1.5707963f,2*(outer+grow)*std::sin(half)*1.04f,wide,y,c);};
    // 黒い下地 (縁取り) と、両端のふた
    for(int k=0;k<steps;++k)arc(k,pad,outer-inner+pad*2,lift,UI::Scale::Black);
    for(float a:{front,front+reach}){const V d{std::cos(a),0,std::sin(a)};quad(center+d*mid,a,outer-inner+pad*2,R*.1f,lift,UI::Scale::Black);}
    // 色の帯: 1 区間ごとに、その位置の 1 回分の色 (次に使う分は赤く点滅)
    for(int k=0;k<steps;++k){const int cell=std::min(left-1,int((float(k)+.5f)/float(steps)*float(total)));
        Engine::Vector4 c=BattleGroupColor[cells[size_t(cell)]];if(cell==nextCell&&blink<.6f)c=UI::Scale::Red;arc(k,0,outer-inner,lift*2,c);}
    // 目盛り: 区切りの線 (内から外へ)。24 本くらいまで。正面 (0) は太い
    const int ringStep=std::max(1,(total+23)/24);
    for(int cell=0;cell<left;cell+=ringStep){const float a=front+6.2831853f*float(cell)/float(total);const V d{std::cos(a),0,std::sin(a)};
        quad(center+d*mid,a,outer-inner,cell==0?R*.09f:R*.04f,lift*3,UI::Scale::Black);}
}

void ChronoSystem::DrawDevourBattleUI(GameContext& ctx){
    using namespace UI::Scale;
    // 変形武器を作る画面
    if(battle_.phase==BattleStep::Craft){DrawWeaponCraft(ctx);return;}
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);auto& b=battle_;const auto& info=ChallengeOf(b.challenge);
    const Engine::Vector4 cream{.99f,.97f,.925f,1},used{.9f,.84f,.59f,1},gray{.6f,.6f,.6f,1};
    std::vector<int> cells;int nextCell=-1;BattleHudCells(cells,nextCell);const auto* groupColor=BattleGroupColor;
    const int left=int(cells.size());b.hudTotal=std::max(b.hudTotal,int(cells.size()));const int total=std::max(10,b.hudTotal);
    // 足元の円いゲージは床に貼った板で描く (DrawBattleFloorGauge)。スライムが上を通ると隠れる
    // ---- 左上: チャレンジの名前と遊び方 (旧来の戦闘は倒した数) ----
    {const std::string name=b.legacy?std::string("たたかい"):info.name,sub=b.legacy?"倒した "+std::to_string(b.kills)+"   拾った "+std::to_string(b.pickups):info.goal,status=ChallengeStatus();
        const float x=24,top=24,w=std::max({ui.Width(name,30,Jp),ui.Width(sub,20,Jp),ui.Width(status,18,Jp)})+32,h=status.empty()?90.f:120.f;Plate(ui,{x,top,w,h},Yellow);
        ui.Text(name,x+16,top+4,30,Black,Jp);ui.Fill({x,top+50,w,3},Black);ui.Text(sub,x+16,top+58,20,Black,Jp);if(!status.empty())ui.Text(status,x+16,top+88,18,Black,Jp);
        std::string buffs;if(b.guard>0)buffs+="守り "+std::to_string(int(std::ceil(b.guard)))+"秒  ";if(b.regen>0)buffs+="回復中 "+std::to_string(int(std::ceil(b.regen)))+"秒  ";
        if(b.fluff>0)buffs+="ふわふわ "+std::to_string(int(std::ceil(b.fluff)))+"秒  ";if(b.dropBoost>0)buffs+="落としやすい "+std::to_string(int(std::ceil(b.dropBoost)))+"秒";
        if(b.snack>0)buffs+="おやつパワー "+std::to_string(int(std::ceil(b.snack)))+"秒  ";if(b.heavy>0)buffs+="重い体 "+std::to_string(int(std::ceil(b.heavy)))+"秒  ";if(b.timeStop>0)buffs+="時計停止中  ";
        if(!buffs.empty()){const float bw=ui.Width(buffs,18,Jp)+24;Plate(ui,{x,top+h+16,bw,32},cream,3,4);ui.Text(buffs,x+12,top+h+19,18,Black,Jp);}}
    // ---- 上の真ん中: スコアとコンボ、自己ベスト ----
    if(!b.legacy){const std::string value=std::to_string(b.score);const float vw=ui.Width(value,52,Num),lw=ui.Width("スコア",22,Jp),w=std::max(220.f,vw+lw+48),x=640-w*.5f,top=24;
        Plate(ui,{x,top,w,70},Yellow);ui.Text("スコア",x+16,top+24,22,Black,Jp);ui.Text(value,x+w-16-vw,top+35-.56f*52,52,Black,Num);
        const bool beat=b.best>0&&b.score>b.best;const std::string best=beat?std::string("自己ベスト更新中！"):b.best>0?"自己ベスト "+std::to_string(b.best):std::string("初挑戦");
        const float bw=ui.Width(best,18,Jp)+24;Plate(ui,{640-bw*.5f,top+84,bw,30},beat?Red:cream,3,4);ui.Text(best,640-bw*.5f+12,top+86,18,beat?White:Black,Jp);
        if(b.combo>1){char text[32];snprintf(text,sizeof(text),"%dコンボ ×%.1f",b.combo,ChallengeComboScale(b.combo));const float kick=std::clamp((b.comboClock-(ChallengeComboWindow-.2f))/.2f,0.f,1.f),size=26*(1+.25f*kick);
            const float cw=ui.Width(text,size,Jp)+28;Plate(ui,{x+w+16,top+12,cw,size*1.6f},Red,3,4);ui.Text(text,x+w+30,top+12+size*.15f,size,White,Jp);}}
    // ---- 上の真ん中 (旧来の戦闘): ボスの HP ----
    for(const auto& e:b.enemies)if(e.boss){const UI::Rect frame{440,24,400,30};Plate(ui,frame,Black,3,4);
        ui.Fill({frame.x+3,frame.y+3,(frame.w-6)*std::clamp(e.hp/e.maxHp,0.f,1.f),frame.h-6},{.62f,.3f,.85f,1});ui.Text("ボス",frame.x+10,frame.y+2,22,White,Jp);}
    // ---- 右上: 残り時間 (残り 10 秒で赤く脈打つ) ----
    if(!b.legacy){const float leftTime=std::max(0.f,b.time-b.clock);const bool hurry=leftTime<=10&&b.phase==BattleStep::Fight;const float beat=hurry?.5f+.5f*std::sin(life_.age*10):0,size=64*(1+.06f*beat);
        const std::string clock=Clock(leftTime),label="のこり";const float clockW=ui.Width(clock,64,Num),labelW=ui.Width(label,22,Jp),w=clockW+labelW+46,x=1280-24-w;const auto ink=hurry?White:Black;
        Plate(ui,{x,24,w,70},hurry?Red:Yellow);ui.Text(label,x+16,24+22,22,ink,Jp);ui.Text(clock,x+labelW+28,24+35-.56f*size,size,ink,Num);}
    // ---- 下: 巻尺の「持ち物」メーター (残りの物 1 回分ずつ。変形武器は回数の制限がないので出さない) ----
    if(!WeaponsActive()){const float x0=150,x1=1100,y0=628,h=46,cell=(x1-x0)/float(total);
        Plate(ui,{x0,y0,x1-x0,h},Yellow,3,5);
        const float readX=x0+cell*float(left);ui.Fill({readX,y0,x1-readX,h},used);
        for(int k=0;k<left;++k)ui.Fill({x0+cell*float(k)+std::min(3.f,cell*.15f),y0+26,std::max(1.f,cell-std::min(6.f,cell*.3f)),16},groupColor[cells[size_t(k)]]);
        if(nextCell>=0&&nextCell<left){const float nx=x0+cell*float(nextCell);ui.Fill({nx,y0+23,cell,3},Red);ui.Fill({nx,y0+43,cell,3},Red);ui.Fill({nx,y0+23,3,23},Red);ui.Fill({nx+cell-3,y0+23,3,23},Red);}
        // 目盛り: 物が多くて 1 目盛りが細いときは、5 回分・25 回分ごとにまとめる (数字は太い目盛りに)
        const int step=cell>=8?1:cell>=2?5:25,every=step*5;
        for(int k=0;k<=total;k+=step){const bool major=k%every==0;const float tx=x0+cell*float(k);ui.Fill({tx-(major?1.5f:1.f),y0,major?3.f:2.f,major?22.f:13.f},Black);
            if(major&&k>0&&k<total)ui.Text(std::to_string(k),tx+4,y0-2,18,Black,Num);}
        // 左の端: 「持ち物」の札
        Plate(ui,{x0-104,y0,92,h},Black,3,4);ui.Text("持ち物",x0-94,y0+10,22,Yellow,Jp);
        // 赤い読み取り線と「あと N 回」の札 (札は巻尺の右の端。真ん中のスロットと重ならない)
        ui.Fill({readX-2.5f,y0-36,5,h+40},Red);
        {const std::string value=std::to_string(left);const float w=ui.Width("あと",20,Jp)+ui.Width(value,36,Num)+ui.Width("回",20,Jp)+16+24,px=x1+12;
            Plate(ui,{px,y0,w,h},Red,3,4);
            float tx=px+12;ui.Text("あと",tx,y0+12,20,White,Jp);tx+=ui.Width("あと",20,Jp)+6;ui.Text(value,tx,y0-1,36,White,Num);tx+=ui.Width(value,36,Num)+6;ui.Text("回",tx,y0+12,20,White,Jp);}
        // HP (旧来の戦闘だけ): 巻尺の上の細い帯
        if(b.legacy){const float share=std::clamp(b.hp/std::max(1.f,b.maxHp),0.f,1.f);
            ui.Fill({x0,y0-20,x1-x0,12},Black);ui.Fill({x0+2,y0-18,(x1-x0-4)*share,8},b.hurtFlash>0?White:share<.3f?Red:Engine::Vector4{.5f,.76f,.2f,1});
            ui.Text("HP "+std::to_string(int(std::ceil(b.hp))),x0,y0-48,24,Black,Num);}}
    // ---- 下の真ん中 (巻尺の上): スロットのステッカー (選んでいるのは黄色)。飴玉は最後 ----
    if(WeaponsActive())DrawWeaponStickers(ctx);
    else{const int boxes=b.slotCount+1;const float size=60,gap=72,x0=640-gap*float(boxes-1)*.5f,cy=548;
        static const float tilt[]={5,-6,4,-3,6};
        std::vector<const BattleBag*> spinning;for(const auto& x:b.bag)if(!x.items.empty())spinning.push_back(&x);
        for(int i=0;i<boxes;++i){const bool candy=i==b.slotCount,selected=i==b.selected;const float cx=x0+gap*float(i),rot=tilt[i%5]*.0174533f;
            const UI::Rect outer{cx-size*.5f,cy-size*.5f,size,size},inner{outer.x+5,outer.y+5,size-10,size-10};
            const BattleSlot* slot=candy?nullptr:&b.slots[size_t(i)];const bool faded=!candy&&slot->items.empty()&&slot->roulette<0;
            ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",{outer.x+3,outer.y+6,outer.w,outer.h},rot,{0,0,0,.45f});
            ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",outer,rot,Black);
            ui.ImageRot("Resources/Textures/UI/HUD/sticker.png",inner,rot,selected?Yellow:faded?Engine::Vector4{.8f,.8f,.8f,1}:White);
            ui.Text(std::to_string(i+1),outer.x-6,outer.y-24,24,Black,Num);
            if(candy){ui.Dot(cx,cy,12,{1,.55f,.8f,1});ui.Text("∞",cx+size*.18f,cy+size*.18f,26,Black,Num);continue;}
            std::string image;if(slot->roulette>=0){if(!spinning.empty())image=ImageOfId(spinning[size_t(int(slot->roulette*18))%spinning.size()]->id);}else if(!slot->items.empty())image=ImageOfId(slot->id);
            if(!image.empty())ui.ImageRot(image,{inner.x+3,inner.y+3,inner.w-6,inner.h-6},rot,faded?Engine::Vector4{1,1,1,.4f}:Engine::Vector4{1,1,1,1});
            if(!slot->items.empty()){const std::string count="x"+std::to_string(slot->items.size());const float cw=ui.Width(count,22,Num)+10;
                ui.Fill({cx+size*.12f,cy+size*.22f,cw,26},Black);ui.Text(count,cx+size*.12f+5,cy+size*.22f-2,22,Yellow,Num);}
            else if(slot->roulette<0)ui.Text("空",cx-8,cy-14,22,gray,Jp);}
        // 選んでいる物の名前と技
        if(b.selected<b.slotCount&&!b.slots[size_t(b.selected)].items.empty()){const auto& slot=b.slots[size_t(b.selected)];
            const std::string name=NameOfId(slot.id)+"  "+BattleActionName(slot.action);const float nw=ui.Width(name,20,Jp)+24;
            // 名前の札はスロットの左に並べる (上に置くと足元の輪とぶつかる)
            const float nx=x0-gap*.5f-12-nw;Plate(ui,{nx,cy-16,nw,32},cream,3,4);ui.Text(name,nx+12,cy-13,20,Black,Jp);}}
    // 得点や拾った物の文字 (その場所に浮かべる)
    for(const auto& pop:b.pops){float x,y;if(!GimmickScreen(ctx,pop.at,x,y))continue;const float a=1-pop.age;ui.Text(pop.text,x-ui.Width(pop.text,24,Jp)*.5f,y-40*pop.age,24,UI::WithAlpha(pop.color,a),Jp);}
    // 始まりの演出・知らせ・終わり (黄色の札に墨の文字)
    auto banner=[&](const std::string& text,float size,float y,Engine::Vector4 fill,Engine::Vector4 ink,float alpha){const float w=ui.Width(text,size,Jp)+40;
        ui.MultiplyAlpha(alpha);Plate(ui,{640-w*.5f,y,w,size*1.35f},fill,3,5);ui.Text(text,640-w*.5f+20,y+size*.08f,size,ink,Jp);ui.MultiplyAlpha(alpha>0?1/alpha:1);};
    if(b.phase==BattleStep::Intro){const float a=std::min(1.f,b.intro*3);
        if(b.legacy){banner("戦闘開始！",56,230,Yellow,Black,a);banner("吸収した物を使って生き残り、ボスを倒せ",24,320,cream,Black,a);}
        else{banner("チャレンジ開始！",30,190,Red,White,a);banner(info.name,56,240,Yellow,Black,a);banner(info.goal,24,330,cream,Black,a);
            banner("制限時間 "+Clock(b.time),22,382,cream,Black,a);}}
    else if(b.messageAge>0)banner(b.message,24,208,cream,Black,std::min(1.f,b.messageAge*2));
    if(b.phase==BattleStep::Over){
        if(b.legacy)banner(b.won?"クリア！":"敗北…",64,250,b.won?Yellow:Red,b.won?Black:White,1);
        else{banner("タイムアップ！",56,220,Yellow,Black,1);banner("スコア "+std::to_string(b.score),40,310,cream,Black,1);
            if(b.score>b.best&&b.best>0)banner("自己ベスト更新！",26,380,Red,White,1);}}
}

// 結果画面に重ねるチャレンジの記録と、チャレンジだけやり直す案内
void ChronoSystem::DrawDevourBattleRecord(GameContext& ctx){
    const auto& b=battle_;if(b.tries<=0||finishAge_<1.f)return;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);ui.MultiplyAlpha(std::min(1.f,(finishAge_-1)/.4f));
    const Engine::Vector4 hazard{.95f,.76f,.19f,1},white{.96f,.96f,.94f,1},red{.95f,.35f,.3f,1};
    const UI::Rect panel{830,450,420,170};ui.Fill(panel,{0,0,0,.6f});
    auto text=[&](const std::string& s,float x,float y,float size,Engine::Vector4 color){ui.Text(s,x,y,size,color,UI::JapaneseFont);};
    if(b.legacy){text(std::string("戦闘 ")+(b.won?"クリア (ボス撃破)":"敗北"),panel.x+16,panel.y+12,24,b.won?hazard:white);
        text("倒した数 "+std::to_string(b.kills)+"   残り HP "+std::to_string(int(std::ceil(b.hp))),panel.x+16,panel.y+50,20,white);}
    else{text(ChallengeOf(b.challenge).name,panel.x+16,panel.y+10,24,hazard);
        text("スコア "+std::to_string(b.score),panel.x+16,panel.y+44,28,white);
        if(b.newBest)text("自己ベスト更新！",panel.x+230,panel.y+50,20,red);
        else{const auto best=challengeBests_.find(ChallengeBestKey(CurrentDevourStage().id,b.challenge));if(best!=challengeBests_.end()&&best->second>0)text("自己ベスト "+std::to_string(best->second),panel.x+230,panel.y+50,20,white);}
        text("チャレンジの時間 "+UI::Scale::Clock(b.time)+"   使った物 "+std::to_string(b.spent)+" 回",panel.x+16,panel.y+84,18,white);}
    text(std::to_string(b.tries)+" 回目",panel.x+16,panel.y+112,18,white);
    text(b.legacy?"L / Bボタン: 戦闘だけやり直す":"L / Bボタン: チャレンジだけやり直す",panel.x+16,panel.y+136,20,hazard);
}

// ---- 変形チャレンジの成長の間の部分 ----
// このステージで挑むチャレンジを決める: --challenge=id で決められる。無ければランダム
void ChronoSystem::PickChallenge(){
    const int forced=ChallengeIndexOf(ArgText(L"--challenge="));
    if(forced>=0){challenge_=ChallengeKind(forced);return;}
    // タイトルの隠しコマンドのチャレンジ直行: 札で選んだチャレンジ
    if(QuickChallenge().active){challenge_=ChallengeKind(QuickChallenge().kind);return;}
    // (ランダムに選ぶのは、できあがっているチャレンジだけ)
    static std::mt19937 random{std::random_device{}()};std::vector<ChallengeKind> ready;for(int i=0;i<ChallengeKindCount;++i)if(ChallengeReady(ChallengeKind(i)))ready.push_back(ChallengeKind(i));
    challenge_=ready.empty()?ChallengeKind::Blocks:ready[size_t(std::uniform_int_distribution<int>(0,int(ready.size())-1)(random))];
}

// 成長の間: 長押しでチャレンジへ移る。残り ChallengeAutoAt 秒になったら自動で移る。移ったら true
// (チュートリアルの間は時計が止まっているので、自分でも移れない)
bool ChronoSystem::TickChallengeMove(Player& p,float dt,bool held){
    if(!devour_||finished_||!BattleEnabled()){challengeHold_=0;return false;}
    if(DevourTutorialHolding()){challengeHold_=0;return false;}
    challengeHold_=held?challengeHold_+dt:0.f;
    if(challengeHold_>=ChallengeMoveHold){challengeHold_=0;challengeMoved_=true;PlayDevourSound(devourSounds_.rankUp,.8f);FinishDevour(p);return true;}
    if(devourClock_.left<=ChallengeAutoAt){challengeHold_=0;FinishDevour(p);return true;}
    return false;
}

// 成長の間の HUD: 左上のステージの札の下に、このステージのチャレンジ・遊び方・おすすめの能力・移り方を出す
void ChronoSystem::DrawChallengeCard(GameContext& ctx){
    using namespace UI::Scale;
    if(!devour_||finished_||BattleActive()||!BattleEnabled()||DevourTutorialCutscene()||DevourShelfDive()>0)return;
    UI::Canvas ui(ctx.renderer,ctx.viewportSize.x,ctx.viewportSize.y);const auto& info=ChallengeOf(challenge_);
    // おすすめの能力の名前 (collection.json の action の名前から)
    std::string recommend;for(const auto& key:info.recommend){if(!recommend.empty())recommend+="・";recommend+=BattleActionName(BattleActionOf(key,""));}
    const bool tutorial=DevourTutorialHolding();const float untilAuto=std::max(0.f,devourClock_.left-ChallengeAutoAt);const bool soon=!tutorial&&untilAuto<=10;
    const std::string tag="チャレンジ",note="おすすめ  "+recommend;
    const float x=HudMargin,top=HudMargin+90+24,w=std::max({ui.Width(info.name,26,Jp)+ui.Width(tag,16,Jp)+44,ui.Width(info.goal,18,Jp),ui.Width(note,16,Jp),300.f})+32,h=tutorial?102.f:144.f;
    Plate(ui,{x,top,w,h},Cream,3,4);
    {const float tw=ui.Width(tag,16,Jp)+16;ui.Fill({x+12,top+10,tw,26},Red);ui.Text(tag,x+20,top+11,16,White,Jp);ui.Text(info.name,x+24+tw,top+4,26,Black,Jp);}
    ui.Text(info.goal,x+16,top+42,18,Black,Jp);
    ui.Text(note,x+16,top+70,16,{.35f,.35f,.35f,1},Jp);
    if(tutorial)return;
    // 移り方: 長押しのキーと、押している間に満ちるゲージ。自動で移るまでの時間 (残り 10 秒からは赤)
    ui.Fill({x,top+98,w,3},Black);
    const InputKey key{"keyboard_enter","xbox_button_y"};const float kx=KeyChip(ui,key,x+14,top+108,28,UI::UsingPad())+8;
    const std::string how="長押しでチャレンジへ";ui.Text(how,kx,top+111,18,Black,Jp);
    const float hold=std::clamp(challengeHold_/ChallengeMoveHold,0.f,1.f);if(hold>0){const float gx=kx+ui.Width(how,18,Jp)+10,gw=70;ui.Fill({gx,top+116,gw,12},Black);ui.Fill({gx+2,top+118,(gw-4)*hold,8},Red);}
    const std::string autoText=soon?"あと "+std::to_string(int(std::ceil(untilAuto)))+" 秒で移る":"のこり "+Clock(ChallengeAutoAt)+" で自動";
    ui.Text(autoText,x+w-16-ui.Width(autoText,16,Jp),top+113,16,soon?Red:Engine::Vector4{.35f,.35f,.35f,1},Jp);
}

// 確認用 (--devour-battle): 吸収を飛ばして、近くの物をいろいろ飲んだことにする
// (種類ごとに近い順に最大 6 個。ステージ全体から集めるので、全部の種類が袋に入る。道をふさぐ物・変形アイテム・建物は除く。デモで全部の能力を試せるよう大きさは問わない)
void ChronoSystem::PrepareBattleForTest(entt::registry& r){
    const auto& stage=CurrentDevourStage();auto& body=life_.bodies[0];
    // 確認用 (--challenge-item-census): ステージにある飲める物を図鑑の項目ごとに数え、能力と一緒に tests/out/item-census-<ステージ>.txt に書いて閉じる
    // (gate は飲める大きさ (cm): 覆える大きさの体の cm。小さい物ほど早く手に入る)
    if(HasArg(L"--challenge-item-census")){struct Count {int n=0;float minCm=1e9f;};std::map<std::string,Count> counts;
        for(const auto& prop:life_.props){if(prop.visual==0xffffffffu||prop.orb)continue;auto e=entt::entity(prop.visual);if(!r.valid(e))continue;const auto* name=r.try_get<NameComponent>(e);if(!name)continue;
            const std::string id=Collection::KeyOfName(name->name);if(id.empty())continue;auto& c=counts[id];++c.n;
            // 覆える大きさ: 物の半径が体の半径の 0.8 倍以下 (DevourCanCover)。体の半径 3*LifeScale から、その大きさになる cm を出す
            const float scale=prop.radius/(.8f*3.f);c.minCm=std::min(c.minCm,stage.start*std::max(1.f,scale));}
        std::filesystem::create_directories("tests/out");std::ofstream out("tests/out/item-census-"+stage.id+".txt");
        for(const auto& [id,c]:counts){bool building=false;const BattleAction a=ActionOfId(id,building);out<<id<<'\t'<<NameOfId(id)<<'\t'<<(building?"(building)":BattleActionName(a))<<'\t'<<c.n<<'\t'<<c.minCm<<'\n';}
        PostQuitMessage(0);}
    float cm=stage.goals[std::min<size_t>(1,stage.goals.size()-1)];
    if(const wchar_t* arg=wcsstr(GetCommandLineW(),L"--devour-battle-cm="))cm=std::max(stage.start,float(_wtof(arg+19)));
    const float mass=DevourGoalMass(stage,cm);
    std::vector<size_t> candidates;for(size_t i=0;i<life_.props.size();++i){const auto& prop=life_.props[i];
        if(!prop.active||prop.sealed||prop.orb||prop.morphItem!=LifeMorphItem::None||prop.visual==0xffffffffu)continue;
        if(std::find(devourWallProps_.begin(),devourWallProps_.end(),int(i))!=devourWallProps_.end()||int(i)==devourBarricadeProp_||int(i)==cityGateProp_)continue;
        candidates.push_back(i);}
    const V from=body.at;
    std::sort(candidates.begin(),candidates.end(),[&](size_t a,size_t c){return LifeDistance(life_.props[a].at,from)<LifeDistance(life_.props[c].at,from);});
    std::map<std::string,int> taken;std::vector<size_t> order;
    for(size_t i:candidates){if(order.size()>=300)break;auto e=entt::entity(life_.props[i].visual);if(!r.valid(e))continue;const auto* name=r.try_get<NameComponent>(e);if(!name)continue;
        const std::string id=Collection::KeyOfName(name->name);if(id.empty())continue;bool building=false;ActionOfId(id,building);if(building)continue;
        if(taken[id]>=6)continue;++taken[id];order.push_back(i);}
    for(size_t i:order){const auto& prop=life_.props[i];DevourRecordEaten(r,prop.visual);if(!devourEaten_.empty()&&devourEaten_.back().visual==prop.visual)devourEaten_.back().radius=prop.radius;
        DevourRemoveProp(r,i);}
    body.mass=mass;devourTutorial_.step=-1;
}

}
