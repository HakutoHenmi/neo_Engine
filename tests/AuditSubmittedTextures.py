"""Read-only package/source inventory for the current Slime city-stage release.

This is a review aid, not a reachability analyzer or an automatic deletion tool.
The meadow-before-city branch was manually traced in ChronoInk.cpp. FBX base-color
references can be supplied from the same ufbx API used by Engine/Model.cpp.
"""
from collections import defaultdict
import csv
import hashlib
import io
import json
from pathlib import Path
import re
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
ZIP = ROOT / "dist/SlimeAssault-Release.zip"
DEST = ROOT / "Docs/TextureAudit"


def read(path):
    return path.read_text(encoding="utf-8-sig", errors="replace")


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    sources = {p.relative_to(ROOT).as_posix(): read(p) for base in ("Game", "Engine")
               for p in (ROOT/base).rglob("*") if p.suffix in {".cpp", ".h", ".hpp", ".inl"}}
    source_text = "\n".join(sources.values())
    tests = {p.relative_to(ROOT).as_posix(): read(p) for p in (ROOT/"tests").rglob("*")
             if p.suffix in {".cpp", ".h", ".inl"}}
    city_ids = set(re.findall(r'"([^"\n]+)"', sources["Game/Systems/CityStage.cpp"]))
    preload = {f"Resources/Textures/PolyHaven/{name}_{suffix}_2k.jpg"
               for name in ["leafy_grass", "boulder_01", "namaqualand_boulder_02", "grass_medium_02"]
               for suffix in ["nor_dx", "Rough", "AO"]}
    preload |= {f"Resources/Textures/PolyHaven/{name}" for name in [
        "leafy_grass_diff_4k.jpg", "boulder_01_diff_2k.jpg", "namaqualand_boulder_02_diff_2k.jpg",
        "rock_09_diff_2k.jpg", "grass_medium_02_rgba_2k.png"]}
    preload |= {f"Resources/Textures/PBR/rock_09_{suffix}.jpg" for suffix in ["nor_dx", "Rough", "AO"]}
    dependency_path = ROOT/"tests/out/texture-fbx-dependencies.tsv"
    deps = read(dependency_path).splitlines() if dependency_path.exists() else []
    dependency_names = set()
    for line in deps:
        parts = line.split("\t")
        if len(parts) == 4:
            for name in parts[1:3]:
                dependency_names.add(name.replace("\\", "/").split("/")[-1].lower())

    def references(name, corpus):
        return [f"{path}:{num}" for path, text in corpus.items()
                for num, line in enumerate(text.splitlines(), 1) if name in line]

    rows = []
    with ZipFile(ZIP) as archive:
        packaged_data = {f"ZIP/{entry.filename}": archive.read(entry).decode("utf-8-sig", errors="replace")
                         for entry in archive.infolist() if entry.filename.startswith(("Resources/Scenes/", "Resources/Data/", "Resources/Models/"))
                         and Path(entry.filename).suffix.lower() in {".json", ".mtl"}}
        data_text = "\n".join(packaged_data.values())
        for entry in archive.infolist():
            name = entry.filename.replace("\\", "/")
            p = Path(name)
            if not name.startswith("Resources/Textures/") or p.suffix.lower() not in {".jpg", ".png", ".dds"}:
                continue
            family = re.sub(r"_(diff|nor_dx|Rough|AO)_\dk$", "", p.stem)
            evidence = references(p.name, sources)
            if "/PolyHaven/" in name and family in city_ids:
                status = "city_material"
                evidence = references(f'"{family}"', {"Game/Systems/CityStage.cpp": sources["Game/Systems/CityStage.cpp"]})
                evidence += ["Game/Systems/CityStage.cpp:93-105 (material path expansion)"]
            elif name in preload:
                status = "meadow_loaded_not_drawn_in_city"
                evidence += ["Game/Systems/ChronoInk.cpp:40-54 (loads)", "Game/Systems/ChronoInk.cpp:76 (city branch skips meadow geometry)"]
            elif name in source_text or ("/UI/" in name and (p.stem in source_text or p.name in source_text)):
                status = "runtime_reference_keep"
                if not evidence:
                    evidence = references(p.stem, sources)
            elif name in data_text or p.name in data_text:
                status = "packaged_data_reference_keep"
                evidence = references(p.name, packaged_data)
            else:
                test_refs = references(p.name, tests)
                if test_refs:
                    status = "test_only_candidate"
                    evidence = test_refs
                elif p.name.lower() in dependency_names:
                    status = "model_name_only_candidate"
                    evidence = ["FBX base-color basename matches; inspect actual relative path and scene reachability"]
                else:
                    status = "no_runtime_reference_candidate"
                    evidence = ["No filename/runtime-family/inspected FBX base-color match; manual review required before removal"]
            rows.append(dict(path=name, family=family, status=status, bytes=entry.file_size,
                             compressedBytes=entry.compress_size, evidence=evidence))
    groups = defaultdict(lambda: dict(count=0, compressedBytes=0, bytes=0))
    for row in rows:
        item = groups[row["status"]]
        item["count"] += 1
        for key in ("bytes", "compressedBytes"):
            item[key] += row[key]
    result = dict(zip=str(ZIP.relative_to(ROOT)), zipBytes=ZIP.stat().st_size,
                  zipSha256=hashlib.sha256(ZIP.read_bytes()).hexdigest(), scope="Current normal city route; legacy/test modes are separate",
                  method="Static source/data-flow audit plus optional ufbx material inspection; no runtime file-I/O trace",
                  fbxBaseColorReferencesInspected=len(deps),
                  sourceSha256={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in [
                      "Game/Systems/ChronoInk.cpp", "Game/Systems/CityStage.cpp", "Game/Systems/ChronoSystem.cpp",
                      "Engine/Model.cpp", "Engine/PathUtils.h", "Resources/Data/game.json", "scripts/SlimeResources.json"]},
                  groups=dict(groups), textures=rows)
    (DEST/"inventory.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    stream = io.StringIO(newline="")
    writer = csv.writer(stream)
    writer.writerow(["path", "status", "compressed_MiB", "uncompressed_MiB", "evidence"])
    for row in rows:
        writer.writerow([row["path"], row["status"], f'{row["compressedBytes"]/1048576:.6f}',
                         f'{row["bytes"]/1048576:.6f}', " | ".join(row["evidence"])])
    (DEST/"inventory.csv").write_text(stream.getvalue(), encoding="utf-8-sig")
    if deps:
        (DEST/"fbx-basecolor-dependencies.tsv").write_text("\n".join(deps)+"\n", encoding="utf-8")
    for status, item in groups.items():
        print(f'{status}: {item["count"]} images, {item["compressedBytes"]/1048576:.3f} MiB in ZIP')


if __name__ == "__main__":
    main()
