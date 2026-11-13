// DesignPatternTests.cpp
// エンジン層のデザインパターン (Object Pool / Command / Strategy) の回帰テスト。
// 描画やウィンドウを初期化せずに、ヘッダーだけで動く部分を確認する。
#include "../Engine/ObjectPool.h"
#include "../Engine/CommandHistory.h"
#include "../Engine/EmissionShapes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Require(bool condition, const char* message) {
	if (!condition) {
		std::printf("FAILED: %s\n", message);
		++failures;
	}
}

// ---- Object Pool ----
struct Item {
	int value = 0;
};

void TestObjectPool() {
	Engine::ObjectPool<Item> pool(4);
	Require(pool.Capacity() == 4 && pool.FreeCount() == 4 && pool.ActiveCount() == 0, "pool starts empty");
	std::vector<Item*> taken;
	for (int i = 0; i < 4; ++i) {
		Item* item = pool.Acquire();
		Require(item != nullptr, "pool hands out up to capacity");
		item->value = i;
		taken.push_back(item);
	}
	Require(pool.Acquire() == nullptr, "pool refuses beyond capacity instead of allocating");
	Require(std::set<Item*>(taken.begin(), taken.end()).size() == 4, "pool hands out distinct slots");

	// 返したものは使い回され、二重返却は無視される
	pool.Release(taken[1]);
	pool.Release(taken[1]);
	Require(pool.ActiveCount() == 3 && pool.FreeCount() == 1, "double release is ignored");
	Require(!pool.IsActive(taken[1]) && pool.IsActive(taken[0]), "release marks only that slot free");
	Require(pool.Acquire() == taken[1], "released slot is reused");

	// 走査中に返却しても、全要素を 1 回ずつ見る
	int visited = 0, sum = 0;
	pool.ForEachActive([&](Item& item) {
		++visited;
		sum += item.value;
		return item.value % 2 == 0; // 奇数は返す
	});
	Require(visited == 4, "ForEachActive visits each active item once while releasing");
	Require(sum == 0 + 1 + 2 + 3, "ForEachActive sees every value");
	Require(pool.ActiveCount() == 2 && pool.FreeCount() == 2, "items returning false are released");
	int remaining = 0;
	const auto& constPool = pool;
	constPool.ForEachActive([&](const Item& item) { remaining += item.value % 2 == 0 ? 1 : 100; });
	Require(remaining == 2, "only kept items remain active");

	pool.ReleaseAll();
	Require(pool.ActiveCount() == 0 && pool.FreeCount() == 4, "ReleaseAll frees every slot");

	// 大量の生成と破棄を繰り返しても、容量を超えず領域を確保し直さない
	Engine::ObjectPool<Item> big(1000);
	const Item* first = big.Acquire();
	big.Release(first);
	for (int frame = 0; frame < 200; ++frame) {
		for (int i = 0; i < 20; ++i) if (Item* item = big.Acquire()) item->value = frame;
		big.ForEachActive([&](Item& item) { return frame - item.value < 30; });
	}
	Require(big.ActiveCount() <= 1000 && big.ActiveCount() == 600, "steady state keeps 30 frames of items");
	Require(big.Capacity() == 1000, "pool never grows");
}

// ---- Command ----
class AddCommand final : public Engine::ICommand {
public:
	AddCommand(int& target, int amount) : target_(target), amount_(amount) {}
	void Execute() override { target_ += amount_; }
	void Undo() override { target_ -= amount_; }
	std::string Name() const override { return "Add " + std::to_string(amount_); }

private:
	int& target_;
	int amount_;
};

void TestCommandHistory() {
	int value = 0;
	Engine::CommandHistory history(3);
	Require(!history.CanUndo() && !history.Undo() && !history.Redo(), "empty history does nothing");

	history.Execute(std::make_unique<AddCommand>(value, 1));
	history.Execute(std::make_unique<AddCommand>(value, 10));
	Require(value == 11 && history.NextUndoName() == "Add 10", "Execute runs and records");

	Require(history.Undo() && value == 1, "Undo reverts the last command");
	Require(history.CanRedo() && history.NextRedoName() == "Add 10", "undone command moves to redo");
	Require(history.Redo() && value == 11 && !history.CanRedo(), "Redo re-executes");

	history.Undo();
	// 実行済みの操作の記録 (ドラッグ編集) は実行せず、Redo の履歴を消す
	value += 5;
	history.Record(std::make_unique<Engine::FunctionCommand>("Drag", [&] { value -= 5; }, [&] { value += 5; }));
	Require(value == 6 && !history.CanRedo(), "Record does not execute and clears redo");
	history.Undo();
	history.Undo();
	Require(value == 0 && !history.CanUndo(), "undo walks back through mixed command types");

	// 最大件数を超えたら古いものから捨てる
	for (int i = 1; i <= 5; ++i) history.Execute(std::make_unique<AddCommand>(value, i));
	Require(value == 15 && history.UndoCount() == 3, "history is capped at max depth");
	while (history.Undo()) {}
	Require(value == 3, "only the newest commands can be undone (1 + 2 stay applied)");
}

// ---- Strategy (放出形状) ----
// 決まった順の値を返す乱数 (形状の計算を再現できるようにする)
std::vector<float> sequence;
size_t cursor = 0;
float SequenceRandom(float min, float max) {
	const float t = sequence[cursor++ % sequence.size()];
	return min + t * (max - min);
}

float Length(const Engine::Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

void TestEmissionShapes() {
	using Engine::EmissionShape;
	const Engine::EmissionShapeParams params{2.0f, 0.5f};
	sequence = {0.1f, 0.7f, 0.9f, 0.3f};

	// 点: 何も変えない
	Engine::Vector3 position{1, 2, 3}, velocity{0, 4, 0};
	cursor = 0;
	Engine::EmissionShapeFor(EmissionShape::Point).Shape(params, &SequenceRandom, position, velocity);
	Require(position.x == 1 && position.y == 2 && position.z == 3 && velocity.y == 4 && cursor == 0, "point keeps base values and draws no random numbers");

	// 球: 中心から半径以内、乱数は 3 回
	for (int trial = 0; trial < 50; ++trial) {
		sequence = {std::fmod(trial * 0.137f, 1.0f), std::fmod(trial * 0.291f, 1.0f), std::fmod(trial * 0.613f + 0.01f, 1.0f)};
		position = {0, 0, 0};
		velocity = {0, 0, 0};
		cursor = 0;
		Engine::EmissionShapeFor(EmissionShape::Sphere).Shape(params, &SequenceRandom, position, velocity);
		Require(Length(position) <= params.radius + 1e-4f, "sphere emits inside its radius");
		Require(std::abs(velocity.x - position.x * 0.5f) < 1e-5f, "sphere pushes velocity outward");
		Require(cursor == 3, "sphere draws three random numbers");
	}

	// 円錐: 速さを保ったまま上向きの円錐の中へ。位置は底面 (y は変えない)
	for (int trial = 0; trial < 50; ++trial) {
		sequence = {std::fmod(trial * 0.173f, 1.0f), std::fmod(trial * 0.311f, 1.0f), std::fmod(trial * 0.577f, 1.0f)};
		position = {0, 5, 0};
		velocity = {3, 4, 0};
		cursor = 0;
		Engine::EmissionShapeFor(EmissionShape::Cone).Shape(params, &SequenceRandom, position, velocity);
		Require(std::abs(Length(velocity) - 5.0f) < 1e-4f, "cone keeps speed");
		Require(velocity.y > 0, "cone points upward");
		Require(position.y == 5 && std::hypot(position.x, position.z) <= params.radius + 1e-4f, "cone emits from its base disc");
		Require(cursor == 3, "cone draws three random numbers");
	}

	// 未知の値 (壊れた保存データ) は点として扱う
	position = {7, 7, 7};
	velocity = {1, 1, 1};
	Engine::EmissionShapeFor(static_cast<EmissionShape>(42)).Shape(params, &SequenceRandom, position, velocity);
	Require(position.x == 7 && velocity.x == 1, "unknown shape falls back to point");
	Require(&Engine::EmissionShapeFor(EmissionShape::Cone) == &Engine::EmissionShapeFor(EmissionShape::Cone), "shape strategies are shared instances");
}
} // namespace

int main() {
	TestObjectPool();
	TestCommandHistory();
	TestEmissionShapes();
	if (failures) {
		std::printf("%d design pattern check(s) failed\n", failures);
		return 1;
	}
	std::printf("Design pattern tests passed\n");
	return 0;
}
