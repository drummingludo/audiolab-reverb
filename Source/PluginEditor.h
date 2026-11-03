#pragma once
#include "PluginProcessor.h"
#include "ReverbWidgets.h"
#include "DecayGraph.h"

class AudioLabReverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit AudioLabReverbEditor (AudioLabReverbProcessor&);
    ~AudioLabReverbEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Version de test sans écran (demande de Ludovic du 03/11/2026) : une colonne, mêmes tailles
    // d'éléments que le compresseur (grille de 24 px) : modes en 2 rangées de 3, Pre-delay / Decay, 4 curseurs
    static constexpr int baseW = 286, baseH = 386;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabReverbEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabReverbEditor& ed;
    };

    float modeDefault (const char* id) const;

    AudioLabReverbProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<audiolab::ReverbKnob> kPre, kDec;
    std::unique_ptr<audiolab::LineSlider> sMix, sSize, sDamp, sWidth;
    audiolab::SegButtons tabs { theme, { "Chamber", "Plate", "Spring", "Room", "Hall", "Church" } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabReverbEditor)
};
