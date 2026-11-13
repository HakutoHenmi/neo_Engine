// EditorCommandTypes.h
// エディタの編集操作のコマンド (Command Pattern の具体クラス)。
// 各操作は「実行」と「取り消し」を対で持ち、EditorUI の履歴 (Engine::CommandHistory) が型を知らずに呼び出す。
// 以前は操作ごとに同じ形のラムダの組を各ウィンドウのコードに書いていたため、
// 無効なエンティティの確認やワールド行列の再計算の漏れが起きやすかった。種類ごとのクラスに集約する。
#pragma once
#include "CommandHistory.h"
#include "../Scenes/GameScene.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>

namespace Game {

// トランスフォームのどの値を対象にするか (Inspector は 1 項目ずつ、ギズモは全部)
enum TransformField : uint8_t {
	TransformTranslate = 1 << 0,
	TransformRotate = 1 << 1,
	TransformScale = 1 << 2,
	TransformAll = TransformTranslate | TransformRotate | TransformScale,
};

// トランスフォームの値の組
struct TransformValues {
	DirectX::XMFLOAT3 translate{0, 0, 0};
	DirectX::XMFLOAT3 rotate{0, 0, 0};
	DirectX::XMFLOAT3 scale{1, 1, 1};
	static TransformValues From(const TransformComponent& t) { return {t.translate, t.rotate, t.scale}; }
};

// 位置・回転・大きさの変更
class TransformCommand final : public Engine::ICommand {
public:
	TransformCommand(GameScene* scene, entt::entity entity, std::string name, uint8_t fields, const TransformValues& before, const TransformValues& after)
	    : scene_(scene), entity_(entity), name_(std::move(name)), fields_(fields), before_(before), after_(after) {}
	void Execute() override { Apply(after_); }
	void Undo() override { Apply(before_); }
	std::string Name() const override { return name_; }

private:
	void Apply(const TransformValues& values) const {
		auto& registry = scene_->GetComponents();
		if (!registry.valid(entity_) || !registry.all_of<TransformComponent>(entity_)) return;
		auto& t = registry.get<TransformComponent>(entity_);
		if (fields_ & TransformTranslate) t.translate = values.translate;
		if (fields_ & TransformRotate) t.rotate = values.rotate;
		if (fields_ & TransformScale) t.scale = values.scale;
	}
	GameScene* scene_;
	entt::entity entity_;
	std::string name_;
	uint8_t fields_;
	TransformValues before_, after_;
};

// モーションのキーフレームの位置の変更 (ギズモでキーフレームを動かしたとき)
class KeyframeMoveCommand final : public Engine::ICommand {
public:
	KeyframeMoveCommand(GameScene* scene, entt::entity entity, std::string clip, int keyframe, const DirectX::XMFLOAT3& before, const DirectX::XMFLOAT3& after)
	    : scene_(scene), entity_(entity), clip_(std::move(clip)), keyframe_(keyframe), before_(before), after_(after) {}
	void Execute() override { Apply(after_); }
	void Undo() override { Apply(before_); }
	std::string Name() const override { return "Move Keyframe"; }

private:
	void Apply(const DirectX::XMFLOAT3& translate) const {
		auto& registry = scene_->GetComponents();
		if (!registry.valid(entity_)) return;
		auto* motion = registry.try_get<MotionComponent>(entity_);
		if (!motion) return;
		auto clip = motion->clips.find(clip_);
		if (clip == motion->clips.end() || keyframe_ < 0 || keyframe_ >= static_cast<int>(clip->second.keyframes.size())) return;
		clip->second.keyframes[static_cast<size_t>(keyframe_)].translate = translate;
	}
	GameScene* scene_;
	entt::entity entity_;
	std::string clip_;
	int keyframe_;
	DirectX::XMFLOAT3 before_, after_;
};

// 名前の変更
class RenameCommand final : public Engine::ICommand {
public:
	RenameCommand(GameScene* scene, entt::entity entity, std::string before, std::string after)
	    : scene_(scene), entity_(entity), before_(std::move(before)), after_(std::move(after)) {}
	void Execute() override { Apply(after_); }
	void Undo() override { Apply(before_); }
	std::string Name() const override { return "Rename"; }

private:
	void Apply(const std::string& name) const {
		auto& registry = scene_->GetComponents();
		if (registry.valid(entity_) && registry.all_of<NameComponent>(entity_)) registry.get<NameComponent>(entity_).name = name;
	}
	GameScene* scene_;
	entt::entity entity_;
	std::string before_, after_;
};

// 親の付け替え (null なら親子解除)。付け替えたらワールド行列を計算し直す
class ReparentCommand final : public Engine::ICommand {
public:
	ReparentCommand(GameScene* scene, entt::entity child, entt::entity before, entt::entity after)
	    : scene_(scene), child_(child), before_(before), after_(after) {}
	void Execute() override { Apply(after_); }
	void Undo() override { Apply(before_); }
	std::string Name() const override { return "Reparent"; }

private:
	void Apply(entt::entity parent) const {
		auto& registry = scene_->GetComponents();
		if (!registry.valid(child_)) return;
		registry.get_or_emplace<HierarchyComponent>(child_).parentId = parent;
		scene_->InvalidateWorldMatrices();
	}
	GameScene* scene_;
	entt::entity child_, before_, after_;
};

// エンティティの生成・削除 (貼り付け・複製・削除)。
// 復元したエンティティは別の ID になるため、今の ID をコマンド自身が追跡する
// (古い ID のままだと Redo → Undo で何も消えなくなる)
class EntityLifetimeCommand final : public Engine::ICommand {
public:
	// existsAfterExecute: 実行後にエンティティが存在する操作 (生成・貼り付け) なら true、削除なら false
	EntityLifetimeCommand(GameScene* scene, std::string name, std::string snapshot, entt::entity current, bool existsAfterExecute)
	    : scene_(scene), name_(std::move(name)), snapshot_(std::move(snapshot)), live_(current), existsAfterExecute_(existsAfterExecute) {}
	void Execute() override { existsAfterExecute_ ? Restore() : Destroy(); }
	void Undo() override { existsAfterExecute_ ? Destroy() : Restore(); }
	std::string Name() const override { return name_; }
	entt::entity Current() const { return live_; }

private:
	void Destroy();
	void Restore();
	GameScene* scene_;
	std::string name_;
	std::string snapshot_;
	entt::entity live_;
	bool existsAfterExecute_;
};

} // namespace Game
