#pragma once
#include "ISystem.h"
#include "../Scenes/GameScene.h"
#include "../ObjectTypes.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "../Scripts/GameManagerScript.h"

namespace Game {

class WaveSystem : public ISystem {
public:
	enum class State {
		Playing,
		Clear
	};

	State state = State::Playing;
	float clearAlpha = 0.0f;

	void Update(entt::registry& registry, GameContext& ctx) override {
		if (!ctx.isPlaying) return;

		// スクリプトインスタンスがなければ動的に生成してアタッチする（フォールバック）
		if (!GameManagerScript::GetInstance()) {
			entt::entity gmEntity = ctx.scene->CreateEntity("GameManager");
			auto& sc = registry.emplace<ScriptComponent>(gmEntity);
			sc.scripts.push_back({"GameManagerScript", "", nullptr, false});
			ScriptEngine::GetInstance()->Execute(gmEntity, ctx.scene, 0.0f); // Startを呼ぶため
		}

		// プレイヤーの生存確認
		bool playerAlive = false;
		auto pView = registry.view<PlayerInputComponent, HealthComponent>();
		if (pView.begin() != pView.end()) {
			auto& hc = pView.get<HealthComponent>(*pView.begin());
			if (!hc.isDead) playerAlive = true;
		}

		if (!playerAlive) return;

		// 敵の数を数える
		int enemyCount = 0;
		auto eView = registry.view<EnemyAIComponent, HealthComponent>();
		for (auto entity : eView) {
			auto& hc = eView.get<HealthComponent>(entity);
			if (!hc.isDead) enemyCount++;
		}

		if (state == State::Playing) {
			if (enemyCount == 0) {
				state = State::Clear;
			}
		} else if (state == State::Clear) {
			// 徐々にフェードイン
			clearAlpha += ctx.dt;
			if (clearAlpha > 1.0f) clearAlpha = 1.0f;

			// STAGE CLEAR テキストの描画
			if (ctx.renderer) {
				auto* gm = GameManagerScript::GetInstance();
				std::string msg = gm ? gm->clearText : "STAGE CLEAR!";
				float scale = gm ? gm->clearTextScale : 3.0f;
				float cColor[4] = {1.0f, 0.8f, 0.2f, 1.0f};
				if (gm) { cColor[0]=gm->clearColor[0]; cColor[1]=gm->clearColor[1]; cColor[2]=gm->clearColor[2]; cColor[3]=gm->clearColor[3]; }

				float centerX = ctx.viewportOffset.x + ctx.viewportSize.x * 0.5f;
				float centerY = ctx.viewportOffset.y + ctx.viewportSize.y * 0.4f;

				float width = ctx.renderer->MeasureTextWidth(msg, scale);
				float sx = centerX - width * 0.5f;

				// シャドウとメイン
				ctx.renderer->DrawString(msg, sx + 5.0f, centerY + 5.0f, scale, {0.0f, 0.0f, 0.0f, clearAlpha});
				ctx.renderer->DrawString(msg, sx, centerY, scale, {cColor[0], cColor[1], cColor[2], clearAlpha});
			}
		}
	}

	void Reset(entt::registry& /*registry*/) override {
		state = State::Playing;
		clearAlpha = 0.0f;
	}
};

} // namespace Game
