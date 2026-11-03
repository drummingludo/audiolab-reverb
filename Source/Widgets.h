// Composants d'interface communs des plugins Audio Lab (charte V2, fiche 02) : sélecteur segmenté, bulles d'aide ;
// 2.0.0 : curseur de réglage fin (SliderRow) et flèche d'ouverture (Disclosure) du panneau « Advanced settings »,
// repris d'Audio Lab Compressor. Les potentiomètres et curseurs de la face principale sont dans ReverbWidgets.h.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace audiolab
{
// Ombre douce façon CSS box-shadow
inline void softShadow (juce::Graphics& g, const juce::Path& p, juce::Colour c, int radius, juce::Point<int> offset)
{
    juce::DropShadow (c, radius, offset).drawForPath (g, p);
}

// Familles de couleur de la charte (teal = niveau, teal secondaire = gain et filtres, orange = temps et action)
enum class Family { teal, teal2, orange };

// ── Sélecteur segmenté ───────────────────────────────────────────────────
// Bande « Surface » (« Rails » sur une surface), marge 3 px, segments égaux ;
// segment choisi : pastille #005F73, texte #F9F9FB en 500.
class SegButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegButtons (Theme& t, juce::StringArray opts) : theme (t), options (std::move (opts)) {}

    bool vertical = false;      // boutons empilés (Ratio, Meter)
    int columns = 0;            // > 0 : grille de « columns » colonnes (ex. modes de la Reverb en 2 rangées de 3)
    bool onSurface = false;     // posé sur une surface : bande couleur « Rails »
    float fontSize = 12, gap = 2, padX = 12;

    // Relie à un paramètre « choix »
    void attach (juce::RangedAudioParameter& prm)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (prm, [this, &prm] (float) {
            selected = juce::roundToInt (prm.convertFrom0to1 (prm.getValue())); repaint(); }, nullptr);
        onSelect = [this] (int i) { attachment->setValueAsCompleteGesture ((float) i); };
        attachment->sendInitialUpdate();
    }

    void setSelected (int i) { selected = i; repaint(); }
    int getSelected() const { return selected; }
    // Désactivé (réglage sans effet dans la situation actuelle) : textes en gris doux, pastille neutre ; reste cliquable
    void setDimmed (bool d) { if (dimmed != d) { dimmed = d; repaint(); } }
    bool isDimmed() const { return dimmed; }
    std::function<void (int)> onSelect;

    float idealWidth() const
    {
        float w = 0;
        for (auto& o : options) w = std::max (w, textWidth (theme.ui (fontSize, 500), o));
        return 6 + options.size() * (w + 2 * padX) + gap * (options.size() - 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        g.setColour (onSurface ? p.rail : p.surface); g.fillRoundedRectangle (getLocalBounds().toFloat(), 9);
        for (int i = 0; i < options.size(); ++i)
        {
            auto r = cell (i);
            const bool sel = i == selected;
            if (sel)
            {
                juce::Path pill; pill.addRoundedRectangle (r, 7);
                softShadow (g, pill, juce::Colours::black.withAlpha (0.18f), 3, { 0, 1 });
                g.setColour (dimmed ? p.cap : p.tealSel); g.fillPath (pill);
            }
            g.setColour (dimmed ? p.soft : sel || (i == hover && isEnabled()) ? p.text : p.text2);
            g.setFont (theme.ui (fontSize, sel ? 500 : 400));
            g.drawText (options[i], r, juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h >= 0) { selected = h; repaint(); if (onSelect) onSelect (h); }
    }

private:
    juce::Rectangle<float> cell (int i) const
    {
        const int n = options.size();
        if (columns > 0)
        {
            const int rows = (n + columns - 1) / columns;
            const float w = ((float) getWidth() - 6 - gap * (columns - 1)) / columns;
            const float h = ((float) getHeight() - 6 - gap * (rows - 1)) / rows;
            return { 3 + (i % columns) * (w + gap), 3 + (i / columns) * (h + gap), w, h };
        }
        if (vertical)
        {
            const float h = ((float) getHeight() - 6 - gap * (n - 1)) / n;
            return { 3.0f, 3 + i * (h + gap), (float) getWidth() - 6, h };
        }
        const float w = ((float) getWidth() - 6 - gap * (n - 1)) / n;
        return { 3 + i * (w + gap), 3.0f, w, (float) getHeight() - 6 };
    }
    int hit (juce::Point<float> pt) const
    {
        for (int i = 0; i < options.size(); ++i) if (cell (i).contains (pt)) return i;
        return -1;
    }
    Theme& theme;
    juce::StringArray options;
    int selected = 0, hover = -1;
    bool dimmed = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ── Curseur de réglage fin (charte § 7, repris du Compressor) : libellé · piste · valeur 48 px ──
// Piste 4 px « Rails », partie avant le bouton remplie de la couleur de la famille (halo léger), bouton 18 px
// « Capuchon » ; rien de coloré à la valeur minimale ; désactivé : piste en gris doux.
// labelW : largeur du libellé (68 px comme le Compressor, plus si un libellé est plus long) ;
// bipolar : réglage dont le neutre est au milieu (Bass, Early / Late) : la piste se remplit depuis le milieu.
class SliderRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    SliderRow (Theme& t, juce::RangedAudioParameter& prm, juce::String label, Family family, std::function<juce::String (float)> format)
        : theme (t), param (prm), labelText (std::move (label)), fam (family), fmt (std::move (format)),
          attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        attachment.sendInitialUpdate();
    }
    void setDimmed (bool d) { if (dimmed != d) { dimmed = d; repaint(); } }
    bool isDimmed() const { return dimmed; }
    float labelW = 68;
    bool bipolar = false;

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const float h = (float) getHeight(), cy = h * 0.5f;
        g.setColour (dimmed ? p.soft : p.text2); g.setFont (theme.ui (12));
        g.drawText (labelText, juce::Rectangle<float> (0, 0, labelW, h), juce::Justification::centredLeft, false);
        auto tr = track();
        g.setColour (hover && ! dimmed ? p.railHi : p.rail); g.fillRoundedRectangle (tr.getX(), cy - 2, tr.getWidth(), 4, 2);
        const float n = param.getValue();
        const float x = tr.getX() + 9 + (tr.getWidth() - 18) * n;
        const float x0 = bipolar ? tr.getX() + 9 + (tr.getWidth() - 18) * 0.5f : tr.getX();
        if (bipolar ? std::abs (n - 0.5f) > 0.002f : n > 0.001f)
        {
            // partie entre le départ et le bouton : teal en dégradé, orange plein, teal secondaire plein (posé sur une surface)
            const juce::Rectangle<float> f (std::min (x0, x), cy - 2, std::abs (x - x0), 4);
            juce::FillType ft;
            if (dimmed) ft = juce::FillType (p.soft);
            else if (fam == Family::teal) ft = juce::FillType (juce::ColourGradient (p.teal, x0, 0, p.teal2, x, 0, false));
            else ft = juce::FillType (fam == Family::orange ? p.orange : p.teal2);
            if (! dimmed)
            {
                const auto glow = fam == Family::orange ? p.orange : p.teal2;
                g.setColour (glow.withAlpha (0.05f)); g.fillRoundedRectangle (f.expanded (1.6f), 3.6f);   // halo fin et discret
                g.setColour (glow.withAlpha (0.12f)); g.fillRoundedRectangle (f.expanded (0.8f), 2.8f);
            }
            g.setFillType (ft); g.fillRoundedRectangle (f, 2);
        }
        if (bipolar) { g.setColour (dimmed ? p.soft : p.mark2); g.fillRect (x0 - 0.5f, cy - 5, 1.0f, 10.0f); }   // repère du neutre
        juce::Path th; th.addEllipse (x - 9, cy - 9, 18, 18);
        softShadow (g, th, juce::Colours::black.withAlpha (0.5f), 6, { 0, 2 });
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (th);
            g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillPath (th);
            g.setColour (p.cap); g.fillEllipse (x - 9, cy - 8, 18, 18);
        }
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.strokePath (th, juce::PathStrokeType (0.5f));
        if (focused) { g.setColour (p.teal2.withAlpha (0.5f)); g.drawEllipse (x - 10.5f, cy - 10.5f, 21, 21, 1.5f); }
        g.setColour (dimmed ? p.soft : p.text);
        drawTabular (g, theme, fmt (param.convertFrom0to1 (n)), 12, 400,
                     juce::Rectangle<float> ((float) getWidth() - 48, 0, 48, h), juce::Justification::right);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { attachment.beginGesture(); drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override { attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f) return;
        const float st = e.mods.isShiftDown() ? 0.002f : 0.01f;
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param.getValue() + (w.deltaY > 0 ? st : -st))));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto& rg = param.getNormalisableRange();
        const float st = rg.interval > 0 ? rg.interval / (rg.end - rg.start) : 0.01f;
        float n = param.getValue();
        if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey)) n += std::max (st, 0.001f);
        else if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::downKey)) n -= std::max (st, 0.001f);
        else return false;
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
        return true;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }

private:
    juce::Rectangle<float> track() const { return { labelW + 10, 0, (float) getWidth() - (labelW + 10) - 58, (float) getHeight() }; }
    void drag (const juce::MouseEvent& e)
    {
        auto tr = track();
        const float n = juce::jlimit (0.0f, 1.0f, (e.position.x - tr.getX() - 9) / (tr.getWidth() - 18));
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
    }
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText;
    Family fam;
    std::function<juce::String (float)> fmt;
    juce::ParameterAttachment attachment;
    bool dimmed = false, focused = false, hover = false;
};

// ── Flèche d'ouverture (« Advanced settings », repris du Compressor) : fermé = texte secondaire, ouvert = texte ──
class Disclosure : public juce::Button
{
public:
    Disclosure (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool open = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& p = theme.pal();
        const juce::Colour c = open || over ? p.text : p.text2;
        const float x = 8, y = getHeight() * 0.5f - 6;   // boîte 12 × 12 du chevron
        juce::Path ch;
        if (open) { ch.startNewSubPath (x + 2.5f, y + 4.5f); ch.lineTo (x + 6, y + 8); ch.lineTo (x + 9.5f, y + 4.5f); }
        else      { ch.startNewSubPath (x + 4.5f, y + 2.5f); ch.lineTo (x + 8, y + 6); ch.lineTo (x + 4.5f, y + 9.5f); }
        g.setColour (c);
        g.strokePath (ch, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (theme.ui (12));
        g.drawText (getButtonText(), juce::Rectangle<float> (26, 0, (float) getWidth() - 26, (float) getHeight()), juce::Justification::centredLeft, false);
    }
    float idealWidth() const { return 26 + textWidth (theme.ui (12), getButtonText()) + 8; }

private:
    Theme& theme;
};

// ── Bulles d'aide et menus ───────────────────────────────────────────────
class ALLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit ALLookAndFeel (Theme& t) : theme (t)
    {
        const auto& p = theme.pal();
        setColour (juce::PopupMenu::backgroundColourId, p.surface);
        setColour (juce::PopupMenu::textColourId, p.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.tealSel);
        setColour (juce::PopupMenu::highlightedTextColourId, p.text);
        setColour (juce::TooltipWindow::backgroundColourId, p.surface);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.rail);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        const auto& p = theme.pal();
        g.setColour (p.surface); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
        g.setColour (p.rail); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6, 1);
        g.setColour (p.text); g.setFont (theme.ui (12));
        g.drawFittedText (tip, juce::Rectangle<int> (0, 0, w, h).reduced (9, 5), juce::Justification::centredLeft, 5);
    }
    void drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override
    {
        // poignée de redimensionnement discrète, couleur « Rails » (éclaircie au survol)
        const auto& p = theme.pal();
        g.setColour (isMouseOver || isMouseDragging ? p.mark2 : p.rail);
        for (float i = 0.3f; i < 1.0f; i += 0.3f)
            g.drawLine ((float) w * i, (float) h + 1.0f, (float) w + 1.0f, (float) h * i, 1.2f);
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::GlyphArrangement ga; ga.addFittedText (theme.ui (12), tip, 0, 0, 260, 200, juce::Justification::topLeft, 5);
        const int w = juce::jmin (280, (int) ga.getBoundingBox (0, -1, true).getWidth() + 20);
        const int h = (int) ga.getBoundingBox (0, -1, true).getHeight() + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 14, w, h).constrainedWithin (parent);
    }

private:
    Theme& theme;
};
} // namespace audiolab
