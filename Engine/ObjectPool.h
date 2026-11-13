#pragma once
// ===============================
//  ObjectPool : 固定容量のオブジェクトプール (Object Pool Pattern)
//  - 生成・破棄の多いオブジェクト (パーティクル・弾・エフェクト) を、最初に確保した領域の中で使い回す。
//  - 実行中はメモリの確保・解放を行わない。空きは free list で O(1) に取り出す。
//  - 使用中の要素は密な添字の配列で持つので、更新・描画は使用中の要素だけを走査する。
//  - 返却は使用中の配列の末尾と入れ替えて O(1)。ForEachActive の中で返却しても安全。
// ===============================
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Engine {

template<class T> class ObjectPool {
public:
	ObjectPool() = default;
	explicit ObjectPool(size_t capacity) { Reset(capacity); }

	// 容量を決めて領域を確保し直す (すべて空きになる)
	void Reset(size_t capacity) {
		items_.assign(capacity, T{});
		active_.clear();
		active_.reserve(capacity);
		slotOfActive_.assign(capacity, kInactive);
		free_.clear();
		free_.reserve(capacity);
		// 若い番号から使われるように逆順に積む
		for (size_t i = capacity; i-- > 0;) free_.push_back(static_cast<uint32_t>(i));
	}

	// 空きを 1 つ取り出す。空きが無ければ nullptr (呼び出し側は放出を諦める)
	T* Acquire() {
		if (free_.empty()) return nullptr;
		const uint32_t index = free_.back();
		free_.pop_back();
		slotOfActive_[index] = static_cast<uint32_t>(active_.size());
		active_.push_back(index);
		return &items_[index];
	}

	// 使い終わった要素を返す
	void Release(const T* item) {
		if (!item || items_.empty()) return;
		const ptrdiff_t index = item - items_.data();
		if (index < 0 || static_cast<size_t>(index) >= items_.size()) return;
		ReleaseIndex(static_cast<uint32_t>(index));
	}

	// すべて返す
	void ReleaseAll() {
		for (uint32_t index : active_) {
			slotOfActive_[index] = kInactive;
			free_.push_back(index);
		}
		active_.clear();
	}

	// 使用中の要素だけを走査する。fn(T&) が false を返した要素はその場で返却する
	template<class Fn> void ForEachActive(Fn&& fn) {
		// 末尾から回すと、返却による入れ替えで未走査の要素を飛ばさない
		for (size_t i = active_.size(); i-- > 0;) {
			const uint32_t index = active_[i];
			if (!fn(items_[index])) ReleaseIndex(index);
		}
	}
	template<class Fn> void ForEachActive(Fn&& fn) const {
		for (size_t i = active_.size(); i-- > 0;) fn(items_[active_[i]]);
	}

	size_t Capacity() const { return items_.size(); }
	size_t ActiveCount() const { return active_.size(); }
	size_t FreeCount() const { return free_.size(); }
	bool IsActive(const T* item) const {
		const ptrdiff_t index = item - items_.data();
		return index >= 0 && static_cast<size_t>(index) < items_.size() && slotOfActive_[static_cast<size_t>(index)] != kInactive;
	}

private:
	static constexpr uint32_t kInactive = 0xffffffffu;

	void ReleaseIndex(uint32_t index) {
		const uint32_t slot = slotOfActive_[index];
		if (slot == kInactive) return; // 二重返却は無視する
		// 末尾の使用中要素を空いた位置へ移す
		const uint32_t last = active_.back();
		active_[slot] = last;
		slotOfActive_[last] = slot;
		active_.pop_back();
		slotOfActive_[index] = kInactive;
		free_.push_back(index);
	}

	std::vector<T> items_;               // 実体 (容量ぶん最初に確保する)
	std::vector<uint32_t> active_;       // 使用中の要素の番号 (密)
	std::vector<uint32_t> slotOfActive_; // 要素の番号 → active_ の位置 (未使用は kInactive)
	std::vector<uint32_t> free_;         // 空きの番号 (free list)
};

} // namespace Engine
