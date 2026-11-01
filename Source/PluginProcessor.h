#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ReverbCore.h"
#include "Params.h"

class AudioLabReverbProcessor : public juce::AudioProcessor
{
public:
    AudioLabReverbProcessor();
    ~AudioLabReverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree ui;   // taille de la fenêtre
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // Réglages actuels convertis pour le moteur (mix, size, damp, width de 0 à 1)
    audiolab::rv::ReverbEngine::Params currentParams() const;

    // Passe au mode choisi et charge ses valeurs de la maquette (geste unique pour l'hôte)
    void selectMode (int mode);

private:
    std::unique_ptr<audiolab::rv::ReverbEngine> engine;
    audiolab::rv::ReverbEngine::Params sent;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::vector<float> inR, outL, outR;
    juce::SmoothedValue<float> bypassSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabReverbProcessor)
};
