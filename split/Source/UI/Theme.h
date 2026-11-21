// Layers Split — couleurs et apparence.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/Separator.h"

namespace lsplit::theme
{

inline const juce::Colour bg         { 0xff0a0b0e };
inline const juce::Colour panel      { 0xff111318 };
inline const juce::Colour panel2     { 0xff15181e };
inline const juce::Colour header     { 0xff121419 };
inline const juce::Colour border     { 0xff1f232b };
inline const juce::Colour line       { 0xff1a1d24 };
inline const juce::Colour text       { 0xffcfd4de };
inline const juce::Colour textBright { 0xffeef1f6 };
inline const juce::Colour dim        { 0xff6d7383 };
inline const juce::Colour dim2       { 0xff4a5060 };
inline const juce::Colour accent     { 0xff3ee0c8 };
inline const juce::Colour accentDark { 0xff1c6b60 };
inline const juce::Colour warn       { 0xfff0c050 };
inline const juce::Colour error      { 0xffff5c7a };
inline const juce::Colour solo       { 0xffd6b13a };
inline const juce::Colour mute       { 0xffc43c4c };

inline juce::Colour stemColour (int id)
{
    switch (id)
    {
        case stemLead:    return juce::Colour (0xffff5c7a);
        case stemBacking: return juce::Colour (0xffff9cc0);
        case stemDrums:   return juce::Colour (0xffffaa3b);
        case stemBass:    return juce::Colour (0xff3ee0c8);
        case stemGuitar:  return juce::Colour (0xff6fdc84);
        case stemGuitarSolo: return juce::Colour (0xffc6f06a);
        case stemPiano:   return juce::Colour (0xff55a3ff);
        case stemOther:   return juce::Colour (0xff9887ff);
        case stemSoprano: return juce::Colour (0xffffc4dc);
        case stemAlto:    return juce::Colour (0xfff49ac4);
        case stemTenor:   return juce::Colour (0xffd97bb0);
        case stemBassVoice: return juce::Colour (0xffb8649c);
        default:          return dim;
    }
}

inline const char* stemSubtitle (int id)
{
    switch (id)
    {
        case stemLead:    return "Voix principale";
        case stemBacking: return "Harmonies, doublages";
        case stemDrums:   return "Kick, caisse, cymbales";
        case stemBass:    return "Basse, sub";
        case stemGuitar:  return "Électriques, acoustiques";
        case stemGuitarSolo: return "Mélodie (expérimental)";
        case stemPiano:   return "Piano, Rhodes, claviers";
        case stemOther:   return "Orgue, synthés, cordes…";
        case stemSoprano: return "Voix aiguës (femmes)";
        case stemAlto:    return "Voix graves (femmes)";
        case stemTenor:   return "Voix aiguës (hommes)";
        case stemBassVoice: return "Voix graves (hommes)";
        default:          return "";
    }
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColourScheme ({ bg, panel, panel, border, text, panel2, text, accent, bg });
       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
       #endif

        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1d24));
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff06231f));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a1d24));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2e38));
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff15181e));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDark);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, juce::Colour (0xff22262f));
        setColour (juce::Slider::thumbColourId, textBright);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1a1d24));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, border);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgCol,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool primary = b.getProperties().getWithDefault ("primary", false);
        const bool on = b.getToggleState();
        juce::Colour c = primary ? accent : (on ? b.findColour (juce::TextButton::buttonOnColourId) : bgCol);
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        else if (down) c = c.darker (0.25f);
        else if (over) c = c.brighter (primary ? 0.1f : 0.15f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        if (! primary && ! on)
        {
            g.setColour (juce::Colour (0xff2a2e38));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const bool primary = b.getProperties().getWithDefault ("primary", false);
        return font (juce::jmin (13.5f, h * 0.55f), primary || b.getProperties().getWithDefault ("bold", false));
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool primary = b.getProperties().getWithDefault ("primary", false);
        auto col = primary ? juce::Colour (0xff06231f)
                           : b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (b.getProperties().contains ("onText") && b.getToggleState())
            col = juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onText"]);
        g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds();
        auto box = r.removeFromLeft (r.getHeight()).toFloat().withSizeKeepingCentre (15.0f, 15.0f);
        g.setColour (b.getToggleState() ? accent : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (b.getToggleState() ? accent : (over ? text : juce::Colour (0xff4a5163)));
        g.drawRoundedRectangle (box, 3.0f, 1.2f);
        if (b.getToggleState())
        {
            juce::Path p;
            p.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
            p.lineTo (box.getX() + 6.5f, box.getBottom() - 4.0f);
            p.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
            g.setColour (juce::Colour (0xff06231f));
            g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        g.setColour (b.isEnabled() ? text : dim);
        g.setFont (font (13.0f));
        g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (6), juce::Justification::centredLeft, 1);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        const float cy = (float) y + (float) h * 0.5f;
        auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) w, 4.0f);
        g.setColour (s.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (s.findColour (juce::Slider::trackColourId).withMultipliedAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ pos, cy }));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
};

} // namespace lsplit::theme
