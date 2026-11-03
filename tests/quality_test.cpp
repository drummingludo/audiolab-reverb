// Banc qualité de Lab Verb 2.0.0, sans JUCE : il fait tourner le moteur (Source/ReverbCore.h) et vérifie des seuils chiffrés.
//   1. vraie entrée stéréo (une source à gauche donne une réverb qui penche à gauche ; Spring : mono)
//   2. RT60 exact (±5 %) ; 3. même son à 44,1 / 48 / 96 / 192 kHz ; 4. niveaux des modes égalisés (ceux de la 1.x)
//   5. Size et mode sans coupure ni clic, la queue reste ; 6. Pre-delay sans clic
//   7. réglages avancés (Low cut, High cut, Bass, Early / Late, Ducking, Freeze) ; 8. temps réel (aucune allocation,
//      blocs, extrêmes, vitesse)
// Compilation : clang++ -O2 -std=c++17 -I Source tests/quality_test.cpp -o qt && ./qt   (code de sortie 1 en cas d'échec)
#include "ReverbCore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// ── Compteur d'allocations (le moteur ne doit rien allouer dans process) ──
static std::atomic<long> gAllocs { 0 };
static std::atomic<bool> gCount { false };
void* operator new (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace rv = audiolab::rv;
using Params = rv::ReverbEngine::Params;
using Sig = std::vector<float>;
using cd = std::complex<double>;

static bool OK = true;
static void check (bool c, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));
static void check (bool c, const char* fmt, ...)
{
    std::printf ("  %s ", c ? "OK   " : "ÉCHEC");
    va_list a; va_start (a, fmt); std::vprintf (fmt, a); va_end (a);
    std::printf ("\n"); OK &= c;
}
static const char* NAMES[] = { "Chamber", "Plate", "Spring", "Room", "Hall", "Church" };
static double dB (double e) { return 10 * std::log10 (std::max (e, 1e-300)); }

// ── Réglages et rendu ──
static Params defaults (int mode)
{
    const auto& d = rv::modeDef (mode).def;
    Params p; p.mode = mode; p.predelay = d.predelay; p.decay = d.decay; p.mix = d.mix; p.size = d.size; p.damp = d.damp; p.width = d.width;
    return p;
}
static Params wet (int mode) { Params p = defaults (mode); p.mix = 1; p.width = 1; p.predelay = 0; return p; }

struct Out { Sig L, R; };
struct Change { size_t at; Params p; };
static Out render (double fs, const Params& p0, const Sig& inL, const Sig& inR, int block = 512, const std::vector<Change>& ch = {}, int* maxBusy = nullptr)
{
    rv::ReverbEngine e (fs);
    e.setParams (p0); e.snap();
    const size_t n = inL.size();
    Out o { Sig (n), Sig (n) };
    size_t ci = 0;
    for (size_t s = 0; s < n; s += (size_t) block)
    {
        while (ci < ch.size() && ch[ci].at <= s) e.setParams (ch[ci++].p);
        const int m = (int) std::min ((size_t) block, n - s);
        e.process (inL.data() + s, inR.data() + s, o.L.data() + s, o.R.data() + s, m);
        if (maxBusy) *maxBusy = std::max (*maxBusy, e.busySlots());
    }
    return o;
}
static Sig impulse (size_t n, float a = 1) { Sig x (n, 0.0f); x[0] = a; return x; }
static Sig noise (size_t n, unsigned seed, float amp)
{
    std::mt19937 g (seed); std::normal_distribution<float> d (0, amp);
    Sig x (n); for (auto& v : x) v = d (g); return x;
}
// Sinus de test, entrée en fondu de 50 ms (une attaque brutale reviendrait dans les échos et fausserait le détecteur de clic)
static Sig sine (size_t n, double f, double fs, float amp)
{
    Sig x (n);
    for (size_t i = 0; i < n; ++i) x[i] = (float) (amp * std::sin (2 * rv::PI * f * (double) i / fs) * std::min (1.0, (double) i / (0.05 * fs)));
    return x;
}
static double energy (const Sig& x, size_t a = 0, size_t b = (size_t) -1)
{
    double e = 0; for (size_t i = a; i < std::min (b, x.size()); ++i) e += (double) x[i] * x[i]; return e;
}

// ── Analyse : FFT, puissance par bande, RT60 par bande (Schroeder) ──
static void fft (std::vector<cd>& a, bool inv)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * rv::PI / (double) len * (inv ? 1 : -1);
        std::vector<cd> w (len / 2); for (size_t k = 0; k < len / 2; ++k) w[k] = std::polar (1.0, ang * (double) k);
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k) { const cd u = a[i + k], v = a[i + k + len / 2] * w[k]; a[i + k] = u + v; a[i + k + len / 2] = u - v; }
    }
    if (inv) for (auto& x : a) x /= (double) n;
}
// Spectre de L + jR (une seule FFT pour les deux voies)
struct Spec
{
    std::vector<cd> X; double fs; size_t n;
    Spec (const Out& o, double sampleRate) : fs (sampleRate), n (o.L.size())
    {
        size_t N = 1; while (N < 2 * n) N <<= 1;
        X.assign (N, 0.0); for (size_t i = 0; i < n; ++i) X[i] = cd (o.L[i], o.R[i]);
        fft (X, false);
    }
    size_t N() const { return X.size(); }
    // puissances séparées des deux voies au point k
    void lr (size_t k, cd& l, cd& r) const { const cd a = X[k], b = std::conj (X[(N() - k) % N()]); l = 0.5 * (a + b); r = cd (0, -0.5) * (a - b); }
    double powerIn (double f0, double f1) const   // puissance moyenne par point de fréquence (L + R) : gain de puissance de la réponse
    {
        double s = 0; int c = 0;
        for (size_t k = 1; k < N() / 2; ++k) { const double f = (double) k * fs / (double) N(); if (f >= f0 && f < f1) { cd l, r; lr (k, l, r); s += std::norm (l) + std::norm (r); ++c; } }
        return s / std::max (1, c);
    }
    double sumIn (double f0, double f1) const
    {
        double s = 0;
        for (size_t k = 1; k < N() / 2; ++k) { const double f = (double) k * fs / (double) N(); if (f >= f0 && f < f1) { cd l, r; lr (k, l, r); s += std::norm (l) + std::norm (r); } }
        return 2 * s / (double) N();
    }
    // module de L à la fréquence f (pour comparer deux réponses identiques à un filtre près)
    cd at (size_t k, bool right) const { cd l, r; lr (k, l, r); return right ? r : l; }
    // RT60 d'une bande gaussienne (écart-type 0,42 octave), intégration de Schroeder, pente de -5 à -35 dB
    double rt60 (double fc) const
    {
        std::vector<cd> Y (N());
        for (size_t k = 0; k < N(); ++k)
        {
            const size_t kk = k <= N() / 2 ? k : N() - k;
            const double f = (double) kk * fs / (double) N(), h = f <= 0 ? 0 : std::exp (-0.5 * std::pow (std::log2 (f / fc) / 0.42, 2));
            Y[k] = X[k] * h;
        }
        fft (Y, true);
        std::vector<double> cum (n); double c = 0;
        for (size_t i = n; i-- > 0;) { c += std::norm (Y[i]); cum[i] = c; }
        auto lv = [&] (size_t i) { return 10 * std::log10 (std::max (cum[i] / cum[0], 1e-30)); };
        size_t i5 = 0; while (i5 < n && lv (i5) > -5) ++i5;
        size_t iE = i5; while (iE < n && lv (iE) > -35) ++iE;
        if (iE >= n) { iE = i5; while (iE < n && lv (iE) > -25) ++iE; }
        double sx = 0, sy = 0, sxx = 0, sxy = 0; const double cnt = (double) (iE - i5);
        for (size_t i = i5; i < iE; ++i) { const double t = (double) i / fs, y = lv (i); sx += t; sy += y; sxx += t * t; sxy += t * y; }
        return -60 / ((cnt * sxy - sx * sy) / (cnt * sxx - sx * sx));
    }
};
static Out irOf (double fs, const Params& p, double sec, float l = 1, float r = 1)
{
    const size_t n = (size_t) (sec * fs);
    return render (fs, p, impulse (n, l), impulse (n, r));
}
static double irSeconds (const Params& p) { return std::min (12.0, p.decay * std::max (1.0, p.bass) * 1.3 + 0.5); }

// Détecteur de clic pour un signal de test à 440 Hz : dérivée seconde rapportée à celle d'un sinus de même niveau
// (crête locale sur ±5 ms). Un sinus, même dont le niveau varie lentement, donne au plus 1 ; un clic donne bien plus.
static double gClickAt = 0;   // instant du pire indice (s), pour le diagnostic
static double clickRatio (const Out& o, double fs, size_t a, size_t b, double f0 = 440)
{
    const double w = 2 * rv::PI * f0 / fs, k = 4 * std::sin (w / 2) * std::sin (w / 2);
    const size_t blk = std::max<size_t> (1, (size_t) (0.0025 * fs));
    double m = 0;
    for (const Sig* x : { &o.L, &o.R })
    {
        const size_t nb = x->size() / blk + 1; std::vector<double> bm (nb, 0);
        for (size_t i = 0; i < x->size(); ++i) bm[i / blk] = std::max (bm[i / blk], (double) std::abs ((*x)[i]));
        for (size_t i = std::max<size_t> (a, 2); i < std::min (b, x->size()); ++i)
        {
            const size_t c = i / blk; double pk = 1e-4;
            for (size_t j = c >= 2 ? c - 2 : 0; j <= std::min (nb - 1, c + 2); ++j) pk = std::max (pk, bm[j]);
            const double r = std::abs ((double) (*x)[i] - 2.0 * (*x)[i - 1] + (*x)[i - 2]) / (k * pk);
            if (r > m) { m = r; gClickAt = (double) i / fs; }
        }
    }
    return m;
}
// Niveau efficace par tranches de 20 ms (L + R)
static std::vector<double> rmsBlocks (const Out& o, double fs)
{
    const size_t w = (size_t) (0.02 * fs); std::vector<double> r;
    for (size_t s = 0; s + w <= o.L.size(); s += w) r.push_back (dB ((energy (o.L, s, s + w) + energy (o.R, s, s + w)) / (double) w));
    return r;
}

int main()
{
    std::printf ("\nBanc qualité Lab Verb 2.0.0 (moteur seul, sans JUCE)\n");
    const double rates[] = { 44100, 48000, 96000, 192000 };

    // ═══ 1. Vraie entrée stéréo ═══
    std::printf ("\n1) Entrée stéréo (48 kHz, réglages du mode, Width 100 %%, Mix 100 %%)\n");
    {
        const double fs = 48000;
        for (int m = 0; m < rv::numModes; ++m)
        {
            const Params p = wet (m); const double sec = irSeconds (p);
            const Out l = irOf (fs, p, sec, 1, 0), r = irOf (fs, p, sec, 0, 1), c = irOf (fs, p, sec, 1, 1);
            const size_t n100 = (size_t) (0.1 * fs);
            const double lAll = dB (energy (l.L)) - dB (energy (l.R)), l100 = dB (energy (l.L, 0, n100)) - dB (energy (l.R, 0, n100));
            const double rAll = dB (energy (r.L)) - dB (energy (r.R)), r100 = dB (energy (r.L, 0, n100)) - dB (energy (r.R, 0, n100));
            const double cAll = dB (energy (c.L)) - dB (energy (c.R));
            if (m == rv::spring)
                check (l.L == r.L && l.R == r.R, "%-8s entrée mono (un seul transducteur) : source à gauche = source à droite, au bit près", NAMES[m]);
            else
            {
                const double need = m == rv::plate ? 1.5 : 3, need100 = m == rv::plate ? 4 : 6;
                check (lAll >= need && l100 >= need100 && rAll <= -need && r100 <= -need100 && std::abs (cAll) <= 1,
                       "%-8s source à gauche : %+.1f dB (100 premières ms %+.1f) ; à droite : %+.1f (%+.1f) ; au centre : %+.1f dB",
                       NAMES[m], lAll, l100, rAll, r100, cAll);
            }
        }
    }

    // ═══ 2. RT60 ═══
    std::printf ("\n2) RT60 à 500 Hz (±5 %%)\n");
    for (int m = 0; m < rv::numModes; ++m)
    {
        double worst = 0; char txt[200] = ""; int pos = 0;
        for (double fs : rates)
        {
            const Params p = wet (m); const Spec s (irOf (fs, p, irSeconds (p)), fs);
            const double e = (s.rt60 (500) - p.decay) / p.decay * 100; worst = std::max (worst, std::abs (e));
            pos += std::snprintf (txt + pos, sizeof (txt) - (size_t) pos, " %+.1f", e);
        }
        check (worst <= 5, "%-8s Decay %.1f s, à 44,1 / 48 / 96 / 192 kHz : écart %s %%", NAMES[m], rv::modeDef (m).def.decay, txt);
    }
    for (int m = 0; m < rv::numModes; ++m)
    {
        // plages réalistes : une très grande pièce (ou une grande plaque) avec un Decay très court n'a plus de décroissance
        // exponentielle (moins de deux passages dans la boucle), le RT60 n'y a plus de sens (identique en 1.x)
        static constexpr double Ts[6][3] = { { 0.6, 2, 4 }, { 1.2, 2.5, 6 }, { 1, 2.5, 5 }, { 0.3, 0.8, 2 }, { 1.2, 3, 6 }, { 2, 5, 9 } };
        double worst = 0; char txt[200] = ""; int pos = 0;
        for (double T : Ts[m])
        {
            Params p = wet (m); p.decay = T; const double fs = 48000; const Spec s (irOf (fs, p, irSeconds (p)), fs);
            const double e = (s.rt60 (500) - T) / T * 100; worst = std::max (worst, std::abs (e));
            pos += std::snprintf (txt + pos, sizeof (txt) - (size_t) pos, " %+.1f", e);
        }
        check (worst <= 5, "%-8s Decay %.1f / %.1f / %.1f s (48 kHz) : écart %s %%", NAMES[m], Ts[m][0], Ts[m][1], Ts[m][2], txt);
    }

    // ═══ 3. Même son de 44,1 à 192 kHz ═══
    std::printf ("\n3) Même son à 44,1 / 48 / 96 / 192 kHz (comparé à 48 kHz ; octaves de 125 Hz à 16 kHz)\n");
    {
        const double bands[] = { 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        for (int m = 0; m < rv::numModes; ++m)
        {
            const Params p = wet (m);
            double g48[8], t48[8]; double wg = 0, wt = 0;
            for (double fs : { 48000.0, 44100.0, 96000.0, 192000.0 })
            {
                const Spec s (irOf (fs, p, irSeconds (p)), fs);
                for (int b = 0; b < 8; ++b)
                {
                    const double g = dB (s.powerIn (bands[b] / 1.4142, bands[b] * 1.4142));
                    const bool rtBand = m == rv::spring ? bands[b] <= 2000 : bands[b] <= 8000;
                    const double t = rtBand ? s.rt60 (bands[b]) : 0;
                    if (fs == 48000) { g48[b] = g; t48[b] = t; }
                    else { wg = std::max (wg, std::abs (g - g48[b])); if (rtBand) wt = std::max (wt, std::abs (t - t48[b]) / t48[b] * 100); }
                }
            }
            const double gLim = m == rv::spring ? 1.0 : 0.75;
            check (wg <= gLim && wt <= 5, "%-8s niveau par octave : écart max %.2f dB (seuil %.2f) ; RT60 par octave (125 Hz à %s) : %.1f %% (seuil 5)",
                   NAMES[m], wg, gLim, m == rv::spring ? "2 kHz" : "8 kHz", wt);
        }
    }

    // ═══ 4. Niveaux des modes ═══
    std::printf ("\n4) Niveaux des modes égalisés (énergie de 90 Hz à 11 kHz, entrée au centre, 48 kHz ; référence : 1.x)\n");
    {
        static constexpr double ref1x[6] = { -3.582, -4.718, -1.346, -4.764, -5.573, -4.992 };   // mesurés sur la 1.1.0
        for (int m = 0; m < rv::numModes; ++m)
        {
            const Params p = wet (m); const double fs = 48000;
            const Spec s (irOf (fs, p, p.decay * 1.4 + 0.6), fs);
            const double e = dB (s.sumIn (90, 11300) / 2);
            check (std::abs (e - ref1x[m]) <= 0.5, "%-8s %+.2f dB (1.x : %+.2f dB)", NAMES[m], e, ref1x[m]);
        }
    }

    // ═══ 5. Size et mode sans coupure ni clic ═══
    std::printf ("\n5) Changer Size ou le mode pendant la lecture\n");
    {
        const double fs = 48000; const size_t n = (size_t) (3 * fs), at = (size_t) (0.5 * fs);
        // a) la queue continue telle quelle (l'emplacement sortant finit sa queue)
        for (int m : { rv::hall, rv::plate, rv::spring })
        {
            Params p = wet (m); Params q = p; q.size = p.size > 0.5 ? 0.2 : 0.9;
            const Out ref = render (fs, p, impulse (n), impulse (n));
            const Out ch = render (fs, p, impulse (n), impulse (n), 512, { { at, q } });
            double d = 0, pk = 0;
            for (size_t i = at; i < n; ++i) { d = std::max (d, (double) std::abs (ch.L[i] - ref.L[i]) + std::abs (ch.R[i] - ref.R[i])); pk = std::max (pk, (double) std::abs (ref.L[i])); }
            // le moteur sortant s'arrête quand sa queue reste sous -120 dBFS (1e-6) pendant 100 ms
            check (d <= std::max (pk * 1e-5, 1.5e-6), "%-8s Size changé 0.5 s après le son : la queue reste identique jusqu'à -120 dBFS (écart max %.1e ; 1.x : passage par zéro en 12 ms)", NAMES[m], d);
        }
        // b) son continu : pas de trou ; c) sinus : pas de clic
        for (int m : { rv::chamber, rv::room, rv::hall, rv::church, rv::plate, rv::spring })
        {
            Params p = wet (m); Params q = p; q.size = p.size > 0.5 ? 0.2 : 0.9;
            Params r = p; r.mode = m == rv::plate ? rv::hall : rv::plate;
            const size_t c1 = (size_t) (1.2 * fs);
            const Sig nz = noise (n, 11, 0.1f);
            const Out a = render (fs, p, nz, nz, 512, { { c1, q } });
            const auto rb = rmsBlocks (a, fs);
            std::vector<double> before (rb.begin() + 30, rb.begin() + 58); std::sort (before.begin(), before.end());
            const double med = before[before.size() / 2]; double dip = 0;
            for (size_t k = 60; k < 85; ++k) dip = std::min (dip, rb[k] - med);
            const Sig sn = sine (n, 440, fs, 0.3f);
            const Out b = render (fs, p, sn, sn, 512, { { c1, q } }), c = render (fs, p, sn, sn, 512, { { c1, r } });
            const double clickS = clickRatio (b, fs, (size_t) (0.7 * fs), (size_t) (1.8 * fs)), clickM = clickRatio (c, fs, (size_t) (0.7 * fs), (size_t) (1.8 * fs));
            check (dip >= -3 && clickS <= 1.3 && clickM <= 1.3,
                   "%-8s bruit continu : creux %.1f dB au changement de Size (seuil -3) ; sinus : indice de clic %.2f (Size), %.2f (mode) (seuil 1.3)", NAMES[m], dip, clickS, clickM);
        }
        // d) Size tourné en continu (automation à chaque tranche) : sain, deux moteurs au plus, pas de clic
        {
            Params p = wet (rv::hall); std::vector<Change> ch;
            for (size_t s = (size_t) (0.5 * fs); s < (size_t) (2.5 * fs); s += 128) { Params q = p; q.size = 0.1 + 0.8 * (double) (s - (size_t) (0.5 * fs)) / (2 * fs); ch.push_back ({ s, q }); }
            const Sig sn = sine (n, 440, fs, 0.3f); int busy = 0;
            const Out o = render (fs, p, sn, sn, 128, ch, &busy);
            double pk = 0; bool fin = true;
            for (size_t i = 0; i < n; ++i) { fin &= std::isfinite (o.L[i]) && std::isfinite (o.R[i]); pk = std::max (pk, (double) std::abs (o.L[i])); }
            const double ratio = clickRatio (o, fs, (size_t) (0.3 * fs), (size_t) (2.6 * fs));
            check (fin && pk < 4 && busy <= 2 && ratio <= 1.3, "Hall : Size automatisé de 10 à 90 %% en 2 s : sortie saine (crête %.2f), %d moteurs au plus, indice de clic %.2f à %.3f s (seuil 1.3)", pk, busy, ratio, gClickAt);
        }
    }

    // ═══ 6. Pre-delay ═══
    std::printf ("\n6) Pre-delay sans clic (deux têtes de lecture en fondu de 30 ms)\n");
    {
        const double fs = 48000; const size_t n = (size_t) (2.5 * fs);
        const Sig sn = sine (n, 440, fs, 0.3f);
        for (int m : { rv::room, rv::plate })
        {
            Params p = wet (m); Params q = p; q.predelay = 120; Params r = q; r.predelay = 7;
            const Out o = render (fs, p, sn, sn, 512, { { (size_t) (1.0 * fs), q }, { (size_t) (1.6 * fs), r } });
            const double ratio = clickRatio (o, fs, (size_t) (0.5 * fs), (size_t) (2.2 * fs));
            std::vector<Change> sw;
            for (size_t s = (size_t) (0.5 * fs); s < (size_t) (2 * fs); s += 512) { Params z = p; z.predelay = std::round (125 + 120 * std::sin ((double) s / fs * 3)); sw.push_back ({ s, z }); }
            const Out o2 = render (fs, p, sn, sn, 512, sw);
            const double ratio2 = clickRatio (o2, fs, (size_t) (0.5 * fs), (size_t) (2.2 * fs));
            check (ratio <= 1.3 && ratio2 <= 1.3, "%-8s 0 → 120 → 7 ms : indice de clic %.2f ; balayage continu 5 à 245 ms : %.2f (seuil 1.3)", NAMES[m], ratio, ratio2);
        }
    }

    // ═══ 7. Réglages avancés ═══
    std::printf ("\n7) Réglages avancés\n");
    {
        // High cut et Low cut : réponse réelle comparée au Butterworth analogique, aux 4 fréquences
        double wHc = 0, wLc = 0;
        for (double fs : rates)
        {
            Params p = wet (rv::room); p.decay = 0.3;
            const double sec = 0.6;
            const Spec off (irOf (fs, p, sec), fs);
            for (double fc : { 2000.0, 8000.0, 15000.0 })
            {
                Params q = p; q.highCut = fc; const Spec on (irOf (fs, q, sec), fs);
                for (size_t k = 1; k < on.N() / 2; ++k)
                {
                    const double f = (double) k * fs / (double) on.N(); if (f < 100 || f > 20000) continue;
                    const double ana = 1 / std::sqrt (1 + std::pow (f / fc, 4)); if (ana < 0.01) continue;   // jusqu'à -40 dB
                    const double ratio = std::abs (on.at (k, false)) / std::max (1e-12, std::abs (off.at (k, false)));
                    if (std::abs (off.at (k, false)) > 1e-4) wHc = std::max (wHc, std::abs (20 * std::log10 (ratio / ana)));
                }
            }
            Params q = p; q.lowCut = 200; const Spec on (irOf (fs, q, sec), fs);
            for (size_t k = 1; k < on.N() / 2; ++k)
            {
                const double f = (double) k * fs / (double) on.N(); if (f < 40 || f > 20000) continue;
                const double ana = 1 / std::sqrt (1 + std::pow (200 / f, 4));
                const double ratio = std::abs (on.at (k, true)) / std::max (1e-12, std::abs (off.at (k, true)));
                if (std::abs (off.at (k, true)) > 1e-4) wLc = std::max (wLc, std::abs (20 * std::log10 (ratio / ana)));
            }
        }
        check (wHc <= 0.5, "High cut 2 / 8 / 15 kHz (12 dB/oct.) : écart au filtre analogique %.2f dB jusqu'à 20 kHz, à 44,1 / 48 / 96 / 192 kHz (seuil 0.5)", wHc);
        check (wLc <= 0.3, "Low cut 200 Hz (12 dB/oct.) : écart au filtre analogique %.2f dB de 40 Hz à 20 kHz (seuil 0.3)", wLc);

        // Bass : multiplicateur du RT60 grave (transition 200 Hz, 2e ordre) ; le médium ne bouge pas
        for (int m : { rv::chamber, rv::hall, rv::church, rv::plate })
        {
            const double fs = 48000; Params p = wet (m);
            double t[3][4]; int i = 0;
            for (double bm : { 1.0, 2.0, 0.5 })
            {
                Params q = p; q.bass = bm; const Spec s (irOf (fs, q, std::min (14.0, q.decay * 2.6 + 0.6)), fs);
                t[i][0] = s.rt60 (63); t[i][1] = s.rt60 (125); t[i][2] = s.rt60 (500); t[i][3] = s.rt60 (1000); ++i;
            }
            const double up63 = t[1][0] / t[0][0], up125 = t[1][1] / t[0][1], dn63 = t[2][0] / t[0][0], dn125 = t[2][1] / t[0][1];
            double mid = 0; for (int k = 1; k < 3; ++k) mid = std::max ({ mid, std::abs (t[k][2] / t[0][2] - 1), std::abs (t[k][3] / t[0][3] - 1) });
            check (up63 >= 1.6 && up125 >= 1.5 && dn63 <= 0.6 && dn125 <= 0.65 && mid <= 0.04,
                   "%-8s Bass x2 : RT60 x%.2f à 63 Hz, x%.2f à 125 Hz ; x0.5 : x%.2f, x%.2f ; 500 Hz et 1 kHz inchangés à %.1f %% près",
                   NAMES[m], up63, up125, dn63, dn125, mid * 100);
        }

        // Early / Late : -100 % = réflexions seules, +100 % = queue seule, et 0 = la somme des deux
        for (int m : { rv::room, rv::church })
        {
            const double fs = 48000; const Params p = wet (m); const double sec = 1.5;
            Params e = p; e.erLate = -1; Params l = p; l.erLate = 1;
            const Out o0 = irOf (fs, p, sec), oe = irOf (fs, e, sec), ol = irOf (fs, l, sec);
            double d = 0, pk = 0;
            for (size_t i = 0; i < o0.L.size(); ++i) { d = std::max (d, (double) std::abs (o0.L[i] - oe.L[i] - ol.L[i])); pk = std::max (pk, (double) std::abs (o0.L[i])); }
            const double after = dB (energy (oe.L, (size_t) (0.3 * fs)) + energy (oe.R, (size_t) (0.3 * fs))) - dB (energy (oe.L) + energy (oe.R));
            check (d <= pk * 1e-5 && after <= -60, "%-8s Early / Late : 0 %% = réflexions + queue (écart %.0f dB) ; -100 %% : rien après 0.3 s (%.0f dB)", NAMES[m], dB (d * d / (pk * pk)), after);
        }

        // Ducking : -30 dB à 100 % pendant le son direct, retour en moins de 0,5 s
        {
            const double fs = 48000; const size_t n = (size_t) (3 * fs), b1 = (size_t) (1.0 * fs);
            Sig x = noise (n, 5, 0.25f); for (size_t i = b1; i < n; ++i) x[i] = 0;   // salve de 1 s à -12 dBFS (crête)
            Params p = wet (rv::hall); Params d1 = p; d1.ducking = 1; Params d5 = p; d5.ducking = 0.5;
            const Out o = render (fs, p, x, x), od = render (fs, d1, x, x), oh = render (fs, d5, x, x);
            auto win = [&] (const Out& z, double a, double b) { return dB (energy (z.L, (size_t) (a * fs), (size_t) (b * fs)) + energy (z.R, (size_t) (a * fs), (size_t) (b * fs))); };
            const double during = win (od, 0.3, 0.95) - win (o, 0.3, 0.95), half = win (oh, 0.3, 0.95) - win (o, 0.3, 0.95);
            const double back = win (od, 1.5, 2.5) - win (o, 1.5, 2.5);
            const Out o0 = render (fs, p, x, x); bool same = o0.L == o.L;
            check (during <= -27 && during >= -33 && half <= -13 && half >= -17 && std::abs (back) <= 0.5 && same,
                   "Ducking 100 %% : %+.1f dB pendant le son (50 %% : %+.1f dB), %+.2f dB 0.5 s après la fin ; 0 %% : son inchangé", during, half, back);
        }

        // Freeze : la queue tient, l'entrée n'y entre plus ; Off : elle s'éteint
        for (int m : { rv::hall, rv::plate, rv::spring })
        {
            const double fs = 48000; const size_t n = (size_t) (6 * fs), f1 = 48128, f2 = (size_t) (4.5 * fs);
            Params p = wet (m); Params fz = p; fz.freeze = true;
            // b : le son s'arrête dès que Freeze a fini de couper l'entrée (20 ms) ; a : il continue
            const Sig x = noise (n, 9, 0.1f); Sig y = x; for (size_t i = f1 + (size_t) (0.021 * fs); i < n; ++i) y[i] = 0;
            const Out a = render (fs, p, x, x, 512, { { f1, fz }, { f2, p } }), b = render (fs, p, y, y, 512, { { f1, fz }, { f2, p } });
            auto win = [&] (const Out& z, double t0, double t1) { return dB (energy (z.L, (size_t) (t0 * fs), (size_t) (t1 * fs)) + energy (z.R, (size_t) (t0 * fs), (size_t) (t1 * fs))); };
            const double hold = std::max (std::abs (win (b, 1.5, 2.0) - win (b, 3.9, 4.4)), std::abs (win (b, 1.5, 2.0) - win (b, 2.7, 3.2)));
            double d = 0, pk = 0;
            for (size_t i = (size_t) (1.2 * fs); i < f2; ++i) { d = std::max (d, (double) std::abs (a.L[i] - b.L[i])); pk = std::max (pk, (double) std::abs (b.L[i])); }
            const double gone = win (b, 5.5, 6.0) - win (b, 3.9, 4.4);
            check (hold <= 1.5 && d <= pk * 1e-3 && gone <= -20,
                   "%-8s Freeze : la queue tient (±%.2f dB sur 3 s), l'entrée n'entre plus (%.0f dB), Off : %+.0f dB 1 s après", NAMES[m], hold, dB (d * d / (pk * pk)), gone);
        }

        // Réglages qui bougent : aucun clic (sinus 440 Hz, chaque réglage balayé pendant la lecture)
        {
            const double fs = 48000; const size_t n = (size_t) (3 * fs); const Sig sn = sine (n, 440, fs, 0.3f);
            // chaque réglage part de sa valeur du moment et va vers une autre (sans saut) ; le sinus reste audible
            struct Sweep { const char* name; void (*set) (Params&, double); };
            const Sweep sw[] = {
                { "Decay", [] (Params& q, double t) { q.decay = q.decay * std::pow (6 / q.decay, t); } },
                { "Damp", [] (Params& q, double t) { q.damp = q.damp + (1 - q.damp) * t; } },
                { "Mix", [] (Params& q, double t) { q.mix = 1 - t; } },
                { "Width", [] (Params& q, double t) { q.width = 1 - t; } },
                { "Low cut", [] (Params& q, double t) { q.lowCut = 20 * std::pow (15.0, t); } },
                { "High cut", [] (Params& q, double t) { q.highCut = 20000 * std::pow (0.1, t); } },
                { "Bass", [] (Params& q, double t) { q.bass = std::pow (2.0, t); } },
                { "Early / Late", [] (Params& q, double t) { q.erLate = -t; } },
                { "Ducking", [] (Params& q, double t) { q.ducking = t; } },
            };
            for (int m : { rv::hall, rv::plate })
            {
                double worst = 0; const char* who = "";
                for (const auto& s : sw)
                {
                    Params p = wet (m); std::vector<Change> ch;
                    for (size_t k = (size_t) (1.0 * fs); k < (size_t) (2.5 * fs); k += 512) { Params q = p; s.set (q, (double) (k - (size_t) fs) / (1.5 * fs)); ch.push_back ({ k, q }); }
                    const Out o = render (fs, p, sn, sn, 512, ch);
                    const double r = clickRatio (o, fs, (size_t) (0.5 * fs), n);
                    if (r > worst) { worst = r; who = s.name; }
                }
                check (worst <= 1.3, "%-8s 9 réglages balayés pendant la lecture : indice de clic max %.2f (%s) (seuil 1.3)", NAMES[m], worst, who);
            }
        }
    }

    // ═══ 8. Temps réel ═══
    std::printf ("\n8) Temps réel\n");
    {
        // aucune allocation dans process : modes, changements de mode / Size / Pre-delay, Freeze, réglages avancés
        const double fs = 48000; const size_t n = (size_t) (4 * fs);
        rv::ReverbEngine e (fs); Params p = wet (rv::hall); e.setParams (p); e.snap();
        const Sig x = noise (n, 3, 0.1f); Sig oL (n), oR (n);
        gAllocs = 0; gCount = true;
        for (size_t s = 0, k = 0; s < n; s += 256, ++k)
        {
            if (k % 40 == 0) { p.mode = (int) (k / 40) % rv::numModes; p.size = (double) ((k * 7) % 11) / 10; p.predelay = (double) ((k * 13) % 250);
                               p.lowCut = 20 + (double) (k % 5) * 100; p.highCut = 20000 - (double) (k % 3) * 5000; p.bass = 0.5 + (double) (k % 4) * 0.5;
                               p.erLate = (double) (k % 3) - 1; p.ducking = (double) (k % 2); p.freeze = (k / 40) % 5 == 4; e.setParams (p); }
            e.process (x.data() + s, x.data() + s, oL.data() + s, oR.data() + s, (int) std::min<size_t> (256, n - s));
        }
        gCount = false;
        check (gAllocs == 0, "aucune allocation dans le traitement (%ld) : 6 modes, Size, Pre-delay, Freeze et réglages avancés qui changent", (long) gAllocs);

        // même sortie quelle que soit la taille des blocs (réglages fixes : 1 / 64 / 300 / 4096 ; changements : 64 / 256 / 4096)
        const size_t n2 = (size_t) (1.5 * fs);
        const Sig y = noise (n2, 4, 0.2f);
        bool same = true;
        for (int m : { rv::church, rv::plate, rv::spring })
        {
            Params q = wet (m); q.mix = 0.5; q.width = 0.7; q.predelay = 31; q.lowCut = 90; q.highCut = 9000; q.ducking = 0.4; q.erLate = 0.3;
            const Out r = render (fs, q, y, y, 128);
            for (int b : { 1, 64, 300, 4096 }) { const Out o = render (fs, q, y, y, b); same &= o.L == r.L && o.R == r.R; }
            Params q2 = q; q2.size = 0.9; q2.decay = 4; q2.predelay = 80; q2.bass = 1.6;
            Params q3 = q2; q3.mode = rv::room; q3.freeze = true;
            const std::vector<Change> ch { { 16384, q2 }, { 36864, q3 }, { 53248, q } };
            const Out rc = render (fs, q, y, y, 4096, ch);
            for (int b : { 64, 256 }) { const Out o = render (fs, q, y, y, b, ch); same &= o.L == rc.L && o.R == rc.R; }
        }
        check (same, "même sortie au bit près avec des blocs de 1, 64, 300 et 4096 échantillons (et réglages qui changent)");

        // extrêmes
        bool fine = true; double mx = 0;
        for (int m = 0; m < rv::numModes; ++m)
            for (double fs2 : { 44100.0, 192000.0 })
            {
                Params q = wet (m); q.decay = 10; q.size = 1; q.damp = 0; q.bass = 2; q.lowCut = 20; q.highCut = 20000;
                const size_t n3 = (size_t) (2 * fs2); const Sig z = noise (n3, 8, 0.5f);
                const Out o = render (fs2, q, z, z, 512);
                for (size_t i = 0; i < n3; ++i) { fine &= std::isfinite (o.L[i]) && std::isfinite (o.R[i]); mx = std::max (mx, (double) std::abs (o.L[i])); }
            }
        check (fine && mx < 8, "extrêmes (Decay 10 s, Size 100 %%, Damp 0, Bass x2) à 44,1 et 192 kHz : sortie saine (crête %.2f)", mx);

        // vitesse (un moteur ; deux pendant un changement)
        for (double fs2 : { 48000.0, 96000.0 })
        {
            double slow = 1e9; const char* who = "";
            for (int m = 0; m < rv::numModes; ++m)
            {
                const size_t n4 = (size_t) (4 * fs2); const Sig z = noise (n4, 2, 0.1f); Sig a (n4), b (n4);
                rv::ReverbEngine e2 (fs2); Params q = defaults (m); e2.setParams (q); e2.snap();
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t s = 0; s < n4; s += 512) e2.process (z.data() + s, z.data() + s, a.data() + s, b.data() + s, (int) std::min<size_t> (512, n4 - s));
                const double sp = 4 / std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                if (sp < slow) { slow = sp; who = NAMES[m]; }
            }
            check (slow >= (fs2 > 50000 ? 5 : 10), "vitesse à %.0f kHz : au moins %.0f fois le temps réel (le plus lent : %s)", fs2 / 1000, slow, who);
        }
    }

    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
