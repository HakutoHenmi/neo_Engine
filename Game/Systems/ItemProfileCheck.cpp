// ItemProfileCheck.cpp
// 物のプロフィールの確かめ (H1。ChronoSystem の一部。作り方は Chrono/ItemProfileRules.h)。
//  --item-profile-sheet: 作る画面が開いたら、図鑑の物 (建物を除く 123 個) を全部プロフィールにして、
//  tests/out/item-profiles/tiles.ppm (物ごとに正面・横・上から見た図。薄い灰色が元の物の形、色がパーツ) と
//  report.tsv (物ごとの形の種類・特徴・パーツ・重なりの割合・意味のタグ・図鑑の絵) を書いて終わる。
//  item_profiles.json のタグに、使ってよい言葉 (ItemTagWords) 以外があれば report.tsv の最後に書く
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/ItemProfileRules.h"
#include <filesystem>
#include <fstream>

namespace Game {
namespace {
struct Tiles {
	int w = 0, h = 0;
	std::vector<uint8_t> rgb;
	void Init(int W, int H) {
		w = W;
		h = H;
		rgb.assign(size_t(w * h * 3), 245);
	}
	void Put(int x, int y, std::array<uint8_t, 3> c) {
		if (x < 0 || y < 0 || x >= w || y >= h)
			return;
		uint8_t* p = &rgb[size_t((y * w + x) * 3)];
		p[0] = c[0];
		p[1] = c[1];
		p[2] = c[2];
	}
	void Rect(int x, int y, int W, int H, std::array<uint8_t, 3> c) {
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i)
				Put(x + i, y + j, c);
	}
};
} // namespace

namespace {
// 意味のタグと手直し (item_profiles.json。一度だけ読む)
struct ItemProfileData {
	std::map<std::string, std::vector<std::string>> tags;
	std::map<std::string, nlohmann::json> overrides;
};
const ItemProfileData& ProfileData() {
	static const ItemProfileData data = [] {
		ItemProfileData out;
		auto& tags = out.tags;
		auto& overrides = out.overrides;
		std::ifstream file("Resources/Data/item_profiles.json");
		if (file) {
			nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
			if (json.is_object() && json.contains("items"))
				for (auto it = json["items"].begin(); it != json["items"].end(); ++it)
					for (const auto& t : it.value())
						tags[it.key()].push_back(t.get<std::string>());
			if (json.is_object() && json.contains("overrides"))
				for (auto it = json["overrides"].begin(); it != json["overrides"].end(); ++it)
					overrides[it.key()] = it.value();
		}
		return out;
	}();
	return data;
}
// 図鑑の物 1 つをプロフィールにする
ItemProfile BuildProfileOf(const Collection::Entry& e, ItemProfileDetail::Grid* gridOut) {
	const auto& tags = ProfileData().tags;
	const auto& overrides = ProfileData().overrides;
	{
		const std::string path = "Resources/Models/Lowpoly/" + e.id + ".obj";
		std::vector<Vec> pts;
		std::vector<uint32_t> idx;
		if (std::filesystem::exists(path)) {
			auto* render = Engine::Renderer::GetInstance();
			if (auto* model = render->GetModel(render->LoadObjMesh(path))) {
				const auto& data = model->GetData();
				for (const auto& v : data.vertices)
					pts.push_back(Vec{v.position.x, v.position.y, v.position.z});
				idx.assign(data.indices.begin(), data.indices.end());
				if (idx.empty())
					for (uint32_t i = 0; i < uint32_t(pts.size()); ++i)
						idx.push_back(i);
			}
		}
		// (モデルの無い物は平たいカードの形にする: トランプ)
		if (pts.empty())
			ItemBoxMesh(pts, idx, Vec{0, .02f, 0}, Vec{.32f, .02f, .45f});
		ItemProfile prof = BuildItemProfile(e.id, pts, idx, gridOut);
		if (auto found = tags.find(e.id); found != tags.end())
			prof.tags = found->second;
		// 手直し (目で確かめて直した形の種類と特徴)
		if (auto found = overrides.find(e.id); found != overrides.end()) {
			std::vector<std::string> features;
			const bool hasFeatures = found->second.contains("features");
			if (hasFeatures)
				for (const auto& f : found->second["features"])
					features.push_back(f.get<std::string>());
			ApplyItemOverride(prof, found->second.value("shape", std::string()), features, hasFeatures);
		}
		return prof;
	}
}
} // namespace

// 図鑑の物 (建物を除く) を全部プロフィールにする (並びは図鑑の順)。意味のタグと手直しは item_profiles.json から。
// grids に、物ごとのそろえた大きさの箱の並びを返す (確かめの絵に使う)
std::vector<ItemProfile> ChronoSystem::BuildItemProfiles(std::vector<ItemProfileDetail::Grid>* grids) {
	std::vector<ItemProfile> profiles;
	for (const auto& e : Collection::Entries()) {
		if (e.battle == "building")
			continue;
		ItemProfileDetail::Grid g;
		profiles.push_back(BuildProfileOf(e, &g));
		if (grids)
			grids->push_back(std::move(g));
	}
	return profiles;
}

const ItemProfile* ChronoSystem::ItemProfileOf(const std::string& id) {
	static std::map<std::string, ItemProfile> cache;
	if (auto found = cache.find(id); found != cache.end())
		return &found->second;
	const int index = Collection::IndexOf(id);
	if (index < 0)
		return nullptr;
	return &cache.emplace(id, BuildProfileOf(Collection::Entries()[size_t(index)], nullptr)).first->second;
}

void ChronoSystem::WriteItemProfileSheet() {
	std::vector<ItemProfileDetail::Grid> grids;
	const std::vector<ItemProfile> profiles = BuildItemProfiles(&grids);
	const std::string dir = "tests/out/item-profiles";
	std::filesystem::create_directories(dir);
	std::vector<const Collection::Entry*> items;
	for (const auto& e : Collection::Entries())
		if (e.battle != "building")
			items.push_back(&e);
	constexpr int view = 64, pad = 4, tileW = view * 3 + pad * 4, tileH = view + pad * 2, cols = 4;
	const int rows = (int(items.size()) + cols - 1) / cols;
	Tiles sheet;
	sheet.Init(cols * tileW, rows * tileH);
	std::ofstream report(dir + "/report.tsv");
	report << "#\tid\tname\tshape\tfeatures\tparts\tkinds\tfit\ttags\timage\tresemble_old\tresemble_new\n";
	[[maybe_unused]] static constexpr std::array<std::array<uint8_t, 3>, 5> colors{
	    {{60, 60, 60}, {50, 110, 210}, {235, 130, 40}, {60, 160, 80}, {170, 80, 190}}};
	std::vector<std::string> unknown;
	for (size_t n = 0; n < items.size(); ++n) {
		const auto& e = *items[n];
		const ItemProfile& prof = profiles[n];
		const ItemProfileDetail::Grid& g = grids[n];
		for (const auto& t : prof.tags)
			if (!ItemTagKnown(t))
				unknown.push_back(e.id + ":" + t);
		std::string features;
		if (prof.Has(ItemHasHole))
			features += "穴 ";
		if (prof.Has(ItemHasHandle))
			features += "持ち手 ";
		if (prof.Has(ItemUpright))
			features += "立つ ";
		std::string kinds;
		static constexpr const char* kindNames[] = {"だ円体", "カプセル", "箱", "円すい"};
		for (const auto& s : prof.parts)
			kinds += std::string(kindNames[int(s.kind)]) + " ";
		std::string tagText;
		for (const auto& t : prof.tags)
			tagText += t + " ";
		char row[1024];
		snprintf(
		    row, sizeof(row), "%d\t%s\t%s\t%s\t%s\t%d\t%s\t%.2f\t%s\t%s\n", int(n), e.id.c_str(), e.name.c_str(),
		    ItemShapeClassName(prof.shape), features.empty() ? "-" : features.c_str(), int(prof.parts.size()),
		    kinds.c_str(), prof.fit, tagText.empty() ? "-" : tagText.c_str(), Collection::ImagePath(e).c_str());
		report << row;
		if (g.nx == 0)
			continue;
		// 面影: ゲームの大きさ (物の体積を武器の形の半分にそろえ、太さの下限をかけたあと) で、元の物の影絵と比べる。
		// 旧 = 基本形のパーツ、新 = 物の形そのもの (距離の場)
		int count = 0;
		for (auto l : g.label)
			count += l != 0;
		const float volume = std::max(1e-6f, float(count) * g.cell * g.cell * g.cell),
		            minHalf = CraftMinThick * .5f / std::cbrt(CraftShapeVolume * .5f / volume);
		const float resembleOld = ItemProfileDetail::Resemblance(g, prof.parts, minHalf),
		            resembleNew = ItemProfileDetail::Resemblance(g, {prof.look}, minHalf);
		report << "#r\t" << e.id << '\t' << resembleOld << '\t' << resembleNew << '\n';
		// 図: 正面 (z を潰す)・上 (y を潰す)・横 (x を潰す)。青 = 元の形と新しい形の両方・だいだい = 太らせて足した所・薄い灰色 = 消えた所
		const int tx = int(n % cols) * tileW, ty = int(n / cols) * tileH;
		const int longest = std::max({g.nx, g.ny, g.nz});
		const float px = float(view) / float(longest);
		SdfPart look = prof.look;
		SdfApplyFloor(look, minHalf);
		std::vector<uint8_t> made(g.label.size(), 0);
		for (int z = 0; z < g.nz; ++z)
			for (int y = 0; y < g.ny; ++y)
				for (int x = 0; x < g.nx; ++x)
					made[g.At(x, y, z)] = SdfPartDistance(look, g.Center(x, y, z)) < 0;
		for (int axis = 0; axis < 3; ++axis) {
			const int ox = tx + pad + axis * (view + pad), oy = ty + pad;
			sheet.Rect(ox, oy, view, view, {236, 236, 236});
			const int w2 = axis == 2 ? g.nz : g.nx, h2 = axis == 1 ? g.nz : g.ny,
			          deep = axis == 0   ? g.nz
			                 : axis == 1 ? g.ny
			                             : g.nx;
			for (int t = 0; t < h2; ++t)
				for (int u = 0; u < w2; ++u) {
					bool orig = false, shape = false;
					for (int k = 0; k < deep; ++k) {
						const int x = axis == 2 ? k : u, y = axis == 1 ? k : t, z = axis == 0 ? k : axis == 1 ? t : u;
						const size_t i = g.At(x, y, z);
						orig = orig || g.label[i] != 0;
						shape = shape || made[i] != 0;
					}
					if (!orig && !shape)
						continue;
					const std::array<uint8_t, 3> col = orig && shape ? std::array<uint8_t, 3>{50, 110, 210}
					                                   : shape       ? std::array<uint8_t, 3>{235, 130, 40}
					                                                 : std::array<uint8_t, 3>{200, 200, 200};
					const int line = axis == 1 ? h2 - 1 - t : t;
					const int X0 = ox + int(float(u) * px), Y0 = oy + view - int(float(line + 1) * px),
					          X1 = ox + int(float(u + 1) * px), Y1 = oy + view - int(float(line) * px);
					sheet.Rect(X0, Y0, std::max(1, X1 - X0), std::max(1, Y1 - Y0), col);
				}
		}
	}
	{
		std::ofstream out(dir + "/tiles.ppm", std::ios::binary);
		out << "P6" << '\n' << sheet.w << ' ' << sheet.h << '\n' << 255 << '\n';
		out.write(reinterpret_cast<const char*>(sheet.rgb.data()), std::streamsize(sheet.rgb.size()));
	}
	if (!unknown.empty()) {
		report << "# unknown tags:";
		for (const auto& u : unknown)
			report << ' ' << u;
		report << '\n';
	}
}
} // namespace Game
