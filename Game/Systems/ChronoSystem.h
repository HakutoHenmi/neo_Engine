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
#include "../Chrono/DevourRules.h"
#include "../Chrono/SlopeTownRules.h"
#include "../Chrono/DevourStageRules.h"
#include "../Chrono/DevourBattleRules.h"
#include "../Chrono/ChallengeRules.h"
#include "../Chrono/WeaponCraftRules.h"
#include "../Chrono/MotifRules.h"
#include "../Chrono/DevourGimmickRules.h"
#include "../Chrono/DevourSpillRules.h"
#include <filesystem>
#include <functional>
#include <array>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>
namespace Game {
namespace UI {
class Canvas;
struct Rect;
namespace Scale {
struct Prompt;
}
} // namespace UI
namespace CityParts {
struct CityBuilder;
}
// シーン全体のまとめ役。キャラクターの残る状態はすべて ECS のコンポーネントにある
class ChronoSystem final : public ISystem {
	friend class SlimeDevTools;    // エディタ専用の開発ツール (Game/Editor/SlimeDevTools.cpp) から中を触れるようにする
	friend class DevourAnimEditor; // 吸収アニメーションのエディタ (Game/Editor/DevourAnimEditor.cpp)
public:
	// クリア後も更新を続けるシステムとして作る
	explicit ChronoSystem(GameScene* scene) : ISystem(true), scene_(scene) {}
	// PLAY 開始時の初期化 / 毎フレームの更新 / 3D の描画 / UI の描画
	void Reset(entt::registry&) override;
	void Update(entt::registry&, GameContext&) override;
	void Draw(entt::registry&, GameContext&) override;
	void DrawUI(entt::registry&, GameContext&) override;
	// ゲームが終わったか / ムービー中か / スライムの研究所を開いているか
	bool Finished() const {
		return finished_;
	}
	// 吸収モードの結果: カメラが寄る間に顔を向け、測り始めたら体を止める
	float DevourResultFace() const {
		return devour_ && finished_ ? std::min(1.f, resultCameraAge_ / .8f) : 0.f;
	}
	bool DevourResultFrozen() const {
		return devour_ && finished_ && resultCameraAge_ >= kResultFreezeAge;
	}
	// 検証の撮影用: 結果の「おなかの中」の升の数と、升を 1 つ大きく見せる
	size_t ShelfCellCountForTest() const {
		return shelf_.cells.size();
	}
	// 確認用: ステージの物を種類ごとに 1 つずつ飲みこんだことにする (結果の升に全部の種類を並べて見え方を確かめる)
	void RecordAllKindsForTest(const entt::registry& r);
	void OpenShelfDetailForTest(int cell) {
		if (cell >= 0 && cell < int(shelf_.cells.size()) && shelf_.detail != cell) {
			shelf_.detail = cell;
			shelf_.detailAt = finishAge_;
		}
	}
	// 鳴り続ける音を止める (シーンを抜けるとき)
	void StopLoopSounds();
	// スライムの状態・敵・街の門・消火栓の狙いを外 (エディタやテスト) から読むための関数
	const Chrono::SlimeLife& LifeState() const {
		return life_;
	}
	// 動く物の数 (道を往復する物・その場で動く物。確認用)
	// 坂の町のギミックの様子 (確認用): 種類 (6 こいのぼり・7 電線)・輪の中心・付いている物の見た目・その柱の生きている電線の数・使えなくなったか
	struct TownGimmickView {
		int kind = -1;
		Chrono::Vec at{};
		uint32_t anchor = 0xffffffffu;
		int edges = 0;
		bool done = false;
	};
	std::vector<TownGimmickView> TownGimmicks() const {
		std::vector<TownGimmickView> out;
		for (const auto& g : gimmicks_) {
			if (g.kind < Chrono::GimmickKind::Glide)
				continue;
			int alive = 0;
			if (g.wireNode >= 0)
				for (int e : wireNodes_[size_t(g.wireNode)].edges)
					alive += wireEdges_[size_t(e)].alive ? 1 : 0;
			out.push_back({int(g.kind), g.at, g.anchor, alive, g.done});
		}
		return out;
	}
	// 確認用: こまが跳ね返った回数・おみこしの子分の数 (取りに行く・持ち帰る)
	int GimmickRideBounces() const {
		return gimmickRide_.bounces;
	}
	size_t MikoshiHelperCount() const {
		return mikoshiHelpers_.size();
	}
	size_t HoseWashCount() const {
		return hose_.washes.size();
	}
	size_t HoseMistCount() const {
		return hose_.mist.size();
	}
	float HoseRainbow() const {
		return hose_.rainbow;
	}
	Chrono::Vec HoseImpact() const {
		return hose_.impact;
	}
	Chrono::Vec HoseNozzle() const {
		return hose_.nozzle;
	}
	bool HoseLob() const {
		return hose_.lob;
	}
	size_t HoseWaterCells() const {
		return hose_.water.size();
	}
	double HoseCpuMs() const {
		return hoseCpuMs_;
	}
	int TownWireEdges(bool aliveOnly) const {
		int n = 0;
		for (const auto& e : wireEdges_)
			n += (!aliveOnly || e.alive) ? 1 : 0;
		return n;
	}
	// 確認用: 体を遠くへ動かしたあと、液体の粒子を体の所に置き直す
	void RequestFluidReseed() {
		lifeReseed_ = true;
	}
	bool GimmickRidingNow() const {
		return gimmickRide_.index >= 0;
	}
	int GimmickRideShape() const {
		return gimmickRide_.shape;
	}
	// その物が道や輪をたどって動くか (確認用。visual は物の見た目のエンティティ)
	bool DevourRouteMover(uint32_t visual) const {
		for (const auto& m : devourMovers_)
			if (m.visual == visual && m.points.size() >= 2)
				return true;
		return false;
	}
	std::pair<size_t, size_t> DevourMoverCounts() const {
		size_t routes = 0, dancing = 0;
		for (const auto& m : devourMovers_)
			(m.points.size() >= 2 ? routes : dancing) += 1;
		return {routes, dancing};
	}
	// 移動を街の通れる範囲に収めた位置 (テストで通れるかを確かめる)
	Chrono::Vec MoveWithinCity(Chrono::Vec from, Chrono::Vec to, float radius) const {
		return ConstrainLifeMovement(from, to, radius);
	}
	const std::vector<Chrono::SwarmEnemy>& LifeEnemies() const {
		return swarm_;
	}
	// 吸収モードか・制限時間と大きさの段階 (テストから読み書きする)
	bool DevourMode() const {
		return devour_;
	}
	// 自動テスト用: このステージの変形アイテムを form にして、拾った状態にする (本編と同じく 1 つの形へ変形できる)
	void DevourTestForm(Chrono::LifeAction form) {
		devourTestForm_ = int(form);
		devourItemReady_ = true;
	}
	const std::filesystem::path& DevourLogPath() const {
		return devourLog_.path;
	}
	float DevourBiteAge() const {
		return devourBiteAge_;
	}
	int DevourRank() const {
		return devourRank_;
	}
	int DevourStageIndex() const {
		return devourStage_;
	}
	const Chrono::DevourStage& DevourStageInfo() const {
		return CurrentDevourStage();
	}
	bool DevourItemReady() const {
		return devourItemReady_;
	}
	void DevourSetMorphLeft(float seconds) {
		devourMorphLeft_ = seconds;
	}
	Chrono::DevourClock& DevourClockState() {
		return devourClock_;
	}
	// 手応えの設定 (ヒットストップ秒・揺れの秒・揺れの大きさ・揺れの速さ・振動の重い側・軽い側・振動の秒)
	struct LifeImpactSpec {
		float stop = 0, shakeTime = 0, shake = 0, frequency = 14, low = 0, high = 0, rumble = 0;
	};
	// スライムの研究所の画面を描く
	// 次の Update で作り直す (シーンを空にしたときも同じ)
	void Invalidate() {
		initialized_ = false;
	}
	void Clear(entt::registry&) override {
		Invalidate();
	}

private:
	// スライムのカメラの対象 (体の中心や大きさ)
	Chrono::LifeCameraSubject GetLifeCameraSubject() const;
	// 保存を読み込み済みか・デモのシーンか
	bool lifeLoaded_ = false, slimePlayerDemo_ = false;
	// 吸収モード (DevourRules.h): 敵なしで、制限時間内に全身で飛びかかって物を飲み込み、街を飲み込む。
	// デモ (スライム試験場) 以外は常にこのモード
	bool devour_ = false;
	Chrono::DevourClock devourClock_{};
	// 体の中に見えたまま溶けていく物 (小物の見た目・体の中心からのずれ・経過時間・体に収める縮小)
	// dir は体の中心から見た向き (飲んだ方向)、now は今の深さ (1 で表面)、lift は前の物の外へ積む量 (体の半径が単位)。古い順に並ぶ
	struct DevourMeal {
		uint32_t visual = 0xffffffffu;
		Chrono::Vec dir{}, tint{};
		float age = 0, fit = 1, radius = 1, now = 1, lift = 0;
		bool orb = false;
	};
	// ギミックの形になっている割合 (0 普段の体〜1 形)。体の中の物の皮を消し、物を小さく形の芯へ集める
	float devourMealMorph_ = 0;
	bool DevourMealsMorphed() const {
		return DevourToolShape() > 0;
	}
	// 形の中の物の位置と縮める倍率 (extent は倍率 1 のときの物の半分の幅)
	Chrono::Vec GimmickMealAt(const DevourMeal&) const;
	float GimmickMealFit(const DevourMeal&, float extent) const;
	std::vector<DevourMeal> devourMeals_;
	// 飲み込んで、まだ体へ移していない量
	float devourDigest_ = 0;
	// 最後の飲み込みからの時間とその重さ (体のつぶれとカメラの引きに使う)
	struct DevourGulp {
		float age = 1, weight = 0;
	} devourGulp_;
	// 自己ベストの量 (保存する)・今回で更新したか・最後の段階に届いたか
	bool devourNewBest_ = false;
	// 開いた輪・輪の壁のエンティティ・門の小物の番号と開ける輪・このフレームに開いた輪 (演出を出す)
	// 掃除機: 飛んでくる途中の物 (小物の番号・吸い始めた位置・経過時間)・次に吸い込むまでの時間・吸い込んでいるか
	// (触れて飲み込む物は体の中心へ、掃除機で吸う物は吸い口へ飛ぶ)
	struct DevourSuck {
		size_t index = 0;
		Chrono::Vec from{};
		float age = 0, flight = .3f;
		bool toBody = false;
	};
	std::vector<DevourSuck> devourSucks_;
	float devourSuckClock_ = 0;
	bool devourSucking_ = false;
	void DevourPull(entt::registry&, size_t index, float flight, bool toBody);
	void DevourOpenMouth(const Chrono::LifeCollectible&, bool turn);
	// 触れて飲むときの大口: 飛びかかりの形の時間 (負なら開けていない) と大きさの倍率
	float devourBiteAge_ = -1, devourBiteScale_ = 1;
	Chrono::Vec DevourMealAt(const DevourMeal&) const;
	Chrono::Vec DevourSuckAt(const DevourSuck&) const;
	// 大きい物を飲み込んでいる間の、足が重くなる残り時間と、大きさの文字が弾む残り
	float devourChew_ = 0, devourSizePulse_ = 0;
	// 弾かれて動けない残り時間・次に弾かれるまでの時間・大物を飲んで遅い残り時間
	float devourStun_ = 0, devourBumpCool_ = 0, devourHeavy_ = 0, devourBumpMemory_ = 0;
	// 動く物: 道をたどる物 (points が 2 点以上) と、その場で踊る物。index は life_.props の番号 (最初の更新で探す)
	// frontZ: 道をたどる物の正面が +z (猫など。ほかは +x の車や犬)
	struct DevourMover {
		uint32_t visual = 0;
		int index = -1;
		std::vector<Chrono::Vec> points;
		float speed = 0, phase = 0, age = 0, sway = 0, hop = 0, baseY = 0, baseYaw = 0;
		bool frontZ = false;
	};
	std::vector<DevourMover> devourMovers_;
	void AddRoute(entt::entity e, std::vector<Chrono::Vec> points, float speed, float phase, bool frontZ = false);
	void AddDance(entt::registry& r, entt::entity e, float speed, float sway, float hop);
	void TickDevourMovers(entt::registry&, float dt);
	// 漫画の描き文字: 飲み込んだ所や、ぶつかった所に出して飛び散らせる。kind は 0 飲む音・1 ぶつかる音・2 小さくなった印 (青い下矢印と暗い影)
	struct DevourPop {
		Chrono::Vec at{};
		std::string text;
		float age = 0, weight = 0;
		int kind = 0;
		float seed = 0;
	};
	// 描き文字の種類 (0 跳ねる・1 集中線つき・2 大きな衝撃) ごとの表示時間のテーブル
	static float DevourPopLife(int kind) {
		static constexpr float life[] = {.8f, .9f, 1.1f};
		return life[kind >= 0 && kind < 2 ? kind : 2];
	}
	// 体から飛び出して、弧を描いて落ちる物 (着地すると拾い直せる)
	struct DevourFly {
		size_t index = 0;
		Chrono::Vec from{}, to{}, last{}, rotate{};
		float age = 0, time = .7f, height = 0, spin = 0;
	};
	std::vector<DevourFly> devourFlies_;
	Chrono::Vec devourEatAt_{};
	std::vector<DevourPop> devourPops_;
	void DevourMealSkin(std::vector<Engine::Renderer::FluidTendril>&) const;
	// ---- 部屋のギミック (DevourGimmick.cpp): 床の輪の中で変形キーを押すと、体が道具の形になって決まった動きをする ----
	// parts は動かす見た目 (だるまの輪・ドミノ・ピン)、partProps はその物の番号、partRadius は止めている間の本当の大きさ。
	// rewards は成功したときにばらまく物 (最初は隠しておく)。link は液体の道の反対側の端
	struct DevourGimmick {
		Chrono::GimmickKind kind = Chrono::GimmickKind::Paint;
		Chrono::Vec at{};
		float radius = 16, minCm = 6;
		Chrono::Vec face{0, 0, 1};
		std::string name;
		std::vector<Chrono::Vec> path;
		std::vector<uint32_t> parts;
		std::vector<int> partProps;
		std::vector<float> partRadius, partAge;
		std::vector<Chrono::Vec> partBase, partRotate;
		std::vector<uint32_t> rewards;
		std::vector<int> rewardProps;
		Chrono::Vec burstAt{};
		uint32_t extra = 0xffffffffu;
		Chrono::Vec extraBase{}, extraRotate{};
		std::vector<Engine::Vector4> colors;
		std::vector<Chrono::Vec> spots;
		int link = -1, state = 0, hits = 0, pending = -1;
		float clock = 0, wobble = 0, scale = 1;
		bool done = false, reversed = false;
		uint32_t ring = 0xffffffffu, sign = 0xffffffffu;
		// 坂の町: anchor はそのギミックが付いている物 (電柱・こいのぼり) の見た目と物の番号。飲まれると使えなくなる。
		// quiet は作動範囲の膜を、範囲に入ったときだけ出す (電柱は数が多い)。clearArea が false なら範囲の中の物を押し出さない。wireNode は電線の柱の番号
		uint32_t anchor = 0xffffffffu;
		int anchorProp = -1, wireNode = -1;
		bool quiet = false, clearArea = true;
		// 使った後に休む残り時間 (おみこし・こま。0 になるまで乗れず、膜も出さない)
		float rest = 0;
		// 坂の町の膜の形: 縁の下の端を地面の高さに合わせた膜を、どの位置と半径で作ったか (違えば作り直す)。curtainSlot は gimmickCurtains_ の番号 (-1 は平らな共通の膜)
		Chrono::Vec curtainAt{0, -1e9f, 0};
		float curtainRadius = 0;
		int curtainSlot = -1;
	};
	std::vector<DevourGimmick> gimmicks_;
	// 坂の町の地面に合わせた膜 (ギミックの番号ごとに 1 つ。作り直しでも捨てずに中身だけ書き換える)
	std::vector<uint32_t> gimmickCurtains_;
	// 乗っている間の状態: どのギミックか・体の形・時間・道の上の距離・横のずれ・形の姿勢 (今と前のフレーム)
	// (坂の町: speed はこいのぼりの速さ、node と edge は電線のいる柱と走っている電線、edgeFrom はその電線のどちらの端から来たか)
	struct GimmickRide {
		int index = -1, shape = 0;
		float age = 0, s = 0, length = 0, lateral = 0, release = -1;
		Chrono::Vec anchor{}, heading{0, 0, 1}, from{};
		Chrono::GimmickPose pose{}, previous{};
		bool fired = false;
		float speed = 0, top = 0;
		int node = -1, edge = -1, edgeFrom = -1;
		Chrono::Vec look{0, 0, 1};
		// (おみこし・こま: bounces はこまが跳ね返った回数、beat はおみこしのわっしょいの数)
		int bounces = 0, beat = 0;
		// (消火栓: aim は地面の狙いの印)
		Chrono::Vec aim{};
	};
	GimmickRide gimmickRide_;
	int gimmickNear_ = -1;
	// 作動範囲の縁から立つ光の膜 (半径 1・高さ 1 の円筒の側面)
	uint32_t gimmickCurtain_ = 0;
	void DrawGimmickCurtains(GameContext&);
	uint32_t GimmickCurtainMesh(size_t index);
	bool GimmickCameraSubject(Chrono::LifeCameraSubject&) const;
	// ---- 最初のチュートリアル (DevourTutorial.cpp): ベッドの下で課題を 1 つずつ出す。step が 0 以上の間は制限時間を止める ----
	// step 0 動かし方と視点・1 巻尺の見方 (ゲームを止める)・2 段差の大きさまで育つ・3 段差の上り方
	struct DevourTutorial {
		int step = -1, page = 0;
		float age = 0, moved = 0, looked = 0, clear = -1, yaw = 0, pitch = 0;
		Chrono::Vec last{};
		bool started = false, previousAdvance = true;
	};
	DevourTutorial devourTutorial_;
	// ベッドの下を出た後: 4 目標の発表・5 ステージの紹介のカメラ・6 カウントダウン (3・2・1) (ここまで時間と操作を止める)・7 収納ケースの上からスタート
	bool DevourTutorialHolding() const {
		return devourTutorial_.step >= 0 && devourTutorial_.step < 7;
	}
	bool DevourTutorialGrowing() const {
		return devourTutorial_.step >= 0 && devourTutorial_.step < 4;
	}
	bool DevourTutorialCutscene() const {
		return devourTutorial_.step >= 4 && devourTutorial_.step <= 6;
	}
	// cinematics が false (スクリプトの確認) なら、目標の発表と紹介を飛ばしてそのままスタートする
	void StartDevourTutorial();
	void TickDevourTutorial(entt::registry&, float dt, bool cinematics);
	bool TickDevourTutorialPause(float dt);
	void DrawDevourTutorial(GameContext&);
	float DevourTutorialGoal() const;
	void PlaceOnStartCase(entt::registry&);
	void DevourTourCamera(GameContext&);
	void ClearGimmickAreas(entt::registry&);
	int GimmickAreaAt(Chrono::Vec at, float margin) const;
	// 水鉄砲の水: 銃口から当たる所まで伸びる液体の管
	struct GimmickJet {
		Chrono::Vec from{}, to{};
		float age = 0;
	};
	std::vector<GimmickJet> gimmickJets_;
	// 絵の具で塗った体の色 (w が 0 なら塗っていない) と、塗った瞬間の光
	Engine::Vector4 devourPaint_{0, 0, 0, 0};
	float devourPaintFlash_ = 0, gimmickMatchCool_ = 0;
	int AddGimmick(
	    entt::registry&, Chrono::GimmickKind kind, const std::string& name, Chrono::Vec at, float radius, float minCm,
	    float signYaw, Chrono::Vec signAt);
	void ResetDevourGimmicks(entt::registry&);
	void HoldGimmickProp(DevourGimmick&, size_t part, bool hold);
	void BurstGimmick(entt::registry&, GameContext&, DevourGimmick&, Chrono::Vec from, float spread);
	bool TickGimmickInput(entt::registry&, Chrono::Player&, GameContext&, bool pressed, bool cancel);
	Chrono::Vec GimmickStep(entt::registry&, Chrono::Player&, GameContext&, float dt, Chrono::Vec wish);
	void TickDevourGimmicks(entt::registry&, GameContext&, float dt);
	void EndGimmickRide(bool burst);
	bool GimmickRiding() const {
		return gimmickRide_.index >= 0;
	}
	// ギミックの種類ごとの動き (Strategy Pattern、DevourGimmickBehaviors.h / DevourGimmick.cpp)。
	// 乗り始め・乗っている間の動きと入力・仕掛けの動き・操作の札・構図・照準・物を置かない範囲を種類ごとのクラスに分ける
	struct GimmickRideStep;
	class GimmickBehavior;
	class ShootGimmick;
	class DominoGimmick;
	class BowlingGimmick;
	class PaintGimmick;
	class PathGimmick;
	class GlideGimmick;
	class WireGimmick;
	class MikoshiGimmick;
	class KomaGimmick;
	class HoseGimmick;
	static const GimmickBehavior& GimmickBehaviorFor(Chrono::GimmickKind kind);
	static const GimmickBehavior& GlideBehavior();
	static const GimmickBehavior& WireBehavior();
	static const GimmickBehavior& MikoshiBehavior();
	static const GimmickBehavior& KomaBehavior();
	static const GimmickBehavior& HoseBehavior();
	// ---- 坂の町のギミック (DevourTownGimmick.cpp): こいのぼりで滑空・電柱を登って電線をつたう ----
	// 電線の柱 (電柱の上の点・つながる電線) と電線 (両端の柱・見た目・使えるか)。電柱が飲まれると、その柱の電線は消える
	struct WireNode {
		Chrono::Vec top{};
		uint32_t visual = 0xffffffffu;
		std::vector<int> edges;
	};
	struct WireEdge {
		int a = -1, b = -1;
		uint32_t mesh = 0xffffffffu;
		bool alive = true;
	};
	std::vector<WireNode> wireNodes_;
	std::vector<WireEdge> wireEdges_;
	// 建物 (背の高い大きな物) の足元と高さ: こいのぼりがぶつかる (飲まれた物はぶつからない)
	struct TownSolid {
		float x0 = 0, z0 = 0, x1 = 0, z1 = 0, top = 0;
		uint32_t visual = 0xffffffffu;
		int prop = -1;
	};
	std::vector<TownSolid> townSolids_;
	// 町を作るときに呼ぶ: 電柱 (見た目・足元・道の番号と道の上の順番) をつないで電線を張り、柱ごとに登る輪を置く。こいのぼりごとに飛ぶ輪を置く
	struct TownPole {
		uint32_t visual = 0xffffffffu;
		Chrono::Vec at{};
		float height = 0;
		int road = -1, order = 0;
	};
	struct TownGlider {
		uint32_t visual = 0xffffffffu;
		Chrono::Vec at{};
		float height = 0;
	};
	void BuildTownGimmicks(entt::registry&, const std::vector<TownPole>& poles, const std::vector<TownGlider>& gliders);
	// おみこし (神社・お寺の前) とこま (町の広場) の輪を置く。場所は slope_town.json の rides
	void BuildTownRides(
	    entt::registry&, const std::vector<Chrono::Vec>& mikoshi, const std::vector<Chrono::Vec>& koma,
	    const std::vector<Chrono::Vec>& hydrants);
	// ---- 消火栓の放水 (DevourHose.cpp): 消火栓に変形して、画面の真ん中へ GPU 流体の水を撃つ。
	// 平らな所で当たった物はこちらへ飛んできて飲める。坂の下りへ撃つと、当たった物は坂を流れ落ちて下に溜まる。霧と虹が出る ----
	// 押し流している物 (物の番号・速さ・時間)・霧の粒 (位置・速さ・大きさ・時間・寿命・模様の種)
	struct HoseWash {
		int prop = -1;
		Chrono::Vec velocity{};
		float age = 0, roll = 0;
	};
	struct HoseMist {
		Chrono::Vec at{}, velocity{};
		float size = 1, age = 0, life = 1, seed = 0;
	};
	// 放水の状態: 撃っているか・ノズルと着弾点・水を出す端数・引き寄せる間隔・水を描く範囲 (流体の範囲に入れる球)・
	// 水の床を保つ残り時間 (地面に落ちた水が消えるまで)・虹の濃さ・音の間隔
	struct HoseState {
		bool spraying = false, washing = false;
		Chrono::Vec nozzle{}, impact{}, flow{}, plane{};
		float budget = 0, pullClock = 0, floorHold = 0, rainbow = 0, talk = 0, sound = 0;
		std::vector<Chrono::Vec> arc;
		std::vector<HoseWash> washes;
		std::vector<HoseMist> mist;
		// 扇の両端の水の道・扇の水が落ちる所 (左から右)・水がある所 (GPU から読み戻した水の粒の場所。流体を描く範囲に入れる)・
		// 地面の高さの表の中心と作り直す時間・高い弧で撃っているか
		std::vector<Chrono::Vec> arcLeft, arcRight, lanes, water;
		Chrono::Vec terrainCenter{};
		float terrainClock = 0;
		bool lob = false;
	};
	HoseState hose_;
	uint32_t rainbowMesh_ = 0;
	// 放水の CPU の時間の合計 (ミリ秒。重さの確認用)
	double hoseCpuMs_ = 0;
	// 乗っている間の放水 (狙い・水・物への効果)、毎フレームの流れ落ちる物と霧、描く
	void HoseSpray(entt::registry&, GameContext&, GimmickRide&, float dt);
	void TickHose(entt::registry&, float dt);
	void DrawHose(GameContext&);
	void ResetHose();
	void BuildHoseTerrain(Chrono::Vec center, bool force);
	// 消火栓の照準の先 (カメラの真ん中の視線が当たる地面か建物)
	Chrono::Vec HoseSight(GameContext& ctx, Chrono::Vec fallback);
	// おみこしの子分: 体から飛び出して小物を取りに行き (phase 0)、担いで戻る (phase 1)。戻ったら飲む
	struct MikoshiHelper {
		int prop = -1, phase = 0;
		Chrono::Vec at{}, from{};
		float age = 0, seed = 0;
	};
	std::vector<MikoshiHelper> mikoshiHelpers_;
	float mikoshiSend_ = 0, mikoshiTalk_ = 0;
	void TickMikoshiHelpers(entt::registry&, Chrono::Vec home, float dt);
	void DrawMikoshiHelpers(GameContext&);
	// 町の乗り物の当たり: from から to へ進むと町の端・高い段差・飲めない物にぶつかるか。ぶつかるなら跳ね返る面の向き (水平) を normal に書く
	bool TownRideHit(Chrono::Vec from, Chrono::Vec to, float radius, Chrono::Vec& normal) const;
	// 乗っているギミックの種類 (乗っていなければ -1)
	int GimmickRidingKind() const {
		return gimmickRide_.index >= 0 ? int(gimmicks_[size_t(gimmickRide_.index)].kind) : -1;
	}
	// 毎フレーム: 飲まれた電柱・こいのぼりのギミックを止め、その電線を消す
	void TickTownGimmicks(entt::registry&);
	// 高い所から落ちたときのヘリコプター (DevourHeli.cpp): 回転翼を回してゆっくり降りる
	struct DevourHeli {
		bool active = false;
		float age = 0;
		Chrono::GimmickPose pose{}, previous{};
	};
	DevourHeli devourHeli_;
	// 部屋のこぼれた液体の粒子 (DevourSpill.cpp)。visible は近くて動かし・描いているか
	struct DevourSpillFlow {
		struct Drop {
			Chrono::Vec at{}, velocity{};
			float radius = 1, age = 0, speed = 0, lane = 0;
			int phase = 0;
			uint32_t serial = 0;
		};
		Chrono::SpillKind kind = Chrono::SpillKind::Honey;
		Engine::Vector4 color{1, 1, 1, 1};
		Chrono::Vec source{}, edge{}, foot{}, pool{}, out{}, side{}, low{}, high{};
		float emit = 0;
		uint32_t serial = 0, seed = 1;
		bool visible = false, warmed = false;
		std::vector<Drop> drops;
	};
	std::vector<DevourSpillFlow> devourSpills_;
	void AddDevourSpill(
	    Chrono::SpillKind, Engine::Vector4 color, Chrono::Vec source, Chrono::Vec edge, Chrono::Vec foot,
	    Chrono::Vec pool);
	void TickDevourSpills(GameContext&, float dt);
	void DrawDevourSpills(GameContext&);
	// 大きい物を飲んで育ったときの水しぶき (DevourGrowth.cpp。体の流体とは別に、しずくはメッシュ・霧は四角で描く)。
	// kind は 0 しずく・1 細かい飛沫・2 地面のぬれ跡・3 霧。age が負の間はまだ出ていない。drag は空気の抵抗 (1 秒あたり)。
	// wait は飛び散るまでの残り (待っていなければ -1)
	struct GrowDrop {
		Chrono::Vec at{}, velocity{};
		float radius = .1f, age = 0, life = 1, ground = 0, gravity = 10, drag = 0, phase = 0;
		int kind = 0;
	};
	struct GrowSplash {
		std::vector<GrowDrop> drops;
		float wait = -1, cool = 0, size = 0;
	};
	GrowSplash devourGrow_;
	uint32_t growQuad_ = 0;
	void DevourGrowBurst(float size);
	void TickDevourGrowth(float dt);
	void DrawDevourGrowth(GameContext&);
	// スライムが通った所のぬれ跡 (DevourGrowth.cpp)。古い順に並び、時間がたつと薄れて古い順に消える
	struct WetMark {
		Chrono::Vec at{};
		float radius = 1, yaw = 0, seed = 0, age = 0;
	};
	std::vector<WetMark> wetMarks_;
	Chrono::Vec wetLast_{};
	bool wetHasLast_ = false;
	void TickDevourTrail(const Chrono::Player&, float dt);
	void DrawDevourTrail(GameContext&);
	void TickDevourHeli(Chrono::Player&, float dt, Chrono::Vec wish, bool blocked);
	// チャレンジの形 (変形武器・技の形) を見せるか: チャレンジの間と、積み木の山の結果の間 (結果でも変形したままでいる)
	bool BattleShapeShown() const {
		return devour_ && (battle_.phase != BattleStep::None || BlocksPileResult());
	}
	// 体が道具の形になっている (ギミックに乗っている・ヘリコプター)。形の番号 (無ければ 0)・形になってからの時間・姿勢
	// (変形武器を使っている間は、武器の形 (材料 3 つを組み合わせた形)。動き (姿勢) は今の技の形のものを使う: DevourPoseShape)
	int DevourToolShape() const {
		if (BattleShapeShown() && battle_.weaponShape > 0 && battle_.weaponForm > 0)
			return battle_.weaponShape;
		if (BattleShapeShown() && battle_.toolShape > 0)
			return battle_.toolShape;
		return devour_ && GimmickRiding() && gimmickRide_.shape > 0 ? gimmickRide_.shape
		       : devour_ && devourHeli_.active                      ? Chrono::GimmickHeliShape
		                                                            : 0;
	}
	// (変形武器の形でいる間は、その形になってからの秒。技の形の秒が 0 のままだと形へ寄せる強さが 0 で、形にならない)
	float DevourToolAge() const {
		if (BattleShapeShown() && battle_.weaponShape > 0 && battle_.weaponForm > 0)
			return battle_.weaponAge;
		if (BattleShapeShown() && battle_.toolShape > 0)
			return battle_.toolAge;
		return GimmickRiding() ? gimmickRide_.age : devourHeli_.age;
	}
	// (変形武器の形の間は、武器の形の動き (WeaponShapePose: 翼・羽根・足・反動・出てくるときの大きさ))
	int DevourPoseShape() const {
		return DevourToolShape();
	}
	const Chrono::GimmickPose& DevourToolPose(bool previous = false) const {
		if (BattleShapeShown() && battle_.weaponShape > 0 && battle_.weaponForm > 0)
			return previous ? battle_.weaponPosePrevious : battle_.weaponPose;
		if (BattleShapeShown() && battle_.toolShape > 0)
			return previous ? battle_.toolPrevious : battle_.toolPose;
		return GimmickRiding() ? (previous ? gimmickRide_.previous : gimmickRide_.pose)
		                       : (previous ? devourHeli_.previous : devourHeli_.pose);
	}
	void GimmickJets(std::vector<Engine::Renderer::FluidTendril>&) const;
	void DrawGimmickMarks(UI::Canvas&, GameContext&);
	// 乗っている間の操作 (下の操作の札) と、作動範囲の中で押せる操作 (体の上の吹き出し。無ければ false)
	void GimmickRidePrompts(std::vector<UI::Scale::Prompt>&) const;
	bool GimmickNearPrompt(UI::Scale::Prompt&) const;
	float GimmickFoodBonus(const Chrono::LifeCollectible&) const;
	Chrono::Vec DevourVacuumMouth() const;
	void TickDevourVacuum(entt::registry&, GameContext&, float dt, bool held);
	// バネ: 跳んでいる間の出発点・着地点・経過時間・全体の時間・高さと、次に跳べるまでの時間
	struct DevourHop {
		bool active = false, landed = false;
		Chrono::Vec from{}, to{};
		float age = 0, time = 1, height = 0;
		bool step = false;
	} devourHop_;
	float devourHopCooldown_ = 0, devourStepPush_ = 0;
	void TickDevourSpring(Chrono::Player&, float dt, bool held, Chrono::Vec wish);
	void StartDevourHop(float power, Chrono::Vec wish);
	// ステージの番号・読み込んだか・届いた目標の数・ステージごとの自己ベストの量
	// ---- 変形チャレンジ (DevourBattle.cpp): 吸収のあと、残り時間で吸収した物を使ってチャレンジに挑む。結果画面からチャレンジだけやり直せる ----
	// (Craft は変形武器を作る画面。チャレンジの始まりの演出の前)
	enum class BattleStep { None, Craft, Intro, Fight, Over };
	struct BattleItem {
		uint32_t visual = 0xffffffffu;
		std::string id;
		float gain = 0, radius = 1;
	};
	// stun は止まっている残りの秒 (ビー玉で転んだなど)
	// 置いた物 (ビー玉・積み木の塔・ドミノ・絵の具の床・ぬいぐるみ)。kind は能力、index はドミノの何枚目か
	struct BattlePlaced {
		Chrono::BattleAction kind = Chrono::BattleAction::Marbles;
		Chrono::Vec at{}, dir{}, velocity{};
		float age = 0, life = 1, radius = 1, size = 1, power = 1, itemRadius = 1;
		uint32_t visual = 0xffffffffu, target = 0;
		int index = 0;
		bool landed = false, done = false;
		Engine::Vector4 color{1, 1, 1, 1};
	};
	// 投げた物・飴玉・水の粒 (style 0 物・1 飴玉・2 水。水は描かない)
	// mode は飛び道具の飛び方 (Chrono::BattleAction の Coin〜Pierce。Throw ならふつうに投げる)。by は撃った能力 (衝撃の数え方)。hits は残りの当たれる数、hit は当てた相手 (同じ相手に続けて当てない)
	struct BattleShot {
		Chrono::Vec at{}, velocity{};
		float radius = 1, damage = 1, age = 0, push = 1, life = 3, size = 1, hitCool = 0, spin = 0;
		uint32_t visual = 0xffffffffu;
		int style = 0, hits = 1;
		Chrono::BattleAction mode = Chrono::BattleAction::Throw;
		Chrono::BattleAction by = Chrono::BattleAction::Throw;
		std::vector<uint32_t> hit;
		// 変形武器の弾 (WeaponCraft.cpp の TickFusedShot で進める): 性質のレベル・床を転がっているか・分かれたか・残りの跳ねる数と跳び移る数・
		// 吸い寄せの時計・威力の倍率・戻り始めたか
		bool fused = false, rolling = false, split = false, returning = false;
		Chrono::FuseLevels lv{};
		int bounces = 0, chain = 0;
		float pulse = 0, power = 1;
		// 撃ち方 (FusePattern)。衛星: 体のまわりの角度と半径。挟み撃ち: 曲がる速さ (ラジアン/秒)。
		// 槍はまっすぐ (重力なし)、降り注ぎは真下へ落ちる、罠は止まって相手を待つ
		Chrono::FusePattern pattern = Chrono::FusePattern::Single;
		float orbitAngle = 0, orbitRadius = 0, curve = 0;
		// 作る画面の見本の弾 (見本の窓に収まるよう縮めて飛ばし、見える大きさだけ戻してある)
		bool demo = false;
	};
	// 袋: 準備フェーズで吸収した物の種類ごとの山。absorbed は吸収した数 (引く重み)、items は残り
	struct BattleBag {
		std::string id;
		Chrono::BattleAction action = Chrono::BattleAction::Throw;
		int absorbed = 0;
		std::vector<BattleItem> items;
	};
	// 拾ったときに出す文字 (画面のその位置に浮かべる)
	struct BattlePop {
		Chrono::Vec at{};
		std::string text;
		float age = 0;
		Engine::Vector4 color{1, 1, 1, 1};
	};
	struct BattleFx {
		Chrono::Vec at{};
		float radius = 1, age = 0, life = .4f;
		Engine::Vector4 color{1, 1, 1, 1};
	};
	// 衝撃を受ける相手 (チャレンジの物)。id は相手ごとに違う番号、big は重い物、stopped は止まっている
	enum class BattleTargetKind : uint8_t { Block };
	// 積み木クラッシャーの積み木 (at は中心)。fragile はひびの残りの秒、painted は色ぬりされた、falling は下が無くなって落ちている、loose は吸い寄せられて滑っている
	struct ChallengeBlock {
		uint32_t id = 0;
		Chrono::Vec at{}, velocity{};
		float size = 1, hp = 1, maxHp = 1, fragile = 0, burn = 0, burnDps = 0, burnTick = 0, flash = 0, tint = 0;
		Chrono::BlockType type = Chrono::BlockType::Normal;
		bool alive = true, painted = false, falling = false, loose = false;
	};
	// 全部の能力 x チャレンジの効き方の実測 (--challenge-matrix。ChallengeMatrix.cpp)。trial は今の試し (場面 x (何もしない + 44 能力))
	struct MatrixResult {
		int scenario = 0, action = -1, score = 0, a = 0, b = 0, c = 0;
		float time = 0;
		bool self = false;
	};
	struct ChallengeMatrixState {
		bool active = false, used = false, walk = false;
		int trial = -1;
		float clock = 0;
		std::vector<MatrixResult> results;
	};
	// 壊れた積み木のかけら (飛んで落ちて消える)
	struct ChallengeDebris {
		Chrono::Vec at{}, velocity{}, spin{};
		float size = 1, age = 0, life = 1.2f;
		Engine::Vector4 color{1, 1, 1, 1};
	};
	struct BattleTarget {
		BattleTargetKind kind = BattleTargetKind::Block;
		uint32_t id = 0;
		Chrono::Vec at{};
		float radius = 1;
		bool big = false, stopped = false;
	};
	// 同じ衝撃 (source) が同じ相手に続けて当たらないための残りの秒
	struct BattleImpactMemo {
		uint32_t source = 0, target = 0;
		float left = 0;
	};
	// 変形武器 (WeaponCraft.cpp。規則は Chrono/WeaponCraftRules.h): 名前・材料 (枠の順の物と技)・入っている枠の数・形の番号・
	// 性質のレベル (3 つの物の性質の合計)・強化の点 (弾数・威力・連射)・次に撃てるまでの秒
	struct BattleWeapon {
		std::string name;
		std::array<std::string, Chrono::CraftParts> ids;
		std::array<Chrono::BattleAction, Chrono::CraftParts> actions{};
		std::array<BattleItem, Chrono::CraftParts> items;
		int filled = 0, shape = 0;
		Chrono::FuseLevels lv{};
		std::array<int, Chrono::CraftBoostCount> points{};
		// 強化の上限の点 (ステージで決まる)
		Chrono::CraftCaps caps = Chrono::CraftDefaultCaps;
		float cool = 0;
		// 連なり撃ち: 残りの数と次の 1 発までの秒。体当たりの次までの秒
		int burstLeft = 0;
		float burstClock = 0, bodyHitClock = 0;
		// 連打で出す撃ち方: 待ちの間に押した分を覚えている残りの秒
		float tapBuffer = 0;
	};
	// 作る画面で入れた材料: 袋の番号・強化 (負なら枠)
	struct CraftPick {
		int bag = -1, boost = -1;
	};
	// 作る画面: 材料 (袋を大きい順に並べた写し)・袋ごとの使った数・入れた材料 (入れた順)・今の強化と升・開いてからの秒・自動で決めるまでの秒 (負なら自動にしない)
	struct CraftState {
		std::vector<BattleBag> bags;
		std::vector<int> used;
		std::vector<CraftPick> picks;
		int boost = 0, cursor = 0;
		float age = 0, hold = -1;
		// 入れられなかったときの知らせ (最大になった強化など) と、出してからの秒
		std::string notice;
		float noticeAge = 99;
		// 段階: 0 材料を 3 つ選ぶ・1 残りの物を cm (大きさ) に変える演出・2 cm を強化に割り振る。段階に入ってからの秒
		int step = 0;
		float stepAge = 0;
		// cm (決定したときに残りの物を変えた量。1 点 = 1cm) と、強化ごとに入れた量
		int energy = 0;
		std::array<int, Chrono::CraftBoostCount> alloc{};
		// ＋− の長押し: 押している行と向き・押している秒・速く入れる分の貯め。レベルが上がった光の秒 (行ごと)
		int holdRow = -1, holdDir = 0;
		float holdTime = 0, holdCarry = 0;
		std::array<float, Chrono::CraftBoostCount> levelFlash{99, 99, 99};
		// cm を残したまま「挑戦」を押した (もう一度押すと始まる)
		bool warned = false;
		// 見本の撃ち方: 見本の窓のスライムが、今の材料と強化で撃って見せる弾 (チャレンジの弾とは別に持ち、何にも効かない)・
		// 分かれてできた弾・撃つ時計 (撃つ間と休む間をくり返す)
		std::vector<BattleShot> demoShots, demoNewShots;
		float demoClock = 0;
		// 見本を撃っている形 (材料が変わったら、出ている弾を消して最初から)
		int demoShape = 0;
		// 形の見本: 今の材料で作った形の番号 (実際のスライムをこの形にして、見本の窓に映す)
		int previewShape = 0;
		// 見本の回し方: 横の角度・縦の角度 (ラジアン)・ドラッグ中か・前のマウスの位置・最後に手で回してからの秒
		// (手で回していないときは、ゆっくり自動で回る)
		float previewYaw = 0, previewPitch = .5f, previewIdle = 99;
		bool previewDrag = false;
		std::array<float, 2> previewMouse{};
		// 見本のカメラの位置・向き (CraftPreviewCamera が置く。背景の箱をこの向きに合わせて描く)・スライムの中心と大きさ
		Chrono::Vec previewEye{}, previewView{0, 0, 1}, previewCenter{};
		float previewRadius = 1, previewFloor = 0;
	};
	struct DevourBattle {
		BattleStep phase = BattleStep::None;
		// 吸収が終わったときの体と袋 (やり直しはここから)・吸収のランク・物のスロットの数
		Chrono::Vec startAt{}, startForward{0, 0, 1};
		float startMass = 0, unit = 1;
		int rank = 0;
		std::vector<BattleBag> startBag;
		// 今の袋
		std::vector<BattleBag> bag;
		float finalCm = 0, clock = 0, intro = 0, overAge = 0, cooldown = 0, guard = 0;
		// チャレンジ: 種類・スコア・コンボ (続いている数と、切れるまでの秒)・このチャレンジの時間 (移ったときの残り)・自己ベスト (始めたとき) と更新したか
		// 衝撃: 次に配る source の番号・今の能力の source・同じ相手に続けて当てないための記録・能力ごとの数 (確認用)
		uint32_t impactSerial = 1, abilitySource = 0;
		std::vector<BattleImpactMemo> impactMemo;
		Chrono::BattleImpactTally tally;
		// 全部の能力の衝撃を確かめる (--challenge-impact-check): 次に使う能力と、使うまでの秒
		bool impactCheck = false;
		int checkNext = 0;
		float checkClock = 0;
		// チャレンジの物: 積み木とかけら・配置の番号・壊した数 (種類ごと)
		std::vector<ChallengeBlock> blocks;
		std::vector<ChallengeDebris> debris;
		int blockPattern = 0;
		std::array<int, 3> broken{};
		// 積み木の街: 崖から落ちている体の下向きの速さ
		float fallSpeed = 0;
		// 積み木の街: 体の下の面の傾き (流体の床に渡す)
		Chrono::Vec groundSlope{};
		// 実測 (--challenge-matrix) と、配置を決めて作るときの番号 (負ならランダム)
		ChallengeMatrixState matrix;
		int forcedPattern = -1;
		// 変形武器: 作った武器・作り終えたか・作る画面・武器の形 (番号と見せる残りの秒)
		BattleWeapon weapon;
		bool crafted = false;
		CraftState craft;
		std::vector<BattleShot> newShots;
		// (weaponAge は武器の形になってからの秒)
		int weaponShape = 0;
		float weaponForm = 0, weaponAge = 0;
		// (F5) 武器の形の姿勢 (今と前のフレーム)・撃った反動の強さ (すぐに 0 へ戻る)
		Chrono::GimmickPose weaponPose{}, weaponPosePrevious{};
		float weaponKick = 0;
		// どのチャレンジでも同じ効き目: 時計が止まっている残り (リモコン)・体が重い残り (守る・だるま) とその衝撃の source・このフレームの秒
		// おやつパワー (回復で、しばらく威力が上がる) の残り
		float snack = 0;
		// (timeBonus は回復で足した時間の合計)
		float timeStop = 0, heavy = 0, frameDt = 0, timeBonus = 0;
		uint32_t heavySource = 0;
		Chrono::BattleAction heavyBy = Chrono::BattleAction::Guard;
		Chrono::ChallengeKind challenge = Chrono::ChallengeKind::Blocks;
		int score = 0, combo = 0, best = 0;
		float comboClock = 0, challengeTime = Chrono::BattleTime;
		bool newBest = false;
		// 自分を強くする効き目の残り秒: 急須・ティッシュ (HUD に出す)
		float regen = 0, fluff = 0;
		int tries = 0;
		uint32_t nextId = 1;
		bool previousFire = false;
		std::vector<BattleShot> shots;
		std::vector<BattleFx> fx;
		std::vector<BattlePop> pops;
		int spent = 0;
		std::vector<BattlePlaced> placed;
		// 戦闘で戻した建物 (小物の番号)
		std::vector<size_t> buildings;
		std::string message;
		float messageAge = 0;
		float time = Chrono::BattleTime;
		// 変形: 今の形 (Chrono の Battle*Shape / Gimmick*Shape。0 は液体)・形になってからの秒・形でいる残りの秒・姿勢 (今と前のフレーム)
		int toolShape = 0;
		float toolAge = 0, toolLeft = 0;
		Chrono::GimmickPose toolPose{}, toolPrevious{};
		// 今の能力: 種類・残りの秒・強さ (使ったときの大きさで決まる)・物の大きさの倍率・次の当たりまでの秒・ためた当たり (叩く・剣・ばねの瞬間)
		Chrono::BattleAction ability = Chrono::BattleAction::Throw;
		float abilityLeft = 0, abilityAge = 0, abilityPower = 1, abilityItem = 1, abilityTick = 0, abilityHitAt = -1;
		bool abilityHit = false;
		// 突進 (轢く・転がる・飛ぶ・ラジコン) の向き
		Chrono::Vec dashDir{};
		// 移動の速度 (普段の移動と同じ加速と減速)
		Chrono::Vec velocity{};
		// 戦う場所の円 (半径 0 なら場所を決めない)
		Chrono::Vec arenaAt{};
		float arenaRadius = 0;
	} battle_;
	// 吸収が終わって、次の更新で戦闘を始めるときのランク (-1 なら無し。FinishDevour には registry が無いので次の Update で始める)
	int battlePendingRank_ = -1;
	bool BattleActive() const {
		return devour_ && battle_.phase != BattleStep::None;
	}
	static bool BattleEnabled();
	void StartDevourBattle(entt::registry&, int rank);
	void BeginBattleFight(entt::registry&);
	void RetryDevourBattle(entt::registry&);
	void UpdateDevourBattle(entt::registry&, Chrono::Player&, GameContext&);
	void StartBattleAbility(GameContext&, Chrono::BattleAction, const BattleItem&);
	void TickBattleAbility(GameContext&, float dt);
	// 変形武器 (WeaponCraft.cpp): 作る画面を開く・進める・描く・おまかせで埋める・決めて武器にする・武器の形を作る・使う・進める
	bool WeaponsActive() const {
		return battle_.crafted;
	}
	void OpenWeaponCraft();
	void UpdateWeaponCraft(GameContext&, float dt);
	// 作る画面の形の見本: 実際のスライムを今の材料の形にし (TickCraftPreview)、カメラを見本の窓に映るように置く (CraftPreviewCamera)
	void TickCraftPreview(GameContext&, float dt);
	void CraftPreviewCamera(GameContext&);
	// 見本の窓の背景: スライムのまわりを無地の箱で囲んで、ステージを隠す
	void DrawCraftBackdrop(GameContext&);
	void DrawWeaponCraft(GameContext&);
	void DrawWeaponCraftInventory(UI::Canvas&) const;
	// 左下の cm の巻尺と、変える演出でテープへ飛び込む物の絵
	void DrawWeaponCraftEnergy(UI::Canvas&) const;
	// cm の量 (材料にしなかった残りの物の点の合計。1 点 = 1cm)
	int CraftEnergy() const;
	void DrawWeaponCraftSelection(UI::Canvas&, int filled) const;
	void DrawWeaponCraftSockets(UI::Canvas&, const BattleWeapon&) const;
	void DrawWeaponCraftPreview(UI::Canvas&, const BattleWeapon&);
	void DrawWeaponCraftTraits(UI::Canvas&, const BattleWeapon&) const;
	void DrawWeaponCraftBoosts(UI::Canvas&, const BattleWeapon&) const;
	void AutoWeaponCraft(bool boosts);
	void FinishWeaponCraft();
	BattleWeapon CraftedWeapon() const;
	int BuildWeaponShape(const BattleWeapon&);
	// 形の確かめ (--craft-shape-sheet。WeaponShapeCheck.cpp)・物のプロフィールの確かめ (--item-profile-sheet。ItemProfileCheck.cpp)
	void WriteWeaponShapeSheet();
	void WriteItemProfileSheet();
	// 図鑑の物を全部プロフィールにする (ItemProfileCheck.cpp)・姿の型紙を読む・186 万通りの当てはめを数える (--motif-stats。MotifCheck.cpp)
	std::vector<Chrono::ItemProfile> BuildItemProfiles(std::vector<Chrono::ItemProfileDetail::Grid>* grids = nullptr);
	static std::vector<Chrono::Motif> LoadMotifs();
	// (H3) 物 1 つのプロフィール (初めて使うときに作って取っておく。図鑑に無い物は nullptr) と、読んだ型紙 (一度だけ読む)
	static const Chrono::ItemProfile* ItemProfileOf(const std::string& id);
	static const std::vector<Chrono::Motif>& Motifs();
	void WriteMotifStats();
	void WriteMotifShapeCheck();
	void WriteMotifBaseSheet();
	void UseBattleWeapon(GameContext&);
	// 変形武器の弾を 1 つ作る (向き yaw へ。撃ち方ごとの置き方は UseBattleWeapon)・狙いの補正をした向き・体の特徴 (体当たり・塗り跡)
	BattleShot MakeFusedShot(float yaw) const;
	float FusedAimYaw() const;
	void TickWeaponBody(GameContext&, float dt);
	// 体の特徴の動く速さの倍率 (変形武器を使っていなければ 1)
	float WeaponMoveScale() const;
	void TickBattleWeapons(GameContext&, float dt);
	void DrawWeaponStickers(GameContext&);
	// 変形武器の弾を進める (性質ごとの動きと当たり)・終わるときの爆発・分裂・壁
	void TickFusedShot(GameContext&, BattleShot&, float dt);
	void EndFusedShot(GameContext&, BattleShot&);
	void BurstFusedShot(GameContext&, BattleShot&, bool landed);
	// チャレンジの得点を足す (コンボの倍率をかけ、at の位置に label と点数を浮かべる)。足した点数を返す
	int AddChallengeScore(int points, Chrono::Vec at, const std::string& label);
	// 衝撃を出す: 形に入った相手に効き目を与え、当たった数を返す (touched に当たった相手を足す。exclude の相手には当てない)
	int EmitBattleImpact(
	    GameContext&, const Chrono::BattleImpact&, std::vector<BattleTarget>* touched = nullptr,
	    const std::vector<uint32_t>* exclude = nullptr);
	std::vector<BattleTarget> CollectBattleTargets() const;
	// from に一番近い相手 (exclude の相手と、maxDistance より遠い相手は除く)。いなければ false
	bool NearestBattleTarget(
	    Chrono::Vec from, float maxDistance, BattleTarget& out, const std::vector<uint32_t>* exclude = nullptr) const;
	// チャレンジの物 (ChallengeBlocks.cpp): 作る・進める・描く・衝撃を受ける・HUD の状況の文字
	void BuildChallenge();
	void TickChallenge(GameContext&, float dt);
	void DrawChallenge(GameContext&);
	std::string ChallengeStatus() const;
	bool ApplyImpactToBlock(GameContext&, ChallengeBlock&, const Chrono::BattleImpact&);
	void DamageBlock(GameContext&, ChallengeBlock&, float amount, Chrono::Vec push, bool ignoreArmor);
	void BreakBlock(GameContext&, ChallengeBlock&, Chrono::Vec push, const char* how);
	void DropBlocksAbove(const ChallengeBlock&);
	// チャレンジの場の位置 (unit を単位にした、場の真ん中からの x,z と床からの高さ)
	Chrono::Vec ArenaPoint(float x, float z, float y = 0) const;
	// チャレンジの場を見せる (積み木の街か、平らな床か)
	void ShowChallengeStage(entt::registry&);
	// 積み木の街 (ChallengeBlocksLand.cpp): 見た目の物と、作ったときの unit と場の真ん中 (違えば作り直す)
	std::vector<entt::entity> blocksStage_;
	float blocksStageUnit_ = 0;
	Chrono::Vec blocksStageAt_{};
	// 積み木の街か・街を作る/見せる・崖と壁と低い橋で体を止める (from は前のフレームの位置)・
	// 体を乗せる傾いた面 (真ん中での高さと傾き)・動いた物が街の物に当たったか (弾)・確認用の書き出し (--blocks-land-check)
	bool BlocksStageActive() const;
	// 結果を「こわした積み木の山」で見せるか (積み木クラッシャーを遊んだあと)・こわした数・積む順の積み木の色と金か (金は最後)
	bool BlocksPileResult() const;
	int BlocksPileCount() const;
	void BlocksPileBlocks(std::vector<Engine::Vector4>& colors, std::vector<bool>& gold) const;
	// 結果の積み木の山 (3D。ChallengeBlocks.cpp): 広場の地面の真ん中・積み木 1 個の大きさ・置き場所・色と金か・
	// 今の山の高さ (地面から、なめらかに追う)・カメラ (位置と見る所・回り込む角度)。結果が始まったときに作る
	// 宇宙まで積もる殻 (Chrono::ChallengePileShells): 形・積み始める時刻・置き場所 (下の段から)
	struct BlocksPileShell {
		Chrono::PileShell shape;
		float at = 0;
		std::vector<Chrono::PileSlot> slots;
	};
	// 山のまわりの景色の 1 つの部品: ローポリのモデル (mesh。at は原点、half は大きさの倍率、yaw は向き) か、
	// 積み木の箱 (mesh が 0。at は真ん中、half は半分の大きさ、color)。size は見える大きさ (半分。小さく見える物は描かない)
	struct BlocksPileDecor {
		Chrono::Vec at{}, half{};
		Engine::Vector4 color{1, 1, 1, 1};
		uint32_t mesh = 0;
		float yaw = 0, size = 0;
		// 雲 (カメラと山の間にあるときは描かない)
		bool cloud = false;
	};
	struct BlocksPileState {
		bool active = false, cameraReady = false;
		Chrono::Vec at{}, cameraAt{}, cameraLook{};
		// (height は今のてっぺん (スライムを乗せる)。top と span は今の高さと裾の広さで、カメラが使う (何倍にも変わるので対数でなめらかに追う))
		float block = 1, height = 0, reach = 0, orbit = 0, top = 1, span = 1, nearClip = .1f, farClip = 2000, space = 0;
		std::vector<Chrono::PileSlot> slots;
		std::vector<Engine::Vector4> colors;
		std::vector<bool> gold;
		std::vector<BlocksPileShell> shells;
		// 景色: 地面の物 (おもちゃの街・木・池。遠い物ほど大きいので、どこまで上っても何かが見える) と雲
		std::vector<BlocksPileDecor> decor;
		// 1 m (本当の積み木 1 個が 4cm) の場の長さ・地球の半径 (場の長さ)・地面の高さ (場の床より少し下)
		float meter = 1, earth = 1, groundY = 0;
		// 積み終わった殻が隠す広さ (半分。中の場の積み木は描かない)
		float cover = 0;
	};
	BlocksPileState pile_;
	// 山の下に置く地球 (半球の形。真ん中から外へ距離の対数で輪を分けて、近くほど細かい。模様もその割合で描いた絵) と月
	uint32_t earthMesh_ = 0, earthTex_ = 0, moonTex_ = 0;
	// 景色を描く (地球・地面の物・雲・月)
	void DrawPileWorld(GameContext&);
	// 積み木の街のまわりの景色 (遊んでいる間。ChallengeBlocksLand.cpp): 地面の高さ・近くのモデル (木と家)・積み木の家の箱・遠くの板。
	// unit か場の真ん中が変わったら作り直す
	struct StageBoard {
		Chrono::Vec at{};
		float size = 1;
		int cell = 0;
	};
	struct StageScenery {
		float unit = 0, groundY = 0;
		Chrono::Vec center{};
		std::vector<BlocksPileDecor> models, cubes;
		std::vector<StageBoard> boards;
	} stageScenery_;
	// 遠くの板の絵 (billboards.png)・升ごとの板の形・升ごとの物の幅と高さと家か (1)
	uint32_t boardTex_ = 0;
	std::vector<uint32_t> boardMeshes_;
	std::vector<std::array<float, 3>> boardShapes_;
	void BuildStageScenery();
	void DrawStageScenery(GameContext&, bool ground = true);
	void PreparePileWorld(bool wait);
	// 山を進める (積み木が降る・スライムをてっぺんに乗せる)・描く・カメラ (wanted と view を書きかえる)
	void TickBlocksPile(float dt);
	void DrawBlocksPile(GameContext&);
	void BlocksPileCamera(float dt, Chrono::Vec& wanted, Chrono::Vec& view);
	void ShowBlocksStage(entt::registry&, bool show);
	void BlockBlocksStageBody(Chrono::Vec& at, Chrono::Vec from, float radius) const;
	float BlocksFootprint(Chrono::Vec at, float fromY, float radius, Chrono::Vec& slope) const;
	// 積み木の街: 体の形の点 (体の右・上・前の座標、原点は体の真下の地面) を、乗っている面の傾きに合わせて回す (ほかの所ではそのまま)
	Chrono::Vec BodyTiltApply(Chrono::Vec local) const;
	float BlocksSettle(
	    Chrono::Vec at, Chrono::Vec from, float radius, float dt, float& fallSpeed, Chrono::Vec& slope) const;
	bool BlocksSweep(
	    Chrono::Vec from, Chrono::Vec to, float lift, Chrono::Vec& hit, Chrono::Vec& normal, bool& wall) const;
	void WriteBlocksLandCheck() const;
	// 実測 (ChallengeMatrix.cpp): 試しを始める・進める (input に歩く向きを入れる)・書き出す
	void StartMatrixTrial();
	void TickChallengeMatrix(GameContext&, float dt, Chrono::Vec& input);
	// 確認用 (--weapon-check。WeaponCraft.cpp): 全部の変形武器の撃ち方を撃って確かめる。list は試す性質、sub は 0 的なし・1 近い列・2 遠い列
	struct WeaponCheckState {
		bool active = false, started = false, done = false, fired = false;
		int trial = 0, sub = -1, nearHits = 0, farHits = 0, flagged = 0;
		float clock = 0, spawnH = 0, spawnLow = 0, lowest = 0, maxDist = 0, slopeLife = 0, slopeDist = 0;
		std::array<bool, 21> band{};
		Chrono::Vec spot{}, slope{}, slopeDir{0, 0, 1};
		std::vector<Chrono::FuseLevels> list;
		std::ofstream log;
	} weaponCheck_;
	void TickWeaponCheck(GameContext&, float dt, Chrono::Vec& input);
	// 変形武器の性質の、積み木への効き目 (WeaponCraft.cpp): 揺らす (まわりの積み木にも当たる)・ひび (まわりにもひびを入れる)
	void ShakeAround(GameContext&, const BattleTarget& target, float damage, int level);
	void CrackAround(GameContext&, const BattleTarget& target);
	void WriteChallengeMatrix() const;
	// どのチャレンジでも同じ、自分に効く能力 (回復・急須・リモコン・守る・だるま)
	void ApplyChallengeSelf(Chrono::BattleAction);
	// 今の威力 (体の大きさ x おやつパワー)
	float ChallengePowerNow() const;
	void WriteImpactCheck() const;
	void DrawDevourBattle(GameContext&);
	void DrawDevourBattleUI(GameContext&);
	void DrawDevourBattleRecord(GameContext&);
	void PrepareBattleForTest(entt::registry&);
	void TickBattlePlaced(GameContext&, float dt);
	void DrawBattleProp(GameContext&, uint32_t visual, Chrono::Vec at, float shrink, float yaw, float tilt);
	void BuildBattleArena(entt::registry&);
	entt::entity battleArenaEntity_ = entt::null;
	int devourStage_ = 0, devourRank_ = 0;
	bool devourStageLoaded_ = false;
	std::array<float, 8> devourBests_{};
	// 変形チャレンジ: このステージで挑むチャレンジ (ステージを始めるときに決める)・チャレンジへ移る長押しの秒・自分で移ったか・
	// チャレンジごとの自己ベストのスコア (キーは ChallengeBestKey)
	Chrono::ChallengeKind challenge_ = Chrono::ChallengeKind::Blocks;
	float challengeHold_ = 0;
	bool challengeMoved_ = false;
	std::map<std::string, int> challengeBests_;
	void PickChallenge();
	// 成長の間: 長押しか残り時間でチャレンジへ移る (移ったら true)
	bool TickChallengeMove(Chrono::Player&, float dt, bool held);
	void DrawChallengeCard(GameContext&);
	// 変形アイテムを拾って使える状態か・変形の残り時間・テスト用に全部の形を使うか
	bool devourItemReady_ = false;
	// 自動テストで決めた変形アイテムの形 (-1 ならステージの物)
	int devourTestForm_ = -1;
	float devourMorphLeft_ = 0;
	// 台座の上の変形アイテムの高さ
	float devourItemY_ = 0;
	const Chrono::DevourStage& CurrentDevourStage() const {
		return Chrono::DevourStages()[size_t(devourStage_)];
	}
	void LoadDevourStage();
	// プレイ記録 (DevourPlayLog.cpp)。飲み方は 0 触れる・1 掃除機・2 飛びかかり・3 バネ・4 ハンマー・5 タイヤ
	struct DevourPlayLog {
		struct Sample {
			float t, cm, mass;
			int eaten, area;
			float x, z, speed;
			int combo;
			float fps;
		};
		struct Event {
			float t;
			std::string name;
			float cm;
		};
		bool active = false;
		std::string stamp;
		std::filesystem::path path;
		float elapsed = 0, nextSample = 0, nextSave = 15, moved = 0, sinceEat = 0, longestIdle = 0, frameSum = 0,
		      worstFrame = 0, morphTime = 0, itemTaken = -1, biggest = 0, biggestAt = 0, bestBefore = 0;
		int frames = 0, eaten = 0, total = 0, bounces = 0, hops = 0, morphs = 0, maxCombo = 0;
		Chrono::Vec last{};
		std::array<int, 6> eatBy{};
		std::array<float, 6> gainBy{};
		std::array<int, 8> areaEaten{}, areaTotal{};
		std::array<float, 8> areaGain{}, areaMassTotal{}, areaFirst{};
		std::vector<float> goalTimes, gateSizeTimes, gateOpenTimes;
		std::vector<Sample> samples;
		std::vector<Event> events;
	} devourLog_;
	int devourEatBy_ = 0;
	std::filesystem::path DevourLogFolder() const;
	void StartDevourLog();
	void TickDevourLog(float dt);
	void DevourLogEvent(const std::string&);
	void DevourLogEat(const Chrono::LifeCollectible&, float gain);
	void SaveDevourLog(const char* result);
	int DevourAreaAt(Chrono::Vec) const;
	Chrono::Vec DevourHopStep(float dt);
	void DevourSpringLand(entt::registry&, GameContext&);
	// 変形の並び (1 からのキーの順): 吸収モードはステージのアイテムの形だけ・試験場 (全部の形)・戦闘モード
	const std::vector<Chrono::LifeAction>& LifeForms() const {
		using A = Chrono::LifeAction;
		static const std::vector<A> all{A::Hammer, A::Tire, A::Vacuum, A::Spring},
		    battle{A::Hammer, A::Tire, A::Hydrant, A::Signal},
		    demo{A::Hammer, A::Tire, A::Hydrant, A::Signal, A::Vacuum, A::Spring, A::Sword, A::Plane};
		// ステージのアイテムの形 1 つだけの並び (行動ごとに作っておき、行動の番号で引く)
		static const auto single = [] {
			std::array<std::vector<A>, Chrono::LifeActionTable.size()> t;
			for (size_t i = 0; i < t.size(); ++i)
				t[i] = {A(i)};
			return t;
		}();
		if (devour_)
			return single[size_t(
			    devourTestForm_ >= 0 ? devourTestForm_ : int(Chrono::DevourItemForm(CurrentDevourStage().item)))];
		return slimePlayerDemo_ ? demo : battle;
	}
	int LifeFormIndex() const {
		const auto& forms = LifeForms();
		for (size_t i = 0; i < forms.size(); ++i)
			if (forms[i] == life_.form)
				return int(i);
		return 0;
	}
	// 取り込むときに周りへ体の流体をばらまく (本体と同じ流体シミュレーションの粒子)。
	// 飛んでいる範囲 (中心・半径・経過時間) は、流体を描く範囲に入れるために覚えておく
	struct DevourSplashArea {
		Chrono::Vec at{};
		float radius = 0, age = 0;
	};
	std::vector<DevourSplashArea> devourSplashes_;
	// エディタでの確認: 0 以上なら、ゲームのスライムをその時間の飛びかかりの形にして見せる (動かない)。
	// 距離は目標までの距離 (体の半径の何倍か)。エディタが毎フレーム hold を戻し、閉じれば止まる
	float devourPreviewTime_ = -1, devourPreviewDistance_ = 3;
	int devourPreviewHold_ = 0;
	bool devourWasPreview_ = false;
	// 前のフレームの飛びかかりの形 (体の半径を単位、進んだ分を含む) とその時間。点の速度をここからの差で求める
	std::vector<Chrono::Vec> devourPosePrevious_;
	float devourPoseAge_ = -1;
	void DevourSplash(Chrono::Vec at, float radius, int count, float strength);
	// 路地の塀・バリケードを吸収の対象にした小物の番号 (塀は CityWalls の順)
	std::vector<int> devourWallProps_;
	int devourBarricadeProp_ = -1;
	void TickDevourBody(entt::registry&, Chrono::Player&, GameContext&, float);
	void DevourTouch(entt::registry&, Chrono::Vec from);
	void TickDevourFlies(entt::registry&, float dt);
	// 飲めない物へぶつかる: 弾かれて体が縮み、飲んだ物が飛び出す
	void DevourBump(entt::registry&, Chrono::Player&, GameContext&, Chrono::Vec source, float impact);
	Chrono::Vec DevourPropBlock(
	    entt::registry&, Chrono::Player&, GameContext&, Chrono::Vec old, Chrono::Vec next, float topSpeed);
	float DevourEat(entt::registry&, size_t index);
	void DevourRemoveProp(entt::registry&, size_t index);
	void DevourShatter(entt::registry&, GameContext&, size_t index);
	void DevourHammerHits(entt::registry&, GameContext&, const Chrono::LifeBody&, int pulse);
	void DevourSmash(entt::registry&, GameContext&, float reach);
	void FinishDevour(Chrono::Player&);
	void DevourShedBody(Chrono::Player&, GameContext&, float damage, Chrono::Vec source);
	void DrawDevourMeals(GameContext&);
	void DrawDevourUI(GameContext&);
	// ---- 結果画面の「おなかの中」(DevourShelf.cpp): 測ったあと体の中へ入り、飲みこんだ物を種類ごとに並べる ----
	// 飲みこんだ物の記録 (図鑑の項目・見た目の番号・色)。ぶつかって飛び出した物は消す
	// gain は飲んだときに増えた量、radius は物の大きさ (戦闘で使うと、この量だけ体が縮む)
	struct DevourEaten {
		std::string id;
		uint32_t visual = 0xffffffffu;
		Engine::Vector4 tint{1, 1, 1, 1};
		float gain = 0, radius = 1;
	};
	std::vector<DevourEaten> devourEaten_;
	void DevourRecordEaten(const entt::registry&, uint32_t visual);
	void DevourForgetEaten(uint32_t visual);
	// 並べる升: 図鑑の項目・今の数・出た時刻・数が増えた時刻
	struct ShelfCell {
		int entry = -1;
		int count = 0, total = 0;
		float popAt = -1, kickAt = -1;
		bool fresh = false;
		Engine::Vector4 tint{1, 1, 1, 1};
	};
	struct DevourShelf {
		std::vector<ShelfCell> cells;
		std::vector<int> order;
		size_t shown = 0;
		float clock = 0, next = 0, soundAt = -1;
		int detail = -1;
		float detailAt = 0;
		int cols = 1;
		float cell = 96, left = 0, top = 0;
		bool built = false;
	};
	DevourShelf shelf_;
	static constexpr float kShelfDive = 2.5f, kShelfDiveTime = 1.f, kShelfPopStart = kShelfDive + kShelfDiveTime + .15f;
	void BuildDevourShelf(const std::vector<std::string>& fresh);
	// 体の中へ入る量 (カメラ 0〜1、流体の見え方 0〜1)
	float DevourShelfDive() const;
	float DevourShelfInterior() const;
	// 升の画面の矩形 (設計座標)
	UI::Rect DevourShelfRect(size_t cell) const;
	// 体の中のカメラの位置と向きを求める (結果のカメラの構図に重ねる)
	void DevourShelfCamera(GameContext&, Chrono::Vec& wanted, Chrono::Vec& view, float ground);
	// 演出を進め、升のクリックを受け取る。操作を使ったら true
	bool UpdateDevourShelf(GameContext&, UI::Canvas&);
	void DrawDevourShelf3D(GameContext&);
	void DrawDevourShelfUI(GameContext&, UI::Canvas&);
	float shelfDepth_ = 6;
	void BuildSlimePlayerDemo(entt::registry&);
	Chrono::Vec ConstrainLifeMovement(Chrono::Vec from, Chrono::Vec to, float radius) const;
	void UpdateSlimePlayerDemo(entt::registry&, Chrono::Player&, GameContext&);
	void DrawSlimePlayerDemoUI(GameContext&);
	// スライムの状態と、研究所のレシピ・素材・クリア数
	Chrono::SlimeLife life_;
	std::array<Chrono::SlimeRecipe, 3> lifeRecipes_{};
	int lifeRecipeSlot_ = 0, lifeMaterials_ = 3, lifeClears_ = 0;
	// 各体の前回の行動と粒子数 (音や演出の切り替えに使う)
	std::array<int, Chrono::LifeBodyCount> lifeLastAction_{};
	// 体ごとの足元の傾き (x 右向き・z 前向きの、1 単位進むごとの高さの変化)。斜面で体の形を傾けるのに使い、なめらかに変える
	std::array<Chrono::Vec, Chrono::LifeBodyCount> lifeSlope_{};
	// 体の形の傾き (lifeSlope_[0] を世界の x・z へ 1 進むごとの高さに直したもの)。積み木の街では流体の床と目の場所に渡し、体の形と合わせる
	Chrono::Vec lifeTiltWorld_{};
	std::array<uint32_t, Chrono::LifeBodyCount> lifeLastCounts_{};
	bool lifeWasLab_ = false, lifeReseed_ = true, lifePreviewEmitted_ = false;
	// 章の見出しを出した段階と、出し始めた時刻 (life_.age)
	int lifeTitleStep_ = -1;
	float lifeTitleStart_ = 0;
	// 信号機: 横断歩道 (中心・渡る向き・前方向・長さ・経過時間・ダメージの倍率・当てた人影と敵の組)・広がる輪・変形アイテムの足元と大きさ
	struct SignalWalk {
		Chrono::Vec center{}, side{}, forward{};
		float length = 14, age = 0, damage = 1, size = 1, dir = 1;
		std::vector<uint64_t> hits;
	};
	struct SignalPulse {
		Chrono::Vec at{};
		float age = 0, radius = 10;
		Chrono::SignalColor color = Chrono::SignalColor::Red;
	};
	std::vector<SignalWalk> signalWalks_;
	std::vector<SignalPulse> signalPulses_;
	int signalWalkCount_ = 0; // 横断歩道を敷く向きを交互にする
	Chrono::Vec signalItemAt_{};
	static constexpr float SignalItemScale = 1.4f;
	// 変形の見せ場: 形・経過時間・形が完成した時刻・寄る構図の中心と半径・戻す太陽の強さと被写界深度
	struct MorphShowcase {
		bool active = false, morphed = false;
		Chrono::LifeAction form = Chrono::LifeAction::Tire;
		float age = 0, readyAt = -1, radius = 1, sun = 1;
		Chrono::Vec center{}, facing{0, 0, 1};
		float dofFocus = 30, dofRange = 30, dofStrength = 0;
	} showcase_;
	// ステージ開始のカメラ演出: 経過時間・スキップの長押しの時間・終わった後の暗転の残り
	// ヒットストップの残り時間 (この間はスライムの世界を止め、カメラの揺れだけを進める)
	float lifeHitStop_ = 0;
	// 武器の形から吸収ボタンで戻ったとき、戻りきったら吸収を始めるための受付の残り時間
	float lifeAbsorbBuffer_ = 0;
	// 形が変わったときのカメラの構図のつなぎ (形の種類・進み・始めと今の体からの中心のずれと半径)、消火栓の肩越しの構図の割合
	struct LifeCameraBlend {
		int kind = -1;
		float age = 1;
		Chrono::Vec fromOffset{};
		float fromRadius = 1;
		Chrono::Vec offset{};
		float radius = 1;
	} cameraBlend_;
	float cameraHydrant_ = 0;
	// スライムの生活 (SlimeLife.inl) の処理: 触手の描画・研究所・試練・作成・更新・描画・UI・吸収・被弾
	void DrawLifeTentacles(GameContext&, const Chrono::LifeBody&);
	// 分裂と飲み込みの音
	uint32_t lifeSplitSounds_[3]{0xffffffffu, 0xffffffffu, 0xffffffffu}, lifeEngulfSound_ = 0xffffffffu;
	// 吸収モードの音 (Resources/Sound/Devour/)
	struct DevourSounds {
		static constexpr uint32_t None = 0xffffffffu;
		uint32_t gulpSmall[4]{None, None, None, None}, gulpMid[2]{None, None},
		    gulpBig = None, note = None, splat = None, pounce = None, bounce = None, hit = None, shatter = None,
		    vacuumLoop = None, rankUp = None, rankTop = None, gateOpen = None, tick = None, hurry = None, timeUp = None,
		    morph = None;
	} devourSounds_;
	uint32_t devourGulpTurn_ = 0;
	size_t devourVacuumVoice_ = 0;
	float devourNoteAt_ = -1;
	void LoadDevourSounds();
	// 効果音の音量を掛けて鳴らす
	void PlayDevourSound(uint32_t sound, float volume, float pitch = 1);
	// 飲んだ音: 重さで音を選び、コンボの音階を重ねる
	void PlayDevourGulp(float weight, int combo);
	// 残り時間の知らせ (before は前のフレームの残り時間)
	void TickDevourTimeSounds(float before);
	void BuildSlimeLife(entt::registry&);
	void UpdateSlimeLife(entt::registry&, Chrono::Player&, GameContext&);
	// フレーム内の入力と移動結果。更新順を保ったまま各処理へ渡す。
	struct LifeFrameInput {
		bool absorb = false, fire = false, firePressed = false, changedForm = false, returned = false;
	};
	struct LifeMovement {
		Chrono::Vec previous{};
		float groundHeight = 0;
		bool wasGrounded = false;
	};
	struct LifeUnlocks {
		bool tire = false, hydrant = false, signal = false, vacuum = false, spring = false;
	};
	bool HandleLifeFramePause(entt::registry&, Chrono::Player&, GameContext&, float dt);
	LifeUnlocks TickLifeFrameTimers(Chrono::Player&, GameContext&, float dt);
	int ReadLifeMorphHotkey(GameContext&, const Chrono::ControlFrame*);
	LifeFrameInput
	    ReadLifeActionInput(entt::registry&, Chrono::Player&, GameContext&, const Chrono::ControlFrame*, bool cutscene);
	void TickLifeTireCharge(GameContext&, Chrono::Vec wish, const LifeFrameInput&, float dt);
	LifeMovement IntegrateLifeMovement(
	    entt::registry&, Chrono::Player&, GameContext&, Chrono::Vec wish, float dt, float speed, bool pouncing,
	    bool jumped);
	LifeMovement UpdateLifeMovement(
	    entt::registry&, Chrono::Player&, GameContext&, const Chrono::ControlFrame*, const LifeFrameInput&,
	    bool cutscene, float dt);
	void UpdateLifeTireDashHits(entt::registry&, Chrono::Player&, GameContext&, Chrono::Vec previous);
	void CollectLifeContacts(entt::registry&, Chrono::Vec previous);
	void UpdateLifeAbsorptionInput(Chrono::Player&, GameContext&, bool absorb, float dt);
	void ApplyLifeLanding(entt::registry&, Chrono::Player&, GameContext&, bool wasGrounded);
	void SyncLifeTransform(entt::registry&, float groundHeight);
	void TickLifeBodyGrabs(entt::registry&, size_t index);
	void TickLifeBodyWeapon(
	    entt::registry&, Chrono::Player&, GameContext&, size_t index, const LifeFrameInput&, float dt);
	void TickLifeBodyRecovery(Chrono::LifeBody&);
	void TickLifeBodyActions(entt::registry&, Chrono::Player&, GameContext&, const LifeFrameInput&, float dt);
	void UpdateLifeContactDamage(entt::registry&, Chrono::Player&, GameContext&, Chrono::Vec previous, float dt);
	void NotifyLifeUnlocks(entt::registry&, GameContext&, const LifeUnlocks& previous);
	void FinishLifeFrame(
	    entt::registry&, Chrono::Player&, GameContext&, const Chrono::ControlFrame*, bool cutscene, float dt);
	void DrawSlimeLife(entt::registry&, GameContext&);
	void DrawSlimeLifeUI(entt::registry&, GameContext&);
	void SyncSlimeBodies(GameContext&);
	void MeasureDevourBody(GameContext&);
	void DamageSlimeLife(entt::registry&, Chrono::Player&, float, GameContext&, Chrono::Vec);
	void LifeHit(
	    entt::registry&, Chrono::Player&, GameContext&, uint32_t, float,
	    Chrono::LifeAction source = Chrono::LifeAction::Idle, Chrono::Vec from = {});
	bool StartLifeAbsorb(int);
	void CityTireDashHits(entt::registry&, GameContext&, const Chrono::LifeBody&, uint64_t);
	void TickLifeChild(entt::registry&, Chrono::Player&, GameContext&, size_t, float);
	void DrawLifeChildMarkers(GameContext&);
	void DrawLifeEnemyMarkers(GameContext&);
	void StartMorphShowcase(entt::registry&, GameContext&, Chrono::LifeAction);
	void UpdateMorphShowcase(entt::registry&, Chrono::Player&, GameContext&, float);
	void DrawMorphShowcase(GameContext&);
	// ステージ開始のカメラ演出 (SlimeIntro.cpp) の更新・描画
	// 手応え (ヒットストップ・カメラの揺れ・コントローラーの振動) を at で起こす。scale で揺れと振動の強さを変える
	void LifeImpact(
	    GameContext&, const LifeImpactSpec&, Chrono::Vec at, Chrono::Vec direction = {0, 1, 0}, float scale = 1);
	void FireSignal(entt::registry&, Chrono::Player&, GameContext&, Chrono::SignalColor);
	void UpdateSignalEffects(entt::registry&, Chrono::Player&, GameContext&, float);
	void DrawSignal(GameContext&);
	void DrawSignalUI(GameContext&);
	bool SaveSlimeLab();
	// 消火栓の水 (SlimeHydrant.inl): 水の発射と狙いの更新
	void UpdateHydrantWater(entt::registry&, Chrono::Player&, GameContext&, float, bool, bool);
	void UpdateHydrantAim(GameContext&);
	Chrono::HydrantAim hydrantAim_{};
	uint64_t hydrantWaterSerial_ = 0;
	// 街のステージ (CityStage.inl): 作った建物・CC0 の小物・門・役割ごとの敵
	// 街のモデル (メッシュ・テクスチャ・大きさ)
	struct CityModel {
		uint32_t mesh = 0, texture = 0;
		std::vector<uint32_t> extra;
		Chrono::Vec lo{}, hi{};
	};
	// 吸収モードのステージを作る道具 (街のステージの材質と色の補正を借りる)
	struct StageTools {
		std::function<const CityModel&(int)> surface;
		std::function<DirectX::XMFLOAT4(int)> tint;
	};
	void BuildRoomGround(CityParts::CityBuilder&);
	void BuildRoomStage(entt::registry&, const StageTools&);
	void BuildRoomSets(entt::registry&);
	void BuildRoomGimmicks(entt::registry&);
	void BuildSlopeTown(entt::registry&, const StageTools&);
	bool GimmickClear(Chrono::Vec at, float margin) const;
	// ローポリのモデル (scripts/lowpoly/build_lowpoly.py): メッシュと大きさの範囲。部屋と町で同じ置き方を使う
	// pieces はモデルのつながった部分ごとの範囲 (脚・板など。モデルの座標)
	// feet: 地面に触れる部分 (一番下から少しの高さまでの頂点を、辺でつながった塊ごとにまとめた範囲)
	struct LowModel {
		uint32_t mesh = 0;
		Chrono::Vec lo{}, hi{};
		std::vector<std::array<Chrono::Vec, 2>> pieces, feet;
	};
	// lowFeet_: 置いた物を、地面に触れる部分だけで体を止める (坂の町)。BeginLowProps で戻る
	std::map<std::string, LowModel> lowModels_;
	uint32_t lowPalette_ = 0;
	std::vector<uint32_t> lowMaps_;
	float lowOcclude_ = 12;
	bool lowFeet_ = false;
	const LowModel& LowModelOf(const std::string& id);
	float LowScaleFor(const std::string& id, float width);
	void BeginLowProps(float occludeHeight);
	entt::entity LowProp(
	    entt::registry& r, const std::string& id, Chrono::Vec at, float scale, float yaw, float fill,
	    Engine::Vector4 tint = {1, 1, 1, 1}, float tilt = 0);
	// 置いた物の細い部分 (脚・頭の板など) を、カメラを遮る判定の parts に加える
	void AddThinOccluderParts(
	    entt::registry& r, entt::entity e, const std::string& id, Chrono::Vec at, float scale, float yaw);
	void BuildStageItem(entt::registry&, const StageTools&);
	// 建物の敷地と、敷地ごとの吸収の対象の番号 (life_.props の番号。建物は飲み込むまで通れない)
	std::vector<Chrono::StageLot> stageLots_;
	// 床に立つギミックの看板 (旗) の足元。土台はなだらかな丸い坂として乗り越え、柱だけは丸く押し出して横へ滑らせる
	std::vector<Chrono::Vec> gimmickSigns_;
	float GimmickSignGround(Chrono::Vec at) const;
	Chrono::Vec GimmickSignSlide(Chrono::Vec at, float bodyRadius) const;
	// 建物の残りの部位と当たり判定 (飲み込んだら隠し、当たり判定は消す)。キーは life_.props の番号
	std::unordered_map<size_t, std::vector<entt::entity>> devourBuildingParts_;
	// 吸収モードの地図の中で、半径 margin の体が歩けるか
	bool DevourWalkable(Chrono::Vec p, float margin) const {
		// 坂の町は建物の敷地を持たず (家は飲み込む物として体を止める)、町の端だけを調べる
		if (CurrentDevourStage().map == "slope")
			return Chrono::SlopeInside(p, margin);
		float half = CurrentDevourStage().half;
		if (half > 0 && (std::abs(p.x) + margin > half || std::abs(p.z) + margin > half))
			return false;
		// (部屋の本棚やおもちゃ箱のように上に乗れる家具は、その上の面より上にいれば止めない)
		return Chrono::StageWalkable(
		    p, margin, stageLots_, [&](size_t i) { return !(stageLots_[i].top > 0 && p.y >= stageLots_[i].top); });
	}
	// 街の小物の見た目
	struct CityPropVisual {
		uint32_t mesh = 0, texture = 0;
		std::vector<uint32_t> extra;
		Chrono::Vec scale{1, 1, 1}, rotate{}, center{};
		float height = 1;
		std::string shader = "EnvironmentSurface";
		Engine::Vector4 color{1, 1, 1, 1};
		// 皮で包む形: 半分の大きさと角の丸み (0 なら飲み込む大きさの箱にする)
		Chrono::Vec half{};
		float round = 0;
	};
	// 街の光る看板など (kind: 0 は光り続ける、1 は炎のように揺らぐ、2 は非常灯のように点滅する)
	struct CityGlow {
		Chrono::Vec center{}, right{}, up{}, size{};
		Engine::Vector4 color{1, 1, 1, 1};
		int kind = 0;
		float phase = 0;
	};
	std::vector<CityGlow> cityGlows_;
	// 街の小物 (触手で取り込める物)
	struct CityPropSeed {
		entt::entity entity = entt::null;
		Chrono::Vec at{};
		float required = 0, reward = 0;
		Chrono::LifeMorphItem morphItem = Chrono::LifeMorphItem::None;
		float radius = 1, fill = 1, reach = 0;
		bool beacon = false;
		float foot = 0, height = 0;
		std::vector<Chrono::Vec> feet;
	};
	// バリケードや塀の部品 (材質: 0 木・1 鉄・2 ブロック・3 金属)・敵の弾・壊れた破片・踏みつけの衝撃
	struct CityPlank {
		Chrono::Vec at{}, size{}, rotate{};
		int material = 0;
	};
	struct CityShot {
		Chrono::Vec at{}, velocity{};
		float age = 0;
		bool shell = false;
		Chrono::Vec target{};
	};
	struct CityDebris {
		Chrono::Vec at{}, velocity{}, spin{}, size{}, rotate{};
		float age = 0;
		int material = 0;
	};
	struct CityStomp {
		Chrono::Vec at{};
		float age = 0, radius = 30;
	};
	// 小物の見た目・小物・バリケード・弾・破片・衝撃の一覧
	std::map<uint32_t, CityPropVisual> cityVisuals_;
	std::vector<CityPropSeed> cityProps_;
	std::vector<CityPlank> cityBarricade_;
	// 路地裏の壁 (部品・体力・揺れ・最後に当たった攻撃)
	struct CityWallState {
		std::vector<CityPlank> pieces;
		float health = 0, shake = 0;
		uint64_t hit = 0;
	};
	std::vector<CityWallState> cityWalls_;
	// 火事場の炎 (残りの火の量。0 で消える)・炎に触れた時間
	struct CityFire {
		Chrono::Vec at{};
		float radius = 12, health = 0, hiss = 0;
	};
	std::vector<CityFire> cityFires_;
	std::vector<CityShot> cityShots_;
	std::vector<CityDebris> cityDebris_;
	std::vector<CityStomp> cityStomps_;
	// 街の敵とバリケード・門のモデル
	CityModel cityRat_, cityCannon_, cityWood_, cityStoneSkin_, cityIron_, cityArmor_, cityBlock_;
	uint32_t cityCube_ = 0;
	// 門のエンティティと門の状態・体力・揺れ・ヒントの時間
	entt::entity cityGateEntity_ = entt::null;
	Chrono::CityGates cityGates_{};
	float cityBarricadeHealth_ = 0, cityGateHealth_ = 0, cityBarricadeShake_ = 0, cityGateShake_ = 0,
	      cityHintClock_ = 0;
	uint64_t cityBarricadeHit_ = 0, cityGateHit_ = 0;
	int cityGateProp_ = -1, cityAnnounced_ = -1;
	bool cityGateUnlocked_ = false, cityHammerAnnounced_ = false;
	// 街のステージの作成・敵の出現・門の破壊・武器の当たり・更新・描画
	void BuildCityStage(entt::registry&);
	void BreakCityGate(entt::registry&, bool barricade, bool absorbed);
	void BreakCityWall(size_t, bool absorbed = false);
	void ResetCityHazards();
	void CityWeaponHits(entt::registry&, GameContext&, const Chrono::LifeBody&, int pulse, uint64_t hitKey);
	void UpdateCityStage(entt::registry&, Chrono::Player&, GameContext&, float);
	void DrawCityStage(GameContext&);
	bool DrawCityPropInTentacle(GameContext&, uint32_t visual, Chrono::Vec at, float shrink);
	// 群れの敵 (HordeCombat.inl / SwarmStates.inl) とその演出・メッシュ・出現の時間
	std::vector<Chrono::SwarmEnemy> swarm_;
	struct SwarmEffect {
		Chrono::Vec at{};
		float age = 0, size = 1;
		int kind = 1;
	};
	std::vector<SwarmEffect> swarmEffects_;
	struct SwarmMesh {
		uint32_t handle = 0;
		Chrono::Vec inverseExtent{1, 1, 1}, center{};
	};
	std::array<SwarmMesh, 3> swarmMeshes_{};
	uint32_t swarmQuad_ = 0, swarmNextId_ = 1;
	float swarmSpawnClock_ = 0, swarmSoundClock_ = 0, swarmAttackClock_ = 0;
	int swarmAttackBurst_ = 0;
	size_t swarmAttackCursor_ = 0;
	bool swarmRestPending_ = false;
	Chrono::Vec swarmEmergenceAt_{};
	// 群れの作成・描画
	void BuildSwarm(entt::registry&);
	void DrawSwarm(entt::registry&, GameContext&);
	// インク (ChronoInk.cpp): カメラが潜らないための地面の面・作成・カメラ
	// こぼれた液体の管のメッシュ
	uint32_t slimeBeamMesh_ = 0;
	std::vector<Chrono::InkSurface> inkSurfaces_;
	void BuildInk(entt::registry&);
	void InkCamera(entt::registry&, Chrono::Player&, GameContext&);
	// インクの地面の高さ
	float InkGround(Chrono::Vec, int* = nullptr) const;
	using V = Chrono::Vec;
	// シーン・プレイヤー
	GameScene* scene_;
	entt::entity player_ = entt::null;
	// 当たり判定に使う箱 (地形)
	std::vector<Chrono::Bounds> solids_;
	// カメラの追従・構図・ポスト効果の強さ・音・テクスチャ
	V cameraFollow_{};
	bool cameraReady_ = false;
	// 街のカメラ: 後ろが塞がれたときに持ち上げる量と、届く距離の割合 (なめらかに戻す)
	float cameraLift_ = 0, cameraReach_ = 1;
	// 坂の町で、地形のせいでカメラを動かしている量 (持ち上げ・体へ寄せる。段の縁を越えるたびに跳ねないよう、時間をかけて変える)
	Chrono::Vec slopeCameraShift_{};
	// カメラの注視点の体の中心からのずれ (坂の町ではなめらかに追わせる)
	Chrono::Vec cameraSubjectOffset_{};
	float postStrength_ = 0;
	// 大きい物を飲んで育つ演出: 遅くなる時間の最初の長さ (物の大きさ)・飲んだ瞬間から 1→0 に減る画面の縁の光
	float devourHeavyPeak_ = 0, devourHeavyKick_ = 0;
	uint32_t hitSound_ = 0xffffffff, coreSound_ = 0xffffffff;
	uint32_t white_ = 0, sphere_ = 0, plane_ = 0, cube_ = 0, disc_ = 0;
	// 状態 (初期化済み・終了・勝利・前フレームの入力)
	bool initialized_ = false, finished_ = false, won_ = false, prevSpace_ = false;
	bool diagnostic_ = false;
	// カメラ (向き・距離・肩越し・視野角・補助・揺れの設定)
	float yaw_ = 0, pitch_ = 0.16f, finishAge_ = 0;
	float devourHudCm_ = 0, resultCameraAge_ = 0, resultLo_ = 0, resultHi_ = 0, resultNear_ = 0, resultFar_ = 0,
	      resultTopY_ = 0;
	static constexpr float kResultFreezeAge = .95f, kResultSettledAge = 1.3f;
	float resultLeft_ = 0, resultRight_ = 0, resultTop_ = 0;
	bool resultScreenSet_ = false;
	bool resultMeasured_ = false;
	Chrono::Vec resultOrigin_{};
	std::vector<entt::entity> resultHidden_;
	float fov_ = 1.0472f;
	int shakeSetting_ = 2;
	bool prevShake_ = false;
	// 対象へ届くか / メッシュや箱の作成 / 当たり判定の箱の記録
	entt::entity Mesh(entt::registry&, const std::string&, const std::string&, V, V);
	void CacheSolids(entt::registry&);
	// 当たり判定 (移動の掃引・空いているか・壁に沿った移動・中心)
	bool Sweep(V, V, V, float&, V&) const;
	bool Clear(V, V, float = 0) const;
	V Move(V, V, V, bool&, V* = nullptr) const;
	// 攻撃の対象選び・射撃・命中・被弾・反応
	void Damage(entt::registry&, Chrono::Player&, float, GameContext&, V, float perfectWindow = .12f);
	// チェインの失敗・演出・プレイヤー・ワールド・ボス・カメラの更新と終了
	void Presentation(entt::registry&, Chrono::Player&, GameContext&);
	void Finish(Chrono::Player&, bool);
};
} // namespace Game
