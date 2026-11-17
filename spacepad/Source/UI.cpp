#include "UI.h"
#include "BinaryData.h"
#include "Lang.h"

namespace spacepad
{
static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// ============================================================================================
// Thème
// ============================================================================================
namespace theme
{
static juce::Typeface::Ptr face (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}
juce::Font title (float h)
{
    static auto tf = face (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
    return juce::Font (juce::FontOptions().withTypeface (tf).withHeight (h));
}
juce::Font ui (float h, int weight)
{
    static auto r = face (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static auto s = face (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    static auto b = face (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
    return juce::Font (juce::FontOptions().withTypeface (weight == 2 ? b : weight == 1 ? s : r).withHeight (h));
}
juce::Colour colourFor (const juce::String& name)
{
    static const juce::uint32 pal[] = { 0xff6fd8ff, 0xffa78bfa, 0xff5cffb0, 0xffffcf8a, 0xffffb3d9, 0xff8fa2ff, 0xff9effe0, 0xffff8a8a, 0xffbdf1ff, 0xffffd27a };
    return juce::Colour (pal[(size_t) ((juce::uint32) name.hashCode() % (sizeof (pal) / sizeof (pal[0])))]);
}
}

static void glowText (juce::Graphics& g, const juce::String& t, juce::Font f, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, juce::Colour glow, int radius, float kerning = 0)
{
    if (kerning != 0) f.setExtraKerningFactor (kerning);
    juce::GlyphArrangement ga;
    ga.addFittedText (f, t, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1);
    juce::Path p;
    ga.createPath (p);
    juce::DropShadow (glow, radius, {}).drawForPath (g, p);
    g.setColour (c);
    g.fillPath (p);
}

static void glowEllipse (juce::Graphics& g, const juce::DropShadow& d, juce::Rectangle<float> r)
{
    juce::Path p;
    p.addEllipse (r);
    d.drawForPath (g, p);
}

static void panelBg (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bottom)
{
    g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, radius);
}

static void insetBox (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c = theme::lcd)
{
    g.setColour (c);
    g.fillRoundedRectangle (r, radius);
    // ombre intérieure en haut + reflet en bas
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 10, false);
    g.setGradientFill (sh);
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.translated (0, 1), radius, 1.0f);
}

// ============================================================================================
// LookAndFeel
// ============================================================================================
SpaceLook::SpaceLook()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff121828));
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff121a2c));
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::line);
    setColour (juce::ComboBox::arrowColourId, theme::accent);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff04121c));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e2638));
    setColour (juce::TextButton::buttonOnColourId, theme::accent);
    setColour (juce::ScrollBar::thumbColourId, theme::accent.withAlpha (0.35f));
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff121828));
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::DocumentWindow::textColourId, theme::text);
}

void SpaceLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const auto c = r.getCentre();
    const float ringR = d * 0.5f - 3;
    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float ang = a0 + pos * (a1 - a0);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, ringR, ringR, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff1b2236));
    g.strokePath (bgArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (pos > 0.001f)
    {
        valArc.addCentredArc (c.x, c.y, ringR, ringR, 0, a0, ang, true);
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (valArc, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (valArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // bouton
    const float kr = d * 0.5f - 12;
    auto body = juce::Rectangle<float> (kr * 2, kr * 2).withCentre (c);
    {
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.7f), c.x, c.y + 5, juce::Colours::transparentBlack, c.x + kr + 10, c.y + 5, true);
        sh.addColour (kr / (kr + 10.0f), juce::Colours::black.withAlpha (0.55f));
        g.setGradientFill (sh);
        g.fillEllipse (c.x - kr - 10, c.y + 5 - kr - 10, (kr + 10) * 2, (kr + 10) * 2);
    }
    juce::ColourGradient grad (juce::Colour (0xff4a5570), c.x - kr * 0.4f, c.y - kr * 0.5f, juce::Colour (0xff0c0f18), c.x + kr, c.y + kr, true);
    grad.addColour (0.55, juce::Colour (0xff1a2032));
    g.setGradientFill (grad);
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    // aiguille
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -kr + 4, 3.0f, kr * 0.42f, 1.5f);
    ptr.applyTransform (juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    g.setColour (juce::Colours::white);
    g.fillPath (ptr);
}

void SpaceLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float rad = juce::jmin (12.0f, r.getHeight() * 0.3f);
    const bool on = b.getToggleState();
    const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
    const bool flat = b.getProperties()["flat"];
    const bool dashed = b.getProperties()["dashed"];
    if (dashed)
    {
        g.setColour (onCol.withAlpha (over ? 0.16f : 0.09f));
        g.fillRoundedRectangle (r, rad);
        juce::Path p; p.addRoundedRectangle (r, rad);
        juce::Path d; const float dl[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dl, 2);
        g.setColour (onCol.withAlpha (0.7f));
        g.fillPath (d);
        return;
    }
    if (on)
    {
        juce::DropShadow (onCol.withAlpha (0.45f), 16, {}).drawForRectangle (g, r.toNearestInt());
        g.setGradientFill (juce::ColourGradient (onCol.brighter (0.35f), r.getTopLeft(), onCol.darker (0.35f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        return;
    }
    if (flat)
    {
        g.setColour (juce::Colours::white.withAlpha (over ? 0.07f : 0.0f));
        g.fillRoundedRectangle (r, rad);
        return;
    }
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 8, { 0, 3 }).drawForRectangle (g, r.toNearestInt());
    auto top = juce::Colour (0xff273049), bot = juce::Colour (0xff161c2c);
    if (over) { top = top.brighter (0.15f); bot = bot.brighter (0.1f); }
    if (down) std::swap (top, bot);
    g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bot, r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::white.withAlpha (0.11f));
    g.drawRoundedRectangle (r, rad, 1.0f);
}

void SpaceLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto col = on ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId);
    if (b.getProperties()["dashed"]) col = b.findColour (juce::TextButton::buttonOnColourId).brighter (0.5f);
    if (! b.isEnabled()) col = col.withAlpha (0.4f);
    g.setColour (col);
    auto f = getTextButtonFont (b, b.getHeight());
    if (b.getProperties().contains ("kerning")) f.setExtraKerningFactor ((float) b.getProperties()["kerning"]);
    g.setFont (f);
    auto area = b.getLocalBounds().reduced (6, 2);
    // icônes dessinées (lecture / arrêt / dossier)
    const juce::String icon = b.getProperties()["icon"];
    if (icon.isNotEmpty())
    {
        const float s = 16.0f;
        juce::Path p;
        auto c = b.getLocalBounds().toFloat().getCentre();
        const bool alone = b.getButtonText().isEmpty();
        float ix = alone ? c.x - s / 2 : (float) area.getX() + 10;
        if (! alone)
        {
            const float tw = juce::GlyphArrangement::getStringWidth (f, b.getButtonText());
            ix = c.x - (tw + s + 8) / 2;
            area.setLeft ((int) (ix + s + 8));
            area.setRight ((int) (ix + s + 8 + tw + 4));
        }
        auto ir = juce::Rectangle<float> (ix, c.y - s / 2, s, s);
        if (icon == "play") { p.addTriangle (ir.getX() + 2, ir.getY(), ir.getRight(), ir.getCentreY(), ir.getX() + 2, ir.getBottom()); g.setColour (on ? col : theme::green); g.fillPath (p); }
        else if (icon == "stop") { g.fillRoundedRectangle (ir.reduced (1.5f), 2.5f); }
        else if (icon == "folder")
        {
            p.startNewSubPath (ir.getX(), ir.getY() + 3); p.lineTo (ir.getX() + 6, ir.getY() + 3); p.lineTo (ir.getX() + 8, ir.getY() + 5);
            p.lineTo (ir.getRight(), ir.getY() + 5); p.lineTo (ir.getRight(), ir.getBottom() - 1); p.lineTo (ir.getX(), ir.getBottom() - 1); p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (col);
        if (alone) return;
        g.drawText (b.getButtonText(), area, juce::Justification::centredLeft, false);
        return;
    }
    g.drawText (b.getButtonText(), area, juce::Justification::centred, false);
}

juce::Font SpaceLook::getTextButtonFont (juce::TextButton& b, int h)
{
    const float size = b.getProperties().contains ("fontSize") ? (float) b.getProperties()["fontSize"] : juce::jmin (15.0f, h * 0.36f);
    return theme::ui (size, b.getProperties().contains ("weight") ? (int) b.getProperties()["weight"] : 1);
}

void SpaceLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    insetBox (g, r, 9.0f, juce::Colour (0xff0a0f1c));
    juce::Path a;
    const float cx = (float) w - 16, cy = h * 0.5f;
    a.startNewSubPath (cx - 5, cy - 2); a.lineTo (cx, cy + 3); a.lineTo (cx + 5, cy - 2);
    g.setColour (theme::accent);
    g.strokePath (a, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SpaceLook::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    auto r = vertical ? juce::Rectangle<int> (x + w / 2 - 2, ts, 4, tsz) : juce::Rectangle<int> (ts, y + h / 2 - 2, tsz, 4);
    g.setColour (theme::accent.withAlpha (over ? 0.6f : 0.3f));
    g.fillRoundedRectangle (r.toFloat(), 2.0f);
}

// ============================================================================================
// Knob
// ============================================================================================
Knob::Knob (SpacepadProcessor& p, const juce::String& paramId, const juce::String& l, juce::Colour c)
    : proc (p), id (paramId), label (l)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    slider.setMouseDragSensitivity (220);
    slider.setDoubleClickReturnValue (true, p.params.getParameter (paramId)->convertFrom0to1 (p.params.getParameter (paramId)->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.setTitle (l);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    slider.setBounds (r.removeFromTop (juce::jmin (r.getWidth(), 76)).withSizeKeepingCentre (76, 76));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (78);
    g.setColour (theme::text);
    g.setFont (theme::ui (13.0f, 1));
    g.drawText (label, r.removeFromTop (18), juce::Justification::centred);
    g.setColour (theme::muted);
    g.setFont (theme::ui (11.5f));
    auto* prm = proc.params.getParameter (id);
    g.drawText (prm->getText (prm->getValue(), 16), r.removeFromTop (15), juce::Justification::centred);
}

// ============================================================================================
// Header
// ============================================================================================
void Header::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2135), 0, 0, juce::Colour (0xff111727), 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (theme::line);
    g.drawLine (0, r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f, 1.0f);

    // logo
    glowText (g, "SPACEPAD", theme::title (25.0f), { 28, 18, 260, 32 }, juce::Justification::centredLeft, juce::Colour (0xfff2f6ff), theme::accent.withAlpha (0.45f), 14, 0.22f);
    auto by = theme::ui (11.5f, 1); by.setExtraKerningFactor (0.28f);
    g.setFont (by);
    g.setColour (juce::Colour (0xff9aa6bd));
    g.drawText ("BY JB PRAISE", juce::Rectangle<float> (30, 52, 240, 16), juce::Justification::centredLeft);

    // écran
    auto lcd = juce::Rectangle<float> (300, 16, r.getWidth() - 300 - 150, 52);
    insetBox (g, lcd, 12.0f);
    const bool sounding = proc.padSounding() || proc.crowdSounding();
    auto dot = juce::Rectangle<float> (lcd.getX() + 20, lcd.getCentreY() - 5, 10, 10);
    if (sounding) glowEllipse (g, juce::DropShadow (theme::green, 10, {}), dot);
    g.setColour (sounding ? theme::green : theme::muted.withAlpha (0.5f));
    g.fillEllipse (dot);

    juce::String libName = L ("AUCUNE LIBRAIRIE", "NO LIBRARY", "NINGUNA LIBRER\xc3\x8d""A");
    if (auto lib = proc.libraries().find (proc.padLibraryId())) libName = lib->name.toUpperCase();
    juce::String sub = U ("PAD \xc2\xb7 ") + noteName (proc.currentKey()).toUpperCase() + " " + modeWord (proc.currentMode()).toUpperCase();
    if (proc.padActive() && proc.padSoundName().isNotEmpty()) sub << U (" \xc2\xb7 ") << proc.padSoundName();
    auto tr = lcd.reduced (44, 0).withTrimmedRight (110);
    auto f1 = theme::ui (17.0f, 1);
    const float w1 = juce::jmin (tr.getWidth() * 0.55f, juce::GlyphArrangement::getStringWidth (f1, libName) + 4);
    glowText (g, libName, f1, tr.removeFromLeft (w1), juce::Justification::centredLeft, theme::accentLight, theme::accent.withAlpha (0.5f), 8);
    g.setColour (juce::Colour (0xff7f8ca6));
    g.setFont (theme::ui (13.0f));
    g.drawText (sub, tr.withTrimmedLeft (16), juce::Justification::centredLeft, true);
    const bool loop = proc.loopOn();
    g.setColour (loop ? juce::Colour (0xffffcf8a) : theme::muted);
    g.setFont (theme::ui (13.0f, 1));
    g.drawText (loop ? "LOOP ON" : "LOOP OFF", lcd.withTrimmedRight (20).removeFromRight (100), juce::Justification::centredRight);

    // niveau de sortie
    const float lvl = proc.outputLevel();
    const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
    const int lit = juce::jlimit (0, 14, (int) std::round ((db + 48.0f) / 48.0f * 14.0f));
    for (int i = 0; i < 14; ++i)
    {
        const float h = 12 + i * 2.2f;
        auto br = juce::Rectangle<float> (r.getRight() - 118 + i * 7.5f, 64 - h, 5, h);
        auto col = i < 9 ? theme::green : i < 12 ? juce::Colour (0xffffcf5c) : theme::red;
        g.setColour (i < lit ? col : juce::Colour (0xff1f2638));
        g.fillRoundedRectangle (br, 1.5f);
    }
}

// ============================================================================================
// KeyRing
// ============================================================================================
int KeyRing::keyAt (juce::Point<float> p) const
{
    auto b = getLocalBounds().toFloat();
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float d = p.getDistanceFrom (c);
    if (d < D * 0.25f) return -1;
    const float R = D * 0.425f, btn = D * 0.068f;
    for (int i = 0; i < 12; ++i)
    {
        const float a = (i * 30.0f - 90.0f) * juce::MathConstants<float>::pi / 180.0f;
        juce::Point<float> k (c.x + R * std::cos (a), c.y + R * std::sin (a));
        if (p.getDistanceFrom (k) < btn * 1.25f) return i;
    }
    return -2;
}

void KeyRing::mouseDown (const juce::MouseEvent& e)
{
    const int h = keyAt (e.position);
    if (h >= 0) proc.pressKey (h);
    else if (h == -1) { if (proc.padActive()) proc.stopPad(); else proc.playPad(); }
    repaint();
}

void KeyRing::mouseMove (const juce::MouseEvent& e)
{
    const int h = keyAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= -1 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// lueur douce : dégradé radial (aussi beau qu'un flou, mais presque gratuit à dessiner à chaque image)
static void softGlow (juce::Graphics& g, juce::Point<float> c, float r0, float r1, juce::Colour col)
{
    if (r1 <= r0 || col.getAlpha() == 0) return;
    juce::ColourGradient gr (col, c.x, c.y, col.withAlpha (0.0f), c.x + r1, c.y, true);
    gr.addColour (juce::jlimit (0.0, 0.99, (double) (r0 / r1)), col);
    gr.addColour (juce::jlimit (0.0, 0.995, (double) ((r0 + (r1 - r0) * 0.45f) / r1)), col.withMultipliedAlpha (0.35f));
    g.setGradientFill (gr);
    g.fillEllipse (c.x - r1, c.y - r1, r1 * 2, r1 * 2);
}

void KeyRing::paintKey (juce::Graphics& g, int i, bool sel, bool active, bool hov, int mode, float D, juce::Point<float> c) const
{
    const float s = D / 470.0f, R = D * 0.425f, btn = D * 0.0617f;
    const float a = (i * 30.0f - 90.0f) * juce::MathConstants<float>::pi / 180.0f;
    auto kc = juce::Point<float> (c.x + R * std::cos (a), c.y + R * std::sin (a));
    auto kr = juce::Rectangle<float> (btn * 2, btn * 2).withCentre (kc);
    if (sel && active)
    {
        softGlow (g, kc, btn * 0.9f, btn * 2.1f, theme::accent.withAlpha (0.55f));
        juce::ColourGradient kg (juce::Colours::white, kc.x - btn * 0.3f, kc.y - btn * 0.5f, juce::Colour (0xff1f5e8a), kc.x + btn, kc.y + btn, true);
        kg.addColour (0.5, theme::accent);
        g.setGradientFill (kg);
        g.fillEllipse (kr);
        g.setColour (juce::Colours::white);
        g.drawEllipse (kr.reduced (1), 2.0f);
        g.setColour (juce::Colour (0xff04121c));
    }
    else
    {
        softGlow (g, kc.translated (0, 6 * s), btn * 0.8f, btn * 1.45f, juce::Colours::black.withAlpha (0.55f));
        juce::ColourGradient kg (juce::Colour (hov ? 0xff4a5878 : 0xff3a4560), kc.x - btn * 0.3f, kc.y - btn * 0.5f, juce::Colour (0xff151b2b), kc.x + btn, kc.y + btn, true);
        g.setGradientFill (kg);
        g.fillEllipse (kr);
        g.setColour (sel ? theme::accent : juce::Colours::white.withAlpha (0.14f));
        g.drawEllipse (kr.reduced (0.5f), sel ? 2.0f : 1.0f);
        g.setColour (sel ? theme::accentLight : juce::Colour (0xffdfe6f5));
    }
    g.setFont (theme::ui (17.0f * s, 2));
    g.drawText (keyName (i) + (sel && mode == 1 ? "m" : ""), kr, juce::Justification::centred);
}

void KeyRing::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float s = D / 470.0f;
    const bool active = proc.padActive();
    const int key = proc.currentKey(), mode = proc.currentMode();

    // 1. le décor fixe (disque, pointillés, 12 touches au repos) est dessiné une seule fois puis réutilisé
    const float px = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! base.isValid() || baseSize != getLocalBounds().getWidth() * 10000 + getLocalBounds().getHeight() || std::abs (baseScale - px) > 0.01f)
    {
        baseSize = getLocalBounds().getWidth() * 10000 + getLocalBounds().getHeight();
        baseScale = px;
        base = juce::Image (juce::Image::ARGB, juce::roundToInt (b.getWidth() * px), juce::roundToInt (b.getHeight() * px), true);
        juce::Graphics bg (base);
        bg.addTransform (juce::AffineTransform::scale (px));
        auto disc = juce::Rectangle<float> (D, D).withCentre (c);
        softGlow (bg, c.translated (0, 24 * s), D * 0.45f, D * 0.56f, juce::Colours::black.withAlpha (0.6f));
        juce::ColourGradient grad (juce::Colour (0xff0b1020), c.x, c.y, juce::Colour (0xff0a0e18), c.x + D * 0.5f, c.y, true);
        grad.addColour (0.76, juce::Colour (0xff0b1020));
        grad.addColour (0.97, juce::Colour (0xff1a2440));
        bg.setGradientFill (grad);
        bg.fillEllipse (disc);
        bg.setColour (juce::Colours::white.withAlpha (0.08f));
        bg.drawEllipse (disc.reduced (1), 1.0f);
        juce::Path circ; circ.addEllipse (disc.reduced (34 * s));
        juce::Path dash; const float dl[] = { 4.0f * s, 5.0f * s };
        juce::PathStrokeType (1.0f).createDashedStroke (dash, circ, dl, 2);
        bg.setColour (theme::accent.withAlpha (0.22f));
        bg.fillPath (dash);
        for (int i = 0; i < 12; ++i) paintKey (bg, i, false, false, false, 0, D, c);
    }
    g.drawImage (base, b);

    // 2. ce qui bouge : touche survolée, touche jouée, cœur lumineux
    if (hover >= 0 && hover != key) paintKey (g, hover, false, false, true, mode, D, c);
    paintKey (g, key, true, active, hover == key, mode, D, c);

    const float coreR = D * 0.234f;
    auto core = juce::Rectangle<float> (coreR * 2, coreR * 2).withCentre (c);
    const float pulse = active ? 0.85f + 0.15f * std::sin (phase) : 0.35f;
    softGlow (g, c, coreR * 0.95f, coreR * 1.75f, theme::violet.withAlpha (0.22f * pulse));
    softGlow (g, c, coreR * 0.95f, coreR * 1.38f, theme::accent.withAlpha (0.42f * pulse));
    juce::ColourGradient cg (juce::Colour (0xffd8f6ff).withMultipliedBrightness (0.4f + 0.6f * pulse), c.x - coreR * 0.2f, c.y - coreR * 0.36f, juce::Colour (0xff070b16), c.x + coreR, c.y + coreR, true);
    cg.addColour (0.15, theme::accent.withMultipliedBrightness (0.45f + 0.55f * pulse));
    cg.addColour (0.42, juce::Colour (0xff2a5f9e).withMultipliedBrightness (0.5f + 0.5f * pulse));
    cg.addColour (0.70, juce::Colour (0xff0e1b38));
    g.setGradientFill (cg);
    g.fillEllipse (core);
    if (hover == -1) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.drawEllipse (core.reduced (2), 2.0f); }

    // nom de la tonalité avec sa lueur : préparé une fois par tonalité, puis réutilisé
    const auto label = keyLabel (key, mode);
    auto labelArea = core.withSizeKeepingCentre (core.getWidth(), 70 * s).translated (0, -14 * s);
    if (label != labelText || ! labelImg.isValid() || std::abs (labelScale - px) > 0.01f || labelW != juce::roundToInt (labelArea.getWidth()))
    {
        labelText = label; labelScale = px; labelW = juce::roundToInt (labelArea.getWidth());
        labelImg = juce::Image (juce::Image::ARGB, juce::roundToInt (labelArea.getWidth() * px), juce::roundToInt (labelArea.getHeight() * px), true);
        juce::Graphics lg (labelImg);
        lg.addTransform (juce::AffineTransform::scale (px));
        glowText (lg, label, theme::title (58.0f * s), labelArea.withPosition (0, 0), juce::Justification::centred, juce::Colours::white, theme::accentLight.withAlpha (0.8f), (int) (22 * s * px));
    }
    g.drawImage (labelImg, labelArea);

    auto f = theme::ui (12.0f * s, 1); f.setExtraKerningFactor (0.3f);
    g.setFont (f);
    g.setColour (juce::Colour (0xffd9f4ff));
    g.drawText ((noteName (key) + " " + modeWord (mode)).toUpperCase(), core.withSizeKeepingCentre (core.getWidth(), 18 * s).translated (0, 30 * s), juce::Justification::centred);
    g.setFont (theme::ui (11.0f * s));
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawText (active ? L ("appuyer pour arr\xc3\xaater", "tap to stop", "toca para detener") : L ("appuyer pour jouer", "tap to play", "toca para tocar"), core.withSizeKeepingCentre (core.getWidth(), 16 * s).translated (0, 52 * s), juce::Justification::centred);
}

// ============================================================================================
// Waveform
// ============================================================================================
void Waveform::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto s = proc.padSample();
    if (s == nullptr || s->peaks.empty())
    {
        g.setColour (theme::muted);
        g.setFont (theme::ui (13.0f));
        g.drawText (proc.libraries().count() == 0 ? L ("Choisissez le dossier de vos sons \xe2\x80\x94 Spacepad l'analyse tout seul", "Choose your sounds folder \xe2\x80\x94 Spacepad analyzes it on its own", "Elige la carpeta de tus sonidos \xe2\x80\x94 Spacepad la analiza solo")
                                                        : L ("Choisissez une tonalit\xc3\xa9 sur l'anneau pour lancer le pad", "Pick a key on the ring to start the pad", "Elige una tonalidad en el anillo para iniciar el pad"),
                    r, juce::Justification::centred);
        return;
    }
    const int L = s->buffer.getNumSamples();
    const int X = juce::jmin (L / 4, (int) (1.5 * s->sampleRate));
    const bool loop = proc.loopOn();
    const float loopEnd = loop ? (float) (L - X) / (float) L : 1.0f;
    if (loop)
    {
        auto lr = juce::Rectangle<float> (r.getX(), r.getY() - 2, r.getWidth() * loopEnd, r.getHeight() + 4);
        g.setColour (theme::amber.withAlpha (0.08f));
        g.fillRoundedRectangle (lr, 6.0f);
        g.setColour (theme::amber);
        g.fillRect (lr.getX(), lr.getY(), 2.0f, lr.getHeight());
        g.fillRect (lr.getRight() - 2, lr.getY(), 2.0f, lr.getHeight());
    }
    const int bars = juce::jmax (40, (int) (r.getWidth() / 5.0f));
    const float bw = r.getWidth() / bars;
    for (int i = 0; i < bars; ++i)
    {
        const size_t a = (size_t) i * s->peaks.size() / (size_t) bars, e = juce::jmax (a + 1, (size_t) (i + 1) * s->peaks.size() / (size_t) bars);
        float v = 0;
        for (size_t k = a; k < e && k < s->peaks.size(); ++k) v = juce::jmax (v, s->peaks[k]);
        const float h = juce::jmax (3.0f, v * r.getHeight());
        const bool in = (i + 0.5f) / bars <= loopEnd;
        g.setColour ((in ? theme::accent : juce::Colour (0xff3a4560)).withAlpha (in ? 0.9f : 0.6f));
        g.fillRoundedRectangle (r.getX() + i * bw + 0.5f, r.getCentreY() - h / 2, juce::jmax (1.0f, bw - 2.0f), h, 1.5f);
    }
    if (proc.padSounding())
    {
        const float x = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, proc.padPosition());
        juce::DropShadow (juce::Colours::white, 6, {}).drawForRectangle (g, juce::Rectangle<int> ((int) x - 1, (int) r.getY(), 2, (int) r.getHeight()));
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1, r.getY(), 2.0f, r.getHeight());
    }
}

// ============================================================================================
// LibraryPanel
// ============================================================================================
LibraryPanel::LibraryPanel (SpacepadProcessor& p) : proc (p)
{
    for (auto* t : { &padTab, &crowdTab })
    {
        t->setClickingTogglesState (false);
        t->getProperties().set ("fontSize", 13.5f);
        t->getProperties().set ("weight", 2);
        t->getProperties().set ("kerning", 0.06f);
        addAndMakeVisible (*t);
    }
    padTab.onClick = [this] { showTab (false); };
    crowdTab.onClick = [this] { showTab (true); };
    chooseBtn.getProperties().set ("dashed", true);
    chooseBtn.getProperties().set ("icon", "folder");
    chooseBtn.getProperties().set ("fontSize", 14.5f);
    chooseBtn.onClick = [this] { if (onChooseFolder) onChooseFolder(); };
    addAndMakeVisible (chooseBtn);
    addChildComponent (crowdPicker);
    crowdPicker.onChange = [this]
    {
        const int i = crowdPicker.getSelectedItemIndex();
        if (i >= 0 && i < (int) crowdIdx.size()) { crowdLibId = libs[(size_t) crowdIdx[(size_t) i]].id; category = 0; rebuildChips(); list.updateContent(); repaint(); }
    };
    chooseBtn.setButtonText (L ("Choisir un dossier\xe2\x80\xa6", "Choose a folder\xe2\x80\xa6", "Elegir una carpeta\xe2\x80\xa6"));
    list.setModel (this);
    list.setRowHeight (56);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
    refresh();
    showTab (false);
}

const Library* LibraryPanel::currentCrowd() const
{
    for (int i : crowdIdx) if (libs[(size_t) i].id == crowdLibId) return &libs[(size_t) i];
    return crowdIdx.empty() ? nullptr : &libs[(size_t) crowdIdx[0]];
}

const Category* LibraryPanel::currentCategory() const
{
    auto* l = currentCrowd();
    if (l == nullptr || l->categories.empty()) return nullptr;
    return &l->categories[(size_t) juce::jlimit (0, (int) l->categories.size() - 1, category)];
}

void LibraryPanel::refresh()
{
    libs = proc.libraries().snapshot();
    padIdx.clear(); crowdIdx.clear();
    for (int i = 0; i < (int) libs.size(); ++i) (libs[(size_t) i].type == LibType::Pad ? padIdx : crowdIdx).push_back (i);
    if (currentCrowd() != nullptr) crowdLibId = currentCrowd()->id;
    crowdPicker.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) crowdIdx.size(); ++i)
    {
        crowdPicker.addItem (libs[(size_t) crowdIdx[(size_t) i]].name, i + 1);
        if (libs[(size_t) crowdIdx[(size_t) i]].id == crowdLibId) crowdPicker.setSelectedItemIndex (i, juce::dontSendNotification);
    }
    rebuildChips();
    list.updateContent();
    repaint();
}

void LibraryPanel::showTab (bool c)
{
    crowd = c;
    padTab.setToggleState (! c, juce::dontSendNotification);
    crowdTab.setToggleState (c, juce::dontSendNotification);
    rebuildChips();
    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void LibraryPanel::rebuildChips()
{
    for (auto& c : chips) removeChildComponent (c.get());
    chips.clear();
    auto chip = [this] (const juce::String& t, bool on, std::function<void()> fn)
    {
        auto b = std::make_unique<juce::TextButton> (t);
        b->setToggleState (on, juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b->getProperties().set ("fontSize", 13.0f);
        b->onClick = std::move (fn);
        addAndMakeVisible (*b);
        chips.push_back (std::move (b));
    };
    if (! crowd)
    {
        const int m = proc.currentMode();
        lastMode = m;
        chip (L ("Majeur", "Major", "Mayor"), m == 0, [this] { proc.setMode (0); rebuildChips(); });
        chip (L ("Mineur", "Minor", "Menor"), m == 1, [this] { proc.setMode (1); rebuildChips(); });
    }
    else if (auto* l = currentCrowd())
    {
        for (int i = 0; i < (int) l->categories.size() && i < 12; ++i)
        {
            auto n = l->categories[(size_t) i].name.isEmpty() ? l->name : l->categories[(size_t) i].name;
            chip (n, i == category, [this, i] { category = i; rebuildChips(); list.updateContent(); list.repaint(); repaint(); });
        }
    }
    crowdPicker.setVisible (crowd && crowdIdx.size() > 1);
    resized();
}

void LibraryPanel::retranslate()
{
    chooseBtn.setButtonText (L ("Choisir un dossier\xe2\x80\xa6", "Choose a folder\xe2\x80\xa6", "Elegir una carpeta\xe2\x80\xa6"));
    rebuildChips();
    list.repaint();
    repaint();
}

void LibraryPanel::updateState()
{
    if (! crowd && proc.currentMode() != lastMode) rebuildChips();
    // la liste n'est redessinée que si la librairie choisie ou le son joué change
    const auto sig = proc.padLibraryId() + "|" + proc.crowdLibraryId() + "|" + juce::String (proc.crowdSoundIndex()) + "|" + juce::String ((int) proc.crowdSounding()) + "|" + juce::String ((int) proc.loopOn());
    if (sig != lastStateSig) { lastStateSig = sig; list.repaint(); }
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto tabs = r.removeFromTop (52);
    auto tl = tabs.reduced (4);
    padTab.setBounds (tl.removeFromLeft (tl.getWidth() / 2).reduced (1));
    crowdTab.setBounds (tl.reduced (1));
    r.removeFromTop (10);
    statusArea = r.removeFromTop (36);
    r.removeFromTop (10);
    if (crowdPicker.isVisible()) { crowdPicker.setBounds (r.removeFromTop (34)); r.removeFromTop (8); }
    // pastilles sur plusieurs lignes
    int x = r.getX(), y = r.getY(), rows = 1;
    for (auto& c : chips)
    {
        auto f = theme::ui (13.0f, 1);
        const int w = juce::jmin (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (f, c->getButtonText()) + 30);
        if (x + w > r.getRight() && x > r.getX()) { x = r.getX(); y += 40; ++rows; }
        if (rows > 3) { c->setVisible (false); continue; }
        c->setVisible (true);
        c->setBounds (x, y, w, 34);
        x += w + 6;
    }
    r.removeFromTop (chips.empty() ? 0 : y - r.getY() + 34 + 10);
    titleArea = r.removeFromTop (20);
    r.removeFromTop (4);
    chooseBtn.setBounds (r.removeFromBottom (48));
    r.removeFromBottom (10);
    list.setBounds (r);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (theme::panel, 0, 0, theme::panel2, b.getRight(), 0, false));
    g.fillRect (b);
    g.setColour (theme::line);
    g.drawLine (b.getRight() - 0.5f, 0, b.getRight() - 0.5f, b.getBottom());

    insetBox (g, padTab.getBounds().getUnion (crowdTab.getBounds()).toFloat().expanded (4), 13.0f, juce::Colour (0xff060912));

    // état : bibliothèque reconnue
    auto st = statusArea.toFloat();
    const int count = crowd ? (int) crowdIdx.size() : (int) padIdx.size();
    const bool ok = count > 0;
    auto col = ok ? theme::green : theme::amber;
    g.setColour (col.withAlpha (0.08f));
    g.fillRoundedRectangle (st, 10.0f);
    g.setColour (col.withAlpha (0.3f));
    g.drawRoundedRectangle (st.reduced (0.5f), 10.0f, 1.0f);
    auto dot = juce::Rectangle<float> (st.getX() + 12, st.getCentreY() - 4, 8, 8);
    glowEllipse (g, juce::DropShadow (col, 8, {}), dot);
    g.setColour (col);
    g.fillEllipse (dot);
    g.setColour (ok ? juce::Colour (0xffc4ffe2) : juce::Colour (0xffffe0bd));
    g.setFont (theme::ui (12.5f, 1));
    juce::String txt = ok ? (juce::String (crowd ? "Crowd Library" : "Pad Library") + L (" reconnue \xc2\xb7 ", " recognized \xc2\xb7 ", " reconocida \xc2\xb7 ") + juce::String (count))
                          : L ("Aucune librairie pour l'instant", "No library yet", "Todav\xc3\xad""a no hay librer\xc3\xad""as");
    g.drawText (txt, st.withTrimmedLeft (28).withTrimmedRight (118), juce::Justification::centredLeft, true);
    float tx = st.getRight() - 112;
    for (auto t : { "APP", "VST3", "AU" })
    {
        const float w = t[0] == 'V' ? 36.0f : 30.0f;
        auto tr = juce::Rectangle<float> (tx, st.getCentreY() - 9, w, 18);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (tr, 4.0f);
        g.setColour (juce::Colour (0xffcfd8ea));
        g.setFont (theme::ui (10.0f, 2));
        g.drawText (t, tr, juce::Justification::centred);
        tx += w + 4;
    }

    // titre de la liste
    g.setColour (theme::muted);
    auto f = theme::ui (11.0f, 1); f.setExtraKerningFactor (0.15f);
    g.setFont (f);
    juce::String title;
    if (! crowd) title = juce::String ((int) padIdx.size()) + L (" librairies de pads", " pad libraries", " librer\xc3\xad""as de pads");
    else if (auto* c = currentCategory()) title = (c->name.isEmpty() ? currentCrowd()->name : c->name) + U (" \xc2\xb7 ") + juce::String ((int) c->sounds.size()) + L (" sons", " sounds", " sonidos");
    g.drawText (title.toUpperCase(), titleArea.toFloat(), juce::Justification::centredLeft, true);

    if (getNumRows() == 0)
    {
        auto lr = list.getBounds().toFloat().reduced (10, 30);
        g.setColour (theme::muted);
        g.setFont (theme::ui (14.0f));
        g.drawFittedText (crowd ? L ("Aucune Crowd Library.\n\nCliquez sur \xc2\xab Choisir un dossier\xe2\x80\xa6 \xc2\xbb : Spacepad lit le dossier, garde son nom et range vos crowds (Worship FX, Natural\xe2\x80\xa6) tout seul.", "No Crowd Library.\n\nClick \xe2\x80\x9c""Choose a folder\xe2\x80\xa6\xe2\x80\x9d: Spacepad reads the folder, keeps its name and sorts your crowds (Worship FX, Natural\xe2\x80\xa6) on its own.", "Ninguna Crowd Library.\n\nHaz clic en \xc2\xab""Elegir una carpeta\xe2\x80\xa6\xc2\xbb: Spacepad lee la carpeta, conserva su nombre y ordena tus crowds (Worship FX, Natural\xe2\x80\xa6) solo.")
                                : L ("Aucune Pad Library.\n\nCliquez sur \xc2\xab Choisir un dossier\xe2\x80\xa6 \xc2\xbb : Spacepad lit le dossier, garde son nom, trouve la tonalit\xc3\xa9 de chaque son et l'enregistre pour l'application et les plugins.", "No Pad Library.\n\nClick \xe2\x80\x9c""Choose a folder\xe2\x80\xa6\xe2\x80\x9d: Spacepad reads the folder, keeps its name, finds the key of every sound and saves it for the app and the plugins.", "Ninguna Pad Library.\n\nHaz clic en \xc2\xab""Elegir una carpeta\xe2\x80\xa6\xc2\xbb: Spacepad lee la carpeta, conserva su nombre, encuentra la tonalidad de cada sonido y la guarda para la aplicaci\xc3\xb3n y los plugins."),
                          lr.toNearestInt(), juce::Justification::centredTop, 10);
    }
}

int LibraryPanel::getNumRows()
{
    if (! crowd) return (int) padIdx.size();
    auto* c = currentCategory();
    return c ? (int) c->sounds.size() : 0;
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0, 3);
    juce::String name, sub, tag;
    bool on = false;
    juce::Colour dotCol;
    if (! crowd)
    {
        if (row >= (int) padIdx.size()) return;
        auto& l = libs[(size_t) padIdx[(size_t) row]];
        name = l.name; sub = U ("Pad \xc2\xb7 ") + l.summary();
        const int ex = l.exactSlots (0) + l.exactSlots (1);
        tag = juce::String (ex) + "/24";
        on = l.id == proc.padLibraryId();
        dotCol = theme::colourFor (l.name);
    }
    else
    {
        auto* c = currentCategory();
        if (c == nullptr || row >= (int) c->sounds.size()) return;
        auto& s = c->sounds[(size_t) row];
        name = s.name;
        sub = s.key >= 0 ? L ("Tonalit\xc3\xa9 : ", "Key: ", "Tonalidad: ") + keyLabel (s.key, s.mode) : L ("Sans tonalit\xc3\xa9", "No key", "Sin tonalidad");
        sub << U (" \xc2\xb7 ") << juce::String (s.seconds, 1) << " s";
        tag = s.key >= 0 ? keyLabel (s.key, s.mode) : (proc.loopOn() ? juce::String ("LOOP") : L ("1 COUP", "ONE SHOT", "1 GOLPE"));
        int flat = row;
        if (auto* l = currentCrowd())
            for (auto& cc : l->categories) { if (&cc == c) break; flat += (int) cc.sounds.size(); }
        on = proc.crowdSounding() && proc.crowdSoundIndex() == flat && proc.crowdLibraryId() == crowdLibId;
        dotCol = theme::colourFor (s.name);
    }
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.18f), r.getX(), 0, juce::Colours::white.withAlpha (0.02f), r.getRight(), 0, false));
        g.fillRoundedRectangle (r, 11.0f);
        g.setColour (theme::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.0f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRoundedRectangle (r, 11.0f);
    }
    auto dot = juce::Rectangle<float> (r.getX() + 14, r.getCentreY() - 5, 10, 10);
    glowEllipse (g, juce::DropShadow (dotCol, 8, {}), dot);
    g.setColour (dotCol);
    g.fillEllipse (dot);
    auto tr = r.withTrimmedLeft (36).withTrimmedRight (62);
    g.setColour (juce::Colour (0xffeef3ff));
    g.setFont (theme::ui (14.0f, 1));
    g.drawText (name, tr.withTrimmedBottom (r.getHeight() * 0.42f), juce::Justification::bottomLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::ui (11.5f));
    g.drawText (sub, tr.withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::topLeft, true);
    g.setColour (juce::Colour (0xffffcf8a));
    g.setFont (theme::ui (11.5f, 1));
    g.drawText (tag, r.withTrimmedRight (12).removeFromRight (56), juce::Justification::centredRight);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! crowd)
    {
        if (row >= (int) padIdx.size()) return;
        auto l = libs[(size_t) padIdx[(size_t) row]];
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addSectionHeader (l.name);
            m.addItem (1, L ("Ranger dans la Crowd Library", "Move to Crowd Library", "Mover a Crowd Library"));
            m.addItem (2, L ("Afficher le dossier", "Show folder", "Mostrar la carpeta"));
            m.addSeparator();
            m.addItem (3, L ("Retirer de Spacepad (vos fichiers ne sont pas effac\xc3\xa9s)", "Remove from Spacepad (your files are not deleted)", "Quitar de Spacepad (tus archivos no se borran)"));
            m.showMenuAsync (juce::PopupMenu::Options(), [this, l] (int r)
            {
                if (r == 1) { proc.libraries().setType (l.id, LibType::Crowd); refresh(); }
                if (r == 2) juce::File (l.folder).revealToUser();
                if (r == 3) { proc.libraries().remove (l.id); refresh(); }
            });
            return;
        }
        proc.selectPadLibrary (l.id);
        if (! proc.padActive()) proc.playPad();
        list.repaint();
        return;
    }
    auto* l = currentCrowd();
    auto* c = currentCategory();
    if (l == nullptr || c == nullptr || row >= (int) c->sounds.size()) return;
    int flat = row;
    for (auto& cc : l->categories) { if (&cc == c) break; flat += (int) cc.sounds.size(); }
    if (e.mods.isPopupMenu())
    {
        auto lib = *l;
        juce::PopupMenu m;
        m.addSectionHeader (lib.name);
        m.addItem (1, L ("Ranger dans la Pad Library", "Move to Pad Library", "Mover a Pad Library"));
        m.addItem (2, L ("Afficher le dossier", "Show folder", "Mostrar la carpeta"));
        m.addSeparator();
        m.addItem (3, L ("Retirer de Spacepad (vos fichiers ne sont pas effac\xc3\xa9s)", "Remove from Spacepad (your files are not deleted)", "Quitar de Spacepad (tus archivos no se borran)"));
        m.showMenuAsync (juce::PopupMenu::Options(), [this, lib] (int r)
        {
            if (r == 1) { proc.libraries().setType (lib.id, LibType::Pad); refresh(); }
            if (r == 2) juce::File (lib.folder).revealToUser();
            if (r == 3) { proc.libraries().remove (lib.id); refresh(); }
        });
        return;
    }
    proc.playCrowd (l->id, flat);
    list.repaint();
}

// ============================================================================================
// ScanOverlay
// ============================================================================================
void ScanOverlay::Job::run()
{
    FolderAnalyzer a ([this] { return threadShouldExit(); });
    a.onProgress = [this] (double p, const juce::String& s)
    {
        progress = p;
        const juce::ScopedLock sl (lock);
        status = s;
    };
    result = a.analyze (folder);
    finished = true;
}

ScanOverlay::ScanOverlay (SpacepadProcessor& p, const juce::File& folder) : proc (p), job (folder)
{
    cancelBtn.setButtonText (L ("Annuler", "Cancel", "Cancelar"));
    list.setModel (this);
    list.setRowHeight (60);
    list.setOutlineThickness (0);
    addChildComponent (list);
    cancelBtn.onClick = [this]
    {
        job.signalThreadShouldExit();
        job.stopThread (5000);
        if (onClose) onClose (false, false);
    };
    saveBtn.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    saveBtn.setToggleState (true, juce::dontSendNotification);
    saveBtn.setClickingTogglesState (false);
    saveBtn.getProperties().set ("weight", 2);
    saveBtn.onClick = [this]
    {
        if (results.empty()) return;
        proc.libraries().addOrReplace (results);
        bool crowdFirst = true;
        for (auto& l : results)
            if (l.type == LibType::Pad)
            {
                crowdFirst = false;
                if (proc.libraries().find (proc.padLibraryId()) == nullptr) proc.selectPadLibrary (l.id);
                break;
            }
        if (proc.libraries().find (proc.padLibraryId()) == nullptr)
            for (auto& l : proc.libraries().snapshot())
                if (l.type == LibType::Pad) { proc.selectPadLibrary (l.id); break; }
        if (onClose) onClose (true, crowdFirst);
    };
    saveBtn.setVisible (false);
    typeBtn.setVisible (false);
    typeBtn.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r < 0 || r >= (int) results.size()) return;
        auto& l = results[(size_t) r];
        l.type = l.type == LibType::Pad ? LibType::Crowd : LibType::Pad;
        updateTypeButton();
        list.repaintRow (r);
        repaint();
    };
    addAndMakeVisible (cancelBtn);
    addChildComponent (saveBtn);
    addChildComponent (typeBtn);
    job.startThread();
    startTimerHz (20);
    setWantsKeyboardFocus (true);
}

ScanOverlay::~ScanOverlay()
{
    job.signalThreadShouldExit();
    job.stopThread (5000);
}

juce::Rectangle<int> ScanOverlay::card() const
{
    return getLocalBounds().withSizeKeepingCentre (900, done ? 640 : 300);
}

void ScanOverlay::timerCallback()
{
    if (! done && job.finished.load())
    {
        done = true;
        results = job.result;
        list.updateContent();
        list.setVisible (true);
        saveBtn.setButtonText (results.empty() ? L ("Enregistrer", "Save", "Guardar")
                                               : L ("Enregistrer ", "Save ", "Guardar ") + juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies", " libraries", " librer\xc3\xad""as") : L (" librairie", " library", " librer\xc3\xad""a")));
        saveBtn.setVisible (! results.empty());
        typeBtn.setVisible (! results.empty());
        cancelBtn.setButtonText (results.empty() ? L ("Fermer", "Close", "Cerrar") : L ("Annuler", "Cancel", "Cancelar"));
        if (! results.empty()) list.selectRow (0);
        stopTimer();
        resized();
    }
    repaint();
}

void ScanOverlay::updateTypeButton()
{
    const int r = list.getSelectedRow();
    if (r < 0 || r >= (int) results.size()) return;
    typeBtn.setButtonText (results[(size_t) r].type == LibType::Pad ? L ("Rang\xc3\xa9 dans : PAD LIBRARY  \xe2\x87\x84", "Stored in: PAD LIBRARY  \xe2\x87\x84", "Guardado en: PAD LIBRARY  \xe2\x87\x84") : L ("Rang\xc3\xa9 dans : CROWD LIBRARY  \xe2\x87\x84", "Stored in: CROWD LIBRARY  \xe2\x87\x84", "Guardado en: CROWD LIBRARY  \xe2\x87\x84"));
}

void ScanOverlay::resized()
{
    auto c = card().reduced (30, 26);
    auto bottom = c.removeFromBottom (46);
    if (done)
    {
        saveBtn.setBounds (bottom.removeFromRight (280));
        bottom.removeFromRight (10);
        cancelBtn.setBounds (bottom.removeFromRight (130));
        c.removeFromTop (100);
        c.removeFromBottom (14);
        auto left = c.removeFromLeft (470);
        list.setBounds (left);
        c.removeFromLeft (20);
        typeBtn.setBounds (c.removeFromBottom (40));
    }
    else
        cancelBtn.setBounds (bottom.removeFromRight (130));
}

void ScanOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xd002040a));
    auto c = card().toFloat();
    for (int i = 1; i <= 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (c.expanded ((float) i * 4).translated (0, (float) i * 2), 24.0f + i * 4);
    }
    g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.55f), c.getTopLeft(), theme::violet.withAlpha (0.4f), c.getBottomRight(), false));
    g.fillRoundedRectangle (c, 24.0f);
    panelBg (g, c.reduced (2), 22.0f, juce::Colour (0xff121a2c), juce::Colour (0xff0a0e18));

    auto r = c.reduced (30, 26);
    g.setColour (theme::text);
    g.setFont (theme::ui (21.0f, 2));
    auto head = r.removeFromTop (30);
    g.drawText (done ? L ("Analyse termin\xc3\xa9""e", "Analysis complete", "An\xc3\xa1lisis terminado") : L ("Analyse du dossier", "Analyzing folder", "Analizando la carpeta"), head, juce::Justification::centredLeft);
    g.setFont (theme::ui (13.0f, 1));
    g.setColour (done ? theme::green : theme::accentLight);
    g.drawText (done ? (juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies trouv\xc3\xa9""es", " libraries found", " librer\xc3\xad""as encontradas") : L (" librairie trouv\xc3\xa9""e", " library found", " librer\xc3\xad""a encontrada"))) : juce::String (juce::roundToInt (job.progress.load() * 100)) + " %",
                head, juce::Justification::centredRight);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    g.drawText (job.folder.getFullPathName(), r.removeFromTop (22), juce::Justification::centredLeft, true);
    r.removeFromTop (10);

    if (! done)
    {
        auto bar = r.removeFromTop (12);
        insetBox (g, bar, 6.0f, juce::Colours::black);
        auto fill = bar.withWidth (juce::jmax (12.0f, bar.getWidth() * (float) job.progress.load()));
        juce::DropShadow (theme::accent, 10, {}).drawForRectangle (g, fill.toNearestInt());
        g.setGradientFill (juce::ColourGradient (theme::accent, fill.getTopLeft(), theme::violet, fill.getTopRight(), false));
        g.fillRoundedRectangle (fill, 6.0f);
        r.removeFromTop (16);
        juce::String st;
        { const juce::ScopedLock sl (job.lock); st = job.status; }
        g.setColour (theme::text);
        g.setFont (theme::ui (14.0f));
        g.drawText (L ("Lecture : ", "Reading: ", "Leyendo: ") + st, r.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (theme::muted);
        g.setFont (theme::ui (12.5f));
        g.drawFittedText (L ("Spacepad garde le nom de chaque dossier, d\xc3\xa9""cide s'il s'agit de pads ou de crowds, et trouve la tonalit\xc3\xa9 de chaque son (par le nom du fichier, sinon \xc3\xa0 l'\xc3\xa9""coute).", "Spacepad keeps each folder's name, decides whether it holds pads or crowds, and finds the key of every sound (from the file name, otherwise by listening).", "Spacepad conserva el nombre de cada carpeta, decide si contiene pads o crowds y encuentra la tonalidad de cada sonido (por el nombre del archivo o, si no, escuch\xc3\xa1ndolo)."),
                          r.removeFromTop (44).toNearestInt(), juce::Justification::topLeft, 2);
        return;
    }

    if (results.empty())
    {
        g.setColour (theme::amber);
        g.setFont (theme::ui (15.0f));
        g.drawFittedText (L ("Aucun son trouv\xc3\xa9 dans ce dossier (WAV, AIFF, FLAC, MP3, OGG).", "No sounds found in this folder (WAV, AIFF, FLAC, MP3, OGG).", "No se encontraron sonidos en esta carpeta (WAV, AIFF, FLAC, MP3, OGG)."), r.removeFromTop (60).toNearestInt(), juce::Justification::centredLeft, 2);
        return;
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    g.drawFittedText (L ("Les noms viennent de vos dossiers. Une fois enregistr\xc3\xa9""es, ces librairies sont reconnues dans l'application, le VST3 et l'Audio Unit.", "Names come from your folders. Once saved, these libraries are recognized in the app, the VST3 and the Audio Unit.", "Los nombres vienen de tus carpetas. Una vez guardadas, estas librer\xc3\xad""as se reconocen en la aplicaci\xc3\xb3n, el VST3 y el Audio Unit."),
                      r.removeFromTop (36).toNearestInt(), juce::Justification::topLeft, 2);

    // détail de la librairie choisie
    const int sel = list.getSelectedRow();
    if (sel < 0 || sel >= (int) results.size()) return;
    auto& lib = results[(size_t) sel];
    auto d = list.getBounds().toFloat().withX ((float) list.getRight() + 20).withRight (r.getRight());
    d.removeFromBottom (54);
    g.setColour (theme::text);
    g.setFont (theme::ui (16.0f, 2));
    g.drawText (lib.name, d.removeFromTop (24), juce::Justification::centredLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (lib.summary(), d.removeFromTop (20), juce::Justification::centredLeft, true);
    d.removeFromTop (10);
    if (lib.type == LibType::Pad)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            g.setColour (juce::Colour (0xffcfd8ea));
            g.setFont (theme::ui (12.0f, 1));
            g.drawText (mode == 0 ? L ("Majeur", "Major", "Mayor") : L ("Mineur", "Minor", "Menor"), d.removeFromTop (20), juce::Justification::centredLeft);
            auto row = d.removeFromTop (70);
            const float cw = (row.getWidth() - 5 * 4) / 6.0f;
            for (int k = 0; k < 12; ++k)
            {
                auto cell = juce::Rectangle<float> (row.getX() + (k % 6) * (cw + 4), row.getY() + (k / 6) * 34.0f, cw, 30);
                auto slot = lib.slotFor (k, mode);
                if (slot.exact)
                {
                    g.setColour (theme::accent.withAlpha (0.15f)); g.fillRoundedRectangle (cell, 6);
                    g.setColour (theme::accent); g.drawRoundedRectangle (cell.reduced (0.5f), 6, 1);
                    g.setColour (juce::Colour (0xffd9f4ff));
                }
                else
                {
                    juce::Path p; p.addRoundedRectangle (cell.reduced (0.5f), 6);
                    juce::Path dsh; const float dl[] = { 3, 3 };
                    juce::PathStrokeType (1).createDashedStroke (dsh, p, dl, 2);
                    g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillPath (dsh);
                    g.setColour (theme::muted);
                }
                g.setFont (theme::ui (11.5f, 2));
                g.drawText (keyLabel (k, mode), cell, juce::Justification::centred);
            }
            d.removeFromTop (6);
        }
        g.setColour (theme::muted);
        g.setFont (theme::ui (11.5f));
        g.drawFittedText (L ("En pointill\xc3\xa9s : tonalit\xc3\xa9 absente du dossier, jou\xc3\xa9""e en transposant le son le plus proche.", "Dashed: key missing from the folder, played by transposing the closest sound.", "Con puntos: tonalidad ausente de la carpeta, tocada transponiendo el sonido m\xc3\xa1s cercano."),
                          d.removeFromTop (34).toNearestInt(), juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (juce::Colour (0xffcfd8ea));
        g.setFont (theme::ui (12.0f, 1));
        g.drawText (L ("Cat\xc3\xa9gories (vos sous-dossiers)", "Categories (your subfolders)", "Categor\xc3\xad""as (tus subcarpetas)"), d.removeFromTop (22), juce::Justification::centredLeft);
        for (auto& c2 : lib.categories)
        {
            if (d.getHeight() < 22) break;
            g.setColour (theme::text);
            g.setFont (theme::ui (13.0f));
            g.drawText (U ("\xe2\x80\xa2 ") + (c2.name.isEmpty() ? lib.name : c2.name) + U ("  \xc2\xb7  ") + juce::String ((int) c2.sounds.size()) + L (" sons", " sounds", " sonidos"), d.removeFromTop (22), juce::Justification::centredLeft, true);
        }
    }
}

void ScanOverlay::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row >= (int) results.size()) return;
    auto& l = results[(size_t) row];
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0, 3);
    g.setColour (selected ? theme::accent.withAlpha (0.16f) : juce::Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (r, 10);
    if (selected) { g.setColour (theme::accent.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1); }
    const bool pad = l.type == LibType::Pad;
    auto pill = juce::Rectangle<float> (r.getRight() - 84, r.getCentreY() - 11, 72, 22);
    g.setColour ((pad ? theme::accent : theme::amber).withAlpha (0.18f));
    g.fillRoundedRectangle (pill, 11);
    g.setColour (pad ? theme::accentLight : juce::Colour (0xffffcf8a));
    g.setFont (theme::ui (11.0f, 2));
    g.drawText (pad ? "PAD" : "CROWD", pill, juce::Justification::centred);
    auto tr = r.withTrimmedLeft (16).withTrimmedRight (96);
    g.setColour (theme::text);
    g.setFont (theme::ui (14.5f, 1));
    g.drawText (l.name, tr.withTrimmedBottom (r.getHeight() * 0.45f), juce::Justification::bottomLeft, true);
    g.setColour (theme::muted);
    g.setFont (theme::ui (11.5f));
    g.drawText (l.summary(), tr.withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::topLeft, true);
}

// ============================================================================================
// MainView
// ============================================================================================
MainView::MainView (SpacepadProcessor& p) : proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (header);
    addAndMakeVisible (library);
    addAndMakeVisible (ring);
    addAndMakeVisible (wave);
    library.onChooseFolder = [this] { chooseFolder(); };

    playBtn.getProperties().set ("icon", "play");
    stopBtn.getProperties().set ("icon", "stop");
    playBtn.setTitle ("Lecture"); stopBtn.setTitle ("Stop");
    playBtn.onClick = [this] { proc.playPad(); };
    stopBtn.onClick = [this] { proc.stopPad(); };
    stopAllBtn.getProperties().set ("fontSize", 12.0f);
    stopAllBtn.getProperties().set ("kerning", 0.08f);
    stopAllBtn.onClick = [this] { proc.stopAll(); };
    for (auto* b : { &loopBtn, &freezeBtn, &swellBtn })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("weight", 2);
        b->getProperties().set ("kerning", 0.18f);
    }
    loopBtn.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    loopBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    freezeBtn.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    swellBtn.setColour (juce::TextButton::buttonOnColourId, theme::violet);
    swellBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff12082e));
    for (auto* b : { &playBtn, &stopBtn, &loopBtn, &stopAllBtn, &freezeBtn, &swellBtn }) addAndMakeVisible (*b);
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "loop", loopBtn);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "freeze", freezeBtn);
    swellAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "swell", swellBtn);

    struct K { const char* id; const char* name; juce::uint32 col; };
    const K ks[] = { { "shimmer", "Shimmer", 0xffbdf1ff }, { "warm", "Warm", 0xffffb86b }, { "air", "Air", 0xff6fd8ff },
                     { "reverb", "Reverb", 0xffa78bfa }, { "motion", "Motion", 0xff5cffb0 }, { "width", "Width", 0xff6fd8ff },
                     { "sub", "Sub oct.", 0xff3d7bff }, { "lowcut", "Low cut", 0xff9aa6bd }, { "attack", "Attack", 0xffffb3d9 },
                     { "xfade", "Fondu", 0xffffcf8a }, { "padvol", "Vol. Pad", 0xff6fd8ff }, { "crowdvol", "Vol. Crowd", 0xffffb86b } };
    for (auto& k : ks)
    {
        knobs.push_back (std::make_unique<Knob> (proc, k.id, U (k.name), juce::Colour (k.col)));
        addAndMakeVisible (*knobs.back());
    }

    static const char* codes[] = { "FR", "EN", "ES" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = langBtns[i];
        b.setButtonText (codes[i]);
        b.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b.getProperties().set ("fontSize", 11.5f);
        b.getProperties().set ("weight", 2);
        b.setTitle (i == 0 ? "Fran\xc3\xa7""ais" : i == 1 ? "English" : "Espa\xc3\xb1ol");
        b.onClick = [this, i] { setLang ((Lang) i); retranslate(); };
        addAndMakeVisible (b);
    }
    retranslate();

    juce::Random rnd (7);
    for (int i = 0; i < 70; ++i) stars.push_back ({ rnd.nextFloat() * W, rnd.nextFloat() * H });
    setSize (W, H);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    scan.reset();
    setLookAndFeel (nullptr);
}

void MainView::retranslate()
{
    for (int i = 0; i < 3; ++i) langBtns[i].setToggleState ((int) lang() == i, juce::dontSendNotification);
    stopAllBtn.setButtonText (L ("TOUT ARR\xc3\x8aTER", "STOP ALL", "DETENER TODO"));
    playBtn.setTitle (L ("Lecture", "Play", "Reproducir"));
    if (knobs.size() >= 12)
    {
        knobs[9]->setLabel (L ("Fondu", "Crossfade", "Fundido"));
        knobs[8]->setLabel (L ("Attack", "Attack", "Ataque"));
    }
    library.retranslate();
    repaint();
}

void MainView::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (L ("Choisissez le dossier de vos sons (pads ou crowds)", "Choose your sounds folder (pads or crowds)", "Elige la carpeta de tus sonidos (pads o crowds)"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.isDirectory()) openScan (f);
                          });
}

void MainView::openScan (const juce::File& folder)
{
    scan = std::make_unique<ScanOverlay> (proc, folder);
    scan->onClose = [this] (bool saved, bool crowdFirst)
    {
        juce::MessageManager::callAsync ([this, saved, crowdFirst]
        {
            scan.reset();
            if (saved) { library.refresh(); library.showTab (crowdFirst); }
        });
    };
    addAndMakeVisible (*scan);
    scan->setBounds (getLocalBounds());
}

void MainView::timerCallback()
{
    ring.phase += 0.09f;
    if (++langTick % 30 == 0 && reloadLangIfChanged()) retranslate();
    if (proc.libraries().changeCount() != lastChanges) { lastChanges = proc.libraries().changeCount(); library.refresh(); }
    if (proc.midiActivity()) midiFlashUntil = juce::Time::getMillisecondCounter() + 150;
    library.updateState();
    header.repaint();
    ring.repaint();
    wave.repaint();
    repaint (getLocalBounds().removeFromBottom (40));
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (12);
    header.setBounds (r.removeFromTop (84));
    auto footer = r.removeFromBottom (38);
    juce::ignoreUnused (footer);
    library.setBounds (r.removeFromLeft (370));
    auto fx = r.removeFromRight (330);
    // effets : 3 colonnes × 4 rangées
    auto fr = fx.reduced (18, 14);
    fr.removeFromTop (26);
    auto toggles = fr.removeFromBottom (52);
    freezeBtn.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (4, 0));
    swellBtn.setBounds (toggles.reduced (4, 0));
    fr.removeFromBottom (8);
    const int cw = fr.getWidth() / 3, ch = fr.getHeight() / 4;
    for (int i = 0; i < (int) knobs.size(); ++i)
        knobs[(size_t) i]->setBounds (fr.getX() + (i % 3) * cw, fr.getY() + (i / 3) * ch, cw, ch);

    // centre
    auto c = r.reduced (20, 14);
    auto card = c.removeFromBottom (150);
    ring.setBounds (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), c.getHeight()), juce::jmin (c.getWidth(), c.getHeight())));
    auto cr = card.reduced (16, 14);
    auto row = cr.removeFromBottom (44);
    playBtn.setBounds (row.removeFromLeft (52)); row.removeFromLeft (8);
    stopBtn.setBounds (row.removeFromLeft (52)); row.removeFromLeft (8);
    loopBtn.setBounds (row.removeFromLeft (110));
    stopAllBtn.setBounds (row.removeFromRight (150));
    cr.removeFromBottom (12);
    wave.setBounds (cr);
    if (scan) scan->setBounds (getLocalBounds());
    // langue : FR EN ES, en bas à droite (à gauche du numéro de version)
    auto lb = juce::Rectangle<int> (getWidth() - 12 - 40 - 90 - 3 * 40, getHeight() - 12 - 38 + 6, 40, 26);
    for (auto& b : langBtns) { b.setBounds (lb.reduced (2, 0)); lb.translate (40, 0); }
}

void MainView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff121a33), b.getCentreX(), b.getHeight() * 0.4f, juce::Colour (0xff03040a), b.getRight(), b.getBottom(), true);
    bg.addColour (0.5, juce::Colour (0xff070a14));
    g.setGradientFill (bg);
    g.fillAll();
    for (size_t i = 0; i < stars.size(); ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f + 0.35f * (float) ((i * 37) % 10) / 10.0f));
        g.fillEllipse (stars[i].x, stars[i].y, 1.4f, 1.4f);
    }
    // châssis
    auto ch = b.reduced (6);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6378), ch.getTopLeft(), juce::Colour (0xff3b4256), ch.getBottomRight(), false));
    g.fillRoundedRectangle (ch, 26.0f);
    panelBg (g, ch.reduced (2), 24.0f, juce::Colour (0xff121828), juce::Colour (0xff0b0f1a));

    // carte forme d'onde
    auto wc = wave.getBounds().getUnion (playBtn.getBounds()).getUnion (stopAllBtn.getBounds()).toFloat().expanded (16, 14);
    insetBox (g, wc, 16.0f);

    // panneau des effets
    auto fxr = juce::Rectangle<float> ((float) getWidth() - 12 - 330, 96.0f, 330.0f, (float) getHeight() - 12 - 38 - 96);
    g.setGradientFill (juce::ColourGradient (theme::panel2, fxr.getX(), 0, theme::panel, fxr.getRight(), 0, false));
    g.fillRect (fxr);
    g.setColour (theme::line);
    g.drawLine (fxr.getX(), fxr.getY(), fxr.getX(), fxr.getBottom());
    auto f = theme::ui (12.0f, 1); f.setExtraKerningFactor (0.25f);
    g.setFont (f);
    g.setColour (juce::Colour (0xff9aa6bd));
    g.drawText (L ("EFFETS", "EFFECTS", "EFECTOS"), fxr.reduced (18, 14).removeFromTop (20), juce::Justification::centredLeft);

    // pied
    auto ft = juce::Rectangle<float> (18, (float) getHeight() - 12 - 38, (float) getWidth() - 36, 38);
    g.setColour (theme::line);
    g.drawLine (ft.getX(), ft.getY(), ft.getRight(), ft.getY());
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    if (scan) return;
    auto ft = juce::Rectangle<float> (40, (float) getHeight() - 12 - 38, (float) getWidth() - 80, 38);
    const bool midi = juce::Time::getMillisecondCounter() < midiFlashUntil;
    auto dot = juce::Rectangle<float> (ft.getX(), ft.getCentreY() - 4, 8, 8);
    if (midi) glowEllipse (g, juce::DropShadow (theme::green, 8, {}), dot);
    g.setColour (midi ? theme::green : theme::muted.withAlpha (0.5f));
    g.fillEllipse (dot);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (L ("MIDI \xc2\xb7 une touche choisit la tonalit\xc3\xa9", "MIDI \xc2\xb7 a key picks the key", "MIDI \xc2\xb7 una tecla elige la tonalidad"), ft.withTrimmedLeft (16), juce::Justification::centredLeft);
    auto f = theme::title (11.5f); f.setExtraKerningFactor (0.3f);
    g.setFont (f);
    g.setColour (juce::Colour (0xffaab5ca));
    g.drawText (U ("SPACEPAD \xc2\xb7 BY JB PRAISE"), ft, juce::Justification::centred);
    g.setFont (theme::ui (12.0f));
    g.setColour (theme::muted);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), ft, juce::Justification::centredRight);
}
}
