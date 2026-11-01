// Écran de la maquette : courbe de décroissance réelle du moteur, réflexions précoces, repère RT60.
// La réponse impulsionnelle est calculée à 48 kHz dans un fil de fond (jamais dans le fil audio).
#pragma once
#include "Widgets.h"
#include "ReverbCore.h"

namespace audiolab
{
inline bool sameParams (const rv::ReverbEngine::Params& a, const rv::ReverbEngine::Params& b)
{
    using rv::same;
    return a.mode == b.mode && same (a.predelay, b.predelay) && same (a.decay, b.decay) && same (a.size, b.size) && same (a.damp, b.damp) && same (a.width, b.width);
}

// Durée de l'axe et graduations, comme la maquette (Room 0,6 s → 1 s, Chamber 1,5 s → 2 s, Plate 2,2 s → 3 s, Church 5 s → 6 s)
inline double axisFor (double t)
{
    for (double c : { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 10.0, 12.0, 16.0 }) if (c >= t - 1e-9) return c;
    return 16.0;
}
inline double axisFor (const rv::ReverbEngine::Params& p) { return axisFor (std::max (p.decay * 1.2, p.predelay / 1000 + p.decay)); }
inline std::vector<double> axisTicks (double T)
{
    if (T <= 2) return { 0, T / 4, T / 2, T };
    if (T <= 3) return { 0, 1, 2, 3 };
    if (T <= 4) return { 0, 1, 2, 4 };
    if (T <= 6) return { 0, 1, 2, 4, 6 };
    if (T <= 8) return { 0, 2, 4, 8 };
    if (T <= 10) return { 0, 2, 5, 10 };
    if (T <= 12) return { 0, 3, 6, 12 };
    return { 0, 4, 8, 16 };
}

struct IrResult
{
    std::vector<float> env;              // enveloppe RMS par tranches de 10 ms (depuis l'impulsion)
    std::vector<rv::Tap> taps;           // réflexions précoces (ou retours du ressort), temps depuis la fin du pré-délai
    rv::ReverbEngine::Params p;
    bool valid = false;
};

class IrRenderer : private juce::Thread
{
public:
    IrRenderer() : juce::Thread ("Audio Lab Reverb - courbe") { startThread (juce::Thread::Priority::low); }
    ~IrRenderer() override { signalThreadShouldExit(); notify(); stopThread (3000); }

    void request (const rv::ReverbEngine::Params& p)
    {
        { const juce::ScopedLock sl (lock); want = p; ++wantId; }
        notify();
    }
    bool fetch (IrResult& out)
    {
        const juce::ScopedLock sl (lock);
        if (! fresh) return false;
        out = result; fresh = false; return true;
    }

private:
    void run() override
    {
        int done = 0;
        while (! threadShouldExit())
        {
            rv::ReverbEngine::Params p; int id;
            { const juce::ScopedLock sl (lock); p = want; id = wantId; }
            if (id == done) { wait (-1); continue; }
            IrResult r;
            if (render (p, id, r))
            {
                const juce::ScopedLock sl (lock);
                result = std::move (r); fresh = true;
            }
            done = id;
        }
    }
    bool render (rv::ReverbEngine::Params p, int id, IrResult& r)
    {
        const double sr = 48000;
        p.mix = 1;
        rv::ReverbEngine e (sr);
        e.setParams (p); e.snap();
        const double secs = std::min (14.0, axisFor (p));
        const int n = (int) (secs * sr), blk = 480, chunk = 4800;
        std::vector<float> iL ((size_t) chunk, 0.0f), oL ((size_t) chunk), oR ((size_t) chunk);
        r.env.assign ((size_t) (n / blk), 0.0f);
        double acc = 0; int cnt = 0; size_t k = 0;
        for (int s = 0; s < n; s += chunk)
        {
            if (threadShouldExit()) return false;
            { const juce::ScopedLock sl (lock); if (wantId != id) return false; }   // une demande plus récente est arrivée
            std::fill (iL.begin(), iL.end(), 0.0f);
            if (s == 0) iL[0] = 1;
            const int m = std::min (chunk, n - s);
            e.process (iL.data(), iL.data(), oL.data(), oR.data(), m);
            for (int i = 0; i < m; ++i)
            {
                acc += (double) oL[(size_t) i] * oL[(size_t) i] + (double) oR[(size_t) i] * oR[(size_t) i];
                if (++cnt == blk && k < r.env.size()) { r.env[k++] = (float) std::sqrt (acc / blk); acc = 0; cnt = 0; }
            }
        }
        const int nt = e.numTaps();
        const rv::Tap* t = e.tapList();
        for (int i = 0; i < nt; ++i) r.taps.push_back (t[i]);
        r.p = p; r.valid = true;
        return true;
    }

    juce::CriticalSection lock;
    rv::ReverbEngine::Params want;
    int wantId = 0;
    IrResult result;
    bool fresh = false;
};

// Écran 520 x 222 : bord « Rails », fond « Surface », dessin dans le repère 420 x 170 de la maquette
class DecayGraph : public juce::Component
{
public:
    explicit DecayGraph (Theme& t) : theme (t) {}

    void setResult (IrResult r) { res = std::move (r); repaint(); }
    void setParams (const rv::ReverbEngine::Params& p) { if (! sameParams (p, cur)) { cur = p; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        auto box = getLocalBounds().toFloat();
        g.setColour (p.surface); g.fillRoundedRectangle (box, 10);
        g.setColour (p.rail); g.drawRoundedRectangle (box.reduced (0.5f), 10, 1);

        // repère de la maquette (420 x 170), mis à l'échelle de la zone utile
        const float k = (box.getWidth() - 30) / 420.0f;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (box.reduced (1).toNearestInt());
        g.addTransform (juce::AffineTransform::scale (k).translated (15, 13));

        const double pre = cur.predelay / 1000, rt = cur.decay, T = axisFor (cur);
        auto X = [&] (double t) { return (float) (10 + std::min (1.0, t / T) * 400); };
        auto Y = [&] (double a) { return (float) (140 - a * 120); };
        const auto ticks = axisTicks (T);
        g.setColour (p.rail);
        for (size_t i = 1; i < ticks.size(); ++i) g.fillRect (X (ticks[i]) - 0.5f, 20.0f, 1.0f, 120.0f);

        if (res.valid && res.p.mode == cur.mode && ! res.env.empty())
        {
            // enveloppe lissée sur 70 ms pour que les premières réflexions n'écrasent pas la queue
            const auto& raw = res.env; std::vector<float> env (raw.size()); float mx = 1e-12f;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                double s = 0; int c = 0;
                for (size_t j = i >= 3 ? i - 3 : 0; j <= std::min (raw.size() - 1, i + 3); ++j) { s += raw[j]; ++c; }
                env[i] = (float) (s / c); mx = std::max (mx, env[i]);
            }
            const double rpre = res.p.predelay / 1000;
            juce::Path curve, fill;
            fill.startNewSubPath (10, 140);
            float lastX = 10;
            for (size_t i = 0; i < env.size(); ++i)
            {
                const double t = i * 0.01; if (t > T) break;
                const float x = X (t), y = Y (env[i] / mx);
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                fill.lineTo (x, y); lastX = x;
            }
            fill.lineTo (lastX, 140); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (p.orange.withAlpha (0.25f), 0, 20, p.orange.withAlpha (0.02f), 0, 140, false));
            g.fillPath (fill);
            // réflexions précoces : barres teal secondaire
            float gm = 1e-12f; for (auto& tp : res.taps) gm = std::max (gm, (float) tp.g);
            g.setColour (p.teal2.withAlpha (0.7f));
            for (auto& tp : res.taps)
            {
                const double t = rpre + tp.t; if (t >= T) continue;
                const float h = 85 * (float) tp.g / gm;
                g.fillRect (X (t) - 1.25f, 140 - h, 2.5f, h);
            }
            g.setColour (p.orange);
            g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // repère RT60 (Decay réglé)
        const float xr = X (pre + rt);
        juce::Path dash; dash.startNewSubPath (xr, 20); dash.lineTo (xr, 140);
        const float dl[] = { 3, 3 }; juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, dash, dl, 2);
        g.setColour (p.orange); g.fillPath (dashed);
        g.setFont (theme.ui (9));
        const auto lab = "RT60 " + juce::String (rt < 1 ? juce::String (rt, 2).trimCharactersAtEnd ("0") : juce::String (rt, 1)) + " s";
        g.drawText (lab, juce::Rectangle<float> (xr - 6 - 120, 4, 120, 14), juce::Justification::bottomRight, false);

        // graduations
        g.setColour (p.soft);
        for (size_t i = 0; i < ticks.size(); ++i)
        {
            const double t = ticks[i];
            const juce::String s = i == 0 ? juce::String ("0") : t < 1 ? juce::String (juce::roundToInt (t * 1000)) + " ms"
                                                                         : juce::String (t, std::abs (t - std::round (t)) < 1e-9 ? 0 : 1) + " s";
            const float x = X (t);
            g.drawText (s, juce::Rectangle<float> (i == 0 ? x : x - 40, 150, i == 0 ? 60.0f : 80.0f, 12),
                        i == 0 ? juce::Justification::left : juce::Justification::centred, false);
        }
    }

private:
    Theme& theme;
    IrResult res;
    rv::ReverbEngine::Params cur;
};
} // namespace audiolab
