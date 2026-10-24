// EditorCommands.cpp
// エディタの編集操作: Undo / Redo の履歴、Console へのログ、エンティティのコピー・貼り付け・複製。
#include "EditorInternal.h"

namespace Game {
// エディタの内部で共有する状態と関数 (EditorInternal.h)
using namespace EditorInternal;

// 変形ツールの状態と、Project ウィンドウのアイコン (EditorUI の静的メンバー)
EditorUI::GizmoState EditorUI::gizmo_;
EditorUI::Icons EditorUI::s_icons;

// 複製したエンティティの名前を作る: 末尾の "_番号" や " (Copy)" を外し、同じ名前の最大番号 + 1 を付ける
static std::string GenerateCopyName(const std::string& baseName, SceneComponents& registry) {
	// 末尾の " (Copy)" をすべて外す
	std::string base = baseName;
	while (base.size() > 7 && base.substr(base.size() - 7) == " (Copy)")
		base = base.substr(0, base.size() - 7);
	{
		// 末尾が "_数字" なら外す
		auto pos = base.rfind('_');
		if (pos != std::string::npos && pos + 1 < base.size()) {
			bool allDigit = true;
			for (size_t i = pos + 1; i < base.size(); ++i)
				if (!isdigit((unsigned char)base[i])) {
					allDigit = false;
					break;
				}
			if (allDigit)
				base = base.substr(0, pos);
		}
	}
	// 空になったら Object にする
	if (base.empty()) base = "Object";
	// 同じ名前で "_数字" が付いたものの最大の番号を探す
	int maxNum = 0;
	auto view = registry.view<NameComponent>();
	for (auto entity : view) {
		const auto& name = view.get<NameComponent>(entity).name;
		if (name.size() > base.size() + 1 && name.substr(0, base.size()) == base && name[base.size()] == '_') {
			// "_" より後ろが数字だけか
			std::string numPart = name.substr(base.size() + 1);
			bool allDigit = true;
			for (char c : numPart) if (!isdigit((unsigned char)c)) { allDigit = false; break; }
			// 10桁以上は int に収まらず std::stoi が例外を投げるので連番として扱わない
			if (allDigit && !numPart.empty() && numPart.size() <= 9) {
				int n = std::stoi(numPart);
				if (n > maxNum) maxNum = n;
			}
		}
	}
	// 最大の番号 + 1 を付ける
	return base + "_" + std::to_string(maxNum + 1);
}

// 取り消しとやり直し
// Undo できる操作を記録する (新しい操作をしたら Redo の履歴は消す)
void EditorUI::PushUndo(const UndoCommand& cmd) {
	// 古いものから消して最大件数を保つ
	undoStack.push_back(cmd);
	if (undoStack.size() > kMaxUndoDepth) undoStack.pop_front();
	redoStack.clear();
}
// 最後の操作を取り消す
void EditorUI::Undo() {
	// 履歴が無ければ何もしない
	if (undoStack.empty()) return;
	// 最後の操作を取り出し、取り消して Redo の履歴へ移す
	auto c = undoStack.back();
	undoStack.pop_back();
	c.undo();
	redoStack.push_back(c);
}
// 取り消した操作をやり直す
void EditorUI::Redo() {
	// 履歴が無ければ何もしない
	if (redoStack.empty()) return;
	// 最後に取り消した操作を取り出し、やり直して Undo の履歴へ戻す
	auto c = redoStack.back();
	redoStack.pop_back();
	c.redo();
	undoStack.push_back(c);
}

// Console (ログの表示)
// Console にログを追加する (情報・警告・エラー)
void EditorUI::Log(const std::string& msg) {
	// 時刻付きで追加し、古いものから消して最大行数を保つ
	consoleLog.push_back({LogLevel::Info, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogWarning(const std::string& msg) {
	consoleLog.push_back({LogLevel::Warning, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}
void EditorUI::LogError(const std::string& msg) {
	consoleLog.push_back({LogLevel::Error, msg, globalTime});
	if (consoleLog.size() > kMaxConsoleLines) consoleLog.pop_front();
}

// エンティティの JSON から1つ復元する
static entt::entity RestoreEntitySnapshot(GameScene* scene, const std::string& snapshot) {
	try {
		// 1 つだけの objects 配列として読み込む (追加モード)
		auto created = RestoreSceneFromJson(scene, json::parse("{\"objects\": [" + snapshot + "]}"), true);
		if (!created.empty()) return created[0];
	} catch (const std::exception& e) {
		// 失敗したらエラーを出す
		EditorUI::LogError(std::string("Failed to restore entity: ") + e.what());
	}
	return entt::null;
}

// 生成/削除の Undo。復元したエンティティは別IDになるため、最新IDを共有して追跡する
// (古いIDを保持したままだと Redo→Undo で何も消えなくなる)
UndoCommand EditorInternal::MakeSpawnUndo(GameScene* scene, const std::string& description, const std::string& snapshot, entt::entity current, bool existsAfterRedo) {
	// 今のエンティティを共有して覚える
	auto live = std::make_shared<entt::entity>(current);
	// 消す処理: 今のエンティティを破棄予約する
	std::function<void()> destroy = [scene, live]() {
		if (*live != entt::null) scene->DestroyObject(static_cast<uint32_t>(*live));
		// 選択中なら選択を外す
		if (scene->GetSelectedEntity() == *live) scene->SelectEntity(entt::null);
		*live = entt::null;
	};
	// 戻す処理: 保存した JSON から作り直し、新しいエンティティを覚える
	std::function<void()> restore = [scene, live, snapshot]() { *live = RestoreEntitySnapshot(scene, snapshot); };
	// Redo 後に存在する操作 (生成・貼り付け) か、消える操作 (削除) かで、取り消しとやり直しを入れ替える
	return existsAfterRedo ? UndoCommand{description, destroy, restore} : UndoCommand{description, restore, destroy};
}

// 選択中のエンティティをコピーする
void EditorInternal::ExecuteCopy(GameScene* scene) {
	// シーンが無ければ何もしない
	if (!scene) return;
	// 選択中のエンティティ (無いか消えていれば何もしない)
	auto ent = scene->GetSelectedEntity();
	if (ent == entt::null || !scene->GetComponents().valid(ent)) return;
	// JSON にして覚える
	s_clipboardJson = SerializeEntity(scene->GetComponents(), ent);
	EditorUI::Log("Copied Entity to Clipboard.");
}

// コピーしたエンティティを貼り付ける (名前に番号を付け、少しずらして置く)
void EditorInternal::ExecutePaste(GameScene* scene) {
	// シーンかコピーした内容が無ければ何もしない
	if (!scene || s_clipboardJson.empty()) return;
	try {
		// 1 つだけの objects 配列として読み込む (追加モード)
		json j = json::parse("{\"objects\": [" + s_clipboardJson + "]}");
		auto createdEnts = RestoreSceneFromJson(scene, j, true);
		if (!createdEnts.empty() && createdEnts[0] != entt::null) {
			// 作ったエンティティ
			auto e = createdEnts[0];
			auto& reg = scene->GetComponents();
			// 名前に番号を付ける
			if (reg.all_of<NameComponent>(e)) {
				auto& nc = reg.get<NameComponent>(e);
				nc.name = GenerateCopyName(nc.name, reg);
			}
			// 元の位置から少しずらす
			if (reg.all_of<TransformComponent>(e)) {
				auto& tc = reg.get<TransformComponent>(e);
				tc.translate.x += 0.5f;
				tc.translate.y += 0.5f;
				tc.translate.z -= 0.5f;
			}
			// 選択し、Undo を登録してログに残す
			scene->SelectEntity(e);
			
			EditorUI::PushUndo(MakeSpawnUndo(scene, "Paste Entity", SerializeEntity(reg, e), e, true));
			EditorUI::Log("Pasted Entity: " + (reg.all_of<NameComponent>(e) ? reg.get<NameComponent>(e).name : "Unknown"));
		}
	// 失敗したらエラーを出す
	} catch (...) { EditorUI::LogError("Failed to paste entity"); }
}

// 複製 (コピー → 貼り付け)
void EditorInternal::ExecuteDuplicate(GameScene* scene) {
	if (!scene) return;
	ExecuteCopy(scene);
	ExecutePaste(scene);
}

} // namespace Game
