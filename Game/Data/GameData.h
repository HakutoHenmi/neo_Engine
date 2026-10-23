// GameData.h
// ゲームのデータ (調整値・表・文章) を Resources/Data の JSON から読み込むための共通の窓口。
// コードに数値や文章を直接書かず、JSON を書き換えるだけで調整できるようにする。
#pragma once
#include "../../externals/nlohmann/json.hpp"
#include <array>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace Game::Data {

// データファイルの置き場所 (作業ディレクトリ = プロジェクトのルート、またはリリースの実行ファイルの隣)
inline constexpr const char* Folder = "Resources/Data/";

// JSON を読み込んで返す。同じファイルは 1 回だけ読み、以後は使い回す。
// ロード画面のスレッドからも呼ばれるので、読み込みは排他制御する。
// ファイルが無い・壊れているときは、どのファイルかが分かる例外を投げる
inline const nlohmann::json& Load(const std::string& name) {
    static std::map<std::string, nlohmann::json> cache;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    const std::string path = std::string(Folder) + name;
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Game data file not found: " + path);
    nlohmann::json data;
    try {
        file >> data;
    } catch (const std::exception& e) {
        throw std::runtime_error("Game data file is broken: " + path + " (" + e.what() + ")");
    }
    return cache.emplace(name, std::move(data)).first->second;
}

// JSON の中の文字列を "区切り.項目" の形の名前で取り出す (例: Text("ui_text.json", "title.start"))。
// 見つけた文字列は覚えておくので、毎フレーム呼んでもファイルは読み直さない
inline const std::string& Text(const std::string& name, const std::string& key) {
    static std::map<std::string, std::string> cache;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const std::string id = name + ":" + key;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    const nlohmann::json* node = &Load(name);
    size_t start = 0;
    while (true) {
        const size_t dot = key.find('.', start);
        const std::string part = key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        auto child = node->find(part);
        if (child == node->end()) throw std::runtime_error("Game data is missing text '" + key + "' in " + name);
        node = &*child;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (!node->is_string()) throw std::runtime_error("Game data text '" + key + "' in " + name + " is not a string");
    return cache.emplace(id, node->get<std::string>()).first->second;
}

// 必須の項目を取り出す (無ければどのファイルのどの項目かが分かる例外を投げる)
inline const nlohmann::json& Require(const nlohmann::json& object, const char* key, const std::string& where) {
    auto it = object.find(key);
    if (it == object.end()) throw std::runtime_error("Game data is missing '" + std::string(key) + "' in " + where);
    return *it;
}

// 数値の配列を std::array に読み込む (要素数が合わなければ例外)
template <class T, size_t N>
inline std::array<T, N> ReadArray(const nlohmann::json& values, const std::string& where) {
    if (!values.is_array() || values.size() != N)
        throw std::runtime_error("Game data expects " + std::to_string(N) + " values in " + where);
    std::array<T, N> out{};
    for (size_t i = 0; i < N; ++i) out[i] = values[i].get<T>();
    return out;
}

// 文字列の一覧を読み込む
inline std::vector<std::string> ReadStrings(const nlohmann::json& values) {
    std::vector<std::string> out;
    for (const auto& value : values) out.push_back(value.get<std::string>());
    return out;
}

} // namespace Game::Data
