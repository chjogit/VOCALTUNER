import sys, subprocess, numpy as np, scipy.signal as signal, scipy.ndimage as ndi
rng = np.random.default_rng(7)

def proto_split(midi_seg, min_frames=12, zero_pad=False):
    """프로토타입 split_by_pitch_steps (구간 인덱스만 반환). zero_pad=False면 medfilt 가장자리만 'nearest'로 교체."""
    voiced_indices = np.where(midi_seg > 0)[0]
    if len(voiced_indices) < min_frames: return []
    v = midi_seg[voiced_indices]
    k = min(11, len(v) // 2 * 2 + 1)
    smoothed = signal.medfilt(v, kernel_size=k) if zero_pad else ndi.median_filter(v, size=k, mode='nearest')
    diff = np.abs(np.gradient(smoothed, 3))
    peaks, _ = signal.find_peaks(diff, height=0.22, distance=min_frames)
    pts = [0] + [voiced_indices[p] for p in peaks] + [len(midi_seg)]
    out = []
    for i in range(len(pts) - 1):
        s, e = pts[i], pts[i + 1]
        if (e - s) < 6: continue
        if len(midi_seg[s:e][midi_seg[s:e] > 0]) == 0: continue
        out.append((int(s), int(e)))
    return out

def make_sequence():
    notes, pitch = [], rng.uniform(55, 70)
    for _ in range(rng.integers(2, 7)):
        n = int(rng.integers(15, 130)); vib = rng.uniform(0, 0.6); f = rng.uniform(4.5, 6.5)
        t = np.arange(n) * 0.005
        notes.append(pitch + vib * np.sin(2 * np.pi * f * t) + rng.normal(0, 0.05, n))
        pitch += rng.choice([-1, 1]) * rng.uniform(0.3, 5.0)
    x = np.concatenate(notes)
    for _ in range(rng.integers(0, 4)):            # 1~4 프레임 유성 끊김 (브릿지 대상)
        p = int(rng.integers(5, len(x) - 10)); x[p:p + int(rng.integers(1, 5))] = 0.0
    return x

trials = [make_sequence() for _ in range(300)]
inp = "\n\n".join("\n".join(f"{v:.9f}" for v in x) for x in trials) + "\n\n"
res = subprocess.run([sys.argv[1]], input=inp, capture_output=True, text=True).stdout.strip().split("\n")
exact = mismatch = zp_diff = 0; examples = []
for x, line in zip(trials, res):
    cpp = [(int(a), int(b)) for a, b in zip(line.split()[0::2], line.split()[1::2])]
    ref = proto_split(x)
    if cpp == ref: exact += 1
    else:
        mismatch += 1
        if len(examples) < 3: examples.append((len(x), ref, cpp))
    if proto_split(x, zero_pad=True) != ref: zp_diff += 1
print(f"trials={len(trials)} identical={exact} different={mismatch}")
print(f"(참고) 원본 medfilt(0 패딩)와 nearest 패딩 결과가 다른 시퀀스: {zp_diff}/{len(trials)}")
for e in examples: print("example", e)
print("blocks per sequence: min/median/max =", min(len(proto_split(x)) for x in trials), int(np.median([len(proto_split(x)) for x in trials])), max(len(proto_split(x)) for x in trials))
