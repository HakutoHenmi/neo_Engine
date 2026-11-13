// SlimeDevTools.cpp
// 「スライム、跡で本気出す」制作用のエディタ拡張 (このゲーム限定)。
// エディタ本体 (EditorUI) には EditorExtension として自己登録するだけで、
// このファイルをプロジェクトから外せば機能ごと無くなる。ChronoSystem 側の変更は friend 宣言1行のみ。
// エディタ本体・シーン・スライム本編のシステム・ImGui
#include "EditorUI.h"
#include "SlimeTuningUI.h"
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
		return chrono && scene->GetIsPlaying() && chrono->initialized_ && chrono->player_ != entt::null;
	}
	// 質量の設定 / スライムの欄の描画
	static void SetMass(GameScene* scene, float mass);
	static void DrawSlime(GameScene* scene);
};

// 静的な変数の初期値 (無敵・表示はすべて OFF、質量は開始時の値)
bool SlimeDevTools::invincible_ = false;
bool SlimeDevTools::showEnemies_ = false;
bool SlimeDevTools::showProps_ = false;
float SlimeDevTools::massInput_ = Chrono::LifeStartMass;

// このファイルの中だけで使う関数
namespace {
// スライムの形態の表示名 (行動のデータテーブル LifeActionTable から引く)
const char* ActionName(Chrono::LifeAction action) { return Chrono::LifeActionInfo(action).name; }

// 文字列が指定の文字で始まるか
bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

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
	if (name == "Player") return "プレイヤー";
	// どれでもなければ分類しない
	return {};
}

// 本体の質量・体力を設定する
void SlimeDevTools::SetMass(GameScene* scene, float mass) {
	// スライム本編のシステムと、スライムの状態と本体
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	auto& body = life.bodies[0];
	// 1 から最大までに収める (吸収モードは上限が大きい)
	mass = std::clamp(mass, 1.0f, chrono->devour_ ? Chrono::DevourMaxMass : Chrono::LifeMaxMass);
	// 回帰テストと同じ手順: 質量・体力を揃え、成長量を記録してから形状の再構築を要求する
	body.mass = body.health = body.maxHealth = mass;
	life.absorbedMass = (std::max)(0.0f, mass - Chrono::LifeStartMass);
	++life.revision;
	// 出力に残す
	EditorUI::Log("[SlimeDevTools] mass = " + std::to_string(static_cast<int>(mass)));
}





// スライムの欄 (質量・HP・形態・子分の数、質量の設定とプリセット、全回復・無敵・形態の解放)
void SlimeDevTools::DrawSlime(GameScene* scene) {
	// スライム本編のシステムと、スライムの状態と本体
	auto* chrono = scene->GetChronoSystem();
	auto& life = chrono->life_;
	auto& body = life.bodies[0];
	// 質量の上限 (吸収モードは大きい)
	const float maxMass = chrono->devour_ ? Chrono::DevourMaxMass : Chrono::LifeMaxMass;
	// 質量・形態・HP・子分・取り込み数の表示
	ImGui::Text("質量 %.0f / %.0f (予約込み %.0f)   形態: %s", body.mass, maxMass, life.ReservedMass(), ActionName(body.action));
	ImGui::Text("HP %.0f / %.0f   子分 %d / %d   取り込み数 %d", body.health, body.maxHealth, life.ChildCount(), life.ChildLimit(), life.absorbed);
	// 成長の進み具合 (最大質量に対する割合)
	const float progress = std::clamp(body.mass / maxMass, 0.0f, 1.0f);
	ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0), "成長");

	// 質量の入力と、設定ボタン
	ImGui::SetNextItemWidth(200.0f);
	ImGui::DragFloat("##Mass", &massInput_, 5.0f, 1.0f, maxMass, "質量 %.0f");
	ImGui::SameLine();
	if (ImGui::Button("質量を設定")) SetMass(scene, massInput_);
	// 質量のプリセット (子分の上限が増える質量など)
	// 表示名はゲームの規則の値から作る (名前と値がずれないように)
	struct Preset { const char* name; float mass; };
	const Preset presets[] = {{"開始", Chrono::LifeStartMass}, {"子分6", Chrono::LifeHammerMass}, {"中盤", Chrono::LifeSwordMass},
		{"子分12", Chrono::LifePlaneMass}, {"門", Chrono::CityGateAbsorbMass()}, {"最大", maxMass}};
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
	ImGui::SameLine();
	ImGui::Checkbox("信号機解放", &body.signalUnlocked);

	// 無敵なら、プレイヤーの無敵時間を保ち、HP が 0 にならないようにする
	if (invincible_) {
		if (auto* player = scene->GetComponents().try_get<Chrono::Player>(chrono->player_)) player->invincible = (std::max)(player->invincible, 0.5f);
		body.health = (std::max)(body.health, 1.0f);
	}
}

// ウィンドウ全体
void SlimeDevTools::DrawWindow(GameScene* scene) {
    DrawSlimeTuning();
	// 使えない状態なら説明だけ出す
	if (!Ready(scene)) {
		ImGui::TextWrapped("PLAY 中 (スライム本編) で使えます。Hierarchy の「街: …」分類は常に有効です。");
		return;
	}
	// スライムの状態
	auto& life = scene->GetChronoSystem()->life_;
	(void)life;
	// 各欄 (スライム・ビューポート表示)
	if (scene->IsSlimePlayerDemo()) ImGui::TextWrapped("スライム試験場: 全形態解放 / F5でリセット / Hで被弾分裂");
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
