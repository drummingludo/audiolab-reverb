// Paramètres du plugin « Audio Lab Reverb » (Lab Verb) : les six réglages de la maquette + le mode + le bypass ;
// 2.0.0 : six réglages avancés (identifiants nouveaux, version 2 ; valeurs par défaut = son de la face principale).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ReverbCore.h"

namespace audiolab::params
{
inline constexpr const char* mode     = "mode";
inline constexpr const char* predelay = "predelay";
inline constexpr const char* decay    = "decay";
inline constexpr const char* mix      = "mix";
inline constexpr const char* size     = "size";
inline constexpr const char* damp     = "damp";
inline constexpr const char* width    = "width";
inline constexpr const char* bypass   = "bypass";
// 2.0.0 (panneau Advanced settings)
inline constexpr const char* lowCut   = "lowcut";
inline constexpr const char* highCut  = "highcut";
inline constexpr const char* bass     = "bass";
inline constexpr const char* erLate   = "erlate";
inline constexpr const char* ducking  = "ducking";
inline constexpr const char* freeze   = "freeze";

// Decay : échelle logarithmique de 0,1 à 10 s (même course que le potentiomètre de la maquette)
inline juce::NormalisableRange<float> decayRange()
{
    return { 0.1f, 10.0f,
             [] (float, float, float v) { return 0.1f * std::pow (100.0f, v); },
             [] (float, float, float t) { return std::log (t / 0.1f) / std::log (100.0f); },
             [] (float, float, float t) { return juce::jlimit (0.1f, 10.0f, t < 1 ? std::round (t * 100) / 100 : std::round (t * 10) / 10); } };
}

inline juce::String decayText (float t) { return t < 1 ? juce::String (t, 2) : juce::String (t, 1); }

// Échelle logarithmique de a à b (curseurs de fréquence et Bass), arrondie au pas « snap »
inline juce::NormalisableRange<float> logRange (float a, float b, std::function<float (float)> snap)
{
    return { a, b,
             [a, b] (float, float, float v) { return a * std::pow (b / a, v); },
             [a, b] (float, float, float t) { return std::log (t / a) / std::log (b / a); },
             [a, b, snap] (float, float, float t) { return juce::jlimit (a, b, snap (t)); } };
}
inline const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");   // vrai signe moins
inline juce::String lowCutText (float f) { return f <= 20.5f ? juce::String ("Off") : juce::String (juce::roundToInt (f)) + " Hz"; }
inline juce::String highCutText (float f)
{
    if (f >= 19999.0f) return "Off";
    if (f < 10000) return juce::String (f / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (f / 1000.0f)) + " kHz";
}
inline juce::String bassText (float v) { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (v, 2); }
inline juce::String signedPct (float v)
{
    const int i = juce::roundToInt (v);
    return (i > 0 ? "+" + juce::String (i) : i < 0 ? minus + juce::String (-i) : juce::String ("0")) + " %";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto& d = rv::modeDef (rv::chamber).def;   // valeurs par défaut : mode Chamber de la maquette
    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                   std::function<String (float, int)> txt)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (txt))));
    };
    StringArray modes;
    for (int m = 0; m < rv::numModes; ++m) modes.add (rv::modeDef (m).label);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { mode, 1 }, "Mode", modes, 0));
    fl (predelay, "Pre-delay", NormalisableRange<float> (0, 250, 1), (float) d.predelay, "ms",
        [] (float v, int) { return String (roundToInt (v)) + " ms"; });
    fl (decay, "Decay", decayRange(), (float) d.decay, "s", [] (float v, int) { return decayText (v) + " s"; });
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    fl (mix, "Mix", NormalisableRange<float> (0, 100, 1), (float) (d.mix * 100), "%", pct);
    fl (size, "Size", NormalisableRange<float> (0, 100, 1), (float) (d.size * 100), "%", pct);
    fl (damp, "Damp", NormalisableRange<float> (0, 100, 1), (float) (d.damp * 100), "%", pct);
    fl (width, "Width", NormalisableRange<float> (0, 100, 1), (float) (d.width * 100), "%", pct);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));

    // 2.0.0 : réglages avancés, valeurs par défaut neutres (une session de la 1.x se rouvre à l'identique)
    auto fl2 = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                    std::function<String (float, int)> txt)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 2 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (txt))));
    };
    fl2 (lowCut, "Low Cut", logRange (20, 1000, [] (float f) { return std::round (f); }), 20, "Hz",
         [] (float v, int) { return lowCutText (v); });
    fl2 (highCut, "High Cut", logRange (1000, 20000, [] (float f) { return f < 10000 ? std::round (f / 10) * 10 : std::round (f / 100) * 100; }), 20000, "Hz",
         [] (float v, int) { return highCutText (v); });
    fl2 (bass, "Bass", logRange (0.5f, 2.0f, [] (float v) { return std::round (v * 100) / 100; }), 1, "",
         [] (float v, int) { return bassText (v); });
    fl2 (erLate, "Early / Late", NormalisableRange<float> (-100, 100, 1), 0, "%", [] (float v, int) { return signedPct (v); });
    fl2 (ducking, "Ducking", NormalisableRange<float> (0, 100, 1), 0, "%", pct);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { freeze, 2 }, "Freeze", false));
    return l;
}
} // namespace audiolab::params
