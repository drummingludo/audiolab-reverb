// Moteur audio d'Audio Lab Reverb : portage ligne à ligne du prototype « Labo Reverb » (JavaScript).
// Six modes : Chamber, Room, Hall, Church (réflexions par sources-images + diffuseur + FDN de Jot),
// Plate (réservoir de Dattorro, 1997) et Spring (boucles dispersives, Välimäki, Parker et Abel).
// Mêmes types que le JavaScript : mémoires de retard en float (Float32Array), calculs en double.
// Temps réel : aucune allocation hors du constructeur ; configure() et setDecay() travaillent sur des tableaux fixes.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace audiolab::rv
{
constexpr double PI = 3.141592653589793;

// Égalité exacte voulue (mêmes nombres que le JavaScript), sans avertissement du compilateur
inline bool same (double a, double b) { return std::equal_to<double>() (a, b); }

// Arrondi de JavaScript (Math.round : demi vers +∞)
inline double jsRound (double x) { return std::floor (x + 0.5); }

// Math.hypot de V8 (somme de Kahan normalisée), pour retrouver exactement les mêmes distances
inline double jsHypot (double a, double b, double c)
{
    const double v[3] = { std::abs (a), std::abs (b), std::abs (c) };
    double mx = 0; for (double x : v) if (x > mx) mx = x;
    if (same (mx, 0)) return 0;
    double sum = 0, comp = 0;
    for (double x : v) { const double n = x / mx, s = n * n - comp, pre = sum + s; comp = (pre - sum) - s; sum = pre; }
    return std::sqrt (sum) * mx;
}
inline double jsHypot (double a, double b)
{
    const double v[2] = { std::abs (a), std::abs (b) };
    double mx = 0; for (double x : v) if (x > mx) mx = x;
    if (same (mx, 0)) return 0;
    double sum = 0, comp = 0;
    for (double x : v) { const double n = x / mx, s = n * n - comp, pre = sum + s; comp = (pre - sum) - s; sum = pre; }
    return std::sqrt (sum) * mx;
}

// ── Définition des modes (valeurs du prototype) ─────────────────────────
enum Engine { roomEngine, plateEngine, springEngine };
struct Defaults { double predelay, decay, mix, size, damp, width; };
struct ModeDef
{
    const char* label; Engine engine; Defaults def;
    double dims[3]; double refl; int order; double er, late, difOut;
    double diff[4]; int nDiff; double bass, fLo, fHi, minMean; double mod[2]; double hp, lp; uint32_t seed; double gain;
};
enum Mode { chamber, plate, spring, room, hall, church, numModes };

inline const ModeDef& modeDef (int m)
{
    static const ModeDef M[numModes] = {
        { "Chamber", roomEngine, { 0, 1.5, 0.30, 0.40, 0.50, 0.80 }, { 6.0, 4.2, 3.2 }, 0.9, 3, 0.8, 1, 1, { 3, 6, 11, 19 }, 4,
          1.0, 350, 3000, 0.03, { 0.06, 0.7 }, 180, 10000, 23, 0.75 },
        { "Plate", plateEngine, { 10, 2.2, 0.25, 0.60, 0.30, 1.0 }, { 0, 0, 0 }, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0,
          0, 0, 0, 0, { 0, 0 }, 0, 0, 0, 0.72 },
        { "Spring", springEngine, { 0, 2.0, 0.35, 0.30, 0.40, 0.50 }, { 0, 0, 0 }, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0,
          0, 0, 0, 0, { 0, 0 }, 0, 0, 0, 1.23 },
        { "Room", roomEngine, { 0, 0.6, 0.20, 0.25, 0.50, 0.70 }, { 7.0, 5.5, 3.0 }, 0.8, 3, 1.0, 1, 1, { 2.5, 5, 9, 16 }, 4,
          1.1, 300, 5000, 0.014, { 0, 0 }, 0, 0, 11, 0.55 },
        { "Hall", roomEngine, { 25, 2.2, 0.30, 0.80, 0.45, 1.0 }, { 26, 17, 12 }, 0.85, 2, 0.55, 1, 1, { 8, 17, 33, 60 }, 4,
          1.35, 250, 4000, 0.035, { 0.25, 0.6 }, 0, 0, 37, 0.95 },
        { "Church", roomEngine, { 40, 5.0, 0.40, 1.0, 0.65, 1.0 }, { 36, 16, 15 }, 0.9, 2, 0.6, 1, 1, { 10, 24, 50, 0 }, 3,
          1.45, 220, 3000, 0.08, { 0.15, 0.4 }, 0, 0, 53, 0.96 },
    };
    return M[std::clamp (m, 0, (int) numModes - 1)];
}

// ── Outils communs ───────────────────────────────────────────────────────
struct Rng   // générateur congruentiel identique au JavaScript
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed) {}
    double next() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; }
};
inline int p2 (int n) { int s = 1; while (s < n) s <<= 1; return s; }
inline bool isPrime (int n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int i = 3; i * i <= n; i += 2) if (n % i == 0) return false;
    return true;
}
// Hadamard 8 x 8 rapide, normalisée (matrice orthogonale)
inline void had8 (double* a)
{
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h << 1)
            for (int j = i; j < i + h; ++j) { const double x = a[j], y = a[j + h]; a[j] = x + y; a[j + h] = x - y; }
    for (int i = 0; i < 8; ++i) a[i] *= 0.35355339059327373;
}
// Étagères du 1er ordre (transformée bilinéaire) : coefs { b0, b1, a1 } pour y = b0 x + b1 x1 - a1 y1
inline void shelfLo (double G, double fc, double sr, double* c)
{
    const double K = std::tan (PI * std::min (fc, sr * 0.45) / sr), a0 = K + 1;
    c[0] = (K * G + 1) / a0; c[1] = (K * G - 1) / a0; c[2] = (K - 1) / a0;
}
inline void shelfHi (double G, double fc, double sr, double* c)
{
    const double K = std::tan (PI * std::min (fc, sr * 0.45) / sr), a0 = 1 + K;
    c[0] = (G + K) / a0; c[1] = (K - G) / a0; c[2] = (K - 1) / a0;
}
inline double mag1 (const double* c, double f, double sr)
{
    const double w = 2 * PI * f / sr, cw = std::cos (w), b0 = c[0], b1 = c[1], a1 = c[2];
    return std::sqrt ((b0 * b0 + b1 * b1 + 2 * b0 * b1 * cw) / (1 + a1 * a1 + 2 * a1 * cw));
}
inline double onePole (double fc, double sr) { return 1 - std::exp (-2 * PI * std::min (fc, sr * 0.45) / sr); }
inline double sizeScale (double size) { return std::pow (2.0, (size - 0.5) * 1.2); }

struct Tap { double t, g; };

// ══════════════════════ Plaque : réservoir de Dattorro (1997) ══════════════════════
class PlateCore
{
public:
    struct P { double predelay = 0, decay = 0.5, size = 1, damping = 20000, bandwidth = 20000, in1 = 0.75, in2 = 0.625,
               dd1 = 0.7, dd2 = 0.5; bool dd2Auto = true; double rate = 1, depth = 16, mix = 1, out = 0; };

    explicit PlateCore (double sampleRate) : sr (sampleRate), k (sampleRate / 29761)
    {
        for (int i = 0; i < 13; ++i)
        {
            const int need = i == 0 ? (int) std::ceil (sr * 0.26) + 4 : (int) std::ceil ((base[i] * 1.6 + 48) * k) + 4;
            const int s = p2 (need);
            B[i].assign ((size_t) s, 0.0f); M[i] = s - 1; W[i] = 0;
        }
        setParams (p);
    }
    void setParams (const P& np)
    {
        p = np;
        const double s = k * p.size;
        for (int i = 1; i < 13; ++i) D[i] = std::max (1, (int) jsRound (base[i] * s));
        D[0] = std::max (0, std::min (M[0] - 1, (int) jsRound (p.predelay * 0.001 * sr)));
        for (int i = 0; i < 14; ++i) T[i] = std::max (1, (int) jsRound (tapBase[i] * s));
        bw = p.bandwidth >= 19990 ? 0.9995 : 1 - std::exp (-2 * PI * p.bandwidth / sr);
        damp = p.damping >= 19990 ? 0.0005 : std::exp (-2 * PI * p.damping / sr);
        dd2v = p.dd2Auto ? std::min (0.5, std::max (0.25, p.decay + 0.15)) : p.dd2;
        exc = p.depth * k;
        dph = 2 * PI * p.rate / sr;
        gain = std::pow (10.0, p.out / 20);
    }
    const P& params() const { return p; }
    void reset()
    {
        for (auto& b : B) std::fill (b.begin(), b.end(), 0.0f);
        bwS = 0; dpL = 0; dpR = 0;
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        float* b0 = B[0].data(); float* b1 = B[1].data(); float* b2 = B[2].data(); float* b3 = B[3].data(); float* b4 = B[4].data();
        float* b5 = B[5].data(); float* b6 = B[6].data(); float* b7 = B[7].data(); float* b8 = B[8].data(); float* b9 = B[9].data();
        float* b10 = B[10].data(); float* b11 = B[11].data(); float* b12 = B[12].data();
        const int m0 = M[0], m1 = M[1], m2 = M[2], m3 = M[3], m4 = M[4], m5 = M[5], m6 = M[6], m7 = M[7], m8 = M[8],
                  m9 = M[9], m10 = M[10], m11 = M[11], m12 = M[12];
        const int L0 = D[0], L1 = D[1], L2 = D[2], L3 = D[3], L4 = D[4], L5 = D[5], L6 = D[6], L7 = D[7], L8 = D[8],
                  L9 = D[9], L10 = D[10], L11 = D[11], L12 = D[12];
        int w0 = W[0], w1 = W[1], w2 = W[2], w3 = W[3], w4 = W[4], w5 = W[5], w6 = W[6], w7 = W[7], w8 = W[8],
            w9 = W[9], w10 = W[10], w11 = W[11], w12 = W[12];
        const double decay = p.decay, i1 = p.in1, i2 = p.in2, g1 = -p.dd1, g2 = dd2v, mix = p.mix, dry = 1 - p.mix;
        double s_bw = bwS, s_dl = dpL, s_dr = dpR, s_ph = ph;
        for (int i = 0; i < n; ++i)
        {
            const double xl = inL[i], xr = inR[i];
            b0[w0] = (float) ((xl + xr) * 0.5);
            double v = b0[(w0 - L0) & m0];
            s_bw += bw * (v - s_bw); v = s_bw;
            double d = b1[(w1 - L1) & m1]; double u = v - i1 * d; b1[w1] = (float) u; v = d + i1 * u;
            d = b2[(w2 - L2) & m2]; u = v - i1 * d; b2[w2] = (float) u; v = d + i1 * u;
            d = b3[(w3 - L3) & m3]; u = v - i2 * d; b3[w3] = (float) u; v = d + i2 * u;
            d = b4[(w4 - L4) & m4]; u = v - i2 * d; b4[w4] = (float) u; v = d + i2 * u;
            const double fbL = b8[(w8 - L8) & m8] * decay;
            const double fbR = b12[(w12 - L12) & m12] * decay;
            double x = v + fbR;
            double df = L5 + exc * std::sin (s_ph); int k0 = (int) std::floor (df); double fr = df - k0;
            d = b5[(w5 - k0) & m5] * (1 - fr) + b5[(w5 - k0 - 1) & m5] * fr;
            u = x - g1 * d; b5[w5] = (float) u; x = d + g1 * u;
            b6[w6] = (float) x; x = b6[(w6 - L6) & m6];
            s_dl = x + damp * (s_dl - x); x = s_dl * decay;
            d = b7[(w7 - L7) & m7]; u = x - g2 * d; b7[w7] = (float) u; x = d + g2 * u;
            b8[w8] = (float) x;
            double y = v + fbL;
            df = L9 + exc * std::cos (s_ph); k0 = (int) std::floor (df); fr = df - k0;
            d = b9[(w9 - k0) & m9] * (1 - fr) + b9[(w9 - k0 - 1) & m9] * fr;
            u = y - g1 * d; b9[w9] = (float) u; y = d + g1 * u;
            b10[w10] = (float) y; y = b10[(w10 - L10) & m10];
            s_dr = y + damp * (s_dr - y); y = s_dr * decay;
            d = b11[(w11 - L11) & m11]; u = y - g2 * d; b11[w11] = (float) u; y = d + g2 * u;
            b12[w12] = (float) y;
            const double yl = 0.6 * ((double) b10[(w10 - T[0]) & m10] + b10[(w10 - T[1]) & m10] - b11[(w11 - T[2]) & m11]
                                   + b12[(w12 - T[3]) & m12] - b6[(w6 - T[4]) & m6] - b7[(w7 - T[5]) & m7] - b8[(w8 - T[6]) & m8]);
            const double yr = 0.6 * ((double) b6[(w6 - T[7]) & m6] + b6[(w6 - T[8]) & m6] - b7[(w7 - T[9]) & m7]
                                   + b8[(w8 - T[10]) & m8] - b10[(w10 - T[11]) & m10] - b11[(w11 - T[12]) & m11] - b12[(w12 - T[13]) & m12]);
            outL[i] = (float) ((xl * dry + yl * mix) * gain);
            outR[i] = (float) ((xr * dry + yr * mix) * gain);
            s_ph += dph; if (s_ph > 6.283185307179586) s_ph -= 6.283185307179586;
            w0 = (w0 + 1) & m0; w1 = (w1 + 1) & m1; w2 = (w2 + 1) & m2; w3 = (w3 + 1) & m3; w4 = (w4 + 1) & m4;
            w5 = (w5 + 1) & m5; w6 = (w6 + 1) & m6; w7 = (w7 + 1) & m7; w8 = (w8 + 1) & m8; w9 = (w9 + 1) & m9;
            w10 = (w10 + 1) & m10; w11 = (w11 + 1) & m11; w12 = (w12 + 1) & m12;
        }
        W[0] = w0; W[1] = w1; W[2] = w2; W[3] = w3; W[4] = w4; W[5] = w5; W[6] = w6;
        W[7] = w7; W[8] = w8; W[9] = w9; W[10] = w10; W[11] = w11; W[12] = w12;
        bwS = s_bw; dpL = s_dl; dpR = s_dr; ph = s_ph;
        if (! (std::abs (dpL) < 1e6) || ! (std::abs (dpR) < 1e6)) reset();
    }

private:
    static constexpr double base[13] = { 0, 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
    static constexpr double tapBase[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121 };
    double sr, k;
    std::vector<float> B[13];
    int M[13] {}, W[13] {}, D[13] {}, T[14] {};
    P p;
    double bw = 0, damp = 0, dd2v = 0, exc = 0, dph = 0, gain = 1;
    double bwS = 0, dpL = 0, dpR = 0, ph = 0;
};

// ═══════════ Pièces : réflexions précoces + diffuseur + FDN (Chamber, Room, Hall, Church) ═══════════
class RoomCore
{
public:
    struct Img { double t, g, p; int ord; };
    struct Info { double dims[3], V, S, mfp, win, mean; int nT, steps; double delaysMs[8], diffMs[4]; };

    explicit RoomCore (double sampleRate) : sr (sampleRate)
    {
        LN = p2 ((int) std::ceil (0.32 * sr)); lm = LN - 1;
        for (auto& l : lines) l.assign ((size_t) LN, 0.0f);
        DN = p2 ((int) std::ceil (0.16 * sr)); dm = DN - 1;
        for (auto& d : dif) d.assign ((size_t) DN, 0.0f);
        EN = p2 ((int) std::ceil (0.3 * sr)); em = EN - 1;
        eb.assign ((size_t) EN, 0.0f);
    }
    void reset()
    {
        for (auto& b : lines) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : dif) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (eb.begin(), eb.end(), 0.0f);
        std::fill (std::begin (st), std::end (st), 0.0); std::fill (std::begin (f), std::end (f), 0.0);
        eL = 0; eR = 0;
    }

    // Méthode des sources-images (Allen et Berkley, 1979) sur une pièce parallélépipédique, sans allocation
    void images (const double* dims, double refl, int maxOrder, double win, int maxTaps)
    {
        const double Lx = dims[0], Ly = dims[1], Lz = dims[2], c = 343;
        const double S[3] = { 0.56 * Lx, 0.3 * Ly, std::min (2.0, 0.5 * Lz) }, R[3] = { 0.43 * Lx, 0.64 * Ly, std::min (1.6, 0.45 * Lz) };
        const double d0 = jsHypot (S[0] - R[0], S[1] - R[1], S[2] - R[2]);
        const int N = maxOrder;
        nCand = 0;
        for (int nx = -N; nx <= N; ++nx) for (int qx = 0; qx < 2; ++qx)
        {
            const int ox = std::abs (2 * nx - qx); if (ox > N) continue;
            const double X = 2 * nx * Lx + (qx ? -S[0] : S[0]);
            for (int ny = -N; ny <= N; ++ny) for (int qy = 0; qy < 2; ++qy)
            {
                const int oy = std::abs (2 * ny - qy); if (ox + oy > N) continue;
                const double Y = 2 * ny * Ly + (qy ? -S[1] : S[1]);
                for (int nz = -N; nz <= N; ++nz) for (int qz = 0; qz < 2; ++qz)
                {
                    const int oz = std::abs (2 * nz - qz), ord = ox + oy + oz; if (ord == 0 || ord > N) continue;
                    const double Z = 2 * nz * Lz + (qz ? -S[2] : S[2]);
                    const double d = jsHypot (X - R[0], Y - R[1], Z - R[2]), t = (d - d0) / c;
                    if (t <= 0.0005 || t > win) continue;
                    const double dx = X - R[0], dy = Y - R[1];
                    if (nCand < (int) cand.size())
                        cand[(size_t) nCand++] = { t, (d0 / d) * std::pow (refl, ord), dx / std::max (1e-9, jsHypot (dx, dy)), ord };
                }
            }
        }
        std::stable_sort (cand.begin(), cand.begin() + nCand, [] (const Img& a, const Img& b) { return b.g < a.g; });
        nT = std::min (nCand, maxTaps);
        std::copy (cand.begin(), cand.begin() + nT, im.begin());
        std::stable_sort (im.begin(), im.begin() + nT, [] (const Img& a, const Img& b) { return a.t < b.t; });
    }

    void configure (const ModeDef& def, double size)
    {
        const double s = sizeScale (size);
        double dims[3]; for (int i = 0; i < 3; ++i) dims[i] = def.dims[i] * s;
        const double V = dims[0] * dims[1] * dims[2], S = 2 * (dims[0] * dims[1] + dims[0] * dims[2] + dims[1] * dims[2]), mfp = 4 * V / S;
        const double win = std::min (0.2, std::max (0.03, (std::sqrt (V) * 1.3 + 20) / 1000));
        images (dims, 0.92, def.order, win, 48);
        for (int k = 0; k < nT; ++k) tT[k] = std::max (1, (int) jsRound (im[(size_t) k].t * sr));
        // diffuseur : étapes de délais + inversions de polarité + permutation + Hadamard (Signalsmith 2021)
        Rng rnd (def.seed); steps = def.nDiff;
        double dsum = 0;
        for (int stp = 0; stp < steps; ++stp)
        {
            const double D = def.diff[stp] * 0.001 * s * sr; dsum += D;
            int perm[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            for (int j = 7; j > 0; --j) { const int r = (int) std::floor (rnd.next() * (j + 1)); std::swap (perm[j], perm[r]); }
            for (int j = 0; j < 8; ++j)
            {
                const int kk = stp * 8 + j;
                dlen[kk] = std::max (1, (int) std::floor (D * (j + rnd.next()) / 8));
                dsg[kk] = rnd.next() < 0.5 ? -1.0f : 1.0f; dperm[kk] = perm[j];
            }
        }
        // FDN 8 lignes : moyenne >= libre parcours moyen 4V/S (Jot), étalement x0,71..x1,41, longueurs premières
        const double mean = std::max (mfp / 343, def.minMean * s);
        static constexpr int order[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };
        int used[8]; int nUsed = 0;
        for (int j = 0; j < 8; ++j)
        {
            int nn = std::max (3, (int) jsRound (mean * std::pow (2.0, j / 7.0 - 0.5) * sr));
            auto isUsed = [&] (int v) { for (int q = 0; q < nUsed; ++q) if (used[q] == v) return true; return false; };
            while (! isPrime (nn) || isUsed (nn)) ++nn;
            used[nUsed++] = nn;
            len[order[j]] = nn;
        }
        depth = def.mod[0] * 0.001 * sr;
        for (int j = 0; j < 8; ++j) { dph[j] = 2 * PI * def.mod[1] * (1 + 0.137 * j) / sr; ph[j] = j * 0.9; }
        hpA = ! same (def.hp, 0) ? onePole (def.hp, sr) : 0; lpA = ! same (def.lp, 0) ? onePole (def.lp, sr) : 1;
        gV = V; gS = S; gWin = win; gMean = mean; gDsum = dsum / sr;
        info.V = V; info.S = S; info.mfp = mfp; info.win = win; info.mean = mean; info.nT = nT; info.steps = steps;
        for (int i = 0; i < 3; ++i) info.dims[i] = dims[i];
        for (int j = 0; j < 8; ++j) info.delaysMs[j] = len[j] / sr * 1000;
        std::sort (info.delaysMs, info.delaysMs + 8);
        for (int i = 0; i < 4; ++i) info.diffMs[i] = i < steps ? def.diff[i] * s : 0;
    }

    void setDecay (const ModeDef& def, double decay, double dampAmt)
    {
        const double T = std::max (0.05, decay), hi = std::max (0.2, 0.95 - 0.75 * dampAmt);
        // filtres d'absorption par ligne (Jot) : gain médium + étagère grave + étagère aiguë
        for (int j = 0; j < 8; ++j)
        {
            const double L = len[j], gm = std::pow (10.0, -3 * L / (T * sr)),
                         glo = std::pow (10.0, -3 * L / (T * def.bass * sr)), ghi = std::pow (10.0, -3 * L / (T * hi * sr));
            double* o = cf + j * 7;
            shelfLo (glo / gm, def.fLo, sr, o + 1); shelfHi (ghi / gm, def.fHi, sr, o + 4);
            // les étagères débordent un peu sur le médium : on recale le gain pour que le RT60 vers 600 Hz soit exact
            const double comp = mag1 (o + 1, 600, sr) * mag1 (o + 4, 600, sr);
            o[0] = std::min (gm / comp, 0.9995 / std::max (1.0, glo / gm));
        }
        // réflexions : coefficient des parois déduit du RT60 (Eyring), niveaux calés sur une seule enveloppe exponentielle
        const double refl = std::min (0.985, std::exp (-0.0805 * gV / (gS * T)));
        double e = 0;
        for (int k = 0; k < nT; ++k) { const double g = im[(size_t) k].g * std::pow (refl / 0.92, im[(size_t) k].ord); raw[k] = g; e += g * g; }
        const double norm = def.er / std::sqrt (! same (e, 0) ? e : 1);
        const double D0 = def.er * def.er / (sr * T / 13.82 * (1 - std::exp (-13.82 * gWin / T)));
        for (int k = 0; k < nT; ++k)
        {
            const double g = raw[k] * norm;
            tL[k] = (float) (g * std::sqrt (1 - im[(size_t) k].p)); tR[k] = (float) (g * std::sqrt (1 + im[(size_t) k].p));
            taps[k] = { im[(size_t) k].t, g };
        }
        const double t0 = 0.5 * gDsum + 0.8 * gMean, tdc = 0.5 * gDsum;
        lateG = def.late * std::sqrt (8 * gMean * sr * D0) * std::exp (-6.91 * t0 / T);
        difG = def.difOut * std::sqrt (D0 * std::max (1.0, gDsum * sr)) * std::exp (-6.91 * tdc / T);
        eA = onePole (16000 * (1 - 0.7 * dampAmt), sr);
        hiRatio = hi; reflCoef = refl;
    }

    void process (const float* x, float* oL, float* oR, int n)
    {
        const double lg = lateG * 0.35355339059327373, dg = difG * 0.35355339059327373, TWO_PI = 6.283185307179586;
        float* e_b = eb.data();
        int lw_ = lw, dw_ = dw, ew_ = ew;
        double eL_ = eL, eR_ = eR;
        double c[8], tmp[8], o[8];
        for (int i = 0; i < n; ++i)
        {
            const double xi = x[i];
            // réflexions précoces (sources-images)
            e_b[ew_] = (float) xi; double el = 0, er = 0;
            for (int k = 0; k < nT; ++k) { const double v = e_b[(ew_ - tT[k]) & em]; el += v * tL[k]; er += v * tR[k]; }
            eL_ += eA * (el - eL_); eR_ += eA * (er - eR_);
            ew_ = (ew_ + 1) & em;
            // diffuseur
            for (int j = 0; j < 8; ++j) c[j] = xi;
            for (int s = 0; s < steps; ++s)
            {
                const int b = s * 8;
                for (int j = 0; j < 8; ++j) { float* buf = dif[(size_t) (b + j)].data(); buf[dw_] = (float) c[j]; tmp[j] = (double) buf[(dw_ - dlen[b + j]) & dm] * dsg[b + j]; }
                for (int j = 0; j < 8; ++j) c[j] = tmp[dperm[b + j]];
                had8 (c);
            }
            dw_ = (dw_ + 1) & dm;
            // FDN : lecture, filtres d'absorption (Jot), matrice de Householder
            double sum = 0, yl = 0, yr = 0;
            for (int j = 0; j < 8; ++j)
            {
                const float* bl = lines[(size_t) j].data(); double v;
                if (depth > 0 && (j & 1))
                {
                    const double df = len[j] + depth * std::sin (ph[j]); const int k0 = (int) std::floor (df); const double fr = df - k0;
                    v = bl[(lw_ - k0) & lm] * (1 - fr) + bl[(lw_ - k0 - 1) & lm] * fr;
                    ph[j] += dph[j]; if (ph[j] > TWO_PI) ph[j] -= TWO_PI;
                }
                else v = bl[(lw_ - len[j]) & lm];
                const double* q = cf + j * 7; double* r = st + j * 4;
                const double y1 = q[1] * v + q[2] * r[0] - q[3] * r[1]; r[0] = v; r[1] = y1;
                const double y2 = q[4] * y1 + q[5] * r[2] - q[6] * r[3]; r[2] = y1; r[3] = y2;
                v = y2 * q[0]; o[j] = v; sum += v;
                yl += (j & 1) ? -v : v; yr += (j & 2) ? -v : v;
            }
            const double hs = sum * 0.25;
            for (int j = 0; j < 8; ++j) lines[(size_t) j][(size_t) lw_] = (float) (o[j] - hs + c[j] * 0.35355339059327373);
            lw_ = (lw_ + 1) & lm;
            const double dl = c[0] - c[1] + c[2] - c[3] + c[4] - c[5] + c[6] - c[7], dr = c[0] + c[1] - c[2] - c[3] + c[4] + c[5] - c[6] - c[7];
            double L = eL_ + lg * yl + dg * dl, R = eR_ + lg * yr + dg * dr;
            if (! same (hpA, 0)) { f[0] += hpA * (L - f[0]); f[1] += hpA * (R - f[1]); L -= f[0]; R -= f[1]; }
            if (lpA < 1) { f[2] += lpA * (L - f[2]); f[3] += lpA * (R - f[3]); L = f[2]; R = f[3]; }
            oL[i] = (float) L; oR[i] = (float) R;
            lastO0 = o[0];
        }
        lw = lw_; dw = dw_; ew = ew_; eL = eL_; eR = eR_;
        if (! (std::abs (lastO0) < 1e6)) reset();
    }

    int numTaps() const { return nT; }
    const Tap* tapList() const { return taps; }
    Info info {};
    double hiRatio = 0, reflCoef = 0;

private:
    double sr;
    int LN = 0, lm = 0, lw = 0, DN = 0, dm = 0, dw = 0, EN = 0, em = 0, ew = 0;
    std::array<std::vector<float>, 8> lines;
    std::array<std::vector<float>, 32> dif;
    std::vector<float> eb;
    int len[8] {}; double cf[56] {}; double st[32] {};
    int dlen[32] {}; float dsg[32] {}; int dperm[32] {}; int steps = 4;
    int tT[64] {}; float tL[64] {}, tR[64] {}; int nT = 0;
    double raw[64] {}; Tap taps[64] {};
    double ph[8] {}, dph[8] {}, depth = 0;
    double eA = 1, eL = 0, eR = 0, hpA = 0, lpA = 1, f[4] {};
    double lateG = 1, difG = 0.5, lastO0 = 0;
    double gV = 1, gS = 1, gWin = 0.03, gMean = 0.01, gDsum = 0;
    std::array<Img, 2744> cand {}; int nCand = 0;
    std::array<Img, 48> im {};
};

// ══════════ Ressort : chaîne de passe-tout étirés dans une boucle (Välimäki, Parker, Abel 2010) ══════════
class SpringCore
{
public:
    struct Info { double Td, chain; int k; double fC; };

    explicit SpringCore (double sampleRate) : sr (sampleRate)
    {
        k = std::max (1, (int) jsRound (sr / (2 * 4400))); fC = sr / (2 * k);
        DN = p2 ((int) std::ceil (0.12 * sr)); dm = DN - 1;
        for (int i = 0; i < 2; ++i)
        {
            auto& S = s[i];
            S.ap.assign ((size_t) (M * k), 0.0f); S.aph.assign ((size_t) MH, 0.0f);
            S.dl.assign ((size_t) DN, 0.0f); S.dh.assign ((size_t) DN, 0.0f);
            S.cnt = i * 4000; S.r = Rng ((uint32_t) (91 + i * 17));
        }
        // passe-bas de sortie à fC (biquad RBJ, Q 0,707)
        const double w0 = 2 * PI * std::min (fC * 0.95, sr * 0.45) / sr, al = std::sin (w0) / (2 * 0.7071), cs = std::cos (w0), a0 = 1 + al;
        lb[0] = (1 - cs) / 2 / a0; lb[1] = (1 - cs) / a0; lb[2] = (1 - cs) / 2 / a0; lb[3] = -2 * cs / a0; lb[4] = (1 - al) / a0;
        hpA = onePole (90, sr);
    }
    void reset()
    {
        for (auto& S : s)
        {
            std::fill (S.ap.begin(), S.ap.end(), 0.0f); std::fill (S.aph.begin(), S.aph.end(), 0.0f);
            std::fill (S.dl.begin(), S.dl.end(), 0.0f); std::fill (S.dh.begin(), S.dh.end(), 0.0f);
            S.fb = 0; S.fbh = 0; S.lp = 0; S.z1 = 0; S.z2 = 0; S.hp = 0;
        }
    }
    double gd (double c, int kk, double f) const { const double om = 2 * PI * f / sr; return kk * (1 - c * c) / (1 + 2 * c * std::cos (kk * om) + c * c); }
    void configure (double size)
    {
        const double Td = (0.03 + 0.04 * size) * sr, ratio[2] = { 1, 47.3 / 41 }, ratioH[2] = { 1, 21.7 / 17.9 };
        for (int i = 0; i < 2; ++i) { s[i].Td = Td * ratio[i]; s[i].Th = Td * 0.437 * ratioH[i]; }
        const double chain = M * gd (a, k, 707) / sr;
        for (int r = 1; r <= 8; ++r) taps[r - 1] = { r * (Td / sr + chain), std::pow (0.72, r - 1) };
        info = { Td / sr * 1000, chain * 1000, k, fC };
    }
    void setDecay (double decay, double dampAmt)
    {
        const double T = std::max (0.05, decay), gl = M * gd (a, k, 707), gh = MH * gd (ah, 1, 707);
        dc = std::exp (-2 * PI * std::min (9000 * std::pow (0.3, dampAmt), sr * 0.45) / sr);
        const double om = 2 * PI * 500 / sr, b = 1 - dc, lpMag = b / std::sqrt (1 + dc * dc - 2 * dc * std::cos (om));
        for (auto& S : s)
        {
            S.g = std::min (0.999, std::pow (10.0, -3 * (S.Td + gl) / (T * sr)) / lpMag);
            S.gh = std::pow (10.0, -3 * (S.Th + gh) / (T * 0.6 * sr));
        }
    }
    void process (const float* x, float* oL, float* oR, int n)
    {
        int w_ = w, ki_ = ki;
        for (int i = 0; i < n; ++i)
        {
            const double xi = x[i];
            for (int sp = 0; sp < 2; ++sp)
            {
                auto& S = s[sp]; float* ap = S.ap.data();
                // boucle basse fréquence : 100 passe-tout étirés (dispersion = chirps)
                double v = xi + S.fb;
                for (int m = 0, q = ki_; m < M; ++m, q += k) { const double d = ap[q], u = v - a * d; ap[q] = (float) u; v = d + a * u; }
                S.lp = v + dc * (S.lp - v);
                S.dl[(size_t) w_] = (float) S.lp;
                if (--S.cnt <= 0) { S.cnt = (int) jsRound (sr * 0.23); S.modT = (S.r.next() * 2 - 1) * 8 * sr / 48000; }
                S.mod += 0.0004 * (S.modT - S.mod);
                const double df = S.Td + S.mod; const int k0 = (int) std::floor (df); const double fr = df - k0;
                const double y = S.dl[(size_t) ((w_ - k0) & dm)] * (1 - fr) + S.dl[(size_t) ((w_ - k0 - 1) & dm)] * fr;
                S.fb = y * S.g;
                // boucle haute fréquence : 40 passe-tout du 1er ordre
                double h = xi + S.fbh; float* aph = S.aph.data();
                for (int m = 0; m < MH; ++m) { const double d = aph[m], u = h - ah * d; aph[m] = (float) u; h = d + ah * u; }
                S.dh[(size_t) w_] = (float) h;
                const double yh = S.dh[(size_t) ((w_ - (int) jsRound (S.Th)) & dm)]; S.fbh = yh * S.gh;
                // sortie : passe-bas à fC + voie aiguë à -14 dB, passe-haut 90 Hz
                const double yo = lb[0] * y + S.z1; S.z1 = lb[1] * y - lb[3] * yo + S.z2; S.z2 = lb[2] * y - lb[4] * yo;
                double out = yo + 0.2 * yh;
                S.hp += hpA * (out - S.hp); out -= S.hp;
                if (sp == 0) oL[i] = (float) out; else oR[i] = (float) out;
            }
            w_ = (w_ + 1) & dm; ki_ = ki_ + 1 == k ? 0 : ki_ + 1;
        }
        w = w_; ki = ki_;
        if (! (std::abs (s[0].fb) < 1e6)) reset();
    }
    const Tap* tapList() const { return taps; }
    Info info {};

private:
    struct Spring
    {
        std::vector<float> ap, aph, dl, dh;
        double fb = 0, fbh = 0, lp = 0, z1 = 0, z2 = 0, hp = 0, mod = 0, modT = 0, Td = 0, Th = 0, g = 0, gh = 0;
        int cnt = 0; Rng r { 0 };
    };
    double sr; static constexpr int M = 100, MH = 40; static constexpr double a = 0.6, ah = -0.6;
    int k = 1; double fC = 0; int DN = 0, dm = 0, w = 0, ki = 0;
    Spring s[2];
    double lb[5] {}, hpA = 0, dc = 0.2;
    Tap taps[8] {};
};

// ══════════════════════ Moteur complet du plugin ══════════════════════
class ReverbEngine
{
public:
    struct Params { int mode = chamber; double predelay = 0, decay = 1.5, mix = 0.3, size = 0.4, damp = 0.5, width = 0.8; };

    explicit ReverbEngine (double sampleRate)
        : sr (sampleRate), room (sampleRate), plate (sampleRate), spring (sampleRate)
    {
        PN = p2 ((int) std::ceil (0.26 * sr)); pm = PN - 1; pb.assign ((size_t) PN, 0.0f);
        fStep = 1 / (0.012 * sr);
        const auto& d = modeDef (chamber).def;
        p = { chamber, d.predelay, d.decay, d.mix, d.size, d.damp, d.width };
        mixS = p.mix; widS = p.width;
        applyStruct();
    }
    void setParams (const Params& np)
    {
        p = np;
        if (p.mode != active || ! same (p.size, sSize)) pend = true; else applyTone();
    }
    void snap() { applyStruct(); pend = false; fade = 1; mixS = p.mix; widS = p.width; }
    const Params& params() const { return p; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int off = 0; off < n; off += 128)
        {
            const int m = std::min (128, n - off), D = (int) jsRound (p.predelay * 0.001 * sr);   // + 1e-24 : anti-dénormaux
            int pw_ = pw;
            for (int i = 0; i < m; ++i)
            {
                pb[(size_t) pw_] = (float) (0.5 * ((double) inL[off + i] + inR[off + i]));
                x[i] = (float) ((double) pb[(size_t) ((pw_ - D) & pm)] + 1e-24); pw_ = (pw_ + 1) & pm;
            }
            pw = pw_;
            const auto e = modeDef (p.mode).engine;
            if (e == plateEngine) plate.process (x, x, wl, wr, m);
            else if (e == roomEngine) room.process (x, wl, wr, m);
            else spring.process (x, wl, wr, m);
            const double mixT = p.mix, widT = p.width;
            for (int i = 0; i < m; ++i)
            {
                if (pend) { fade -= fStep; if (fade <= 0) { fade = 0; applyStruct(); pend = false; } }
                else if (fade < 1) { fade += fStep; if (fade > 1) fade = 1; }
                mixS += 0.002 * (mixT - mixS); widS += 0.002 * (widT - widS);
                const double g = fade * gain;
                double L = wl[i] * g, R = wr[i] * g; const double Mm = (L + R) * 0.5, Ss = (L - R) * 0.5 * widS;
                L = Mm + Ss; R = Mm - Ss;
                outL[off + i] = (float) (inL[off + i] * (1 - mixS) + L * mixS);
                outR[off + i] = (float) (inR[off + i] * (1 - mixS) + R * mixS);
            }
        }
    }

    // Repères pour l'affichage (réflexions précoces ou retours du ressort)
    int numTaps() const
    {
        const auto e = modeDef (p.mode).engine;
        return e == roomEngine ? room.numTaps() : e == springEngine ? 8 : 0;
    }
    const Tap* tapList() const { return modeDef (p.mode).engine == roomEngine ? room.tapList() : spring.tapList(); }
    const RoomCore& roomCore() const { return room; }
    const SpringCore& springCore() const { return spring; }

private:
    void applyStruct()
    {
        const auto& def = modeDef (p.mode); const bool changed = p.mode != active;
        if (def.engine == roomEngine) { room.configure (def, p.size); if (changed) room.reset(); }
        else if (def.engine == springEngine) { spring.configure (p.size); if (changed) spring.reset(); }
        else { auto q = plate.params(); q.size = 0.6 + 0.8 * p.size; plate.setParams (q); if (changed) plate.reset(); }
        active = p.mode; sSize = p.size;
        applyTone();
    }
    void applyTone()
    {
        const auto& def = modeDef (p.mode);
        if (def.engine == roomEngine) room.setDecay (def, p.decay, p.damp);
        else if (def.engine == springEngine) spring.setDecay (p.decay, p.damp);
        else
        {
            // RT60 théorique de la boucle Dattorro, corrigé d'après mesure (la plaque sonne ~5 à 15 % plus long que la formule)
            const double ps = 0.6 + 0.8 * p.size, half = 10794.5 / 29761 * ps, T = std::max (0.05, p.decay), Tc = T / (1 + 0.125 / std::sqrt (T));
            PlateCore::P q = plate.params();
            q.decay = std::min (0.9995, std::pow (10.0, -1.5 * half / Tc));
            q.damping = p.damp < 0.005 ? 20000 : 20000 * std::pow (0.15, p.damp);
            q.bandwidth = 20000; q.predelay = 0; q.mix = 1; q.out = 0; q.size = ps;
            plate.setParams (q);
        }
        gain = def.gain;
    }

    double sr;
    RoomCore room; PlateCore plate; SpringCore spring;
    int PN = 0, pm = 0, pw = 0; std::vector<float> pb;
    float x[128] {}, wl[128] {}, wr[128] {};
    Params p;
    int active = -1; double sSize = -1; bool pend = false; double fade = 1, fStep = 0, mixS = 0, widS = 0, gain = 1;
};
} // namespace audiolab::rv
