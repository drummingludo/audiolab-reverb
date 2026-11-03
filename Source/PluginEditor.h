#pragma once
#include "PluginProcessor.h"
#include "ReverbWidgets.h"
#include "DecayGraph.h"

class AudioLabReverbEditor : public juce::AudioProcessorEditor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    explicit AudioLabReverbEditor (AudioLabReverbProcessor&);
    ~AudioLabReverbEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // Version de test sans écran (demande de Ludovic du 03/11/2026) : une colonne, mêmes tailles
    // d'éléments que le compresseur (grille de 24 px) : modes en 2 rangées de 3, Pre-delay / Decay, 4 curseurs.
    // 2.0.0 : face inchangée (286 x 386) + rangée du bouton « Advanced settings » : 286 x 422 replié,
    // 286 x 666 déplié (panneau de 6 lignes, modèle du Compressor)
    static constexpr int baseW = 286, faceH = 386, baseH = 422, baseHOpen = 666;
    static constexpr int advRows = 6;
    bool isAdvancedOpen() const { return advOpen; }

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
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void refreshState();
    void setAdvanced (bool open, bool resizeWindow);
    int currentH() const { return advOpen ? baseHOpen : baseH; }

    AudioLabReverbProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<audiolab::ReverbKnob> kPre, kDec;
    std::unique_ptr<audiolab::LineSlider> sMix, sSize, sDamp, sWidth;
    audiolab::SegButtons tabs { theme, { "Chamber", "Plate", "Spring", "Room", "Hall", "Church" } };

    // 2.0.0 : panneau « Advanced settings »
    audiolab::Disclosure advBtn { theme, "Advanced settings" };
    std::unique_ptr<audiolab::SliderRow> rLow, rHigh, rBass, rErl, rDuck;
    audiolab::SegButtons freezeSeg { theme, { "Off", "On" } };
    juce::Array<juce::Component*> advItems;
    float labelW = 68;
    bool advOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabReverbEditor)
};
