#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpPre   = "Pre-delay: delay before the reverb starts. A few tens of ms separate the voice from its reverb "
                               "and keep the attack clear. Double-click: value of the current mode.";
static const char* helpDec   = "Decay: length of the reverb (RT60), the time it takes the tail to drop by 60 dB, "
                               "measured in the midrange. The lows and highs follow the character of the mode.";
static const char* helpMix   = "Mix: balance between the direct sound and the reverb. On an insert, 15 to 30 %; "
                               "on a send track (bus), 100 %.";
static const char* helpSize  = "Size: size of the space. Bigger = early reflections further apart and a wider tail.";
static const char* helpDamp  = "Damp: high-frequency damping. The higher it is, the faster the highs die away, "
                               "as in an absorbent room.";
static const char* helpWidth = "Width: stereo width of the reverb. 0 % = mono, 100 % = full width.";
static const char* helpTabs  = "Reverb mode. Choosing a mode loads its starting settings: "
                               "Chamber (echo chamber), Plate, Spring, Room, Hall (concert hall), Church.";
// 2.0.0 : réglages avancés
static const char* helpAdv   = "Show / hide the tone, ducking and freeze settings";
static const char* helpLow   = "Low cut: removes the lows from the reverb only (the dry sound is untouched). "
                               "Keeps a long reverb from muddying the bass. Off at 20 Hz.";
static const char* helpHigh  = "High cut: removes the highs from the reverb only. Lower = a darker, softer reverb "
                               "that sits behind the dry sound. Off at 20 kHz.";
static const char* helpBass  = "Bass: decay time of the lows (below about 200 Hz) compared with Decay. "
                               "\xc3\x97" "2 = the lows ring twice as long (warmer, bigger), \xc3\x97" "0.5 = half as long (tighter, less mud). "
                               "\xc3\x97" "1.00 = the natural balance of the mode. A spring has no setting for this.";
static const char* helpErl   = "Early / Late: balance between the early reflections (the first echoes from the walls) and the reverb tail. "
                               "0 % = the natural balance of the mode, \xe2\x88\x92" "100 % = early reflections only, +100 % = tail only. "
                               "Plate and Spring have no early reflections.";
static const char* helpDuck  = "Ducking: the reverb gets quieter while the dry sound plays and comes back in the gaps "
                               "(about 0.3 s after it stops). Keeps a vocal clear with a long reverb. 100 % = up to 30 dB quieter.";
static const char* helpFrz   = "Freeze: holds the reverb tail for as long as it is on; new sound no longer goes into the reverb. "
                               "Play a chord, freeze it, play over it. While frozen, Mode and Size changes wait until Freeze is off.";

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

    tabs.columns = 3;   // Chamber, Plate, Spring / Room, Hall, Church
    tabs.attach (prm (params::mode));
    tabs.onSelect = [this] (int i) { proc.selectMode (i); };
    tabs.setTooltip (U (helpTabs));

    for (auto* c : std::initializer_list<juce::Component*> { kPre.get(), kDec.get(), sMix.get(), sSize.get(), sDamp.get(), sWidth.get(), &tabs })
        content.addAndMakeVisible (c);

    // 2.0.0 : panneau « Advanced settings » (caché par défaut, état mémorisé avec la session), modèle du Compressor ;
    // couleurs de la charte : filtres = teal secondaire, temps (Bass) et action (Ducking) = orange, niveau (Early / Late) = teal
    rLow = std::make_unique<SliderRow> (theme, prm (params::lowCut), "Low cut", Family::teal2, [] (float v) { return params::lowCutText (v); });
    rHigh = std::make_unique<SliderRow> (theme, prm (params::highCut), "High cut", Family::teal2, [] (float v) { return params::highCutText (v); });
    rBass = std::make_unique<SliderRow> (theme, prm (params::bass), "Bass", Family::orange, [] (float v) { return params::bassText (v); });
    rErl = std::make_unique<SliderRow> (theme, prm (params::erLate), "Early / Late", Family::teal, [] (float v) { return params::signedPct (v); });
    rDuck = std::make_unique<SliderRow> (theme, prm (params::ducking), "Ducking", Family::orange, [] (float v) { return String (juce::roundToInt (v)) + " %"; });
    rBass->bipolar = true; rErl->bipolar = true;   // neutre au milieu
    rLow->setTooltip (U (helpLow)); rHigh->setTooltip (U (helpHigh)); rBass->setTooltip (U (helpBass));
    rErl->setTooltip (U (helpErl)); rDuck->setTooltip (U (helpDuck));
    freezeSeg.onSurface = true;
    freezeSeg.attach (prm (params::freeze));
    freezeSeg.setTooltip (U (helpFrz));
    for (auto* l : { "Low cut", "High cut", "Bass", "Early / Late", "Ducking", "Freeze" })
        labelW = std::max (labelW, std::ceil (textWidth (theme.ui (12), U (l))));
    for (auto* r : { rLow.get(), rHigh.get(), rBass.get(), rErl.get(), rDuck.get() }) r->labelW = labelW;
    advBtn.setTooltip (U (helpAdv));
    advBtn.onClick = [this] { setAdvanced (! advOpen, true); };
    advItems = { rLow.get(), rHigh.get(), rBass.get(), rErl.get(), rDuck.get(), &freezeSeg };
    content.addAndMakeVisible (advBtn);
    for (auto* c : advItems) content.addAndMakeVisible (c);
    proc.apvts.addParameterListener (params::mode, this);

    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant les limites
    setAdvanced ((bool) proc.ui.getProperty ("adv", false), false);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
    setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (currentH() * sc));
    refreshState();
}

AudioLabReverbEditor::~AudioLabReverbEditor()
{
    proc.apvts.removeParameterListener (params::mode, this);
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabReverbEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void AudioLabReverbEditor::handleAsyncUpdate() { refreshState(); }

// Grisé = sans effet dans le mode choisi (le réglage reste modifiable) : Plate et Spring n'ont pas de réflexions
// précoces séparées ; un ressort n'a pas de réglage du grave
void AudioLabReverbEditor::refreshState()
{
    const auto e = rv::modeDef (juce::roundToInt (proc.getParam (params::mode))).engine;
    rErl->setDimmed (e != rv::roomEngine);
    rBass->setDimmed (e == rv::springEngine);
    content.repaint();
}

// Ouvre / ferme le panneau « Advanced settings » : la fenêtre grandit ou rétrécit de 244 px (à l'échelle)
void AudioLabReverbEditor::setAdvanced (bool open, bool resizeWindow)
{
    advOpen = open;
    proc.ui.setProperty ("adv", open, nullptr);
    advBtn.open = open; advBtn.repaint();
    for (auto* c : advItems) c->setVisible (open);
    if (resizeWindow)
    {
        const double s = (double) getWidth() / baseW;   // échelle actuelle, lue avant de changer les limites
        getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
        setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
        setSize (juce::roundToInt (baseW * s), juce::roundToInt (currentH() * s));
    }
    resized();
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

void AudioLabReverbEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, currentH());
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabReverbEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg);
    if (! ed.advOpen) return;
    // panneau des réglages avancés (« Surface », coins 12, marge intérieure 16) + libellé de la ligne Freeze
    g.setColour (p.surface); g.fillRoundedRectangle (24, 414, 238, (float) (16 + 34 * advRows - 8 + 16), 12);
    auto b = ed.freezeSeg.getBounds().toFloat();
    g.setColour (ed.freezeSeg.isDimmed() ? p.soft : p.text2); g.setFont (ed.theme.ui (12));
    g.drawText ("Freeze", juce::Rectangle<float> (40, b.getY(), ed.labelW, b.getHeight()), juce::Justification::centredLeft, false);
}

void AudioLabReverbEditor::Content::resized()
{
    auto& e = ed;
    // Une colonne (x 24 → 262) : modes (2 rangées de 3), Pre-delay et Decay (potentiomètres du compresseur,
    // centres x 79,5 et 206,5, y 159), puis les 4 curseurs ; l'écran de décroissance est retiré pour ce test
    e.tabs.setBounds (24, 24, 238, 60);
    e.kPre->setBounds (8, 103, 143, 130);
    e.kDec->setBounds (135, 103, 143, 130);
    e.sMix->setBounds (24, 254, 238, 24);
    e.sSize->setBounds (24, 282, 238, 24);
    e.sDamp->setBounds (24, 310, 238, 24);
    e.sWidth->setBounds (24, 338, 238, 24);
    // 2.0.0 : flèche « Advanced settings » sous la face (x 16 : chevron aligné sur la marge de 24 px, comme le Compressor),
    // panneau 16 px plus bas : 6 lignes de 26 px (écart 8) dans une surface de x 24 à 262
    e.advBtn.setBounds (16, 370, juce::roundToInt (e.advBtn.idealWidth()), 28);
    auto y = [] (int row) { return 430 + 34 * row; };
    int r = 0;
    for (auto* row : { e.rLow.get(), e.rHigh.get(), e.rBass.get(), e.rErl.get(), e.rDuck.get() }) row->setBounds (40, y (r++), 206, 26);
    e.freezeSeg.setBounds (40 + juce::roundToInt (e.labelW) + 10, y (r), juce::roundToInt (e.freezeSeg.idealWidth()), 26);
}
