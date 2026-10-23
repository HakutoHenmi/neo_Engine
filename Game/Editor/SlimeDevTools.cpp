// SlimeDevTools.cpp
// 「スライム、跡で本気出す」制作用のエディタ拡張 (このゲーム限定)。
// エディタ本体 (EditorUI) には EditorExtension として自己登録するだけで、
// このファイルをプロジェクトから外せば機能ごと無くなる。ChronoSystem 側の変更は friend 宣言1行のみ。
// エディタ本体・シーン・スライム本編のシステム・ImGui
#include "EditorUI.h"
#include "../Scenes/GameScene.h"
#include "../Systems/ChronoSystem.h"
#include "../../externals/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#include "../Data/GameData.h"

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// スライム開発ツール (ChronoSystem の friend として内部の状態を直接操作する)
class SlimeDevTools {
public:
	// ウィンドウの中身 / Hierarchy の分類 / ビューポートへの重ね描き (EditorExtension に登録する関数)
	static void DrawWindow(GameScene* scene);
	static std::string Classify(SceneComponents& reg, entt::entity e);
	static void DrawOverlay(GameScene* scene, const ImVec2& imageMin, const ImVec2& imageMax);

private:
	// 無敵・敵の表示・小物の表示の切り替えと、入力中の質量
	static bool invincible_;
	static bool showEnemies_;
	static bool showProps_;
	static float massInput_;

	// スライム本編を PLAY 中で、ChronoSystem の準備ができているか
	static bool Ready(GameScene* scene) {
		// ChronoSystem (シーンが無ければ無し)
		auto* chrono = scene ? scene->GetChronoSystem() : nullptr;
		// 初期化済み・PLAY 中・スライムの状態とプレイヤーがあるか
		return chrono && scene->GetIsPlaying() && chrono->initialized_ && chrono->slimeLife_ && chrono->player_ != entt::null;
	}
	// ChronoSystem の門の処理は entt::registry を要求する。GameScene は const でしか公開しないため、
	// 回帰テスト (tests/SlimeLifeValidationScene.h) と同じくここだけ const を外す
	static entt::registry& Registry(GameScene* scene) { return const_cast<entt::registry&>(scene->GetRegistry()); }
	// 質量の設定 / 敵の全滅 / 段階のジャンプ / ワープ / ステージとスライムの欄の描画
	static void SetMass(GameScene* scene, float mass);
	static void KillStepEnemies(GameScene* scene, bool everyStep);
	static void JumpToStep(GameScene* scene, int step);
	static void Warp(GameScene* scene, Chrono::Vec at);
	static void DrawStage(GameScene* scene);
	static void DrawSlime(GameScene* scene);
};

// 静的な変数の初期値 (無敵・表示はすべて OFF、質量は開始時の値)
bool SlimeDevTools::invincible_ = false;
bool SlimeDevTools::showEnemies_ = false;
bool SlimeDevTools::showProps_ = false;
float SlimeDevTools::massInput_ = Chrono::LifeStartMass;

// このファイルの中だけで使う関数
namespace {
// スライムの形態の表示名
const char* ActionName(Chrono::LifeAction action) {
	switch (action) {
	case Chrono::LifeAction::Idle: return "通常";
	case Chrono::LifeAction::Engulf: return "取り込み中";
	case Chrono::LifeAction::Hammer: return "ハンマー";
	case Chrono::LifeAction::Sword: return "剣";
	case Chrono::LifeAction::Plane: return "飛行機";
	case Chrono::LifeAction::Tire: return "タイヤ";
	case Chrono::LifeAction::Hydrant: return "消火栓";
	case Chrono::LifeAction::Restore: return "復元中";
	}
	return "?";
}

// 文字列が指定の文字で始まるか
bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// ワープのボタン (各エリアの代表地点) と、段階ごとの開始位置 (Resources/Data/editor.json の slimeDevTools)
struct WarpPoint { std::string label; Chrono::Vec at; };
struct DevToolsData { std::vector<WarpPoint> warpPoints; std::vector<Chrono::Vec> stepStarts; };
const DevToolsData& DevTools() {
	static const DevToolsData data = [] {
		const auto& json = Data::Require(Data::Load("editor.json"), "slimeDevTools", "editor.json");
		auto vec = [](const nlohmann::json& v) { auto a = Data::ReadArray<float, 3>(v, "editor.json slimeDevTools"); return Chrono::Vec{a[0], a[1], a[2]}; };
		DevToolsData out;
		for (const auto& point : json.at("warpPoints")) out.warpPoints.push_back({point.at("label").get<std::string>(), vec(point.at("at"))});
		for (const auto& at : json.at("stepStarts")) out.stepStarts.push_back(vec(at));
		if (out.stepStarts.size() != size_t(Chrono::CityStepCount)) throw std::runtime_error("editor.json: slimeDevTools.stepStarts must have one position per stage");
		return out;
	}();
	return data;
}
} // namespace

// Hierarchy の分類: ChronoSystem が作るエンティティの名前から、街の小物・路面・建物などに分ける
std::string SlimeDevTools::Classify(SceneComponents& reg, entt::entity e) {
	// 名前が無ければ分類しない
	auto* nc = reg.try_get<NameComponent>(e);
	if (!nc) return {};
	const std::string& name = nc->name;
	// 名前の始まりで分ける
	if (StartsWith(name, "Chrono City prop ")) return "街: 吸い込める小物";
	if (StartsWith(name, "Chrono City surface ")) return "街: 路面・地形";
	if (StartsWith(name, "Chrono City building")) return "街: 建物の当たり判定";
	if (StartsWith(name, "Chrono City")) return "街: その他";
	// Chrono で始まるものは、当たり判定かそれ以外か
	if (StartsWith(name, "Chrono ")) return name.find(" collision") != std::string::npos ? "Chrono: 当たり判定" : "Chrono: 生成物";
	// プレイヤーとその関連物
	if (name == "Player" || name == "PlayerDeathFluid" || name == "LostFluidPickup" || name == "CanStatusVisual") return "プレイヤー";
	// どれでもなければ分類しない
	return {};
}

// 本体の質量・体力を設定する
void SlimeDevTools::SetMass(GameScene* scene, float mass) {
	// スライム本編のシステムと、スライムの状態と本体
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	auto& body = life.bodies[0];
	// 1 から最大までに収める
	mass = std::clamp(mass, 1.0f, Chrono::LifeMaxMass);
	// 回帰テストと同じ手順: 質量・体力を揃え、成長量を記録してから形状の再構築を要求する
	body.mass = body.health = body.maxHealth = mass;
	life.absorbedMass = (std::max)(0.0f, mass - Chrono::LifeStartMass);
	++life.revision;
	// 出力に残す
	EditorUI::Log("[SlimeDevTools] mass = " + std::to_string(static_cast<int>(mass)));
}

// 生存中の敵を倒す (everyStep が false なら今の段階の敵だけ)
void SlimeDevTools::KillStepEnemies(GameScene* scene, bool everyStep) {
	auto* chrono = scene->GetChronoSystem();
	// プレイヤーのインク (倒した数を数える)
	auto* ink = scene->GetComponents().try_get<Chrono::InkPlayer>(chrono->player_);
	int killed = 0;
	// 敵を順に調べる
	for (auto& enemy : chrono->swarm_) {
		// 生きていないか、別の段階の敵 (全段階でなければ) は飛ばす
		if (!Chrono::SwarmAlive(enemy) || (!everyStep && enemy.step != chrono->life_.step)) continue;
		// 体力を 0 にして倒れた状態にする
		enemy.health = 0;
		enemy.phase = Chrono::SwarmPhase::Dead;
		// 倒した数を数える
		if (ink) ++ink->swarmKills;
		++killed;
	}
	EditorUI::Log("[SlimeDevTools] defeated " + std::to_string(killed) + " enemies");
}

// 指定の段階から始める: 敵を消し、必要な門を開け、その段階の敵を配置してワープする
void SlimeDevTools::JumpToStep(GameScene* scene, int step) {
	// スライム本編のシステムと、スライムの状態
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	// 段階の番号を範囲に収める
	step = std::clamp(step, 0, Chrono::CityStepCount - 1);
	// 生きている敵をすべて倒す
	KillStepEnemies(scene, true);
	// 段階2 (見回り) より先はバリケード、段階5 (騎士団) より先は鉄門を越えている前提
	if (step >= 2) chrono->OpenCityGates(Registry(scene), step < 5);
	// 段階を設定し、少し待ってからその段階の敵を出す
	life.step = step;
	life.stepDelay = 2;
	chrono->SpawnCityStep(step);
	// 画面に段階の名前を 3.5 秒出す
	const auto& info = Chrono::CityStepInfo(step);
	life.message = "段階 " + std::to_string(step + 1) + " / " + std::to_string(Chrono::CityStepCount) + "  " + info.title;
	life.messageAge = 3.5f;
	// 段階ごとのワープ先
	Warp(scene, DevTools().stepStarts[size_t(step)]);
}

// 本体と子分をまとめて指定位置へ移動する
void SlimeDevTools::Warp(GameScene* scene, Chrono::Vec at) {
	// スライム本編のシステムと、スライムの状態と本体
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	auto& body = life.bodies[0];
	// 本体の移動量
	const Chrono::Vec offset = at - body.at;
	// 子分も一緒に移動させ、本体から遠く離れて取り残されないようにする
	for (auto& b : life.bodies) {
		if (b.mass > 0) b.at = b.at + offset;
	}
	// プレイヤーの速度を 0 にする
	if (auto* player = scene->GetComponents().try_get<Chrono::Player>(chrono->player_)) player->velocity = {};
	// 形の作り直しを要求する
	++life.revision;
}

// ステージ進行の欄 (現在地・段階・目標・敵の数・門の状態とボタン、ワープ)
void SlimeDevTools::DrawStage(GameScene* scene) {
	// スライム本編のシステムと、スライムの状態
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	// 今の段階の情報と、本体のいるエリア
	const auto& info = Chrono::CityStepInfo(life.step);
	const auto area = Chrono::CityAreaAt(life.bodies[0].at);
	// 今の段階の敵の数と、生きている敵の総数を数える
	int alive = 0, total = 0;
	for (const auto& enemy : chrono->swarm_) {
		if (!Chrono::SwarmAlive(enemy)) continue;
		++total;
		if (enemy.step == life.step) ++alive;
	}
	// 現在地・段階・目標・敵の数
	ImGui::Text("現在地: %s", Chrono::CityAreaName(area));
	ImGui::Text("段階 %d / %d  %s", life.step + 1, Chrono::CityStepCount, info.title);
	ImGui::TextWrapped("目標: %s", info.goal);
	ImGui::Text("この段階の敵: %d   生存中の全敵: %d", alive, total);
	// 門の状態
	const auto gates = chrono->cityGates_;
	ImGui::Text("工事バリケード: %s   鉄門: %s", gates.barricadeOpen ? "開" : "閉", gates.gateOpen ? "開" : "閉");

	// 敵の全滅と、次の段階への移動のボタン
	if (ImGui::Button("この段階の敵を全滅")) KillStepEnemies(scene, false);
	ImGui::SameLine();
	if (ImGui::Button("次の段階へ")) {
		// 自然な進行と同じ経路を通す: 全滅させ、必要な門を開け、待ち時間を0にする
		KillStepEnemies(scene, false);
		if (life.step == 1 || life.step == 4) chrono->OpenCityGates(Registry(scene), life.step == 1);
		life.stepDelay = 0;
	}
	// 門を開くボタン
	if (ImGui::Button("バリケードを開く")) chrono->OpenCityGates(Registry(scene), true);
	ImGui::SameLine();
	if (ImGui::Button("すべての門を開く")) chrono->OpenCityGates(Registry(scene), false);

	// 開始する段階を選ぶ一覧と、開始ボタン
	static int jumpStep = 0;
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("##JumpStep", (std::to_string(jumpStep + 1) + ". " + Chrono::CityStepInfo(jumpStep).title).c_str())) {
		for (int i = 0; i < Chrono::CityStepCount; ++i) {
			std::string label = std::to_string(i + 1) + ". " + Chrono::CityStepInfo(i).title;
			if (ImGui::Selectable(label.c_str(), jumpStep == i)) jumpStep = i;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("この段階から開始")) JumpToStep(scene, jumpStep);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("生存中の敵を倒し、必要な門を開けてから\n指定段階の敵を配置し、そのエリアへワープします");

	ImGui::SeparatorText("ワープ");
	// ワープ先のボタンを 2 列で並べる
	const auto& points = DevTools().warpPoints;
	for (size_t i = 0; i < points.size(); ++i) {
		if (i % 2) ImGui::SameLine();
		if (ImGui::Button(points[i].label.c_str(), ImVec2(170.0f, 0))) Warp(scene, points[i].at);
	}
}

// スライムの欄 (質量・HP・形態・子分の数、質量の設定とプリセット、全回復・無敵・形態の解放)
void SlimeDevTools::DrawSlime(GameScene* scene) {
	// スライム本編のシステムと、スライムの状態と本体
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	auto& body = life.bodies[0];
	// 質量・形態・HP・子分・取り込み数の表示
	ImGui::Text("質量 %.0f / %.0f (予約込み %.0f)   形態: %s", body.mass, Chrono::LifeMaxMass, life.ReservedMass(), ActionName(body.action));
	ImGui::Text("HP %.0f / %.0f   子分 %d / %d   取り込み数 %d", body.health, body.maxHealth, life.ChildCount(), life.ChildLimit(), life.absorbed);
	// 成長の進み具合 (最大質量に対する割合)
	const float progress = std::clamp(body.mass / Chrono::LifeMaxMass, 0.0f, 1.0f);
	ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0), "成長");

	// 質量の入力と、設定ボタン
	ImGui::SetNextItemWidth(200.0f);
	ImGui::DragFloat("##Mass", &massInput_, 5.0f, 1.0f, Chrono::LifeMaxMass, "質量 %.0f");
	ImGui::SameLine();
	if (ImGui::Button("質量を設定")) SetMass(scene, massInput_);
	// 質量のプリセット (形態の解放に必要な質量など)
	// 表示名はゲームの規則の値から作る (名前と値がずれないように)
	struct Preset { const char* name; float mass; };
	const Preset presets[] = {{"開始", Chrono::LifeStartMass}, {"ハンマー", Chrono::LifeHammerMass}, {"剣", Chrono::LifeSwordMass},
		{"飛行機", Chrono::LifePlaneMass}, {"門", Chrono::CityGateAbsorbMass()}, {"最大", Chrono::LifeMaxMass}};
	// 3 個ずつ並べ、押したらその質量にする
	for (size_t i = 0; i < std::size(presets); ++i) {
		if (i % 3) ImGui::SameLine();
		const std::string label = std::string(presets[i].name) + " " + std::to_string(int(presets[i].mass));
		if (ImGui::Button(label.c_str(), ImVec2(110.0f, 0))) {
			massInput_ = presets[i].mass;
			SetMass(scene, presets[i].mass);
		}
	}
	// 全回復と無敵
	if (ImGui::Button("全回復")) body.health = body.maxHealth;
	ImGui::SameLine();
	ImGui::Checkbox("無敵 (このウィンドウ表示中)", &invincible_);
	// タイヤと消火栓の形態の解放
	ImGui::Checkbox("タイヤ解放", &body.tireUnlocked);
	ImGui::SameLine();
	ImGui::Checkbox("消火栓解放", &body.hydrantUnlocked);

	// 無敵なら、プレイヤーの無敵時間を保ち、HP が 0 にならないようにする
	if (invincible_) {
		if (auto* player = scene->GetComponents().try_get<Chrono::Player>(chrono->player_)) player->invincible = (std::max)(player->invincible, 0.5f);
		body.health = (std::max)(body.health, 1.0f);
	}
}

// ウィンドウ全体
void SlimeDevTools::DrawWindow(GameScene* scene) {
	// 使えない状態なら説明だけ出す
	if (!Ready(scene)) {
		ImGui::TextWrapped("PLAY 中 (スライム本編) で使えます。Hierarchy の「街: …」分類は常に有効です。");
		return;
	}
	// スライムの状態
	auto& life = scene->GetChronoSystem()->life_;
	// 調合室を開いているときの注意
	if (life.lab) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "調合室を開いています (SAVE & DEPLOY で出撃)");
	// 各欄 (ステージ進行・スライム・ビューポート表示)
	if (ImGui::CollapsingHeader("ステージ進行", ImGuiTreeNodeFlags_DefaultOpen)) DrawStage(scene);
	if (ImGui::CollapsingHeader("スライム", ImGuiTreeNodeFlags_DefaultOpen)) DrawSlime(scene);
	if (ImGui::CollapsingHeader("ビューポート表示")) {
		ImGui::Checkbox("敵の位置と体力", &showEnemies_);
		ImGui::Checkbox("吸い込める小物 (必要質量)", &showProps_);
	}
}

// ビューポートに敵の位置と体力、吸い込める小物の必要質量を重ねて描く
void SlimeDevTools::DrawOverlay(GameScene* scene, const ImVec2& imageMin, const ImVec2& imageMax) {
	// 使えないか、どちらの表示も OFF なら何もしない
	if (!Ready(scene) || (!showEnemies_ && !showProps_)) return;
	auto* chrono = scene->GetChronoSystem();
	// ビューポートの画像の大きさ (小さすぎれば描かない)
	const float w = imageMax.x - imageMin.x, h = imageMax.y - imageMin.y;
	if (w < 1.0f || h < 1.0f) return;
	// ビューとプロジェクションの行列
	const auto view = scene->GetViewCamera().View();
	const auto proj = scene->GetViewCamera().Proj();
	// 3D の位置を画像の上の位置に変換する (画面内なら true)
	auto project = [&](Chrono::Vec p, ImVec2& out) {
		auto v = DirectX::XMVector3Project(DirectX::XMVectorSet(p.x, p.y, p.z, 1.0f), 0, 0, w, h, 0.0f, 1.0f, proj, view, DirectX::XMMatrixIdentity());
		const float z = DirectX::XMVectorGetZ(v);
		out = ImVec2(imageMin.x + DirectX::XMVectorGetX(v), imageMin.y + DirectX::XMVectorGetY(v));
		return z > 0.0f && z < 1.0f && out.x >= imageMin.x && out.x <= imageMax.x && out.y >= imageMin.y && out.y <= imageMax.y;
	};
	// ウィンドウの描画リストに、画像の範囲だけ描く
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imageMin, imageMax, true);
	// 本体の質量
	const float mass = chrono->life_.bodies[0].mass;
	if (showProps_) {
		// 使える小物ごとに、必要な質量を円と数字で描く (足りていれば緑、足りなければ赤)
		for (const auto& prop : chrono->life_.props) {
			ImVec2 at;
			if (!prop.active || !project(prop.at, at)) continue;
			const ImU32 color = prop.required <= mass ? IM_COL32(120, 255, 140, 220) : IM_COL32(255, 120, 90, 200);
			draw->AddCircle(at, 5.0f, color, 0, 1.5f);
			char text[32];
			snprintf(text, sizeof(text), "%.0f", prop.required);
			draw->AddText(ImVec2(at.x + 6.0f, at.y - 7.0f), color, text);
		}
	}
	if (showEnemies_) {
		// 生きている敵ごとに、円・体力の棒・名前を描く (今の段階は赤、それ以外は灰色)
		for (const auto& enemy : chrono->swarm_) {
			ImVec2 at;
			if (!Chrono::SwarmAlive(enemy) || !project(enemy.at, at)) continue;
			const bool current = enemy.step == chrono->life_.step;
			const ImU32 color = current ? IM_COL32(255, 70, 70, 230) : IM_COL32(170, 170, 170, 160);
			draw->AddCircle(at, 9.0f, color, 0, 2.0f);
			const float ratio = enemy.maxHealth > 0 ? std::clamp(enemy.health / enemy.maxHealth, 0.0f, 1.0f) : 0.0f;
			draw->AddRectFilled(ImVec2(at.x - 14, at.y - 18), ImVec2(at.x + 14, at.y - 15), IM_COL32(0, 0, 0, 160));
			draw->AddRectFilled(ImVec2(at.x - 14, at.y - 18), ImVec2(at.x - 14 + 28 * ratio, at.y - 15), color);
			draw->AddText(ImVec2(at.x + 11, at.y - 6), color, Chrono::CityEnemyName(static_cast<Chrono::CityEnemy>(enemy.kind)));
		}
	}
	// 描く範囲の制限を戻す
	draw->PopClipRect();
}

namespace {
// 静的初期化で EditorUI へ登録する (EditorUI::Extensions は関数内 static なので初期化順に依存しない)
const bool kRegistered = [] {
	// ウィンドウの名前と、各関数を登録する
	EditorExtension ext;
	ext.windowName = "Slime Dev Tools";
	ext.drawWindow = &SlimeDevTools::DrawWindow;
	ext.classify = &SlimeDevTools::Classify;
	ext.drawViewportOverlay = &SlimeDevTools::DrawOverlay;
	EditorUI::RegisterExtension(std::move(ext));
	return true;
}();
} // namespace

} // namespace Game
