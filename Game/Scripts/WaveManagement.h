#pragma once
#include "IScript.h"
#include <vector>

namespace Game {

class WaveManagement : public IScript {
public:
	void Start(entt::entity entity, GameScene* scene) override;
	void Update(entt::entity entity, GameScene* scene, float dt) override;
	void OnDestroy(entt::entity entity, GameScene* scene) override;

	void OnEditorUI() override;
	std::string SerializeParameters() override;
	void DeserializeParameters(const std::string& data) override;

	void SpawnSpanner(int currentWave, GameScene* scene);

	static void SetWave(int waveNumber) { currentWave_ = waveNumber; }
	static entt::entity GetManagerEntity() { return managerEntity_; }

private:
	static int currentWave_;
	static inline entt::entity managerEntity_ = static_cast<entt::entity>(entt::null);
	int previousWave_ = -1;

	// 各ウェーブごとのスポナー（エンティティ名）のリスト (シリアライズ用)
	std::vector<std::vector<std::string>> enemySpawnerNames_;
	// 実行時・Editor時の実体
	std::vector<std::vector<entt::entity>> enemySpawners_;

	// 最後に Update で参照したシーン (UI生成用)
	GameScene* cachedScene_ = nullptr;
};

} // namespace Game