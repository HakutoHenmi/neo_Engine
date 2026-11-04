// DevourAnimEditor.cpp
// 吸収モードの飛びかかりの体の形を作るアニメーションエディタ (このゲーム限定のエディタ拡張)。
// 体をいくつかの「塊」(楕円体) で表し、時間ごとのキーで塊の位置と大きさを決める (Game/Chrono/DevourAnimation.h)。
// 横から・上からの 2 つの図で塊をマウスで動かし、粒子がどう並ぶかをその場で見ながら形を作る。
// キーの無い時間で動かすと、その時間にキーを自動で作る。PLAY 中はゲームのスライムを止めたままその形にして確かめられる。
// 保存先は Resources/Data/devour_animation.json (ゲームは起動時にこれを読む)
#include "EditorUI.h"
#include "../Scenes/GameScene.h"
#include "../Systems/ChronoSystem.h"
#include "../Chrono/DevourAnimation.h"
#include "../../externals/imgui/imgui.h"
#include "FluidMorphShape.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Game {

// 吸収アニメーションのエディタ (ChronoSystem の friend として確認用の値を直接書く)
class DevourAnimEditor {
public:
	static void DrawWindow(GameScene* scene);
	static void StartAt(float time) { time_ = std::clamp(time, 0.0f, Chrono::DevourPounceTime); }

private:
	// 再生中か・繰り返すか・再生の速さ・今の時間
	static bool playing_, loop_;
	static float speed_, time_;
	// ゲームのスライムで確認するか / 目標までの距離 (体の半径の何倍か)
	static bool gamePreview_;
	static float distance_;
	// 選んでいる塊 / 保存していない変更があるか / 最後の結果の文
	static int selected_;
	static bool dirty_;
	static std::string message_;
	// マウスで動かしている物 (図の番号・塊・つまみ。つまみは 0 中心・1 横の半径・2 縦の半径) と、始めのずれ
	struct Drag { int view = -1, blob = -1, handle = 0; Chrono::Vec grab{}; Chrono::DevourAnimation before; };
	static Drag drag_;
	// 時間の帯で動かしているキー (-1 なら時間を動かしている)
	static int timelineKey_;
	static bool timelineActive_;
	static Chrono::DevourAnimation timelineBefore_;
	// コピーした形 / 粒子の通り道を描くか
	static std::vector<Chrono::DevourBlob> clipboard_;
	static bool trails_;

	static bool Ready(GameScene* scene) {
		auto* chrono = scene ? scene->GetChronoSystem() : nullptr;
		return chrono && scene->GetIsPlaying() && chrono->initialized_ && chrono->slimeLife_ && chrono->devour_ && chrono->player_ != entt::null;
	}
	// 変更を取り消しの履歴に積む (before は変更前のアニメーション)
	static void Commit(const Chrono::DevourAnimation& before, const char* label);
	static void DrawToolbar(GameScene* scene);
	static void DrawTimeline();
	static void DrawView(int view, const ImVec2& size);
	static void DrawBlobPanel();
};

bool DevourAnimEditor::playing_ = false;
bool DevourAnimEditor::loop_ = true;
float DevourAnimEditor::speed_ = 0.25f;
float DevourAnimEditor::time_ = 0.0f;
bool DevourAnimEditor::gamePreview_ = false;
float DevourAnimEditor::distance_ = 2.5f;
int DevourAnimEditor::selected_ = 0;
bool DevourAnimEditor::dirty_ = false;
std::string DevourAnimEditor::message_;
DevourAnimEditor::Drag DevourAnimEditor::drag_;
int DevourAnimEditor::timelineKey_ = -1;
bool DevourAnimEditor::timelineActive_ = false;
Chrono::DevourAnimation DevourAnimEditor::timelineBefore_;
std::vector<Chrono::DevourBlob> DevourAnimEditor::clipboard_;
bool DevourAnimEditor::trails_ = true;

namespace {
using Chrono::Vec;
// 塊ごとの色 (粒子と輪郭)
ImU32 BlobColor(int index, int alpha) {
	const ImU32 colors[][3] = {{90, 200, 255}, {255, 170, 60}, {120, 240, 120}, {240, 110, 200}, {250, 230, 80}, {170, 140, 255}, {80, 230, 210}, {255, 110, 110}};
	const auto& c = colors[size_t(index) % std::size(colors)];
	return IM_COL32(c[0], c[1], c[2], alpha);
}
// 確認用の粒子 (普段の体の中の点と、後ろから前への順位)
struct PreviewCloud { std::vector<Vec> rest; std::vector<float> rank; };
const PreviewCloud& Cloud() {
	static const PreviewCloud cloud = [] {
		PreviewCloud c;
		for (const auto& p : Engine::FluidShape::Sample(nullptr, 1800)) c.rest.push_back({p.x, p.y, p.z});
		c.rank = Chrono::DevourRanks(c.rest);
		return c;
	}();
	return cloud;
}
// 飛びかかりの区切り (時間の帯に色で出す)
struct Phase { float begin, end; const char* name; ImU32 color; };
const Phase kPhases[] = {
	{0, Chrono::DevourWindup, "溜め", IM_COL32(90, 90, 140, 255)},
	{Chrono::DevourWindup, Chrono::DevourLandAt, "飛び込む", IM_COL32(70, 120, 90, 255)},
	{Chrono::DevourLandAt, Chrono::DevourCoverEnd, "覆う", IM_COL32(140, 90, 60, 255)},
	{Chrono::DevourCoverEnd, Chrono::DevourPounceTime, "満足", IM_COL32(110, 110, 110, 255)}};
} // namespace

void DevourAnimEditor::Commit(const Chrono::DevourAnimation& before, const char* label) {
	const Chrono::DevourAnimation after = DevourAnim();
	EditorUI::PushUndo({std::string("吸収アニメーション: ") + label, [before] { DevourAnim() = before; }, [after] { DevourAnim() = after; }});
	dirty_ = true;
}

// 上の操作の列: 再生・時間・キー・保存・ゲームでの確認
void DevourAnimEditor::DrawToolbar(GameScene* scene) {
	auto& anim = DevourAnim();
	if (ImGui::Button(playing_ ? "停止" : "再生", ImVec2(60, 0))) {
		playing_ = !playing_;
		if (playing_ && time_ >= Chrono::DevourPounceTime - 1e-4f) time_ = 0;
	}
	ImGui::SameLine();
	ImGui::Checkbox("繰り返す", &loop_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(130);
	ImGui::SliderFloat("##Speed", &speed_, 0.05f, 1.0f, "再生の速さ x%.2f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150);
	ImGui::SliderFloat("##Time", &time_, 0.0f, Chrono::DevourPounceTime, "時間 %.3f 秒");

	// キーの操作 (前後へ移る・足す・消す・形のコピーと貼り付け)
	const int keyHere = Chrono::DevourKeyAt(anim, time_);
	if (ImGui::Button("< 前のキー")) {
		for (int i = int(anim.keys.size()) - 1; i >= 0; --i) if (anim.keys[size_t(i)].time < time_ - 0.004f) { time_ = anim.keys[size_t(i)].time; break; }
	}
	ImGui::SameLine();
	if (ImGui::Button("次のキー >")) {
		for (const auto& key : anim.keys) if (key.time > time_ + 0.004f) { time_ = key.time; break; }
	}
	ImGui::SameLine();
	if (ImGui::Button("キーを追加")) { auto before = anim; Chrono::DevourEnsureKey(anim, time_); Commit(before, "キーを追加"); }
	ImGui::SameLine();
	ImGui::BeginDisabled(keyHere < 0 || anim.keys.size() <= 1);
	if (ImGui::Button("キーを削除")) { auto before = anim; anim.keys.erase(anim.keys.begin() + keyHere); Commit(before, "キーを削除"); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("形をコピー")) { clipboard_ = Chrono::DevourSampleBlobs(anim, time_); message_ = "今の時間の形をコピーしました"; }
	ImGui::SameLine();
	ImGui::BeginDisabled(clipboard_.size() != anim.names.size());
	if (ImGui::Button("形を貼り付け")) { auto before = anim; int key = Chrono::DevourEnsureKey(anim, time_); anim.keys[size_t(key)].blobs = clipboard_; Commit(before, "形を貼り付け"); }
	ImGui::EndDisabled();

	// 保存・読み直し・初めの形に戻す
	if (ImGui::Button(dirty_ ? "保存 *" : "保存")) { std::string text; if (SaveDevourAnim(text)) dirty_ = false; message_ = text; }
	ImGui::SameLine();
	if (ImGui::Button("保存したものを読み直す")) { auto before = anim; std::string text; if (ReloadDevourAnim(text)) { Commit(before, "読み直す"); dirty_ = false; } message_ = text; }
	ImGui::SameLine();
	if (ImGui::Button("初めの形に戻す")) { auto before = anim; anim = Chrono::DevourDefaultAnimation(); selected_ = 0; Commit(before, "初めの形に戻す"); message_ = "初めの形に戻しました (保存するまでファイルは変わりません)"; }

	// ゲームでの確認
	const bool ready = Ready(scene);
	ImGui::BeginDisabled(!ready);
	ImGui::Checkbox("ゲームのスライムで確認 (止まったまま今の時間の形にする)", &gamePreview_);
	ImGui::SameLine();
	if (ImGui::Button("実際に飛びかかる")) scene->GetChronoSystem()->lifeAbsorbBuffer_ = 0.3f;
	ImGui::EndDisabled();
	if (!ready) { ImGui::SameLine(); ImGui::TextDisabled("(PLAY 中の吸収モードで使えます)"); }
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("目標までの距離 (体の半径の倍数)", &distance_, 0.0f, 6.0f, "%.1f");
	ImGui::SameLine();
	ImGui::Checkbox("粒子の通り道を表示 (直前 0.15 秒)", &trails_);
	if (ready && gamePreview_) {
		auto* chrono = scene->GetChronoSystem();
		chrono->devourPreviewTime_ = time_;
		chrono->devourPreviewDistance_ = distance_;
		chrono->devourPreviewHold_ = 3;
	}
}

// 時間の帯: 区切りの色・キー (◆)・今の時間。帯をクリックやドラッグで時間を動かし、◆をドラッグでキーの時間を変える
void DevourAnimEditor::DrawTimeline() {
	auto& anim = DevourAnim();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = (std::max)(200.0f, ImGui::GetContentRegionAvail().x), height = 52.0f;
	const float total = Chrono::DevourPounceTime;
	auto toX = [&](float t) { return at.x + t / total * width; };
	auto toTime = [&](float x) { return std::clamp((x - at.x) / width * total, 0.0f, total); };
	ImGui::InvisibleButton("##DevourTimeline", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	// 区切りの帯
	for (const auto& phase : kPhases) {
		draw->AddRectFilled(ImVec2(toX(phase.begin), at.y), ImVec2(toX(phase.end), at.y + 18), phase.color);
		draw->AddText(ImVec2(toX(phase.begin) + 4, at.y + 2), IM_COL32(235, 235, 235, 255), phase.name);
	}
	draw->AddRectFilled(ImVec2(at.x, at.y + 18), ImVec2(at.x + width, at.y + height), IM_COL32(30, 30, 34, 255));
	// 0.05 秒ごとの目盛り
	for (float t = 0; t <= total + 1e-4f; t += 0.05f) draw->AddLine(ImVec2(toX(t), at.y + 18), ImVec2(toX(t), at.y + 24), IM_COL32(120, 120, 120, 255));
	// キー
	const float keyY = at.y + 36;
	int hoveredKey = -1;
	for (size_t i = 0; i < anim.keys.size(); ++i) {
		const float x = toX(anim.keys[i].time);
		const bool here = std::abs(anim.keys[i].time - time_) <= 0.004f;
		if (hovered && std::abs(mouse.x - x) < 7 && std::abs(mouse.y - keyY) < 9) hoveredKey = int(i);
		const ImU32 color = here ? IM_COL32(255, 220, 90, 255) : hoveredKey == int(i) ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
		draw->AddQuadFilled(ImVec2(x, keyY - 7), ImVec2(x + 6, keyY), ImVec2(x, keyY + 7), ImVec2(x - 6, keyY), color);
	}
	// クリックとドラッグ
	if (ImGui::IsItemActivated()) {
		timelineActive_ = true;
		timelineKey_ = hoveredKey;
		timelineBefore_ = anim;
		playing_ = false;
	}
	if (timelineActive_ && ImGui::IsItemActive()) {
		const float t = toTime(mouse.x);
		if (timelineKey_ >= 0 && size_t(timelineKey_) < anim.keys.size()) {
			// キーは前後のキーを越えない範囲で動かす
			const float low = timelineKey_ > 0 ? anim.keys[size_t(timelineKey_) - 1].time + 0.005f : 0.0f;
			const float high = size_t(timelineKey_) + 1 < anim.keys.size() ? anim.keys[size_t(timelineKey_) + 1].time - 0.005f : total;
			anim.keys[size_t(timelineKey_)].time = std::clamp(t, low, high);
			time_ = anim.keys[size_t(timelineKey_)].time;
		} else time_ = t;
	}
	if (timelineActive_ && ImGui::IsItemDeactivated()) {
		timelineActive_ = false;
		if (timelineKey_ >= 0 && timelineBefore_.keys.size() == anim.keys.size() && std::abs(timelineBefore_.keys[size_t(timelineKey_)].time - anim.keys[size_t(timelineKey_)].time) > 1e-5f) Commit(timelineBefore_, "キーの時間");
		timelineKey_ = -1;
	}
	// 今の時間
	draw->AddLine(ImVec2(toX(time_), at.y), ImVec2(toX(time_), at.y + height), IM_COL32(255, 80, 80, 255), 2.0f);
	if (hovered) ImGui::SetTooltip("クリック/ドラッグ: 時間を動かす    白い印 (キー) をドラッグ: キーの時間を変える (飲み込むのは「覆う」の始め)");
}

// 塊の図 (0: 横から 横軸=前・縦軸=上 / 1: 上から 横軸=前・縦軸=横)。粒子・塊の輪郭・つまみ・目標の例を描き、マウスで塊を動かす
void DevourAnimEditor::DrawView(int view, const ImVec2& size) {
	auto& anim = DevourAnim();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(view == 0 ? "##DevourSide" : "##DevourTop", size);
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	draw->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
	draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(24, 26, 30, 255));
	// 体の半径 1 あたりの画素数と、原点 (跳んだ位置の足元。上からの図は体の中心線)
	const float scale = (std::min)(size.x / 7.0f, size.y / (view == 0 ? 3.2f : 4.2f));
	const ImVec2 origin(at.x + scale * 1.6f, view == 0 ? at.y + size.y - 22 : at.y + size.y * 0.5f);
	// 本当に飛びかかるときは体そのものが前へ進むので、図でも進んだ分ずらす
	const float travel = Chrono::DevourLeapProgress(time_) * distance_;
	auto toScreen = [&](Vec p) { return ImVec2(origin.x + (p.z + travel) * scale, view == 0 ? origin.y - p.y * scale : origin.y + p.x * scale); };
	// 目盛り (体の半径ごと) と地面
	for (int i = -2; i <= 8; ++i) draw->AddLine(ImVec2(origin.x + i * scale, at.y), ImVec2(origin.x + i * scale, at.y + size.y), IM_COL32(45, 48, 55, 255));
	if (view == 0) {
		for (int i = 1; i <= 4; ++i) draw->AddLine(ImVec2(at.x, origin.y - i * scale), ImVec2(at.x + size.x, origin.y - i * scale), IM_COL32(45, 48, 55, 255));
		draw->AddLine(ImVec2(at.x, origin.y), ImVec2(at.x + size.x, origin.y), IM_COL32(150, 130, 100, 255), 2.0f);
	} else {
		for (int i = -2; i <= 2; ++i) draw->AddLine(ImVec2(at.x, origin.y + i * scale), ImVec2(at.x + size.x, origin.y + i * scale), IM_COL32(45, 48, 55, 255));
	}
	// 目標の例 (体の半径の 0.9 倍の箱)
	{
		const float half = 0.45f;
		const ImVec2 a(origin.x + (distance_ - half) * scale, view == 0 ? origin.y - 2 * half * scale : origin.y - half * scale);
		const ImVec2 b(origin.x + (distance_ + half) * scale, view == 0 ? origin.y : origin.y + half * scale);
		draw->AddRectFilled(a, b, IM_COL32(120, 85, 50, 160));
		draw->AddText(ImVec2(a.x, a.y - 16), IM_COL32(200, 160, 110, 255), "目標の例");
	}
	// 粒子 (塊ごとの色)
	const auto& cloud = Cloud();
	const auto blobs = Chrono::DevourSampleBlobs(anim, time_);
	if (!blobs.empty()) {
		const auto start = Chrono::DevourBlobStarts(blobs);
		// 粒子の通り道: 一部の粒子の、少し前から今までの道すじ (進んだ分も含めて)
		if (trails_) {
			constexpr int kSteps = 10;
			const float span = 0.15f;
			std::vector<std::vector<Chrono::DevourBlob>> past(kSteps + 1);
			std::vector<std::vector<float>> pastStart(kSteps + 1);
			std::vector<float> pastTime(kSteps + 1);
			for (int s = 0; s <= kSteps; ++s) {
				pastTime[size_t(s)] = (std::max)(0.0f, time_ - span * float(kSteps - s) / kSteps);
				past[size_t(s)] = Chrono::DevourSampleBlobs(anim, pastTime[size_t(s)]);
				pastStart[size_t(s)] = Chrono::DevourBlobStarts(past[size_t(s)]);
			}
			for (size_t i = 0; i < cloud.rest.size(); i += 23) {
				ImVec2 line[kSteps + 1];
				for (int s = 0; s <= kSteps; ++s) {
					Vec p = Chrono::DevourBlobPoint(cloud.rest[i], cloud.rank[i], past[size_t(s)], pastStart[size_t(s)], pastTime[size_t(s)]);
					p.y = (std::max)(0.03f, p.y);
					// 図は今の時間の進み具合で描くので、昔の点は進んだ差の分だけ後ろへずらす
					p.z += (Chrono::DevourLeapProgress(pastTime[size_t(s)]) - Chrono::DevourLeapProgress(time_)) * distance_;
					line[s] = toScreen(p);
				}
				draw->AddPolyline(line, kSteps + 1, BlobColor(int(Chrono::DevourBlobOf(cloud.rank[i], start)), 110), 0, 1.0f);
			}
		}
		for (size_t i = 0; i < cloud.rest.size(); ++i) {
			Vec p = Chrono::DevourBlobPoint(cloud.rest[i], cloud.rank[i], blobs, start, time_);
			p.y = (std::max)(0.03f, p.y);
			draw->AddCircleFilled(toScreen(p), 1.8f, BlobColor(int(Chrono::DevourBlobOf(cloud.rank[i], start)), 150), 6);
		}
	}
	// 塊の輪郭とつまみ (0 中心・1 横の半径・2 縦の半径)
	auto handle = [&](const Chrono::DevourBlob& blob, int which) {
		Vec p = blob.at;
		if (which == 1) p.z += blob.size.z;
		if (which == 2) { if (view == 0) p.y += blob.size.y; else p.x += blob.size.x; }
		return toScreen(p);
	};
	int hoverBlob = -1, hoverHandle = 0;
	float best = 10.0f;
	for (size_t i = 0; i < blobs.size(); ++i) {
		const auto& blob = blobs[i];
		const ImVec2 c = toScreen(blob.at);
		const ImVec2 radius(blob.size.z * scale, (view == 0 ? blob.size.y : blob.size.x) * scale);
		const bool chosen = int(i) == selected_;
		draw->AddEllipse(c, radius, chosen ? IM_COL32(255, 255, 255, 255) : BlobColor(int(i), 200), 0.0f, 48, chosen ? 2.5f : 1.2f);
		for (int h = 0; h < 3; ++h) {
			const ImVec2 p = handle(blob, h);
			const float d = std::hypot(mouse.x - p.x, mouse.y - p.y);
			// 選んでいる塊のつまみを優先する
			if (hovered && d < best + (chosen ? 4.0f : 0.0f)) { best = d; hoverBlob = int(i); hoverHandle = h; }
		}
		if (chosen) {
			draw->AddCircleFilled(c, 5.0f, IM_COL32(255, 255, 255, 255));
			for (int h = 1; h < 3; ++h) { const ImVec2 p = handle(blob, h); draw->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(255, 220, 90, 255)); }
		} else draw->AddCircleFilled(c, 3.5f, BlobColor(int(i), 255));
		draw->AddText(ImVec2(c.x + 6, c.y - 16), BlobColor(int(i), 255), i < anim.names.size() ? anim.names[i].c_str() : "?");
	}
	// 輪郭の中をクリックしても選べる
	if (hovered && hoverBlob < 0) {
		for (size_t i = 0; i < blobs.size(); ++i) {
			const ImVec2 c = toScreen(blobs[i].at);
			const float rx = blobs[i].size.z * scale, ry = (view == 0 ? blobs[i].size.y : blobs[i].size.x) * scale;
			const float u = (mouse.x - c.x) / (std::max)(1.0f, rx), v = (mouse.y - c.y) / (std::max)(1.0f, ry);
			if (u * u + v * v <= 1) { hoverBlob = int(i); hoverHandle = 0; }
		}
	}
	// マウスの位置 (体の半径の単位)
	auto toWorld = [&](ImVec2 s, Vec base) {
		base.z = (s.x - origin.x) / scale - travel;
		if (view == 0) base.y = (origin.y - s.y) / scale; else base.x = (s.y - origin.y) / scale;
		return base;
	};
	// つかむ・動かす・放す (キーの無い時間なら、つかんだときにキーを作る)
	if (ImGui::IsItemActivated() && hoverBlob >= 0) {
		drag_.view = view; drag_.blob = hoverBlob; drag_.handle = hoverHandle; drag_.before = anim;
		selected_ = hoverBlob; playing_ = false;
		const Vec m = toWorld(mouse, blobs[size_t(hoverBlob)].at);
		drag_.grab = blobs[size_t(hoverBlob)].at - m;
		Chrono::DevourEnsureKey(anim, time_);
	}
	if (drag_.view == view && ImGui::IsItemActive() && drag_.blob >= 0) {
		const int key = Chrono::DevourEnsureKey(anim, time_);
		auto& blob = anim.keys[size_t(key)].blobs[size_t(drag_.blob)];
		const Vec m = toWorld(mouse, blob.at);
		if (drag_.handle == 0) {
			const Vec moved = m + drag_.grab;
			blob.at.z = moved.z;
			if (view == 0) blob.at.y = (std::max)(0.0f, moved.y); else blob.at.x = moved.x;
		} else if (drag_.handle == 1) blob.size.z = (std::max)(0.01f, std::abs(m.z - blob.at.z));
		else if (view == 0) blob.size.y = (std::max)(0.01f, std::abs(m.y - blob.at.y));
		else blob.size.x = (std::max)(0.01f, std::abs(m.x - blob.at.x));
	}
	if (drag_.view == view && ImGui::IsItemDeactivated()) { Commit(drag_.before, "塊を動かす"); drag_ = {}; }
	draw->AddText(ImVec2(at.x + 6, at.y + 4), IM_COL32(220, 220, 220, 255), view == 0 ? "横から (右が前)" : "上から (右が前)");
	draw->PopClipRect();
	if (hovered && drag_.blob < 0) ImGui::SetTooltip("丸いつまみをドラッグ: 塊を動かす   四角いつまみ: 大きさ   輪郭の中をクリック: 選ぶ");
}

// 塊の一覧と、選んだ塊の値
void DevourAnimEditor::DrawBlobPanel() {
	auto& anim = DevourAnim();
	selected_ = std::clamp(selected_, 0, (std::max)(0, int(anim.names.size()) - 1));
	ImGui::TextDisabled("塊は上から順に、体の後ろの粒子から前の粒子を受け持ちます (後ろの塊を上に)");
	for (size_t i = 0; i < anim.names.size(); ++i) {
		ImGui::PushID(int(i));
		ImGui::ColorButton("##Color", ImGui::ColorConvertU32ToFloat4(BlobColor(int(i), 255)), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
		ImGui::SameLine();
		if (ImGui::Selectable(anim.names[i].c_str(), selected_ == int(i), 0, ImVec2(160, 0))) selected_ = int(i);
		ImGui::PopID();
	}
	if (ImGui::Button("塊を追加")) {
		auto before = anim;
		auto blobs = Chrono::DevourSampleBlobs(anim, time_);
		Chrono::DevourBlob blob;
		blob.at = blobs.empty() ? Vec{0, 0.4f, 0.5f} : blobs[size_t(selected_)].at + Vec{0, 0, 0.4f};
		blob.size = {0.01f, 0.01f, 0.01f};
		Chrono::DevourAddBlob(anim, "塊" + std::to_string(anim.names.size() + 1), blob);
		// 今の時間だけ見える大きさにする (他の時間は見えない)
		const int key = Chrono::DevourEnsureKey(anim, time_);
		anim.keys[size_t(key)].blobs.back().size = {0.3f, 0.3f, 0.3f};
		selected_ = int(anim.names.size()) - 1;
		Commit(before, "塊を追加");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(anim.names.size() <= 1);
	if (ImGui::Button("塊を削除")) { auto before = anim; Chrono::DevourRemoveBlob(anim, size_t(selected_)); selected_ = (std::max)(0, selected_ - 1); Commit(before, "塊を削除"); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	// 並び順 (粒子の受け持ちが変わる)
	auto swap = [&](int a, int b) {
		auto before = anim;
		std::swap(anim.names[size_t(a)], anim.names[size_t(b)]);
		for (auto& key : anim.keys) std::swap(key.blobs[size_t(a)], key.blobs[size_t(b)]);
		selected_ = b;
		Commit(before, "塊の順番");
	};
	ImGui::BeginDisabled(selected_ <= 0);
	if (ImGui::Button("上へ")) swap(selected_, selected_ - 1);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selected_ + 1 >= int(anim.names.size()));
	if (ImGui::Button("下へ")) swap(selected_, selected_ + 1);
	ImGui::EndDisabled();
	if (anim.names.empty()) return;

	// 名前
	char name[64];
	snprintf(name, sizeof(name), "%s", anim.names[size_t(selected_)].c_str());
	ImGui::SetNextItemWidth(200);
	if (ImGui::InputText("名前", name, sizeof(name))) { anim.names[size_t(selected_)] = name; dirty_ = true; }
	// 今の時間の値 (変えるとその時間にキーを作る)
	auto blobs = Chrono::DevourSampleBlobs(anim, time_);
	auto blob = blobs[size_t(selected_)];
	const bool onKey = Chrono::DevourKeyAt(anim, time_) >= 0;
	ImGui::TextDisabled(onKey ? "キーの上です (値を変えるとこのキーが変わります)" : "キーの間です (値を変えるとこの時間にキーを作ります)");
	float position[3] = {blob.at.x, blob.at.y, blob.at.z}, radius[3] = {blob.size.x, blob.size.y, blob.size.z};
	static Chrono::DevourAnimation fieldBefore;
	bool changed = false;
	ImGui::SetNextItemWidth(300);
	changed |= ImGui::DragFloat3("位置 (横・上・前)", position, 0.01f, -6.0f, 8.0f, "%.2f");
	if (ImGui::IsItemActivated()) fieldBefore = anim;
	bool released = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SetNextItemWidth(300);
	changed |= ImGui::DragFloat3("大きさ (横・上・前の半径)", radius, 0.01f, 0.01f, 5.0f, "%.2f");
	if (ImGui::IsItemActivated()) fieldBefore = anim;
	released |= ImGui::IsItemDeactivatedAfterEdit();
	// 種類 (全部のキーで同じ)
	int kind = int(blob.kind);
	ImGui::SetNextItemWidth(300);
	if (ImGui::Combo("種類", &kind, "楕円体 (中の詰まった塊)\0口 (開いた口と触手のような唇)\0しずく形 (後ろへ細く伸びる胴体)\0アメーバ (平たく広がり、縁から仮足が出る体)\0大口 (口を大きく開けて飛びかかる体)\0")) {
		auto before = anim;
		Chrono::DevourSetBlobKind(anim, size_t(selected_), Chrono::DevourBlobKind(kind));
		Commit(before, "塊の種類");
		blob.kind = Chrono::DevourBlobKind(kind);
	}
	// 口の値 (この時間の値。変えるとキーを作る)
	float mouth[6] = {blob.thickness, blob.reach, blob.curl, blob.wave, blob.tentacles, blob.twist};
	if (blob.kind == Chrono::DevourBlobKind::Mouth) {
		ImGui::TextDisabled("口: 後ろの粒子ほど奥 (のど)、前の粒子ほど縁と触手の先へ行きます");
		struct Field { const char* label; float min, max; const char* format; };
		const Field fields[] = {{"壁の厚さ (1 で中まで詰まる)", 0.05f, 1.0f, "%.2f"}, {"触手が前へ伸びる長さ", 0.0f, 3.0f, "%.2f"},
			{"縁が内側へ巻き込む強さ", 0.0f, 1.0f, "%.2f"}, {"表面を流れる波の大きさ", 0.0f, 0.5f, "%.2f"},
			{"触手の本数", 0.0f, 16.0f, "%.0f"}, {"流れ込むねじれ (渦)", -4.0f, 4.0f, "%.2f"}};
		for (int i = 0; i < 6; ++i) {
			ImGui::SetNextItemWidth(300);
			changed |= ImGui::SliderFloat(fields[i].label, &mouth[i], fields[i].min, fields[i].max, fields[i].format);
			if (ImGui::IsItemActivated()) fieldBefore = anim;
			released |= ImGui::IsItemDeactivatedAfterEdit();
		}
	}
	if (blob.kind == Chrono::DevourBlobKind::Tail) {
		ImGui::TextDisabled("しずく形: 前が丸く、後ろへ細く伸びて揺れる胴体 (後ろに塊が残らない)");
		ImGui::SetNextItemWidth(300);
		changed |= ImGui::SliderFloat("後ろを細くする強さ (0 で楕円体)", &mouth[2], 0.0f, 1.0f, "%.2f");
		if (ImGui::IsItemActivated()) fieldBefore = anim;
		released |= ImGui::IsItemDeactivatedAfterEdit();
		ImGui::SetNextItemWidth(300);
		changed |= ImGui::SliderFloat("尾の左右の揺れ", &mouth[3], 0.0f, 0.5f, "%.2f");
		if (ImGui::IsItemActivated()) fieldBefore = anim;
		released |= ImGui::IsItemDeactivatedAfterEdit();
	}
	if (blob.kind == Chrono::DevourBlobKind::Gulp) {
		ImGui::TextDisabled("大口: 体 1 つで口を開けます。粒子は形の量に合わせて配るので、口の壁がまばらになりません");
		struct Field { int index; const char* label; float min, max; const char* format; };
		const Field fields[] = {{1, "口の開き (0 で楕円体)", 0.0f, 1.0f, "%.2f"}, {0, "唇と壁の厚さ", 0.05f, 1.0f, "%.2f"},
			{2, "唇が内側へ巻き込む強さ", 0.0f, 1.0f, "%.2f"}, {3, "口から尾へ流れる波", 0.0f, 0.3f, "%.2f"}, {5, "尾の左右の揺れ", 0.0f, 0.5f, "%.2f"}};
		for (const auto& field : fields) {
			ImGui::SetNextItemWidth(300);
			changed |= ImGui::SliderFloat(field.label, &mouth[field.index], field.min, field.max, field.format);
			if (ImGui::IsItemActivated()) fieldBefore = anim;
			released |= ImGui::IsItemDeactivatedAfterEdit();
		}
	}
	if (blob.kind == Chrono::DevourBlobKind::Amoeba) {
		ImGui::TextDisabled("アメーバ: 体の各点をなめらかに動かすので、広げてもちぎれません");
		struct Field { int index; const char* label; float min, max; const char* format; };
		const Field fields[] = {{0, "平たさ (0 で楕円体、1 でパンケーキ)", 0.0f, 1.0f, "%.2f"}, {1, "縁から出る仮足の長さ", 0.0f, 1.0f, "%.2f"},
			{4, "仮足の本数", 0.0f, 12.0f, "%.0f"}, {2, "上ほど前へ傾く強さ", 0.0f, 1.0f, "%.2f"}, {3, "縁を流れる波の大きさ", 0.0f, 0.3f, "%.2f"}};
		for (const auto& field : fields) {
			ImGui::SetNextItemWidth(300);
			changed |= ImGui::SliderFloat(field.label, &mouth[field.index], field.min, field.max, field.format);
			if (ImGui::IsItemActivated()) fieldBefore = anim;
			released |= ImGui::IsItemDeactivatedAfterEdit();
		}
	}
	if (changed) {
		const int key = Chrono::DevourEnsureKey(anim, time_);
		auto& target = anim.keys[size_t(key)].blobs[size_t(selected_)];
		target.at = {position[0], (std::max)(0.0f, position[1]), position[2]};
		target.size = {(std::max)(0.01f, radius[0]), (std::max)(0.01f, radius[1]), (std::max)(0.01f, radius[2])};
		target.thickness = mouth[0]; target.reach = mouth[1]; target.curl = mouth[2];
		target.wave = mouth[3]; target.tentacles = std::round(mouth[4]); target.twist = mouth[5];
	}
	if (released) Commit(fieldBefore, "塊の値");

	// 体の量 (普段の体と比べた割合)。大きく外れると流体が押し合って形が崩れる
	const float volume = Chrono::DevourVolume(blobs) / Chrono::DevourBodyVolume;
	const ImVec4 color = std::abs(volume - 1) < 0.15f ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.5f, 0.4f, 1);
	ImGui::TextColored(color, "体の量 %.0f%% (普段の体と比べて。100%% から大きく外れると流体が押し合って形が崩れます)", volume * 100);
	if (ImGui::Button("この時間の体の量を 100% にする")) {
		auto before = anim;
		const int key = Chrono::DevourEnsureKey(anim, time_);
		const float k = std::cbrt(Chrono::DevourBodyVolume / (std::max)(1e-5f, Chrono::DevourVolume(anim.keys[size_t(key)].blobs)));
		for (auto& b : anim.keys[size_t(key)].blobs) b.size = b.size * k;
		Commit(before, "体の量を合わせる");
	}
}

// ウィンドウ全体
void DevourAnimEditor::DrawWindow(GameScene* scene) {
	// 再生
	if (playing_) {
		time_ += ImGui::GetIO().DeltaTime * speed_;
		if (time_ > Chrono::DevourPounceTime) { if (loop_) time_ = std::fmod(time_, Chrono::DevourPounceTime); else { time_ = Chrono::DevourPounceTime; playing_ = false; } }
	}
	ImGui::TextWrapped("吸収 (飛びかかり) の体の形を、塊 (楕円体) の位置と大きさのキーで作ります。位置と大きさは体の半径が 1 の単位です。");
	DrawToolbar(scene);
	DrawTimeline();
	// 2 つの図を横に並べる
	const float width = (std::max)(200.0f, (ImGui::GetContentRegionAvail().x - 8) * 0.5f);
	const ImVec2 size(width, (std::min)(340.0f, width * 0.62f));
	DrawView(0, size);
	ImGui::SameLine(0, 8);
	DrawView(1, size);
	DrawBlobPanel();
	if (!message_.empty()) ImGui::TextDisabled("%s", message_.c_str());
}

namespace {
// 静的初期化で EditorUI へ登録する
const bool kRegistered = [] {
	EditorExtension ext;
	ext.windowName = "吸収アニメーション";
	// 普段は閉じておく (Window メニューから開く)。--devour-anim-editor を付けて起動すると開いた状態で始まる
	ext.open = wcsstr(GetCommandLineW(), L"--devour-anim-editor") != nullptr;
	// --devour-anim-time=秒 で、始めの時間を決められる (確認用)
	if (const wchar_t* at = wcsstr(GetCommandLineW(), L"--devour-anim-time=")) DevourAnimEditor::StartAt(wcstof(at + 19, nullptr));
	ext.drawWindow = &DevourAnimEditor::DrawWindow;
	EditorUI::RegisterExtension(std::move(ext));
	return true;
}();
} // namespace

} // namespace Game
