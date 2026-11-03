// Moteur audio de Lab Verb (Audio Lab Reverb) 2.0.0.
// Né du prototype « Labo Reverb » (JavaScript, porté ligne à ligne en 1.x), devenu la version « pro » du plugin :
// la page web ne change pas, le plugin s'en écarte volontairement (voir docs/11_GUI_Plugin_Reverb.md, § 2.0.0).
// Six modes : Chamber, Room, Hall, Church (réflexions par sources-images + diffuseurs + FDN de Jot),
// Plate (réservoir de Dattorro, 1997) et Spring (boucles dispersives, Välimäki, Parker et Abel).
// 2.0.0 :
//  - vraie entrée stéréo : Chamber / Room / Hall / Church ont deux sources (gauche, droite), deux diffuseurs et un FDN
//    de 16 lignes en deux moitiés (gauche, droite) couplées par une rotation : une source à gauche donne une réverb
//    qui penche à gauche, puis s'étale ; Plate : L et R entrent chacun dans une moitié du réservoir ; Spring : mono ;
//  - FDN 16 lignes (Hadamard 8 dans chaque moitié), toutes les lignes modulées (interpolation passe-tout, sans perte d'aigus) ;
//  - changement de Size ou de mode par fondu d'entrée entre deux moteurs : le moteur sortant garde sa queue ;
//    Pre-delay : deux têtes de lecture en fondu ;
//  - réglages avancés : Low cut, High cut, Bass, Early / Late, Ducking, Freeze ;
//  - filtres audibles conçus « matched » (même son de 44,1 à 192 kHz).
// Temps réel : toute la mémoire est réservée dans le constructeur ; process() n'alloue rien, ne verrouille rien.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace audiolab::rv
{
constexpr double PI = 3.141592653589793;
constexpr double TWO_PI = 6.283185307179586;

// Égalité exacte voulue (comparaison de réglages), sans avertissement du compilateur
inline bool same (double a, double b) { return std::equal_to<double>() (a, b); }

// Arrondi de JavaScript (Math.round : demi vers +∞), gardé pour les longueurs héritées du prototype
inline double jsRound (double x) { return std::floor (x + 0.5); }

// Math.hypot de V8 (somme de Kahan normalisée), pour retrouver les mêmes distances que le prototype
inline double jsHypot (double a, double b, double c)
{
    const double v[3] = { std::abs (a), std::abs (b), std::abs (c) };
    double mx = 0; for (double x : v) if (x > mx) mx = x;
    if (same (mx, 0)) return 0;
    double sum = 0, comp = 0;
    for (double x : v) { const double n = x / mx, s = n * n - comp, pre = sum + s; comp = (pre - sum) - s; sum = pre; }
    return std::sqrt (sum) * mx;
}
inline double jsHypot (double a, double b) { return jsHypot (a, b, 0); }

// ── Définition des modes (valeurs du prototype ; 2.0.0 : modulation très faible ajoutée à Room) ──
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
    // gain : niveaux des modes égalisés sur leurs réglages par défaut ; 2.0.0 : recalés pour garder les niveaux de la 1.x
    // (énergie de la réponse de 90 Hz à 11 kHz à 48 kHz, entrée au centre ; 1.x : 0,75 / 0,72 / 1,23 / 0,55 / 0,95 / 0,96)
    static const ModeDef M[numModes] = {
        { "Chamber", roomEngine, { 0, 1.5, 0.30, 0.40, 0.50, 0.80 }, { 6.0, 4.2, 3.2 }, 0.9, 3, 0.8, 1, 1, { 3, 6, 11, 19 }, 4,
          1.0, 350, 3000, 0.03, { 0.06, 0.7 }, 180, 10000, 23, 0.710 },
        { "Plate", plateEngine, { 10, 2.2, 0.25, 0.60, 0.30, 1.0 }, { 0, 0, 0 }, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0,
          0, 0, 0, 0, { 0, 0 }, 0, 0, 0, 0.692 },
        { "Spring", springEngine, { 0, 2.0, 0.35, 0.30, 0.40, 0.50 }, { 0, 0, 0 }, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0,
          0, 0, 0, 0, { 0, 0 }, 0, 0, 0, 1.218 },
        { "Room", roomEngine, { 0, 0.6, 0.20, 0.25, 0.50, 0.70 }, { 7.0, 5.5, 3.0 }, 0.8, 3, 1.0, 1, 1, { 2.5, 5, 9, 16 }, 4,
          1.1, 300, 5000, 0.014, { 0.02, 0.45 }, 0, 0, 11, 0.522 },
        { "Hall", roomEngine, { 25, 2.2, 0.30, 0.80, 0.45, 1.0 }, { 26, 17, 12 }, 0.85, 2, 0.55, 1, 1, { 8, 17, 33, 60 }, 4,
          1.35, 250, 4000, 0.035, { 0.25, 0.6 }, 0, 0, 37, 0.866 },
        { "Church", roomEngine, { 40, 5.0, 0.40, 1.0, 0.65, 1.0 }, { 36, 16, 15 }, 0.9, 2, 0.6, 1, 1, { 10, 24, 50, 0 }, 3,
          1.45, 220, 3000, 0.08, { 0.15, 0.4 }, 0, 0, 53, 0.878 },
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
// Étagères du 1er ordre « matched » : coefs { b0, b1, a1 } pour y = b0 x + b1 x1 - a1 y1 ; pôle par invariance
// impulsionnelle, module exact à 0 Hz et à Nyquist (la transformée bilinéaire de la 1.x écrasait le haut du spectre
// à 44,1 / 48 kHz, ce qui raccourcissait le RT60 des aigus par rapport à 96 / 192 kHz)
inline void shelfLoM (double G, double fc, double sr, double* c)
{
    const double p = std::exp (-TWO_PI * fc / sr), wn = PI * sr, wc = TWO_PI * fc;
    const double hn = std::sqrt ((wn * wn + G * G * wc * wc) / (wn * wn + wc * wc));
    c[0] = 0.5 * (G * (1 - p) + hn * (1 + p)); c[1] = 0.5 * (G * (1 - p) - hn * (1 + p)); c[2] = -p;
}
inline void shelfHiM (double G, double fc, double sr, double* c)
{
    const double p = std::exp (-TWO_PI * fc / sr), wn = PI * sr, wc = TWO_PI * fc;
    const double hn = std::sqrt ((G * G * wn * wn + wc * wc) / (wn * wn + wc * wc));
    c[0] = 0.5 * ((1 - p) + hn * (1 + p)); c[1] = 0.5 * ((1 - p) - hn * (1 + p)); c[2] = -p;
}
// Étagère grave du 2e ordre (RBJ, pente S = 1, sans dépassement) : gain G au grave, 1 au-dessus, milieu de la
// transition à fc ; coefs { b0, b1, b2, a1, a2 } pour y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
// 2.0.0 : sert au réglage Bass (une étagère du 1er ordre déborderait jusqu'à 1 kHz et changerait le médium)
constexpr double bassXover = 200;   // fréquence de transition du réglage Bass (Hz)
inline void shelfLo2 (double G, double fc, double sr, double* c)
{
    const double A = std::sqrt (std::max (1e-9, G)), w0 = TWO_PI * std::min (fc, sr * 0.45) / sr, cs = std::cos (w0);
    const double al = std::sin (w0) * 0.7071067811865476, sa = 2 * std::sqrt (A) * al;
    const double a0 = (A + 1) + (A - 1) * cs + sa;
    c[0] = A * ((A + 1) - (A - 1) * cs + sa) / a0; c[1] = 2 * A * ((A - 1) - (A + 1) * cs) / a0; c[2] = A * ((A + 1) - (A - 1) * cs - sa) / a0;
    c[3] = -2 * ((A - 1) + (A + 1) * cs) / a0; c[4] = ((A + 1) + (A - 1) * cs - sa) / a0;
}
inline double mag2 (const double* c, double f, double sr)
{
    const double w = TWO_PI * f / sr, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = c[0] + c[1] * c1 + c[2] * c2, ni = -c[1] * s1 - c[2] * s2, dr = 1 + c[3] * c1 + c[4] * c2, di = -c[3] * s1 - c[4] * s2;
    return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
}
inline double mag1 (const double* c, double f, double sr)
{
    const double w = 2 * PI * f / sr, cw = std::cos (w), b0 = c[0], b1 = c[1], a1 = c[2];
    return std::sqrt ((b0 * b0 + b1 * b1 + 2 * b0 * b1 * cw) / (1 + a1 * a1 + 2 * a1 * cw));
}
inline double sizeScale (double size) { return std::pow (2.0, (size - 0.5) * 1.2); }

// Filtre du 1er ordre y = b0 x + b1 x1 + p y1
struct Coef1 { double b0 = 1, b1 = 0, p = 0; };
// Passe-bas « matched » : pôle par invariance impulsionnelle, zéro placé pour que le module à Nyquist soit celui
// du filtre analogique (Vicanek) : même réponse de 44,1 à 192 kHz, sans la remontée de la formule exponentielle seule
inline Coef1 lp1Matched (double fc, double sr)
{
    Coef1 c;
    const double p = std::exp (-TWO_PI * fc / sr), r = 0.5 * sr / fc, hn = 1 / std::sqrt (1 + r * r);
    c.b0 = 0.5 * ((1 - p) + hn * (1 + p)); c.b1 = 0.5 * ((1 - p) - hn * (1 + p)); c.p = p;
    return c;
}
// Passe-haut bilinéaire pré-déformé (exact à fc ; aux fréquences de coupure basses, identique à l'analogique)
inline Coef1 hp1 (double fc, double sr)
{
    Coef1 c;
    const double K = std::tan (PI * std::min (fc, sr * 0.45) / sr);
    c.b0 = 1 / (1 + K); c.b1 = -c.b0; c.p = (1 - K) / (1 + K);
    return c;
}
inline double mag1c (const Coef1& c, double f, double sr)
{
    const double w = TWO_PI * f / sr, cw = std::cos (w);
    return std::sqrt ((c.b0 * c.b0 + c.b1 * c.b1 + 2 * c.b0 * c.b1 * cw) / (1 + c.p * c.p - 2 * c.p * cw));
}

// Filtre d'état variable (Zavalishin, TPT) : y = m0 hp + m1 bp + m2 lp ; stable quand ses réglages bougent
struct Svf { double g = 0, k = 1.41421356, m0 = 1, m1 = 0, m2 = 0; };
// Passe-haut de Butterworth (12 dB/oct.), pré-déformé
inline Svf svfHp (double fc, double sr) { Svf s; s.g = std::tan (PI * std::min (fc, sr * 0.45) / sr); s.k = 1.4142135623730951; s.m0 = 1; s.m1 = 0; s.m2 = 0; return s; }
// Passe-bas de Butterworth (12 dB/oct.) « matched » (d'après Vicanek 2016) : pôles par invariance impulsionnelle,
// numérateur du 2e ordre calé sur le module analogique à 0 Hz, à la coupure et à 20 kHz ; mis sous forme SVF.
// Écart au filtre analogique : ±0,45 dB jusqu'à 20 kHz pour une coupure jusqu'à 15 kHz, à 44,1 comme à 192 kHz
inline Svf svfLpMatched (double fc, double sr)
{
    const double Q = 0.7071067811865476, zeta = 0.5 / Q, w0 = TWO_PI * std::min (fc, sr * 0.49) / sr;
    const double e = std::exp (-zeta * w0), a1 = -2 * e * std::cos (std::sqrt (1 - zeta * zeta) * w0), a2 = e * e;
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
    auto phi = [] (double w, double* f) { const double s = std::sin (w * 0.5); f[1] = s * s; f[0] = 1 - f[1]; f[2] = 4 * f[0] * f[1]; };
    auto ana = [fc] (double f) { const double r = f / fc; return 1 / (1 + r * r * r * r); };   // module analogique au carré
    const double fh = std::min (0.98 * sr * 0.5, std::max (20000.0, 1.05 * fc));
    double pc[3], ph[3]; phi (w0, pc); phi (TWO_PI * fh / sr, ph);
    const double Dc = A0 * pc[0] + A1 * pc[1] + A2 * pc[2], Dh = A0 * ph[0] + A1 * ph[1] + A2 * ph[2];
    const double B0 = A0, r1 = ana (fc) * Dc - B0 * pc[0], r2 = ana (fh) * Dh - B0 * ph[0];
    const double det = pc[1] * ph[2] - pc[2] * ph[1];
    const double B1 = std::max (0.0, (r1 * ph[2] - pc[2] * r2) / det), B2 = (pc[1] * r2 - r1 * ph[1]) / det;
    const double W = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
    const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2))), b1 = 0.5 * (std::sqrt (B0) - std::sqrt (B1)), b2 = -B2 / (4 * b0);
    // biquad → SVF (mêmes pôles, mélange des trois sorties)
    Svf s;
    s.g = std::sqrt ((1 + a1 + a2) / (1 - a1 + a2));
    s.k = 2 * (1 - a2) / ((1 + a2 - a1) * s.g);
    const double d0 = 1 + s.g * s.k + s.g * s.g;
    s.m0 = d0 * (b0 - b1 + b2) / 4; s.m1 = d0 * (b0 - b2) / (2 * s.g); s.m2 = d0 * (b0 + b1 + b2) / (4 * s.g * s.g);
    return s;
}
// Module d'un SVF (pour les tests et l'affichage)
inline double svfMag (const Svf& s, double f, double sr)
{
    // H(z) = (m0 (1 - z^-1)^2 + m1 g (1 - z^-2) + m2 g^2 (1 + z^-1)^2) / ((1 + gk + g^2) + 2 (g^2 - 1) z^-1 + (1 - gk + g^2) z^-2)
    const double w = TWO_PI * f / sr, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w), g = s.g, k = s.k;
    const double n0 = s.m0 + s.m1 * g + s.m2 * g * g, n1 = -2 * s.m0 + 2 * s.m2 * g * g, n2 = s.m0 - s.m1 * g + s.m2 * g * g;
    const double d0 = 1 + g * k + g * g, d1 = 2 * (g * g - 1), d2 = 1 - g * k + g * g;
    const double nr = n0 + n1 * c1 + n2 * c2, ni = -n1 * s1 - n2 * s2, dr = d0 + d1 * c1 + d2 * c2, di = -d1 * s1 - d2 * s2;
    return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
}

struct Tap { double t, g; };

// Vidage progressif des mémoires d'un moteur inactif (jamais tout d'un coup dans le fil audio) :
// chaque appel efface au plus « budget » échantillons et dit s'il reste du travail
struct Clearer
{
    int buf = 0; size_t pos = 0;
    template <typename Get>
    bool step (int nBufs, Get get, size_t& budget)
    {
        while (buf < nBufs && budget > 0)
        {
            std::vector<float>& b = get (buf);
            const size_t n = std::min (budget, b.size() - pos);
            if (n > 0) std::memset (b.data() + pos, 0, n * sizeof (float));
            pos += n; budget -= n;
            if (pos >= b.size()) { ++buf; pos = 0; }
        }
        if (buf < nBufs) return false;
        buf = 0; pos = 0;
        return true;
    }
};

// Fondu « en cloche » de 0 à 1 (raised cosine)
inline double rcos (double x) { return 0.5 - 0.5 * std::cos (PI * std::clamp (x, 0.0, 1.0)); }

// ══════════════════════ Plaque : réservoir de Dattorro (1997) ══════════════════════
class PlateCore
{
public:
    struct P { double decay = 0.5, size = 1, damping = 20000, dampW = 1, bandwidth = 20000, in1 = 0.75, in2 = 0.625,
               dd1 = 0.7, dd2 = 0.5; bool dd2Auto = true; double rate = 1, depth = 16, loG = 1, loComp = 1; };
    static constexpr int NB = 16;   // 4 + 4 diffuseurs d'entrée (L, R), 8 lignes du réservoir

    explicit PlateCore (double sampleRate) : sr (sampleRate), k (sampleRate / 29761)
    {
        for (int i = 0; i < NB; ++i)
        {
            const int s = p2 ((int) std::ceil ((base[i] * 1.6 + 48) * k) + 4);
            B[i].assign ((size_t) s, 0.0f); M[i] = s - 1; W[i] = 0;
        }
        setParams (p);
    }
    void setParams (const P& np)
    {
        p = np;
        const double s = k * p.size;
        for (int i = 0; i < NB; ++i) D[i] = std::max (1, (int) jsRound (base[i] * s));
        for (int i = 0; i < 14; ++i) T[i] = std::max (1, (int) jsRound (tapBase[i] * s));
        bw = p.bandwidth >= 19990 ? 0.9995 : 1 - std::exp (-TWO_PI * p.bandwidth / sr);
        exc = p.depth * k;
        dph = TWO_PI * p.rate / sr;
        // coefficients de la boucle (interpolés à chaque échantillon quand ils changent, voir preTick / postTick)
        const Coef1 dc = lp1Matched (std::min (p.damping, sr * 0.49), sr);
        cb[0] = p.decay * p.loComp; cb[1] = p.dd2Auto ? std::min (0.5, std::max (0.25, p.decay + 0.15)) : p.dd2;
        cb[2] = dc.b0; cb[3] = dc.b1; cb[4] = dc.p; cb[5] = p.dampW;
        shelfLo2 (p.loG, bassXover, sr, cb + 6);
    }
    void preTick() { std::copy (std::begin (cb), std::end (cb), std::begin (ca)); }
    void postTick (int tickLen)
    {
        ramp = ! std::equal (std::begin (ca), std::end (ca), std::begin (cb), [] (double x, double y) { return same (x, y); });
        invTick = 1.0 / tickLen;
    }
    void snapCoefs() { std::copy (std::begin (cb), std::end (cb), std::begin (ca)); ramp = false; }
    const P& params() const { return p; }
    void reset()
    {
        for (auto& b : B) std::fill (b.begin(), b.end(), 0.0f);
        clearState();
    }
    bool clearSome (size_t& budget)
    {
        if (! clr.step (NB, [this] (int i) -> std::vector<float>& { return B[i]; }, budget)) return false;
        clearState(); return true;
    }
    // Entrée stéréo : L dans la moitié dont les prises dominent la sortie gauche (« y »), R dans l'autre (« x »).
    // Avec une entrée mono (L = R), les deux moitiés reçoivent le même signal, comme le réservoir d'origine.
    // tk : rang du premier échantillon dans la tranche de 128 (rampes des coefficients)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, int tk)
    {
        float* b[NB]; int m[NB], L[NB], w[NB];
        for (int i = 0; i < NB; ++i) { b[i] = B[i].data(); m[i] = M[i]; L[i] = D[i]; w[i] = W[i]; }
        const double i1 = p.in1, i2 = p.in2, g1 = -p.dd1;
        double c[NCP]; std::copy (std::begin (cb), std::end (cb), c);
        double sbL = bwL, sbR = bwR, s_dl = dpL, s_dr = dpR, s_xl = dxL, s_xr = dxR, s_ph = ph;
        double q0 = loS[0], q1 = loS[1], q2 = loS[2], q3 = loS[3], q4 = loS[4], q5 = loS[5], q6 = loS[6], q7 = loS[7], apX = apS[0], apYv = apS[1];
        auto diffuse = [&] (double v, int o)
        {
            // 4 passe-tout de diffusion d'entrée (diffuseurs o … o + 3)
            for (int s = 0; s < 4; ++s)
            {
                const int j = o + s; const double gg = s < 2 ? i1 : i2;
                const double d = b[j][(w[j] - L[j]) & m[j]], u = v - gg * d; b[j][w[j]] = (float) u; v = d + gg * u;
            }
            return v;
        };
        for (int i = 0; i < n; ++i)
        {
            if (ramp) { const double t = (tk + i + 1) * invTick; for (int q = 0; q < NCP; ++q) c[q] = ca[q] + (cb[q] - ca[q]) * t; }
            const double decay = c[0], g2 = c[1], db0 = c[2], db1 = c[3], dp = c[4], dw = c[5], l0 = c[6], l1 = c[7], l2 = c[8], la1 = c[9], la2 = c[10];
            const double xl = inL[i], xr = inR[i];
            sbL += bw * (xl - sbL); sbR += bw * (xr - sbR);
            const double vL = diffuse (sbL, 0), vR = diffuse (sbR, 4);
            // réservoir : moitié « x » (lignes 8 à 11), moitié « y » (12 à 15)
            float* b5 = b[8]; float* b6 = b[9]; float* b7 = b[10]; float* b8 = b[11];
            float* b9 = b[12]; float* b10 = b[13]; float* b11 = b[14]; float* b12 = b[15];
            const int m5 = m[8], m6 = m[9], m7 = m[10], m8 = m[11], m9 = m[12], m10 = m[13], m11 = m[14], m12 = m[15];
            const int w5 = w[8], w6 = w[9], w7 = w[10], w8 = w[11], w9 = w[12], w10 = w[13], w11 = w[14], w12 = w[15];
            const double fbL = b8[(w8 - L[11]) & m8] * decay;
            const double fbR = b12[(w12 - L[15]) & m12] * decay;
            double x = vR + fbR;
            // passe-tout modulés de Dattorro : lecture par interpolation passe-tout (1.x : linéaire, qui perdait des aigus à 44,1 / 48 kHz)
            double df = L[8] + exc * std::sin (s_ph); int k0 = (int) (df - 0.5); double eta = df - k0, ac = (1 - eta) / (1 + eta);
            double d = ac * ((double) b5[(w5 - k0) & m5] - apX) + b5[(w5 - k0 - 1) & m5]; apX = d;
            double u = x - g1 * d; b5[w5] = (float) u; x = d + g1 * u;
            b6[w6] = (float) x; x = b6[(w6 - L[9]) & m6];
            { const double yv = db0 * x + db1 * s_xl + dp * s_dl; s_xl = x; s_dl = yv; x += dw * (yv - x); }
            x *= decay;
            { const double yv = l0 * x + l1 * q0 + l2 * q1 - la1 * q2 - la2 * q3; q1 = q0; q0 = x; q3 = q2; q2 = yv; x = yv; }
            d = b7[(w7 - L[10]) & m7]; u = x - g2 * d; b7[w7] = (float) u; x = d + g2 * u;
            b8[w8] = (float) x;
            double y = vL + fbL;
            df = L[12] + exc * std::cos (s_ph); k0 = (int) (df - 0.5); eta = df - k0; ac = (1 - eta) / (1 + eta);
            d = ac * ((double) b9[(w9 - k0) & m9] - apYv) + b9[(w9 - k0 - 1) & m9]; apYv = d;
            u = y - g1 * d; b9[w9] = (float) u; y = d + g1 * u;
            b10[w10] = (float) y; y = b10[(w10 - L[13]) & m10];
            { const double yv = db0 * y + db1 * s_xr + dp * s_dr; s_xr = y; s_dr = yv; y += dw * (yv - y); }
            y *= decay;
            { const double yv = l0 * y + l1 * q4 + l2 * q5 - la1 * q6 - la2 * q7; q5 = q4; q4 = y; q7 = q6; q6 = yv; y = yv; }
            d = b11[(w11 - L[14]) & m11]; u = y - g2 * d; b11[w11] = (float) u; y = d + g2 * u;
            b12[w12] = (float) y;
            const double yl = 0.6 * ((double) b10[(w10 - T[0]) & m10] + b10[(w10 - T[1]) & m10] - b11[(w11 - T[2]) & m11]
                                   + b12[(w12 - T[3]) & m12] - b6[(w6 - T[4]) & m6] - b7[(w7 - T[5]) & m7] - b8[(w8 - T[6]) & m8]);
            const double yr = 0.6 * ((double) b6[(w6 - T[7]) & m6] + b6[(w6 - T[8]) & m6] - b7[(w7 - T[9]) & m7]
                                   + b8[(w8 - T[10]) & m8] - b10[(w10 - T[11]) & m10] - b11[(w11 - T[12]) & m11] - b12[(w12 - T[13]) & m12]);
            outL[i] = (float) yl; outR[i] = (float) yr;
            s_ph += dph; if (s_ph > TWO_PI) s_ph -= TWO_PI;
            for (int j = 0; j < NB; ++j) w[j] = (w[j] + 1) & m[j];
        }
        for (int i = 0; i < NB; ++i) W[i] = w[i];
        bwL = sbL; bwR = sbR; dpL = s_dl; dpR = s_dr; dxL = s_xl; dxR = s_xr; ph = s_ph;
        loS[0] = q0; loS[1] = q1; loS[2] = q2; loS[3] = q3; loS[4] = q4; loS[5] = q5; loS[6] = q6; loS[7] = q7; apS[0] = apX; apS[1] = apYv;
        if (! (std::abs (dpL) < 1e6) || ! (std::abs (dpR) < 1e6)) reset();
    }

private:
    void clearState() { bwL = bwR = dpL = dpR = dxL = dxR = ph = 0; std::fill (std::begin (loS), std::end (loS), 0.0); apS[0] = apS[1] = 0; for (auto& x : W) x = 0; }
    // longueurs de Dattorro à 29 761 Hz : diffuseurs d'entrée (deux fois : L puis R), puis le réservoir
    static constexpr double base[NB] = { 142, 107, 379, 277, 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
    static constexpr double tapBase[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121 };
    double sr, k;
    std::vector<float> B[NB];
    int M[NB] {}, W[NB] {}, D[NB] {}, T[14] {};
    P p;
    static constexpr int NCP = 11;   // decay, dd2, amortissement (b0, b1, p, part), étagère Bass (5)
    double ca[NCP] {}, cb[NCP] {}; bool ramp = false; double invTick = 1.0 / 128;
    double bw = 0, exc = 0, dph = 0;
    double bwL = 0, bwR = 0, dpL = 0, dpR = 0, dxL = 0, dxR = 0, ph = 0, loS[8] {}, apS[2] {};
    Clearer clr;
};

// ═══════════ Pièces : réflexions précoces + diffuseurs + FDN 16 lignes (Chamber, Room, Hall, Church) ═══════════
class RoomCore
{
public:
    struct Img { double t, g, p; int ord, idx; };
    struct Info { double dims[3], V, S, mfp, win, mean; int nT, steps; double delaysMs[16], diffMs[4]; };
    static constexpr int NL = 16, MAXT = 48;
    static constexpr double Tfreeze = 1e5;   // « RT60 » d'une queue gelée (s)

    explicit RoomCore (double sampleRate) : sr (sampleRate)
    {
        LN = p2 ((int) std::ceil (0.32 * sr)); lm = LN - 1;
        for (auto& l : lines) l.assign ((size_t) LN, 0.0f);
        // diffuseurs : longueur maximale de chaque étape (tous les modes, Size 100 %)
        static constexpr double maxMs[4] = { 10, 24, 50, 60 };
        dmMax = 0;
        for (int s = 0; s < 4; ++s) { const int n = p2 ((int) std::ceil (maxMs[s] * 0.001 * sizeScale (1) * sr) + 4); dmS[s] = n - 1; dmMax = std::max (dmMax, n - 1); }
        for (int h = 0; h < 2; ++h) for (int s = 0; s < 4; ++s) for (int j = 0; j < 8; ++j) dif[(size_t) ((h * 4 + s) * 8 + j)].assign ((size_t) dmS[s] + 1, 0.0f);
        EN = p2 ((int) std::ceil (0.205 * sr) + 4); em = EN - 1;
        for (auto& e : eb) e.assign ((size_t) EN, 0.0f);
        for (int j = 0; j < NL; ++j) { osc[j][0] = 0; osc[j][1] = 1; }
    }
    void reset()
    {
        for (auto& b : lines) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : dif) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : eb) std::fill (b.begin(), b.end(), 0.0f);
        clearState();
    }
    bool clearSome (size_t& budget)
    {
        const int n = NL + 64 + 2;
        if (! clr.step (n, [this] (int i) -> std::vector<float>& { return i < NL ? lines[(size_t) i] : i < NL + 64 ? dif[(size_t) (i - NL)] : eb[(size_t) (i - NL - 64)]; }, budget))
            return false;
        clearState(); return true;
    }

    // Méthode des sources-images (Allen et Berkley, 1979) sur une pièce parallélépipédique, sans allocation.
    // Garde les maxTaps réflexions les plus fortes (tri sur place, départage par ordre de calcul), rangées par temps.
    void images (const double* dims, const double* S, const double* R, double refl, int maxOrder, double win, int maxTaps, Img* out, int& nOut)
    {
        const double Lx = dims[0], Ly = dims[1], Lz = dims[2], c = 343;
        const double d0 = jsHypot (S[0] - R[0], S[1] - R[1], S[2] - R[2]);
        const int N = maxOrder;
        int nCand = 0;
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
                    {
                        cand[(size_t) nCand] = { t, (d0 / d) * std::pow (refl, ord), dx / std::max (1e-9, jsHypot (dx, dy)), ord, nCand };
                        ++nCand;
                    }
                }
            }
        }
        nOut = std::min (nCand, maxTaps);
        std::partial_sort (cand.begin(), cand.begin() + nOut, cand.begin() + nCand,
                           [] (const Img& a, const Img& b) { return a.g > b.g || (same (a.g, b.g) && a.idx < b.idx); });
        std::copy (cand.begin(), cand.begin() + nOut, out);
        std::sort (out, out + nOut, [] (const Img& a, const Img& b) { return a.t < b.t || (same (a.t, b.t) && a.idx < b.idx); });
    }

    void configure (const ModeDef& def, double size)
    {
        const double s = sizeScale (size);
        double dims[3]; for (int i = 0; i < 3; ++i) dims[i] = def.dims[i] * s;
        const double V = dims[0] * dims[1] * dims[2], S = 2 * (dims[0] * dims[1] + dims[0] * dims[2] + dims[1] * dims[2]), mfp = 4 * V / S;
        const double win = std::min (0.2, std::max (0.03, (std::sqrt (V) * 1.3 + 20) / 1000));
        // deux sources (gauche, droite), symétriques autour de l'auditeur, comme deux enceintes devant lui
        const double R[3] = { 0.43 * dims[0], 0.64 * dims[1], std::min (1.6, 0.45 * dims[2]) };
        const double Sz = std::min (2.0, 0.5 * dims[2]), Sy = 0.3 * dims[1], a = 0.2 * dims[0];
        const double SL[3] = { R[0] - a, Sy, Sz }, SR[3] = { R[0] + a, Sy, Sz };
        images (dims, SL, R, 0.92, def.order, win, MAXT, imL.data(), nTL);
        images (dims, SR, R, 0.92, def.order, win, MAXT, imR.data(), nTR);
        for (int q = 0; q < nTL; ++q) tTL[q] = std::max (1, (int) jsRound (imL[(size_t) q].t * sr));
        for (int q = 0; q < nTR; ++q) tTR[q] = std::max (1, (int) jsRound (imR[(size_t) q].t * sr));
        // deux diffuseurs (un par entrée) : étapes de délais + inversions de polarité + permutation + Hadamard (Signalsmith 2021)
        steps = def.nDiff;
        double dsum = 0;
        for (int h = 0; h < 2; ++h)
        {
            Rng rnd (def.seed + 7919u * (uint32_t) h);
            for (int stp = 0; stp < steps; ++stp)
            {
                const double Dl = def.diff[stp] * 0.001 * s * sr; if (h == 0) dsum += Dl;
                int perm[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                for (int j = 7; j > 0; --j) { const int r = (int) std::floor (rnd.next() * (j + 1)); std::swap (perm[j], perm[r]); }
                for (int j = 0; j < 8; ++j)
                {
                    const int kk = (h * 4 + stp) * 8 + j;
                    dlen[kk] = std::max (1, std::min (dmS[stp] - 1, (int) std::floor (Dl * (j + rnd.next()) / 8)));
                    dsg[kk] = rnd.next() < 0.5 ? -1.0f : 1.0f; dperm[kk] = perm[j];
                }
            }
        }
        // FDN 16 lignes : moyenne >= libre parcours moyen 4V/S (Jot), étalement x0,71..x1,41, longueurs premières distinctes ;
        // moitié gauche = lignes 0 à 7, moitié droite = 8 à 15, longueurs réparties pour que les deux moitiés aient la même moyenne
        const double mean = std::max (mfp / 343, def.minMean * s);
        int sorted[NL]; int nUsed = 0;
        for (int j = 0; j < NL; ++j)
        {
            int nn = std::max (8, (int) jsRound (mean * std::pow (2.0, j / 15.0 - 0.5) * sr));
            auto isUsed = [&] (int v) { for (int q = 0; q < nUsed; ++q) if (sorted[q] == v) return true; return false; };
            while (! isPrime (nn) || isUsed (nn)) ++nn;
            sorted[nUsed++] = nn;
        }
        static constexpr int toLeft[8] = { 0, 3, 4, 7, 8, 11, 12, 15 }, toRight[8] = { 1, 2, 5, 6, 9, 10, 13, 14 };
        static constexpr int order[8] = { 0, 5, 2, 7, 4, 1, 6, 3 };
        for (int q = 0; q < 8; ++q) { len[order[q]] = sorted[toLeft[q]]; len[8 + order[q]] = sorted[toRight[q]]; }
        // modulation : toutes les lignes, vitesses toutes différentes (jamais en phase), profondeur en ms (même effet à toute fréquence)
        depth = def.mod[0] * 0.001 * sr;
        for (int j = 0; j < NL; ++j)
        {
            const double w = TWO_PI * def.mod[1] * (0.62 + 0.0647 * ((j * 7) % NL)) / sr;
            rot[j][0] = std::sin (w); rot[j][1] = std::cos (w);
            osc[j][0] = std::sin (j * 0.9); osc[j][1] = std::cos (j * 0.9);
        }
        hpC = ! same (def.hp, 0) ? hp1 (def.hp, sr) : Coef1 {};
        lpC = ! same (def.lp, 0) ? lp1Matched (def.lp, sr) : Coef1 {};
        useHp = ! same (def.hp, 0); useLp = ! same (def.lp, 0);
        gV = V; gS = S; gWin = win; gMean = mean; gDsum = dsum / sr;
        info.V = V; info.S = S; info.mfp = mfp; info.win = win; info.mean = mean; info.nT = nTL + nTR; info.steps = steps;
        for (int i = 0; i < 3; ++i) info.dims[i] = dims[i];
        for (int j = 0; j < NL; ++j) info.delaysMs[j] = sorted[j] / sr * 1000;
        for (int i = 0; i < 4; ++i) info.diffMs[i] = i < steps ? def.diff[i] * s : 0;
    }

    // decay : RT60 du médium ; bassMul : multiplicateur du RT60 grave (réglage Bass) ; freeze : 0 → 1 (queue gelée)
    void setDecay (const ModeDef& def, double decay, double dampAmt, double bassMul, double freeze)
    {
        const double T = std::max (0.05, decay), hi = std::max (0.2, 0.95 - 0.75 * dampAmt), f = std::clamp (freeze, 0.0, 1.0);
        const double iT = (1 - f) / T + f / Tfreeze, iTlo = (1 - f) / (T * def.bass * bassMul) + f / Tfreeze, iThi = (1 - f) / (T * hi) + f / Tfreeze;
        const double cap = 0.9995 + f * 0.00049;
        // filtres d'absorption par ligne (Jot) : gain médium + étagère grave du mode (1er ordre, comme la 1.x)
        // + étagère du réglage Bass (2e ordre, 200 Hz) + étagère aiguë
        const double iTc = (1 - f) / (T * def.bass) + f / Tfreeze;
        for (int j = 0; j < NL; ++j)
        {
            const double L = len[j], gm = std::pow (10.0, -3 * L * iT / sr), gc = std::pow (10.0, -3 * L * iTc / sr);
            const double glo = std::pow (10.0, -3 * L * iTlo / sr), ghi = std::pow (10.0, -3 * L * iThi / sr);
            double* o = cfB + j * 12;
            shelfLoM (gc / gm, def.fLo, sr, o + 1); shelfLo2 (glo / gc, bassXover, sr, o + 4); shelfHiM (ghi / gm, def.fHi, sr, o + 9);
            // les étagères débordent un peu sur le médium : on recale le gain pour que le RT60 à 500 Hz soit exact
            // (1.x : 600 Hz ; 2.0.0 : 500 Hz, la fréquence où Decay est défini et mesuré)
            const double comp = mag1 (o + 1, 500, sr) * mag2 (o + 4, 500, sr) * mag1 (o + 9, 500, sr);
            o[0] = std::min (gm / comp, cap / std::max (1.0, glo / gm));
        }
        // réflexions : coefficient des parois déduit du RT60 (Eyring), niveaux calés sur une seule enveloppe exponentielle ;
        // chaque source porte la moitié de l'énergie (une entrée mono garde le niveau de la 1.x)
        const double refl = std::min (0.985, std::exp (-0.0805 * gV / (gS * T)));
        auto level = [&] (const std::array<Img, MAXT>& im, int nT, float* gl, float* gr, Tap* tp)
        {
            double e = 0, raw[MAXT];
            for (int q = 0; q < nT; ++q) { const double g = im[(size_t) q].g * std::pow (refl / 0.92, im[(size_t) q].ord); raw[q] = g; e += g * g; }
            const double norm = def.er * 0.7071067811865476 / std::sqrt (! same (e, 0) ? e : 1);
            for (int q = 0; q < nT; ++q)
            {
                const double g = raw[q] * norm;
                gl[q] = (float) (g * std::sqrt (1 - im[(size_t) q].p)); gr[q] = (float) (g * std::sqrt (1 + im[(size_t) q].p));
                tp[q] = { im[(size_t) q].t, g };
            }
        };
        level (imL, nTL, tLL, tLR, taps); level (imR, nTR, tRL, tRR, taps + nTL);
        const double D0 = def.er * def.er / (sr * T / 13.82 * (1 - std::exp (-13.82 * gWin / T)));
        const double t0 = 0.5 * gDsum + 0.8 * gMean, tdc = 0.5 * gDsum;
        lateG = def.late * std::sqrt (8 * gMean * sr * D0) * std::exp (-6.91 * t0 / T);
        difG = def.difOut * std::sqrt (D0 * std::max (1.0, gDsum * sr)) * std::exp (-6.91 * tdc / T);
        const Coef1 eC = lp1Matched (16000 * (1 - 0.7 * dampAmt), sr);
        cfB[NL * 12] = eC.b0; cfB[NL * 12 + 1] = eC.b1; cfB[NL * 12 + 2] = eC.p;
        hiRatio = hi; reflCoef = refl;
    }

    // Rampe linéaire sur une tranche de 128 échantillons : valeur = a + b (k + 1), k = rang dans la tranche
    struct Ramp { double a = 1, b = 0; double at (int k) const { return a + b * (k + 1); } };
    // Début d'une tranche, avant setDecay : les coefficients atteints deviennent le point de départ de la rampe
    void preTick()
    {
        std::copy (std::begin (cfB), std::end (cfB), std::begin (cfA));
        std::copy (tLL, tLL + MAXT, eA[0]); std::copy (tLR, tLR + MAXT, eA[1]); std::copy (tRL, tRL + MAXT, eA[2]); std::copy (tRR, tRR + MAXT, eA[3]);
    }
    // Après setDecay : rampes de la tranche (coefficients des boucles interpolés à chaque échantillon s'ils ont changé)
    void postTick (int tickLen)
    {
        // oscillateurs de modulation renormalisés une fois par tranche (pas de dérive d'amplitude)
        for (auto& os : osc) { const double m = 1 / std::sqrt (os[0] * os[0] + os[1] * os[1]); os[0] *= m; os[1] *= m; }
        const double lg1 = lateG * 0.35355339059327373, dg1 = difG * 0.35355339059327373;
        lgR = { lgPrev, (lg1 - lgPrev) / tickLen }; dgR = { dgPrev, (dg1 - dgPrev) / tickLen };
        lgPrev = lg1; dgPrev = dg1;
        ramp = ! std::equal (std::begin (cfA), std::end (cfA), std::begin (cfB), [] (double x, double y) { return same (x, y); });
        erRamp = ! (std::equal (tLL, tLL + nTL, eA[0]) && std::equal (tLR, tLR + nTL, eA[1]) && std::equal (tRL, tRL + nTR, eA[2]) && std::equal (tRR, tRR + nTR, eA[3]));
        invTick = 1.0 / tickLen;
    }
    // tk : rang du premier échantillon dans la tranche ; ge, gl : gains des réflexions précoces et de la queue (Early / Late)
    void process (const float* xL, const float* xR, float* oL, float* oR, int n, int tk, const Ramp& geR, const Ramp& glR)
    {
        const double inj = 0.35355339059327373;
        float* eLb = eb[0].data(); float* eRb = eb[1].data();
        int lw_ = lw, dw_ = dw, ew_ = ew;
        double eL_ = eL, eR_ = eR, ex1L = exL, ex1R = exR;
        double cA[8], cB[8], tmp[8], o[NL], u[8], v8[8];
        const double rc = 0.9238795325112867, rs = 0.3826834323650898;   // rotation de 22,5° entre les deux moitiés
        for (int i = 0; i < n; ++i)
        {
            const double xl = xL[i], xr = xR[i];
            // coefficients du moment : interpolés entre l'ancien et le nouveau réglage pendant une rampe (pas d'escalier)
            const double* cf = cfB;
            if (ramp)
            {
                const double t = (tk + i + 1) * invTick;
                for (int c = 0; c < NC; ++c) cfI[c] = cfA[c] + (cfB[c] - cfA[c]) * t;
                cf = cfI;
            }
            const double eb0 = cf[NL * 12], eb1 = cf[NL * 12 + 1], ep = cf[NL * 12 + 2];
            // réflexions précoces : source gauche et source droite
            eLb[ew_] = (float) xl; eRb[ew_] = (float) xr;
            double el = 0, er = 0;
            if (! erRamp)
            {
                for (int q = 0; q < nTL; ++q) { const double v = eLb[(ew_ - tTL[q]) & em]; el += v * tLL[q]; er += v * tLR[q]; }
                for (int q = 0; q < nTR; ++q) { const double v = eRb[(ew_ - tTR[q]) & em]; el += v * tRL[q]; er += v * tRR[q]; }
            }
            else
            {
                // pendant une rampe de Decay / Damp : gains des réflexions interpolés à chaque échantillon
                const double t = (tk + i + 1) * invTick;
                for (int q = 0; q < nTL; ++q) { const double v = eLb[(ew_ - tTL[q]) & em]; el += v * (eA[0][q] + (tLL[q] - eA[0][q]) * t); er += v * (eA[1][q] + (tLR[q] - eA[1][q]) * t); }
                for (int q = 0; q < nTR; ++q) { const double v = eRb[(ew_ - tTR[q]) & em]; el += v * (eA[2][q] + (tRL[q] - eA[2][q]) * t); er += v * (eA[3][q] + (tRR[q] - eA[3][q]) * t); }
            }
            { const double y = eb0 * el + eb1 * ex1L + ep * eL_; ex1L = el; eL_ = y; }
            { const double y = eb0 * er + eb1 * ex1R + ep * eR_; ex1R = er; eR_ = y; }
            ew_ = (ew_ + 1) & em;
            // diffuseurs (gauche : L, droite : R)
            for (int j = 0; j < 8; ++j) { cA[j] = xl; cB[j] = xr; }
            for (int h = 0; h < 2; ++h)
            {
                double* c = h == 0 ? cA : cB;
                for (int s = 0; s < steps; ++s)
                {
                    const int b = (h * 4 + s) * 8, msk = dmS[s], wi = dw_ & msk;
                    for (int j = 0; j < 8; ++j) { float* buf = dif[(size_t) (b + j)].data(); buf[wi] = (float) c[j]; tmp[j] = (double) buf[(wi - dlen[b + j]) & msk] * dsg[b + j]; }
                    for (int j = 0; j < 8; ++j) c[j] = tmp[dperm[b + j]];
                    had8 (c);
                }
            }
            dw_ = (dw_ + 1) & dmMax;
            // FDN : lecture modulée (interpolation passe-tout du 1er ordre : aucune perte d'aigus, quelle que soit
            // la fréquence d'échantillonnage), filtres d'absorption (Jot)
            double yl = 0, yr = 0;
            for (int j = 0; j < NL; ++j)
            {
                const float* bl = lines[(size_t) j].data();
                double* os = osc[j];
                const double sn = os[0], cs = os[1];
                os[0] = sn * rot[j][1] + cs * rot[j][0]; os[1] = cs * rot[j][1] - sn * rot[j][0];
                const double df = len[j] + depth * sn; const int k0 = (int) (df - 0.5); const double eta = df - k0, ac = (1 - eta) / (1 + eta);
                double v = ac * ((double) bl[(lw_ - k0) & lm] - apY[j]) + bl[(lw_ - k0 - 1) & lm];
                apY[j] = v;
                const double* q = cf + j * 12; double* r = st + j * 8;
                const double y0 = q[1] * v + q[2] * r[0] - q[3] * r[1]; r[0] = v; r[1] = y0;
                const double y1 = q[4] * y0 + q[5] * r[2] + q[6] * r[3] - q[7] * r[4] - q[8] * r[5]; r[3] = r[2]; r[2] = y0; r[5] = r[4]; r[4] = y1;
                const double y2 = q[9] * y1 + q[10] * r[6] - q[11] * r[7]; r[6] = y1; r[7] = y2;
                v = y2 * q[0]; o[j] = v;
            }
            for (int j = 0; j < 8; ++j)
            {
                yl += (j & 1) ? -o[j] : o[j]; yr += (j & 2) ? -o[8 + j] : o[8 + j];
                u[j] = rc * o[j] + rs * o[8 + j]; v8[j] = rc * o[8 + j] - rs * o[j];
            }
            had8 (u); had8 (v8);
            for (int j = 0; j < 8; ++j)
            {
                lines[(size_t) j][(size_t) lw_] = (float) (u[j] + cA[j] * inj);
                lines[(size_t) (8 + j)][(size_t) lw_] = (float) (v8[j] + cB[j] * inj);
            }
            lw_ = (lw_ + 1) & lm;
            const double dl = cA[0] - cA[1] + cA[2] - cA[3] + cA[4] - cA[5] + cA[6] - cA[7];
            const double dr = cB[0] + cB[1] - cB[2] - cB[3] + cB[4] + cB[5] - cB[6] - cB[7];
            const int kk = tk + i;
            const double lg = lgR.at (kk), dg = dgR.at (kk), ge = geR.at (kk), gl = glR.at (kk);
            double L = ge * eL_ + gl * (lg * yl + dg * dl), Rr = ge * eR_ + gl * (lg * yr + dg * dr);
            if (useHp) { const double y = hpC.b0 * L + hpC.b1 * fo[0] + hpC.p * fo[1]; fo[0] = L; fo[1] = y; L = y;
                         const double z = hpC.b0 * Rr + hpC.b1 * fo[2] + hpC.p * fo[3]; fo[2] = Rr; fo[3] = z; Rr = z; }
            if (useLp) { const double y = lpC.b0 * L + lpC.b1 * fo[4] + lpC.p * fo[5]; fo[4] = L; fo[5] = y; L = y;
                         const double z = lpC.b0 * Rr + lpC.b1 * fo[6] + lpC.p * fo[7]; fo[6] = Rr; fo[7] = z; Rr = z; }
            oL[i] = (float) L; oR[i] = (float) Rr;
            lastO0 = o[0];
        }
        lw = lw_; dw = dw_; ew = ew_; eL = eL_; eR = eR_; exL = ex1L; exR = ex1R;
        if (! (std::abs (lastO0) < 1e6)) reset();
    }
    // Gains de sortie appliqués d'emblée (premier bloc après configure, sans rampe depuis l'ancien réglage)
    void snapGains()
    {
        lgPrev = lateG * 0.35355339059327373; dgPrev = difG * 0.35355339059327373; lgR = { lgPrev, 0 }; dgR = { dgPrev, 0 };
        std::copy (std::begin (cfB), std::end (cfB), std::begin (cfA)); ramp = false; erRamp = false;
    }

    int numTaps() const { return nTL + nTR; }
    const Tap* tapList() const { return taps; }
    Info info {};
    double hiRatio = 0, reflCoef = 0;

private:
    void clearState()
    {
        std::fill (std::begin (st), std::end (st), 0.0); std::fill (std::begin (fo), std::end (fo), 0.0); std::fill (std::begin (apY), std::end (apY), 0.0);
        eL = eR = exL = exR = 0; lastO0 = 0; lw = dw = ew = 0;
    }
    double sr;
    int LN = 0, lm = 0, lw = 0, dmS[4] {}, dmMax = 0, dw = 0, EN = 0, em = 0, ew = 0;
    std::array<std::vector<float>, NL> lines;
    std::array<std::vector<float>, 64> dif;
    std::array<std::vector<float>, 2> eb;
    static constexpr int NC = NL * 12 + 3;   // coefficients interpolés : filtres d'absorption des 16 lignes + passe-bas des réflexions
    int len[NL] {}; double cfA[NC] {}, cfB[NC] {}, cfI[NC] {}; bool ramp = false; double invTick = 1.0 / 128;
    double st[NL * 8] {}, apY[NL] {};
    int dlen[64] {}; float dsg[64] {}; int dperm[64] {}; int steps = 4;
    int tTL[MAXT] {}, tTR[MAXT] {}; float tLL[MAXT] {}, tLR[MAXT] {}, tRL[MAXT] {}, tRR[MAXT] {}; int nTL = 0, nTR = 0;
    float eA[4][MAXT] {}; bool erRamp = false;   // gains des réflexions au début de la tranche (rampe)
    Tap taps[2 * MAXT] {};
    double osc[NL][2] {}, rot[NL][2] {}, depth = 0;
    Coef1 hpC, lpC; bool useHp = false, useLp = false;
    double eL = 0, eR = 0, exL = 0, exR = 0, fo[8] {};
    double lateG = 1, difG = 0.5, lgPrev = 0, dgPrev = 0, lastO0 = 0;
    Ramp lgR, dgR;
    double gV = 1, gS = 1, gWin = 0.03, gMean = 0.01, gDsum = 0;
    std::array<Img, 2744> cand {};
    std::array<Img, MAXT> imL {}, imR {};
    Clearer clr;
};

// ══════════ Ressort : chaîne de passe-tout étirés dans une boucle (Välimäki, Parker, Abel 2010) ══════════
// Entrée mono, comme un vrai ressort (un seul transducteur d'entrée) ; deux ressorts différents donnent la stéréo.
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
        }
        initState();
        modA = 1 - std::pow (1 - 0.0004, 48000 / sr);   // lissage de la modulation : même vitesse à toute fréquence
        // passe-bas de sortie à fC (biquad RBJ, Q 0,707)
        const double w0 = TWO_PI * std::min (fC * 0.95, sr * 0.45) / sr, al = std::sin (w0) / (2 * 0.7071), cs = std::cos (w0), a0 = 1 + al;
        lb[0] = (1 - cs) / 2 / a0; lb[1] = (1 - cs) / a0; lb[2] = (1 - cs) / 2 / a0; lb[3] = -2 * cs / a0; lb[4] = (1 - al) / a0;
        hpC = hp1 (90, sr);
    }
    void reset()
    {
        for (auto& S : s)
        {
            std::fill (S.ap.begin(), S.ap.end(), 0.0f); std::fill (S.aph.begin(), S.aph.end(), 0.0f);
            std::fill (S.dl.begin(), S.dl.end(), 0.0f); std::fill (S.dh.begin(), S.dh.end(), 0.0f);
        }
        initState();
    }
    bool clearSome (size_t& budget)
    {
        if (! clr.step (8, [this] (int i) -> std::vector<float>& { auto& S = s[i / 4]; const int q = i % 4; return q == 0 ? S.ap : q == 1 ? S.aph : q == 2 ? S.dl : S.dh; }, budget))
            return false;
        initState(); return true;
    }
    double gd (double c, int kk, double f) const { const double om = TWO_PI * f / sr; return kk * (1 - c * c) / (1 + 2 * c * std::cos (kk * om) + c * c); }
    void configure (double size)
    {
        const double Td = (0.03 + 0.04 * size) * sr, ratio[2] = { 1, 47.3 / 41 }, ratioH[2] = { 1, 21.7 / 17.9 };
        for (int i = 0; i < 2; ++i) { s[i].Td = Td * ratio[i]; s[i].Th = Td * 0.437 * ratioH[i]; }
        const double chain = M * gd (a, k, 707) / sr;
        for (int r = 1; r <= 8; ++r) taps[r - 1] = { r * (Td / sr + chain), std::pow (0.72, r - 1) };
        info = { Td / sr * 1000, chain * 1000, k, fC };
    }
    void setDecay (double decay, double dampAmt, double freeze)
    {
        const double T = std::max (0.05, decay), gl = M * gd (a, k, 707), gh = MH * gd (ah, 1, 707), f = std::clamp (freeze, 0.0, 1.0);
        const double iT = (1 - f) / T + f / 1e5, iTh = (1 - f) / (T * 0.6) + f / 1e5;
        dC = lp1Matched (std::min (9000 * std::pow (0.3, dampAmt), sr * 0.45), sr);
        dW = 1 - f;   // queue gelée : le filtre de la boucle devient transparent
        // module du filtre de boucle (mélangé) vers 500 Hz, compensé dans le gain de boucle
        const double om = TWO_PI * 500 / sr, cw = std::cos (om), sw = std::sin (om);
        const double nr = dC.b0 + dC.b1 * cw, ni = -dC.b1 * sw, dr = 1 - dC.p * cw, di = dC.p * sw, den = dr * dr + di * di;
        const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
        const double br = 1 - dW + dW * hr, bi = dW * hi, lpMag = std::sqrt (br * br + bi * bi);
        const double cap = 0.999 + f * 0.00099;
        for (int i = 0; i < 2; ++i)
        {
            cb[i] = std::min (cap, std::pow (10.0, -3 * (s[i].Td + gl) * iT / sr) / lpMag);
            cb[2 + i] = std::min (cap, std::pow (10.0, -3 * (s[i].Th + gh) * iTh / sr));
        }
        cb[4] = dC.b0; cb[5] = dC.b1; cb[6] = dC.p; cb[7] = dW;
    }
    void preTick() { std::copy (std::begin (cb), std::end (cb), std::begin (ca)); }
    void postTick (int tickLen)
    {
        ramp = ! std::equal (std::begin (ca), std::end (ca), std::begin (cb), [] (double x, double y) { return same (x, y); });
        invTick = 1.0 / tickLen;
    }
    void snapCoefs() { std::copy (std::begin (cb), std::end (cb), std::begin (ca)); ramp = false; }
    void process (const float* x, float* oL, float* oR, int n, int tk)
    {
        int w_ = w, ki_ = ki;
        double c[8]; std::copy (std::begin (cb), std::end (cb), c);
        for (int i = 0; i < n; ++i)
        {
            if (ramp) { const double t = (tk + i + 1) * invTick; for (int q = 0; q < 8; ++q) c[q] = ca[q] + (cb[q] - ca[q]) * t; }
            const double db0 = c[4], db1 = c[5], dp = c[6], dw = c[7];
            const double xi = x[i];
            for (int sp = 0; sp < 2; ++sp)
            {
                auto& S = s[sp]; float* ap = S.ap.data();
                // boucle basse fréquence : 100 passe-tout étirés (dispersion = chirps)
                double v = xi + S.fb;
                for (int m = 0, q = ki_; m < M; ++m, q += k) { const double d = ap[q], u = v - a * d; ap[q] = (float) u; v = d + a * u; }
                { const double y = db0 * v + db1 * S.lx + dp * S.lp; S.lx = v; S.lp = y; }
                const double lpo = v + dw * (S.lp - v);
                S.dl[(size_t) w_] = (float) lpo;
                if (--S.cnt <= 0) { S.cnt = (int) jsRound (sr * 0.23); S.modT = (S.r.next() * 2 - 1) * 8 * sr / 48000; }
                S.mod += modA * (S.modT - S.mod);
                const double df = S.Td + S.mod; const int k0 = (int) (df - 0.5); const double eta = df - k0, ac = (1 - eta) / (1 + eta);
                const double y = ac * ((double) S.dl[(size_t) ((w_ - k0) & dm)] - S.apy) + S.dl[(size_t) ((w_ - k0 - 1) & dm)]; S.apy = y;
                S.fb = y * c[sp];
                // boucle haute fréquence : 40 passe-tout du 1er ordre
                double h = xi + S.fbh; float* aph = S.aph.data();
                for (int m = 0; m < MH; ++m) { const double d = aph[m], u = h - ah * d; aph[m] = (float) u; h = d + ah * u; }
                S.dh[(size_t) w_] = (float) h;
                const double yh = S.dh[(size_t) ((w_ - (int) jsRound (S.Th)) & dm)]; S.fbh = yh * c[2 + sp];
                // sortie : passe-bas à fC + voie aiguë à -14 dB, passe-haut 90 Hz
                const double yo = lb[0] * y + S.z1; S.z1 = lb[1] * y - lb[3] * yo + S.z2; S.z2 = lb[2] * y - lb[4] * yo;
                double out = yo + 0.2 * yh;
                { const double z = hpC.b0 * out + hpC.b1 * S.hx + hpC.p * S.hp; S.hx = out; S.hp = z; out = z; }
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
        double fb = 0, fbh = 0, lp = 0, lx = 0, z1 = 0, z2 = 0, hp = 0, hx = 0, mod = 0, modT = 0, Td = 0, Th = 0, apy = 0;
        int cnt = 0; Rng r { 0 };
    };
    void initState()
    {
        for (int i = 0; i < 2; ++i)
        {
            auto& S = s[i];
            S.fb = S.fbh = S.lp = S.lx = S.z1 = S.z2 = S.hp = S.hx = S.mod = S.modT = S.apy = 0;
            S.cnt = (int) std::lround (i * 4000 * sr / 48000); S.r = Rng ((uint32_t) (91 + i * 17));
        }
        w = 0; ki = 0;
    }
    double sr; static constexpr int M = 100, MH = 40; static constexpr double a = 0.6, ah = -0.6;
    int k = 1; double fC = 0; int DN = 0, dm = 0, w = 0, ki = 0;
    Spring s[2];
    double lb[5] {}; Coef1 hpC, dC; double dW = 1, modA = 0.0004;
    double ca[8] {}, cb[8] {}; bool ramp = false; double invTick = 1.0 / 128;   // gains de boucle (2 + 2), filtre de boucle (4)
    Tap taps[8] {};
    Clearer clr;
};

// ══════════════════════ Moteur complet du plugin ══════════════════════
// Deux « emplacements » complets (Room + Plate + Spring chacun) : l'un reçoit le son, l'autre finit sa queue.
// Changer de mode ou de Size prépare l'emplacement libre, puis l'entrée passe de l'un à l'autre en 20 ms :
// rien n'est coupé, le moteur sortant s'éteint de lui-même (ou en 30 ms s'il faut le réutiliser pour un autre changement).
// Tous les réglages sont mis à jour sur une grille fixe de 128 échantillons : la sortie ne dépend pas des blocs de l'hôte.
class ReverbEngine
{
public:
    struct Params
    {
        int mode = chamber; double predelay = 0, decay = 1.5, mix = 0.3, size = 0.4, damp = 0.5, width = 0.8;
        // 2.0.0 (réglages avancés) : valeurs par défaut neutres
        double lowCut = 20, highCut = 20000, bass = 1, erLate = 0, ducking = 0; bool freeze = false;
    };
    static constexpr int TICK = 128;
    using Ramp = RoomCore::Ramp;

    explicit ReverbEngine (double sampleRate)
        : sr (sampleRate), slots { Slot (sampleRate), Slot (sampleRate) }
    {
        PN = p2 ((int) std::ceil (0.26 * sr) + 4); pm = PN - 1;
        pbL.assign ((size_t) PN, 0.0f); pbR.assign ((size_t) PN, 0.0f);
        tin = 1 / (0.020 * sr); tkill = 1 / (0.030 * sr); tpd = 1 / (0.030 * sr);
        aSm = 1 - std::exp (-1 / (0.0104 * sr));
        dAtt = 1 - std::exp (-1 / (0.005 * sr)); dRel = 100 / sr;
        debTicks = std::max (1, (int) std::ceil (0.004 * sr / TICK)); maxWait = std::max (2, (int) std::ceil (0.03 * sr / TICK));
        silentNeed = (int) std::ceil (0.1 * sr / TICK);
        const auto& d = modeDef (chamber).def;
        p.mode = chamber; p.predelay = d.predelay; p.decay = d.decay; p.mix = d.mix; p.size = d.size; p.damp = d.damp; p.width = d.width;
        snap();
    }
    void setParams (const Params& np) { p = np; }
    // Applique tous les réglages d'un coup, sans fondu (début de lecture, rendu hors temps réel)
    void snap()
    {
        for (auto& s : slots)
        {
            s.state = Slot::idle; s.mode = -1; s.size = -1; s.inG = 0; s.inStep = 0; s.outG = 1; s.outStep = 0; s.silent = 0; s.pk = 0;
            s.room.reset(); s.plate.reset(); s.spring.reset(); for (auto& d : s.dirty) d = false;
        }
        std::fill (pbL.begin(), pbL.end(), 0.0f); std::fill (pbR.begin(), pbR.end(), 0.0f); pw = 0;
        decS = std::log (std::max (0.05, p.decay)); dampS = p.damp; bassS = std::log (std::clamp (p.bass, 0.25, 4.0));
        frzS = p.freeze ? 1 : 0; inFrz = p.freeze ? 0 : 1;
        mixS = p.mix; widS = p.width; duckS = p.ducking; envDb = -120;
        balance (p.erLate, geC, glC); geR = { geC, 0 }; glR = { glC, 0 };
        lcS = std::log (std::clamp (p.lowCut, 20.0, 1000.0)); hcS = std::log (std::clamp (p.highCut, 1000.0, 20000.0));
        lcW = p.lowCut > 20.0001 ? 1 : 0; hcW = p.highCut < 19999.9 ? 1 : 0;
        lcN = svfHp (std::exp (lcS), sr); hcN = svfLpMatched (std::exp (hcS), sr); lcC = lcN; hcC = hcN;
        std::fill (std::begin (svfS), std::end (svfS), 0.0);
        active = 0; startSlot (slots[0], p.mode, p.size); slots[0].inG = 1; slots[0].inStep = 0;
        dA = dB = predelaySamples(); xf = 0; xfOn = false;
        phase = 0; stable = 0; age = 0; lastMode = p.mode; lastSize = p.size;
    }
    const Params& params() const { return p; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        int off = 0;
        while (off < n)
        {
            if (phase == 0) tick();
            const int m = std::min (TICK - phase, n - off);
            run (inL + off, inR + off, outL + off, outR + off, m);
            off += m; phase += m;
            if (phase >= TICK) { endTick(); phase = 0; }
        }
    }

    // Repères pour l'affichage (réflexions précoces ou retours du ressort) : moteur qui reçoit le son
    int numTaps() const
    {
        const auto e = modeDef (slots[active].mode).engine;
        return e == roomEngine ? slots[active].room.numTaps() : e == springEngine ? 8 : 0;
    }
    const Tap* tapList() const { const auto& s = slots[active]; return modeDef (s.mode).engine == roomEngine ? s.room.tapList() : s.spring.tapList(); }
    const RoomCore& roomCore() const { return slots[active].room; }
    const SpringCore& springCore() const { return slots[active].spring; }
    // Pour les tests : nombre d'emplacements qui calculent (1 = normal, 2 = pendant un changement de Size ou de mode)
    int busySlots() const { int c = 0; for (auto& s : slots) c += s.state != Slot::idle; return c; }
    bool transitionPending() const { const auto& a = slots[active]; return a.mode != p.mode || ! same (a.size, p.size); }

private:
    struct Slot
    {
        explicit Slot (double sr) : room (sr), plate (sr), spring (sr) {}
        RoomCore room; PlateCore plate; SpringCore spring;
        enum State { idle, active, ringing, killing } state = idle;
        int mode = -1; double size = -1;
        double inG = 0, inStep = 0, outG = 1, outStep = 0;
        int silent = 0; float pk = 0; bool dirty[3] {};
        double tone[4] { -1, -1, -1, -1 };   // derniers Decay, Damp, Bass, Freeze appliqués
    };

    double predelaySamples() const { return std::clamp (std::round (p.predelay * 0.001 * sr), 0.0, (double) PN - 8); }
    double aTick (double tau) const { return 1 - std::exp (-TICK / (tau * sr)); }
    static void balance (double b, double& ge, double& gl)
    {
        // Early / Late : 0 = équilibre du mode ; -100 % = réflexions seules ; +100 % = queue seule
        const double x = std::clamp (b, -1.0, 1.0);
        ge = std::min (1.0, 1 - x); gl = std::min (1.0, 1 + x);
    }

    void startSlot (Slot& s, int mode, double size)
    {
        const auto& def = modeDef (mode);
        s.mode = mode; s.size = size;
        if (def.engine == roomEngine) s.room.configure (def, size);
        else if (def.engine == springEngine) s.spring.configure (size);
        else { auto q = s.plate.params(); q.size = 0.6 + 0.8 * size; s.plate.setParams (q); }
        for (auto& t : s.tone) t = -1;
        applyTone (s);
        if (def.engine == roomEngine) s.room.snapGains(); else if (def.engine == plateEngine) s.plate.snapCoefs(); else s.spring.snapCoefs();
        s.state = Slot::active; s.outG = 1; s.outStep = 0; s.silent = 0; s.pk = 0;
        s.dirty[def.engine] = true;   // à vider quand il sera de nouveau libre
    }

    void applyTone (Slot& s)
    {
        if (s.mode < 0) return;
        const double T = std::exp (decS), bm = std::exp (bassS);
        if (same (s.tone[0], T) && same (s.tone[1], dampS) && same (s.tone[2], bm) && same (s.tone[3], frzS)) return;
        s.tone[0] = T; s.tone[1] = dampS; s.tone[2] = bm; s.tone[3] = frzS;
        const auto& def = modeDef (s.mode);
        if (def.engine == roomEngine) s.room.setDecay (def, T, dampS, bm, frzS);
        else if (def.engine == springEngine) s.spring.setDecay (T, dampS, frzS);
        else
        {
            // RT60 théorique de la boucle Dattorro, corrigé d'après mesure (la plaque sonne ~5 à 15 % plus long que la formule)
            const double ps = 0.6 + 0.8 * s.size, half = 10794.5 / 29761 * ps, Tm = std::max (0.05, T), Tc = Tm / (1 + 0.125 / std::sqrt (Tm));
            const double f = frzS, iT = (1 - f) / Tc + f / RoomCore::Tfreeze, iTlo = (1 - f) / (Tc * bm) + f / RoomCore::Tfreeze;
            PlateCore::P q = s.plate.params();
            q.decay = std::min (0.99999, std::pow (10.0, -1.5 * half * iT));
            q.damping = 20000 * std::pow (0.15, dampS);
            q.dampW = std::min (1.0, dampS / 0.05) * (1 - f);       // Damp 0 % : pas d'amortissement (continu jusqu'à 5 %)
            q.loG = std::pow (10.0, -3 * half * (iTlo - iT));         // Bass : étagère grave 200 Hz (2e ordre), une fois par demi-boucle
            double lo[5]; shelfLo2 (q.loG, bassXover, sr, lo);
            q.loComp = 1 / std::sqrt (mag2 (lo, 500, sr));            // RT60 à 500 Hz inchangé
            q.bandwidth = 20000; q.size = ps;
            s.plate.setParams (q);
        }
    }

    // Début de chaque tranche de 128 échantillons : changements de structure, pré-délai, lissage des réglages
    void tick()
    {
        // lissage des réglages de timbre (constante 50 ms) ; Freeze en rampe de 100 ms
        const double a50 = aTick (0.05);
        auto toward = [] (double& v, double t, double a, double eps) { if (std::abs (t - v) < eps) v = t; else v += a * (t - v); };
        toward (decS, std::log (std::max (0.05, p.decay)), a50, 1e-4);
        toward (dampS, p.damp, a50, 1e-4);
        toward (bassS, std::log (std::clamp (p.bass, 0.25, 4.0)), a50, 1e-4);
        { const double st = TICK / (0.1 * sr), t = p.freeze ? 1.0 : 0.0; frzS = frzS < t ? std::min (t, frzS + st) : std::max (t, frzS - st); }
        for (auto& s : slots)
            if (s.state != Slot::idle)
            {
                const auto e = modeDef (s.mode).engine;
                if (e == roomEngine) s.room.preTick(); else if (e == plateEngine) s.plate.preTick(); else s.spring.preTick();
                applyTone (s);
                if (e == roomEngine) s.room.postTick (TICK); else if (e == plateEngine) s.plate.postTick (TICK); else s.spring.postTick (TICK);
            }

        // changement de mode ou de Size : seulement quand le réglage est stable (ou après 30 ms d'attente),
        // jamais pendant un fondu d'entrée, et pas tant que la queue est gelée (appliqué à la sortie de Freeze)
        Slot& act = slots[active];
        const bool differs = act.mode != p.mode || ! same (act.size, p.size);
        if (differs && ! p.freeze && frzS <= 0)
        {
            if (p.mode == lastMode && same (p.size, lastSize)) ++stable; else stable = 0;
            ++age;
            Slot& sp = slots[1 - active];
            if ((stable >= debTicks || age >= maxWait) && same (act.inStep, 0) && act.inG >= 1)
            {
                if (sp.state == Slot::ringing || sp.state == Slot::active) { sp.state = Slot::killing; sp.outStep = -tkill; }
                else if (sp.state == Slot::idle && ! sp.dirty[modeDef (p.mode).engine])
                {
                    startSlot (sp, p.mode, p.size); sp.inG = 0; sp.inStep = tin;
                    act.state = Slot::ringing; act.inStep = -tin; act.silent = 0;
                    active = 1 - active; stable = 0; age = 0;
                }
            }
        }
        else { stable = 0; age = 0; }
        lastMode = p.mode; lastSize = p.size;
        // emplacements libres : vidage progressif (64 k échantillons par tranche au plus)
        size_t budget = 65536;
        for (auto& s : slots)
            if (s.state == Slot::idle)
                for (int e = 0; e < 3 && budget > 0; ++e)
                    if (s.dirty[e])
                    {
                        const bool done = e == roomEngine ? s.room.clearSome (budget) : e == plateEngine ? s.plate.clearSome (budget) : s.spring.clearSome (budget);
                        if (done) s.dirty[e] = false;
                    }

        // pré-délai : deux têtes de lecture ; un nouveau réglage démarre un fondu de 30 ms (le suivant attend la fin)
        const double D = predelaySamples();
        if (! xfOn && ! same (D, dA)) { dB = D; xf = 0; xfOn = true; }

        // Early / Late : rampe linéaire sur la tranche (constante 10 ms)
        double geT, glT; balance (p.erLate, geT, glT);
        const double a10 = aTick (0.0104);
        const double ge0 = geC, gl0 = glC;
        toward (geC, geT, a10, 1e-7); toward (glC, glT, a10, 1e-7);
        geR = { ge0, (geC - ge0) / TICK }; glR = { gl0, (glC - gl0) / TICK };
        // Low cut / High cut : fréquences lissées (constante 30 ms) dans le domaine logarithmique, coefficients interpolés
        const double a30 = aTick (0.03);
        toward (lcS, std::log (std::clamp (p.lowCut, 20.0, 1000.0)), a30, 1e-5);
        toward (hcS, std::log (std::clamp (p.highCut, 1000.0, 20000.0)), a30, 1e-5);
        lcC = lcN; hcC = hcN;
        lcN = svfHp (std::exp (lcS), sr); hcN = svfLpMatched (std::exp (hcS), sr);
        lcWT = p.lowCut > 20.0001 ? 1 : 0; hcWT = p.highCut < 19999.9 ? 1 : 0;
    }
    // Fin de tranche : un emplacement qui finit sa queue s'arrête quand il est sous -120 dBFS pendant 100 ms
    void endTick()
    {
        for (auto& s : slots)
        {
            if (s.state == Slot::ringing && s.inG <= 0)
            {
                s.silent = s.pk < 1e-6f ? s.silent + 1 : 0;
                if (s.silent >= silentNeed) { s.state = Slot::idle; s.silent = 0; }
            }
            s.pk = 0;
        }
    }

    void run (const float* inL, const float* inR, float* outL, float* outR, int m)
    {
        // 1) pré-délai stéréo (deux têtes en fondu) et coupure de l'entrée pendant Freeze
        const double frzT = p.freeze ? 0.0 : 1.0;
        int pw_ = pw;
        for (int i = 0; i < m; ++i)
        {
            pbL[(size_t) pw_] = inL[i]; pbR[(size_t) pw_] = inR[i];
            const int ia = (pw_ - (int) dA) & pm;
            double l = pbL[(size_t) ia], r = pbR[(size_t) ia];
            if (xfOn)
            {
                const int ib = (pw_ - (int) dB) & pm;
                const double wb = rcos (xf);
                l += wb * (pbL[(size_t) ib] - l); r += wb * (pbR[(size_t) ib] - r);
                xf += tpd; if (xf >= 1) { xfOn = false; dA = dB; xf = 0; }
            }
            if (! same (inFrz, frzT)) inFrz = inFrz < frzT ? std::min (frzT, inFrz + tin) : std::max (frzT, inFrz - tin);
            const double g = inFrz >= 1 ? 1.0 : rcos (inFrz);
            xL[i] = (float) (l * g + 1e-24); xR[i] = (float) (r * g + 1e-24);   // + 1e-24 : anti-dénormaux
            pw_ = (pw_ + 1) & pm;
        }
        pw = pw_;

        // 2) les deux emplacements (celui qui reçoit le son, celui qui finit sa queue)
        std::fill (wl, wl + m, 0.0f); std::fill (wr, wr + m, 0.0f);
        for (auto& s : slots)
        {
            if (s.state == Slot::idle) continue;
            const auto& def = modeDef (s.mode);
            for (int i = 0; i < m; ++i)
            {
                if (! same (s.inStep, 0)) { s.inG += s.inStep; if (s.inG >= 1) { s.inG = 1; s.inStep = 0; } else if (s.inG <= 0) { s.inG = 0; s.inStep = 0; } }
                const double g = s.inG >= 1 ? 1.0 : rcos (s.inG);
                sxL[i] = (float) (xL[i] * g); sxR[i] = (float) (xR[i] * g);
            }
            if (def.engine == plateEngine) s.plate.process (sxL, sxR, yl, yr, m, phase);
            else if (def.engine == roomEngine) s.room.process (sxL, sxR, yl, yr, m, phase, geR, glR);
            else
            {
                for (int i = 0; i < m; ++i) sm[i] = (float) (0.5 * ((double) sxL[i] + sxR[i]));   // un ressort : entrée mono
                s.spring.process (sm, yl, yr, m, phase);
            }
            float pk = s.pk;
            for (int i = 0; i < m; ++i)
            {
                if (s.outStep < 0) { s.outG += s.outStep; if (s.outG <= 0) { s.outG = 0; s.outStep = 0; } }
                const double g = (s.outG >= 1 ? 1.0 : rcos (s.outG)) * def.gain;
                wl[i] = (float) (wl[i] + yl[i] * g); wr[i] = (float) (wr[i] + yr[i] * g);
                pk = std::max (pk, std::max (std::abs (yl[i]), std::abs (yr[i])));
            }
            s.pk = pk;
            if (s.state == Slot::killing && s.outG <= 0) { s.state = Slot::idle; s.outG = 1; s.inG = 0; s.inStep = 0; }
        }

        // 3) sortie de la réverb : Low cut, High cut, Ducking, Width, Mix
        const double mixT = p.mix, widT = p.width, duckT = p.ducking;
        double* z = svfS;
        for (int i = 0; i < m; ++i)
        {
            const double t = (double) (phase + i + 1) / TICK;
            auto lerp = [t] (double a, double b) { return a + (b - a) * t; };
            if (! same (lcW, lcWT)) { lcW += (lcWT - lcW) * aSm * 2; if (std::abs (lcWT - lcW) < 1e-9) lcW = lcWT; }
            if (! same (hcW, hcWT)) { hcW += (hcWT - hcW) * aSm * 2; if (std::abs (hcWT - hcW) < 1e-9) hcW = hcWT; }

            double L = wl[i], R = wr[i];
            // Low cut (passe-haut 12 dB/oct.) ; à 20 Hz il est hors circuit (part nulle : son exact) mais continue de tourner,
            // pour entrer sans à-coup quand on le règle
            {
                const double g = lerp (lcC.g, lcN.g), k = lcC.k, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                for (int c = 0; c < 2; ++c)
                {
                    double& x = c == 0 ? L : R; double* q = z + c * 2;
                    const double v3 = x - q[1], v1 = a1 * q[0] + a2 * v3, v2 = q[1] + a2 * q[0] + a3 * v3;
                    q[0] = 2 * v1 - q[0]; q[1] = 2 * v2 - q[1];
                    const double hp = x - k * v1 - v2;
                    x += lcW * (hp - x);
                }
            }
            // High cut (passe-bas 12 dB/oct. « matched ») ; à 20 kHz il est hors circuit (part nulle : son exact) mais tourne
            {
                const double g = lerp (hcC.g, hcN.g), k = lerp (hcC.k, hcN.k), m0 = lerp (hcC.m0, hcN.m0), m1 = lerp (hcC.m1, hcN.m1), m2 = lerp (hcC.m2, hcN.m2);
                const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                for (int c = 0; c < 2; ++c)
                {
                    double& x = c == 0 ? L : R; double* q = z + 4 + c * 2;
                    const double v3 = x - q[1], v1 = a1 * q[0] + a2 * v3, v2 = q[1] + a2 * q[0] + a3 * v3;
                    q[0] = 2 * v1 - q[0]; q[1] = 2 * v2 - q[1];
                    const double hp = x - k * v1 - v2, y = m0 * hp + m1 * v1 + m2 * v2;
                    x += hcW * (y - x);
                }
            }
            // Ducking : la réverb baisse pendant que le son direct joue (jusqu'à -30 dB à 100 %, pleine baisse au-dessus
            // de -20 dBFS, aucune sous -50 dBFS) ; détecteur de crête en dB : attaque 5 ms, retour 100 dB/s
            // (la réverb revient en 0,3 s environ quand le son direct s'arrête)
            if (! same (duckS, duckT)) { duckS += aSm * (duckT - duckS); if (std::abs (duckT - duckS) < 1e-9) duckS = duckT; }
            if (duckS > 0 || duckT > 0)
            {
                const double a = std::max (std::abs ((double) inL[i]), std::abs ((double) inR[i])), adB = 20 * std::log10 (a + 1e-12);
                envDb = adB > envDb ? envDb + dAtt * (adB - envDb) : std::max (adB, envDb - dRel);
                const double amt = std::clamp ((envDb + 50) / 30, 0.0, 1.0);
                if (duckS > 0 && amt > 0) { const double g = std::pow (10.0, -1.5 * duckS * amt); L *= g; R *= g; }
            }
            else envDb = -120;
            mixS += aSm * (mixT - mixS); widS += aSm * (widT - widS);
            const double Mm = (L + R) * 0.5, Ss = (L - R) * 0.5 * widS;
            L = Mm + Ss; R = Mm - Ss;
            outL[i] = (float) (inL[i] * (1 - mixS) + L * mixS);
            outR[i] = (float) (inR[i] * (1 - mixS) + R * mixS);
        }
    }

    double sr;
    Slot slots[2];
    int active = 0;
    int PN = 0, pm = 0, pw = 0; std::vector<float> pbL, pbR;
    double dA = 0, dB = 0, xf = 0; bool xfOn = false;
    float xL[TICK] {}, xR[TICK] {}, sxL[TICK] {}, sxR[TICK] {}, sm[TICK] {}, yl[TICK] {}, yr[TICK] {}, wl[TICK] {}, wr[TICK] {};
    Params p;
    double tin = 0, tkill = 0, tpd = 0, aSm = 0, dAtt = 0, dRel = 0;
    int debTicks = 1, maxWait = 2, silentNeed = 1;
    int phase = 0, stable = 0, age = 0, lastMode = -1; double lastSize = -1;
    double decS = 0, dampS = 0, bassS = 0, frzS = 0, inFrz = 1;
    double mixS = 0, widS = 0, duckS = 0, envDb = -120, geC = 1, glC = 1;
    Ramp geR, glR;
    double lcS = 0, hcS = 0, lcW = 0, hcW = 0, lcWT = 0, hcWT = 0;
    Svf lcC, hcC, lcN, hcN; double svfS[8] {};
};
} // namespace audiolab::rv
