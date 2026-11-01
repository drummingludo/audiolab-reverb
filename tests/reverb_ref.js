/* ============ Moteur DSP (portable en C++) ============ */
class PlateCore {
  constructor(sr) {
    this.sr = sr;
    this.k = sr / 29761;
    // 0 pré-délai, 1-4 diffuseurs, 5 passe-tout modulé G, 6 délai G1, 7 passe-tout G, 8 délai G2,
    // 9 passe-tout modulé D, 10 délai D1, 11 passe-tout D, 12 délai D2
    this.base = [0, 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163];
    this.tapBase = [266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121];
    const p2 = (n) => { let s = 1; while (s < n) s <<= 1; return s; };
    this.B = []; this.M = new Int32Array(13); this.W = new Int32Array(13); this.D = new Int32Array(13);
    for (let i = 0; i < 13; i++) {
      const need = i === 0 ? Math.ceil(sr * 0.26) + 4 : Math.ceil((this.base[i] * 1.6 + 48) * this.k) + 4;
      const s = p2(need);
      this.B.push(new Float32Array(s)); this.M[i] = s - 1;
    }
    this.T = new Int32Array(14);
    this.bwS = 0; this.dpL = 0; this.dpR = 0; this.ph = 0;
    this.p = { predelay: 0, decay: 0.5, size: 1, damping: 20000, bandwidth: 20000, in1: 0.75, in2: 0.625,
               dd1: 0.7, dd2: 0.5, dd2Auto: true, rate: 1, depth: 16, mix: 1, out: 0 };
    this.setParams({});
  }
  setParams(o) {
    const p = Object.assign(this.p, o), sr = this.sr, s = this.k * p.size;
    for (let i = 1; i < 13; i++) this.D[i] = Math.max(1, Math.round(this.base[i] * s));
    this.D[0] = Math.max(0, Math.min(this.M[0] - 1, Math.round(p.predelay * 0.001 * sr)));
    for (let i = 0; i < 14; i++) this.T[i] = Math.max(1, Math.round(this.tapBase[i] * s));
    this.bw = p.bandwidth >= 19990 ? 0.9995 : 1 - Math.exp(-2 * Math.PI * p.bandwidth / sr);
    this.damp = p.damping >= 19990 ? 0.0005 : Math.exp(-2 * Math.PI * p.damping / sr);
    this.dd2v = p.dd2Auto ? Math.min(0.5, Math.max(0.25, p.decay + 0.15)) : p.dd2;
    this.exc = p.depth * this.k;
    this.dph = 2 * Math.PI * p.rate / sr;
    this.gain = Math.pow(10, p.out / 20);
  }
  reset() {
    for (const b of this.B) b.fill(0);
    this.bwS = 0; this.dpL = 0; this.dpR = 0;
  }
  process(inL, inR, outL, outR, n) {
    const B = this.B, M = this.M, W = this.W, D = this.D, T = this.T, p = this.p;
    const b0 = B[0], b1 = B[1], b2 = B[2], b3 = B[3], b4 = B[4], b5 = B[5], b6 = B[6],
          b7 = B[7], b8 = B[8], b9 = B[9], b10 = B[10], b11 = B[11], b12 = B[12];
    const m0 = M[0], m1 = M[1], m2 = M[2], m3 = M[3], m4 = M[4], m5 = M[5], m6 = M[6],
          m7 = M[7], m8 = M[8], m9 = M[9], m10 = M[10], m11 = M[11], m12 = M[12];
    const L0 = D[0], L1 = D[1], L2 = D[2], L3 = D[3], L4 = D[4], L5 = D[5], L6 = D[6],
          L7 = D[7], L8 = D[8], L9 = D[9], L10 = D[10], L11 = D[11], L12 = D[12];
    let w0 = W[0], w1 = W[1], w2 = W[2], w3 = W[3], w4 = W[4], w5 = W[5], w6 = W[6],
        w7 = W[7], w8 = W[8], w9 = W[9], w10 = W[10], w11 = W[11], w12 = W[12];
    const bw = this.bw, damp = this.damp, decay = p.decay, i1 = p.in1, i2 = p.in2,
          g1 = -p.dd1, g2 = this.dd2v, exc = this.exc, dph = this.dph,
          mix = p.mix, dry = 1 - p.mix, gain = this.gain;
    let bwS = this.bwS, dpL = this.dpL, dpR = this.dpR, ph = this.ph;
    for (let i = 0; i < n; i++) {
      const xl = inL[i], xr = inR[i];
      // pré-délai et filtre de bande passante
      b0[w0] = (xl + xr) * 0.5;
      let v = b0[(w0 - L0) & m0];
      bwS += bw * (v - bwS); v = bwS;
      // 4 diffuseurs d'entrée (passe-tout en treillis)
      let d = b1[(w1 - L1) & m1]; let u = v - i1 * d; b1[w1] = u; v = d + i1 * u;
      d = b2[(w2 - L2) & m2]; u = v - i1 * d; b2[w2] = u; v = d + i1 * u;
      d = b3[(w3 - L3) & m3]; u = v - i2 * d; b3[w3] = u; v = d + i2 * u;
      d = b4[(w4 - L4) & m4]; u = v - i2 * d; b4[w4] = u; v = d + i2 * u;
      // retours croisés du réservoir
      const fbL = b8[(w8 - L8) & m8] * decay;
      const fbR = b12[(w12 - L12) & m12] * decay;
      // moitié gauche
      let x = v + fbR;
      let df = L5 + exc * Math.sin(ph); let k0 = Math.floor(df); let fr = df - k0;
      d = b5[(w5 - k0) & m5] * (1 - fr) + b5[(w5 - k0 - 1) & m5] * fr;
      u = x - g1 * d; b5[w5] = u; x = d + g1 * u;
      b6[w6] = x; x = b6[(w6 - L6) & m6];
      dpL = x + damp * (dpL - x); x = dpL * decay;
      d = b7[(w7 - L7) & m7]; u = x - g2 * d; b7[w7] = u; x = d + g2 * u;
      b8[w8] = x;
      // moitié droite
      let y = v + fbL;
      df = L9 + exc * Math.cos(ph); k0 = Math.floor(df); fr = df - k0;
      d = b9[(w9 - k0) & m9] * (1 - fr) + b9[(w9 - k0 - 1) & m9] * fr;
      u = y - g1 * d; b9[w9] = u; y = d + g1 * u;
      b10[w10] = y; y = b10[(w10 - L10) & m10];
      dpR = y + damp * (dpR - y); y = dpR * decay;
      d = b11[(w11 - L11) & m11]; u = y - g2 * d; b11[w11] = u; y = d + g2 * u;
      b12[w12] = y;
      // prises de sortie (tableau 2)
      const yl = 0.6 * (b10[(w10 - T[0]) & m10] + b10[(w10 - T[1]) & m10] - b11[(w11 - T[2]) & m11]
               + b12[(w12 - T[3]) & m12] - b6[(w6 - T[4]) & m6] - b7[(w7 - T[5]) & m7] - b8[(w8 - T[6]) & m8]);
      const yr = 0.6 * (b6[(w6 - T[7]) & m6] + b6[(w6 - T[8]) & m6] - b7[(w7 - T[9]) & m7]
               + b8[(w8 - T[10]) & m8] - b10[(w10 - T[11]) & m10] - b11[(w11 - T[12]) & m11] - b12[(w12 - T[13]) & m12]);
      outL[i] = (xl * dry + yl * mix) * gain;
      outR[i] = (xr * dry + yr * mix) * gain;
      ph += dph; if (ph > 6.283185307179586) ph -= 6.283185307179586;
      w0 = (w0 + 1) & m0; w1 = (w1 + 1) & m1; w2 = (w2 + 1) & m2; w3 = (w3 + 1) & m3; w4 = (w4 + 1) & m4;
      w5 = (w5 + 1) & m5; w6 = (w6 + 1) & m6; w7 = (w7 + 1) & m7; w8 = (w8 + 1) & m8; w9 = (w9 + 1) & m9;
      w10 = (w10 + 1) & m10; w11 = (w11 + 1) & m11; w12 = (w12 + 1) & m12;
    }
    W[0] = w0; W[1] = w1; W[2] = w2; W[3] = w3; W[4] = w4; W[5] = w5; W[6] = w6;
    W[7] = w7; W[8] = w8; W[9] = w9; W[10] = w10; W[11] = w11; W[12] = w12;
    this.bwS = bwS; this.dpL = dpL; this.dpR = dpR; this.ph = ph;
    if (!(Math.abs(dpL) < 1e6) || !(Math.abs(dpR) < 1e6)) this.reset();
  }
}

/* ============ Outils communs ============ */
class DSP {
  static rng(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }
  static p2(n) { let s = 1; while (s < n) s <<= 1; return s; }
  static isPrime(n) { if (n < 2) return false; if (n % 2 === 0) return n === 2; for (let i = 3; i * i <= n; i += 2) if (n % i === 0) return false; return true; }
  static prime(n, used) { n = Math.max(3, Math.round(n)); while (!DSP.isPrime(n) || used.has(n)) n++; used.add(n); return n; }
  // Hadamard 8x8 rapide, normalisée (matrice orthogonale)
  static had8(a) {
    for (let h = 1; h < 8; h <<= 1) for (let i = 0; i < 8; i += h << 1) for (let j = i; j < i + h; j++) { const x = a[j], y = a[j + h]; a[j] = x + y; a[j + h] = x - y; }
    for (let i = 0; i < 8; i++) a[i] *= 0.35355339059327373;
  }
  // Étagères du 1er ordre (transformée bilinéaire). Coefs [b0, b1, a1] pour y = b0 x + b1 x1 - a1 y1
  static shelfLo(G, fc, sr, out, o) { const K = Math.tan(Math.PI * Math.min(fc, sr * 0.45) / sr), a0 = K + 1; out[o] = (K * G + 1) / a0; out[o + 1] = (K * G - 1) / a0; out[o + 2] = (K - 1) / a0; }
  static shelfHi(G, fc, sr, out, o) { const K = Math.tan(Math.PI * Math.min(fc, sr * 0.45) / sr), a0 = 1 + K; out[o] = (G + K) / a0; out[o + 1] = (K - G) / a0; out[o + 2] = (K - 1) / a0; }
  // Module d'un filtre du 1er ordre [b0, b1, a1] à la fréquence f
  static mag1(c, o, f, sr) {
    const w = 2 * Math.PI * f / sr, cw = Math.cos(w), b0 = c[o], b1 = c[o + 1], a1 = c[o + 2];
    return Math.sqrt((b0 * b0 + b1 * b1 + 2 * b0 * b1 * cw) / (1 + a1 * a1 + 2 * a1 * cw));
  }
  static onePole(fc, sr) { return 1 - Math.exp(-2 * Math.PI * Math.min(fc, sr * 0.45) / sr); }
  // Size 0..1 -> facteur d'échelle 0,66..1,52
  static scale(size) { return Math.pow(2, (size - 0.5) * 1.2); }
  // Méthode des sources-images (Allen & Berkley 1979) sur une pièce parallélépipédique
  static images(dims, refl, maxOrder, win, maxTaps) {
    const Lx = dims[0], Ly = dims[1], Lz = dims[2], c = 343;
    const S = [0.56 * Lx, 0.3 * Ly, Math.min(2.0, 0.5 * Lz)], R = [0.43 * Lx, 0.64 * Ly, Math.min(1.6, 0.45 * Lz)];
    const d0 = Math.hypot(S[0] - R[0], S[1] - R[1], S[2] - R[2]), N = maxOrder, out = [];
    for (let nx = -N; nx <= N; nx++) for (let qx = 0; qx < 2; qx++) {
      const ox = Math.abs(2 * nx - qx); if (ox > N) continue;
      const X = 2 * nx * Lx + (qx ? -S[0] : S[0]);
      for (let ny = -N; ny <= N; ny++) for (let qy = 0; qy < 2; qy++) {
        const oy = Math.abs(2 * ny - qy); if (ox + oy > N) continue;
        const Y = 2 * ny * Ly + (qy ? -S[1] : S[1]);
        for (let nz = -N; nz <= N; nz++) for (let qz = 0; qz < 2; qz++) {
          const oz = Math.abs(2 * nz - qz), ord = ox + oy + oz; if (ord === 0 || ord > N) continue;
          const Z = 2 * nz * Lz + (qz ? -S[2] : S[2]);
          const d = Math.hypot(X - R[0], Y - R[1], Z - R[2]), t = (d - d0) / c;
          if (t <= 0.0005 || t > win) continue;
          const dx = X - R[0], dy = Y - R[1];
          out.push({ t, g: (d0 / d) * Math.pow(refl, ord), p: dx / Math.max(1e-9, Math.hypot(dx, dy)), ord });
        }
      }
    }
    out.sort((a, b) => b.g - a.g);
    const keep = out.slice(0, maxTaps); keep.sort((a, b) => a.t - b.t);
    return keep;
  }
}

/* ============ Moteur pièces : réflexions précoces + diffuseur + FDN (Chamber, Room, Hall, Church) ============ */
class RoomCore {
  constructor(sr) {
    this.sr = sr;
    this.LN = DSP.p2(Math.ceil(0.32 * sr)); this.lm = this.LN - 1; this.lw = 0; this.lines = [];
    for (let j = 0; j < 8; j++) this.lines.push(new Float32Array(this.LN));
    this.len = new Int32Array(8); this.cf = new Float64Array(56); this.st = new Float64Array(32);
    this.DN = DSP.p2(Math.ceil(0.16 * sr)); this.dm = this.DN - 1; this.dw = 0; this.dif = [];
    for (let k = 0; k < 32; k++) this.dif.push(new Float32Array(this.DN));
    this.dlen = new Int32Array(32); this.dsg = new Float32Array(32); this.dperm = new Int32Array(32); this.steps = 4;
    this.EN = DSP.p2(Math.ceil(0.3 * sr)); this.em = this.EN - 1; this.ew = 0; this.eb = new Float32Array(this.EN);
    this.tT = new Int32Array(64); this.tL = new Float32Array(64); this.tR = new Float32Array(64); this.nT = 0;
    this.ph = new Float64Array(8); this.dph = new Float64Array(8); this.depth = 0;
    this.c = new Float64Array(8); this.tmp = new Float64Array(8); this.o = new Float64Array(8);
    this.eA = 1; this.eL = 0; this.eR = 0; this.hpA = 0; this.lpA = 1; this.f = new Float64Array(4);
    this.lateG = 1; this.difG = 0.5; this.taps = []; this.info = {};
  }
  reset() {
    for (const b of this.lines) b.fill(0);
    for (const b of this.dif) b.fill(0);
    this.eb.fill(0); this.st.fill(0); this.f.fill(0); this.eL = 0; this.eR = 0;
  }
  configure(def, p) {
    const sr = this.sr, s = DSP.scale(p.size), dims = def.dims.map((v) => v * s);
    const V = dims[0] * dims[1] * dims[2], S = 2 * (dims[0] * dims[1] + dims[0] * dims[2] + dims[1] * dims[2]), mfp = 4 * V / S;
    // Réflexions précoces : fenêtre liée au volume (temps de mélange ~ racine du volume, Lindau et al. 2012)
    const win = Math.min(0.2, Math.max(0.03, (Math.sqrt(V) * 1.3 + 20) / 1000));
    this.im = DSP.images(dims, 0.92, def.order, win, 48);
    this.nT = this.im.length;
    this.im.forEach((t, k) => { this.tT[k] = Math.max(1, Math.round(t.t * sr)); });
    // Diffuseur : étapes de délais + inversions de polarité + permutation + Hadamard (Signalsmith 2021)
    const rnd = DSP.rng(def.seed); this.steps = def.diff.length;
    let dsum = 0;
    for (let st = 0; st < this.steps; st++) {
      const D = def.diff[st] * 0.001 * s * sr, perm = [0, 1, 2, 3, 4, 5, 6, 7]; dsum += D;
      for (let j = 7; j > 0; j--) { const r = Math.floor(rnd() * (j + 1)); const t = perm[j]; perm[j] = perm[r]; perm[r] = t; }
      for (let j = 0; j < 8; j++) {
        const k = st * 8 + j;
        this.dlen[k] = Math.max(1, Math.floor(D * (j + rnd()) / 8));
        this.dsg[k] = rnd() < 0.5 ? -1 : 1; this.dperm[k] = perm[j];
      }
    }
    // FDN 8 lignes : moyenne >= libre parcours moyen 4V/S (Jot), étalement x0,71..x1,41, longueurs premières
    const mean = Math.max(mfp / 343, def.minMean * s), used = new Set(), order = [0, 5, 2, 7, 4, 1, 6, 3];
    for (let j = 0; j < 8; j++) this.len[order[j]] = DSP.prime(mean * Math.pow(2, j / 7 - 0.5) * sr, used);
    this.depth = def.mod[0] * 0.001 * sr;
    for (let j = 0; j < 8; j++) { this.dph[j] = 2 * Math.PI * def.mod[1] * (1 + 0.137 * j) / sr; this.ph[j] = j * 0.9; }
    this.hpA = def.hp ? DSP.onePole(def.hp, sr) : 0; this.lpA = def.lp ? DSP.onePole(def.lp, sr) : 1;
    this.geo = { V, S, win, mean, dsum: dsum / sr };
    this.info = { dims, V, S, mfp, win, nT: this.im.length, mean,
      delaysMs: Array.from(this.len).map((v) => v / sr * 1000).sort((a, b) => a - b),
      diffMs: def.diff.map((v) => v * s) };
  }
  setDecay(def, p) {
    const sr = this.sr, T = Math.max(0.05, p.decay), hi = Math.max(0.2, 0.95 - 0.75 * p.damp), G = this.geo;
    // Filtres d'absorption par ligne (Jot) : gain médium + étagère grave + étagère aiguë
    for (let j = 0; j < 8; j++) {
      const L = this.len[j], gm = Math.pow(10, -3 * L / (T * sr)),
            glo = Math.pow(10, -3 * L / (T * def.bass * sr)), ghi = Math.pow(10, -3 * L / (T * hi * sr)), o = j * 7;
      DSP.shelfLo(glo / gm, def.fLo, sr, this.cf, o + 1); DSP.shelfHi(ghi / gm, def.fHi, sr, this.cf, o + 4);
      // les étagères débordent un peu sur le médium : on recale le gain pour que le RT60 vers 600 Hz soit exact
      const comp = DSP.mag1(this.cf, o + 1, 600, sr) * DSP.mag1(this.cf, o + 4, 600, sr);
      this.cf[o] = Math.min(gm / comp, 0.9995 / Math.max(1, glo / gm));
    }
    // Réflexions : coefficient des parois déduit du RT60 (Eyring), puis niveaux calés sur une seule enveloppe exponentielle
    const refl = Math.min(0.985, Math.exp(-0.0805 * G.V / (G.S * T)));
    let e = 0; const raw = this.im.map((t) => { const g = t.g * Math.pow(refl / 0.92, t.ord); e += g * g; return g; });
    const norm = def.er / Math.sqrt(e || 1);
    const D0 = def.er * def.er / (sr * T / 13.82 * (1 - Math.exp(-13.82 * G.win / T)));
    this.im.forEach((t, k) => { const g = raw[k] * norm; this.tL[k] = g * Math.sqrt(1 - t.p); this.tR[k] = g * Math.sqrt(1 + t.p); });
    this.taps = this.im.map((t, k) => ({ t: t.t, g: raw[k] * norm }));
    const t0 = 0.5 * G.dsum + 0.8 * G.mean, tdc = 0.5 * G.dsum;
    this.lateG = def.late * Math.sqrt(8 * G.mean * sr * D0) * Math.exp(-6.91 * t0 / T);
    this.difG = def.difOut * Math.sqrt(D0 * Math.max(1, G.dsum * sr)) * Math.exp(-6.91 * tdc / T);
    this.eA = DSP.onePole(16000 * (1 - 0.7 * p.damp), sr);
    this.hiRatio = hi; this.refl = refl;
  }
  process(x, oL, oR, n) {
    const lines = this.lines, lm = this.lm, len = this.len, cf = this.cf, st = this.st, dif = this.dif, dm = this.dm,
          dlen = this.dlen, dsg = this.dsg, dperm = this.dperm, steps = this.steps, eb = this.eb, em = this.em,
          tT = this.tT, tL = this.tL, tR = this.tR, nT = this.nT, c = this.c, tmp = this.tmp, o = this.o,
          ph = this.ph, dph = this.dph, depth = this.depth, eA = this.eA, lateG = this.lateG * 0.35355339059327373,
          difG = this.difG * 0.35355339059327373, hpA = this.hpA, lpA = this.lpA, f = this.f, TWO_PI = 6.283185307179586;
    let lw = this.lw, dw = this.dw, ew = this.ew, eL = this.eL, eR = this.eR;
    for (let i = 0; i < n; i++) {
      const xi = x[i];
      // réflexions précoces (sources-images)
      eb[ew] = xi; let el = 0, er = 0;
      for (let k = 0; k < nT; k++) { const v = eb[(ew - tT[k]) & em]; el += v * tL[k]; er += v * tR[k]; }
      eL += eA * (el - eL); eR += eA * (er - eR);
      ew = (ew + 1) & em;
      // diffuseur
      for (let j = 0; j < 8; j++) c[j] = xi;
      for (let s = 0; s < steps; s++) {
        const b = s * 8;
        for (let j = 0; j < 8; j++) { const buf = dif[b + j]; buf[dw] = c[j]; tmp[j] = buf[(dw - dlen[b + j]) & dm] * dsg[b + j]; }
        for (let j = 0; j < 8; j++) c[j] = tmp[dperm[b + j]];
        DSP.had8(c);
      }
      dw = (dw + 1) & dm;
      // FDN : lecture, filtres d'absorption (Jot), matrice de Householder
      let sum = 0, yl = 0, yr = 0;
      for (let j = 0; j < 8; j++) {
        const b = lines[j]; let v;
        if (depth > 0 && (j & 1)) {
          const df = len[j] + depth * Math.sin(ph[j]), k0 = Math.floor(df), fr = df - k0;
          v = b[(lw - k0) & lm] * (1 - fr) + b[(lw - k0 - 1) & lm] * fr;
          ph[j] += dph[j]; if (ph[j] > TWO_PI) ph[j] -= TWO_PI;
        } else v = b[(lw - len[j]) & lm];
        const q = j * 7, r = j * 4;
        const y1 = cf[q + 1] * v + cf[q + 2] * st[r] - cf[q + 3] * st[r + 1]; st[r] = v; st[r + 1] = y1;
        const y2 = cf[q + 4] * y1 + cf[q + 5] * st[r + 2] - cf[q + 6] * st[r + 3]; st[r + 2] = y1; st[r + 3] = y2;
        v = y2 * cf[q]; o[j] = v; sum += v;
        yl += (j & 1) ? -v : v; yr += (j & 2) ? -v : v;
      }
      const hs = sum * 0.25;
      for (let j = 0; j < 8; j++) lines[j][lw] = o[j] - hs + c[j] * 0.35355339059327373;
      lw = (lw + 1) & lm;
      const dl = c[0] - c[1] + c[2] - c[3] + c[4] - c[5] + c[6] - c[7], dr = c[0] + c[1] - c[2] - c[3] + c[4] + c[5] - c[6] - c[7];
      let L = eL + lateG * yl + difG * dl, R = eR + lateG * yr + difG * dr;
      if (hpA) { f[0] += hpA * (L - f[0]); f[1] += hpA * (R - f[1]); L -= f[0]; R -= f[1]; }
      if (lpA < 1) { f[2] += lpA * (L - f[2]); f[3] += lpA * (R - f[3]); L = f[2]; R = f[3]; }
      oL[i] = L; oR[i] = R;
    }
    this.lw = lw; this.dw = dw; this.ew = ew; this.eL = eL; this.eR = eR;
    if (!(Math.abs(o[0]) < 1e6)) this.reset();
  }
}

/* ============ Moteur ressort : chaîne de passe-tout étirés dans une boucle (Välimäki, Parker, Abel 2010) ============ */
class SpringCore {
  constructor(sr) {
    this.sr = sr; this.M = 100; this.a = 0.6; this.k = Math.max(1, Math.round(sr / (2 * 4400))); this.fC = sr / (2 * this.k);
    this.MH = 40; this.ah = -0.6;
    this.DN = DSP.p2(Math.ceil(0.12 * sr)); this.dm = this.DN - 1; this.w = 0; this.ki = 0;
    this.s = [0, 1].map((i) => ({ ap: new Float32Array(this.M * this.k), aph: new Float32Array(this.MH),
      dl: new Float32Array(this.DN), dh: new Float32Array(this.DN), fb: 0, fbh: 0, lp: 0, z1: 0, z2: 0, hp: 0,
      mod: 0, modT: 0, cnt: i * 4000, Td: 0, Th: 0, g: 0, gh: 0, r: DSP.rng(91 + i * 17) }));
    // passe-bas de sortie à fC (biquad RBJ, Q 0,707)
    const w0 = 2 * Math.PI * Math.min(this.fC * 0.95, sr * 0.45) / sr, al = Math.sin(w0) / (2 * 0.7071), cs = Math.cos(w0), a0 = 1 + al;
    this.lb = [(1 - cs) / 2 / a0, (1 - cs) / a0, (1 - cs) / 2 / a0, -2 * cs / a0, (1 - al) / a0];
    this.hpA = DSP.onePole(90, sr); this.dc = 0.2; this.taps = []; this.info = {};
  }
  reset() { for (const S of this.s) { S.ap.fill(0); S.aph.fill(0); S.dl.fill(0); S.dh.fill(0); S.fb = 0; S.fbh = 0; S.lp = 0; S.z1 = 0; S.z2 = 0; S.hp = 0; } }
  gd(a, k, f) { const w = 2 * Math.PI * f / this.sr; return k * (1 - a * a) / (1 + 2 * a * Math.cos(k * w) + a * a); }
  configure(def, p) {
    const sr = this.sr, Td = (0.03 + 0.04 * p.size) * sr, ratio = [1, 47.3 / 41], ratioH = [1, 21.7 / 17.9];
    this.s.forEach((S, i) => { S.Td = Td * ratio[i]; S.Th = Td * 0.437 * ratioH[i]; });
    this.taps = []; const chain = this.M * this.gd(this.a, this.k, 707) / sr;
    for (let r = 1; r <= 8; r++) this.taps.push({ t: r * (Td / sr + chain), g: Math.pow(0.72, r - 1) });
    this.info = { Td: Td / sr * 1000, chain: chain * 1000, k: this.k, fC: this.fC };
  }
  setDecay(def, p) {
    const sr = this.sr, T = Math.max(0.05, p.decay), gl = this.M * this.gd(this.a, this.k, 707), gh = this.MH * this.gd(this.ah, 1, 707);
    this.dc = Math.exp(-2 * Math.PI * Math.min(9000 * Math.pow(0.3, p.damp), sr * 0.45) / sr);
    const w = 2 * Math.PI * 500 / sr, b = 1 - this.dc, lpMag = b / Math.sqrt(1 + this.dc * this.dc - 2 * this.dc * Math.cos(w));
    for (const S of this.s) {
      S.g = Math.min(0.999, Math.pow(10, -3 * (S.Td + gl) / (T * sr)) / lpMag);
      S.gh = Math.pow(10, -3 * (S.Th + gh) / (T * 0.6 * sr));
    }
  }
  process(x, oL, oR, n) {
    const M = this.M, k = this.k, a = this.a, MH = this.MH, ah = this.ah, dm = this.dm, dc = this.dc, lb = this.lb, hpA = this.hpA;
    let w = this.w, ki = this.ki;
    for (let i = 0; i < n; i++) {
      const xi = x[i];
      for (let sp = 0; sp < 2; sp++) {
        const S = this.s[sp], ap = S.ap;
        // boucle basse fréquence : 100 passe-tout étirés (dispersion = chirps)
        let v = xi + S.fb;
        for (let m = 0, q = ki; m < M; m++, q += k) { const d = ap[q], u = v - a * d; ap[q] = u; v = d + a * u; }
        S.lp = v + dc * (S.lp - v);
        S.dl[w] = S.lp;
        if (--S.cnt <= 0) { S.cnt = Math.round(this.sr * 0.23); S.modT = (S.r() * 2 - 1) * 8 * this.sr / 48000; }
        S.mod += 0.0004 * (S.modT - S.mod);
        const df = S.Td + S.mod, k0 = Math.floor(df), fr = df - k0;
        const y = S.dl[(w - k0) & dm] * (1 - fr) + S.dl[(w - k0 - 1) & dm] * fr;
        S.fb = y * S.g;
        // boucle haute fréquence : 40 passe-tout du 1er ordre
        let h = xi + S.fbh;
        const aph = S.aph;
        for (let m = 0; m < MH; m++) { const d = aph[m], u = h - ah * d; aph[m] = u; h = d + ah * u; }
        S.dh[w] = h;
        const yh = S.dh[(w - Math.round(S.Th)) & dm]; S.fbh = yh * S.gh;
        // sortie : passe-bas à fC + voie aiguë à -14 dB, passe-haut 90 Hz
        const yo = lb[0] * y + S.z1; S.z1 = lb[1] * y - lb[3] * yo + S.z2; S.z2 = lb[2] * y - lb[4] * yo;
        let out = yo + 0.2 * yh;
        S.hp += hpA * (out - S.hp); out -= S.hp;
        if (sp === 0) oL[i] = out; else oR[i] = out;
      }
      w = (w + 1) & dm; ki = ki + 1 === k ? 0 : ki + 1;
    }
    this.w = w; this.ki = ki;
    if (!(Math.abs(this.s[0].fb) < 1e6)) this.reset();
  }
}

/* ============ Moteur complet du plug-in ============ */
class ReverbEngine {
  constructor(sr, modes) {
    this.sr = sr; this.modes = modes;
    this.room = new RoomCore(sr); this.plate = new PlateCore(sr); this.spring = new SpringCore(sr);
    this.PN = DSP.p2(Math.ceil(0.26 * sr)); this.pm = this.PN - 1; this.pw = 0; this.pb = new Float32Array(this.PN);
    this.x = new Float32Array(128); this.wl = new Float32Array(128); this.wr = new Float32Array(128);
    this.p = Object.assign({ mode: 'chamber' }, modes.chamber.defaults);
    this.active = ''; this.sSize = -1; this.pend = false; this.fade = 1; this.fStep = 1 / (0.012 * sr);
    this.mixS = this.p.mix; this.widS = this.p.width;
    this.applyStruct();
  }
  core() { const e = this.modes[this.p.mode].engine; return e === 'room' ? this.room : e === 'plate' ? this.plate : this.spring; }
  setParams(o) {
    Object.assign(this.p, o);
    if (this.p.mode !== this.active || this.p.size !== this.sSize) this.pend = true; else this.applyTone();
  }
  snap() { this.applyStruct(); this.pend = false; this.fade = 1; this.mixS = this.p.mix; this.widS = this.p.width; }
  applyStruct() {
    const p = this.p, def = this.modes[p.mode], changed = p.mode !== this.active;
    if (def.engine === 'room') { this.room.configure(def, p); if (changed) this.room.reset(); }
    else if (def.engine === 'spring') { this.spring.configure(def, p); if (changed) this.spring.reset(); }
    else { this.plate.setParams({ size: 0.6 + 0.8 * p.size }); if (changed) this.plate.reset(); }
    this.active = p.mode; this.sSize = p.size;
    this.applyTone();
  }
  applyTone() {
    const p = this.p, def = this.modes[p.mode];
    if (def.engine === 'room') this.room.setDecay(def, p);
    else if (def.engine === 'spring') this.spring.setDecay(def, p);
    else {
      // RT60 théorique de la boucle Dattorro, corrigé d'après mesure (la plaque sonne ~5 à 15 % plus long que la formule)
      const ps = 0.6 + 0.8 * p.size, half = 10794.5 / 29761 * ps, T = Math.max(0.05, p.decay), Tc = T / (1 + 0.125 / Math.sqrt(T));
      this.plate.setParams({ decay: Math.min(0.9995, Math.pow(10, -1.5 * half / Tc)),
        damping: p.damp < 0.005 ? 20000 : 20000 * Math.pow(0.15, p.damp), bandwidth: 20000, predelay: 0, mix: 1, out: 0, size: ps });
    }
    this.gain = def.gain;
  }
  info() { const e = this.modes[this.p.mode].engine; return e === 'plate' ? null : this.core().info; }
  taps() { const e = this.modes[this.p.mode].engine; return e === 'plate' ? [] : this.core().taps; }
  process(inL, inR, outL, outR, n) {
    const sr = this.sr, pb = this.pb, pm = this.pm, x = this.x, wl = this.wl, wr = this.wr;
    for (let off = 0; off < n; off += 128) {
      const m = Math.min(128, n - off), D = Math.round(this.p.predelay * 0.001 * sr); // + 1e-24 : anti-dénormaux (FTZ en C++)
      let pw = this.pw;
      for (let i = 0; i < m; i++) { pb[pw] = 0.5 * (inL[off + i] + inR[off + i]); x[i] = pb[(pw - D) & pm] + 1e-24; pw = (pw + 1) & pm; }
      this.pw = pw;
      const e = this.modes[this.p.mode].engine;
      if (e === 'plate') this.plate.process(x, x, wl, wr, m); else this.core().process(x, wl, wr, m);
      const mixT = this.p.mix, widT = this.p.width;
      for (let i = 0; i < m; i++) {
        if (this.pend) { this.fade -= this.fStep; if (this.fade <= 0) { this.fade = 0; this.applyStruct(); this.pend = false; } }
        else if (this.fade < 1) { this.fade += this.fStep; if (this.fade > 1) this.fade = 1; }
        this.mixS += 0.002 * (mixT - this.mixS); this.widS += 0.002 * (widT - this.widS);
        const g = this.fade * this.gain;
        let L = wl[i] * g, R = wr[i] * g; const M = (L + R) * 0.5, S = (L - R) * 0.5 * this.widS;
        L = M + S; R = M - S;
        outL[off + i] = inL[off + i] * (1 - this.mixS) + L * this.mixS;
        outR[off + i] = inR[off + i] * (1 - this.mixS) + R * this.mixS;
      }
    }
  }
}

const MODES = {
  chamber: { label: 'Chamber', engine: 'room', defaults: { predelay: 0, decay: 1.5, mix: 0.30, size: 0.40, damp: 0.50, width: 0.80 },
    dims: [6.0, 4.2, 3.2], refl: 0.9, order: 3, er: 0.8, late: 1, difOut: 1, diff: [3, 6, 11, 19], bass: 1.0, fLo: 350, fHi: 3000,
    minMean: 0.03, mod: [0.06, 0.7], hp: 180, lp: 10000, seed: 23, gain: 0.75 },
  plate: { label: 'Plate', engine: 'plate', defaults: { predelay: 10, decay: 2.2, mix: 0.25, size: 0.60, damp: 0.30, width: 1.0 }, gain: 0.72 },
  spring: { label: 'Spring', engine: 'spring', defaults: { predelay: 0, decay: 2.0, mix: 0.35, size: 0.30, damp: 0.40, width: 0.50 }, gain: 1.23 },
  room: { label: 'Room', engine: 'room', defaults: { predelay: 0, decay: 0.6, mix: 0.20, size: 0.25, damp: 0.50, width: 0.70 },
    dims: [7.0, 5.5, 3.0], refl: 0.8, order: 3, er: 1.0, late: 1, difOut: 1, diff: [2.5, 5, 9, 16], bass: 1.1, fLo: 300, fHi: 5000,
    minMean: 0.014, mod: [0, 0], hp: 0, lp: 0, seed: 11, gain: 0.55 },
  hall: { label: 'Hall', engine: 'room', defaults: { predelay: 25, decay: 2.2, mix: 0.30, size: 0.80, damp: 0.45, width: 1.0 },
    dims: [26, 17, 12], refl: 0.85, order: 2, er: 0.55, late: 1, difOut: 1, diff: [8, 17, 33, 60], bass: 1.35, fLo: 250, fHi: 4000,
    minMean: 0.035, mod: [0.25, 0.6], hp: 0, lp: 0, seed: 37, gain: 0.95 },
  church: { label: 'Church', engine: 'room', defaults: { predelay: 40, decay: 5.0, mix: 0.40, size: 1.0, damp: 0.65, width: 1.0 },
    dims: [36, 16, 15], refl: 0.9, order: 2, er: 0.6, late: 1, difOut: 1, diff: [10, 24, 50], bass: 1.45, fLo: 220, fHi: 3000,
    minMean: 0.08, mod: [0.15, 0.4], hp: 0, lp: 0, seed: 53, gain: 0.96 },
};


