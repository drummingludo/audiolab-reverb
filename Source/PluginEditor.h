#pragma once
#include "PluginProcessor.h"
#include "ReverbWidgets.h"
#include "DecayGraph.h"

class AudioLabReverbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioLabReverbEditor (AudioLabReverbProcessor&);
    ~AudioLabReverbEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Mêmes tailles d'éléments que le compresseur (grille de 24 px) : 760 x 304
    static constexpr int baseW = 760, baseH = 304;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabReverbEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabReverbEditor& ed;
    };

    void timerCallback() override;
    float modeDefault (const char* id) const;

    AudioLabReverbProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<audiolab::ReverbKnob> kPre, kDec;
    std::unique_ptr<audiolab::LineSlider> sMix, sSize, sDamp, sWidth;
    audiolab::SegButtons tabs { theme, { "Chamber", "Plate", "Spring", "Room", "Hall", "Church" } };
    audiolab::DecayGraph graph { theme };
    audiolab::IrRenderer renderer;
    audiolab::rv::ReverbEngine::Params requested;
    bool firstRequest = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabReverbEditor)
};
