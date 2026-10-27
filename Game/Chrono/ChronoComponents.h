// ChronoComponents.h
// ChronoSystem が使う ECS のコンポーネント (対象・弾・羽・生き物のボス・プレイヤー・入力の記録など)。
#pragma once
#include "ChronoRules.h"
#include "CreatureCombat.h"
#include "CreatureMotion.h"
#include "../../externals/entt/entt.hpp"
namespace Game::Chrono {
// 対象の種類と、プレイヤーの行動の状態
enum class Kind { Enemy, Projectile, Anchor, Weakpoint, Boss };
enum class Action { Free, Extending, Pulling, Retracting, Recovery, Dodge, Collapsed };
// 狙える対象 (敵・弾・足場・弱点・ボス)
struct Target {
    Kind kind=Kind::Enemy;
    // 当たり半径・体力・倒したときの報酬
    float radius=1, hp=1, reward=12;
    // 有効か・波の番号・位置のずれ
    bool active=true;
    int wave=-1;
    Vec offset{};
};
// 跳ねる敵 / 弾 / 羽 (始点から目的地へ飛ぶ)
struct Hopper { Vec velocity{}; float timer=0; bool grounded=false; };
struct Projectile { Vec velocity{}; float lifetime=18; bool breakable=true; };
struct Feather { Vec start{},destination{};float age=0,delay=0; bool readySound=false,fast=false; };
// 生き物のボスの段階 (蛇・集合・連射・扇状の羽・急降下・下降)
enum class CreatureStage { Snake, Assemble, Volley, Opening, Dive, Descend };
// 生き物のボスの状態 (段階・時間・攻撃・衝撃・移動・変形)
struct CreatureBoss { CreatureStage stage=CreatureStage::Snake;float age=0,timer=0,form=0; int sweepPhase=0; float sweepTime=0; Vec sweepCenter{}; bool sweepHit=false;
    float impactAge=10.f,impactPower=0;Vec impactPoint{};
    CreatureAttack attack=CreatureAttack::Sweep;int attackCount=0;float wingClock=0,recoil=0;Vec attention{};bool wingCounter=false,wingDone=false;
    Vec offset{0,0,65},moveVelocity{},cruiseGoal{},diveStart{};float roam=0,heading=0,flightAngle=0,volleyClock=0;int volleys=0,cruiseStep=0;bool cruiseReady=false;
    CreaturePose morphSource{};Vec morphDestination{};float morphHeading=0;bool morphActive=false;
};
// ボスの部位 (大きさ・位置・番号・役割)
struct CreaturePart { Vec unitScale{1,1,1},unitOffset{};int index=0,segment=-1;CreatureRole role=CreatureRole::Spine; };
// 蛇の中継 / 足場 / 固い物 / カメラを遮る物
struct SerpentRelay { Vec origin{}; float progress=0; };
struct Anchor { bool reusable=false; float cooldown=0; };
struct Solid {};
struct CameraOccluder { Vec center{},size{};float opacity=1; };
// チェインが失敗した理由
enum class ChainFailure { None, NoTarget, OutOfRange, Obstructed, Recovering, Unstable, Missed, Fell };
// 決まった再生や確認用のシーンで使う、任意の入力の元
struct ControlFrame {
    // 移動の入力
    Vec move{};
    // 攻撃・狙い・切り替え・ジャンプ・回避・ロックオン
    bool attack=false, aim=false, toggle=false, jump=false, dodge=false,lockOn=false;
    // アリーナ / 生き物 / 蛇のモードの切り替え
    bool arena=false;
    bool creature=false,snake=false;
    // 強化の選択・引き直し・見送り
    int upgradeChoice=-1;bool upgradeReroll=false,upgradeSkip=false;
    int attackPattern=-1; // 確認用の上書き。通常のプレイは状況で選ぶ
    bool cameraInput=false; // スクリプトによる視点の変更は、マウスの動きと同じ優先度
    // 取り込み・研究所・やり直し・スライムに戻る
    bool absorb=false,lab=false,restart=false,returnToSlime=false;
    // 変形の種類
    int morphForm=0;
    bool morphNow=false; // 数字キーと同じく、morphForm の形へすぐ変形する
    bool demoHit=false; // practice-field input for scripted checks
    bool showcase=false; // スクリプトの確認でも変形の見せ場を出す (通常の確認は自分のタイミングで進める)
    // カメラの向き
    float yaw=0, pitch=0;
};
// 旧モードのボスの状態
struct Boss {
    // 時間・段階・波・出した数・壊した数
    float clock=0; int phase=0, wave=0, emitted=0, waveBreaks=0;
    // 戦闘中か・反撃されたか
    bool engaged=false, countered=false;
    // 攻撃の向きと当たったか
    Vec attackDirection{0,0,-1};
    bool attackHit=false;
};
// プレイヤーの状態 (行動・速度・量・タイマー・カメラ・狙い・先行入力・流れ・成績)
struct Player {
    Action action=Action::Free;
    // 速度・手・射撃の向きと始点・終点・回避の向き
    Vec velocity{}, hand{}, shotDirection{}, shotStart{}, endpoint{}, dodgeDirection{};
    // 量・不安定さ・タイマー・先行入力・無敵・待ち時間・コヨーテ・ジャンプの先行入力
    float mass=100, instability=0, timer=0, buffer=0, invincible=0, cooldown=0, coyote=0, jumpBuffer=0;
    // 鎖の距離・上限・負担
    float shotDistance=0, shotLimit=0, pullCost=0;
    float collisionScale=1;
    float airHang=0;
    // 時間の倍率 (狙い・世界)
    float aimScale=1, worldScale=1;
    // ヒットストップ・着地・被弾・失敗の時間
    float hitStop=0, landingAge=1, landingStrength=0, damageAge=1, failureTime=0;
    float cameraOpacity=1;
    // チェインの勢いと、タイミングの良い入力
    float chainDrive=1,lastChainHit=10; bool timedInput=false,shotTimed=false;
    // 被弾の元と復帰位置
    Vec damageSource{}, recoveryPoint{0,3,-27};
    ChainFailure failure=ChainFailure::None;
    bool automatic=false, aiming=false, grounded=false, shotManual=false;
    // 狙い・照準の候補・遮られた候補・先行入力の対象
    entt::entity target=entt::null, preview=entt::null, blockedPreview=entt::null, bufferedTarget=entt::null;
    bool bufferedManual=false;
    float dodgeBuffer=0;
    bool holdCancelled=false;
    Vec bufferedDodge{};
    Vec pullWaypoint{};
    bool pullStep=false;
    const char* damageReason="HIT";
    // コンボの流れと成績
    Flow flow;
    Stats stats;
};
}
