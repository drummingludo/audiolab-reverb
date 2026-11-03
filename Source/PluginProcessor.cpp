#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace audiolab;

AudioLabReverbProcessor::AudioLabReverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AudioLabReverb", params::createLayout()),
      ui ("ui")
{
    bypassParam = apvts.getParameter (params::bypass);
    ui.setProperty ("scale", 1.0, nullptr);
}

bool AudioLabReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // mono → mono, mono → stéréo, stéréo → stéréo
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return ! (out == juce::AudioChannelSet::mono() && in == juce::AudioChannelSet::stereo());
}

rv::ReverbEngine::Params AudioLabReverbProcessor::currentParams() const
{
    rv::ReverbEngine::Params p;
    p.mode = juce::jlimit (0, (int) rv::numModes - 1, juce::roundToInt (getParam (params::mode)));
    p.predelay = getParam (params::predelay);
    p.decay = getParam (params::decay);
    p.mix = getParam (params::mix) / 100.0;
    p.size = getParam (params::size) / 100.0;
    p.damp = getParam (params::damp) / 100.0;
    p.width = getParam (params::width) / 100.0;
    // 2.0.0 : réglages avancés
    p.lowCut = getParam (params::lowCut);
    p.highCut = getParam (params::highCut);
    p.bass = getParam (params::bass);
    p.erLate = getParam (params::erLate) / 100.0;
    p.ducking = getParam (params::ducking) / 100.0;
    p.freeze = getParam (params::freeze) > 0.5f;
    return p;
}

void AudioLabReverbProcessor::selectMode (int mode)
{
    const auto& d = rv::modeDef (mode).def;
    const std::pair<const char*, float> vals[] = {
        { params::mode, (float) mode }, { params::predelay, (float) d.predelay }, { params::decay, (float) d.decay },
        { params::mix, (float) (d.mix * 100) }, { params::size, (float) (d.size * 100) },
        { params::damp, (float) (d.damp * 100) }, { params::width, (float) (d.width * 100) } };
    for (auto& [id, v] : vals)
    {
        auto* q = apvts.getParameter (id);
        q->beginChangeGesture();
        q->setValueNotifyingHost (q->convertTo0to1 (v));
        q->endChangeGesture();
    }
}

double AudioLabReverbProcessor::getTailLengthSeconds() const
{
    if (getParam (params::freeze) > 0.5f) return 3600;   // queue gelée : elle dure tant que Freeze reste allumé
    const int m = juce::jlimit (0, (int) rv::numModes - 1, juce::roundToInt (getParam (params::mode)));
    const auto& d = rv::modeDef (m);
    const double lowMul = d.engine == rv::springEngine ? 1.0 : std::max (1.0, (d.engine == rv::roomEngine ? d.bass : 1.0) * getParam (params::bass));
    return getParam (params::decay) * lowMul * 1.6 + getParam (params::predelay) / 1000.0 + 0.1;
}

void AudioLabReverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // deux moteurs complets (changement de Size ou de mode en fondu) : toute la mémoire est réservée ici
    engine = std::make_unique<rv::ReverbEngine> (sampleRate);
    engine->setParams (currentParams());
    engine->snap();
    const int block = std::max (512, samplesPerBlock);
    inR.assign ((size_t) block, 0.0f);
    outL.assign ((size_t) block, 0.0f);
    outR.assign ((size_t) block, 0.0f);
    bypassSm.reset (sampleRate, 0.02);
    bypassSm.setCurrentAndTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);
}

void AudioLabReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    for (int ch = nIn; ch < nOut; ++ch) buffer.clear (ch, 0, total);
    if (nIn == 0 || total == 0 || engine == nullptr) return;

    engine->setParams (currentParams());   // simple copie : le moteur lisse et applique tout sur sa grille de 128 échantillons
    bypassSm.setTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = nOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool stereoIn = nIn > 1;
    const int chunkMax = (int) outL.size();
    for (int off = 0; off < total; off += chunkMax)
    {
        const int n = std::min (chunkMax, total - off);
        const float* inL = L + off;
        const float* inRight = stereoIn ? R + off : inL;    // mono : la même voie à gauche et à droite
        engine->process (inL, inRight, outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float b = bypassSm.getNextValue();
            const float xl = inL[i], xr = inRight[i];
            if (R != nullptr)
            {
                const float ol = outL[(size_t) i] * (1 - b) + xl * b, orr = outR[(size_t) i] * (1 - b) + xr * b;
                L[off + i] = ol; R[off + i] = orr;
            }
            else
            {
                const float om = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
                L[off + i] = om * (1 - b) + xl * b;
            }
        }
    }
}

void AudioLabReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName ("ui"); old.isValid()) state.removeChild (old, nullptr);
    state.appendChild (ui.createCopy(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AudioLabReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        if (auto u = state.getChildWithName ("ui"); u.isValid())
        {
            ui.copyPropertiesAndChildrenFrom (u, nullptr);
            state.removeChild (u, nullptr);
        }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* AudioLabReverbProcessor::createEditor() { return new AudioLabReverbEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioLabReverbProcessor(); }
