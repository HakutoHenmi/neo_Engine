// CollectionUI.h
// タイトルの「ずかん」: これまでに飲みこんだ物の一覧。左に升を並べ、選んだ物を右に大きく出す。
// まだ飲みこんでいない物は影絵で出す。項目は Resources/Data/collection.json、数は保存の記録 (Game/Data/Collection.h)
// 升は 7 段まで見せ、それより多い分は上下に送る (ホイール・選んだ升に合わせて動く)
#pragma once
#include "ScaleUI.h"
#include "../Data/Collection.h"

namespace Game::UI {
// 升の並び (設計座標)
inline constexpr int CollectionCols = 9, CollectionRows = 7;
inline constexpr float CollectionCell = 76, CollectionLeft = 48, CollectionTop = 124;
inline constexpr Rect CollectionBack{1066, 36, 160, 42};
// scroll は上に送った段の数 (小数で、なめらかに動く)
inline Rect CollectionCellRect(int index, float scroll = 0) {
	return {
	    CollectionLeft + float(index % CollectionCols) * CollectionCell + 3,
	    CollectionTop + (float(index / CollectionCols) - scroll) * CollectionCell + 3, CollectionCell - 6,
	    CollectionCell - 6};
}
inline int CollectionRowCount() {
	return (int(Collection::Entries().size()) + CollectionCols - 1) / CollectionCols;
}

// 開いている間の状態: 選んでいる項目・いちばん上に見せる段・開いてからの時間・今見えている段の位置 (first へ追いつく)・集めた数
struct CollectionState {
	int selected = 0, first = 0;
	float age = 0, pickedAt = 0, scroll = 0;
	Collection::Counts counts;
};
// 送る段を範囲に収め、keep なら選んだ升が見える所まで送る
inline void ClampCollectionScroll(CollectionState& state, bool keep) {
	if (keep) {
		const int row = state.selected / CollectionCols;
		if (row < state.first)
			state.first = row;
		if (row >= state.first + CollectionRows)
			state.first = row - CollectionRows + 1;
	}
	state.first = std::clamp(state.first, 0, (std::max)(0, CollectionRowCount() - CollectionRows));
}
// 段が見えている割合 (枠の上下の端からはみ出すほど 0 へ)
inline float CollectionRowShown(int index, float scroll) {
	const float row = float(index / CollectionCols) - scroll;
	return std::clamp(1 - 2 * ((std::max)(0.f, -row) + (std::max)(0.f, row - float(CollectionRows - 1))), 0.f, 1.f);
}
// 開くときに記録を読み直し、最初に見つけた物を選ぶ
inline void OpenCollection(CollectionState& state) {
	state = {};
	state.counts = Collection::Load();
	const auto& entries = Collection::Entries();
	for (size_t i = 0; i < entries.size(); ++i)
		if (state.counts.count(entries[i].id)) {
			state.selected = int(i);
			break;
		}
	ClampCollectionScroll(state, true);
	state.scroll = float(state.first);
}
inline int CollectionCount(const CollectionState& state, int index) {
	const auto& entries = Collection::Entries();
	if (index < 0 || index >= int(entries.size()))
		return 0;
	auto found = state.counts.find(entries[size_t(index)].id);
	return found == state.counts.end() ? 0 : found->second;
}
// 操作: 矢印・十字キーで升を動かし、マウスは乗せるかクリックで選ぶ。閉じるなら true を返す
inline bool UpdateCollection(Canvas& ui, CollectionState& state, float dt) {
	state.age += dt;
	if (BackPressed() || ui.MouseClick(CollectionBack))
		return true;
	const int count = int(Collection::Entries().size());
	if (count == 0)
		return false;
	const int before = state.selected;
	const int dx =
	    (Pressed(DIK_RIGHT) || Pressed(DIK_D) || PadPressed(XINPUT_GAMEPAD_DPAD_RIGHT | Engine::Input::kPadLsRight)
	         ? 1
	         : 0) -
	    (Pressed(DIK_LEFT) || Pressed(DIK_A) || PadPressed(XINPUT_GAMEPAD_DPAD_LEFT | Engine::Input::kPadLsLeft) ? 1
	                                                                                                             : 0);
	const int dy =
	    (Pressed(DIK_DOWN) || Pressed(DIK_S) || PadPressed(XINPUT_GAMEPAD_DPAD_DOWN | Engine::Input::kPadLsDown) ? 1
	                                                                                                             : 0) -
	    (Pressed(DIK_UP) || Pressed(DIK_W) || PadPressed(XINPUT_GAMEPAD_DPAD_UP | Engine::Input::kPadLsUp) ? 1 : 0);
	if (dx || dy) {
		state.selected = std::clamp(state.selected + dx + dy * CollectionCols, 0, count - 1);
		ClampCollectionScroll(state, true);
	}
	// ホイールで段を送る (選んだ升はそのまま。マウスを乗せた升が選ばれる)
	if (const auto* input = Engine::Input::GetInstance()) {
		const float wheel = input->GetMouseWheelDelta();
		if (wheel != 0) {
			state.first -= wheel > 0 ? 1 : -1;
			ClampCollectionScroll(state, false);
		}
	}
	state.scroll += (float(state.first) - state.scroll) * (1 - std::exp(-14 * dt));
	for (int i = 0; i < count; ++i) {
		if (CollectionRowShown(i, state.scroll) < .9f)
			continue;
		const Rect b = CollectionCellRect(i, state.scroll);
		if (ui.MouseClick(b) || (ui.Hover(b) && !UsingPad()))
			state.selected = i;
	}
	if (state.selected != before) {
		state.pickedAt = state.age;
		PlayUISound(UISound::Move);
	}
	return false;
}
inline void DrawCollection(Canvas& ui, const CollectionState& state) {
	namespace S = Scale;
	const auto& entries = Collection::Entries();
	S::Backdrop(ui);
	int found = 0;
	for (int i = 0; i < int(entries.size()); ++i)
		found += CollectionCount(state, i) > 0;
	// 見出しと集めた数
	ui.Big(Str("collection.heading"), 46, 16, 92, S::Black, S::JpBig, S::JpBigBase);
	{
		const float x = 60 + ui.BigWidth(Str("collection.heading"), 92, S::JpBig, S::JpBigBase);
		ui.Text(Str("collection.found"), x, 64, 22, S::Black, S::Jp);
		const std::string number = std::to_string(found) + " / " + std::to_string(entries.size());
		ui.Text(number, x + ui.Width(Str("collection.found"), 22, S::Jp) + 10, 44, 44, S::Red, S::Num);
	}
	S::Hint(ui, "keyboard_escape", "xbox_button_b", Str("collection.back"), 1070, 40);
	// 升: 見つけた物は写真、まだの物は影絵。見えている段の左上から一つずつ現れ、枠の上下の端では小さくなって消える
	for (int i = 0; i < int(entries.size()); ++i) {
		const float shown = CollectionRowShown(i, state.scroll);
		if (shown <= 0)
			continue;
		const Rect b0 = CollectionCellRect(i, state.scroll);
		const int count = CollectionCount(state, i);
		const float pop = S::Overshoot((state.age - float(i - state.first * CollectionCols) * .012f) / .25f) * shown;
		if (pop <= 0)
			continue;
		const Rect b{b0.x + b0.w * .5f * (1 - shown), b0.y + b0.h * .5f * (1 - shown), b0.w * shown, b0.h * shown};
		const Rect art{b.x + b.w * .5f * (1 - pop) + 4, b.y + b.h * .5f * (1 - pop) + 4, b.w * pop - 8, b.h * pop - 8};
		ui.Rounded(b, count ? S::Cream : WithAlpha(S::Black, .08f), 10);
		if (count)
			ui.Photo(Collection::ImagePath(entries[size_t(i)]), art);
		else
			ui.Image(Collection::ImagePath(entries[size_t(i)], true), art, WithAlpha(S::Black, .22f));
		if (i == state.selected) {
			ui.RoundedRing({b.x - 3, b.y - 3, b.w + 6, b.h + 6}, S::Red, 12);
			ui.RoundedRing({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, S::Red, 11);
		}
	}
	// 送れる段があるときは、升の右に今どこを見ているかの棒を出す
	if (const int rows = CollectionRowCount(); rows > CollectionRows) {
		const float h = CollectionRows * CollectionCell, x = CollectionLeft + CollectionCols * CollectionCell + 8;
		ui.Rounded({x, CollectionTop + 3, 8, h - 6}, WithAlpha(S::Black, .12f), 4);
		const float thumb = (h - 6) * float(CollectionRows) / float(rows),
		            y = CollectionTop + 3 + (h - 6 - thumb) * state.scroll / float(rows - CollectionRows);
		ui.Rounded({x, y, 8, thumb}, S::Red, 4);
	}
	// 選んだ物: 大きな写真・名前・説明・数
	if (entries.empty())
		return;
	const int selected = std::clamp(state.selected, 0, int(entries.size()) - 1);
	const auto& entry = entries[size_t(selected)];
	const int count = CollectionCount(state, selected);
	const Rect card{
	    CollectionLeft + CollectionCols * CollectionCell + 28, CollectionTop,
	    1240 - (CollectionLeft + CollectionCols * CollectionCell + 28), 532};
	S::Plate(ui, card, S::Cream);
	const float grow = S::Overshoot((state.age - state.pickedAt) / .3f), size = 250 * grow,
	            bob = std::sin(state.age * 2) * 5;
	const Rect art{card.x + card.w * .5f - size * .5f, card.y + 150 - size * .5f + bob, size, size};
	if (count) {
		ui.Image(Collection::ImagePath(entry, true), {art.x + 10, art.y + 16, art.w, art.h}, WithAlpha(S::Shadow, .8f));
		ui.Photo(Collection::ImagePath(entry), art);
	} else
		ui.Image(Collection::ImagePath(entry, true), art, WithAlpha(S::Black, .22f));
	ui.Fill({card.x + 20, card.y + 300, card.w - 40, 3}, S::Black);
	const std::string& name = count ? entry.name : Str("collection.unknown");
	ui.Text(name, card.x + 24, card.y + 314, 40, S::Black, S::Jp);
	ui.Text(count ? entry.text : Str("collection.unknownText"), card.x + 24, card.y + 376, 21, S::Black, S::Jp);
	if (count) {
		const std::string head = Str("collection.total");
		ui.Text(head, card.x + 24, card.y + 452, 24, S::Black, S::Jp);
		// (× は数字の書体に無いので日本語の書体で書く)
		const float x = card.x + 34 + ui.Width(head, 24, S::Jp);
		ui.Text("×", x, card.y + 446, 30, S::Red, S::Jp);
		ui.Text(std::to_string(count), x + ui.Width("×", 30, S::Jp) + 4, card.y + 430, 52, S::Red, S::Num);
	}
}
} // namespace Game::UI
