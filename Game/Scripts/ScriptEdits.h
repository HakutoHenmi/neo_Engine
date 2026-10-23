// ScriptEdits.h
// Inspector でのスクリプトの追加・差し替え・削除を、描画の後でまとめて安全に反映する。
#pragma once
// スクリプトの開始・停止の処理
#include "ScriptLifecycle.h"
#include <cstddef>

// ゲーム側のコードはすべて Game 名前空間に置く
namespace Game {

// 変更の種類 (差し替え・削除・追加・コンポーネントごと削除)
enum class ScriptEditKind { Replace, Remove, Add, RemoveComponent };

// 変更内容 (対象・番号・変更前のクラス名と実体・差し替え後のクラス名)
struct ScriptEdit {
    // 変更の種類
    ScriptEditKind kind;
    // 対象のエンティティ
    entt::entity entity;
    // 対象のスクリプトの番号
    size_t index = 0;
    // 変更を予約したときのクラス名 (変わっていたら反映しない)
    std::string expectedPath;
    // 変更を予約したときの実体 (変わっていたら反映しない)
    std::shared_ptr<IScript> expectedInstance;
    // 差し替え後のクラス名
    std::string replacementPath;
};

// Inspector の描画が終わった後に反映する。構造の変更をすべて済ませてからユーザーの後片付けを呼び、
// コールバックの後はコンポーネントやエントリへの参照を使わない
template<class Components>
bool ApplyScriptEdit(Components& components, const ScriptEdit& edit, GameScene* scene) {
    // エンティティが消えていれば何もしない
    if (!components.valid(edit.entity)) return false;
    // スクリプトのコンポーネントが無ければ何もしない
    auto* component = components.template try_get<ScriptComponent>(edit.entity);
    if (!component) return false;
    // 追加: 空のスクリプト欄を末尾に増やす
    if (edit.kind == ScriptEditKind::Add) {
        component->scripts.emplace_back();
        return true;
    }
    // コンポーネントごと削除: 先に一覧を取り出してから外し、最後に後片付けを呼ぶ
    if (edit.kind == ScriptEditKind::RemoveComponent) {
        ScriptComponent detached;
        detached.scripts.swap(component->scripts);
        components.template remove<ScriptComponent>(edit.entity);
        StopScripts(detached, edit.entity, scene);
        return true;
    }
    // 番号が範囲外なら何もしない
    if (edit.index >= component->scripts.size()) return false;
    auto& entry = component->scripts[edit.index];
    // 予約した後で中身が変わっていたら反映しない
    if (entry.scriptPath != edit.expectedPath || entry.instance != edit.expectedInstance) return false;
    // 古いスクリプトを取り出しておく
    ScriptEntry detached = std::move(entry);
    // 差し替え: 同じ番号に新しいクラス名の空の欄を入れる
    if (edit.kind == ScriptEditKind::Replace) {
        entry = ScriptEntry{edit.replacementPath, "{}", nullptr, false};
    // 削除: 一覧から取り除く
    } else {
        component->scripts.erase(component->scripts.begin() + static_cast<std::ptrdiff_t>(edit.index));
    }
    // 取り出した古いスクリプトの後片付けを呼ぶ
    StopScript(detached, edit.entity, scene);
    return true;
}

} // namespace Game
