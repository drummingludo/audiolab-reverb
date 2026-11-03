// Composants de « Lab Verb » : potentiomètres Pre-delay / Decay et curseurs Mix, Size, Damp, Width,
// aux mêmes tailles que le compresseur (charte V2 : potentiomètre principal arc Ø 98, curseur piste 4 px, bouton 18 px).
#pragma once
#include "Widgets.h"

namespace audiolab
{
// Texte à chiffres en cellules fixes, posé sur une ligne de base (valeur au centre du potentiomètre)
inline void drawTabularAt (juce::Graphics& g, const Theme& t, const juce::String& s, float size, int weight, float cx, float baseline)
{
    const auto f = t.ui (size, weight);
    g.setFont (f);
    float x = cx - tabularWidth (t, s, size, weight) / 2;
    for (auto c : s)
    {
        const auto ch = juce::String::charToString (c);
        const float adv = textWidth (f, ch);
        const float cell = juce::CharacterFunctions::isDigit (c) ? size * Theme::digitCell (weight) : adv;
        juce::GlyphArrangement ga; ga.addLineOfText (f, ch, x + (cell - adv) / 2, baseline); ga.draw (g);
        x += cell;
    }
}

// Accessibilité commune (lecteurs d'écran) pour un réglage relié à un paramètre
template <typename Owner>
std::unique_ptr<juce::AccessibilityHandler> rangedHandler (Owner& o, juce::RangedAudioParameter& prm, juce::ParameterAttachment& att)
{
    struct Value : juce::AccessibilityRangedNumericValueInterface
    {
        Value (juce::RangedAudioParameter& p, juce::ParameterAttachment& a) : param (p), attachment (a) {}
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return param.convertFrom0to1 (param.getValue()); }
        void setValue (double v) override { attachment.setValueAsCompleteGesture ((float) v); }
        AccessibleValueRange getRange() const override
        {
            const auto& rg = param.getNormalisableRange();
            return { { rg.start, rg.end }, rg.interval > 0 ? rg.interval : 0.01 };
        }
        juce::RangedAudioParameter& param; juce::ParameterAttachment& attachment;
    };
    return std::make_unique<juce::AccessibilityHandler> (o, juce::AccessibilityRole::slider, juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (prm, att) });
}

// ── Potentiomètre orange (Pre-delay, Decay), à la taille du compresseur ──
// Boîte 143 x 130 : arc Ø 98 centré à 56 px du haut, nom dessous.
class ReverbKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    ReverbKnob (Theme& t, juce::RangedAudioParameter& prm, juce::String label, juce::String unit,
                std::function<juce::String (float)> format, std::function<float()> defaultValue, juce::String help)
        : theme (t), param (prm), labelText (std::move (label)), unitText (std::move (unit)), fmt (std::move (format)),
          dflt (std::move (defaultValue)), attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        setTooltip (help);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        // Même géométrie que le potentiomètre principal du compresseur (charte V2) :
        // arc Ø 98, épaisseur 3,2, capuchon 74, valeur 14 px, unité 11 px, nom 11 px 58 px sous le centre.
        const auto& p = theme.pal();
        const float cx = getWidth() * 0.5f, cy = 56.0f, r = 49.0f, sw = 3.2f, cd = 74.0f;
        const float A0 = -0.75f * juce::MathConstants<float>::pi, A1 = -A0;
        const float v = param.getValue(), a = A0 + (A1 - A0) * v;
        auto pt = [&] (float ang, float rad) { return juce::Point<float> (cx + std::sin (ang) * rad, cy - std::cos (ang) * rad); };

        // rail (bouts ronds), éclairci au survol
        juce::Path trk; trk.addCentredArc (cx, cy, r, r, 0, A0, A1, true);
        g.setColour (hover ? p.railHi : p.rail);
        g.strokePath (trk, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool on = v > 0.003f;
        if (on)
        {
            const auto z = pt (A0, r), e = pt (a, r);
            auto brush = [&] (float alpha) { return juce::FillType (juce::ColourGradient (p.orange0.withMultipliedAlpha (alpha), z, p.orange.withMultipliedAlpha (alpha), e, false)); };
            juce::Path arc; arc.addCentredArc (cx, cy, r, r, 0, A0, a, true);
            // halo fin et discret, à peine renforcé pendant le réglage
            const float boost = dragging ? 1.5f : 1.0f;
            const float extra[] = { 3.2f, 1.6f }, alpha[] = { 0.05f, 0.12f };
            for (int i = 0; i < 2; ++i)
            {
                g.setFillType (brush (alpha[i] * boost));
                g.strokePath (arc, juce::PathStrokeType (sw + extra[i], juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            }
            g.setFillType (brush (1.0f));
            g.strokePath (arc, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            g.setColour (p.orange0); g.fillEllipse (z.x - sw / 2, z.y - sw / 2, sw, sw);
        }

        // capuchon : ombres, liseré de lumière en haut, disque
        juce::Path cap; cap.addEllipse (cx - cd / 2, cy - cd / 2, cd, cd);
        softShadow (g, cap, juce::Colours::black.withAlpha (0.5f), 20, { 0, 8 });
        softShadow (g, cap, juce::Colours::black.withAlpha (0.45f), 2, { 0, 1 });
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (cap);
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillPath (cap);
            g.setColour (p.cap); g.fillEllipse (cx - cd / 2, cy - cd / 2 + 1, cd, cd);
        }

        // repère, couleur de fin de l'arc, au bord du capuchon
        juce::Path m; m.startNewSubPath (pt (a, cd / 2)); m.lineTo (pt (a, cd / 2 - 8));
        g.setColour (on ? p.orange : p.orange0);
        g.strokePath (m, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        if (focused)
        {
            const float fr = r + sw / 2 + 3;
            g.setColour (p.teal2.withAlpha (0.6f)); g.drawEllipse (cx - fr, cy - fr, 2 * fr, 2 * fr, 1.5f);
        }

        // valeur (14 px, 600) et unité (11 px, gris doux)
        g.setColour (p.text);
        drawTabular (g, theme, fmt (param.convertFrom0to1 (v)), 14, 600, juce::Rectangle<float> (cx - cd / 2, cy - 16, cd, 18), juce::Justification::centred, -0.01f);
        g.setColour (p.soft); g.setFont (theme.ui (11));
        g.drawText (unitText, juce::Rectangle<float> (cx - cd / 2, cy + 2, cd, 14), juce::Justification::centred, false);
        // nom (11 px, 600, majuscules, 58 px sous le centre)
        g.setColour (p.text); g.setFont (theme.ui (11, 600, 0.06f));
        g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, cy + 58, (float) getWidth(), 13), juce::Justification::centredTop, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { y0 = e.position.y; v0 = param.getValue(); attachment.beginGesture(); dragging = true; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        setNorm (v0 + (y0 - e.position.y) / (e.mods.isShiftDown() ? 1200.0f : 220.0f), true);   // Maj = réglage fin
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { attachment.endGesture(); dragging = false; repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (dflt()); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f) return;
        setNorm (param.getValue() + (w.deltaY > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 0.002f : 0.01f), false);
    }
    bool keyPressed (const juce::KeyPress& kp) override
    {
        const float st = kp.getModifiers().isShiftDown() ? 0.05f : 0.01f;
        if (kp.isKeyCode (juce::KeyPress::upKey) || kp.isKeyCode (juce::KeyPress::rightKey)) { setNorm (param.getValue() + st, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::downKey) || kp.isKeyCode (juce::KeyPress::leftKey)) { setNorm (param.getValue() - st, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::homeKey)) { setNorm (0, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::endKey)) { setNorm (1, false); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return rangedHandler (*this, param, attachment); }

private:
    void setNorm (float nn, bool partOfGesture)
    {
        const float val = param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, nn));
        if (partOfGesture) attachment.setValueAsPartOfGesture (val); else attachment.setValueAsCompleteGesture (val);
    }
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText, unitText;
    std::function<juce::String (float)> fmt;
    std::function<float()> dflt;
    juce::ParameterAttachment attachment;
    float y0 = 0, v0 = 0;
    bool dragging = false, focused = false, hover = false;
};

// ── Curseur (Mix, Size, Damp, Width) ────────────────────────────────────
// Ligne de 238 x 24 : nom (54 px) · piste · valeur (48 px, à droite).
class LineSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    LineSlider (Theme& t, juce::RangedAudioParameter& prm, juce::String label, std::function<float()> defaultValue, juce::String help)
        : theme (t), param (prm), labelText (std::move (label)), dflt (std::move (defaultValue)),
          attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        setTooltip (help);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const float h = (float) getHeight(), cyy = h * 0.5f, v = param.getValue();
        g.setColour (p.text); g.setFont (theme.ui (11, 600, 0.06f));
        g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, 0, 54, h), juce::Justification::centredLeft, false);

        const auto tr = track();
        g.setColour (hover ? p.railHi : p.rail);
        g.fillRoundedRectangle (tr.getX(), cyy - 2, tr.getWidth(), 4, 2);
        if (v > 0.001f)
        {
            const float fw = tr.getWidth() * v;
            g.setGradientFill (juce::ColourGradient (p.teal, tr.getX(), 0, p.teal2, tr.getX() + fw, 0, false));
            g.fillRoundedRectangle (tr.getX(), cyy - 2, fw, 4, 2);
        }
        // bouton 18 px : ombres, liseré, capuchon
        const float tx = tr.getX() + 9 + v * (tr.getWidth() - 18);
        juce::Path th; th.addEllipse (tx - 9, cyy - 9, 18, 18);
        softShadow (g, th, juce::Colours::black.withAlpha (0.5f), 10, { 0, 4 });
        softShadow (g, th, juce::Colours::black.withAlpha (0.45f), 2, { 0, 1 });
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (th);
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillPath (th);
            g.setColour (p.cap); g.fillEllipse (tx - 9, cyy - 8, 18, 18);
        }
        if (focused) { g.setColour (p.teal2.withAlpha (0.6f)); g.drawEllipse (tx - 11, cyy - 11, 22, 22, 1.5f); }

        g.setColour (p.text);
        drawTabular (g, theme, juce::String (juce::roundToInt (param.convertFrom0to1 (v))) + " %", 12, 400,
                     juce::Rectangle<float> ((float) getWidth() - 48, 0, 48, h), juce::Justification::right);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! track().expanded (0, 12).contains (e.position)) return;
        attachment.beginGesture(); dragging = true; setFromX (e.position.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFromX (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { attachment.endGesture(); dragging = false; } }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (dflt()); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f) return;
        step (w.deltaY > 0 ? 1 : -1);
    }
    bool keyPressed (const juce::KeyPress& kp) override
    {
        const int st = kp.getModifiers().isShiftDown() ? 10 : 1;
        if (kp.isKeyCode (juce::KeyPress::upKey) || kp.isKeyCode (juce::KeyPress::rightKey)) { step (st); return true; }
        if (kp.isKeyCode (juce::KeyPress::downKey) || kp.isKeyCode (juce::KeyPress::leftKey)) { step (-st); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return rangedHandler (*this, param, attachment); }

private:
    juce::Rectangle<float> track() const { return { 58.0f, 0.0f, (float) getWidth() - 58 - 56, (float) getHeight() }; }
    void setFromX (float x)
    {
        const auto tr = track();
        const float nn = juce::jlimit (0.0f, 1.0f, (x - tr.getX() - 9) / (tr.getWidth() - 18));
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (nn));
    }
    void step (int d)
    {
        const float val = juce::jlimit (0.0f, 100.0f, (float) juce::roundToInt (param.convertFrom0to1 (param.getValue())) + d);
        attachment.setValueAsCompleteGesture (val);
    }
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText;
    std::function<float()> dflt;
    juce::ParameterAttachment attachment;
    bool dragging = false, focused = false, hover = false;
};
} // namespace audiolab
