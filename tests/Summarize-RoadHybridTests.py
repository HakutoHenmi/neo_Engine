"""Archive a measured hybrid sweep and create its static comparison figure.

Requires Pillow for DDS/PNG reading. Run after Run-RoadHybridTests.ps1.
This reads GPU results; it never trains or modifies runtime game assets.
"""
import hashlib
import json
import math
import shutil
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/out/road-texture"
DEST = ROOT / "Docs/Benchmarks/2026-10-27-road-hybrid"
PROFILES = ["hybrid-3", "hybrid-5", "hybrid-8", "hybrid-12", "hybrid-16"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def bc_payload(path):
    data = path.read_bytes()
    assert data[:4] == b"DDS "
    return data[148 if data[84:88] == b"DX10" else 128:]


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    gpu = read_json(OUT / "hybrid-gpu.json")
    assert not gpu["debugLayer"], "Do not publish debug-layer timings as measurements"
    quality = read_json(OUT / "hybrid-quality.json")
    by_name = {v["name"]: v for v in gpu["variants"]}
    baseline = by_name["bcn"]["fileBytes"]
    jpeg_bytes = sum(p.stat().st_size for p in (ROOT / "Resources/Textures/PolyHaven").glob("asphalt_02_*_2k.jpg"))
    normal = bc_payload(OUT / "bcn/normal.dds")
    assert normal == bc_payload(OUT / "bcn-readback/normal.dds")
    reference = Image.open(OUT / "bcn-preview.png").convert("RGB")
    rows = []
    for profile in PROFILES:
        assert normal == bc_payload(OUT / profile / "normal.dds"), f"Normal changed: {profile}"
        metrics = {q["map"]: q for q in quality if q["variant"] == profile}
        diff = ImageChops.difference(reference, Image.open(OUT / f"{profile}-preview.png").convert("RGB"))
        mse = sum(v*v for v in ImageStat.Stat(diff).rms) / 3
        assert mse > 0
        item = dict(by_name[profile])
        item.update({
            "reductionVsBcnPercent": 100 * (1 - item["fileBytes"] / baseline),
            "reductionVsJpegPercent": 100 * (1 - item["fileBytes"] / jpeg_bytes),
            "mip0Quality": metrics,
            "renderedRgbPsnrVsBcnDb": 10 * math.log10(255**2 / mse),
            "normalPayloadByteIdentical": True,
            "ntcSha256": digest(OUT / profile / "material.ntc"),
        })
        rows.append(item)

    selection = {
        "date": "2026-10-27", "selected": "hybrid-12", "capacityAlternative": "hybrid-8",
        "scope": "One fixed offscreen road material; not applied to shipping engine",
        "normal": "Original BC5, payload identical across all 12 mips",
        "notLossless": True, "dllExcludedFromMaterialBytes": True,
        "libntcDllBytes": (ROOT / "build/ntc/sdk/bin/windows-x64/libntc.dll").stat().st_size,
        "sourceSha256": {str(p.relative_to(ROOT)): digest(p) for p in [
            ROOT / "tests/RoadTextureGpuTests.cpp", ROOT / "tests/Run-RoadTextureTests.ps1",
            ROOT / "tests/Run-RoadHybridTests.ps1", Path(__file__).resolve()]},
        "profiles": rows,
    }
    (DEST / "summary.json").write_text(json.dumps(selection, ensure_ascii=False, indent=2), encoding="utf-8")
    for name in ["hybrid-gpu.json", "hybrid-gpu-debug.json", "hybrid-quality.json", "hybrid-inputs.json", "hybrid-gpu-environment.csv"]:
        shutil.copy2(OUT / name, DEST / name)

    selected = OUT / "hybrid-selected"
    selected.mkdir(exist_ok=True)
    shutil.copy2(OUT / "hybrid-12/material.ntc", selected / "material.ntc")
    shutil.copy2(OUT / "bcn/normal.dds", selected / "normal.dds")
    (selected / "selection.json").write_text(json.dumps({k: selection[k] for k in ["selected", "capacityAlternative", "scope", "normal", "notLossless"]}, indent=2), encoding="utf-8")

    # Same UV/lighting, followed by unscaled mip0 crops of the three NTC maps.
    columns = [("bcn", "BCn reference"), ("hybrid-8", "8: capacity alternative"), ("hybrid-12", "12: quality candidate"), ("hybrid-16", "16: larger file")]
    canvas = Image.new("RGB", (1536, 928), "#15191f")
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 21)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 17)
    for col, (profile, label) in enumerate(columns):
        x = col * 384
        draw.text((x+10, 10), label, font=font, fill="white")
        preview = Image.open(OUT / f"{profile}-preview.png").convert("RGB")
        canvas.paste(preview.resize((384, 240), Image.Resampling.LANCZOS), (x, 44))
        for name, top, height in [("albedo", 322, 256), ("roughness", 612, 128), ("ao", 774, 128)]:
            draw.text((x+10, top-27), f"{name} mip0, same 1:1 crop", font=small, fill="#d2d8df")
            image = Image.open(OUT / profile / f"{name}.dds").convert("RGB")
            canvas.paste(image.crop((768, 768, 1152, 768+height)), (x, top))
    canvas.save(DEST / "comparison.png")
    for r in rows:
        print(f'{r["name"]}: {r["fileBytes"]/1048576:.3f} MiB, {r["reductionVsBcnPercent"]:.2f}% smaller than BCn, '
              f'albedo {r["mip0Quality"]["albedo"]["psnrDb"]:.2f} dB, rendered {r["renderedRgbPsnrVsBcnDb"]:.2f} dB, '
              f'wall {r["warmLoadWallMedianMs"]:.2f} ms, GPU {r["gpuTotalMedianMs"]:.2f} ms')


if __name__ == "__main__":
    main()
