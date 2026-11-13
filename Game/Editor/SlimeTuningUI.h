#pragma once
#include "../Data/SlimeTuning.h"
#include "../../externals/imgui/imgui.h"
#include <cstring>

namespace Game {

inline void DrawSlimeTuning() {
    auto& store = Data::SlimeTuningData();
    store.Poll(); // EDIT中や一時停止中も外部ファイルの変更を拾う。
    if (!ImGui::CollapsingHeader("ゲームの調整値 (JSON)")) return;
    static Data::SlimeTuning draft;
    static uint64_t revision = 0;
    static bool valid = true;
    if (revision != store.Revision()) {
        draft = *store.Snapshot();
        revision = store.Revision();
        valid = true;
    }
    ImGui::TextWrapped("Resources/Data/slime_tuning.json  |  %s", store.Dirty() ? "未保存" : "保存済み");
    ImGui::TextWrapped("編集は即時反映されます。「保存」で次回起動にも適用します。外部JSONの変更は0.5秒ごとに確認します。");
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("JSONへ保存")) store.Save();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("JSONを再読み込み (編集を破棄)")) {
        if (store.Reload()) {
            draft = *store.Snapshot();
            revision = store.Revision();
            valid = true;
        }
    }
    bool edited = false;
    if (ImGui::TreeNode("移動・調合・液体の係数")) {
        ImGui::TextWrapped("結果 = 基本値 + 各材料の割合 × 係数。材料の順序: 水 / ホウ砂水 / 洗濯のり / 泡。");
        for (size_t i = 0; i < draft.recipe.size(); ++i) {
            const auto& spec = Data::RecipeParameters[i];
            ImGui::PushID(spec.key);
            if (ImGui::TreeNode(spec.label)) {
                edited |= ImGui::InputFloat("基本値", &draft.recipe[i].base);
                edited |= ImGui::InputFloat4("材料の係数", draft.recipe[i].materials.data());
                ImGui::TextDisabled("各配合での許容範囲: %.2f ~ %.2f", spec.min, spec.max);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    const char* previousGroup = nullptr;
    bool open = false;
    for (const auto& spec : Data::SlimeScalars) {
        if (!previousGroup || std::strcmp(previousGroup, spec.group) != 0) {
            if (open) ImGui::TreePop();
            previousGroup = spec.group;
            open = ImGui::TreeNode(spec.group);
        }
        if (open) {
            ImGui::PushID(spec.key);
            edited |= ImGui::InputFloat(spec.label, &(draft.*spec.field));
            ImGui::PopID();
        }
    }
    if (open) ImGui::TreePop();
    if (ImGui::TreeNode("形態ごとの攻撃")) {
        for (size_t i = 0; i < draft.weapons.size(); ++i) {
            ImGui::PushID(Data::WeaponKeys[i]);
            if (ImGui::TreeNode(Data::WeaponLabels[i])) {
                auto& weapon = draft.weapons[i];
                edited |= ImGui::InputFloat("攻撃時間 (秒)", &weapon.duration);
                edited |= ImGui::InputFloat("基本ダメージ", &weapon.damage);
                edited |= ImGui::InputFloat("当たり開始 (秒)", &weapon.hitBegin);
                edited |= ImGui::InputFloat("当たり終了 (秒)", &weapon.hitEnd);
                ImGui::TextDisabled("当たり判定の時間はハンマー・剣・飛行機で使います。");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (edited) {
        valid = store.Apply(draft);
        if (valid) revision = store.Revision();
    }
    if (!store.Error().empty()) ImGui::TextWrapped("%s", store.Error().c_str());
    if (!valid) ImGui::TextWrapped("入力が範囲外のため未反映です。値を修正するか再読み込みしてください。");
}

} // namespace Game
