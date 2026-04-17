#include "NavigationManager.h"
#include "../Scenes/GameScene.h"
#include "../ObjectTypes.h"
#include <queue>

void NavigationManager::Initialize(int width, int height, float cellSize, float originX, float originZ) {
	width_ = width;
	height_ = height;
	cellSize_ = cellSize;
	originX_ = originX;
	originZ_ = originZ;
	grid_.assign(width * height, FlowCell{1, FLT_MAX, 0.0f, 0.0f});
}

void NavigationManager::UpdateCostMap(Game::GameScene* scene) {
	auto& registry = scene->GetRegistry();

	// 全セルをリセット(平地[1]にリセット)
	for (auto& cell : grid_) {
		cell.cost = 1;
	}

	// シーン内の壁オブジェクト(Wall, Cannon, Pipe)を検索
	// コンポーネントをひとつづつに分ける
	auto tagView = registry.view<Game::TagComponent>();
	auto tcView = registry.view<Game::TransformComponent>();
	for (entt::entity entity : tagView) {
		// TagComponentを持っているかチェック
		if (registry.all_of<Game::TagComponent>(entity)) {
			const auto& tag = tagView.get<Game::TagComponent>(entity).tag;
			// チェックしたタグがWall, Canon, Pipeなら
			if (tag == Game::TagType::Wall || tag == Game::TagType::Cannon || tag == Game::TagType::Pipe) {
				if (registry.all_of<Game::TransformComponent>(entity)) {
					auto& tc = tcView.get<Game::TransformComponent>(entity);

					// オブジェクトの範囲をグリッド座標に変換して、その範囲を壁[255]にする
					int minX = static_cast<int>(std::floor((tc.translate.x - tc.scale.x - originX_) / cellSize_));
					int maxX = static_cast<int>(std::floor((tc.translate.x + tc.scale.x - originX_) / cellSize_));
					int minZ = static_cast<int>(std::floor((tc.translate.z - tc.scale.z - originZ_) / cellSize_));
					int maxZ = static_cast<int>(std::floor((tc.translate.z + tc.scale.z - originZ_) / cellSize_));

					for (int z = minZ; z <= maxZ; ++z) {
						for (int x = minX; x <= maxX; ++x) {
							// グリッドの範囲内かチェック
							if (x >= 0 && x < width_ && z >= 0 && z < height_) {
								grid_[GetIndex(x, z)].cost = 255; // 壁
							}
						}
					}
				}
			}
		}
	}
}

void NavigationManager::GenerateFlowField(float targetWorldX, float targetWorldZ) {
	// 初期化する(全マスのコストを最大に)
	for (auto& cell : grid_) {
		cell.bestCost = FLT_MAX;
	}

	// 目的地をグリッド座標に変換
	int targetX = static_cast<int>((targetWorldX - originX_) / cellSize_);
	int targetZ = static_cast<int>((targetWorldZ - originZ_) / cellSize_);

	// 目的地がグリッドの範囲外なら何もしない
	if (targetX < 0 || targetX >= width_ || targetZ < 0 || targetZ >= height_)
		return;

	// ゴールの設定
	std::queue<int> openIndices;
	int targetIndex = GetIndex(targetX, targetZ);
	grid_[targetIndex].bestCost = 0;
	openIndices.push(targetIndex);

	// ダイクストラ法による伝播(上下左右斜め)
	int dx[] = {0, 0, 1, -1, 1, 1, -1, -1};
	int dz[] = {1, -1, 0, 0, 1, -1, 1, -1};

	while (!openIndices.empty()) {
		int currIndex = openIndices.front();
		openIndices.pop();

		int currX = currIndex % width_;
		int currZ = currIndex / width_;

		for (int i = 0; i < 8; ++i) {
			int nextX = currX + dx[i];
			int nextZ = currZ + dz[i];
			
			// グリッドの範囲内かチェック（配列アクセス前に行う必要がある）
			if (nextX < 0 || nextX >= width_ || nextZ < 0 || nextZ >= height_) {
				continue;
			}

			// 斜め移動の場合、その横にある2マスが壁なら通れないようにする
			if (grid_[GetIndex(nextX, currZ)].cost == 255 || grid_[GetIndex(currX, nextZ)].cost == 255) {
				continue;
			}

			int nextIndex = GetIndex(nextX, nextZ);
			FlowCell& nextCell = grid_[nextIndex];

			// 壁[255]ではないより短い経路が見つかった場合
			if (nextCell.cost < 255) {
				float moveCost = (i < 4) ? (float)nextCell.cost : (float)nextCell.cost * 1.414f;
				float newCost = grid_[currIndex].bestCost + moveCost;

				if (newCost < nextCell.bestCost) {
					nextCell.bestCost = newCost;
					openIndices.push(nextIndex);
				}
			}
		}
	}

	// ベクトル場の生成
	CalculateDirections();
}

void NavigationManager::CalculateDirections() {
	for (int z = 0; z < height_; ++z) {
		for (int x = 0; x < width_; ++x) {
			int currIndex = GetIndex(x, z);
			if (grid_[currIndex].cost == 255) {
				grid_[currIndex].dirX = 0;
				grid_[currIndex].dirZ = 0;
				continue;
			}

			float dirX = 0.0f;
			float dirZ = 0.0f;

			// 周囲8マスを見て、最も「下り坂」な方向を探す
			float currentBestCost = grid_[currIndex].bestCost;

			for (int dz = -1; dz <= 1; ++dz) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dz == 0) continue;

					int nx = x + dx;
					int nz = z + dz;

					if (nx >= 0 && nx < width_ && nz >= 0 && nz < height_) {
						int targetIdx = GetIndex(nx, nz);
						float neighborCost;

						if (grid_[targetIdx].cost == 255) {
							// 壁のコストを「自分のコスト + セルサイズ分」程度に抑える
							// これにより、壁を避ける力が「適度」になり、ゴールへの力と混ざるようになる
							neighborCost = grid_[currIndex].bestCost + cellSize_; 
						} else {
							neighborCost = grid_[targetIdx].bestCost;
						}

						// 今のマスよりコストが低い方向があれば、そちらへの向きを加算
						if (neighborCost < currentBestCost) {
							// 斜め移動の寄与率を調整（1.0 / 距離）
							float weight = (dx != 0 && dz != 0) ? 0.707f : 1.0f;
							dirX += (float)dx * (currentBestCost - neighborCost) * weight;
							dirZ += (float)dz * (currentBestCost - neighborCost) * weight;
						}
					}
				}
			}

			// 正規化して保存
			float length = std::sqrt(dirX * dirX + dirZ * dirZ);
			if (length > 0.001f) {
				grid_[currIndex].dirX = dirX / length;
				grid_[currIndex].dirZ = dirZ / length;
			} else {
				// どこにも行けない（またはゴール地点）
				grid_[currIndex].dirX = 0;
				grid_[currIndex].dirZ = 0;
			}
		}
	}
}

void NavigationManager::GetDirection(float worldX, float worldZ, float& outX, float& outZ) {
	int x = static_cast<int>((worldX - originX_) / cellSize_);
	int z = static_cast<int>((worldZ - originZ_) / cellSize_);

	if (x >= 0 && x < width_ && z >= 0 && z < height_) {
		const auto& cell = grid_[GetIndex(x, z)];
		outX = cell.dirX;
		outZ = cell.dirZ;
	} else {
		outX = 0;
		outZ = 0;
	}
}

void NavigationManager::DrawDebug(Game::GameScene* scene) {
	auto* renderer = scene->GetRenderer();
	if (!renderer) return;

	for (int z = 0; z < height_; ++z) {
		for (int x = 0; x < width_; ++x) {
			int idx = GetIndex(x, z);

			// セルのワールド座標を計算
			float wx = x * cellSize_ + (cellSize_ * 0.5f) + originX_;
			float wz = z * cellSize_ + (cellSize_ * 0.5f) + originZ_;
			float wy = 0.5f; // 地面より少し上に表示

			// --- 1. 壁の表示 (コストが255なら赤い点) ---
			if (grid_[idx].cost == 255) {
				Engine::Vector3 p1 = { wx - 0.2f, wy, wz };
				Engine::Vector3 p2 = { wx + 0.2f, wy, wz };
				renderer->DrawLine3D(p1, p2, { 1.0f, 0.0f, 0.0f, 1.0f }, true);
			}

			// --- 2. ベクトルの表示 (方向があれば青い線) ---
			if (grid_[idx].dirX != 0.0f || grid_[idx].dirZ != 0.0f) {
				Engine::Vector3 start = { wx, wy, wz };
				Engine::Vector3 end = { 
					wx + grid_[idx].dirX * (cellSize_ * 0.4f), 
					wy, 
					wz + grid_[idx].dirZ * (cellSize_ * 0.4f) 
				};
				renderer->DrawLine3D(start, end, { 0.0f, 0.5f, 1.0f, 1.0f }, true);
			}
		}
	}
}