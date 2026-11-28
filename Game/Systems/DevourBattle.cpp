// DevourBattle.cpp
// 変形チャレンジ (吸収のあとの第 2 フェーズ。ChronoSystem の一部)。規則は Chrono/ChallengeRules.h
//  - 成長 (吸収) とチャレンジでステージの制限時間を分け合う。長押しか残り 30 秒でチャレンジへ移り、残り時間で挑む
//  - 準備フェーズで吸収した物は「袋」に入る。チャレンジが始まる前に、袋の物から変形武器を作って使う (WeaponCraft.cpp)
//  - 物ごとに能力がある (collection.json の action。省略は battle の分類の動き)。投げる以外は変形する
//  - 物を使っても体は縮まない。大きさは威力 (BattlePower) になる
//  - 時間が来たら終わり。スコアは自己ベストとして残し、次のステージへはいつでも進める。結果画面の L / B でチャレンジだけやり直せる
// 操作: WASD / 左スティックで移動、マウス / 右スティックで向き、左クリック / RT で撃つ
// 規則は Chrono/DevourBattleRules.h、物の能力は collection.json の battle / action、能力の形は Chrono/DevourGimmickRules.h
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/MotifShape.h"
#include "CityBuilder.h"
#include <random>

namespace Game {
namespace {
bool HasArg(const wchar_t* flag) {
	return wcsstr(GetCommandLineW(), flag) != nullptr;
}
std::string ArgText(const wchar_t* flag) {
	const wchar_t* at = wcsstr(GetCommandLineW(), flag);
	if (!at)
		return {};
	at += wcslen(flag);
	std::string out;
	while (*at && *at != L' ' && *at != L'"')
		out += char(*at++);
	return out;
}
std::mt19937& BattleRandom() {
	static std::mt19937 random{12345u};
	return random;
}
float BU(float low, float high) {
	return std::uniform_real_distribution<float>(low, high)(BattleRandom());
}
// 図鑑の id から能力 (建物なら building=true)
BattleAction ActionOfId(const std::string& id, bool& building) {
	const int index = Collection::IndexOf(id);
	building = false;
	if (index < 0)
		return BattleAction::Throw;
	const auto& entry = Collection::Entries()[size_t(index)];
	BattleKindOf(entry.battle, building);
	return BattleActionOf(entry.action, entry.battle);
}
// 図鑑の名前と絵 (無ければ id)
std::string NameOfId(const std::string& id) {
	const int index = Collection::IndexOf(id);
	return index < 0 ? id : Collection::Entries()[size_t(index)].name;
}
// 能力の色 (仮の見た目と HUD)
Engine::Vector4 ActionColor(BattleAction a) {
	static constexpr Engine::Vector4 colors[] = {
	    {.95f, .62f, .2f, 1},
	    {.35f, .7f, 1, 1},
	    {.95f, .32f, .28f, 1},
	    {.65f, .65f, .72f, 1},
	    {.45f, .9f, .4f, 1},
	    {.9f, .8f, .35f, 1},
	    {.3f, .75f, 1, 1},
	    {.55f, .4f, .85f, 1},
	    {.95f, .5f, .3f, 1},
	    {.8f, .85f, .95f, 1},
	    {1, 1, 1, 1},
	    {1, .6f, .85f, 1},
	    {.75f, .3f, .3f, 1},
	    {.3f, .6f, .95f, 1},
	    // 将棋の駒 (歩・香車・桂馬・銀・金・角・飛車・王): 駒の木の色から、強い駒ほど金色に
	    {.93f, .78f, .48f, 1},
	    {.93f, .72f, .4f, 1},
	    {.93f, .68f, .36f, 1},
	    {.85f, .85f, .9f, 1},
	    {1, .82f, .3f, 1},
	    {.95f, .55f, .3f, 1},
	    {.95f, .4f, .3f, 1},
	    {1, .9f, .35f, 1},
	    // 置く・まく物 (ビー玉・積み木の塔・ドミノ・絵の具・ぬいぐるみ)
	    {.55f, .85f, 1, 1},
	    {.95f, .75f, .3f, 1},
	    {.95f, .95f, .9f, 1},
	    {.9f, .4f, .75f, 1},
	    {.85f, .65f, .45f, 1},
	    // 飛び道具 (十円玉・輪・ゴムボール・紙飛行機・トランプ・鉛筆)
	    {.85f, .55f, .3f, 1},
	    {.9f, .5f, .4f, 1},
	    {.95f, .35f, .35f, 1},
	    {.95f, .95f, 1, 1},
	    {.9f, .3f, .35f, 1},
	    {.4f, .75f, .4f, 1},
	    // 止める・弱らせる (リモコン・まんが・かご・ノート・消しゴム・湯のみ) と自分を強くする (だるま・招き猫・急須・ティッシュ・靴下)
	    {.5f, .5f, .6f, 1},
	    {.95f, .6f, .3f, 1},
	    {.8f, .65f, .4f, 1},
	    {.95f, .95f, .85f, 1},
	    {.6f, .7f, 1, 1},
	    {.6f, .8f, .4f, 1},
	    {.9f, .25f, .2f, 1},
	    {1, .9f, .5f, 1},
	    {.5f, .75f, .45f, 1},
	    {1, 1, 1, 1},
	    {.6f, .75f, .4f, 1}};
	return colors[std::min(int(a), int(BattleAction::Count) - 1)];
}
// 突進する能力か (轢く・転がる・飛ぶ・ラジコン)
bool DashAction(BattleAction a) {
	return a == BattleAction::Guard || a == BattleAction::Ride || a == BattleAction::Roll || a == BattleAction::Fly ||
	       a == BattleAction::Rc || a == BattleAction::Kyo || a == BattleAction::Kei;
}
// 桂馬は跳んでいる間 (最初の 0.42 秒) だけ進む
constexpr float kKeiJump = .42f;
} // namespace

// 戦闘を使うか: 従来の進行を確かめる自動テストと計測では、時間切れからそのまま結果画面へ進む
bool ChronoSystem::BattleEnabled() {
	if (HasArg(L"--devour-battle") || QuickChallenge().active)
		return true;
	return !HasArg(L"--slime-life-smoke") && !HasArg(L"--load-profile-auto");
}

// 吸収が終わった体と袋を覚え、建物を戻して戦闘の始まりの演出へ
void ChronoSystem::StartDevourBattle(entt::registry& r, int rank) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	// 準備フェーズの終わりにまだ体に入りきっていない成長は、ここで体に足してから大きさを覚える
	body.mass = std::min(LifeMaxMass, body.mass + std::max(0.f, body.pendingGrowth));
	body.pendingGrowth = 0;
	b.startAt = body.at;
	b.startForward = body.forward;
	b.startMass = body.mass;
	b.unit = BattleUnitAt(body.mass);
	b.rank = rank;
	// チャレンジ: ステージを始めるときに決めたもの。時間は成長の残り (やり直しも同じ時間)
	b.challenge = challenge_;
	b.challengeTime = ChallengeTimeFor(devourClock_.left);
	// 戦う場所: マップの外 (+x の側) に置いた専用の広い平らなステージへ飛ばす
	BuildBattleArena(r);
	b.startAt = b.arenaAt + V{0, 1.25f, 0};
	b.startForward = {0, 0, 1};
	// 吸収した物を種類ごとに袋へ入れる (建物は使えない。戦闘で街に戻す)
	b.startBag.clear();
	for (const auto& eaten : devourEaten_) {
		bool building = false;
		const BattleAction action = ActionOfId(eaten.id, building);
		if (building)
			continue;
		auto bag =
		    std::find_if(b.startBag.begin(), b.startBag.end(), [&](const BattleBag& x) { return x.id == eaten.id; });
		if (bag == b.startBag.end()) {
			b.startBag.push_back({eaten.id, action, 0, {}});
			bag = b.startBag.end() - 1;
		}
		bag->items.push_back(
		    {eaten.visual, eaten.id, eaten.gain > 0 ? eaten.gain : DevourReward(eaten.radius), eaten.radius});
	}
	// 小さい物 (collection.json の bundle) は bundle 個ずつ束にして 1 回分にする。縮む量は束の合計 (1 回の上限はある)。引く重みは束の数
	for (auto& bag : b.startBag) {
		const int index = Collection::IndexOf(bag.id);
		const int bundle = index < 0 ? 1 : Collection::Entries()[size_t(index)].bundle;
		if (bundle > 1) {
			std::vector<BattleItem> bundled;
			for (size_t i = 0; i < bag.items.size(); i += size_t(bundle)) {
				BattleItem item = bag.items[i];
				for (size_t k = i + 1; k < std::min(bag.items.size(), i + size_t(bundle)); ++k) {
					item.gain += bag.items[k].gain;
					item.radius = std::max(item.radius, bag.items[k].radius);
				}
				bundled.push_back(item);
			}
			bag.items = std::move(bundled);
		}
		bag.absorbed = int(bag.items.size());
	}
	// 変形武器は新しいチャレンジごとに作り直す (やり直しでは同じ武器のまま)
	b.crafted = false;
	// 部屋: 残すのは家具 (机・こたつ・ベッドなど、吸収できない物) だけ。吸収しきれなかった小物は片付ける
	// (家具は吸収の対象の一覧 life_.props に入っていないので、一覧に残っている物を全部消せばよい。積み木の壁も道が開く)
	if (CurrentDevourStage().map == "room") {
		for (size_t i = 0; i < life_.props.size(); ++i)
			if (life_.props[i].active)
				DevourRemoveProp(r, i);
		// 部屋のギミック (だるま落としなど) の看板・床の輪・部品も隠し、看板の土台の坂と作動範囲の光の膜も消す (戦闘では使わない)
		auto hide = [&](uint32_t visual) {
			if (visual == 0xffffffffu)
				return;
			auto e = entt::entity(visual);
			if (r.valid(e))
				if (auto* mr = r.try_get<MeshRendererComponent>(e))
					mr->enabled = false;
		};
		for (const auto& g : gimmicks_) {
			hide(g.sign);
			hide(g.ring);
			hide(g.extra);
			for (uint32_t part : g.parts)
				hide(part);
			for (uint32_t reward : g.rewards)
				hide(reward);
		}
		gimmickSigns_.clear();
	}
	// 吸収で飲んだ建物を街に戻す (見た目と通れなさ。もう飲めない)
	b.buildings.clear();
	for (size_t i = 0; i < life_.props.size(); ++i) {
		auto& prop = life_.props[i];
		if (prop.visual == 0xffffffffu)
			continue;
		auto e = entt::entity(prop.visual);
		if (!r.valid(e))
			continue;
		const auto* name = r.try_get<NameComponent>(e);
		if (!name)
			continue;
		bool building = false;
		ActionOfId(Collection::KeyOfName(name->name), building);
		if (!building)
			continue;
		if (!prop.active)
			if (auto* mr = r.try_get<MeshRendererComponent>(e))
				mr->enabled = true;
		prop.active = false;
		b.buildings.push_back(i);
	}
	BeginBattleFight(r);
}

// 専用の広いステージを作る: マップの外に平らな床 (積み木の街では隠す。確認用の平らな配置で使う)。やり直しでは作り直さない
void ChronoSystem::BuildBattleArena(entt::registry& r) {
	auto& b = battle_;
	const auto& stage = CurrentDevourStage();
	b.arenaRadius = battle_.unit * BattleArenaUnits;
	// マップの範囲 (吸収の対象の物の位置) の +x の端から、半径の数倍離す
	float maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	for (const auto& prop : life_.props) {
		maxX = std::max(maxX, prop.at.x);
		minZ = std::min(minZ, prop.at.z);
		maxZ = std::max(maxZ, prop.at.z);
	}
	if (maxX < -1e8f) {
		maxX = life_.bodies[0].at.x;
		minZ = maxZ = life_.bodies[0].at.z;
	}
	// (積み木の街は広いので、街の柵の外に元の部屋や町が見えないよう、ずっと遠く (BattleArenaFar) に置く。普段の描く範囲の外になる)
	b.arenaAt = {
	    maxX + std::max(b.arenaRadius * BattleArenaGap, (ChallengeBlocksHalf + BattleArenaFar) * battle_.unit), 0,
	    (minZ + maxZ) * .5f};
	if (r.valid(battleArenaEntity_))
		return;
	const float R = b.arenaRadius, floorHalf = R * 1.6f;
	auto make = [&](const char* name, const std::vector<std::array<V, 2>>& list, V color) {
		CityParts::CityBuilder builder;
		for (const auto& box : list)
			builder.Box(CityParts::CityMetal, box[0], box[1]);
		auto& g = builder.geometry[CityParts::CityMetal];
		auto e = r.create();
		r.emplace<NameComponent>(e, name);
		r.emplace<TransformComponent>(e).scale = {1, 1, 1};
		auto& mr = r.emplace<MeshRendererComponent>(e);
		mr.modelHandle = Engine::Renderer::GetInstance()->CreateDynamicMesh(g.vertices, g.indices);
		mr.textureHandle = white_;
		mr.extraTextureHandles = lowMaps_;
		mr.shaderName = "LowpolySurface";
		mr.useCubemap = false;
		mr.color = {color.x, color.y, color.z, 1};
		return e;
	};
	const V c = b.arenaAt;
	battleArenaEntity_ = make(
	    "Chrono Battle floor", {{V{c.x - floorHalf, -4, c.z - floorHalf}, V{c.x + floorHalf, 0, c.z + floorHalf}}},
	    stage.battleColor);
}

// 戦闘の中身を始めから (やり直しもここ)
void ChronoSystem::BeginBattleFight(entt::registry& r) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	body.at = b.startAt;
	body.forward = b.startForward;
	body.mass = b.startMass;
	body.velocity = {};
	body.air = 0;
	// 準備フェーズの取り込み中に時間切れになると、触手 (grabs) が元のマップの物へ伸びたまま残る。行動と体のひずみも全部戻す
	body.action = LifeAction::Idle;
	body.grabs.clear();
	body.target = 0xffffffffu;
	body.committed = false;
	body.actionAge = 0;
	body.pendingGrowth = 0;
	body.liquidForward = body.previousForward = body.forward;
	body.engulfPoint = body.grabOrigin = body.at;
	body.flowShape = {};
	body.fluidOffset = {};
	body.coreLead = {};
	body.coreVelocity = {};
	body.aspect = 1;
	body.aspectVelocity = 0;
	body.motion = 0;
	// 飛びかかりの途中だった場合も止める (元のマップの目標へ体を引っぱり続けないように)
	devourBiteAge_ = -1;
	// 吸い込み中だった物 (devourSucks_) は、元のマップの位置から体へ液体の腕を伸ばして描くので、残ると準備のステージから体が伸びて見える。
	// 戦闘では吸い込みを進めないので全部やめる。水鉄砲の水も消す
	devourSucks_.clear();
	devourSucking_ = false;
	devourSuckClock_ = 0;
	gimmickJets_.clear();
	// 遠くへ飛ぶので、体の粒子を新しい位置に置き直し、カメラもすぐ追いつかせる
	if (auto* t = r.try_get<TransformComponent>(player_))
		t->translate = Write(body.at);
	cameraReady_ = false;
	lifeReseed_ = true;
	b.bag = b.startBag;
	// 確認用: --devour-battle-item=id の物は、袋の中で 40 個に増やす (ステージに少ししか無い物の能力も続けて試せるように)
	{
		const std::string forced = ArgText(L"--devour-battle-item=");
		if (!forced.empty())
			for (auto& bag : b.bag)
				if (bag.id == forced && !bag.items.empty())
					while (bag.items.size() < 40)
						bag.items.push_back(bag.items.front());
	}
	b.time = b.challengeTime;
	if (const wchar_t* arg = wcsstr(GetCommandLineW(), L"--devour-battle-time="))
		b.time = std::max(10.f, float(_wtof(arg + 21)));
	b.phase = BattleStep::Intro;
	b.intro = 0;
	b.clock = 0;
	b.overAge = 0;
	b.cooldown = 0;
	b.guard = 0;
	b.score = b.combo = 0;
	b.comboClock = 0;
	b.timeBonus = 0;
	b.newBest = false;
	b.matrix = {};
	b.matrix.active = HasArg(L"--challenge-matrix");
	weaponCheck_.active = HasArg(L"--weapon-check");
	b.impactMemo.clear();
	b.tally = {};
	b.impactCheck = HasArg(L"--challenge-impact-check");
	b.checkNext = 0;
	b.checkClock = .5f;
	if (b.impactCheck)
		b.time = float(int(BattleAction::Count)) * BattleImpactCheckStep + 7;
	{
		auto best = challengeBests_.find(ChallengeBestKey(CurrentDevourStage().id, b.challenge));
		b.best = best == challengeBests_.end() ? 0 : best->second;
	}
	b.regen = b.fluff = 0;
	b.toolShape = 0;
	b.toolAge = b.toolLeft = 0;
	b.abilityLeft = 0;
	b.abilityHitAt = -1;
	b.velocity = {};
	b.previousFire = true;
	b.shots.clear();
	b.fx.clear();
	b.placed.clear();
	b.pops.clear();
	b.spent = 0;
	b.message.clear();
	b.messageAge = 0;
	// 物のスロットを袋から埋める (最初はルーレットなし)。最初は物の入った一番前のスロットを選ぶ (無ければ飴玉)
	// (配置を決めてから、その配置のステージを見せる)
	BuildChallenge();
	ShowChallengeStage(r);
	// 積み木の街は手前の広場の真ん中から始める
	b.fallSpeed = 0;
	if (BlocksStageActive()) {
		body.at = ArenaPoint(0, ChallengeBlocksStartZ) + V{0, ChallengeBodyGround, 0};
		body.engulfPoint = body.grabOrigin = body.at;
		if (auto* t = r.try_get<TransformComponent>(player_))
			t->translate = Write(body.at);
	}
	// 変形武器: 回数と待ちを戻す。まだ作っていなければ (新しいチャレンジ) 作る画面を開く
	b.weapon.cool = 0;
	b.weaponShape = 0;
	b.weaponForm = 0;
	if (!b.crafted)
		OpenWeaponCraft();
	++b.tries;
	lifeHitStop_ = 0;
	V facing = body.forward;
	facing.y = 0;
	if (Length(facing) > .01f)
		yaw_ = std::atan2(facing.x, facing.z);
	pitch_ = .22f;
	Music::Play(Music::Battle);
}

// 結果画面から戦闘だけやり直す
void ChronoSystem::RetryDevourBattle(entt::registry& r) {
	for (auto e : resultHidden_)
		if (r.valid(e) && r.all_of<MeshRendererComponent>(e))
			r.get<MeshRendererComponent>(e).enabled = true;
	resultHidden_.clear();
	finished_ = won_ = false;
	finishAge_ = 0;
	resultCameraAge_ = 0;
	resultMeasured_ = false;
	resultScreenSet_ = false;
	BeginBattleFight(r);
}



// チャレンジの得点: コンボの倍率をかけて足し、その場所に文字を浮かべる。時間が終わったあとは数えない
int ChronoSystem::AddChallengeScore(int points, V at, const std::string& label) {
	auto& b = battle_;
	if (points <= 0 || b.phase != BattleStep::Fight)
		return 0;
	b.combo = b.comboClock > 0 ? b.combo + 1 : 1;
	b.comboClock = ChallengeComboWindow;
	const int added = int(std::round(float(points) * ChallengeComboScale(b.combo)));
	b.score += added;
	b.pops.push_back(
	    {at, (label.empty() ? std::string() : label + " ") + "+" + std::to_string(added), 0, {1, .85f, .3f, 1}});
	return added;
}

// ---- 衝撃 (能力の当たり) ----
// 衝撃を受ける相手の一覧: チャレンジの物
std::vector<ChronoSystem::BattleTarget> ChronoSystem::CollectBattleTargets() const {
	std::vector<BattleTarget> out;
	// (変形武器を作る画面の見本の撃ち方は、相手を持たない: 隠してあるチャレンジの物を狙わない)
	if (battle_.phase == BattleStep::Craft)
		return out;
	out.reserve(battle_.blocks.size());
	// 積み木 (硬い積み木は重い相手)
	for (const auto& blk : battle_.blocks)
		if (blk.alive)
			out.push_back(
			    {BattleTargetKind::Block, blk.id, blk.at, blk.size * .6f, blk.type == BlockType::Hard, false});
	return out;
}

bool ChronoSystem::NearestBattleTarget(
    V from, float maxDistance, BattleTarget& out, const std::vector<uint32_t>* exclude) const {
	float best = maxDistance;
	bool found = false;
	for (const auto& t : CollectBattleTargets()) {
		if (exclude && std::find(exclude->begin(), exclude->end(), t.id) != exclude->end())
			continue;
		const float d = Length(t.at - from);
		if (d < best) {
			best = d;
			out = t;
			found = true;
		}
	}
	return found;
}

// 衝撃を出す: 形に入った相手それぞれに効き目を与える。数え方 (確認用) も残す
int ChronoSystem::EmitBattleImpact(
    GameContext& ctx, const BattleImpact& impact, std::vector<BattleTarget>* touched,
    const std::vector<uint32_t>* exclude) {
	auto& b = battle_;
	// (作る画面の見本の撃ち方は、何にも効かない: チャレンジの物を始まる前に壊さない)
	if (b.phase == BattleStep::Craft)
		return 0;
	const size_t a = std::min(size_t(impact.action), size_t(BattleAction::Count) - 1);
	++b.tally.emitted[a];
	++b.tally.effects[a][size_t(impact.effect)];
	int count = 0;
	for (const auto& t : CollectBattleTargets()) {
		if (impact.limit > 0 && count >= impact.limit)
			break;
		if (impact.only != 0 && t.id != impact.only)
			continue;
		if (exclude && std::find(exclude->begin(), exclude->end(), t.id) != exclude->end())
			continue;
		if ((impact.spareBig && t.big) || (impact.onlyFree && t.stopped))
			continue;
		if (!BattleImpactTouches(impact, t.at, t.radius))
			continue;
		// 同じ衝撃は、同じ相手に rehit 秒たつまで当てない
		BattleImpactMemo* memo = nullptr;
		if (impact.source != 0 && impact.rehit > 0) {
			for (auto& m : b.impactMemo)
				if (m.source == impact.source && m.target == t.id)
					memo = &m;
			if (memo && memo->left > 0)
				continue;
		}
		bool hit = false;
		switch (t.kind) {
		case BattleTargetKind::Block: {
			const size_t k = size_t(t.id - ChallengeBlockIdBase);
			if (k < b.blocks.size())
				hit = ApplyImpactToBlock(ctx, b.blocks[k], impact);
			break;
		}
		}
		if (!hit)
			continue;
		if (impact.source != 0 && impact.rehit > 0) {
			if (memo)
				memo->left = impact.rehit;
			else
				b.impactMemo.push_back({impact.source, t.id, impact.rehit});
		}
		++count;
		if (touched)
			touched->push_back(t);
		if (impact.label)
			b.pops.push_back({t.at + V{0, t.radius, 0}, impact.label, 0, ActionColor(impact.action)});
	}
	if (count > 0)
		++b.tally.touched[a];
	return count;
}

// 全部の能力の衝撃の数を書く (--challenge-impact-check)。一度も衝撃を出さなかった能力は missing に並べる
void ChronoSystem::WriteImpactCheck() const {
	const auto& b = battle_;
	std::filesystem::create_directories("tests/out");
	std::ofstream out("tests/out/challenge-impacts.txt");
	static constexpr const char* keys[] = {
	    "throw",   "ride",   "smash",  "guard",  "heal",  "heli", "water", "roll",   "spin",   "sword", "fly",
	    "spring",  "vacuum", "rc",     "fu",     "kyo",   "kei",  "gin",   "kin",    "kaku",   "hi",    "ou",
	    "marbles", "tower",  "domino", "paint",  "decoy", "coin", "disc",  "bounce", "homing", "fan",   "pierce",
	    "pause",   "read",   "cage",   "sticky", "erase", "tea",  "roly",  "beckon", "brew",   "fluff", "smell"};
	static_assert(std::size(keys) == size_t(BattleAction::Count));
	std::string missing;
	int ok = 0;
	out << "step=" << BattleImpactCheckStep << '\n';
	for (size_t a = 0; a < size_t(BattleAction::Count); ++a) {
		out << keys[a] << " emitted=" << b.tally.emitted[a] << " touched=" << b.tally.touched[a] << " effects=";
		bool first = true;
		for (size_t k = 0; k < size_t(ImpactEffect::Count); ++k)
			if (b.tally.effects[a][k]) {
				out << (first ? "" : ",") << ImpactEffectName(ImpactEffect(k)) << ':' << b.tally.effects[a][k];
				first = false;
			}
		out << '\n';
		if (b.tally.emitted[a] > 0)
			++ok;
		else
			missing += std::string(missing.empty() ? "" : ",") + keys[a];
	}
	out << "summary emitted=" << ok << "/" << int(BattleAction::Count)
	    << " missing=" << (missing.empty() ? "none" : missing) << '\n';
}


// 能力を始める: 体の形を変え (投げる以外)、能力の時間と強さを決める。当たりは TickBattleAbility で進める
void ChronoSystem::StartBattleAbility(GameContext& ctx, BattleAction action, const BattleItem& item) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	const auto& stage = CurrentDevourStage();
	const float R = DevourBodyRadius(body.mass);
	const V aim = Forward(yaw_, 0);
	body.forward = aim;
	// 体を形に変える (同じ形ならそのまま続ける)
	auto setTool = [&](int shape, float seconds) {
		if (b.toolShape != shape) {
			b.toolShape = shape;
			b.toolAge = 0;
			b.toolPose = b.toolPrevious = {};
		}
		b.toolLeft = seconds;
	};
	b.ability = action;
	b.abilityAge = 0;
	b.abilityLeft = BattleActionTime(action);
	b.abilityPower = action == BattleAction::Heal ? BattlePower(stage, body.mass) : ChallengePowerNow();
	b.abilityItem = BattleItemScale(b.unit, item.radius);
	b.abilityTick = 0;
	b.abilityHit = false;
	b.abilityHitAt = -1;
	b.dashDir = aim;
	b.abilitySource = b.impactSerial++;
	// 衝撃を作る (能力・効き目・形。中心は体、向きは狙っている向き)
	auto make = [&](ImpactEffect effect, ImpactShape shape) {
		BattleImpact i;
		i.action = action;
		i.effect = effect;
		i.shape = shape;
		i.at = body.at;
		i.dir = aim;
		return i;
	};
	// 自分に効く能力・場所に置く能力も、体のまわり (置いた所) に衝撃を出して知らせる (チャレンジの物が反応できるように)
	auto self = [&](float reach) {
		BattleImpact i = make(ImpactEffect::Self, ImpactShape::Sphere);
		i.reach = reach;
		EmitBattleImpact(ctx, i);
	};
	auto zone = [&](V at, float reach) {
		BattleImpact i = make(ImpactEffect::Zone, ImpactShape::Sphere);
		i.at = at;
		i.reach = reach;
		EmitBattleImpact(ctx, i);
	};
	switch (action) {
	case BattleAction::Throw: {
		const float speed = battle_.unit * 11;
		BattleShot shot;
		shot.at = body.at + aim * (R * .8f) + V{0, R * .4f, 0};
		shot.velocity = aim * speed + V{0, speed * .12f, 0};
		shot.visual = item.visual;
		shot.push = 3;
		shot.radius = std::max(item.radius, battle_.unit * .2f);
		shot.damage = BattleThrowDamage * b.abilityPower * b.abilityItem;
		shot.by = action;
		// (チャレンジ: 大きい物ほど当たる数が多く、床に落ちても転がって当たり続ける)
		shot.hits = ChallengeThrowHits(b.abilityItem);
		b.shots.push_back(shot);
		b.abilityLeft = 0;
		PlayDevourSound(devourSounds_.pounce, .6f, 1.1f);
		break;
	}
	case BattleAction::Ride:
	case BattleAction::Rc:
		setTool(BattleCarShape, b.abilityLeft + .25f);
		PlayDevourSound(devourSounds_.pounce, .8f, .8f);
		LifeImpact(ctx, ImpactTireLaunch, body.at, aim, 1);
		break;
	case BattleAction::Roll:
		setTool(GimmickBallShape, b.abilityLeft + .2f);
		PlayDevourSound(devourSounds_.pounce, .8f, .6f);
		LifeImpact(ctx, ImpactTireLaunch, body.at, aim, 1);
		break;
	case BattleAction::Fly:
		setTool(GimmickPlaneShape, b.abilityLeft + .2f);
		PlayDevourSound(devourSounds_.pounce, .7f, 1.3f);
		break;
	case BattleAction::Smash:
		setTool(BattleHammerShape, b.abilityLeft);
		b.abilityHitAt = BattleSmashWindup + BattleSmashSwing;
		PlayDevourSound(devourSounds_.morph, .5f, 1.2f);
		break;
	case BattleAction::Sword:
		setTool(BattleSwordShape, b.abilityLeft);
		b.abilityHitAt = .16f;
		PlayDevourSound(devourSounds_.morph, .5f, 1.4f);
		break;
	case BattleAction::Spring:
		setTool(BattleSpringShape, b.abilityLeft);
		b.abilityHitAt = .65f;
		PlayDevourSound(devourSounds_.morph, .5f, .9f);
		break;
	case BattleAction::Guard:
		setTool(BattleShieldShape, BattleGuardTime);
		b.guard = BattleGuardTime;
		self(R * 1.2f);
		PlayDevourSound(devourSounds_.morph, .6f);
		// (盾を構えて前へ短く突進する盾タックル)
		b.abilityLeft = ChallengeTackleTime;
		LifeImpact(ctx, ImpactTireLaunch, body.at, aim, .7f);
		break;
	case BattleAction::Heal:
		setTool(BattleHeartShape, b.abilityLeft);
		self(R * 1.2f);
		PlayDevourSound(devourSounds_.rankUp, .6f);
		break;
	case BattleAction::Heli:
		setTool(GimmickHeliShape, b.abilityLeft);
		PlayDevourSound(devourSounds_.morph, .5f, 1.3f);
		break;
	case BattleAction::Water:
		setTool(GimmickGunShape, b.abilityLeft);
		PlayDevourSound(devourSounds_.morph, .5f, 1.1f);
		break;
	case BattleAction::Spin:
		setTool(GimmickKomaShape, b.abilityLeft);
		PlayDevourSound(devourSounds_.morph, .5f, 1.6f);
		break;
	case BattleAction::Vacuum:
		setTool(BattleVacuumShape, b.abilityLeft);
		PlayDevourSound(devourSounds_.vacuumLoop, .4f);
		break;
	// 将棋の駒: 駒の形になり、駒の動きで当てる (王は形でいる 3 秒間ずっと無敵)
	case BattleAction::Fu:
	case BattleAction::Gin:
	case BattleAction::Kin:
	case BattleAction::Kaku:
	case BattleAction::Hi:
	case BattleAction::Ou:
		setTool(BattleShogiShape, b.abilityLeft + .15f);
		b.abilityHitAt = action == BattleAction::Ou ? .05f : .12f;
		PlayDevourSound(devourSounds_.morph, .5f, 1.5f);
		break;
	case BattleAction::Kyo:
		setTool(BattleShogiShape, b.abilityLeft + .15f);
		PlayDevourSound(devourSounds_.pounce, .8f, 1.2f);
		LifeImpact(ctx, ImpactTireLaunch, body.at, aim, 1);
		break;
	// 置く・まく物: 置いた物は TickBattlePlaced で進める
	case BattleAction::Marbles:
		// ビー玉: 前の扇の範囲へ、ばらばらに転がるようにまく
		for (int i = 0; i < BattleMarbleCount; ++i) {
			BattlePlaced m;
			m.kind = action;
			m.visual = item.visual;
			m.itemRadius = item.radius;
			m.size = battle_.unit * .28f;
			m.radius = battle_.unit * .45f;
			const V dir = Forward(yaw_ + BU(-.7f, .7f), 0);
			const float speed = battle_.unit * BU(5, 11);
			m.at = body.at + dir * (R * .8f) + V{0, R * .3f, 0};
			m.velocity = dir * speed + V{0, battle_.unit * BU(2, 5), 0};
			m.life = BattleMarbleLife;
			m.power = b.abilityPower;
			b.placed.push_back(m);
		}
		zone(body.at + aim * (R * 3), R * 3);
		PlayDevourSound(devourSounds_.note, .6f, 1.6f);
		break;
	case BattleAction::Tower: {
		// 積み木の塔: 前に横長の壁を建てる。前に物があれば、その手前に建てる。倒れる向きは、物の多い側 (前か後ろ)
		const float size = std::max(R * .55f, battle_.unit * .5f);
		float ahead = R * BattleTowerAhead;
		V fall = aim;
		{
			const V side{aim.z, 0, -aim.x};
			int front = 0, back = 0;
			for (const auto& t : CollectBattleTargets()) {
				V d = t.at - body.at;
				d.y = 0;
				const float along = Dot(d, aim), across = std::abs(Dot(d, side));
				if (across > R * BattleTowerHalf + t.radius)
					continue;
				if (along > 0 && along < ahead + size + t.radius)
					ahead = std::max(R * .9f, std::min(ahead, along - t.radius - size * .6f - R * ChallengeTowerGap));
				if (along > -R * 2 && along < R * 6)
					(along >= ahead ? front : back) += 1;
			}
			if (back > front)
				fall = aim * -1.f;
		}
		BattlePlaced t;
		t.kind = action;
		t.visual = item.visual;
		t.itemRadius = item.radius;
		t.at = body.at + aim * ahead;
		t.at.y = InkGround(t.at);
		t.dir = fall;
		t.radius = R * BattleTowerHalf;
		t.size = size;
		t.life = BattleTowerLife;
		t.power = b.abilityPower;
		b.placed.push_back(t);
		zone(t.at, t.radius);
		PlayDevourSound(devourSounds_.morph, .6f, .8f);
		LifeImpact(ctx, ImpactStomp, t.at, {0, 1, 0}, .4f);
		break;
	}
	case BattleAction::Domino:
		// ドミノ: ドミノの形で前へ倒れ込みながら、前へ一列に並べたドミノを順に倒す
		setTool(GimmickDominoShape, .9f);
		for (int i = 0; i < BattleDominoCount; ++i) {
			BattlePlaced d;
			d.kind = action;
			d.visual = item.visual;
			d.itemRadius = item.radius;
			d.index = i;
			d.dir = aim;
			d.at = body.at + aim * (R * (1.6f + BattleDominoGap * float(i)));
			d.at.y = InkGround(d.at);
			d.size = std::max(R * 1.4f, battle_.unit * .9f);
			d.radius = R * .9f;
			d.life = BattleDominoStart + BattleDominoStep * float(i) + 1.2f;
			d.power = b.abilityPower;
			b.placed.push_back(d);
		}
		PlayDevourSound(devourSounds_.morph, .5f, 1.3f);
		break;
	case BattleAction::Paint: {
		// 絵の具: 足元の床をまるく塗る (色は毎回変える)
		static const Engine::Vector4 paints[] = {
		    {.95f, .3f, .35f, 1},
		    {.3f, .55f, .95f, 1},
		    {.98f, .85f, .25f, 1},
		    {.4f, .85f, .4f, 1},
		    {.8f, .45f, .9f, 1}};
		static int turn = 0;
		BattlePlaced c;
		c.kind = action;
		c.at = body.at;
		c.at.y = InkGround(c.at);
		c.radius = R * BattlePaintRadius;
		c.life = BattlePaintLife;
		c.color = paints[(turn++) % 5];
		b.placed.push_back(c);
		zone(c.at, c.radius);
		PlayDevourSound(devourSounds_.splat, .6f);
		break;
	}
	case BattleAction::Decoy: {
		// ぬいぐるみ: 少し前に置く。最後に弾けてまわりを吹き飛ばす
		BattlePlaced d;
		d.kind = action;
		d.visual = item.visual;
		d.itemRadius = item.radius;
		d.at = body.at + aim * (R * BattleDecoyAhead);
		d.at.y = InkGround(d.at);
		d.size = std::max(R * 1.1f, battle_.unit * .8f);
		d.radius = R * BattleDecoyReach;
		d.life = BattleDecoyLife;
		d.power = b.abilityPower;
		b.placed.push_back(d);
		zone(d.at, d.radius);
		PlayDevourSound(devourSounds_.pounce, .6f, .9f);
		break;
	}
	// 飛び道具: 投げた物の飛び方を変える (UpdateDevourBattle の飛んでいる物の更新)
	case BattleAction::Coin:
	case BattleAction::Disc:
	case BattleAction::Bounce:
	case BattleAction::Homing:
	case BattleAction::Fan:
	case BattleAction::Pierce: {
		const float u = battle_.unit, power = b.abilityPower * b.abilityItem;
		auto launch = [&](V dir, float speed, float lift) {
			BattleShot s;
			s.mode = action;
			s.by = action;
			s.visual = item.visual;
			s.at = body.at + dir * (R * .9f) + V{0, R * .45f, 0};
			s.velocity = dir * speed + V{0, lift, 0};
			s.push = 3;
			return s;
		};
		const V dir = Forward(yaw_, 0);
		if (action == BattleAction::Coin) {
			auto s = launch(dir, u * 16, 0);
			s.size = u * .8f;
			s.radius = u * .4f;
			s.damage = BattleCoinDamage * power;
			s.hits = BattleCoinHops;
			s.life = BattleCoinLife;
			b.shots.push_back(s);
		} else if (action == BattleAction::Disc) {
			auto s = launch(dir, u * 13, 0);
			s.at.y = InkGround(s.at) + u * .3f;
			s.size = u * 1.5f;
			s.radius = u * .7f;
			s.damage = BattleDiscDamage * power;
			s.hits = BattleDiscHits;
			s.life = BattleDiscLife;
			s.push = 5;
			b.shots.push_back(s);
		} else if (action == BattleAction::Bounce) {
			auto s = launch(dir, u * 10, u * 6);
			s.size = u * .8f;
			s.radius = u * .45f;
			s.damage = BattleBounceDamage * power;
			s.hits = BattleBounceHits;
			s.life = BattleBounceLife;
			s.push = 5;
			b.shots.push_back(s);
		} else if (action == BattleAction::Homing) {
			auto s = launch(dir, u * 9, 0);
			s.size = u * 1.6f;
			s.radius = u * .5f;
			s.damage = BattleHomingDamage * power;
			s.life = BattleHomingLife;
			s.push = 4;
			b.shots.push_back(s);
		} else if (action == BattleAction::Fan) {
			for (int i = 0; i < BattleFanCards; ++i) {
				const float a = (float(i) / float(BattleFanCards - 1) - .5f) * .9f;
				auto s = launch(Forward(yaw_ + a, 0), u * 15, 0);
				s.size = u * 1.f;
				s.radius = u * .45f;
				s.damage = BattleFanDamage * power;
				s.life = 1.4f;
				b.shots.push_back(s);
			}
		} else {
			auto s = launch(dir, u * 18, 0);
			s.size = u * 2.4f;
			s.radius = u * .45f;
			s.damage = BattlePierceDamage * power;
			s.hits = 99;
			s.life = BattlePierceLife;
			s.push = 4;
			b.shots.push_back(s);
		}
		PlayDevourSound(
		    devourSounds_.pounce, .55f,
		    action == BattleAction::Coin     ? 1.8f
		    : action == BattleAction::Pierce ? 1.4f
		                                     : 1.15f);
		break;
	}
	// 止める・弱らせる
	case BattleAction::Pause:
		// リモコン: まわりの物をしばらく止める
		{
			BattleImpact i = make(ImpactEffect::Stun, ImpactShape::Sphere);
			i.reach = 1e9f;
			i.flat = false;
			i.seconds = BattlePauseTime;
			i.bigScale = BattlePauseBoss / BattlePauseTime;
			EmitBattleImpact(ctx, i);
		}
		b.message = "一時停止！";
		b.messageAge = 1.5f;
		PlayDevourSound(devourSounds_.note, .7f, .6f);
		LifeImpact(ctx, ImpactSignalPulse, body.at, {0, 1, 0}, 1);
		break;
	case BattleAction::Read: {
		// まんが: 重い本を前に広げて落とし、範囲の中の物を押しつぶす
		const V at = body.at + aim * (R * 1.5f);
		{
			BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Sphere);
			i.at = at;
			i.reach = R * BattleReadReach;
			i.damage = ChallengeBookDamage * b.abilityPower;
			i.push = 2;
			i.radial = true;
			EmitBattleImpact(ctx, i);
			LifeImpact(ctx, ImpactStomp, at, {0, 1, 0}, .6f);
			PlayDevourSound(devourSounds_.splat, .7f, .7f);
		}
		BattlePlaced m;
		m.kind = action;
		m.visual = item.visual;
		m.itemRadius = item.radius;
		m.at = at;
		m.at.y = InkGround(at);
		m.size = R * .8f;
		m.life = BattleReadTime;
		b.placed.push_back(m);
		PlayDevourSound(devourSounds_.note, .6f, 1.1f);
		break;
	}
	case BattleAction::Cage: {
		// かご: 前の近い相手 1 つを閉じ込める
		// (大きい相手 (重い物) は選ばない)
		bool found = false;
		BattleTarget target;
		float best = R * BattleCageReach;
		for (const auto& t : CollectBattleTargets()) {
			if (t.big)
				continue;
			V d = t.at - body.at;
			d.y = 0;
			const float dist = Length(d);
			if (dist < best && (dist < R * 1.5f || Dot(d * (1 / std::max(.01f, dist)), aim) > .3f)) {
				best = dist;
				target = t;
				found = true;
			}
		}
		BattleImpact i = make(ImpactEffect::Capture, ImpactShape::Sphere);
		i.at = found ? target.at : body.at + aim * (R * 2);
		i.reach = found ? 0.f : R;
		i.seconds = BattleCageTime;
		i.only = found ? target.id : 0;
		i.limit = 1;
		i.label = "閉じ込めた！";
		if (found && EmitBattleImpact(ctx, i) > 0) {
			BattlePlaced c;
			c.kind = action;
			c.visual = item.visual;
			c.itemRadius = item.radius;
			c.target = target.id;
			c.at = target.at;
			c.size = target.radius * 2.6f;
			c.life = BattleCageTime;
			b.placed.push_back(c);
			PlayDevourSound(devourSounds_.hit, .6f, .7f);
		} else {
			if (!found)
				EmitBattleImpact(ctx, i);
			b.message = "閉じ込める相手がいない";
			b.messageAge = 1.f;
		}
		break;
	}
	case BattleAction::Sticky: {
		// ノート: 紙を飛ばして、当たった相手に貼り付けて止める
		const float u = battle_.unit;
		BattleShot s;
		s.mode = action;
		s.by = action;
		s.visual = item.visual;
		s.at = body.at + aim * (R * .9f) + V{0, R * .45f, 0};
		s.velocity = aim * (u * 14);
		s.size = u * 1.2f;
		s.radius = u * .5f;
		s.damage = BattleStickyDamage * b.abilityPower;
		s.life = 2;
		s.push = 1;
		b.shots.push_back(s);
		PlayDevourSound(devourSounds_.pounce, .5f, 1.4f);
		break;
	}
	case BattleAction::Erase:
		setTool(BattleEraserShape, b.abilityLeft);
		b.abilityHitAt = .2f;
		PlayDevourSound(devourSounds_.morph, .5f, 1.2f);
		break;
	case BattleAction::Tea: {
		// 湯のみ: 前の扇の範囲へ熱いお茶をまく。かかった相手はやけどでしばらく削られる
		{
			BattleImpact i = make(ImpactEffect::Burn, ImpactShape::Cone);
			i.reach = R * BattleTeaReach;
			i.cone = .35f;
			i.seconds = BattleBurnTime;
			i.damage = BattleBurnDps * b.abilityPower;
			i.label = "あちっ！";
			EmitBattleImpact(ctx, i);
		}
		for (int k = 0; k < 14; ++k) {
			const V d = Forward(yaw_ + BU(-.55f, .55f), 0);
			b.fx.push_back(
			    {body.at + d * (R * BU(1, BattleTeaReach)) + V{0, R * .3f, 0},
			     R * .22f,
			     0,
			     .4f,
			     {.65f, .75f, .35f, 1}});
		}
		PlayDevourSound(devourSounds_.splat, .6f, 1.2f);
		break;
	}
	// 自分を強くする
	case BattleAction::Roly:
		setTool(BattleDarumaShape, b.abilityLeft);
		self(R * 1.2f);
		b.message = "起き上がりこぼし！";
		b.messageAge = 1.5f;
		PlayDevourSound(devourSounds_.morph, .6f, .7f);
		break;
	case BattleAction::Beckon: {
		// 招き猫: まわりの物を前の 1 か所へ招き寄せる
		const V spot = body.at + aim * (R * 3);
		{
			BattleImpact i = make(ImpactEffect::Pull, ImpactShape::Sphere);
			i.at = spot;
			i.reach = R * BattleBeckonReach;
			i.push = 2;
			i.pullCap = R * 4;
			i.bigScale = .25f;
			EmitBattleImpact(ctx, i);
		}
		BattlePlaced m;
		m.kind = action;
		m.visual = item.visual;
		m.itemRadius = item.radius;
		m.at = spot;
		m.at.y = InkGround(spot);
		m.size = R * 1.2f;
		m.life = 1.5f;
		b.placed.push_back(m);
		PlayDevourSound(devourSounds_.rankUp, .6f, 1.3f);
		break;
	}
	case BattleAction::Brew:
		b.regen = BattleBrewTime;
		self(R * 1.2f);
		b.message = "お茶でひと息… 少しずつ回復";
		b.messageAge = 1.5f;
		PlayDevourSound(devourSounds_.rankUp, .5f, .9f);
		break;
	case BattleAction::Fluff:
		b.fluff = BattleFluffTime;
		self(R * 1.6f);
		b.message = "ふわふわ… 攻撃が当たりにくい";
		b.messageAge = 1.5f;
		PlayDevourSound(devourSounds_.morph, .4f, 1.6f);
		break;
	case BattleAction::Smell: {
		// 靴下: 足元ににおいの雲を置く。中の物を外へ押し出す
		BattlePlaced c;
		c.kind = action;
		c.at = body.at;
		c.at.y = InkGround(c.at);
		c.radius = R * BattleSmellRadius;
		c.life = BattleSmellLife;
		c.visual = item.visual;
		c.itemRadius = item.radius;
		b.placed.push_back(c);
		zone(c.at, c.radius);
		PlayDevourSound(devourSounds_.splat, .5f, .7f);
		break;
	}
	case BattleAction::Kei: {
		// 桂馬: 斜め前へ跳ぶ。左右は近い相手のいる側 (いなければ右)
		const V right{aim.z, 0, -aim.x};
		float side = 1;
		BattleTarget nearest;
		if (NearestBattleTarget(body.at, 1e9f, nearest))
			side = Dot(nearest.at - body.at, right) < 0 ? -1.f : 1.f;
		b.dashDir = Unit(aim * 2 + right * side);
		b.abilityHitAt = kKeiJump + .02f;
		setTool(BattleShogiShape, b.abilityLeft + .15f);
		PlayDevourSound(devourSounds_.pounce, .7f, 1.5f);
		break;
	}
	default:
		break;
	}
	// どのチャレンジでも同じ、自分に効く能力 (回復で時間が増えるなど)
	ApplyChallengeSelf(action);
}

// 能力を進める: 突進の当たり・回り続ける当たり・水を撃つ・吸い込む・ためた一撃 (叩く・剣・ばね)。当たりは全部衝撃で出す
void ChronoSystem::TickBattleAbility(GameContext& ctx, float dt) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	if (b.abilityLeft <= 0)
		return;
	b.abilityAge += dt;
	b.abilityLeft -= dt;
	const float R = DevourBodyRadius(body.mass), unit = battle_.unit, power = b.abilityPower * b.abilityItem;
	auto make = [&](ImpactEffect effect, ImpactShape shape) {
		BattleImpact i;
		i.action = b.ability;
		i.effect = effect;
		i.shape = shape;
		i.at = body.at;
		i.dir = body.forward;
		return i;
	};
	auto around = [&](float reach, float damage, float push) {
		BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Sphere);
		i.reach = reach;
		i.damage = damage;
		i.push = push;
		i.radial = true;
		EmitBattleImpact(ctx, i);
	};
	auto front = [&](float reach, float cone, float damage, float push) {
		BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Cone);
		i.reach = reach;
		i.cone = cone;
		i.damage = damage;
		i.push = push;
		i.radial = true;
		EmitBattleImpact(ctx, i);
	};
	switch (b.ability) {
	case BattleAction::Ride:
	case BattleAction::Rc:
	case BattleAction::Fly:
	case BattleAction::Roll:
	case BattleAction::Kyo: {
		// 突進: 触れた相手に当てる。転がるは同じ相手に何度も当たる (0.35 秒ごと)。香車は止まらずに突き抜ける
		const float damage = (b.ability == BattleAction::Roll  ? BattleRollDamage
		                      : b.ability == BattleAction::Fly ? BattleFlyDamage
		                      : b.ability == BattleAction::Rc  ? BattleRcDamage
		                      : b.ability == BattleAction::Kyo ? BattleKyoDamage
		                                                       : BattleRideDamage) *
		                     b.abilityPower;
		BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Sphere);
		i.reach = R;
		i.dir = b.dashDir;
		i.damage = damage;
		i.push = 5;
		i.source = b.abilitySource;
		i.rehit = b.ability == BattleAction::Roll ? .35f : 99.f;
		EmitBattleImpact(ctx, i);
		break;
	}
	case BattleAction::Guard: {
		// 盾タックル: 盾の前に触れた物に強く当てて押し出す (同じ物には 1 回)
		BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Cone);
		i.dir = b.dashDir;
		i.reach = R * ChallengeTackleReach;
		i.cone = .2f;
		i.damage = ChallengeTackleDamage * b.abilityPower;
		i.push = 6;
		i.radial = true;
		i.source = b.abilitySource;
		i.rehit = 99;
		EmitBattleImpact(ctx, i);
		break;
	}
	case BattleAction::Heli:
	case BattleAction::Spin:
		// 回る翼・こま: 0.2 秒ごとにまわりへ当てて弾き飛ばす
		b.abilityTick -= dt;
		if (b.abilityTick <= 0) {
			b.abilityTick = .2f;
			if (b.ability == BattleAction::Heli)
				around(R * BattleHeliReach, BattleHeliDamage * power, 2);
			else
				around(R * BattleSpinReach, BattleSpinDamage * power, 5);
		}
		break;
	case BattleAction::Water:
		// 水鉄砲: 前へ水の粒を撃ち続ける (当たると強く押し流す)
		b.abilityTick -= dt;
		if (b.abilityTick <= 0 && b.abilityAge > .2f) {
			b.abilityTick = .07f;
			const V dir = Forward(yaw_ + BU(-.06f, .06f), 0);
			const float speed = unit * 18;
			BattleShot shot;
			shot.at = body.at + dir * (R * 1.2f) + V{0, R * .5f, 0};
			shot.velocity = dir * speed + V{0, speed * .05f, 0};
			shot.style = 2;
			shot.by = BattleAction::Water;
			shot.radius = unit * .22f;
			shot.damage = BattleWaterDamage * power;
			shot.push = 5;
			b.shots.push_back(shot);
		}
		break;
	case BattleAction::Vacuum: {
		// 掃除機: 前の相手を吸い寄せて少しずつ削り、落ちた物も吸い込む
		const float reach = R * BattleVacuumReach;
		{
			BattleImpact i = make(ImpactEffect::Pull, ImpactShape::Cone);
			i.reach = reach;
			i.cone = .3f;
			i.push = 6 * dt;
			i.bigScale = .2f;
			EmitBattleImpact(ctx, i);
		}
		b.abilityTick -= dt;
		if (b.abilityTick <= 0) {
			b.abilityTick = .2f;
			front(reach, .3f, BattleVacuumDamage * power, -1);
		}
		break;
	}
	case BattleAction::Fu:
	case BattleAction::Kei:
	case BattleAction::Gin:
	case BattleAction::Kin:
	case BattleAction::Kaku:
	case BattleAction::Hi:
	case BattleAction::Ou: {
		// 将棋の駒: 駒の動ける向きへ一度に当てる。突き (歩・銀・金) は近く、ビーム (角・飛車) はステージの端まで届く
		if (b.abilityHit || b.abilityAge < b.abilityHitAt)
			break;
		b.abilityHit = true;
		const V f = Forward(yaw_, 0), rt{f.z, 0, -f.x};
		const float power2 = b.abilityPower;
		// 向き dir へ長さ len・太さ width の線の上に当て、線に沿って光の粒を並べる
		auto line = [&](V dir, float len, float width, float damage, float push) {
			dir = Unit(dir);
			const V from = body.at;
			BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Segment);
			i.to = from + dir * len;
			i.dir = dir;
			i.width = width;
			i.damage = damage;
			i.push = push;
			EmitBattleImpact(ctx, i);
			const int dots = std::clamp(int(len / (R * .7f)), 2, 40);
			for (int k = 1; k <= dots; ++k)
				b.fx.push_back(
				    {from + dir * (len * float(k) / float(dots)) + V{0, R * .4f, 0}, R * .28f, 0, .3f,
				     ActionColor(b.ability)});
		};
		const float thrust = R * BattleThrustLength,
		            beam = BlocksStageActive() ? ChallengeBlocksHalf * 2 * b.unit
		                   : b.arenaRadius > 0 ? b.arenaRadius * 2
		                                       : R * 40;
		const V back = f * -1.f, left = rt * -1.f;
		switch (b.ability) {
		case BattleAction::Fu:
			line(f, thrust * 1.2f, R * BattleThrustWidth, BattleFuDamage * power2, 7);
			LifeImpact(ctx, ImpactBladeHit, body.at + f * R, f, 1);
			break;
		case BattleAction::Gin:
			for (const V& d : {f, f + rt, f + left, back + rt, back + left})
				line(d, thrust, R * BattleThrustWidth, BattleGinDamage * power2, 5);
			LifeImpact(ctx, ImpactBladeHit, body.at, f, 1);
			break;
		case BattleAction::Kin:
			for (const V& d : {f, f + rt, f + left, rt, left, back})
				line(d, thrust, R * BattleThrustWidth, BattleKinDamage * power2, 5);
			LifeImpact(ctx, ImpactBladeHit, body.at, f, 1);
			break;
		case BattleAction::Kaku:
			for (const V& d : {f + rt, f + left, back + rt, back + left})
				line(d, beam, R * BattleBeamWidth, BattleKakuDamage * power2, 4);
			LifeImpact(ctx, ImpactStructureHit, body.at, f, 1);
			break;
		case BattleAction::Hi:
			for (const V& d : {f, rt, left, back})
				line(d, beam, R * BattleBeamWidth, BattleHiDamage * power2, 4);
			LifeImpact(ctx, ImpactStructureHit, body.at, f, 1);
			break;
		case BattleAction::Kei:
			around(R * BattleKeiReach, BattleKeiDamage * power2, 6);
			LifeImpact(ctx, ImpactStomp, body.at, {0, 1, 0}, 1);
			break;
		case BattleAction::Ou:
			around(R * BattleOuReach, BattleOuDamage * power2, 9);
			LifeImpact(ctx, ImpactStructureBreak, body.at, {0, 1, 0}, 1);
			for (int k = 0; k < 16; ++k) {
				const float a = 6.2831853f * float(k) / 16;
				b.fx.push_back(
				    {body.at + V{std::cos(a), 0, std::sin(a)} * (R * BattleOuReach) + V{0, R * .4f, 0}, R * .4f, 0, .4f,
				     ActionColor(b.ability)});
			}
			break;
		default:
			break;
		}
		PlayDevourSound(devourSounds_.hit, .8f, b.ability == BattleAction::Ou ? .7f : 1.2f);
		break;
	}
	case BattleAction::Erase:
		// 消しゴム: こすった前の相手を弱らせる (受ける当たりが増える)
		if (!b.abilityHit && b.abilityAge >= b.abilityHitAt) {
			b.abilityHit = true;
			BattleImpact i = make(ImpactEffect::Weaken, ImpactShape::Cone);
			i.reach = R * BattleEraseReach;
			i.cone = -.1f;
			i.seconds = BattleWeakTime;
			i.damage = BattleEraseDamage * b.abilityPower;
			i.push = 2;
			i.label = "うすくなった";
			EmitBattleImpact(ctx, i);
			PlayDevourSound(devourSounds_.hit, .5f, 1.5f);
		}
		break;
	case BattleAction::Smash:
	case BattleAction::Sword:
	case BattleAction::Spring:
		// ためた一撃: 振り下ろした・振り抜いた・着地した瞬間に 1 回当てる
		if (!b.abilityHit && b.abilityHitAt >= 0 && b.abilityAge >= b.abilityHitAt) {
			b.abilityHit = true;
			if (b.ability == BattleAction::Smash) {
				front(R * BattleSmashReach, -.2f, BattleSmashDamage * power, 4);
				LifeImpact(ctx, ImpactHammerHit, body.at + body.forward * R, body.forward, 1);
				PlayDevourSound(devourSounds_.hit, .8f, .8f);
			} else if (b.ability == BattleAction::Sword) {
				front(R * BattleSwordReach, -.15f, BattleSwordDamage * power, 3);
				LifeImpact(ctx, ImpactBladeHit, body.at + body.forward * R, body.forward, 1);
				PlayDevourSound(devourSounds_.hit, .7f, 1.4f);
			} else {
				around(R * BattleSpringReach, BattleSpringDamage * power, 7);
				LifeImpact(ctx, ImpactStomp, body.at, {0, 1, 0}, 1);
				PlayDevourSound(devourSounds_.bounce, .9f, .8f);
			}
		}
		break;
	default:
		break;
	}
}

// 置いた物を進める: ビー玉の罠・積み木の塔の壁と崩れ・ドミノが順に倒れる・においの雲・ぬいぐるみが弾ける
void ChronoSystem::TickBattlePlaced(GameContext& ctx, float dt) {
	auto& b = battle_;
	const float unit = battle_.unit;
	for (auto& pl : b.placed) {
		pl.age += dt;
		auto make = [&](ImpactEffect effect, ImpactShape shape) {
			BattleImpact i;
			i.action = pl.kind;
			i.effect = effect;
			i.shape = shape;
			i.at = pl.at;
			i.dir = pl.dir;
			return i;
		};
		switch (pl.kind) {
		case BattleAction::Marbles:
			// 転がって止まる。止まっていない相手が踏んだら転ばせて消える
			if (!pl.landed) {
				pl.velocity.y -= unit * 30 * dt;
				pl.at = pl.at + pl.velocity * dt;
				const float floor = InkGround(pl.at) + pl.size * .5f;
				if (pl.at.y <= floor) {
					pl.at.y = floor;
					pl.velocity.y = 0;
					pl.velocity.x *= std::exp(-3 * dt);
					pl.velocity.z *= std::exp(-3 * dt);
					if (Length(pl.velocity) < unit * .5f)
						pl.landed = true;
				}
			}
			if (!pl.done) {
				BattleImpact i = make(ImpactEffect::Stun, ImpactShape::Sphere);
				i.reach = pl.radius;
				i.seconds = BattleMarbleStun;
				i.damage = BattleMarbleDamage * pl.power;
				i.spareBig = i.onlyFree = true;
				i.limit = 1;
				i.label = "すべった！";
				if (EmitBattleImpact(ctx, i) > 0)
					pl.done = true;
			}
			break;
		case BattleAction::Tower: {
			// 壁: 相手は塔を通り抜けられない。時間が来たら前へ崩れて当てる
			const V side{pl.dir.z, 0, -pl.dir.x};
			{
				BattleImpact i = make(ImpactEffect::Wall, ImpactShape::Segment);
				i.at = pl.at - side * pl.radius;
				i.to = pl.at + side * pl.radius;
				i.width = pl.size * .6f;
				EmitBattleImpact(ctx, i);
			}
			if (!pl.done && pl.age >= pl.life - .3f) {
				pl.done = true;
				BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Strip);
				i.width = pl.radius;
				i.back = pl.size;
				i.reach = pl.size + unit * 4;
				i.damage = BattleTowerDamage * pl.power;
				i.push = 6;
				EmitBattleImpact(ctx, i);
				LifeImpact(ctx, ImpactStructureBreak, pl.at, pl.dir, .8f);
				PlayDevourSound(devourSounds_.shatter, .8f, 1.1f);
			}
			break;
		}
		case BattleAction::Domino: {
			// 1 枚ずつ順に倒れ、倒れた所に当てる
			const float fall = BattleDominoStart + BattleDominoStep * float(pl.index);
			if (!pl.done && pl.age >= fall + .12f) {
				pl.done = true;
				BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Sphere);
				i.at = pl.at + pl.dir * (pl.size * .6f);
				i.reach = pl.radius;
				i.damage = BattleDominoDamage * pl.power;
				i.push = 4;
				EmitBattleImpact(ctx, i);
				if (pl.index % 3 == 2)
					PlayDevourSound(devourSounds_.hit, .4f, 1.6f);
			}
			break;
		}
		case BattleAction::Smell: {
			// においの雲: 中にいる相手を外へ押し出す
			BattleImpact i = make(ImpactEffect::PushOut, ImpactShape::Sphere);
			i.reach = pl.radius;
			i.push = std::min(1.f, dt * 8);
			EmitBattleImpact(ctx, i);
			break;
		}
		case BattleAction::Decoy:
			if (!pl.done && pl.age >= pl.life - .05f) {
				pl.done = true;
				BattleImpact i = make(ImpactEffect::Hit, ImpactShape::Sphere);
				i.reach = pl.radius;
				i.damage = BattleDecoyDamage * pl.power;
				i.push = 7;
				i.radial = true;
				EmitBattleImpact(ctx, i);
				for (int k = 0; k < 14; ++k) {
					const float a = 6.2831853f * float(k) / 14;
					b.fx.push_back(
					    {pl.at + V{std::cos(a), 0, std::sin(a)} * pl.radius + V{0, pl.size * .4f, 0},
					     pl.size * .25f,
					     0,
					     .35f,
					     {1, .95f, .9f, 1}});
				}
				LifeImpact(ctx, ImpactStructureBreak, pl.at, {0, 1, 0}, .8f);
				PlayDevourSound(devourSounds_.shatter, .8f, .9f);
			}
			break;
		default:
			break;
		}
	}
	b.placed.erase(
	    std::remove_if(
	        b.placed.begin(), b.placed.end(),
	        [](const BattlePlaced& pl) { return pl.age > pl.life || (pl.kind == BattleAction::Marbles && pl.done); }),
	    b.placed.end());
}

// ステージの物のモデルを好きな向きと大きさで描く (height は描く大きさ (高さか幅の大きい方)、yaw は向き、tilt は前へ倒す角度)。
// 大きさはモデルの高さと幅から決める (物の半径はギミックの当たりに合わせて大きくしてある物があり、半径からだと小さく描かれる)
void ChronoSystem::DrawBattleProp(GameContext& ctx, uint32_t visual, V at, float height, float yaw, float tilt) {
	auto found = cityVisuals_.find(visual);
	if (found == cityVisuals_.end())
		return;
	const auto& v = found->second;
	const float big = std::max(v.height, 2 * (std::max(v.half.x, v.half.z) + v.round)),
	            shrink = height / std::max(.01f, big);
	Engine::Transform t;
	t.scale = EV(v.scale * shrink);
	t.rotate = EV(V{v.rotate.x + tilt, yaw, v.rotate.z});
	t.translate = EV(at - v.center * shrink + V{0, v.height * shrink * .5f, 0});
	ctx.renderer->DrawMeshInstanced(v.mesh, v.texture, t, v.color, v.shader, v.extra);
}

// 戦闘の毎フレームの更新
void ChronoSystem::UpdateDevourBattle(entt::registry& r, Player& p, GameContext& ctx) {
	auto& b = battle_;
	auto& body = life_.bodies[0];
	auto& ink = r.get<InkPlayer>(player_);
	const auto& stage = CurrentDevourStage();
	const float dt = std::clamp(ctx.dt, 0.f, .05f);
	b.frameDt = dt;
	if (lifeHitStop_ > 0) {
		lifeHitStop_ -= dt;
		ctx.renderer->SetWorldFrozen(true);
		InkCamera(r, p, ctx);
		return;
	}
	ctx.renderer->SetWorldFrozen(false);
	Engine::WindowDX::SetCursorVisible(b.phase == BattleStep::Craft);
	// 変形武器を作る画面 (決めたら始まりの演出へ)
	if (b.phase == BattleStep::Craft) {
		UpdateWeaponCraft(ctx, dt);
		if (b.phase == BattleStep::Craft)
			TickCraftPreview(ctx, dt);
	}
	CraftScreenOpen() = b.phase == BattleStep::Craft;
	life_.age += dt;
	b.messageAge = std::max(0.f, b.messageAge - dt);
	const bool autoPlay = HasArg(L"--devour-battle-auto");
	const float R = DevourBodyRadius(body.mass), unit = battle_.unit;
	// カメラ
	if (ctx.input && b.phase != BattleStep::Over && b.phase != BattleStep::Craft) {
		float y, x;
		Controls::Look(dt, y, x);
		yaw_ += ctx.input->GetMouseDeltaX() * .0025f + y;
		pitch_ = std::clamp(pitch_ + ctx.input->GetMouseDeltaY() * .0025f + x, -.7f, .85f);
	}
	// 始まりの演出
	if (b.phase == BattleStep::Intro) {
		b.intro += dt;
		if (b.intro >= ChallengeIntro) {
			b.phase = BattleStep::Fight;
			b.message = ChallengeOf(b.challenge).goal;
			b.messageAge = 2.5f;
		}
	}
	b.comboClock = std::max(0.f, b.comboClock - dt);
	if (b.comboClock <= 0)
		b.combo = 0;
	for (auto& m : b.impactMemo)
		m.left -= dt;
	b.impactMemo.erase(
	    std::remove_if(b.impactMemo.begin(), b.impactMemo.end(), [](const BattleImpactMemo& m) { return m.left <= 0; }),
	    b.impactMemo.end());
	// 移動 (突進の間は向きに進む)
	V move{};
	if (b.phase == BattleStep::Fight) {
		// (リモコンで時計が止まっている間は、チャレンジの時間が進まない)
		if (b.timeStop > 0)
			b.timeStop -= dt;
		else
			b.clock += dt;
		b.cooldown = std::max(0.f, b.cooldown - dt);
		b.guard = std::max(0.f, b.guard - dt);
		// 自分を強くする効き目: 時間が来たら切れる
		b.fluff = std::max(0.f, b.fluff - dt);
		b.snack = std::max(0.f, b.snack - dt);
		b.regen = std::max(0.f, b.regen - dt);
		V input{};
		Controls::Move(input.x, input.z);
		if (autoPlay) {
			input = {std::sin(life_.age * .7f), 0, .3f * std::cos(life_.age * .5f)};
			// 衛星 (体のまわりでしか当たらない撃ち方): 一番近い相手へ近づく
			BattleTarget close;
			if (WeaponsActive() && FusePatternOf(b.weapon.lv) == FusePattern::Orbit &&
			    NearestBattleTarget(body.at, 1e9f, close)) {
				V to = close.at - body.at;
				to.y = 0;
				to = Unit(to);
				input = {
				    to.x * std::cos(yaw_) - to.z * std::sin(yaw_), 0, to.x * std::sin(yaw_) + to.z * std::cos(yaw_)};
			}
		}
		// 確認用 (--challenge-matrix): 全部の能力 x チャレンジの実測。歩く向きも実測が決める
		if (b.matrix.active)
			TickChallengeMatrix(ctx, dt, input);
		// 確認用 (--weapon-check): 全部の変形武器の撃ち方を撃って確かめる
		if (weaponCheck_.active && WeaponsActive())
			TickWeaponCheck(ctx, dt, input);
		V wish{
		    input.x * std::cos(yaw_) + input.z * std::sin(yaw_), 0,
		    -input.x * std::sin(yaw_) + input.z * std::cos(yaw_)};
		// 速さは普段の移動と同じ (配合と大きさで決まり、加速と減速もある)
		// (変形武器の体の特徴: 速い・転がるで速く、重いで少し遅く)
		float walk = life_.recipe.Speed() * LifeTravelScale(body) * DevourSpeedScale(body.mass) * WeaponMoveScale();
		// 絵の具の床の上では速い
		for (const auto& pl : b.placed)
			if (pl.kind == BattleAction::Paint) {
				V d = body.at - pl.at;
				d.y = 0;
				if (Length(d) < pl.radius) {
					walk *= BattlePaintBoost;
					break;
				}
			}
		const bool dashing =
		    b.abilityLeft > 0 && DashAction(b.ability) && !(b.ability == BattleAction::Kei && b.abilityAge > kKeiJump);
		if (dashing) {
			b.velocity = b.dashDir * (walk * BattleDashBoost(b.ability));
			body.forward = b.dashDir;
		} else
			b.velocity = LifeMoveVelocity(
			    b.velocity, wish, life_.recipe, true, dt,
			    walk / std::max(.01f, life_.recipe.Speed()) * (b.toolShape == GimmickKomaShape ? .7f : 1.f));
		// 狙って使う形 (盾・ハンマー・剣・水鉄砲・掃除機) の間は、狙っている向き (カメラ) を向く
		const int t = b.toolShape;
		if (!dashing &&
		    (t == BattleShieldShape || t == BattleHammerShape || t == BattleSwordShape || t == GimmickGunShape ||
		     t == BattleVacuumShape || t == BattleShogiShape || t == BattleEraserShape))
			body.forward = Forward(yaw_, 0);
		// 変形武器: 移動の向きへなめらかに回る (撃つ向きは体の向き。一瞬で向きを変えると、大きな形がガクッと回る)
		else if (!dashing && WeaponsActive() && Length(wish) > .1f)
			body.forward = LifeWeaponFacing(body.forward, Unit(wish), dt);
		else if (!dashing && Length(wish) > .1f)
			body.forward = Unit(wish);
		// 使う (押し続けると待ち時間ごとに使う)
		bool fire = Controls::MorphAttack();
		if (autoPlay) {
			// 確認用の自動操作: 一番近い相手を向く
			BattleTarget nearest;
			const bool found = NearestBattleTarget(body.at, 1e9f, nearest);
			if (found) {
				V d = nearest.at - body.at;
				yaw_ = std::atan2(d.x, d.z);
				// (変形武器は体の向きへ撃つので、体も相手へ向ける)
				d.y = 0;
				if (WeaponsActive() && Length(d) > .01f)
					body.forward = Unit(d);
			}
			fire = true;
			// (連打で出す撃ち方は、押す・離すをくり返す)
			if (WeaponsActive() && FusePatternTap(FusePatternOf(b.weapon.lv)))
				fire = fire && std::fmod(b.clock, .3f) < .15f;
		}
		// 確認用 (--challenge-impact-check): 袋は使わず、全部の能力を順に 1 回ずつ使う (一番近い相手がいればそちらを向く)
		if (b.impactCheck) {
			fire = false;
			b.checkClock -= dt;
			if (b.checkClock <= 0 && b.checkNext < int(BattleAction::Count)) {
				b.checkClock = BattleImpactCheckStep;
				BattleTarget nearest;
				if (NearestBattleTarget(body.at, 1e9f, nearest)) {
					V d = nearest.at - body.at;
					yaw_ = std::atan2(d.x, d.z);
				}
				BattleItem item;
				item.radius = b.unit * .5f;
				++b.spent;
				b.abilityLeft = 0;
				StartBattleAbility(ctx, BattleAction(b.checkNext++), item);
			}
		}
		if (b.matrix.active || weaponCheck_.active)
			fire = false;
		// (能力の最中 (突進・回転など) は次を使わない)
		// (変形武器: 押している間、連射の間隔ごとに撃つ。衛星・置き罠は押した瞬間だけ (連打): FusePatternTap)
		if (WeaponsActive()) {
			auto& w = b.weapon;
			if (FusePatternTap(FusePatternOf(w.lv))) {
				if (fire && !b.previousFire)
					w.tapBuffer = FuseTapBuffer;
				if (w.tapBuffer > 0 && w.cool <= 0) {
					w.tapBuffer = 0;
					UseBattleWeapon(ctx);
				}
				w.tapBuffer = std::max(0.f, w.tapBuffer - dt);
			} else if (fire)
				UseBattleWeapon(ctx);
		}
		if (WeaponsActive())
			TickBattleWeapons(ctx, dt);
		b.previousFire = fire;
		// チャレンジ: 時間が来たら終わり (スコアを見せて結果画面へ)
		if (!b.matrix.active && b.clock >= b.time) {
			b.phase = BattleStep::Over;
			b.overAge = 0;
			b.message.clear();
			b.messageAge = 0;
			PlayDevourSound(devourSounds_.rankTop, .8f);
		}
	} else
		b.velocity = LifeMoveVelocity(b.velocity, {}, life_.recipe, true, dt);
	move = b.velocity * dt;
	TickBattleAbility(ctx, dt);
	TickBattlePlaced(ctx, dt);
	TickChallenge(ctx, dt);
	// 形の時間と姿勢: 能力ごとに動かす。時間が来たら液体へ戻る (守っている間は盾のまま)
	if (b.toolShape > 0) {
		b.toolAge += dt;
		b.toolLeft -= dt;
		b.toolPrevious = b.toolPose;
		auto& pose = b.toolPose;
		const float a = b.toolAge;
		switch (b.toolShape) {
		case BattleHammerShape: {
			const float t = std::clamp((a - BattleSmashWindup) / BattleSmashSwing, 0.f, 1.f);
			pose.pitch = -1.1f + 2.6f * t * t;
			break;
		}
		case BattleHeartShape:
			pose.spin += dt * 4;
			break;
		case GimmickHeliShape:
			pose.spin += dt * 28;
			pose.pitch = .15f;
			break;
		case GimmickKomaShape:
			pose.spin += dt * 24;
			pose.roll = .08f * std::sin(a * 9);
			break;
		case GimmickBallShape:
			pose.spin += dt * Length(b.velocity) / std::max(.1f, R * .6f);
			break;
		case GimmickPlaneShape:
			pose.roll = .25f * std::sin(a * 5);
			pose.pitch = -.1f;
			break;
		case BattleEraserShape:
			pose.spin = 1.2f * std::sin(a * 28);
			break;
		case BattleDarumaShape:
			pose.roll = .25f * std::sin(a * 7) * std::exp(-a * .3f);
			break;
		case GimmickDominoShape:
			pose.tip = 1.4f * std::clamp((a - .15f) / .4f, 0.f, 1.f);
			break;
		case BattleSwordShape: {
			const float t = std::clamp(a / .22f, 0.f, 1.f);
			pose.spin = -1.3f + 2.6f * (1 - (1 - t) * (1 - t));
			break;
		}
		case BattleSpringShape:
			pose.squash = a < .45f   ? .5f * a / .45f
			              : a < .65f ? .5f - .9f * (a - .45f) / .2f
			                         : std::max(0.f, -.4f + (a - .65f) * 1.6f);
			pose.squash = std::clamp(pose.squash, -.4f, .5f);
			break;
		default:
			break;
		}
		if (b.toolLeft <= 0 && !(b.toolShape == BattleShieldShape && b.guard > 0)) {
			// 守っている間に別の形を使ったら、終わったあと盾に戻る
			if (b.guard > 0) {
				b.toolShape = BattleShieldShape;
				b.toolAge = 0;
				b.toolLeft = b.guard;
				b.toolPose = b.toolPrevious = {};
			} else
				b.toolShape = 0;
		}
	}
	// 体を動かす: 固い箱は Move で、建物と動かない大きな物は足元の丸で止める
	{
		const float half = std::max(.5f, R * .3f);
		bool grounded = false;
		V at = Move(body.at, move, {half, .5f, half}, grounded);
		for (size_t i = 0; i < life_.props.size(); ++i) {
			const auto& prop = life_.props[i];
			const bool building = std::find(b.buildings.begin(), b.buildings.end(), i) != b.buildings.end();
			if (!building && !(prop.active && prop.foot > 0))
				continue;
			const float footR = prop.foot > 0 ? prop.foot : prop.radius * .7f;
			V d = at - prop.at;
			d.y = 0;
			const float reach = footR + R * .6f, len = Length(d);
			if (len < reach && len > .001f)
				at = at + d * ((reach - len) / len);
		}
		// 積み木は通れない (下の面が体より低いもの。体当たりや能力で壊して進む)
		for (const auto& blk : b.blocks) {
			if (!blk.alive || blk.at.y - blk.size * .5f > at.y + R * .3f)
				continue;
			V d = at - blk.at;
			d.y = 0;
			const float reach = blk.size * .6f + R * .6f, len = Length(d);
			if (len < reach && len > .001f)
				at = at + d * ((reach - len) / len);
		}
		// 戦う場所から出ない (積み木の街は縁の柵と崖・壁で止める。ほかは戦う円)
		if (BlocksStageActive())
			BlockBlocksStageBody(at, body.at, R);
		else if (b.arenaRadius > 0) {
			V d = at - b.arenaAt;
			d.y = 0;
			const float limit = std::max(1.f, b.arenaRadius - R * .6f), len = Length(d);
			if (len > limit)
				at = b.arenaAt + V{d.x * (limit / len), 0, d.z * (limit / len)};
		}
		// 積み木の街: 体の下の 9 か所から求めた傾いた面に乗る (足元から段以内の面。橋の下を通るときは下の面)。
		// 段より高い所から下りたら、すぐ下へ移さずに落ちる
		if (BlocksStageActive())
			at.y = BlocksSettle(at, body.at, R, dt, b.fallSpeed, b.groundSlope);
		else {
			at.y = InkGround(at) + 1.25f;
			b.fallSpeed = 0;
		}
		const V moved = (at - body.at) * (1 / std::max(dt, 1e-4f));
		body.at = at;
		// 液体の体の動き (中心の遅れ・進む向きへの伸び・核の速さ) を普段の移動と同じように進める。
		// 進めないと粒子の追いつける速さが上がらず、体の後ろが置いていかれて三日月の形になる
		LifeTickMotion(body, life_.recipe, moved, true, false, dt, body.forward);
	}
	// 投げた物・水: 飛んで、物に当たったら消える
	for (auto& s : b.shots) {
		// (変形武器の弾は性質ごとに進める)
		if (s.fused) {
			TickFusedShot(ctx, s, dt);
			continue;
		}
		s.age += dt;
		if (s.mode != BattleAction::Throw) {
			// 飛び道具: 飛び方ごとに進め、当たり方も変える
			s.hitCool = std::max(0.f, s.hitCool - dt);
			s.spin += dt * 12;
			const float speed = Length(V{s.velocity.x, 0, s.velocity.z});
			if (s.mode == BattleAction::Homing) {
				// 紙飛行機: 一番近い相手へ向きを少しずつ曲げる
				BattleTarget target;
				const bool found = NearestBattleTarget(s.at, 1e9f, target);
				if (found && speed > .01f) {
					const float now = std::atan2(s.velocity.x, s.velocity.z);
					V to = target.at - s.at;
					const float want = std::atan2(to.x, to.z);
					const float turn = std::clamp(
					    std::remainder(want - now, 6.2831853f), -BattleHomingTurn * dt, BattleHomingTurn * dt);
					const V d = Forward(now + turn, 0);
					s.velocity = {d.x * speed, 0, d.z * speed};
				}
				s.at.y += ((InkGround(s.at) + R * .6f) - s.at.y) * std::min(1.f, dt * 4);
				// (積み木の街: 上り坂で遅れて丘の中を飛ばないように)
				if (BlocksStageActive())
					s.at.y = std::max(s.at.y, InkGround(s.at) + s.radius);
			}
			if (s.mode == BattleAction::Bounce) {
				s.velocity.y -= unit * 20 * dt;
			}
			const V before = s.at;
			s.at = s.at + s.velocity * dt;
			// 積み木の街: 橋・坂・塔・手すりに当たる (上から落ちたら面の上に乗る。壁は、輪とボールは跳ね返り、ほかは終わる)
			{
				V hit, n;
				bool wall = false;
				if (BlocksSweep(before, s.at, unit * ChallengeBlocksStep, hit, n, wall)) {
					if (s.mode == BattleAction::Bounce) {
						// ボール: 当たった面 (床・坂・壁) の向きで跳ね返る。面から離れる速さは少し落とし、床や坂では跳ねる高さを残す
						const float into = Dot(s.velocity, n);
						if (into < 0) {
							s.velocity = s.velocity - n * (into * 1.82f);
							if (n.y > .3f && Dot(s.velocity, n) < unit * 5)
								s.velocity = s.velocity + n * (unit * 5 - Dot(s.velocity, n));
							s.hit.clear();
						}
						s.at = (wall ? hit : hit + V{0, s.radius, 0});
					} else if (!wall)
						s.at.y = std::max(s.at.y, hit.y);
					else if (s.mode == BattleAction::Disc) {
						// 輪: 床に沿って転がるので、壁の横の向きで跳ね返る
						s.at = hit;
						V side{n.x, 0, n.z};
						side = Length(side) > 1e-3f ? Unit(side) : V{0, 0, 1};
						const float into = Dot(s.velocity, side);
						if (into < 0) {
							s.velocity = s.velocity - side * (2 * into);
							s.hit.clear();
						}
					} else {
						s.at = hit;
						s.age = 99;
					}
				}
			}
			// 飛んだ跡: 後ろから見ると細い物 (鉛筆・紙飛行機・輪・十円玉) も道すじが分かるよう、色の粒を少しの間残す
			if (s.mode != BattleAction::Bounce && s.mode != BattleAction::Fan && s.mode != BattleAction::Sticky &&
			    std::fmod(s.age, .03f) < dt)
				b.fx.push_back({s.at, s.radius * .45f, 0, .28f, ActionColor(s.mode)});
			const float floor = InkGround(s.at) + s.radius;
			if (s.mode == BattleAction::Bounce && s.at.y < floor) {
				s.at.y = floor;
				s.velocity.y = std::max(std::abs(s.velocity.y) * .82f, unit * 5);
			}
			if (s.mode == BattleAction::Disc)
				s.at.y = InkGround(s.at) + unit * .3f;
			// 輪とボールはステージの縁で跳ね返る (跳ね返ったら同じ相手にまた当たれる)
			// (積み木の街は四角い縁の柵で跳ね返る)
			if ((s.mode == BattleAction::Disc || s.mode == BattleAction::Bounce) && BlocksStageActive()) {
				const float edge = ChallengeBlocksHalf * unit - s.radius;
				for (int axis = 0; axis < 2; ++axis) {
					float& pos = axis == 0 ? s.at.x : s.at.z;
					float& v = axis == 0 ? s.velocity.x : s.velocity.z;
					const float mid = axis == 0 ? b.arenaAt.x : b.arenaAt.z, off = pos - mid;
					if (std::abs(off) > edge) {
						pos = mid + std::clamp(off, -edge, edge);
						if (v * off > 0) {
							v = -v;
							s.hit.clear();
						}
					}
				}
			} else if ((s.mode == BattleAction::Disc || s.mode == BattleAction::Bounce) && b.arenaRadius > 0) {
				V o = s.at - b.arenaAt;
				o.y = 0;
				const float len = Length(o);
				if (len > b.arenaRadius - s.radius && len > .01f) {
					const V n = o * (1 / len);
					const float into = Dot(s.velocity, n);
					if (into > 0) {
						s.velocity = s.velocity - n * (2 * into);
						s.hit.clear();
					}
					s.at = b.arenaAt + n * (b.arenaRadius - s.radius) + V{0, s.at.y - b.arenaAt.y, 0};
				}
			}
			// 当たり: 一番近い 1 つに衝撃を出す (同じ相手には続けて当てない)。当たったあとの動きは飛び方ごと
			if (s.hits > 0 && s.hitCool <= 0) {
				BattleImpact i;
				i.action = s.by;
				i.effect = s.mode == BattleAction::Sticky ? ImpactEffect::Stun : ImpactEffect::Hit;
				i.shape = ImpactShape::Sphere;
				i.at = s.at;
				i.reach = s.radius;
				i.flat = s.mode == BattleAction::Disc;
				i.dir = V{s.velocity.x, 0, s.velocity.z};
				i.damage = s.damage;
				i.push = s.push;
				i.limit = 1;
				if (s.mode == BattleAction::Sticky) {
					i.seconds = BattleStickyTime;
					i.bigScale = .4f;
					i.label = "ぺたっ";
				}
				std::vector<BattleTarget> touched;
				if (EmitBattleImpact(ctx, i, &touched, &s.hit) > 0) {
					const BattleTarget& e = touched.front();
					--s.hits;
					s.hit.push_back(e.id);
					b.fx.push_back({s.at, s.radius * 1.4f, 0, .18f, ActionColor(s.mode)});
					if (s.mode == BattleAction::Coin) {
						// 十円玉: 次に近い相手へ跳ね移る (いなければ消える)
						BattleTarget next;
						if (s.hits > 0 && NearestBattleTarget(s.at, R * 9, next, &s.hit)) {
							V to = next.at - s.at;
							to.y = 0;
							s.velocity = Unit(to) * std::max(speed, unit * 16);
							PlayDevourSound(devourSounds_.note, .4f, 1.9f);
						} else
							s.hits = 0;
					} else if (s.mode == BattleAction::Bounce) {
						V away = s.at - e.at;
						away.y = 0;
						away = Length(away) > .01f ? Unit(away) : V{0, 0, 1};
						s.velocity = V{away.x * speed, unit * 7, away.z * speed};
						s.hitCool = .15f;
					} else if (s.mode == BattleAction::Disc) {
						s.hitCool = .08f;
					} else if (
					    s.mode == BattleAction::Homing || s.mode == BattleAction::Fan ||
					    s.mode == BattleAction::Sticky) {
						s.hits = 0;
					}
				}
			}
			if (s.hits <= 0 || s.age > s.life ||
			    (s.mode != BattleAction::Bounce && s.mode != BattleAction::Disc && s.mode != BattleAction::Homing &&
			     s.at.y < InkGround(s.at)))
				s.age = 99;
			continue;
		}
		s.velocity.y -= unit * (s.style == 2 ? 2.f : 6.f) * dt;
		const V before = s.at;
		s.at = s.at + s.velocity * dt;
		// 積み木の街: 橋・坂・塔・手すりに当たる (上から落ちたら面の上。壁には弱く跳ね返る)
		{
			V hit, n;
			bool wall = false;
			if (BlocksSweep(before, s.at, unit * ChallengeBlocksStep, hit, n, wall)) {
				if (!wall)
					s.at.y = std::max(s.at.y, hit.y);
				else {
					s.at = hit;
					const float into = Dot(s.velocity, n);
					if (into < 0)
						s.velocity = (s.velocity - n * (2 * into)) * .5f;
				}
			}
		}
		{
			BattleImpact i;
			i.action = s.by;
			i.shape = ImpactShape::Sphere;
			i.at = s.at;
			i.reach = s.radius;
			i.flat = false;
			i.dir = V{s.velocity.x, 0, s.velocity.z};
			i.damage = s.damage;
			i.push = s.push;
			i.limit = 1;
			std::vector<BattleTarget> touched;
			if (EmitBattleImpact(ctx, i, &touched, &s.hit) > 0) {
				--s.hits;
				s.hit.push_back(touched.front().id);
				s.velocity.x *= ChallengeThrowSlow;
				s.velocity.z *= ChallengeThrowSlow;
				if (s.hits <= 0)
					s.age = 99;
				if (s.style != 2)
					b.fx.push_back(
					    {s.at, s.radius * 1.5f, 0, .2f,
					     s.style == 1 ? Engine::Vector4{1, .6f, .85f, .7f} : Engine::Vector4{1, .8f, .4f, .7f}});
			}
		}
		// 投げた物 (飴玉と水以外) は、当たれる数が残っていれば床を転がる (チャレンジ)
		const float floorY = InkGround(s.at) + s.radius * .5f;
		if (s.style == 0 && s.hits > 0 && s.at.y < floorY) {
			s.at.y = floorY;
			s.velocity.y = 0;
			s.velocity.x *= std::exp(-ChallengeThrowRoll * dt);
			s.velocity.z *= std::exp(-ChallengeThrowRoll * dt);
			if (Length(s.velocity) < unit * 1.2f)
				s.age = 99;
		} else if (s.at.y < InkGround(s.at) || (s.style == 2 && s.age > .8f))
			s.age = 99;
	}
	// (変形武器の弾が分かれてできた弾を足す。進めている間に足すと並びが壊れるので、あとで足す)
	b.shots.insert(b.shots.end(), b.newShots.begin(), b.newShots.end());
	b.newShots.clear();
	for (auto& pop : b.pops)
		pop.age += dt;
	b.pops.erase(
	    std::remove_if(b.pops.begin(), b.pops.end(), [](const BattlePop& pop) { return pop.age > 1; }), b.pops.end());
	b.shots.erase(
	    std::remove_if(
	        b.shots.begin(), b.shots.end(),
	        [](const BattleShot& s) { return s.age > (s.mode == BattleAction::Throw ? 3.f : s.life) || s.age >= 99; }),
	    b.shots.end());
	for (auto& f : b.fx)
		f.age += dt;
	b.fx.erase(std::remove_if(b.fx.begin(), b.fx.end(), [](const BattleFx& f) { return f.age > f.life; }), b.fx.end());
	// 終わり: 少し見せてから結果画面へ
	if (b.phase == BattleStep::Over) {
		b.overAge += dt;
		if (b.overAge >= 2.5f) {
			if (autoPlay) {
				std::filesystem::create_directories("tests/out");
				std::ofstream log("tests/out/devour-battle.txt", std::ios::app);
				int left = 0;
				for (const auto& x : b.bag)
					left += int(x.items.size());
				log << "stage=" << stage.id << " challenge=" << ChallengeKindId(b.challenge) << " time=" << b.time
				    << " score=" << b.score << " rank=" << b.rank << " clock=" << b.clock << " spent=" << b.spent << " pattern=" << b.blockPattern
				    << " broken=" << b.broken[0]
				    << "/" << b.broken[1] << "/" << b.broken[2] << " cm=" << DevourSizeCm(stage, body.mass)
				    << " items_left=" << left;
				if (b.crafted)
					log << " weapon=" << b.weapon.name << " pattern=" << FusePatternName(FusePatternOf(b.weapon.lv))
					    << " shots=" << FuseShotCount(FusePatternOf(b.weapon.lv), b.weapon.points[0], b.weapon.caps[0])
					    << " power=" << CraftPower(b.weapon.points[1], b.weapon.caps[1]) << " interval="
					    << CraftInterval(b.weapon.points[2], b.weapon.lv[size_t(FuseTrait::Heavy)], b.weapon.caps[2]);
				log << '\n';
			}
			if (b.impactCheck)
				WriteImpactCheck();
			// 自己ベストのスコアを残す
			if (!b.impactCheck) {
				int& best = challengeBests_[ChallengeBestKey(stage.id, b.challenge)];
				if (b.score > best) {
					best = b.score;
					b.newBest = true;
					SaveSlimeLab();
				}
			}
			// 結果画面は吸収の結果 (大きさのランク) を見せるので、体を吸収が終わったときの大きさに戻す
			b.finalCm = DevourSizeCm(stage, body.mass);
			body.mass = b.startMass;
			// 次のステージへはいつでも進める。結果の「クリア」は大きさの目標に届いたか
			b.phase = BattleStep::None;
			Finish(p, DevourRankAt(stage, b.startMass) > 0);
		}
	}
	// 体の位置と見た目を反映する
	auto& transform = r.get<TransformComponent>(player_);
	transform.translate = Write(body.at);
	const float size = LifeScale(body.mass);
	transform.scale = {size, size, size};
	transform.rotate.y = std::atan2(body.forward.x, body.forward.z);
	ink.fluidDirection = body.forward;
	// (積み木の街: 体が乗っている傾いた面を流体の床にする。坂でも体の粒子が坂に沿って接地する)
	if (BlocksStageActive() && b.fallSpeed <= 0) {
		ink.groundHeight = body.at.y - ChallengeBodyGround;
		// (傾きは体の形と同じもの (なめらかにした傾き)。目の場所もこれで傾ける)
		ink.groundSlope = lifeTiltWorld_;
	} else {
		ink.groundHeight = InkGround(body.at);
		ink.groundSlope = {};
	}
	ink.airBlend = 0;
	ink.fluidAspect = body.aspect;
	ink.fluidMotion = std::min(
	    1.f, Length(body.velocity) / std::max(1.f, BattleMoveSpeed(battle_.unit, DevourBodyRadius(body.mass))));
	ink.swimming = false;
	ink.phase = SlimePhase::Roaming;
	p.worldScale = p.aimScale = 1;
	p.aiming = false;
	p.action = Action::Free;
	InkCamera(r, p, ctx);
	Presentation(r, p, ctx);
	SyncSlimeBodies(ctx);
}

// 見た目: 投げた物・置いた物はその物のモデル (無ければ描かない)、飴玉はアメのモデル。水の粒・効果の粒は描かない
void ChronoSystem::DrawDevourBattle(GameContext& ctx) {
	// 結果の積み木の山: 場に残った積み木と、降ってきて積もる山を描く
	if (finished_) {
		if (BlocksPileResult()) {
			DrawChallenge(ctx);
			DrawBlocksPile(ctx);
		}
		return;
	}
	if (!BattleActive())
		return;
	// 積み木の街のまわりの景色 (変形武器を作る画面では、背景の箱に隠れるので描かない)
	if (BlocksStageActive() && battle_.phase != BattleStep::Craft)
		DrawStageScenery(ctx);
	const auto& b = battle_;
	const auto& body = life_.bodies[0];
	DrawChallenge(ctx);
	// 変形武器を作る画面: 見本の窓にステージが映らないよう、スライムのまわりを背景の箱で囲む
	DrawCraftBackdrop(ctx);
	// (見た目の無い物の代わりの玉・飛んだ跡や当たったときの効果の粒・水の玉・ふわふわの粒は描かない。技の物 (モデル) だけを見せる)
	// 飛ぶ物のモデルは、弾の位置を真ん中にして描く (下の面を弾の位置から半分下げると、寝かせた細長い物 (鉛筆) が丸ごと弾より下になり、
	// 地面すれすれの弾では地面に埋まって見えない)
	auto propAround = [&](uint32_t visual, V at, float size, float yaw, float tilt) {
		auto found = cityVisuals_.find(visual);
		if (found == cityVisuals_.end())
			return;
		const auto& v = found->second;
		const float big = std::max(v.height, 2 * (std::max(v.half.x, v.half.z) + v.round)),
		            half = v.height * (size / std::max(.01f, big)) * .5f;
		DrawBattleProp(ctx, visual, at - V{0, half, 0}, size, yaw, tilt);
	};
	// (F4) 変形武器の形の間は、材料の物を体の中のそれぞれの所に浮かべる (どれを混ぜた武器か分かるように)。形になるにつれて大きくなり、ゆっくり回る
	// (作る画面では、見本の窓に映るスライムにも、今の材料で並べる)
	const bool crafting = b.phase == BattleStep::Craft;
	const BattleWeapon shownWeapon = crafting ? CraftedWeapon() : b.weapon;
	if ((WeaponsActive() || crafting) && b.weaponShape > 0 && b.weaponForm > 0)
		if (const auto* g = WeaponShapeOf(b.weaponShape)) {
			const float scale = LifeScale(body.mass),
			            grow = std::clamp(b.weaponAge / .3f, 0.f, 1.f) * std::clamp(b.weaponForm / .15f, 0.f, 1.f);
			V f = body.forward;
			f.y = 0;
			f = Length(f) > .01f ? Unit(f) : V{0, 0, 1};
			const V right{f.z, 0, -f.x};
			// (J3) 土台に並べる本物のモデル: 並べた所に、形の向きのまま半分沈めて描く (物の場の座標 → モデルの座標は
			// ItemProfileRules.h のそろえ方: 左右前後の真ん中・一番下を 0、一番長い辺を 2。場の原点は profile.look.center)
			if (!g->models.empty()) {
				static std::map<std::string, bool> hasModel;
				for (const auto& m : g->models) {
					if (m.item >= shownWeapon.filled)
						continue;
					const std::string& id = shownWeapon.ids[m.item];
					auto known = hasModel.find(id);
					if (known == hasModel.end())
						known = hasModel.emplace(id, std::filesystem::exists("Resources/Models/Lowpoly/" + id + ".obj"))
						            .first;
					const ItemProfile* prof = ItemProfileOf(id);
					if (!known->second || !prof)
						continue;
					const auto& low = LowModelOf(id);
					const V size = low.hi - low.lo;
					const float kk = 2 / std::max({size.x, size.y, size.z, 1e-4f});
					const V mid{(low.lo.x + low.hi.x) * .5f, low.lo.y, (low.lo.z + low.hi.z) * .5f};
					// 左右に映した物は向きが鏡になるので、左右の向きに一番近い軸を裏返して、回転にする (裏返しの面にしない)
					std::array<V, 3> axes = m.axes;
					if (m.mirrored) {
						int flip = 0;
						for (int a = 1; a < 3; ++a)
							if (std::abs(axes[size_t(a)].x) > std::abs(axes[size_t(flip)].x))
								flip = a;
						axes[size_t(flip)] = axes[size_t(flip)] * -1.f;
					}
					// 形の座標 → 世界: 体の向き (right・上・f) に回し、体の大きさ scale と、形になる途中の大きさ grow をかける
					// (積み木の街では、体の形と同じく坂の傾きに合わせて回す: 坂で物が体の外へ出ないように)
					auto world = [&](V v) {
						v = BodyTiltApply(v);
						return right * v.x + V{0, v.y, 0} + f * v.z;
					};
					const V local = WeaponShapePose(b.weaponShape, m.pos, b.weaponPose);
					V origin = local;
					// (液体が形に寄るのは少し遅れるので、モデルは形になって MotifModelDelay 秒たってから出す。先に出ると体の外に浮いて見える)
					const float s =
					    m.scale * grow * std::clamp((b.weaponAge - MotifModelDelay) / MotifModelFade, 0.f, 1.f);
					const float offs[3] = {
					    mid.x * kk + prof->look.center.x, mid.y * kk + prof->look.center.y,
					    mid.z * kk + prof->look.center.z};
					for (int a = 0; a < 3; ++a)
						origin = origin - axes[size_t(a)] * (s * offs[a]);
					const V at = body.at + V{0, -1.25f, 0} + world(origin) * scale;
					Engine::Matrix4x4 mat{};
					for (int a = 0; a < 3; ++a) {
						const V row = world(axes[size_t(a)]) * (s * kk * scale);
						mat.m[a][0] = row.x;
						mat.m[a][1] = row.y;
						mat.m[a][2] = row.z;
						mat.m[a][3] = 0;
					}
					mat.m[3][0] = at.x;
					mat.m[3][1] = at.y;
					mat.m[3][2] = at.z;
					mat.m[3][3] = 1;
					if (s * scale > .01f)
						ctx.renderer->DrawMeshInstanced(
						    low.mesh, lowPalette_, mat, {1, 1, 1, 1}, "LowpolySurface", lowMaps_);
				}
			}
			for (int i = 0; i < std::min(g->anchors, shownWeapon.filled); ++i) {
				// (J3) モデルを並べた形では、浮かべるのは 1 つ目の物 (土台の比率を決めた物) だけ
				if (!g->models.empty() && i > 0)
					break;
				const V local = BodyTiltApply(WeaponShapePose(b.weaponShape, g->anchor[size_t(i)], b.weaponPose));
				const float size = 2 * g->anchorFit[size_t(i)] * scale * grow;
				if (size < .05f)
					continue;
				const V at = body.at + V{0, -1.25f, 0} + right * (local.x * scale) + V{0, local.y * scale, 0} +
				             f * (local.z * scale);
				DrawBattleProp(
				    ctx, shownWeapon.items[size_t(i)].visual, at - V{0, size * .5f, 0}, size,
				    std::atan2(f.x, f.z) + life_.age * .6f * (i % 2 ? -1.f : 1.f), 0);
			}
		}
	// (変形武器を作る画面では、見本の撃ち方の弾を描く)
	for (const auto& s : crafting ? b.craft.demoShots : b.shots) {
		if (s.style == 1) {
			// 飴玉 (アメのモデル。回りながら飛ぶ)
			const auto& m = LowModelOf("candy");
			Engine::Transform t;
			const float scale = LowScaleFor("candy", s.radius * 3.2f);
			t.scale = {scale, scale, scale};
			t.rotate = {s.age * 9, s.age * 6, 0};
			t.translate = EV(s.at - V{0, (m.lo.y + m.hi.y) * .5f * scale, 0});
			ctx.renderer->DrawMeshInstanced(m.mesh, lowPalette_, t, {1, 1, 1, 1}, "LowpolySurface", lowMaps_);
		} else if (s.style == 2)
			continue;
		else if (s.mode != BattleAction::Throw) {
			// 飛び道具: 進む向きを向いて飛ぶ。十円玉と輪とボールは回る
			// (十円玉とトランプは寝かせたモデルなので、起こして面を見せる。鉛筆はモデルの長い向き (x) を進む向きへ向ける)
			float yaw = std::atan2(s.velocity.x, s.velocity.z), tilt = 0;
			if (s.mode == BattleAction::Coin || s.mode == BattleAction::Disc || s.mode == BattleAction::Bounce)
				yaw += s.spin;
			if (s.mode == BattleAction::Coin || s.mode == BattleAction::Fan || s.mode == BattleAction::Sticky)
				tilt = 1.2f;
			if (s.mode == BattleAction::Bounce)
				tilt = s.spin * .7f;
			if (s.mode == BattleAction::Pierce)
				yaw -= 1.5707963f;
			if (cityVisuals_.count(s.visual))
				propAround(s.visual, s.at, s.size, yaw, tilt);
		} else
			DrawCityPropInTentacle(ctx, s.visual, s.at, 1.f);
	}
	// 置いた物
	for (const auto& pl : b.placed) {
		switch (pl.kind) {
		case BattleAction::Marbles: {
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(ctx, pl.visual, pl.at - V{0, pl.size * .5f, 0}, pl.size * 1.4f, pl.age * 3, 0);
			break;
		}
		case BattleAction::Tower: {
			// 横に 5 列・縦に 3 段の積み木 (崩れる最後の 0.3 秒は前へ倒れる)
			const V side{pl.dir.z, 0, -pl.dir.x};
			const float yaw = std::atan2(pl.dir.x, pl.dir.z),
			            fall = std::clamp((pl.age - (pl.life - .3f)) / .3f, 0.f, 1.f);
			const float cube = pl.size, rise = std::min(1.f, pl.age / .25f);
			for (int row = 0; row < 3; ++row)
				for (int col = 0; col < 5; ++col) {
					V at = pl.at + side * (pl.radius * (float(col) / 2.f - 1)) +
					       V{0, cube * 1.1f * float(row) * rise, 0} + pl.dir * (cube * 1.4f * float(row) * fall);
					if (cityVisuals_.count(pl.visual))
						DrawBattleProp(ctx, pl.visual, at, cube, yaw + .2f * float((row + col) % 3 - 1), fall * 1.2f);
				}
			break;
		}
		case BattleAction::Domino: {
			const float fall =
			    std::clamp((pl.age - (BattleDominoStart + BattleDominoStep * float(pl.index))) / .15f, 0.f, 1.f);
			const float rise = std::min(1.f, pl.age / .15f);
			if (rise < 1 && pl.index > int(pl.age / .02f))
				break;
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(ctx, pl.visual, pl.at, pl.size, std::atan2(pl.dir.x, pl.dir.z), 1.45f * fall * fall);
			break;
		}
		case BattleAction::Paint: {
			// 塗った床: 平たくつぶした球 (最後の 1 秒で小さくなる)
			const float fade = std::clamp(pl.life - pl.age, 0.f, 1.f), grow = std::min(1.f, pl.age / .2f);
			Engine::Transform t;
			t.translate = EV(pl.at + V{0, .15f, 0});
			const float r2 = pl.radius * grow * (.6f + .4f * fade);
			t.scale = {r2, .12f, r2};
			ctx.renderer->DrawMeshInstanced(sphere_, white_, t, pl.color);
			break;
		}
		case BattleAction::Read:
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(ctx, pl.visual, pl.at, pl.size, 0, 0);
			break;
		case BattleAction::Beckon:
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(
				    ctx, pl.visual, pl.at, pl.size * std::min(1.f, pl.age * 4), 3.14159f + std::sin(pl.age * 12) * .3f,
				    0);
			break;
		case BattleAction::Cage:
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(ctx, pl.visual, pl.at, pl.size, pl.age * .5f, 3.14159f);
			break;
		case BattleAction::Smell: {
			// においの雲: 地面に広がる緑がかった雲と、ゆらゆら立ちのぼる粒
			const float fade = std::clamp(pl.life - pl.age, 0.f, 1.f), grow = std::min(1.f, pl.age / .3f);
			Engine::Transform t;
			t.translate = EV(pl.at + V{0, .2f, 0});
			const float r2 = pl.radius * grow * (.6f + .4f * fade);
			t.scale = {r2, .15f, r2};
			ctx.renderer->DrawMeshInstanced(sphere_, white_, t, {.65f, .75f, .45f, 1});
			break;
		}
		case BattleAction::Decoy: {
			// ぬいぐるみ: 跳ねる。弾ける直前はふくらむ
			const float hop = std::abs(std::sin(pl.age * 6)) * pl.size * .25f,
			            swell = 1 + .4f * std::clamp((pl.age - (pl.life - .6f)) / .6f, 0.f, 1.f);
			if (cityVisuals_.count(pl.visual))
				DrawBattleProp(ctx, pl.visual, pl.at + V{0, hop, 0}, pl.size * swell * 1.4f, pl.age * 2, 0);
			break;
		}
		default:
			break;
		}
	}
}

// 戦闘の HUD (チリツモの巻尺のデザイン): 左上にチャレンジの名前と遊び方と効き目、上の真ん中にスコアとコンボ、右上に残り時間、
// 下の真ん中に変形武器のステッカー
void ChronoSystem::DrawDevourBattleUI(GameContext& ctx) {
	using namespace UI::Scale;
	// 変形武器を作る画面
	if (battle_.phase == BattleStep::Craft) {
		DrawWeaponCraft(ctx);
		return;
	}
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	auto& b = battle_;
	const auto& info = ChallengeOf(b.challenge);
	const Engine::Vector4 cream{.99f, .97f, .925f, 1}, used{.9f, .84f, .59f, 1}, gray{.6f, .6f, .6f, 1};
	// ---- 左上: チャレンジの名前と遊び方 ----
	{
		const std::string name = info.name, sub = info.goal, status = ChallengeStatus();
		const float x = 24, top = 24,
		            w = std::max({ui.Width(name, 30, Jp), ui.Width(sub, 20, Jp), ui.Width(status, 18, Jp)}) + 32,
		            h = status.empty() ? 90.f : 120.f;
		Plate(ui, {x, top, w, h}, Yellow);
		ui.Text(name, x + 16, top + 4, 30, Black, Jp);
		ui.Fill({x, top + 50, w, 3}, Black);
		ui.Text(sub, x + 16, top + 58, 20, Black, Jp);
		if (!status.empty())
			ui.Text(status, x + 16, top + 88, 18, Black, Jp);
		std::string buffs;
		if (b.guard > 0)
			buffs += "守り " + std::to_string(int(std::ceil(b.guard))) + "秒  ";
		if (b.regen > 0)
			buffs += "回復中 " + std::to_string(int(std::ceil(b.regen))) + "秒  ";
		if (b.fluff > 0)
			buffs += "ふわふわ " + std::to_string(int(std::ceil(b.fluff))) + "秒  ";
		if (b.snack > 0)
			buffs += "おやつパワー " + std::to_string(int(std::ceil(b.snack))) + "秒  ";
		if (b.heavy > 0)
			buffs += "重い体 " + std::to_string(int(std::ceil(b.heavy))) + "秒  ";
		if (b.timeStop > 0)
			buffs += "時計停止中  ";
		if (!buffs.empty()) {
			const float bw = ui.Width(buffs, 18, Jp) + 24;
			Plate(ui, {x, top + h + 16, bw, 32}, cream, 3, 4);
			ui.Text(buffs, x + 12, top + h + 19, 18, Black, Jp);
		}
	}
	// ---- 上の真ん中: スコアとコンボ、自己ベスト ----
	{
		const std::string value = std::to_string(b.score);
		const float vw = ui.Width(value, 52, Num), lw = ui.Width("スコア", 22, Jp), w = std::max(220.f, vw + lw + 48),
		            x = 640 - w * .5f, top = 24;
		Plate(ui, {x, top, w, 70}, Yellow);
		ui.Text("スコア", x + 16, top + 24, 22, Black, Jp);
		ui.Text(value, x + w - 16 - vw, top + 35 - .56f * 52, 52, Black, Num);
		const bool beat = b.best > 0 && b.score > b.best;
		const std::string best = beat         ? std::string("自己ベスト更新中！")
		                         : b.best > 0 ? "自己ベスト " + std::to_string(b.best)
		                                      : std::string("初挑戦");
		const float bw = ui.Width(best, 18, Jp) + 24;
		Plate(ui, {640 - bw * .5f, top + 84, bw, 30}, beat ? Red : cream, 3, 4);
		ui.Text(best, 640 - bw * .5f + 12, top + 86, 18, beat ? White : Black, Jp);
		if (b.combo > 1) {
			char text[32];
			snprintf(text, sizeof(text), "%dコンボ ×%.1f", b.combo, ChallengeComboScale(b.combo));
			const float kick = std::clamp((b.comboClock - (ChallengeComboWindow - .2f)) / .2f, 0.f, 1.f),
			            size = 26 * (1 + .25f * kick);
			const float cw = ui.Width(text, size, Jp) + 28;
			Plate(ui, {x + w + 16, top + 12, cw, size * 1.6f}, Red, 3, 4);
			ui.Text(text, x + w + 30, top + 12 + size * .15f, size, White, Jp);
		}
	}
	// ---- 右上: 残り時間 (残り 10 秒で赤く脈打つ) ----
	{
		const float leftTime = std::max(0.f, b.time - b.clock);
		const bool hurry = leftTime <= 10 && b.phase == BattleStep::Fight;
		const float beat = hurry ? .5f + .5f * std::sin(life_.age * 10) : 0, size = 64 * (1 + .06f * beat);
		const std::string clock = Clock(leftTime), label = "のこり";
		const float clockW = ui.Width(clock, 64, Num), labelW = ui.Width(label, 22, Jp), w = clockW + labelW + 46,
		            x = 1280 - 24 - w;
		const auto ink = hurry ? White : Black;
		Plate(ui, {x, 24, w, 70}, hurry ? Red : Yellow);
		ui.Text(label, x + 16, 24 + 22, 22, ink, Jp);
		ui.Text(clock, x + labelW + 28, 24 + 35 - .56f * size, size, ink, Num);
	}
	// ---- 下の真ん中: 変形武器のステッカー ----
	if (WeaponsActive())
		DrawWeaponStickers(ctx);
	// 得点や拾った物の文字 (その場所に浮かべる)
	for (const auto& pop : b.pops) {
		float x, y;
		if (!GimmickScreen(ctx, pop.at, x, y))
			continue;
		const float a = 1 - pop.age;
		ui.Text(pop.text, x - ui.Width(pop.text, 24, Jp) * .5f, y - 40 * pop.age, 24, UI::WithAlpha(pop.color, a), Jp);
	}
	// 始まりの演出・知らせ・終わり (黄色の札に墨の文字)
	auto banner = [&](const std::string& text, float size, float y, Engine::Vector4 fill, Engine::Vector4 ink,
	                  float alpha) {
		const float w = ui.Width(text, size, Jp) + 40;
		ui.MultiplyAlpha(alpha);
		Plate(ui, {640 - w * .5f, y, w, size * 1.35f}, fill, 3, 5);
		ui.Text(text, 640 - w * .5f + 20, y + size * .08f, size, ink, Jp);
		ui.MultiplyAlpha(alpha > 0 ? 1 / alpha : 1);
	};
	if (b.phase == BattleStep::Intro) {
		const float a = std::min(1.f, b.intro * 3);
		banner("チャレンジ開始！", 30, 190, Red, White, a);
		banner(info.name, 56, 240, Yellow, Black, a);
		banner(info.goal, 24, 330, cream, Black, a);
		banner("制限時間 " + Clock(b.time), 22, 382, cream, Black, a);
	} else if (b.messageAge > 0)
		banner(b.message, 24, 208, cream, Black, std::min(1.f, b.messageAge * 2));
	if (b.phase == BattleStep::Over) {
		banner("タイムアップ！", 56, 220, Yellow, Black, 1);
		banner("スコア " + std::to_string(b.score), 40, 310, cream, Black, 1);
		if (b.score > b.best && b.best > 0)
			banner("自己ベスト更新！", 26, 380, Red, White, 1);
	}
}

// 結果画面に重ねるチャレンジの記録と、チャレンジだけやり直す案内
void ChronoSystem::DrawDevourBattleRecord(GameContext& ctx) {
	const auto& b = battle_;
	// (こわした積み木の山の結果では、記録はその画面の中に出す)
	if (b.tries <= 0 || finishAge_ < 1.f || BlocksPileResult())
		return;
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	ui.MultiplyAlpha(std::min(1.f, (finishAge_ - 1) / .4f));
	const Engine::Vector4 hazard{.95f, .76f, .19f, 1}, white{.96f, .96f, .94f, 1}, red{.95f, .35f, .3f, 1};
	const UI::Rect panel{830, 450, 420, 170};
	ui.Fill(panel, {0, 0, 0, .6f});
	auto text = [&](const std::string& s, float x, float y, float size, Engine::Vector4 color) {
		ui.Text(s, x, y, size, color, UI::JapaneseFont);
	};
	{
		text(ChallengeOf(b.challenge).name, panel.x + 16, panel.y + 10, 24, hazard);
		text("スコア " + std::to_string(b.score), panel.x + 16, panel.y + 44, 28, white);
		if (b.newBest)
			text("自己ベスト更新！", panel.x + 230, panel.y + 50, 20, red);
		else {
			const auto best = challengeBests_.find(ChallengeBestKey(CurrentDevourStage().id, b.challenge));
			if (best != challengeBests_.end() && best->second > 0)
				text("自己ベスト " + std::to_string(best->second), panel.x + 230, panel.y + 50, 20, white);
		}
		text(
		    "チャレンジの時間 " + UI::Scale::Clock(b.time) + "   使った物 " + std::to_string(b.spent) + " 回",
		    panel.x + 16, panel.y + 84, 18, white);
	}
	text(std::to_string(b.tries) + " 回目", panel.x + 16, panel.y + 112, 18, white);
	text(
	    "L / Bボタン: チャレンジだけやり直す", panel.x + 16, panel.y + 136, 20, hazard);
}

// ---- 変形チャレンジの成長の間の部分 ----
// このステージで挑むチャレンジを決める: --challenge=id で決められる。無ければランダム (どれも遊べるチャレンジの中から)
void ChronoSystem::PickChallenge() {
	// (遊べないチャレンジ (色ぬり) は、指定されても選ばない)
	const int forced = ChallengeIndexOf(ArgText(L"--challenge="));
	if (forced >= 0 && ChallengeReady(ChallengeKind(forced))) {
		challenge_ = ChallengeKind(forced);
		return;
	}
	// タイトルの隠しコマンドのチャレンジ直行: 札で選んだチャレンジ
	if (QuickChallenge().active && ChallengeReady(ChallengeKind(QuickChallenge().kind))) {
		challenge_ = ChallengeKind(QuickChallenge().kind);
		return;
	}
	// (ランダムに選ぶのは、できあがっているチャレンジだけ)
	static std::mt19937 random{std::random_device{}()};
	std::vector<ChallengeKind> ready;
	for (int i = 0; i < ChallengeKindCount; ++i)
		if (ChallengeReady(ChallengeKind(i)))
			ready.push_back(ChallengeKind(i));
	challenge_ = ready.empty() ? ChallengeKind::Blocks
	                           : ready[size_t(std::uniform_int_distribution<int>(0, int(ready.size()) - 1)(random))];
}

// 成長の間: 長押しでチャレンジへ移る。残り ChallengeAutoAt 秒になったら自動で移る。移ったら true
// (チュートリアルの間は時計が止まっているので、自分でも移れない)
bool ChronoSystem::TickChallengeMove(Player& p, float dt, bool held) {
	if (!devour_ || finished_ || !BattleEnabled()) {
		challengeHold_ = 0;
		return false;
	}
	if (DevourTutorialHolding()) {
		challengeHold_ = 0;
		return false;
	}
	challengeHold_ = held ? challengeHold_ + dt : 0.f;
	if (challengeHold_ >= ChallengeMoveHold) {
		challengeHold_ = 0;
		challengeMoved_ = true;
		PlayDevourSound(devourSounds_.rankUp, .8f);
		FinishDevour(p);
		return true;
	}
	if (devourClock_.left <= ChallengeAutoAt) {
		challengeHold_ = 0;
		FinishDevour(p);
		return true;
	}
	return false;
}

// 成長の間の HUD: 左上のステージの札の下に、このステージのチャレンジ・遊び方・おすすめの能力・移り方を出す
void ChronoSystem::DrawChallengeCard(GameContext& ctx) {
	using namespace UI::Scale;
	if (!devour_ || finished_ || BattleActive() || !BattleEnabled() || DevourTutorialCutscene() ||
	    DevourShelfDive() > 0)
		return;
	UI::Canvas ui(ctx.renderer, ctx.viewportSize.x, ctx.viewportSize.y);
	const auto& info = ChallengeOf(challenge_);
	// おすすめの能力の名前 (collection.json の action の名前から)
	std::string recommend;
	for (const auto& key : info.recommend) {
		if (!recommend.empty())
			recommend += "・";
		recommend += BattleActionName(BattleActionOf(key, ""));
	}
	const bool tutorial = DevourTutorialHolding();
	const float untilAuto = std::max(0.f, devourClock_.left - ChallengeAutoAt);
	const bool soon = !tutorial && untilAuto <= 10;
	const std::string tag = "チャレンジ", note = "おすすめ  " + recommend;
	const float x = HudMargin, top = HudMargin + 90 + 24,
	            w = std::max(
	                    {ui.Width(info.name, 26, Jp) + ui.Width(tag, 16, Jp) + 44, ui.Width(info.goal, 18, Jp),
	                     ui.Width(note, 16, Jp), 300.f}) +
	                32,
	            h = tutorial ? 102.f : 144.f;
	Plate(ui, {x, top, w, h}, Cream, 3, 4);
	{
		const float tw = ui.Width(tag, 16, Jp) + 16;
		ui.Fill({x + 12, top + 10, tw, 26}, Red);
		ui.Text(tag, x + 20, top + 11, 16, White, Jp);
		ui.Text(info.name, x + 24 + tw, top + 4, 26, Black, Jp);
	}
	ui.Text(info.goal, x + 16, top + 42, 18, Black, Jp);
	ui.Text(note, x + 16, top + 70, 16, {.35f, .35f, .35f, 1}, Jp);
	if (tutorial)
		return;
	// 移り方: 長押しのキーと、押している間に満ちるゲージ。自動で移るまでの時間 (残り 10 秒からは赤)
	ui.Fill({x, top + 98, w, 3}, Black);
	const InputKey key{"keyboard_enter", "xbox_button_y"};
	const float kx = KeyChip(ui, key, x + 14, top + 108, 28, UI::UsingPad()) + 8;
	const std::string how = "長押しでチャレンジへ";
	ui.Text(how, kx, top + 111, 18, Black, Jp);
	const float hold = std::clamp(challengeHold_ / ChallengeMoveHold, 0.f, 1.f);
	if (hold > 0) {
		const float gx = kx + ui.Width(how, 18, Jp) + 10, gw = 70;
		ui.Fill({gx, top + 116, gw, 12}, Black);
		ui.Fill({gx + 2, top + 118, (gw - 4) * hold, 8}, Red);
	}
	const std::string autoText = soon ? "あと " + std::to_string(int(std::ceil(untilAuto))) + " 秒で移る"
	                                  : "のこり " + Clock(ChallengeAutoAt) + " で自動";
	ui.Text(
	    autoText, x + w - 16 - ui.Width(autoText, 16, Jp), top + 113, 16,
	    soon ? Red : Engine::Vector4{.35f, .35f, .35f, 1}, Jp);
}

// 確認用 (--devour-battle): 吸収を飛ばして、近くの物をいろいろ飲んだことにする
// (種類ごとに近い順に最大 6 個。ステージ全体から集めるので、全部の種類が袋に入る。道をふさぐ物・変形アイテム・建物は除く。デモで全部の能力を試せるよう大きさは問わない)
void ChronoSystem::PrepareBattleForTest(entt::registry& r) {
	const auto& stage = CurrentDevourStage();
	auto& body = life_.bodies[0];
	// 確認用 (--challenge-item-census): ステージにある飲める物を図鑑の項目ごとに数え、能力と一緒に tests/out/item-census-<ステージ>.txt に書いて閉じる
	// (gate は飲める大きさ (cm): 覆える大きさの体の cm。小さい物ほど早く手に入る)
	if (HasArg(L"--challenge-item-census")) {
		struct Count {
			int n = 0;
			float minCm = 1e9f;
		};
		std::map<std::string, Count> counts;
		for (const auto& prop : life_.props) {
			if (prop.visual == 0xffffffffu || prop.orb)
				continue;
			auto e = entt::entity(prop.visual);
			if (!r.valid(e))
				continue;
			const auto* name = r.try_get<NameComponent>(e);
			if (!name)
				continue;
			const std::string id = Collection::KeyOfName(name->name);
			if (id.empty())
				continue;
			auto& c = counts[id];
			++c.n;
			// 覆える大きさ: 物の半径が体の半径の 0.8 倍以下 (DevourCanCover)。体の半径 3*LifeScale から、その大きさになる cm を出す
			const float scale = prop.radius / (.8f * 3.f);
			c.minCm = std::min(c.minCm, stage.start * std::max(1.f, scale));
		}
		std::filesystem::create_directories("tests/out");
		std::ofstream out("tests/out/item-census-" + stage.id + ".txt");
		for (const auto& [id, c] : counts) {
			bool building = false;
			const BattleAction a = ActionOfId(id, building);
			out << id << '\t' << NameOfId(id) << '\t' << (building ? "(building)" : BattleActionName(a)) << '\t' << c.n
			    << '\t' << c.minCm << '\n';
		}
		PostQuitMessage(0);
	}
	// (--devour-battle-all: ステージの物を全部集め、最後の目標の大きさで始める。全部集めたときの強化の点を確かめる用)
	const bool all = HasArg(L"--devour-battle-all");
	float cm = all ? stage.goals.back() : stage.goals[std::min<size_t>(1, stage.goals.size() - 1)];
	if (const wchar_t* arg = wcsstr(GetCommandLineW(), L"--devour-battle-cm="))
		cm = std::max(stage.start, float(_wtof(arg + 19)));
	const float mass = DevourGoalMass(stage, cm);
	std::vector<size_t> candidates;
	for (size_t i = 0; i < life_.props.size(); ++i) {
		const auto& prop = life_.props[i];
		if (!prop.active || prop.sealed || prop.orb || prop.morphItem != LifeMorphItem::None ||
		    prop.visual == 0xffffffffu)
			continue;
		if (std::find(devourWallProps_.begin(), devourWallProps_.end(), int(i)) != devourWallProps_.end() ||
		    int(i) == devourBarricadeProp_ || int(i) == cityGateProp_)
			continue;
		candidates.push_back(i);
	}
	const V from = body.at;
	std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t c) {
		return LifeDistance(life_.props[a].at, from) < LifeDistance(life_.props[c].at, from);
	});
	std::map<std::string, int> taken;
	std::vector<size_t> order;
	for (size_t i : candidates) {
		if (!all && order.size() >= 300)
			break;
		auto e = entt::entity(life_.props[i].visual);
		if (!r.valid(e))
			continue;
		const auto* name = r.try_get<NameComponent>(e);
		if (!name)
			continue;
		const std::string id = Collection::KeyOfName(name->name);
		if (id.empty())
			continue;
		bool building = false;
		ActionOfId(id, building);
		if (building)
			continue;
		if (!all && taken[id] >= 6)
			continue;
		++taken[id];
		order.push_back(i);
	}
	for (size_t i : order) {
		const auto& prop = life_.props[i];
		DevourRecordEaten(r, prop.visual);
		if (!devourEaten_.empty() && devourEaten_.back().visual == prop.visual)
			devourEaten_.back().radius = prop.radius;
		DevourRemoveProp(r, i);
	}
	body.mass = mass;
	devourTutorial_.step = -1;
}

} // namespace Game
