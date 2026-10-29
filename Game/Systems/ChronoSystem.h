// ChronoSystem.h
// 現在のゲーム (スライム) の中心になるシステムの宣言。
// 本体は ChronoSystem.cpp と、機能ごとに分けた .inl (SlimeLife・CityStage・HomingDomains など) にある。
#pragma once
#include "ISystem.h"
#include "../Chrono/ChronoComponents.h"
#include "../Chrono/InkRules.h"
#include "../Chrono/LiquidTrail.h"
#include "../Chrono/DomainHighlight.h"
#include "../Chrono/CinematicRules.h"
#include "../Chrono/SlimeWeaponRules.h"
#include "../Chrono/SlimeHydrantRules.h"
#include "../Chrono/SlimeSignalRules.h"
#include "../Chrono/CityStageRules.h"
#include <map>
#include <vector>
namespace Game {
// シーン全体のまとめ役。キャラクターの残る状態はすべて ECS のコンポーネントにある
class ChronoSystem final : public ISystem {
    friend class SlimeDevTools; // エディタ専用の開発ツール (Game/Editor/SlimeDevTools.cpp) から中を触れるようにする
public:
    // クリア後も更新を続けるシステムとして作る
    explicit ChronoSystem(GameScene* scene):ISystem(true),scene_(scene){}
    // PLAY 開始時の初期化 / 毎フレームの更新 / 3D の描画 / UI の描画
    void Reset(entt::registry&) override;
    void Update(entt::registry&,GameContext&) override;
    void Draw(entt::registry&,GameContext&) override;
    void DrawUI(entt::registry&,GameContext&) override;
    // ゲームが終わったか / ムービー中か / スライムの研究所を開いているか
    bool Finished() const {return finished_;}
    bool CinematicActive() const {return cinematicActive_;}
    bool SlimeLabOpen() const {return slimeLife_&&life_.lab&&!finished_;}
    // スライムの状態・敵・街の門・消火栓の狙いを外 (エディタやテスト) から読むための関数
    const Chrono::SlimeLife& LifeState()const {return life_;}
    const std::vector<Chrono::SwarmEnemy>& LifeEnemies()const {return swarm_;}
    // 変形アイテムを初めて取ったときの見せ場 (カメラが正面へ寄り、周りの時間が止まる) の最中か
    bool MorphShowcaseActive()const {return showcase_.active;}
    // 手応えの設定 (ヒットストップ秒・揺れの秒・揺れの大きさ・揺れの速さ・振動の重い側・軽い側・振動の秒)
    struct LifeImpactSpec {float stop=0,shakeTime=0,shake=0,frequency=14,low=0,high=0,rumble=0;};
    // ステージ開始のカメラ演出の最中か
    bool StageIntroActive()const {return intro_.active;}
    float StageIntroAge()const {return intro_.age;}
    const Chrono::CityGates& CityGateState()const {return cityGates_;}
    const Chrono::HydrantAim& HydrantAimState()const {return hydrantAim_;}
    // 確認用: 区画の出口を手前から exits 個だけ壊したように開ける (1 路地の壁・2 バリケード・3 鉄の門)
    void OpenCityGates(entt::registry& r,int exits=3){
        if(exits>=1)for(size_t i=0;i<Chrono::CityWalls().size();++i)if(!cityGates_.WallOpen(i))BreakCityWall(i);
        if(exits>=2&&!cityGates_.barricadeOpen)BreakCityGate(r,true,false);
        if(exits>=3&&!cityGates_.gateOpen){cityGateUnlocked_=true;BreakCityGate(r,false,false);}}
    // スライムの研究所の画面を描く
    void DrawSlimeLab(entt::registry&,GameContext&);
    // 次の Update で作り直す (シーンを空にしたときも同じ)
    void Invalidate() {initialized_=false;}
    void Clear(entt::registry&) override { Invalidate(); }
private:
    // スライムのカメラの対象 (体の中心や大きさ)
    Chrono::LifeCameraSubject GetLifeCameraSubject()const;
    // インクのモード・スライムの生活モード・読み込み済みか
    bool inkMode_=true;
    bool slimeLife_=true,lifeLoaded_=false,slimePlayerDemo_=false;
    void BuildSlimePlayerDemo(entt::registry&);
    Chrono::Vec ConstrainLifeMovement(Chrono::Vec from,Chrono::Vec to,float radius)const;
    void UpdateSlimePlayerDemo(entt::registry&,Chrono::Player&,GameContext&);
    void DrawSlimePlayerDemoUI(GameContext&);
    // スライムの状態と、研究所のレシピ・素材・クリア数
    Chrono::SlimeLife life_;
    std::array<Chrono::SlimeRecipe,3> lifeRecipes_{};
    int lifeRecipeSlot_=0,lifeMaterials_=3,lifeClears_=0;
    // 各体の前回の行動と粒子数 (音や演出の切り替えに使う)
    std::array<int,Chrono::LifeBodyCount> lifeLastAction_{};
    std::array<uint32_t,Chrono::LifeBodyCount> lifeLastCounts_{};
    bool lifeWasLab_=false,lifeReseed_=true,lifePreviewEmitted_=false;
    // 章の見出しを出した段階と、出し始めた時刻 (life_.age)
    int lifeTitleStep_=-1;float lifeTitleStart_=0;
    // 信号機: 横断歩道 (中心・渡る向き・前方向・長さ・経過時間・ダメージの倍率・当てた人影と敵の組)・広がる輪・変形アイテムの足元と大きさ
    struct SignalWalk {Chrono::Vec center{},side{},forward{};float length=14,age=0,damage=1,size=1,dir=1;std::vector<uint64_t> hits;};
    struct SignalPulse {Chrono::Vec at{};float age=0,radius=10;Chrono::SignalColor color=Chrono::SignalColor::Red;};
    std::vector<SignalWalk> signalWalks_;std::vector<SignalPulse> signalPulses_;int signalWalkCount_=0; // 横断歩道を敷く向きを交互にする
    Chrono::Vec signalItemAt_{};static constexpr float SignalItemScale=1.4f;
    // 変形の見せ場: 形・経過時間・形が完成した時刻・寄る構図の中心と半径・戻す太陽の強さと被写界深度
    struct MorphShowcase {bool active=false,morphed=false;Chrono::LifeAction form=Chrono::LifeAction::Tire;float age=0,readyAt=-1,radius=1,sun=1;
        Chrono::Vec center{},facing{0,0,1};float dofFocus=30,dofRange=30,dofStrength=0;} showcase_;
    // ステージ開始のカメラ演出: 経過時間・スキップの長押しの時間・終わった後の暗転の残り
    struct StageIntro {bool active=false;float age=0,hold=0,fadeOut=0;} intro_;
    // ヒットストップの残り時間 (この間はスライムの世界を止め、カメラの揺れだけを進める)
    float lifeHitStop_=0;
    // 武器の形から吸収ボタンで戻ったとき、戻りきったら吸収を始めるための受付の残り時間
    float lifeAbsorbBuffer_=0;
    // 形が変わったときのカメラの構図のつなぎ (形の種類・進み・始めと今の体からの中心のずれと半径)、消火栓の肩越しの構図の割合
    struct LifeCameraBlend {int kind=-1;float age=1;Chrono::Vec fromOffset{};float fromRadius=1;Chrono::Vec offset{};float radius=1;} cameraBlend_;
    float cameraHydrant_=0;
    // スライムの生活 (SlimeLife.inl) の処理: 触手の描画・研究所・試練・作成・更新・描画・UI・吸収・被弾
    void DrawLifeTentacles(GameContext&,const Chrono::LifeBody&);
    void SetSlimeLabOpen(bool);
    void UpdateSlimeLab(GameContext&);
      void StartSlimeTrial(Chrono::SlimeTrial);
    // 分裂と飲み込みの音
    uint32_t lifeSplitSounds_[3]{0xffffffffu,0xffffffffu,0xffffffffu},lifeEngulfSound_=0xffffffffu;
    void BuildSlimeLife(entt::registry&);
    void UpdateSlimeLife(entt::registry&,Chrono::Player&,GameContext&);
    void DrawSlimeLife(entt::registry&,GameContext&);
    void DrawSlimeLifeUI(entt::registry&,GameContext&);
    void SyncSlimeBodies(GameContext&);
    void DamageSlimeLife(entt::registry&,Chrono::Player&,float,GameContext&,Chrono::Vec);
    void LifeHit(entt::registry&,Chrono::Player&,GameContext&,uint32_t,float,Chrono::LifeAction source=Chrono::LifeAction::Idle,Chrono::Vec from={});
    bool StartLifeAbsorb(int);
    void CityTireDashHits(entt::registry&,GameContext&,const Chrono::LifeBody&,uint64_t);
    void TickLifeChild(entt::registry&,Chrono::Player&,GameContext&,size_t,float);
    void DrawLifeChildMarkers(GameContext&);
    void DrawLifeEnemyMarkers(GameContext&);
    void StartMorphShowcase(entt::registry&,GameContext&,Chrono::LifeAction);
    void UpdateMorphShowcase(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawMorphShowcase(GameContext&);
    // ステージ開始のカメラ演出 (SlimeIntro.cpp) の更新・描画
    void UpdateStageIntro(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawStageIntro(GameContext&);
    // 手応え (ヒットストップ・カメラの揺れ・コントローラーの振動) を at で起こす。scale で揺れと振動の強さを変える
    void LifeImpact(GameContext&,const LifeImpactSpec&,Chrono::Vec at,Chrono::Vec direction={0,1,0},float scale=1);
    void FireSignal(entt::registry&,Chrono::Player&,GameContext&,Chrono::SignalColor);
    void UpdateSignalEffects(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawSignal(GameContext&);
    void DrawSignalUI(GameContext&);
    bool SaveSlimeLab();
    // 消火栓の水 (SlimeHydrant.inl): 水の発射と狙いの更新
    void UpdateHydrantWater(entt::registry&,Chrono::Player&,GameContext&,float,bool,bool);
    void UpdateHydrantAim(GameContext&);
    Chrono::HydrantAim hydrantAim_{};
    uint64_t hydrantWaterSerial_=0;
    // 街のステージ (CityStage.inl): 作った建物・CC0 の小物・門・役割ごとの敵
    // 街のモデル (メッシュ・テクスチャ・大きさ)
    struct CityModel {uint32_t mesh=0,texture=0;std::vector<uint32_t> extra;Chrono::Vec lo{},hi{};};
    // 街の小物の見た目
    struct CityPropVisual {uint32_t mesh=0,texture=0;std::vector<uint32_t> extra;Chrono::Vec scale{1,1,1},rotate{},center{};float height=1;std::string shader="EnvironmentSurface";};
    // 街の光る看板など (kind: 0 は光り続ける、1 は炎のように揺らぐ、2 は非常灯のように点滅する)
    struct CityGlow {Chrono::Vec center{},right{},up{},size{};Engine::Vector4 color{1,1,1,1};int kind=0;float phase=0;};
    std::vector<CityGlow> cityGlows_;
    // 街の小物 (触手で取り込める物)
    struct CityPropSeed {entt::entity entity=entt::null;Chrono::Vec at{};float required=0,reward=0;Chrono::LifeMorphItem morphItem=Chrono::LifeMorphItem::None;};
    // バリケードや塀の部品 (材質: 0 木・1 鉄・2 ブロック・3 金属)・敵の弾・壊れた破片・踏みつけの衝撃
    struct CityPlank {Chrono::Vec at{},size{},rotate{};int material=0;};
    struct CityShot {Chrono::Vec at{},velocity{};float age=0;bool shell=false;Chrono::Vec target{};};
    struct CityDebris {Chrono::Vec at{},velocity{},spin{},size{},rotate{};float age=0;int material=0;};
    struct CityStomp {Chrono::Vec at{};float age=0,radius=30;};
    // 小物の見た目・小物・バリケード・弾・破片・衝撃の一覧
    std::map<uint32_t,CityPropVisual> cityVisuals_;
    std::vector<CityPropSeed> cityProps_;
    std::vector<CityPlank> cityBarricade_;
    // 路地裏の壁 (部品・体力・揺れ・最後に当たった攻撃)
    struct CityWallState {std::vector<CityPlank> pieces;float health=0,shake=0;uint64_t hit=0;};
    std::vector<CityWallState> cityWalls_;
    // 火事場の炎 (残りの火の量。0 で消える)・炎に触れた時間
    struct CityFire {Chrono::Vec at{};float radius=12,health=0,hiss=0;};
    std::vector<CityFire> cityFires_;
    std::vector<CityShot> cityShots_;
    std::vector<CityDebris> cityDebris_;
    std::vector<CityStomp> cityStomps_;
    // 街の敵とバリケード・門のモデル
    CityModel cityRat_,cityCannon_,cityWood_,cityStoneSkin_,cityIron_,cityArmor_,cityBlock_;
    uint32_t cityCube_=0;
    // 門のエンティティと門の状態・体力・揺れ・ヒントの時間
    entt::entity cityGateEntity_=entt::null;
    Chrono::CityGates cityGates_{};
    float cityBarricadeHealth_=0,cityGateHealth_=0,cityBarricadeShake_=0,cityGateShake_=0,cityHintClock_=0;
    uint64_t cityBarricadeHit_=0,cityGateHit_=0;
    int cityGateProp_=-1,cityAnnounced_=-1;bool cityGateUnlocked_=false,cityHammerAnnounced_=false;
    // 街のステージの作成・敵の出現・門の破壊・武器の当たり・更新・描画
    void BuildCityStage(entt::registry&);
    void SpawnCityStep(int);
    int CityStepAlive()const;
    void BreakCityGate(entt::registry&,bool barricade,bool absorbed);
    void BreakCityWall(size_t);
    void ResetCityHazards();
    void CityWeaponHits(entt::registry&,GameContext&,const Chrono::LifeBody&,int pulse,uint64_t hitKey);
    void UpdateCityStage(entt::registry&,Chrono::Player&,GameContext&,float);
    void UpdateCityEnemies(entt::registry&,Chrono::Player&,GameContext&,float);
    void DrawCityStage(GameContext&);
    bool DrawCityPropInTentacle(GameContext&,uint32_t visual,Chrono::Vec at,float shrink);
    // インクの弾
    struct InkShot {Chrono::Vec at,velocity;float age=0;};
    std::vector<InkShot> inkShots_;
    // スライムが残した跡
    struct SlimeTrail {Chrono::Vec at;int surface;float mass,radius;std::array<float,32> footprint{};float originalMass=0;bool bonus=false;};
    std::vector<SlimeTrail> slimeTrails_;
    // 誘導の領域 (HomingDomains.inl): 描いた輪と、そこから出るミサイル
    struct HomingDomain {Chrono::DomainLoop loop;Chrono::LiquidTrail liquid;std::vector<std::array<Chrono::Vec,3>> highlight;float age=0,clock=0,afterglow=0,bulletPower=1,lifetime=1.2f;int remaining=0,launched=0,total=0;bool enclosesBoss=false,meteor=false;Chrono::DomainReachArea reach{};};
    struct DomainMissile {Chrono::Vec at{},velocity{};std::array<Chrono::Vec,8> tail{};float age=0,seed=0,tailClock=0,power=1;int tailCount=0,lane=1;entt::entity projectile=entt::null;uint32_t target=Chrono::SwarmNoTarget;bool enclosed=false,meteor=false;Chrono::DomainShape shape=Chrono::DomainShape::Loop;Chrono::Vec origin{};float reach=1e9f;};
    std::vector<HomingDomain> homingDomains_;
    std::vector<DomainMissile> domainMissiles_;
    // 群れの敵 (HordeCombat.inl / SwarmStates.inl) とその演出・メッシュ・出現の時間
    std::vector<Chrono::SwarmEnemy> swarm_;
    struct SwarmEffect {Chrono::Vec at{};float age=0,size=1;int kind=1;};
    std::vector<SwarmEffect> swarmEffects_;
    struct SwarmMesh {uint32_t handle=0;Chrono::Vec inverseExtent{1,1,1},center{};};
    std::array<SwarmMesh,3> swarmMeshes_{};
    uint32_t swarmQuad_=0,swarmNextId_=1;float swarmSpawnClock_=0,swarmSoundClock_=0,swarmAttackClock_=0;int swarmAttackBurst_=0;
    size_t swarmAttackCursor_=0;bool swarmRestPending_=false;
    Chrono::Vec swarmEmergenceAt_{};
    // 使い魔
    struct Familiar { Chrono::Vec at{};float age=0,clock=0,lifetime=12; };
    std::vector<Familiar> familiars_;
    // ローグライト (RogueliteCombat.inl): ダメージの時間・カメラ・被写界深度・流体の見た目
    float rogueDamageClock_=0;
    Chrono::Vec rogueCameraPosition_{},rogueCameraRotation_{};
    float rogueCameraFov_=1.13f,rogueDofFocus_=0,rogueDofRange_=0,rogueDofStrength_=0;
    float rogueFluidAspect_=1,rogueFluidMotion_=0;
    // ローグライトのメニュー・スキル・発動・完璧な輪
    bool UpdateRogueMenu(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateRogueSkills(entt::registry&,Chrono::Player&,GameContext&);
    void ActivateRogue(entt::registry&,Chrono::Player&,GameContext&,HomingDomain&);
    void PerfectRogueLoop(entt::registry&,Chrono::Player&,GameContext&);
    // 群れの作成・更新・描画・ボスの表示・被弾
    void BuildSwarm(entt::registry&);
    void UpdateSwarm(entt::registry&,Chrono::Player&,GameContext&);
    void DrawSwarm(entt::registry&,GameContext&);
    void SetSwarmBossVisible(entt::registry&,bool,float=0);
    void HitSwarm(entt::registry&,Chrono::Player&,GameContext&,uint32_t,float,bool=true);
    // reach が 1e8 未満なら、origin (発射した輪) から水平距離 reach 以内の敵だけを選ぶ
    uint32_t ChooseSwarmTarget(entt::registry&,Chrono::Vec,bool,Chrono::Vec origin={},float reach=1e9f);
    // 捕獲: 発射した輪の中の敵はすぐに溶ける
    int candidateCapture_=0,captureShown_=0;float captureScanClock_=0,captureAge_=10;
    // 描いている輪の液体・候補の液体と強調表示
    Chrono::LiquidTrail trailLiquid_,candidateLiquid_;
    std::vector<Chrono::Vec> highlightedCandidate_;
    std::vector<std::array<Chrono::Vec,3>> candidateHighlight_;
    // 領域の塗りのメッシュと、領域・ミサイルの更新
    uint32_t domainFillMesh_=0;
    void UpdateDomains(entt::registry&,Chrono::Player&,GameContext&);
    void HitDomainMissile(entt::registry&,Chrono::Player&,GameContext&,Chrono::Vec,bool,float);
    // インク (ChronoInk.inl): 面・メッシュ・テクスチャと作成・更新・描画・カメラ
    bool inkNeedsRebuild_=false;
    // スライムのビームと輪のメッシュ・音
    uint32_t slimeBeamMesh_=0,slimeRingMesh_=0;
    uint32_t slimePressureSound_=0xffffffff,slimeRecallSound_=0xffffffff,slimeAcidSound_=0xffffffff;
    void DrawSlimeBeam(entt::registry&,GameContext&);
    std::vector<Chrono::InkSurface> inkSurfaces_;
    std::vector<uint32_t> inkMeshes_;
    uint32_t inkTexture_=0;bool inkDirty_=true;float inkUploadClock_=0;
    void BuildInk(entt::registry&);
    void UpdateInk(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateInkBoss(entt::registry&,Chrono::Player&,GameContext&);
    void DrawInk(entt::registry&,GameContext&);
    void DrawInkUI(entt::registry&,GameContext&);
    void InkCamera(entt::registry&,Chrono::Player&,GameContext&);
    // ボスの被弾の反応と倒れる演出 (BossDefeat.inl)
    struct BossDebris {entt::entity part=entt::null;Chrono::Vec rest{},velocity{},spin{};DirectX::XMFLOAT4 color{};float clearance=.5f;};
    std::vector<BossDebris> bossDebris_;
    // ボスの光・倒れる演出の時間と状態
    float bossFlash_=0,defeatAge_=0,defeatPulse_=0;bool bossDefeat_=false,defeatBurst_=false;
    Chrono::Bounds defeatBounds_{};Chrono::Vec defeatOutward_{};Chrono::CinematicPose defeatReturn_{};
    void FlashBoss(entt::registry&);
    void StartBossDefeat(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateBossDefeat(entt::registry&,Chrono::Player&,GameContext&);
    void DrawBossDefeat(GameContext&);
    // ムービー (ChronoCinematic.inl) の更新・描画・状態・被写界深度
    bool UpdateCinematic(entt::registry&,Chrono::Player&,GameContext&);
    void DrawCinematic(GameContext&);
    bool openingPending_=true,cinematicActive_=false,cinematicBoss_=false;
    float cinematicAge_=0;
    float cinematicDofFocus_=0,cinematicDofRange_=0,cinematicDofStrength_=0;
    Chrono::CinematicPose cinematicReturn_{};
    Chrono::Bounds cinematicBossBounds_{};
    // インクの地面の高さ / インクのテクスチャをアップロードする
    float InkGround(Chrono::Vec,int* =nullptr)const;
    void UploadInk();
    using V=Chrono::Vec;
    // シーン・プレイヤー・ボス・弱点
    GameScene* scene_;
    entt::entity player_=entt::null,boss_=entt::null,weakpoint_=entt::null;
    // 当たり判定に使う箱 (地形と生き物)
    std::vector<Chrono::Bounds> solids_;
    std::vector<Chrono::Bounds> creatureSolids_;
    // 火花と水滴の演出
    struct Spark {V from,to; float age=0;};
    std::vector<Spark> sparks_;
    struct Droplet {V position,velocity;float age=0;bool dust=false;};
    std::vector<Droplet> droplets_;
    // カメラの追従・構図・ポスト効果の強さ・音・テクスチャ
    V cameraFollow_{},composition_{};
    bool cameraReady_=false;
    // 街のカメラ: 後ろが塞がれたときに持ち上げる量と、届く距離の割合 (なめらかに戻す)
    float cameraLift_=0,cameraReach_=1;
    float viewPitch_=0.12f,postStrength_=0;
    uint32_t hitSound_=0xffffffff,coreSound_=0xffffffff;
    uint32_t white_=0,sphere_=0;
    // 状態 (初期化済み・終了・勝利・前フレームの入力)
    bool initialized_=false,finished_=false,won_=false,prevL_=false,prevM_=false,prevSpace_=false,prevShift_=false;
    bool newTime_=false,newChain_=false;
    bool diagnostic_=false;
    // カメラ (向き・距離・肩越し・視野角・補助・揺れの設定)
    float yaw_=0,pitch_=0.16f,zoom_=17,finishAge_=0;
    float shoulder_=1.8f,chainCameraHold_=0,fov_=1.0472f;
    float manualCameraHold_=0,zoomVelocity_=0,cameraBoom_=-1;
    float cameraAssist_=0;int shakeSetting_=2;bool prevShake_=false;
    // アリーナ・生き物・蛇だけのモード
    bool arena_=false,prevArena_=false;
    bool creature_=false,prevCreature_=false,snakeOnly_=false,prevSnake_=false;
    // 最高記録 (時間とチェイン)
    float bestTime_=0; int bestChain_=0;
    // ステージの作成 (通常・アリーナ・生き物)
    void Build(entt::registry&);
    void BuildArena(entt::registry&);
    void BuildCreature(entt::registry&);
    // 生き物のボスの更新・姿勢・衝撃
    void UpdateCreature(entt::registry&,Chrono::Player&,GameContext&);
    void PoseCreature(entt::registry&);
    void BossImpact(entt::registry&,GameContext&,V,V,float);
    void DrawBossImpact(GameContext&,const Chrono::CreatureBoss&);
    // 対象へ届くか / メッシュや箱の作成 / 当たり判定の箱の記録
    bool Reachable(entt::registry&,const Chrono::Player&,entt::entity,V,V* =nullptr) const;
    entt::entity Mesh(entt::registry&,const std::string&,const std::string&,V,V);
    void Block(entt::registry&,const std::string&,V,V);
    void CacheSolids(entt::registry&);
    // 当たり判定 (移動の掃引・空いているか・壁に沿った移動・中心)
    bool Sweep(V,V,V,float&,V&) const;
    bool Clear(V,V,float=0) const;
    V Move(V,V,V,bool&,V* =nullptr) const;
    V Center(entt::registry&,entt::entity) const;
    // 攻撃の対象選び・射撃・命中・被弾・反応
    entt::entity Choose(entt::registry&,Chrono::Player&,GameContext&,bool);
    void Shoot(entt::registry&,Chrono::Player&,GameContext&);
    void Impact(entt::registry&,Chrono::Player&,entt::entity,GameContext&,bool);
    void Damage(entt::registry&,Chrono::Player&,float,GameContext&,V,float perfectWindow=.12f);
    void Feedback(Chrono::Player&,V,bool);
    // チェインの失敗・演出・プレイヤー・ワールド・ボス・カメラの更新と終了
    Chrono::ChainFailure Failure(entt::registry&,Chrono::Player&,GameContext&);
    void Presentation(entt::registry&,Chrono::Player&,GameContext&);
    void UpdatePlayer(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateWorld(entt::registry&,Chrono::Player&,GameContext&);
    void UpdateBoss(entt::registry&,Chrono::Player&,GameContext&);
    void Camera(entt::registry&,Chrono::Player&,GameContext&,float);
    void Finish(Chrono::Player&,bool);
};
}
