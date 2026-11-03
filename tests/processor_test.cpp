// Tests du plugin hors DAW : sauvegarde / rappel, choix d'un mode, bypass, Mix 0 %, mono, blocs, latence, extrêmes ;
// 2.0.0 : réglages avancés (défauts neutres, sauvegarde, session de la 1.x qui se rouvre), textes, longueur de queue.
#include "PluginProcessor.h"
#include <cstdarg>
#include <cstdio>

static bool OK = true;
static void check (bool c, const char* fmt, ...)
{
    std::printf ("  %s ", c ? "OK   " : "ÉCHEC");
    va_list a; va_start (a, fmt); std::vprintf (fmt, a); va_end (a);
    std::printf ("\n"); OK &= c;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace audiolab;
    using Proc = AudioLabReverbProcessor;
    auto set = [] (Proc& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto music = [] (juce::AudioBuffer<float>& b, int64_t& pos, double fs)
    {
        for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
        {
            const double t = pos / fs, tb = std::fmod (t, 0.5);
            const double s = std::sin (2 * juce::MathConstants<double>::pi * (60 + 140 * std::exp (-tb * 30)) * tb) * std::exp (-tb * 12) * 0.6
                           + 0.15 * std::sin (2 * juce::MathConstants<double>::pi * 330 * t) * (t < 3 ? 1 : 0);
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, (float) (c == 0 ? s : 0.9 * s));
        }
    };

    std::puts ("\nPlugin Audio Lab Reverb (Lab Verb 2.0.0)");
    const char* newIds[] = { params::lowCut, params::highCut, params::bass, params::erLate, params::ducking, params::freeze };
    {
        Proc p;
        check (std::abs (p.getParam (params::lowCut) - 20) < 1e-4 && std::abs (p.getParam (params::highCut) - 20000) < 1e-2
               && std::abs (p.getParam (params::bass) - 1) < 1e-6 && std::abs (p.getParam (params::erLate)) < 1e-6
               && std::abs (p.getParam (params::ducking)) < 1e-6 && p.getParam (params::freeze) < 0.5f,
               "réglages avancés par défaut neutres (Low cut 20 Hz, High cut 20 kHz, Bass x1, Early / Late 0, Ducking 0, Freeze Off)");
        auto txt = [&] (const char* id, float v) { auto* q = p.apvts.getParameter (id); return q->getText (q->convertTo0to1 (v), 32); };
        const auto mn = juce::String::fromUTF8 ("\xe2\x88\x92"), times = juce::String::fromUTF8 ("\xc3\x97");
        check (txt (params::lowCut, 20) == "Off" && txt (params::lowCut, 150) == "150 Hz" && txt (params::highCut, 20000) == "Off"
               && txt (params::highCut, 6500) == "6.5 kHz" && txt (params::highCut, 12000) == "12 kHz" && txt (params::bass, 1) == times + "1.00"
               && txt (params::erLate, -40) == mn + "40 %" && txt (params::erLate, 25) == "+25 %" && txt (params::erLate, 0) == "0 %",
               "textes des réglages avancés (Off, 150 Hz, 6.5 kHz, ×1.00, −40 %%, +25 %%)");
    }
    {
        Proc a; set (a, params::mode, 4); set (a, params::predelay, 37); set (a, params::decay, 3.4f); set (a, params::mix, 42);
        set (a, params::size, 63); set (a, params::damp, 21); set (a, params::width, 55); a.ui.setProperty ("scale", 1.4, nullptr);
        set (a, params::lowCut, 140); set (a, params::highCut, 7000); set (a, params::bass, 1.5f); set (a, params::erLate, -30);
        set (a, params::ducking, 45); set (a, params::freeze, 1); a.ui.setProperty ("adv", true, nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        Proc b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.getParam (params::lowCut) - 140) < 0.5f && std::abs (b.getParam (params::highCut) - 7000) < 10
               && std::abs (b.getParam (params::bass) - 1.5f) < 0.01f && std::abs (b.getParam (params::erLate) + 30) < 1e-3
               && std::abs (b.getParam (params::ducking) - 45) < 1e-3 && b.getParam (params::freeze) > 0.5f && (bool) b.ui.getProperty ("adv"),
               "sauvegarde / rappel des 6 réglages avancés et du panneau ouvert");
        check (juce::roundToInt (b.getParam (params::mode)) == 4 && std::abs (b.getParam (params::predelay) - 37) < 1e-4
               && std::abs (b.getParam (params::decay) - 3.4f) < 1e-3 && std::abs (b.getParam (params::mix) - 42) < 1e-4
               && std::abs (b.getParam (params::size) - 63) < 1e-4 && std::abs (b.getParam (params::damp) - 21) < 1e-4
               && std::abs (b.getParam (params::width) - 55) < 1e-4 && std::abs ((double) b.ui.getProperty ("scale") - 1.4) < 1e-9,
               "sauvegarde / rappel de la session (mode, 6 réglages, taille de la fenêtre)");
    }
    {
        // session enregistrée par la 1.x : elle ne contient pas les nouveaux paramètres → valeurs par défaut (son de la 1.x)
        Proc a; set (a, params::mode, 3); set (a, params::decay, 1.2f); set (a, params::mix, 33);
        auto st = a.apvts.copyState();
        for (auto* id : newIds) { auto c = st.getChildWithProperty ("id", id); if (c.isValid()) st.removeChild (c, nullptr); }
        bool absent = true; for (auto* id : newIds) absent &= ! st.getChildWithProperty ("id", id).isValid();
        juce::MemoryBlock mb; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        Proc b;   // réglages avancés déplacés avant de charger : la session doit les remettre au neutre
        set (b, params::lowCut, 300); set (b, params::highCut, 3000); set (b, params::bass, 2); set (b, params::erLate, 80); set (b, params::ducking, 90); set (b, params::freeze, 1);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (absent && juce::roundToInt (b.getParam (params::mode)) == 3 && std::abs (b.getParam (params::decay) - 1.2f) < 1e-3
               && std::abs (b.getParam (params::mix) - 33) < 1e-4 && std::abs (b.getParam (params::lowCut) - 20) < 1e-4
               && std::abs (b.getParam (params::highCut) - 20000) < 1e-2 && std::abs (b.getParam (params::bass) - 1) < 1e-6
               && std::abs (b.getParam (params::erLate)) < 1e-6 && std::abs (b.getParam (params::ducking)) < 1e-6 && b.getParam (params::freeze) < 0.5f,
               "session de la 1.x (sans les nouveaux paramètres) : réglages rappelés, réglages avancés au neutre");
    }
    {
        Proc p; set (p, params::lowCut, 200); set (p, params::ducking, 30); p.selectMode (5);
        check (std::abs (p.getParam (params::lowCut) - 200) < 0.5f && std::abs (p.getParam (params::ducking) - 30) < 1e-3,
               "choisir un mode ne touche pas aux réglages avancés");
        const double t0 = p.getTailLengthSeconds(); set (p, params::bass, 2); const double t1 = p.getTailLengthSeconds();
        set (p, params::freeze, 1); const double t2 = p.getTailLengthSeconds();
        check (t1 > t0 * 1.9 && t2 >= 3600, "longueur de queue annoncée : %.1f s, %.1f s avec Bass x2, %.0f s avec Freeze", t0, t1, t2);
    }
    {
        Proc p; bool all = true;
        for (int m = 0; m < rv::numModes; ++m)
        {
            p.selectMode (m);
            const auto q = p.currentParams(); const auto& d = rv::modeDef (m).def;
            all &= q.mode == m && std::abs (q.predelay - d.predelay) < 1e-6 && std::abs (q.decay - d.decay) < 1e-3 && std::abs (q.mix - d.mix) < 1e-6
                   && std::abs (q.size - d.size) < 1e-6 && std::abs (q.damp - d.damp) < 1e-6 && std::abs (q.width - d.width) < 1e-6;
        }
        check (all, "choisir un mode charge ses valeurs de la maquette (6 modes)");
    }
    {
        Proc p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        set (p, params::bypass, 1);
        juce::AudioBuffer<float> b (2, 512), ref (2, 512); juce::MidiBuffer m; int64_t pos = 0, pr = 0; bool same = true;
        for (int k = 0; k < 60; ++k) { music (b, pos, 48000); music (ref, pr, 48000); p.processBlock (b, m);
            if (k > 5) for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) same &= juce::exactlyEqual (b.getSample (c, i), ref.getSample (c, i)); }
        check (same, "bypass : la sortie est exactement l'entrée");
    }
    {
        Proc p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        set (p, params::mix, 0); p.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512), ref (2, 512); juce::MidiBuffer m; int64_t pos = 0, pr = 0; bool same = true;
        for (int k = 0; k < 40; ++k) { music (b, pos, 48000); music (ref, pr, 48000); p.processBlock (b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) same &= juce::exactlyEqual (b.getSample (c, i), ref.getSample (c, i)); }
        check (same, "Mix à 0 %% : son direct exact");
    }
    {
        // la sortie ne dépend pas de la taille des blocs envoyés par le logiciel
        auto render = [&] (int block)
        {
            Proc p; p.setPlayConfigDetails (2, 2, 48000, block); set (p, params::mode, 5); p.prepareToPlay (48000, block);
            std::vector<float> out; juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m; int64_t pos = 0;
            for (int s = 0; s < 48000 * 2; s += block) { music (b, pos, 48000); p.processBlock (b, m); for (int i = 0; i < block; ++i) out.push_back (b.getSample (1, i)); }
            out.resize (48000 * 2); return out;
        };
        const auto a = render (64), b2 = render (4096), c = render (300);
        check (a == b2 && a == c, "même sortie avec des blocs de 64, 300 et 4096 échantillons");
    }
    {
        Proc p; p.setPlayConfigDetails (1, 2, 44100, 256); p.prepareToPlay (44100, 256);
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; int64_t pos = 0; float l = 0, r = 0, diff = 0;
        for (int k = 0; k < 300; ++k) { b.clear(); juce::AudioBuffer<float> mono (1, 256); music (mono, pos, 44100); b.copyFrom (0, 0, mono, 0, 0, 256);
            p.processBlock (b, m); l = std::max (l, b.getMagnitude (0, 0, 256)); r = std::max (r, b.getMagnitude (1, 0, 256));
            for (int i = 0; i < 256; ++i) diff = std::max (diff, std::abs (b.getSample (0, i) - b.getSample (1, i))); }
        check (std::isfinite (l) && l > 0.05f && r > 0.05f && diff > 1e-3f, "mono vers stéréo : deux voies saines et différentes (largeur)");
        Proc q; q.setPlayConfigDetails (1, 1, 96000, 333); q.prepareToPlay (96000, 333);
        juce::AudioBuffer<float> mb (1, 333); float mx = 0; int64_t pq = 0;
        for (int k = 0; k < 400; ++k) { music (mb, pq, 96000); q.processBlock (mb, m); mx = std::max (mx, mb.getMagnitude (0, 0, 333)); }
        check (std::isfinite (mx) && mx > 0.05f && mx < 4, "mono vers mono à 96 kHz (blocs de 333)");
    }
    {
        Proc p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        check (p.getLatencySamples() == 0, "aucune latence annoncée");
        set (p, params::mix, 100);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; float tail = 0;
        b.clear(); b.setSample (0, 0, 1); b.setSample (1, 0, 1); p.processBlock (b, m);
        for (int k = 0; k < 90; ++k) { b.clear(); p.processBlock (b, m); if (k > 80) tail = std::max (tail, b.getMagnitude (0, 512)); }
        check (tail > 1e-6f, "la queue continue après la fin du son (≈ 1 s plus tard)");
    }
    {
        bool fine = true;
        for (int m = 0; m < rv::numModes; ++m)
        {
            Proc p; p.setPlayConfigDetails (2, 2, 44100, 512); p.prepareToPlay (44100, 512);
            p.selectMode (m); set (p, params::decay, 10); set (p, params::size, 100); set (p, params::damp, 0); set (p, params::mix, 100);
            set (p, params::bass, 2); set (p, params::erLate, 100); set (p, params::ducking, 100);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mm; int64_t pos = 0; float mx = 0;
            for (int k = 0; k < 900; ++k) { music (b, pos, 44100); p.processBlock (b, mm); mx = std::max (mx, b.getMagnitude (0, 512)); }
            for (int k = 0; k < 200; ++k) { set (p, params::size, (float) ((k * 37) % 101)); set (p, params::decay, 0.1f + (k % 7) * 1.4f);
                set (p, params::lowCut, 20 + (k % 5) * 200.0f); set (p, params::highCut, 20000 - (k % 4) * 4500.0f); set (p, params::freeze, (k / 50) % 2 ? 1.0f : 0.0f);
                set (p, params::predelay, (float) ((k * 13) % 251));
                music (b, pos, 44100); p.processBlock (b, mm); mx = std::max (mx, b.getMagnitude (0, 512)); }
            fine &= std::isfinite (mx) && mx < 8;
        }
        check (fine, "extrêmes (Decay 10 s, Size 100 %%, Damp 0, Bass x2) et réglages qui bougent (Size, Decay, Pre-delay, filtres, Freeze) : sortie saine dans les 6 modes");
    }
    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}
