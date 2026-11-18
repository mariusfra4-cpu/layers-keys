// Spacepad by JB Praise — interface : couleurs, boutons, boutons rotatifs, en-tête, roue dentée, listes.
#include "UIHelpers.h"
#include "BinaryData.h"

namespace spacepad
{
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
juce::Colour accentChoice (int i)
{
    static const juce::uint32 c[] = { 0xff6fd8ff, 0xffa78bfa, 0xffffcf7a, 0xff5cffb0, 0xffff8fb8, 0xffff8a5c, 0xffe8edf7 };
    return juce::Colour (c[(size_t) juce::jlimit (0, 6, i)]);
}
void apply (const UiSettings& s)
{
    accent = accentChoice (s.accent);
    accentLight = accent.interpolatedWith (juce::Colours::white, 0.55f);
    light = s.theme == 3;
    text = light ? juce::Colour (0xff1b2233) : juce::Colour (0xffe8edf7);
    muted = light ? juce::Colour (0xff5d6880) : juce::Colour (0xff8b97ad);
    line = light ? juce::Colour (0x1a000000) : juce::Colour (0x18ffffff);
    ink = light ? juce::Colours::black : juce::Colours::white;
    switch (s.theme)
    {
        case 1: // Minuit bleu
            bg = juce::Colour (0xff050a1c); panel = juce::Colour (0xff0b1430); panel2 = juce::Colour (0xff0d1838); lcd = juce::Colour (0xff040a1a);
            chassisTop = juce::Colour (0xff101c3c); chassisBottom = juce::Colour (0xff0a1228); headerTop = juce::Colour (0xff16244a); headerBottom = juce::Colour (0xff0f1a36);
            break;
        case 2: // Graphite
            bg = juce::Colour (0xff0b0b0d); panel = juce::Colour (0xff141416); panel2 = juce::Colour (0xff18181b); lcd = juce::Colour (0xff08080a);
            chassisTop = juce::Colour (0xff1d1d21); chassisBottom = juce::Colour (0xff111114); headerTop = juce::Colour (0xff232327); headerBottom = juce::Colour (0xff18181b);
            break;
        case 3: // Clair (la zone de l'espace et l'écran restent sombres)
            bg = juce::Colour (0xffe9edf4); panel = juce::Colour (0xfff7f8fb); panel2 = juce::Colour (0xffeef1f6); lcd = juce::Colour (0xff0d1322);
            chassisTop = juce::Colour (0xfffbfcfe); chassisBottom = juce::Colour (0xffe8ecf3); headerTop = juce::Colour (0xffffffff); headerBottom = juce::Colour (0xfff1f4f8);
            break;
        default: // Nuit
            bg = juce::Colour (0xff070a14); panel = juce::Colour (0xff0d1220); panel2 = juce::Colour (0xff0f1524); lcd = juce::Colour (0xff050912);
            chassisTop = juce::Colour (0xff121828); chassisBottom = juce::Colour (0xff0b0f1a); headerTop = juce::Colour (0xff1a2135); headerBottom = juce::Colour (0xff111727);
            break;
    }
}
}

juce::String keyText (int key)
{
    static const char* letters[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (key < 0 || key > 11) return "-";
    return uiSettings().solfegeNames ? solfege (key) : juce::String (letters[key]);
}

// ============================================================================================
// LookAndFeel : boutons arrondis en verre dépoli (style iPhone)
// ============================================================================================
SpaceLook::SpaceLook() { refreshColours(); }

void SpaceLook::refreshColours()
{
    setColour (juce::PopupMenu::backgroundColourId, theme::panel2.brighter (0.1f));
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, theme::panel2);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::line);
    setColour (juce::ComboBox::arrowColourId, theme::accent);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff04121c));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e2638));
    setColour (juce::TextButton::buttonOnColourId, theme::accent);
    setColour (juce::ScrollBar::thumbColourId, theme::accent.withAlpha (0.35f));
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::DocumentWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::backgroundColourId, theme::panel2);
    setColour (juce::TooltipWindow::textColourId, theme::text);
}

void SpaceLook::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (theme::panel2.brighter (0.15f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (theme::text);
    g.setFont (theme::ui (13.0f, 1));
    g.drawText (text, r, juce::Justification::centred);
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
    g.setColour (theme::ink.withAlpha (0.08f));
    g.strokePath (bgArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (pos > 0.001f)
    {
        valArc.addCentredArc (c.x, c.y, ringR, ringR, 0, a0, ang, true);
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (valArc, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (valArc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float kr = d * 0.5f - 12;
    softGlow (g, c.translated (0, 5), kr, kr + 10, juce::Colours::black.withAlpha (0.55f));
    auto body = juce::Rectangle<float> (kr * 2, kr * 2).withCentre (c);
    juce::ColourGradient grad (juce::Colour (0xff4a5570), c.x - kr * 0.4f, c.y - kr * 0.5f, juce::Colour (0xff0c0f18), c.x + kr, c.y + kr, true);
    grad.addColour (0.55, juce::Colour (0xff1a2032));
    g.setGradientFill (grad);
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -kr + 4, 3.0f, kr * 0.42f, 1.5f);
    ptr.applyTransform (juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    g.setColour (juce::Colours::white);
    g.fillPath (ptr);
}

void SpaceLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = r.withSizeKeepingCentre (r.getWidth() - 4, 6.0f);
    g.setColour (theme::ink.withAlpha (0.10f));
    g.fillRoundedRectangle (track, 3.0f);
    const auto col = s.findColour (juce::Slider::trackColourId);
    auto fill = track.withRight (juce::jlimit (track.getX(), track.getRight(), pos));
    g.setGradientFill (juce::ColourGradient (col, fill.getTopLeft(), col.interpolatedWith (theme::violet, 0.6f), fill.getTopRight(), false));
    g.fillRoundedRectangle (fill, 3.0f);
    auto thumb = juce::Rectangle<float> (20, 20).withCentre ({ juce::jlimit (track.getX() + 10, track.getRight() - 10, pos), track.getCentreY() });
    softGlow (g, thumb.getCentre().translated (0, 2), 8, 14, juce::Colours::black.withAlpha (0.4f));
    g.setColour (juce::Colours::white);
    g.fillEllipse (thumb);
}

void SpaceLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    if (down) r = r.reduced (1.0f);                       // petit enfoncement à l'appui
    const float rad = r.getHeight() * 0.5f;               // pastille / rond : jamais de coins carrés
    const bool on = b.getToggleState();
    const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
    if (b.getProperties()["dashed"])
    {
        g.setColour (onCol.withAlpha (over ? 0.16f : 0.08f));
        g.fillRoundedRectangle (r, rad);
        juce::Path p; p.addRoundedRectangle (r, rad);
        juce::Path d; const float dl[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dl, 2);
        g.setColour (onCol.withAlpha (0.7f));
        g.fillPath (d);
        return;
    }
    if (b.getProperties()["flat"] && ! on)
    {
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (r, rad); }
        return;
    }
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (onCol.brighter (0.35f), r.getTopLeft(), onCol.darker (0.2f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (r.getX() + rad, r.getY() + 1.5f, r.getRight() - rad, r.getY() + 1.5f, 1.0f);
        return;
    }
    const bool onDark = b.getProperties()["onDark"];
    glassShape (g, r, rad, over, onDark ? juce::Colours::white : theme::ink);
}

void SpaceLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto col = on ? b.findColour (juce::TextButton::textColourOnId) : b.findColour (juce::TextButton::textColourOffId);
    if (b.getProperties()["dashed"]) col = b.findColour (juce::TextButton::buttonOnColourId).interpolatedWith (theme::light ? juce::Colours::black : juce::Colours::white, theme::light ? 0.35f : 0.45f);
    if (! b.isEnabled()) col = col.withAlpha (0.4f);
    auto f = getTextButtonFont (b, b.getHeight());
    if (b.getProperties().contains ("kerning")) f.setExtraKerningFactor ((float) b.getProperties()["kerning"]);
    g.setFont (f);
    const juce::String icon = b.getProperties()["icon"];
    auto c = b.getLocalBounds().toFloat().getCentre();
    if (icon == "play" || icon == "stop")
    {
        const float s = juce::jmin (20.0f, b.getHeight() * 0.36f);
        auto ir = juce::Rectangle<float> (s, s).withCentre (c);
        juce::Path p;
        if (icon == "play") { p.addTriangle (ir.getX() + s * 0.18f, ir.getY(), ir.getRight(), ir.getCentreY(), ir.getX() + s * 0.18f, ir.getBottom()); g.setColour (on ? col : theme::green); }
        else { p.addRoundedRectangle (ir.reduced (s * 0.08f), s * 0.18f); g.setColour (col); }
        g.fillPath (p);
        return;
    }
    g.setColour (col);
    auto area = b.getLocalBounds().reduced (10, 2);
    if (icon == "folder")
    {
        const float s = 16.0f;
        const float tw = juce::GlyphArrangement::getStringWidth (f, b.getButtonText());
        const float ix = c.x - (tw + s + 8) / 2;
        auto ir = juce::Rectangle<float> (ix, c.y - s / 2, s, s);
        juce::Path p;
        p.startNewSubPath (ir.getX(), ir.getY() + 3); p.lineTo (ir.getX() + 6, ir.getY() + 3); p.lineTo (ir.getX() + 8, ir.getY() + 5);
        p.lineTo (ir.getRight(), ir.getY() + 5); p.lineTo (ir.getRight(), ir.getBottom() - 1); p.lineTo (ir.getX(), ir.getBottom() - 1); p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawText (b.getButtonText(), juce::Rectangle<float> (ix + s + 8, (float) area.getY(), tw + 6, (float) area.getHeight()), juce::Justification::centredLeft, false);
        return;
    }
    g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1, 0.8f);
}

juce::Font SpaceLook::getTextButtonFont (juce::TextButton& b, int h)
{
    const float size = b.getProperties().contains ("fontSize") ? (float) b.getProperties()["fontSize"] : juce::jmin (15.0f, h * 0.36f);
    return theme::ui (size, b.getProperties().contains ("weight") ? (int) b.getProperties()["weight"] : 1);
}

void SpaceLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    glassShape (g, r, r.getHeight() * 0.5f, false, theme::ink);
    juce::Path a;
    const float cx = (float) w - 18, cy = h * 0.5f;
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
    auto* prm = p.params.getParameter (paramId);
    slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
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
    g.drawText (valueText ? valueText() : prm->getText (prm->getValue(), 16), r.removeFromTop (15), juce::Justification::centred);
}

// ============================================================================================
// Header
// ============================================================================================
void Header::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (theme::headerTop, 0, 0, theme::headerBottom, 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (theme::line);
    g.drawLine (0, r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f, 1.0f);

    const float px = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! logoImg.isValid() || std::abs (logoScale - px) > 0.01f)
    {
        logoScale = px;
        logoImg = makeCacheImage (juce::roundToInt (290 * px), juce::roundToInt (56 * px));
        juce::Graphics lg (logoImg);
        lg.addTransform (juce::AffineTransform::scale (px));
        glowText (lg, "SPACEPAD", theme::title (25.0f), { 14, 12, 260, 32 }, juce::Justification::centredLeft, theme::light ? theme::text : juce::Colour (0xfff2f6ff), theme::accent.withAlpha (theme::light ? 0.25f : 0.45f), (int) (14 * px), 0.22f);
    }
    g.setOpacity (1.0f);
    g.drawImage (logoImg, juce::Rectangle<float> (14, 6, 290, 56));
    auto by = theme::ui (11.5f, 1); by.setExtraKerningFactor (0.28f);
    g.setFont (by);
    g.setColour (theme::muted);
    g.drawText ("BY JB PRAISE", juce::Rectangle<float> (30, 52, 240, 16), juce::Justification::centredLeft);

    // écran
    auto lcd = juce::Rectangle<float> (300, 16, r.getWidth() - 300 - 270, 52);
    insetBox (g, lcd, 14.0f, theme::lcd);
    const bool sounding = proc.padSounding() || proc.crowdSounding();
    auto dot = juce::Rectangle<float> (lcd.getX() + 20, lcd.getCentreY() - 5, 10, 10);
    if (sounding) softGlow (g, dot.getCentre(), 4, 13, theme::green.withAlpha (0.6f));
    g.setColour (sounding ? theme::green : theme::muted.withAlpha (0.5f));
    g.fillEllipse (dot);

    const int mode = proc.currentMode();
    auto lib = proc.libraries().find (proc.padLibraryId());
    juce::String libName = lib ? lib->name.toUpperCase()
                               : (mode == 1 ? L ("AUCUNE LIBRAIRIE DE PADS MINEURS", "NO MINOR PAD LIBRARY", "NINGUNA LIBRER\xc3\x8d""A DE PADS MENORES")
                                            : L ("AUCUNE LIBRAIRIE DE PADS MAJEURS", "NO MAJOR PAD LIBRARY", "NINGUNA LIBRER\xc3\x8d""A DE PADS MAYORES"));
    juce::String sub = U ("PAD \xc2\xb7 ") + keyText (proc.currentKey()).toUpperCase() + " " + modeWord (mode).toUpperCase();
    if (proc.padActive() && proc.padSoundName().isNotEmpty() && lib) sub << U (" \xc2\xb7 ") << proc.padSoundName();
    if (! lib) sub = L ("Choisissez-en une dans la Pad Library", "Pick one in the Pad Library", "Elige una en la Pad Library");
    auto tr = lcd.reduced (44, 0).withTrimmedRight (110);
    auto f1 = theme::ui (17.0f, 1);
    const float w1 = juce::jmin (tr.getWidth() * 0.6f, juce::GlyphArrangement::getStringWidth (f1, libName) + 4);
    auto nameArea = tr.removeFromLeft (w1);
    const auto nameCol = lib ? theme::accentLight : theme::amber;
    const juce::String key = libName + juce::String ((int) nameCol.getARGB());
    if (key != nameText || ! nameImg.isValid() || std::abs (nameScale - px) > 0.01f || nameW != juce::roundToInt (nameArea.getWidth()))
    {
        nameText = key; nameScale = px; nameW = juce::roundToInt (nameArea.getWidth());
        nameImg = makeCacheImage (juce::roundToInt ((nameArea.getWidth() + 20) * px), juce::roundToInt ((nameArea.getHeight() + 20) * px));
        juce::Graphics ng (nameImg);
        ng.addTransform (juce::AffineTransform::scale (px));
        glowText (ng, libName, f1, { 10, 10, nameArea.getWidth(), nameArea.getHeight() }, juce::Justification::centredLeft, nameCol, nameCol.withAlpha (0.45f), (int) (8 * px));
    }
    g.setOpacity (1.0f);
    g.drawImage (nameImg, nameArea.expanded (10));
    g.setColour (lib ? juce::Colour (0xff7f8ca6) : theme::amber.withAlpha (0.8f));
    g.setFont (theme::ui (13.0f));
    g.drawText (sub, tr.withTrimmedLeft (16), juce::Justification::centredLeft, true);
    const bool loop = proc.loopOn();
    g.setColour (loop ? juce::Colour (0xffffcf8a) : theme::muted);
    g.setFont (theme::ui (13.0f, 1));
    g.drawText (loop ? "LOOP ON" : "LOOP OFF", lcd.withTrimmedRight (20).removeFromRight (100), juce::Justification::centredRight);

    // niveau de sortie
    const float db = juce::Decibels::gainToDecibels (proc.outputLevel(), -60.0f);
    const int lit = juce::jlimit (0, 14, (int) std::round ((db + 48.0f) / 48.0f * 14.0f));
    for (int i = 0; i < 14; ++i)
    {
        const float h = 12 + i * 2.2f;
        auto br = juce::Rectangle<float> (r.getRight() - 246 + i * 7.5f, 64 - h, 5, h);
        auto col = i < 9 ? theme::green : i < 12 ? juce::Colour (0xffffcf5c) : theme::red;
        g.setColour (i < lit ? col : theme::ink.withAlpha (0.08f));
        g.fillRoundedRectangle (br, 2.5f);
    }
}

// ============================================================================================
// Roue dentée (Paramètres)
// ============================================================================================
void GearButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    if (down) r = r.reduced (1);
    glassShape (g, r, r.getHeight() * 0.5f, over, theme::ink);
    const auto c = r.getCentre();
    const float R = r.getWidth() * 0.27f;
    juce::Path p;
    for (int i = 0; i < 8; ++i)
    {
        const float a = i * juce::MathConstants<float>::twoPi / 8;
        juce::Path tooth;
        tooth.addRoundedRectangle (-R * 0.22f, -R * 1.38f, R * 0.44f, R * 0.6f, R * 0.12f);
        tooth.applyTransform (juce::AffineTransform::rotation (a).translated (c.x, c.y));
        p.addPath (tooth);
    }
    p.addEllipse (c.x - R, c.y - R, R * 2, R * 2);
    g.setColour (over ? theme::accent : theme::text.withAlpha (0.9f));
    g.fillPath (p);
    g.setColour (theme::light ? juce::Colours::white : theme::headerBottom);
    g.fillEllipse (c.x - R * 0.45f, c.y - R * 0.45f, R * 0.9f, R * 0.9f);
}

// ============================================================================================
// RowList
// ============================================================================================
void RowList::setRows (std::vector<Row> r)
{
    rows = std::move (r);
    setSize (getWidth(), juce::jmax (contentHeight(), getParentHeight()));
    repaint();
}

int RowList::contentHeight() const
{
    int h = 0;
    for (auto& r : rows) h += r.header ? 34 : 60;
    return h;
}

int RowList::rowAt (int y) const
{
    int top = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const int h = rows[(size_t) i].header ? 34 : 60;
        if (y >= top && y < top + h) return rows[(size_t) i].header ? -1 : i;
        top += h;
    }
    return -1;
}

void RowList::mouseMove (const juce::MouseEvent& e)
{
    const int h = rowAt (e.y);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void RowList::mouseUp (const juce::MouseEvent& e)
{
    const int i = rowAt (e.y);
    if (i < 0 || e.mouseWasDraggedSinceMouseDown()) return;
    if (rows[(size_t) i].star != 0 && ! e.mods.isPopupMenu() && e.x > getWidth() - 44)
    {
        auto fnStar = rows[(size_t) i].onStar;
        if (fnStar) fnStar();
        return;
    }
    if (rows[(size_t) i].chips && ! e.mods.isPopupMenu() && e.x > getWidth() - 70)
    {
        auto fnChip = e.x < getWidth() - 36 ? rows[(size_t) i].onA : rows[(size_t) i].onB;
        if (fnChip) fnChip();
        return;
    }
    // copie : l'action peut reconstruire la liste (et donc effacer cette ligne) pendant qu'elle s'exécute
    auto fn = e.mods.isPopupMenu() ? rows[(size_t) i].onMenu : rows[(size_t) i].onClick;
    if (fn) fn();
}

void RowList::paint (juce::Graphics& g)
{
    if (rows.empty())
    {
        g.setColour (theme::muted);
        g.setFont (theme::ui (14.0f));
        g.drawFittedText (emptyText, getLocalBounds().reduced (10, 20), juce::Justification::centredTop, 10);
        return;
    }
    int top = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& row = rows[(size_t) i];
        if (row.header)
        {
            auto hr = juce::Rectangle<float> (2, (float) top + 8, (float) getWidth() - 4, 24);
            auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.16f);
            g.setFont (f);
            g.setColour (row.colour);
            const float tw = juce::GlyphArrangement::getStringWidth (f, row.name);
            g.drawText (row.name, hr, juce::Justification::centredLeft);
            auto pill = juce::Rectangle<float> (hr.getX() + tw + 10, hr.getCentreY() - 9, 26, 18);
            g.setColour (row.colour.withAlpha (0.16f));
            g.fillRoundedRectangle (pill, 9);
            g.setColour (row.colour);
            g.setFont (theme::ui (10.5f, 2));
            g.drawText (row.tag, pill, juce::Justification::centred);
            top += 34;
            continue;
        }
        auto r = juce::Rectangle<float> (0, (float) top, (float) getWidth(), 60).reduced (0, 3);
        const auto col = row.colour;
        if (row.on)
        {
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.20f), r.getX(), 0, theme::ink.withAlpha (0.02f), r.getRight(), 0, false));
            g.fillRoundedRectangle (r, 18.0f);
            g.setColour (col.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 1.0f);
        }
        else
        {
            g.setColour (theme::ink.withAlpha (hover == i ? 0.07f : 0.035f));
            g.fillRoundedRectangle (r, 18.0f);
        }
        if (row.badge.isNotEmpty())
        {
            auto bd = juce::Rectangle<float> (r.getX() + 10, r.getCentreY() - 13, 26, 26);
            g.setColour (row.on ? col : theme::ink.withAlpha (0.08f));
            g.fillEllipse (bd);
            g.setColour (row.on ? juce::Colour (0xff06210f) : theme::text.withAlpha (0.8f));
            g.setFont (theme::ui (12.0f, 2));
            g.drawText (row.badge, bd, juce::Justification::centred);
        }
        else
        {
            auto dot = juce::Rectangle<float> (r.getX() + 16, r.getCentreY() - 5, 10, 10);
            if (row.on || row.playing) softGlow (g, dot.getCentre(), 4, 12, col.withAlpha (0.6f));
            g.setColour (row.on || row.playing ? col : col.withMultipliedSaturation (0.5f).withMultipliedBrightness (0.7f));
            g.fillEllipse (dot);
        }
        const float extra = (row.star != 0 ? 30.0f : 0.0f) + (row.fx ? 34.0f : 0.0f);
        auto tr = r.withTrimmedLeft (row.badge.isNotEmpty() ? 46 : 38).withTrimmedRight ((row.chips ? 74 : 66) + extra);
        g.setColour (theme::text);
        g.setFont (theme::ui (14.0f, 1));
        g.drawText (row.name, tr.withTrimmedBottom (r.getHeight() * 0.44f), juce::Justification::bottomLeft, true);
        g.setColour (theme::muted);
        g.setFont (theme::ui (11.5f));
        g.drawText (row.sub, tr.withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::topLeft, true);
        if (row.chips)
        {
            // pastilles A / B : choisir les deux pads joués ensemble
            for (int k = 0; k < 2; ++k)
            {
                const bool on = k == 0 ? row.aOn : row.bOn;
                auto chip = juce::Rectangle<float> (r.getRight() - 64 + k * 30, r.getCentreY() - 13, 26, 26);
                const auto cc = k == 0 ? theme::accent : theme::violet;
                g.setColour (on ? cc : theme::ink.withAlpha (0.08f));
                g.fillEllipse (chip);
                g.setColour (on ? juce::Colour (0xff04121c) : theme::muted);
                g.setFont (theme::ui (11.5f, 2));
                g.drawText (k == 0 ? "A" : "B", chip, juce::Justification::centred);
            }
        }
        else
        {
            auto right = r.withTrimmedRight (14);
            if (row.star != 0)
            {
                // étoile des favoris
                auto sr = right.removeFromRight (24);
                drawStar (g, sr.getCentre(), 8.0f, row.star == 2, row.star == 2 ? juce::Colour (0xffffc94a) : theme::muted.withAlpha (0.6f));
                right.removeFromRight (6);
            }
            const bool tagIsSlot = row.tag.length() == 1 && row.tag.containsOnly ("12");
            if (tagIsSlot)
            {
                // numéro du crowd qui joue ce son
                auto pill = right.removeFromRight (24).withSizeKeepingCentre (22, 22);
                g.setColour (col);
                g.fillEllipse (pill);
                g.setColour (juce::Colour (0xff2a1300));
                g.setFont (theme::ui (11.5f, 2));
                g.drawText (row.tag, pill, juce::Justification::centred);
                right.removeFromRight (6);
            }
            if (row.fx)
            {
                auto fb = right.removeFromRight (30).withSizeKeepingCentre (30, 20);
                g.setColour (theme::ink.withAlpha (0.08f));
                g.fillRoundedRectangle (fb, 8.0f);
                g.setColour (theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a));
                g.setFont (theme::ui (10.0f, 2));
                g.drawText ("FX", fb, juce::Justification::centred);
                right.removeFromRight (6);
            }
            if (! tagIsSlot)
            {
                g.setColour (theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a));
                g.setFont (theme::ui (11.5f, 1));
                g.drawText (row.tag, right.removeFromRight (60), juce::Justification::centredRight);
            }
        }
        top += 60;
    }
}
}
