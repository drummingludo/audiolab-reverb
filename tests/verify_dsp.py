"""Banc de test du moteur Audio Lab Reverb : compare le plugin (C++) au prototype « Labo Reverb » (JavaScript),
puis mesure le RT60 obtenu dans chaque mode et la vitesse de calcul."""
import hashlib, json, os, subprocess, time, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get("ALR_TMP", "/tmp/alr")
os.makedirs(TMP, exist_ok=True)
rng = np.random.default_rng(1234)
OK = True
REF_SHA = "88ac8e4e40d0e4c9bcaaf996bd0caad51deab35f6c9daeba37d0be8e6fef4eb2"   # moteur du Labo Reverb (artifact du 01/11/2026)
MODES = ["chamber", "plate", "spring", "room", "hall", "church"]
DEF = {"chamber": (0, 1.5, .3, .4, .5, .8), "plate": (10, 2.2, .25, .6, .3, 1), "spring": (0, 2.0, .35, .3, .4, .5),
       "room": (0, .6, .2, .25, .5, .7), "hall": (25, 2.2, .3, .8, .45, 1), "church": (40, 5, .4, 1, .65, 1)}
KEYS = ["predelay", "decay", "mix", "size", "damp", "width"]

def check(cond, msg):
    global OK
    print(("  OK   " if cond else "  ÉCHEC ") + msg)
    OK &= bool(cond)

def params(mode, over):
    d = dict(zip(KEYS, DEF[mode])); d.update(over); d["mode"] = over.get("mode", mode); return d

def run_both(fs, x, mode, over={}, block=128, changes=()):
    p0 = params(mode, over)
    lines = [(0, p0)] + [(a, {**p0, **c}) for a, c in changes]
    inp = os.path.join(TMP, "in.f32"); x.astype(np.float32).tofile(inp)
    pj, pt = os.path.join(TMP, "p.json"), os.path.join(TMP, "p.txt")
    json.dump({"p": p0, "changes": [[a, q] for a, q in lines[1:]]}, open(pj, "w"))
    with open(pt, "w") as f:
        for a, q in lines:
            f.write("%d %d %r %r %r %r %r %r\n" % (a, MODES.index(q["mode"]), *[float(q[k]) for k in KEYS]))
    oj, oc = os.path.join(TMP, "oj.f32"), os.path.join(TMP, "oc.f32")
    subprocess.run(["node", os.path.join(HERE, "ref_run.js"), str(fs), pj, inp, oj, "128"], check=True)
    subprocess.run([os.path.join(HERE, "core_run"), str(fs), pt, inp, oc, str(block)], check=True)
    return np.fromfile(oj, np.float32).reshape(-1, 2), np.fromfile(oc, np.float32).reshape(-1, 2)

def music(fs, sec=3.0):
    n = int(fs * sec); t = np.arange(n) / fs; L = np.zeros(n); R = np.zeros(n)
    for k in range(int(sec / 0.5)):
        s = int(k * 0.5 * fs); m = min(n - s, int(0.25 * fs)); tt = np.arange(m) / fs
        kick = np.sin(2 * np.pi * (50 + 120 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 9) * 0.9
        L[s:s + m] += kick; R[s:s + m] += kick
        if k % 2:
            sn = rng.standard_normal(m) * np.exp(-tt * 18) * 0.5
            L[s:s + m] += sn; R[s:s + m] += 0.8 * sn
    voice = 0.2 * np.sin(2 * np.pi * 220 * t + 3 * np.sin(2 * np.pi * 5 * t)) * (np.sin(2 * np.pi * 0.7 * t) > -0.3)
    L += voice; R += 0.6 * voice
    L[int(2.2 * fs):] = 0; R[int(2.2 * fs):] = 0          # silence final : on compare aussi la queue
    return np.stack([L, R], 1).astype(np.float32)

def impulse(fs, sec):
    x = np.zeros((int(fs * sec), 2), np.float32); x[0] = 1; return x

def rt60_500(o, fs):
    """RT60 à 500 Hz (bande d'octave, intégration de Schroeder, pente entre -5 et -35 dB)."""
    from numpy.fft import rfft, irfft
    n = len(o); f = np.fft.rfftfreq(2 * n, 1 / fs)
    H = np.exp(-0.5 * (np.log2(np.maximum(f, 1e-9) / 500) / 0.42) ** 2)
    e = np.zeros(n)
    for c in range(2): e += irfft(rfft(o[:, c], 2 * n) * H, 2 * n)[:n] ** 2
    cum = np.cumsum(e[::-1])[::-1]; db = 10 * np.log10(np.maximum(cum / cum[0], 1e-30))
    i5 = np.argmax(db <= -5); iE = np.argmax(db <= -35)
    if iE <= i5: iE = np.argmax(db <= -25)
    t = np.arange(i5, iE) / fs; sl = np.polyfit(t, db[i5:iE], 1)[0]
    return -60 / sl

print("\n0) Copie du prototype utilisée comme référence")
check(hashlib.sha256(open(os.path.join(HERE, "reverb_ref.js"), "rb").read()).hexdigest() == REF_SHA,
      "reverb_ref.js = moteur du Labo Reverb (empreinte SHA-256)")

print("\n1) Plugin (C++) et prototype (JavaScript) : même son")
worst, total, exact = 0, 0, 0
for fs in (44100, 48000, 96000):
    xm, xi = music(fs), impulse(fs, 2.5)
    for m in MODES:
        for name, over in (("défaut", {}), ("variante", {"predelay": 73, "decay": 3.7, "mix": 0.8, "size": 0.15, "damp": 0.9, "width": 0.35})):
            for sig, x in (("impulsion", xi), ("musique", xm)):
                oj, oc = run_both(fs, x, m, over)
                d = float(np.max(np.abs(oj - oc))); worst = max(worst, d)
                total += oj.size; exact += int(np.sum(oj == oc))
check(worst < 1e-6, "6 modes x 2 réglages x 2 signaux x 3 fréquences (44,1 / 48 / 96 kHz) : écart max %.1e, %.4f %% des échantillons identiques au bit près"
      % (worst, 100.0 * exact / total))

print("\n2) Taille des blocs du logiciel hôte")
x = music(48000)
same = True
for m in ("hall", "spring"):
    for b in (1, 64, 300, 4096):
        oj, oc = run_both(48000, x, m, {"mix": 0.5}, block=b)
        same &= bool(np.array_equal(oj, oc))
check(same, "blocs de 1, 64, 300 et 4096 échantillons : identique au prototype")

print("\n3) Réglages qui bougent pendant la lecture (fondu de changement de mode et de Size)")
# (instants multiples de 512 : la référence travaille par blocs de 128, le plugin ici par blocs de 256)
ch = [(48128, {"decay": 4.0}), (60416, {"size": 0.9}), (96256, {"mode": "plate"}), (110080, {"mix": 0.1, "width": 0.2}), (120320, {"mode": "church", "damp": 0.1})]
oj, oc = run_both(48000, x, "room", {}, block=256, changes=ch)
check(np.array_equal(oj, oc), "changements de Decay, Size, mode, Mix et Width en cours de route : identique au prototype")

print("\n4) RT60 mesuré à 500 Hz dans le plugin (valeurs par défaut de la maquette)")
for m in MODES:
    T = DEF[m][1]; fs = 48000
    _, oc = run_both(fs, impulse(fs, min(14, T * 1.4 + 1)), m, {"mix": 1})
    rt = rt60_500(oc, fs); err = (rt - T) / T * 100
    check(abs(err) < 10, "%-8s Decay %.1f s → RT60 mesuré %.2f s (%+.0f %%)" % (m, T, rt, err))

print("\n5) Vitesse de calcul (une voie stéréo, 96 kHz)")
x = music(96000, 10.0)
pt = os.path.join(TMP, "p.txt")
for m in MODES:
    with open(pt, "w") as f: f.write("0 %d %r %r %r %r %r %r\n" % (MODES.index(m), *[float(v) for v in DEF[m]]))
    x.tofile(os.path.join(TMP, "in.f32"))
    t0 = time.time(); subprocess.run([os.path.join(HERE, "core_run"), "96000", pt, os.path.join(TMP, "in.f32"), os.path.join(TMP, "oc.f32"), "512"], check=True)
    speed = 10.0 / (time.time() - t0)
    check(speed > 10, "%-8s %4.0f fois plus rapide que le temps réel" % (m, speed))

print("\nRÉSULTAT : " + ("tout est conforme" if OK else "ÉCHECS ci-dessus"))
raise SystemExit(0 if OK else 1)
