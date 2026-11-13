#pragma once
// ===============================
//  CommandHistory : 取り消し・やり直しの履歴 (Command Pattern)
//  - 操作を ICommand のオブジェクトとして表し、実行 (Execute) と取り消し (Undo) を対にして持つ。
//  - 履歴は操作の中身を知らず、ICommand 経由で呼ぶだけ。新しい操作は ICommand の派生を足せばよい。
//  - 新しい操作を記録するとやり直しの履歴は消える。最大件数を超えたら古い操作から捨てる。
// ===============================
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Engine {

class ICommand {
public:
	virtual ~ICommand() = default;
	// 操作を行う (やり直しでも呼ばれる)
	virtual void Execute() = 0;
	// 操作を取り消す
	virtual void Undo() = 0;
	// 表示用の名前 (メニューやログに出す)
	virtual std::string Name() const = 0;
};

// 関数の組で表す操作。専用のクラスを作るほどでもない一度きりの編集 (外部データの差し替えなど) に使う
class FunctionCommand final : public ICommand {
public:
	FunctionCommand(std::string name, std::function<void()> undo, std::function<void()> redo)
	    : name_(std::move(name)), undo_(std::move(undo)), redo_(std::move(redo)) {}
	void Execute() override { if (redo_) redo_(); }
	void Undo() override { if (undo_) undo_(); }
	std::string Name() const override { return name_; }

private:
	std::string name_;
	std::function<void()> undo_, redo_;
};

class CommandHistory {
public:
	explicit CommandHistory(size_t maxDepth = 100) : maxDepth_(maxDepth) {}

	// 実行してから記録する
	void Execute(std::unique_ptr<ICommand> command) {
		if (!command) return;
		command->Execute();
		Record(std::move(command));
	}
	// 実行済みの操作を記録する (ドラッグ編集のように、操作中に値がすでに変わっているとき)
	void Record(std::unique_ptr<ICommand> command) {
		if (!command) return;
		undo_.push_back(std::move(command));
		while (undo_.size() > maxDepth_) undo_.pop_front();
		redo_.clear();
	}
	// 最後の操作を取り消す。履歴が無ければ false
	bool Undo() {
		if (undo_.empty()) return false;
		auto command = std::move(undo_.back());
		undo_.pop_back();
		command->Undo();
		redo_.push_back(std::move(command));
		return true;
	}
	// 取り消した操作をやり直す。履歴が無ければ false
	bool Redo() {
		if (redo_.empty()) return false;
		auto command = std::move(redo_.back());
		redo_.pop_back();
		command->Execute();
		undo_.push_back(std::move(command));
		return true;
	}
	void Clear() {
		undo_.clear();
		redo_.clear();
	}

	bool CanUndo() const { return !undo_.empty(); }
	bool CanRedo() const { return !redo_.empty(); }
	size_t UndoCount() const { return undo_.size(); }
	size_t RedoCount() const { return redo_.size(); }
	// 次に取り消す / やり直す操作の名前 (無ければ空)
	std::string NextUndoName() const { return undo_.empty() ? std::string() : undo_.back()->Name(); }
	std::string NextRedoName() const { return redo_.empty() ? std::string() : redo_.back()->Name(); }

private:
	size_t maxDepth_;
	std::deque<std::unique_ptr<ICommand>> undo_;
	std::deque<std::unique_ptr<ICommand>> redo_;
};

} // namespace Engine
