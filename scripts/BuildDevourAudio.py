"""吸収モードの効果音を作る (Resources/Sound/Devour/*.wav)。

使い方: python scripts/BuildDevourAudio.py <素材のフォルダ> [出力先]
必要なもの: numpy, scipy, soundfile

素材のフォルダには OpenGameArt の CC0 素材を展開して置く (出典は Resources/Sound/Devour/CREDITS.md):
  pop1.wav ... pop9.wav          "Pop sounds" (EZduzziteh)
  squish_03.mp3                  "Squish Sounds Effects" (EZduzziteh)
  mud_08.ogg mud_11.ogg mud_24.ogg mud_25.ogg  "25 CC0 mud sfx" (rubberduck)
  slime_07.ogg                   "80 CC0 creature SFX #2" (rubberduck)
  jingles_PIZZI02.ogg jingles_SAX07.ogg  "Music Jingles" (Kenney)

気持ちよさの作り:
  - 芯は音程が上がる泡の音 (水の中で泡がはじける音の形)。耳に痛い 4kHz より上は素材から削る
  - 大きい物ほど低音の手応えを足し、「ごくん」は 2 段にして飲み下す感じを出す
  - note.wav は C5 ちょうどの音。コンボごとにゲーム側で音階へ移し、続けて飲むと旋律になる
"""
import os
import sys

import numpy as np
import soundfile as sf
from scipy import signal

RATE = 48000
rng = np.random.default_rng(731)


def seconds(n):
    return int(RATE * n)


def load(folder, name):
    data, rate = sf.read(os.path.join(folder, name), always_2d=True)
    mono = data.mean(axis=1)
    if rate != RATE:
        mono = signal.resample_poly(mono, RATE, rate)
    # 音の頭まで詰める (鳴らした瞬間に音が出るように)
    start = int(np.argmax(np.abs(mono) > np.abs(mono).max() * 0.08))
    mono = mono[max(0, start - seconds(0.002)):]
    return mono / (np.abs(mono).max() + 1e-9)


def speed(x, ratio):
    """再生速度を変える (音程も変わる)"""
    return signal.resample(x, max(8, int(len(x) / ratio)))


def lowpass(x, hz, order=4):
    return signal.sosfilt(signal.butter(order, hz, "low", fs=RATE, output="sos"), x)


def highpass(x, hz, order=2):
    return signal.sosfilt(signal.butter(order, hz, "high", fs=RATE, output="sos"), x)


def bandpass(x, low, high, order=2):
    return signal.sosfilt(signal.butter(order, [low, high], "band", fs=RATE, output="sos"), x)


def fade(x, attack=0.001, release=0.02):
    x = x.copy()
    a, r = min(seconds(attack), len(x)), min(seconds(release), len(x))
    if a:
        x[:a] *= np.linspace(0, 1, a)
    if r:
        x[-r:] *= np.linspace(1, 0, r) ** 2
    return x


def clip_len(x, length):
    return fade(x[:seconds(length)])


def place(length, *parts):
    """(開始秒, 音, 音量) を重ねる"""
    out = np.zeros(seconds(length))
    for at, sound, gain in parts:
        start = seconds(at)
        end = min(len(out), start + len(sound))
        out[start:end] += gain * sound[:end - start]
    return out


def bubble(start, end, length, decay=14.0):
    """音程が上がる泡の音"""
    t = np.arange(seconds(length)) / RATE
    frequency = start + (end - start) * (1 - np.exp(-t / (length * 0.35)))
    phase = 2 * np.pi * np.cumsum(frequency) / RATE
    tone = np.sin(phase) + 0.18 * np.sin(2 * phase)
    return fade(tone * np.exp(-t * decay), 0.002, 0.015)


def thump(start, end, length, decay=10.0):
    """音程が下がる低音の手応え"""
    t = np.arange(seconds(length)) / RATE
    frequency = end + (start - end) * np.exp(-t * 18)
    return fade(np.sin(2 * np.pi * np.cumsum(frequency) / RATE) * np.exp(-t * decay), 0.002, 0.03)


def mallet(frequency, length=0.7, decay=7.0):
    """木琴の音: 基音に叩いた瞬間の倍音を足す"""
    t = np.arange(seconds(length)) / RATE
    tone = (np.sin(2 * np.pi * frequency * t) * np.exp(-t * decay)
            + 0.3 * np.sin(2 * np.pi * frequency * 4 * t) * np.exp(-t * decay * 3.5)
            + 0.1 * np.sin(2 * np.pi * frequency * 9.2 * t) * np.exp(-t * decay * 8))
    return fade(tone, 0.001, 0.03)


def noise(length):
    return rng.standard_normal(seconds(length))


def whoosh(length, low, high):
    """低い帯域から高い帯域へ抜ける風切り音"""
    t = np.linspace(0, 1, seconds(length))
    source = noise(length)
    out = np.zeros_like(t)
    bands = np.geomspace(low, high, 6)
    for i, center in enumerate(bands):
        peak = i / (len(bands) - 1)
        out += bandpass(source, center * 0.7, center * 1.4) * np.exp(-((t - peak) / 0.28) ** 2)
    return fade(out * np.sin(np.pi * t) ** 0.7, 0.004, 0.03)


def loop_noise(length):
    """切れ目なく繰り返せる掃除機の音"""
    overlap = seconds(0.25)
    size = seconds(length)
    source = noise(length + 0.25 + 0.5)
    body = bandpass(source, 350, 1500) + 0.5 * lowpass(source, 240) + 0.2 * bandpass(source, 2000, 3400)
    body = body[seconds(0.5):]
    ramp = np.sin(np.linspace(0, np.pi / 2, overlap)) ** 2
    out = body[:size].copy()
    out[:overlap] = out[:overlap] * ramp + body[size:size + overlap] * (1 - ramp)
    t = np.arange(size) / RATE
    # 長さに整数回入る周波数だけを使うと、継ぎ目でも波がそろう
    hum = sum(gain * np.sin(2 * np.pi * round(hz * length) / length * t) for hz, gain in ((118, 0.5), (236, 0.28), (354, 0.14)))
    wobble = 1 + 0.12 * np.sin(2 * np.pi * round(5 * length) / length * t)
    return out / (np.abs(out).max() + 1e-9) * wobble + 0.28 * hum


def finish(x, peak=0.72):
    x = x - x.mean()
    return x / (np.abs(x).max() + 1e-9) * peak


def build(folder):
    pops = [load(folder, f"pop{i}.wav") for i in (1, 3, 4, 8, 9)]
    squish = lowpass(load(folder, "squish_03.mp3"), 3400)
    slime = lowpass(load(folder, "slime_07.ogg"), 3200)
    mud_big = lowpass(load(folder, "mud_11.ogg"), 2800)
    mud_hit = lowpass(load(folder, "mud_08.ogg"), 3000)
    mud_tiny = [lowpass(load(folder, n), 3800) for n in ("mud_24.ogg", "mud_25.ogg")]
    out = {}

    # 小物: ぽこっ (連続で鳴るので短く軽く、4 種類で同じ音に聞こえないようにする)
    for i, (a, b, ratio) in enumerate(((520, 980, 1.0), (440, 860, .9), (600, 1100, 1.1), (480, 920, .95))):
        out[f"gulp-small-{i}"] = finish(place(.2,
            (0, lowpass(speed(pops[i], ratio), 4200), .55),
            (0, bubble(a, b, .13, 20), .8),
            (0, clip_len(mud_tiny[i % 2], .12), .16),
            (0, thump(170, 110, .07, 28), .3)), .62)
    # 中くらい: ぐぽっ
    for i, (a, b, ratio) in enumerate(((270, 640, .85), (230, 560, .75))):
        out[f"gulp-mid-{i}"] = finish(place(.36,
            (0, clip_len(speed(squish if i == 0 else slime, ratio), .24), .55),
            (.015, bubble(a, b, .2, 12), .9),
            (0, thump(140, 62, .22, 13), .7),
            (.12, bubble(620, 1040, .1, 24), .22)), .7)
    # 大きい物: ごっくん (飲み下す 2 段と、後から上がる泡)
    out["gulp-big"] = finish(place(.75,
        (0, clip_len(speed(mud_big, .72), .32), .6),
        (0, bubble(170, 430, .24, 9), .9),
        (0, thump(120, 46, .42, 7), .95),
        (.19, bubble(130, 340, .26, 9), .85),
        (.19, thump(95, 42, .3, 9), .6),
        (.42, bubble(620, 1020, .09, 26), .2),
        (.49, bubble(760, 1240, .08, 28), .16),
        (.55, bubble(900, 1420, .07, 30), .12)), .78)
    # 飛びかかりの覆う瞬間: ガバッ
    out["splat"] = finish(place(.5,
        (0, clip_len(speed(mud_hit, .8), .3), .75),
        (0, clip_len(speed(slime, .7), .3), .4),
        (0, thump(130, 48, .36, 8), 1),
        (.03, bubble(240, 520, .18, 13), .5)), .78)
    # 飛びかかり: ズバーッ
    out["pounce"] = finish(whoosh(.24, 260, 2600), .5)
    # 弾かれた: ぼよん
    t = np.arange(seconds(.46)) / RATE
    wobble = 190 + 120 * t / .46 + 60 * np.sin(2 * np.pi * 13 * t) * np.exp(-5 * t)
    phase = 2 * np.pi * np.cumsum(wobble) / RATE
    out["bounce"] = finish(fade((np.sin(phase) + .3 * np.sin(2 * phase)) * np.exp(-6 * t), .002, .04), .62)
    # 硬い物に当たった: ごつん
    out["hit"] = finish(place(.26,
        (0, thump(210, 80, .2, 16), 1),
        (0, fade(lowpass(noise(.05), 1900) * np.exp(-np.arange(seconds(.05)) / RATE * 70), .0005, .005), 1.4)), .68)
    # 砕ける: がしゃん (細かい破片が続けて落ちる)
    crumbs = [(0, thump(170, 55, .3, 9), 1), (0, fade(bandpass(noise(.1), 500, 3000) * np.exp(-np.arange(seconds(.1)) / RATE * 34)), .9)]
    for i in range(9):
        length = .05
        grain = fade(bandpass(noise(length), 900 + 240 * i, 2200 + 300 * i) * np.exp(-np.arange(seconds(length)) / RATE * 80), .0005, .005)
        crumbs.append((.04 + .035 * i + .012 * rng.random(), grain, .55 * .9 ** i))
    out["shatter"] = finish(place(.5, *crumbs), .72)
    # 掃除機の連続音
    out["vacuum-loop"] = finish(loop_noise(2), .5)

    # 音階の音は C を基準にする (ゲーム側で音階の上の音へ移す)
    c5, e5, g5, c6, g6 = 523.25, 659.26, 783.99, 1046.5, 1567.98
    out["note"] = finish(mallet(c5, .5, 9), .6)
    out["confirm"] = finish(place(.5, (0, mallet(g5, .4, 10), .8), (.07, mallet(c6, .42, 9), 1)), .6)
    # ランクが上がった: 生楽器のジングル (ゲームの読み込みは ogg に頼れないので wav にする)
    out["rank-up"] = finish(load(folder, "jingles_PIZZI02.ogg"), .72)
    out["rank-top"] = finish(load(folder, "jingles_SAX07.ogg"), .72)
    # 輪が開いた: 下から持ち上がる
    swell = whoosh(.9, 120, 1500)
    out["gate-open"] = finish(place(1.9,
        (0, swell, .55), (0, thump(90, 44, .9, 3), 1),
        (.62, mallet(c5 / 2, 1.2, 3.5), .8), (.62, mallet(g5 / 2, 1.2, 3.5), .7), (.62, mallet(c5, 1.2, 3.5), .8),
        (.62, mallet(e5, 1.2, 3.5), .7), (.72, mallet(g5, 1, 4), .6), (.82, mallet(c6, 1, 4), .6)), .76)
    # 残り時間
    out["tick"] = finish(mallet(1180, .12, 42), .45)
    out["hurry"] = finish(place(.75, (0, mallet(g5, .3, 9), 1), (.16, mallet(g5, .3, 9), 1), (.32, mallet(c6, .4, 8), 1)), .6)
    out["time-up"] = finish(place(1.2, (0, mallet(g5, .6, 6), 1), (.14, mallet(e5, .6, 6), .9), (.28, mallet(c5, .8, 4), 1), (.28, thump(110, 50, .4, 7), .7)), .68)
    # 変形: 泡が駆け上がる
    out["morph"] = finish(place(.5, *[(.045 * i, bubble(320 + 90 * i, 620 + 170 * i, .11, 20), .55 + .05 * i) for i in range(6)]), .55)
    # メニュー
    out["ui-move"] = finish(mallet(880, .14, 30), .38)
    out["ui-confirm"] = finish(place(.32, (0, mallet(c6, .22, 16), .8), (.06, mallet(g6, .25, 14), 1)), .46)
    out["ui-back"] = finish(place(.28, (0, mallet(g5, .2, 18), 1), (.06, mallet(e5, .2, 18), .8)), .42)
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    folder = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "Resources", "Sound", "Devour")
    os.makedirs(target, exist_ok=True)
    for name, sound in build(folder).items():
        peak = float(np.abs(sound).max())
        if peak >= .9:
            raise SystemExit(f"{name}: peak {peak:.2f} leaves no headroom")
        sf.write(os.path.join(target, name + ".wav"), sound, RATE, subtype="PCM_16")
        print(f"{name:14s} {len(sound) / RATE:5.2f}s peak {peak:.2f}")


if __name__ == "__main__":
    main()
