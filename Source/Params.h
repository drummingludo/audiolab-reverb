// Paramètres du plugin « Audio Lab Reverb » : les six réglages de la maquette + le mode + le bypass.
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

// Decay : échelle logarithmique de 0,1 à 10 s (même course que le potentiomètre de la maquette)
inline juce::NormalisableRange<float> decayRange()
{
    return { 0.1f, 10.0f,
             [] (float, float, float v) { return 0.1f * std::pow (100.0f, v); },
             [] (float, float, float t) { return std::log (t / 0.1f) / std::log (100.0f); },
             [] (float, float, float t) { return juce::jlimit (0.1f, 10.0f, t < 1 ? std::round (t * 100) / 100 : std::round (t * 10) / 10); } };
}

inline juce::String decayText (float t) { return t < 1 ? juce::String (t, 2) : juce::String (t, 1); }

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
    return l;
}
} // namespace audiolab::params
