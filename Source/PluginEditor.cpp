#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpPre   = "Pre-delay : retard avant la réverbération. Quelques dizaines de ms détachent la voix de sa réverb "
                               "et gardent l'attaque nette. Double-clic : valeur du mode.";
static const char* helpDec   = "Decay : durée de la réverbération (RT60), le temps pour que la queue baisse de 60 dB, "
                               "mesuré dans le médium. Les graves et les aigus suivent le caractère du mode.";
static const char* helpMix   = "Mix : dosage entre le son direct et la réverbération. En insert, 15 à 30 % ; "
                               "sur une piste de départ (bus), 100 %.";
static const char* helpSize  = "Size : taille de l'espace. Plus grand = premières réflexions plus espacées et queue plus ample.";
static const char* helpDamp  = "Damp : amortissement des aigus. Plus on monte, plus les aigus s'éteignent vite, "
                               "comme dans une salle absorbante.";
static const char* helpWidth = "Width : largeur stéréo de la réverbération. 0 % = mono, 100 % = toute la largeur.";
static const char* helpTabs  = "Mode de réverbération. Choisir un mode charge ses réglages de départ : "
                               "Chamber (chambre d'écho), Plate (plaque), Spring (ressort), Room (pièce), Hall (salle de concert), Church (église).";

AudioLabReverbEditor::AudioLabReverbEditor (AudioLabReverbProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };

    kPre = std::make_unique<ReverbKnob> (theme, prm (params::predelay), "Pre-delay", "ms",
        [] (float v) { return String (juce::roundToInt (v)); }, [this] { return modeDefault (params::predelay); }, U (helpPre));
    kDec = std::make_unique<ReverbKnob> (theme, prm (params::decay), "Decay", "s",
        [] (float v) { return params::decayText (v); }, [this] { return modeDefault (params::decay); }, U (helpDec));
    sMix = std::make_unique<LineSlider> (theme, prm (params::mix), "Mix", [this] { return modeDefault (params::mix); }, U (helpMix));
    sSize = std::make_unique<LineSlider> (theme, prm (params::size), "Size", [this] { return modeDefault (params::size); }, U (helpSize));
    sDamp = std::make_unique<LineSlider> (theme, prm (params::damp), "Damp", [this] { return modeDefault (params::damp); }, U (helpDamp));
    sWidth = std::make_unique<LineSlider> (theme, prm (params::width), "Width", [this] { return modeDefault (params::width); }, U (helpWidth));

    tabs.gap = 0;
    tabs.attach (prm (params::mode));
    tabs.onSelect = [this] (int i) { proc.selectMode (i); };
    tabs.setTooltip (U (helpTabs));

    for (auto* c : std::initializer_list<juce::Component*> { kPre.get(), kDec.get(), sMix.get(), sSize.get(), sDamp.get(), sWidth.get(), &tabs, &graph })
        content.addAndMakeVisible (c);

    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant les limites
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc));
    timerCallback();
    startTimerHz (30);
}

AudioLabReverbEditor::~AudioLabReverbEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

float AudioLabReverbEditor::modeDefault (const char* id) const
{
    const auto& d = rv::modeDef (juce::roundToInt (proc.getParam (params::mode))).def;
    const String s (id);
    if (s == params::predelay) return (float) d.predelay;
    if (s == params::decay) return (float) d.decay;
    if (s == params::mix) return (float) (d.mix * 100);
    if (s == params::size) return (float) (d.size * 100);
    if (s == params::damp) return (float) (d.damp * 100);
    return (float) (d.width * 100);
}

void AudioLabReverbEditor::timerCallback()
{
    const auto p = proc.currentParams();
    graph.setParams (p);
    if (firstRequest || ! sameParams (p, requested)) { renderer.request (p); requested = p; firstRequest = false; }
    IrResult r;
    if (renderer.fetch (r)) graph.setResult (std::move (r));
}

void AudioLabReverbEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, baseH);
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabReverbEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (ed.theme.pal().bg);
}

void AudioLabReverbEditor::Content::resized()
{
    auto& e = ed;
    // Colonne de gauche (x 24 → 424) : deux potentiomètres de 170 px (centres x 122 et 326, y 132), puis les 4 curseurs
    e.kPre->setBounds (32, 47, 180, 197);
    e.kDec->setBounds (236, 47, 180, 197);
    e.sMix->setBounds (24, 268, 400, 24);
    e.sSize->setBounds (24, 308, 400, 24);
    e.sDamp->setBounds (24, 348, 400, 24);
    e.sWidth->setBounds (24, 388, 400, 24);
    // Colonne de droite (x 456 → 976) : onglets des modes, puis l'écran de décroissance
    e.tabs.setBounds (456, 87, 520, 41);
    e.graph.setBounds (456, 144, 520, 222);
}
