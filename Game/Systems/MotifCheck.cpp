// MotifCheck.cpp
// 姿の型紙の読み込みと、当てはめの確かめ (H2。ChronoSystem の一部。当てはめは Chrono/MotifRules.h)。
//  --motif-stats: 作る画面が開いたら、図鑑の物 (建物を除く 123 個) の組み合わせを全部 (順番あり・同じ物を重ねてよい = 123^3 通り) 当てはめて、
//  tests/out/motifs/ に書いて終わる:
//   stats.txt   型紙ごとに選ばれた数と割合・型紙なしの数・枠に入った物の数・並べる順で姿が変わるか
//   items.tsv   物ごとに、どの型紙のどの枠に入ったか (多い順)・使われなかった割合
//   best.tsv    型紙ごとに点数の高い組み合わせ (10 個ずつ)
//   samples.tsv 決まった間隔で選んだ組み合わせ (400 個)
#include "ChronoSystemInternal.h"
#include "../Data/Collection.h"
#include "../Chrono/MotifShape.h"
#include "../Chrono/WeaponShapeMetrics.h"
#include <filesystem>
#include <fstream>

namespace Game {
// 部品の並びを箱に分ける (WeaponCraft.cpp)
WeaponShapeGrid RasterizeParts(const std::vector<SdfPart>& parts, const std::string& key, int& count);
namespace {
MotifSize SizeOf(const std::string& s) {
	return s == "大" ? MotifSize::Big : s == "小" ? MotifSize::Small : MotifSize::Medium;
}
} // namespace

// 姿の型紙を Resources/Data/motifs.json から読む
std::vector<Motif> ChronoSystem::LoadMotifs() {
	std::vector<Motif> motifs;
	std::ifstream file("Resources/Data/motifs.json");
	if (!file)
		return motifs;
	const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
	if (!data.is_object() || !data.contains("motifs"))
		return motifs;
	for (const auto& m : data["motifs"]) {
		Motif motif;
		std::vector<std::pair<size_t, std::string>> onNames;
		motif.id = m.value("id", std::string());
		motif.name = m.value("name", std::string());
		motif.family = m.value("family", std::string());
		motif.nameFormat = m.value("name_format", motif.name);
		if (m.contains("traits"))
			for (auto it = m["traits"].begin(); it != m["traits"].end(); ++it)
				for (int t = 0; t < FuseTraitCount; ++t)
					if (it.key() == FuseTraitName(FuseTrait(t)))
						motif.traitFit[size_t(t)] = it.value().get<float>();
		if (m.contains("tags"))
			for (auto it = m["tags"].begin(); it != m["tags"].end(); ++it)
				motif.tagFit[it.key()] = it.value().get<float>();
		for (const auto& s : m["slots"]) {
			MotifSlot slot;
			slot.name = s.value("name", std::string());
			slot.main = s.value("main", false);
			slot.mirror = s.value("mirror", false);
			slot.size = SizeOf(s.value("size", std::string("中")));
			slot.handle = s.value("handle", 0.f);
			slot.upright = s.value("upright", 0.f);
			slot.hole = s.value("hole", 0.f);
			slot.emptyPenalty = s.value("empty", 0.f);
			if (s.contains("shapes"))
				for (auto it = s["shapes"].begin(); it != s["shapes"].end(); ++it)
					for (int c = 0; c < int(ItemShapeClass::Count); ++c)
						if (it.key() == ItemShapeClassName(ItemShapeClass(c)))
							slot.shapeFit[size_t(c)] = it.value().get<float>();
			if (s.contains("place")) {
				const auto& p = s["place"];
				auto vec = [&](const char* key, Vec fallback) {
					if (!p.contains(key) || !p[key].is_array() || p[key].size() < 3)
						return fallback;
					return Vec{p[key][0].get<float>(), p[key][1].get<float>(), p[key][2].get<float>()};
				};
				auto axis = [&](const char* key) {
					const std::string a = p.value(key, std::string());
					return a == "x" ? 0 : a == "y" ? 1 : a == "z" ? 2 : -1;
				};
				auto& pl = slot.place;
				pl.at = vec("at", {});
				pl.off = vec("off", {});
				pl.scale = p.value("scale", 1.f);
				pl.longAxis = axis("long");
				pl.thinAxis = axis("thin");
				pl.keep = p.value("keep", false);
				if (p.value("motion", std::string()) == "flap")
					pl.motion = SdfMotion::Flap;
				if (p.contains("fill")) {
					const auto& f = p["fill"];
					const std::string kind = f.value("kind", std::string());
					pl.hasFill = true;
					pl.fillKind = kind == "カプセル" ? SdfKind::Capsule
					              : kind == "箱"     ? SdfKind::RoundBox
					                                 : SdfKind::Ellipsoid;
					if (f.contains("half") && f["half"].size() >= 3)
						pl.fillHalf = {f["half"][0].get<float>(), f["half"][1].get<float>(), f["half"][2].get<float>()};
				}
				// (on は名前で書く。読み終えてから番号に直す)
				slot.place.on = -2 - int(motif.slots.size());
				if (!p.contains("on"))
					slot.place.on = -1;
				else
					onNames.push_back({motif.slots.size(), p["on"].get<std::string>()});
			}
			motif.slots.push_back(slot);
		}
		// (J1) 土台と、物を並べる所
		auto vecOf = [](const nlohmann::json& j, const char* key, Vec fallback) {
			if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
				return fallback;
			return Vec{j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>()};
		};
		auto axisOf = [](const nlohmann::json& j) {
			const std::string a = j.value("axis", std::string("z"));
			return a == "x" ? 0 : a == "y" ? 1 : 2;
		};
		if (m.contains("base"))
			for (const auto& b : m["base"]) {
				MotifBasePart part;
				const std::string kind = b.value("kind", std::string());
				part.kind = kind == "カプセル" ? SdfKind::Capsule
				            : kind == "箱"     ? SdfKind::RoundBox
				            : kind == "円すい" ? SdfKind::Cone
				                               : SdfKind::Ellipsoid;
				part.center = vecOf(b, "at", {});
				part.half = vecOf(b, "half", {.5f, .5f, .5f});
				part.axis = axisOf(b);
				part.round = b.value("round", .2f);
				part.mirror = b.value("mirror", false);
				motif.base.push_back(part);
			}
		if (m.contains("sockets"))
			for (const auto& s : m["sockets"]) {
				MotifSocket socket;
				socket.name = s.value("name", std::string());
				socket.emblem = s.value("role", std::string()) == "紋章";
				socket.mirror = s.value("mirror", false);
				const std::string pattern = s.value("pattern", std::string());
				socket.pattern = pattern == "輪"   ? MotifPattern::Ring
				                 : pattern == "列" ? MotifPattern::Line
				                                   : MotifPattern::Point;
				socket.at = vecOf(s, "at", {});
				const int axis = axisOf(s);
				socket.axis = axis == 0 ? Vec{1, 0, 0} : axis == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
				socket.from = vecOf(s, "from", {});
				socket.to = vecOf(s, "to", {});
				socket.normal = vecOf(s, "normal", {});
				if (s.contains("radius") && s["radius"].size() >= 2) {
					socket.radius1 = s["radius"][0].get<float>();
					socket.radius2 = s["radius"][1].get<float>();
				}
				if (s.contains("angles") && s["angles"].size() >= 2) {
					socket.angle0 = s["angles"][0].get<float>();
					socket.angle1 = s["angles"][1].get<float>();
				}
				motif.sockets.push_back(socket);
			}
		for (const auto& [index, name] : onNames)
			for (size_t k = 0; k < index; ++k)
				if (motif.slots[k].name == name)
					motif.slots[index].place.on = int(k);
		for (auto& slot : motif.slots)
			if (slot.place.on < -1)
				slot.place.on = -1;
		motifs.push_back(std::move(motif));
	}
	return motifs;
}

const std::vector<Motif>& ChronoSystem::Motifs() {
	static const std::vector<Motif> motifs = LoadMotifs();
	return motifs;
}

void ChronoSystem::WriteMotifStats() {
	const std::string dir = "tests/out/motifs";
	std::filesystem::create_directories(dir);
	const std::vector<ItemProfile> profiles = BuildItemProfiles();
	const std::vector<Motif> motifs = LoadMotifs();
	std::vector<const Collection::Entry*> entries;
	for (const auto& e : Collection::Entries())
		if (e.battle != "building")
			entries.push_back(&e);
	const int n = int(std::min(profiles.size(), entries.size())), M = int(motifs.size());
	std::vector<MotifItem> items;
	for (int i = 0; i < n; ++i)
		items.push_back(PrepareMotifItem(
		    motifs, profiles[size_t(i)],
		    FuseTraitsOf(BattleActionOf(entries[size_t(i)]->action, entries[size_t(i)]->battle))));
	// 名前: 型紙の name_format の {枠の名前} を、その枠に入った物の名前にする (空いた枠は本体の物の名前)
	auto nameOf = [&](const MotifChoice& c, const std::array<int, 3>& ids) {
		if (c.motif < 0)
			return std::string("(型紙なし)");
		const auto& m = motifs[size_t(c.motif)];
		std::string name = m.nameFormat;
		for (size_t s = 0; s < m.slots.size(); ++s) {
			const std::string key = "{" + m.slots[s].name + "}";
			const size_t at = name.find(key);
			if (at == std::string::npos)
				continue;
			std::string fill = entries[size_t(ids[0])]->name;
			for (int i = 0; i < 3; ++i)
				if (c.slotOf[size_t(i)] == int(s))
					fill = entries[size_t(ids[size_t(i)])]->name;
			name.replace(at, key.size(), fill);
		}
		return name;
	};
	auto describe = [&](const MotifChoice& c, const std::array<int, 3>& ids) {
		std::string text;
		for (int i = 0; i < 3; ++i) {
			const int s = c.slotOf[size_t(i)];
			text += entries[size_t(ids[size_t(i)])]->name + "=" +
			        (c.motif < 0 || s < 0 ? std::string("-") : motifs[size_t(c.motif)].slots[size_t(s)].name) + " ";
		}
		return text;
	};
	// 全部の組み合わせ
	std::vector<int8_t> result(size_t(n) * size_t(n) * size_t(n), -1);
	std::vector<long long> motifCount(size_t(M + 1), 0);
	std::array<long long, 4> placed{};
	std::vector<std::vector<std::vector<int>>> slotUse(
	    size_t(M), std::vector<std::vector<int>>(8, std::vector<int>(size_t(n), 0)));
	std::vector<long long> unused(size_t(n), 0), appear(size_t(n), 0);
	struct Best {
		float score;
		std::array<int, 3> ids;
		MotifChoice choice;
	};
	std::vector<std::vector<Best>> best(static_cast<size_t>(M));
	std::ofstream samples(dir + "/samples.tsv");
	samples << "#\tbody\tpart2\tpart3\tmotif\tname\tassign\tscore\tshape\ttrait\ttag\n";
	const long long total = (long long)n * n * n, step = std::max(1LL, total / 400);
	double scoreSum = 0;
	for (int a = 0; a < n; ++a)
		for (int b = 0; b < n; ++b)
			for (int c = 0; c < n; ++c) {
				const std::array<int, 3> ids{a, b, c};
				const MotifChoice choice = ChooseMotif(
				    motifs, {&items[size_t(a)], &items[size_t(b)], &items[size_t(c)]},
				    MotifHash(uint32_t(a * 131 + b), uint32_t(c)));
				const long long index = ((long long)a * n + b) * n + c;
				result[size_t(index)] = int8_t(choice.motif);
				++motifCount[size_t(choice.motif + 1)];
				scoreSum += choice.score;
				int count = 0;
				for (int i = 0; i < 3; ++i) {
					++appear[size_t(ids[size_t(i)])];
					const int s = choice.slotOf[size_t(i)];
					if (choice.motif >= 0 && s >= 0) {
						++count;
						++slotUse[size_t(choice.motif)][size_t(s)][size_t(ids[size_t(i)])];
					} else
						++unused[size_t(ids[size_t(i)])];
				}
				++placed[size_t(count)];
				if (choice.motif >= 0) {
					auto& list = best[size_t(choice.motif)];
					if (list.size() < 10 || choice.score > list.back().score) {
						list.push_back({choice.score, ids, choice});
						std::sort(
						    list.begin(), list.end(), [](const Best& x, const Best& y) { return x.score > y.score; });
						if (list.size() > 10)
							list.pop_back();
					}
				}
				if (index % step == 0) {
					char line[256];
					snprintf(
					    line, sizeof(line), "%.2f\t%.2f\t%.2f\t%.2f", choice.score, choice.shape, choice.trait,
					    choice.tag);
					samples << index << '\t' << entries[size_t(a)]->name << '\t' << entries[size_t(b)]->name << '\t'
					        << entries[size_t(c)]->name << '\t'
					        << (choice.motif < 0 ? std::string("-") : motifs[size_t(choice.motif)].name) << '\t'
					        << nameOf(choice, ids) << '\t' << describe(choice, ids) << '\t' << line << '\n';
				}
			}
	// 並べる順で姿が変わるか (違う物 3 つの組で、6 通りの並べ方のうち何種類の姿になるか)
	std::array<long long, 7> orderVariety{};
	for (int a = 0; a < n; ++a)
		for (int b = a + 1; b < n; ++b)
			for (int c = b + 1; c < n; ++c) {
				const std::array<std::array<int, 3>, 6> perms{
				    {{a, b, c}, {a, c, b}, {b, a, c}, {b, c, a}, {c, a, b}, {c, b, a}}};
				std::array<int, 6> got{};
				for (int p = 0; p < 6; ++p) {
					const auto& q = perms[size_t(p)];
					got[size_t(p)] = result[size_t(((long long)q[0] * n + q[1]) * n + q[2])];
				}
				std::sort(got.begin(), got.end());
				++orderVariety[size_t(std::unique(got.begin(), got.end()) - got.begin())];
			}
	std::ofstream stats(dir + "/stats.txt");
	stats << "組み合わせ " << total << " 通り (物 " << n << " 個・型紙 " << M << " 個)。点数の平均 "
	      << scoreSum / double(total) << "\n\n型紙ごとに選ばれた数:\n";
	for (int m = -1; m < M; ++m)
		stats << "  " << (m < 0 ? std::string("(型紙なし)") : motifs[size_t(m)].name) << '\t'
		      << motifCount[size_t(m + 1)] << '\t' << 100.0 * double(motifCount[size_t(m + 1)]) / double(total)
		      << "%\n";
	stats << "\n枠に入った物の数 (0〜3):";
	for (int k = 0; k < 4; ++k)
		stats << ' ' << 100.0 * double(placed[size_t(k)]) / double(total) << '%';
	stats << "\n\n違う物 3 つの組で、6 通りの並べ方のうち何種類の姿になるか (1〜6):";
	long long sets = 0;
	for (int k = 1; k <= 6; ++k)
		sets += orderVariety[size_t(k)];
	for (int k = 1; k <= 6; ++k)
		stats << ' ' << k << ":" << 100.0 * double(orderVariety[size_t(k)]) / double(std::max(1LL, sets)) << '%';
	stats << '\n';
	// 物ごとの役
	std::ofstream itemOut(dir + "/items.tsv");
	itemOut << "#id\tname\tshape\ttags\tunused%\troles (多い順)\n";
	for (int i = 0; i < n; ++i) {
		std::vector<std::pair<int, std::string>> roles;
		for (int m = 0; m < M; ++m)
			for (size_t s = 0; s < motifs[size_t(m)].slots.size() && s < 8; ++s)
				if (const int k = slotUse[size_t(m)][s][size_t(i)])
					roles.push_back({k, motifs[size_t(m)].name + ":" + motifs[size_t(m)].slots[s].name});
		std::sort(roles.begin(), roles.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
		std::string tags, text;
		for (const auto& t : profiles[size_t(i)].tags)
			tags += t + " ";
		for (size_t r = 0; r < roles.size() && r < 5; ++r)
			text +=
			    roles[r].second + "(" + std::to_string(100 * roles[r].first / std::max(1LL, appear[size_t(i)])) + "%) ";
		itemOut << entries[size_t(i)]->id << '\t' << entries[size_t(i)]->name << '\t'
		        << ItemShapeClassName(profiles[size_t(i)].shape) << '\t' << tags << '\t'
		        << 100 * unused[size_t(i)] / std::max(1LL, appear[size_t(i)]) << '\t' << text << '\n';
	}
	std::ofstream bestOut(dir + "/best.tsv");
	bestOut << "#motif\tname\tassign\tscore\n";
	for (int m = 0; m < M; ++m)
		for (const auto& b : best[size_t(m)])
			bestOut << motifs[size_t(m)].name << '\t' << nameOf(b.choice, b.ids) << '\t' << describe(b.choice, b.ids)
			        << '\t' << b.score << '\n';
}

// (H3) --motif-shape-check: 図鑑の物 (建物を除く) から決まった並びで組み合わせを選び (既定 300。--motif-shape-count=N)、
// 武器の形を作って測る。tests/out/motifs/shapes.txt (型紙ごとの数・部品にしなかった物の数・点数・問題ごとの数) と
// shapes.tsv (組み合わせごと) を書く。柱の突き出しは 0 でなければならない
void ChronoSystem::WriteMotifShapeCheck() {
	const std::string dir = "tests/out/motifs";
	std::filesystem::create_directories(dir);
	std::vector<const Collection::Entry*> entries;
	for (const auto& e : Collection::Entries())
		if (e.battle != "building")
			entries.push_back(&e);
	const int n = int(entries.size());
	int want = 300;
	if (const wchar_t* at = wcsstr(GetCommandLineW(), L"--motif-shape-count="))
		want = std::max(1, _wtoi(at + 20));
	std::map<std::string, int> motifCount;
	std::array<int, 4> dropCount{};
	std::array<int, size_t(WeaponShapeIssue::Count)> issueCount{};
	long long scoreSum = 0;
	double symmetrySum = 0;
	long long repeatSum = 0, visibleSum = 0;
	std::ofstream rows(dir + "/shapes.tsv");
	rows << "#\tmotif\tdrops\tscore\tbody\tpart2\tpart3\tpillar\tissues\tsymmetry\trepeats\tvisible\n";
	for (int t = 0; t < want; ++t) {
		const std::array<int, 3> ids{(t * 7) % n, (t * 37 + 11) % n, (t * 61 + 29) % n};
		BattleWeapon w;
		w.filled = 3;
		for (int i = 0; i < 3; ++i) {
			const auto* e = entries[size_t(ids[size_t(i)])];
			w.ids[size_t(i)] = e->id;
			w.actions[size_t(i)] = BattleActionOf(e->action, e->battle);
		}
		const auto* g = WeaponShapeOf(BuildWeaponShape(w));
		if (!g)
			continue;
		// (形の箱の並びも書く: nx ny nz (int32) と埋まり・部品の番号 (1 バイトずつ)。絵にして確かめる用)
		// (J2) 並べるモデルも、箱を 2 倍に細かくして書き込む (部品の番号 2 が繰り返し・3 が紋章)。面より外に出た箱のあるモデルを「見える」と数える
		int repeats = 0, visible = 0;
		{
			constexpr int F = 2;
			const int NX = g->nx * F, NY = g->ny * F, NZ = g->nz * F;
			const float fine = g->cell / F;
			std::vector<uint8_t> fill(size_t(NX) * NY * NZ), part(fill.size());
			auto baseAt = [&](int X, int Y, int Z) {
				return g->fill[size_t(((Z / F) * g->ny + Y / F) * g->nx + X / F)] != 0;
			};
			for (int Z = 0; Z < NZ; ++Z)
				for (int Y = 0; Y < NY; ++Y)
					for (int X = 0; X < NX; ++X)
						if (baseAt(X, Y, Z)) {
							fill[size_t((Z * NY + Y) * NX + X)] = 1;
							part[size_t((Z * NY + Y) * NX + X)] = 1;
						}
			const auto& fields = SdfFields();
			const ItemProfile* profs[3] = {ItemProfileOf(w.ids[0]), ItemProfileOf(w.ids[1]), ItemProfileOf(w.ids[2])};
			for (const auto& m : g->models) {
				const ItemProfile* prof = profs[m.item];
				if (!prof || prof->field < 0)
					continue;
				const auto& field = fields[size_t(prof->field)];
				const float reach = m.scale * std::max({field.half.x, field.half.y, field.half.z}) * 1.8f;
				const int x0 = std::max(0, int((m.pos.x - reach - g->lo.x) / fine)),
				          x1 = std::min(NX - 1, int((m.pos.x + reach - g->lo.x) / fine)),
				          y0 = std::max(0, int((m.pos.y - reach - g->lo.y) / fine)),
				          y1 = std::min(NY - 1, int((m.pos.y + reach - g->lo.y) / fine)),
				          z0 = std::max(0, int((m.pos.z - reach - g->lo.z) / fine)),
				          z1 = std::min(NZ - 1, int((m.pos.z + reach - g->lo.z) / fine));
				bool out = false;
				for (int Z = z0; Z <= z1; ++Z)
					for (int Y = y0; Y <= y1; ++Y)
						for (int X = x0; X <= x1; ++X) {
							const Vec p = g->lo + Vec{float(X) + .5f, float(Y) + .5f, float(Z) + .5f} * fine;
							const Vec d = p - m.pos;
							const Vec q{
							    (d.x * m.axes[0].x + d.y * m.axes[0].y + d.z * m.axes[0].z) / m.scale,
							    (d.x * m.axes[1].x + d.y * m.axes[1].y + d.z * m.axes[1].z) / m.scale,
							    (d.x * m.axes[2].x + d.y * m.axes[2].y + d.z * m.axes[2].z) / m.scale};
							float fd = 0, ft = 0;
							field.Sample(q, fd, ft);
							if (fd >= 0)
								continue;
							const size_t i = size_t((Z * NY + Y) * NX + X);
							out = out || !fill[i];
							fill[i] = 1;
							part[i] = m.emblem ? 3 : 2;
						}
				if (!m.emblem) {
					++repeats;
					visible += out;
				}
			}
			std::ofstream vox(dir + "/shape-" + std::to_string(t) + ".vox", std::ios::binary);
			const int32_t dims[3] = {NX, NY, NZ};
			vox.write(reinterpret_cast<const char*>(dims), sizeof(dims));
			vox.write(reinterpret_cast<const char*>(fill.data()), std::streamsize(fill.size()));
			vox.write(reinterpret_cast<const char*>(part.data()), std::streamsize(part.size()));
		}
		WeaponShapeVoxels v{g->nx, g->ny, g->nz, g->cell, &g->fill, &g->part};
		const WeaponShapeReport r = MeasureWeaponShape(v);
		const std::string motif = g->motif.empty() ? std::string("(型紙なし)") : g->motif;
		++motifCount[motif];
		++dropCount[size_t(std::clamp(g->motifDrops, 0, 3))];
		scoreSum += r.score;
		// (J2) 規則の数: 左右対称 (埋まった箱を左右に映して重なる割合)・繰り返しの物の数と、スライムの面で残る数
		// (残る = その物の部品の箱が、太さの下限の玉の 4 割以上)
		float symmetry = 1;
		{
			long long same = 0, total = 0;
			for (int z = 0; z < g->nz; ++z)
				for (int y = 0; y < g->ny; ++y)
					for (int x = 0; x < g->nx; ++x) {
						const bool a = g->fill[size_t((z * g->ny + y) * g->nx + x)] != 0,
						           b = g->fill[size_t((z * g->ny + y) * g->nx + (g->nx - 1 - x))] != 0;
						same += a && b;
						total += a || b;
					}
			symmetry = total ? float(same) / float(total) : 1.f;
		}
		symmetrySum += symmetry;
		repeatSum += repeats;
		visibleSum += visible;
		std::string issues;
		for (int k = 0; k < int(WeaponShapeIssue::Count); ++k)
			if (r.issues[size_t(k)]) {
				++issueCount[size_t(k)];
				issues += WeaponShapeIssueName(WeaponShapeIssue(k)) + std::string(" ");
			}
		rows << t << '\t' << motif << '\t' << g->motifDrops << '\t' << r.score << '\t' << entries[size_t(ids[0])]->name
		     << '\t' << entries[size_t(ids[1])]->name << '\t' << entries[size_t(ids[2])]->name << '\t' << r.pillar
		     << '\t' << issues << '\t' << symmetry << '\t' << repeats << '\t' << visible << '\n';
	}
	std::ofstream out(dir + "/shapes.txt");
	out << "形 " << want << " 個。点数の平均 " << double(scoreSum) / double(std::max(1, want)) << "\n左右対称の平均 "
	    << symmetrySum / std::max(1, want) << "。繰り返しの物の数の平均 " << double(repeatSum) / std::max(1, want)
	    << " (面から見えるモデルの数の平均 " << double(visibleSum) / std::max(1, want) << ")\n\n型紙ごと:\n";
	for (const auto& [name, count] : motifCount)
		out << "  " << name << '\t' << count << '\n';
	out << "\n部品にしなかった物の数 (0〜3):";
	for (int k = 0; k < 4; ++k)
		out << ' ' << dropCount[size_t(k)];
	out << "\n\n問題ごとの数:\n";
	for (int k = 0; k < int(WeaponShapeIssue::Count); ++k)
		out << "  " << WeaponShapeIssueName(WeaponShapeIssue(k)) << '\t' << issueCount[size_t(k)] << '\n';
}
// (J1) --motif-base-sheet: 型紙ごとの土台だけの形を、1 つ目の物を 4 種類 (丸・長い・平たい・箱) 変えて作り、
// tests/out/motifs/base-<型紙>-<物>.vox (箱の並び) と .pts (並べる所の点: 役 (0 繰り返し・1 紋章) と箱の座標) と
// base.txt (形の点数・柱の突き出し・土台の倍率) を書く。並べる所の点は、繰り返し 6 個 (左右の所は両側)・紋章 1 個
void ChronoSystem::WriteMotifBaseSheet() {
	const std::string dir = "tests/out/motifs";
	std::filesystem::create_directories(dir);
	const auto& motifs = Motifs();
	const std::array<const char*, 4> bodies{"daruma", "enpitsu", "note", "tsumiki"};
	std::ofstream out(dir + "/base.txt");
	out << "#motif\tbody\tscale\tscore\tpillar\tissues\n";
	for (size_t m = 0; m < motifs.size(); ++m)
		for (size_t b = 0; b < bodies.size(); ++b) {
			const Vec scale = MotifBaseScale(ItemProfileOf(bodies[b]));
			std::vector<SdfPart> parts;
			WeaponShapeGrid g;
			float minHalf = 0, k = 1;
			for (int pass = 0; pass < 4; ++pass) {
				parts = BuildMotifBase(motifs[m], scale);
				for (auto& s : parts)
					SdfApplyFloor(s, minHalf);
				int count = 0;
				g = RasterizeParts(parts, "base", count);
				const float longest = float(std::max({g.nx, g.ny, g.nz})) * g.cell;
				const float volume = std::max(1e-4f, float(count) * g.cell * g.cell * g.cell);
				k = std::min(std::cbrt(CraftShapeVolume / volume), CraftShapeLongest / std::max(longest, 1e-3f));
				const float want = CraftMinThick * .5f / k;
				if (std::abs(want - minHalf) < .01f * want)
					break;
				minHalf = want;
			}
			const std::string name = dir + "/base-" + motifs[m].id + "-" + bodies[b];
			{
				std::ofstream vox(name + ".vox", std::ios::binary);
				const int32_t dims[3] = {g.nx, g.ny, g.nz};
				vox.write(reinterpret_cast<const char*>(dims), sizeof(dims));
				vox.write(reinterpret_cast<const char*>(g.fill.data()), std::streamsize(g.fill.size()));
				vox.write(reinterpret_cast<const char*>(g.part.data()), std::streamsize(g.part.size()));
			}
			{
				std::ofstream pts(name + ".pts");
				for (const auto& sk : motifs[m].sockets) {
					const auto points = MotifSocketPoints(sk, sk.emblem ? 1 : 6, scale, parts, CraftBlend);
					for (const auto& q : points)
						for (float side : {1.f, -1.f}) {
							if (side < 0 && !sk.mirror)
								continue;
							const Vec p{q.pos.x * side, q.pos.y, q.pos.z};
							const Vec n{q.normal.x * side, q.normal.y, q.normal.z};
							pts << (sk.emblem ? 1 : 0) << ' ' << (p.x - g.lo.x) / g.cell << ' '
							    << (p.y - g.lo.y) / g.cell << ' ' << (p.z - g.lo.z) / g.cell << ' ' << n.x << ' ' << n.y
							    << ' ' << n.z << '\n';
						}
				}
			}
			WeaponShapeVoxels v{g.nx, g.ny, g.nz, g.cell * k, &g.fill, nullptr};
			const WeaponShapeReport r = MeasureWeaponShape(v);
			std::string issues;
			for (int i = 0; i < int(WeaponShapeIssue::Count); ++i)
				if (r.issues[size_t(i)])
					issues += WeaponShapeIssueName(WeaponShapeIssue(i)) + std::string(" ");
			char line[128];
			snprintf(line, sizeof(line), "%.2f,%.2f,%.2f", scale.x, scale.y, scale.z);
			out << motifs[m].name << '\t' << bodies[b] << '\t' << line << '\t' << r.score << '\t' << r.pillar << '\t'
			    << issues << '\n';
		}
}
} // namespace Game
