// DevourBattleRules.h
// 戦闘フェーズ (吸収のあと、吸収した物を使って 2 分耐える) の規則。エンジンに依存しない。
//  - 吸収した物は図鑑の分類 (collection.json の battle) で、投げる・轢く・叩く・守る・回復に分かれる
//  - 物を使うと減り、吸収したときに増えた量だけ体も縮む (下限は準備フェーズを始めたときの大きさ)
//  - 最大 HP と威力は今の大きさから決める。縮んだら今の HP も同じ割合で減らす
//  - 飴玉は吸収していなくても投げられる (弾数は無限・体は縮まない・弱い)。下限まで縮んだら飴玉しか投げられない
//  - 2 分耐えるか、最後に出るボスを倒したら勝ち。HP が 0 で負け
#pragma once
#include "DevourRules.h"
#include "DevourStageRules.h"
#include <algorithm>
#include <cmath>
#include <string>
namespace Game::Chrono {
// 使い道の種類 (Candy は吸収していなくても使える飴玉)
enum class BattleKind : uint8_t {Throw,Ride,Smash,Guard,Heal,Candy,Count};
inline constexpr int BattleKindCount=int(BattleKind::Count);
// 物の種類 (Candy 以外) の数
inline constexpr int BattleItemKinds=5;
inline const char* BattleKindName(BattleKind kind){static constexpr const char* names[]={"投げる","轢く","叩く","守る","回復","飴玉"};return names[std::min(int(kind),BattleKindCount-1)];}
// collection.json の battle の文字から種類を引く。building は戦闘で戻す建物 (使い道なし)
inline BattleKind BattleKindOf(const std::string& name,bool& building){
    building=name=="building";
    if(name=="ride")return BattleKind::Ride;if(name=="smash")return BattleKind::Smash;if(name=="guard")return BattleKind::Guard;if(name=="heal")return BattleKind::Heal;
    return BattleKind::Throw;
}

// ---- 物ごとの能力 (collection.json の action。省略すると battle の分類の動き) ----
// Throw 投げる・Ride 轢く (車)・Smash 叩く (ハンマー)・Guard 守る (盾)・Heal 回復 (ハート) は分類の動き。
// 残りは物ごとの能力: Heli 竹とんぼ・Water 水鉄砲・Roll 転がる (ボウリングの玉)・Spin こま・Sword 剣・Fly 飛行機・Spring ばね・Vacuum 掃除機・Rc ラジコン
// 将棋の駒は駒の動きの技: Fu 歩 (前を突く)・Kyo 香車 (まっすぐ突進)・Kei 桂馬 (斜め前へ跳んで着地で押しつぶす)・Gin 銀 (前と斜め 4 方向を突く)・
// Kin 金 (後ろの斜め以外の 6 方向を突く)・Kaku 角 (斜め 4 方向のビーム)・Hi 飛車 (十字 4 方向のビーム)・Ou 王 (まわり全部を吹き飛ばし、しばらく無敵)
// 置く・まく物: Marbles ビー玉をまく (踏んだ敵が転んで止まる)・Tower 積み木の塔 (敵を止め、最後に崩れて前へダメージ)・
// Domino ドミノ倒し (前へ一列に並べて順に倒す)・Paint 絵の具 (塗った床で敵は遅く、自分は速い)・Decoy ぬいぐるみ (敵を引きつけ、最後に弾ける)
// 飛び道具 (投げるけれど飛び方が違う): Coin 十円玉 (敵から敵へ跳ね移る)・Disc だるま落としの輪 (床を滑り、ステージの縁で跳ね返る)・
// Bounce ゴムボール (床と敵で何度も跳ねる)・Homing 紙飛行機 (近い敵を追う)・Fan トランプ (5 枚を扇に投げる)・Pierce 鉛筆 (敵を突き抜ける)
enum class BattleAction : uint8_t {Throw,Ride,Smash,Guard,Heal,Heli,Water,Roll,Spin,Sword,Fly,Spring,Vacuum,Rc,Fu,Kyo,Kei,Gin,Kin,Kaku,Hi,Ou,Marbles,Tower,Domino,Paint,Decoy,
    Coin,Disc,Bounce,Homing,Fan,Pierce,
    // 止める・弱らせる: Pause リモコン (敵が全部しばらく止まる)・Read まんが (まわりの敵が座り込んで読みふける)・Cage かご (前の敵 1 体を閉じ込める)・
    // Sticky ノート (紙を貼り付けて止める)・Erase 消しゴム (前をこすって弱らせる: 受けるダメージが増える)・Tea 湯のみ (熱いお茶をまいてやけど)
    // 自分を強くする: Roly だるま (起き上がりこぼし: 無敵で、殴った敵にはね返す)・Beckon 招き猫 (敵を招き寄せ、落とす物が 2 倍)・
    // Brew 急須 (少しずつ回復し続ける)・Fluff ティッシュ (ふわふわの雲で攻撃が当たりにくい)・Smell 靴下 (においの雲で敵が近づけない)
    Pause,Read,Cage,Sticky,Erase,Tea,Roly,Beckon,Brew,Fluff,Smell,Count};
inline const char* BattleActionName(BattleAction a){
    static constexpr const char* names[]={"投げる","轢く","叩く","守る","回復","回る翼","水鉄砲","転がる","こま","剣","飛ぶ","ばね","吸い込む","ラジコン","歩の突き","香車の突進","桂馬跳び","銀の突き","金の突き","角のビーム","飛車のビーム","王の威光","ばらまき","積み木の塔","ドミノ倒し","色ぬり","おとり","はじく","円盤","はねる","追いかける","扇投げ","突き抜ける","一時停止","読みふける","閉じ込める","貼り付ける","消して弱らせる","熱いお茶","起き上がり","招く","お茶で回復","ふわふわ","におい"};
    return names[std::min(int(a),int(BattleAction::Count)-1)];}
inline BattleAction BattleActionOf(const std::string& action,const std::string& category){
    static constexpr const char* keys[]={"throw","ride","smash","guard","heal","heli","water","roll","spin","sword","fly","spring","vacuum","rc","fu","kyo","kei","gin","kin","kaku","hi","ou","marbles","tower","domino","paint","decoy","coin","disc","bounce","homing","fan","pierce","pause","read","cage","sticky","erase","tea","roly","beckon","brew","fluff","smell"};
    for(int i=0;i<int(BattleAction::Count);++i)if(action==keys[i])return BattleAction(i);
    if(category=="ride")return BattleAction::Ride;if(category=="smash")return BattleAction::Smash;if(category=="guard")return BattleAction::Guard;if(category=="heal")return BattleAction::Heal;
    return BattleAction::Throw;}
// 投げる系 (体は液体のまま) か
// (置く・まく物も、体は液体のまま。ドミノだけはドミノの形になって倒れ込む)
inline bool BattleActionLiquid(BattleAction a){return a==BattleAction::Throw||a==BattleAction::Marbles||a==BattleAction::Tower||a==BattleAction::Paint||a==BattleAction::Decoy||(a>=BattleAction::Coin&&a!=BattleAction::Erase&&a!=BattleAction::Roly);}
// 能力の長さ (秒)・使った後の待ち時間
inline float BattleActionTime(BattleAction a){static constexpr float t[]={0,1.f,.74f,5,.9f,1.6f,1.4f,1.4f,2.f,.5f,1.1f,1.f,1.6f,1.8f,.4f,1.2f,.6f,.45f,.5f,.55f,.55f,3.f,.3f,.4f,1.1f,.5f,.4f,0,0,0,0,0,0,0,0,0,0,.6f,0,4.f,0,0,0,0};return t[std::min(int(a),int(BattleAction::Count)-1)];}
inline float BattleActionWait(BattleAction a){static constexpr float t[]={.35f,1.2f,.8f,.5f,.9f,1.7f,1.5f,1.5f,2.1f,.55f,1.2f,1.1f,1.7f,1.9f,.5f,1.4f,.9f,.8f,.9f,1.2f,1.2f,3.2f,.7f,1.2f,1.4f,1.6f,2.f,.45f,.8f,.6f,.7f,.7f,.5f,3.f,1.6f,1.2f,.6f,.8f,.9f,4.2f,2.f,1.5f,1.5f,1.8f};return t[std::min(int(a),int(BattleAction::Count)-1)];}
// 能力の強さ: 回る翼・こまの 1 回の当たり (0.2 秒ごと)・水の 1 粒・転がる/飛ぶ/ラジコンの当たり・剣・ばねの着地・掃除機の 1 回の当たり
inline constexpr float BattleHeliDamage=7,BattleSpinDamage=8,BattleWaterDamage=4,BattleRollDamage=12,BattleFlyDamage=18,BattleRcDamage=14,BattleSwordDamage=22,BattleSpringDamage=30,BattleVacuumDamage=4;
// 届く範囲 (体の半径に対する倍率)
inline constexpr float BattleHeliReach=2.2f,BattleSpinReach=2.f,BattleSwordReach=2.8f,BattleSpringReach=3.2f,BattleVacuumReach=4.5f;
// 突進の速さの倍率 (歩く速さに対して): 轢く・転がる・飛ぶ・ラジコン
inline float BattleDashBoost(BattleAction a){return a==BattleAction::Roll?2.2f:a==BattleAction::Fly?3.4f:a==BattleAction::Rc?3.f:a==BattleAction::Kyo?3.6f:a==BattleAction::Kei?4.2f:a==BattleAction::Fu?2.4f:2.6f;}
// 将棋の駒の技の強さ (1 回の当たり)・突きの長さ (体の半径に対する倍率)・突きとビームの太さ (体の半径に対する倍率)
inline constexpr float BattleFuDamage=32,BattleKyoDamage=24,BattleKeiDamage=32,BattleGinDamage=20,BattleKinDamage=18,BattleKakuDamage=24,BattleHiDamage=24,BattleOuDamage=38;
inline constexpr float BattleThrustLength=3.2f,BattleThrustWidth=.8f,BattleBeamWidth=.7f,BattleOuReach=4.f,BattleKeiReach=2.6f;
// ---- 置く・まく物 ----
// ビー玉: まく数・残る秒・転んで止まる秒・当たり
inline constexpr int BattleMarbleCount=9;inline constexpr float BattleMarbleLife=12,BattleMarbleStun=1.8f,BattleMarbleDamage=5;
// 積み木の塔: 置く距離と横幅 (体の半径に対する倍率)・残る秒・崩れたときの当たり
inline constexpr float BattleTowerAhead=2.6f,BattleTowerHalf=1.8f,BattleTowerLife=6,BattleTowerDamage=34;
// ドミノ: 並べる数・間隔 (体の半径に対する倍率)・倒れ始めるまでと 1 枚ごとの間・1 枚の当たり
inline constexpr int BattleDominoCount=9;inline constexpr float BattleDominoGap=1.15f,BattleDominoStart=.35f,BattleDominoStep=.13f,BattleDominoDamage=16;
// 絵の具: 塗る半径 (体の半径に対する倍率)・残る秒・敵の遅さ・自分の速さ
inline constexpr float BattlePaintRadius=5,BattlePaintLife=8,BattlePaintSlow=.4f,BattlePaintBoost=1.4f;
// ぬいぐるみ: 置く距離 (体の半径に対する倍率)・引きつける秒・弾ける範囲と当たり
inline constexpr float BattleDecoyAhead=3,BattleDecoyLife=5,BattleDecoyReach=3.6f,BattleDecoyDamage=32;
// ---- 飛び道具 ----
// 当たり・当たる回数 (十円玉は跳ね移る数、輪とボールは当たれる数)・残る秒
inline constexpr float BattleCoinDamage=13,BattleDiscDamage=11,BattleBounceDamage=10,BattleHomingDamage=28,BattleFanDamage=10,BattlePierceDamage=24;
inline constexpr int BattleCoinHops=5,BattleDiscHits=10,BattleBounceHits=7,BattleFanCards=5;
// ---- 止める・弱らせる・自分を強くする ----
// リモコン: 止める秒 (ボスは短い)・まんが: 届く範囲 (体の半径に対する倍率) と止める秒・かご: 届く範囲と閉じ込める秒・ノート: 止める秒
inline constexpr float BattlePauseTime=2.2f,BattlePauseBoss=.8f,BattleReadReach=4,BattleReadTime=3,BattleCageReach=6,BattleCageTime=6,BattleStickyTime=2.5f,BattleStickyDamage=6;
// 消しゴム: 届く範囲・弱らせる秒・受けるダメージの倍率・こすった当たり。湯のみ: 届く範囲・やけどの秒と 1 秒の当たり
inline constexpr float BattleEraseReach=2.6f,BattleWeakTime=6,BattleWeakTake=1.5f,BattleEraseDamage=6,BattleTeaReach=3.6f,BattleBurnTime=4,BattleBurnDps=7;
// だるま: はね返す当たり。招き猫: 招く範囲と、落とす物が増える秒。急須: 回復する秒と 1 秒に戻る割合。ティッシュ: 秒とかわす確率の足し。靴下: 雲の半径と秒
inline constexpr float BattleRolyReflect=16,BattleBeckonReach=9,BattleBeckonTime=8,BattleBrewTime=6,BattleBrewShare=.045f,BattleFluffTime=6,BattleFluffDodge=.5f,BattleSmellRadius=4,BattleSmellLife=7;
inline constexpr float BattleCoinLife=2.5f,BattleDiscLife=4.5f,BattleBounceLife=5,BattleHomingLife=4,BattlePierceLife=1.6f,BattleHomingTurn=5;

// ---- スロット ----
// 物のスロットの数 (ランク D=1・C=2・B=3・A=3・S=4)・1 つのスロットに入る数の上限・袋から引く重み (吸収した数の何乗か。数が多い物ばかり出ないよう 0.5)・ルーレットの秒
// 小さい物 (collection.json の bundle) は、bundle 個をまとめて 1 回分の「束」にして袋に入れる (引く重みも束の数で数える)
inline constexpr int BattleSlotMax=4,BattleSlotCapacity=3;
inline constexpr float BattleBagPower=.5f,BattleRouletteTime=.5f;
inline int BattleSlotsForRank(int rank){return rank<=0?1:rank==1?2:rank==2?3:rank==3?3:4;}

// 時間: ボスが出るまでの基準の長さ (ボスは残り 30 秒の時点 = 90 秒で出て、倒すまで続く)・始まりの演出
inline constexpr float BattleTime=120,BattleBossLead=30,BattleIntro=2.5f;
inline float BattleBossAt(float time){return std::max(time*.25f,time-BattleBossLead);}

// 大きさの比 (準備フェーズを始めたときの大きさを 1 とする)。下限の量は開始の量
inline float BattleSizeRatio(const DevourStage& stage,float mass){return std::max(1.f,DevourSizeCm(stage,mass)/std::max(.01f,stage.start));}
inline float BattleFloorMass(){return LifeStartMass;}
// 1 回使って縮む量の上限: 吸収で増えた量 (戦闘を始めたときの量 − 下限) に対する割合。
// 大きい物 (飲んだときに大きく育った物) を 1 つ使っただけで一気に縮まないようにする
inline constexpr float BattleShrinkCap=.08f;
inline float BattleShrinkAmount(float gain,float startMass){return std::min(gain,BattleShrinkCap*std::max(0.f,startMass-BattleFloorMass()));}
// 最大 HP: 大きさの比の平方根に比例 (開始の大きさで 100)
inline float BattleMaxHp(const DevourStage& stage,float mass){return 100*std::sqrt(BattleSizeRatio(stage,mass));}
// 威力の倍率: クリアの大きさ (最初の目標) で 1
inline float BattlePower(const DevourStage& stage,float mass){return std::clamp(std::sqrt(DevourSizeCm(stage,mass)/std::max(.01f,stage.goals.front())),.35f,4.f);}
// 使える物の種類の数: D=1・C=2・B=3・A と S は全部 (飴玉は数えない)
inline int BattleKindsAllowed(int rank){return rank<=0?1:rank==1?2:rank==2?3:BattleItemKinds;}

// 戦闘の大きさの基準 (unit): 戦闘を始めたときのスライムの体の半径。
// 敵・弾・ドロップ・速さ・戦うステージの広さは全部これに比例するので、どれだけ大きく育っても画面での見え方は同じになる
// (大きく育った強さは HP・使える物の数・ランクで出す)。BattleUnit はステージの最初の目標の大きさ (基準が決まる前の予備)
inline float BattleUnit(const DevourStage& stage){return DevourBodyRadius(DevourGoalMass(stage,stage.goals.front()));}
inline float BattleUnitAt(float startMass){return DevourBodyRadius(startMass);}
inline float BattleEnemyRadius(float unit,bool boss){return unit*(boss?1.3f:.45f);}
// 体の動く速さ (戦闘を始めたときより大きいほど少し速い)・敵の速さ
inline float BattleMoveSpeed(float unit,float radius){return unit*(3.5f+1.5f*std::clamp(std::sqrt(radius/std::max(.01f,unit)),.35f,4.f));}
inline float BattleEnemySpeed(float unit,bool boss){return unit*(boss?1.9f:2.6f);}
// 敵の体力 (時間がたつほど硬い)・ボスの体力・敵の攻撃
inline float BattleEnemyHp(float elapsed){return 20*(1+elapsed/60);}
inline constexpr float BattleBossHp=420,BattleEnemyDamage=7,BattleBossDamage=18,BattleEnemyHitCooldown=1;
// 出現の間隔 (始めは 1.6 秒、終わりは 0.5 秒)・同時に出る数の上限
inline float BattleSpawnInterval(float elapsed,float time){return 1.6f-1.1f*std::clamp(elapsed/std::max(1.f,time),0.f,1.f);}
inline constexpr int BattleEnemyLimit=28;
// 敵のドロップ: 倒したときに 1 個落とす確率 (招き猫の間は 2 倍)・残る秒・吸い寄せ始める距離 (体の半径に対する倍率)・
// 吸い込まれる秒・1 個で戻る量 (1 回使って縮む上限に対する割合)。体より大きい物でも、触れれば吸収できる
inline constexpr float BattleDropChance=.12f,BattleDropLife=18,BattleDropMagnet=2.6f,BattleDropSuck=.22f,BattleDropGain=.6f;
// 落とす物の上限: それまでに使った回数のこの割合まで (5 回使って 1 個戻るくらい)。使った物がドロップで大きく戻らないようにする
inline constexpr float BattleDropRefill=.2f;
// 専用の広いステージ: 戦う円の半径 (クリアの大きさの体の半径に対する倍率) と、マップの端からの離し方 (半径に対する倍率)
inline constexpr float BattleArenaUnits=45,BattleArenaGap=3;

// 攻撃の強さ (威力の倍率をかける前)
inline constexpr float BattleThrowDamage=14,BattleRideDamage=20,BattleSmashDamage=26,BattleCandyDamage=6;
// ---- 小さくなったときの特殊能力 ----
// 大きいと高火力・高耐久 (BattlePower・BattleMaxHp)、小さいと高速・高機動。体積を減らすことを戦術のひとつにする。
// 小ささ: 戦闘を始めたときの大きさで 0、下限 (準備フェーズを始めたときの大きさ) で 1
inline float BattleSmallness(float mass,float startMass){const float spare=startMass-BattleFloorMass();return spare>1e-3f?std::clamp(1-(mass-BattleFloorMass())/spare,0.f,1.f):1.f;}
// 移動の速さの倍率 (下限で 1.9 倍)・敵の攻撃をかわす確率 (下限で 55%)
inline float BattleSmallSpeed(float shrunk){return 1+.9f*shrunk;}
inline float BattleSmallDodge(float shrunk){return .55f*shrunk;}
// 飴玉: 待ち時間 (下限で 0.07 秒の連射)・1 回に撃つ数 (小ささ 0.4 で 2 発、0.75 で 3 発の扇形)・威力 (下限で 1.6 倍)
inline float BattleCandyWait(float shrunk){return .22f*(1-.7f*shrunk);}
inline int BattleCandyCount(float shrunk){return shrunk>=.75f?3:shrunk>=.4f?2:1;}
inline float BattleCandyPower(float shrunk){return 1+.6f*shrunk;}
// 守るの秒数と受けるダメージの割合・回復の量 (最大 HP に対する割合)
inline constexpr float BattleGuardTime=5,BattleGuardTake=.3f,BattleHealShare=.35f;
// 轢くの秒数と速さの倍率・叩くの届く距離 (体の半径に対する倍率)
inline constexpr float BattleRideTime=1.f,BattleRideBoost=2.6f,BattleSmashReach=2.4f;
// 叩く: ハンマーの形になって振りかぶる秒と、振り下ろす秒 (振り下ろした瞬間に当たる)
inline constexpr float BattleSmashWindup=.3f,BattleSmashSwing=.14f;
// 攻撃の待ち時間
inline float BattleCooldown(BattleKind kind){static constexpr float wait[]={.35f,1.2f,.8f,.5f,.9f,.22f};return wait[std::min(int(kind),BattleKindCount-1)];}
// 投げた物の大きさで威力を変える (戦闘の大きさの基準に対して)
inline float BattleItemScale(float unit,float radius){return std::clamp(std::sqrt(radius/std::max(.01f,unit*.5f)),.5f,2.f);}
}
