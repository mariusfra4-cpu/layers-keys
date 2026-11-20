// Spacepad by JB Praise — curseur PAD ⇄ CROWD, mode scène, bouton d'icône et fenêtre de travail long.
#include "UIHelpers.h"

namespace spacepad
{
// ============================================================================================
// MixBar : équilibre entre le pad et les crowds
// ============================================================================================
MixBar::MixBar (SpacepadProcessor& p) : proc (p)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId, theme::amber);
    slider.setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "mix", slider);
    slider.onValueChange = [this] { repaint(); };
}

void MixBar::resized()
{
    slider.setBounds (getLocalBounds().reduced (62, 0).withSizeKeepingCentre (getWidth() - 124, 24));
}

void MixBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
    const float v = (float) slider.getValue();
    auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.14f);
    g.setFont (f);
    g.setColour (theme::accent.withAlpha (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, 2.0f * (1.0f - v))));
    g.drawText ("PAD", b.withWidth (60), juce::Justification::centred);
    g.setColour (theme::amber.withAlpha (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, 2.0f * v)));
    g.drawText ("CROWD", b.withTrimmedLeft (b.getWidth() - 62), juce::Justification::centred);
}

// ============================================================================================
// MasterBar : volume général
// ============================================================================================
MasterBar::MasterBar (SpacepadProcessor& p) : proc (p)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId, theme::green);
    slider.setDoubleClickReturnValue (true, 0.0);       // double-clic : retour à 0 dB
    slider.setTooltip (L ("Volume g\xc3\xa9n\xc3\xa9ral de Spacepad (double-clic : 0 dB)", "Spacepad master volume (double-click: 0 dB)", "Volumen general de Spacepad (doble clic: 0 dB)"));
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "master", slider);
    slider.onValueChange = [this] { repaint(); };
}

void MasterBar::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (18).withTrimmedLeft (2).withTrimmedRight (2).withHeight (26));
}

void MasterBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto f = theme::ui (10.5f, 2); f.setExtraKerningFactor (0.16f);
    g.setFont (f);
    g.setColour (theme::muted);
    g.drawText ("MASTER", b.withHeight (16).withTrimmedLeft (6), juce::Justification::centredLeft);
    auto* prm = proc.params.getParameter ("master");
    const float db = prm->convertFrom0to1 (prm->getValue());
    g.setColour (db > 0.05f ? theme::amber : db <= -59.9f ? theme::red : theme::text);
    g.setFont (theme::ui (12.0f, 1));
    g.drawText (prm->getText (prm->getValue(), 8), b.withHeight (16).withTrimmedRight (6), juce::Justification::centredRight);
}

// ============================================================================================
// IconButton
// ============================================================================================
void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    if (down) r = r.reduced (1);
    glassShape (g, r, r.getHeight() * 0.5f, over, theme::ink);
    g.setColour (theme::text.withAlpha (over ? 1.0f : 0.85f));
    auto ir = r.reduced (r.getWidth() * 0.3f);
    if (icon == "stage")
    {
        // quatre coins : « plein écran »
        const float l = ir.getWidth() * 0.34f;
        juce::Path p;
        auto corner = [&] (float x, float y, float dx, float dy)
        {
            p.startNewSubPath (x + dx * l, y);
            p.lineTo (x, y);
            p.lineTo (x, y + dy * l);
        };
        corner (ir.getX(), ir.getY(), 1, 1);
        corner (ir.getRight(), ir.getY(), -1, 1);
        corner (ir.getX(), ir.getBottom(), 1, -1);
        corner (ir.getRight(), ir.getBottom(), -1, -1);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ============================================================================================
// StageOverlay : mode scène
// ============================================================================================
StageOverlay::StageOverlay (SpacepadProcessor& p) : proc (p)
{
    for (auto* b : { &next, &mode, &fin, &stop })
    {
        b->getProperties().set ("fontSize", 22.0f);
        b->getProperties().set ("weight", 2);
        b->getProperties().set ("kerning", 0.08f);
        b->getProperties().set ("onDark", true);
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible (*b);
    }
    next.setColour (juce::TextButton::buttonOnColourId, theme::green);
    next.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff06210f));
    next.setToggleState (true, juce::dontSendNotification);
    next.setButtonText (L ("CHANT SUIVANT \xe2\x96\xb6", "NEXT SONG \xe2\x96\xb6", "SIGUIENTE \xe2\x96\xb6"));
    fin.setButtonText (L ("FIN DU CHANT", "END OF SONG", "FIN DE LA CANCI\xc3\x93N"));
    stop.setButtonText (L ("TOUT ARR\xc3\x8aTER", "STOP ALL", "DETENER TODO"));
    next.onClick = [this] { proc.nextSong(); };
    mode.onClick = [this] { proc.setMode (proc.currentMode() == 1 ? 0 : 1); };
    fin.onClick = [this] { proc.fadeOutAll(); };
    stop.onClick = [this] { proc.stopAll(); };
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

void StageOverlay::resized()
{
    auto r = getLocalBounds().reduced (28);
    quit = r.removeFromTop (64).removeFromRight (64);
    auto bottom = r.removeFromBottom (110);
    r.removeFromBottom (24);
    const int bw = (bottom.getWidth() - 3 * 16) / 4;
    for (auto* b : { &mode, &fin, &stop, &next })
    {
        b->setBounds (bottom.removeFromLeft (b == &next ? bottom.getWidth() : bw));
        bottom.removeFromLeft (16);
    }
    mode.setButtonText (proc.currentMode() == 1 ? L ("PASSER EN MAJEUR", "TO MAJOR", "A MAYOR") : L ("PASSER EN MINEUR", "TO MINOR", "A MENOR"));
    auto keysArea = r.removeFromBottom (juce::jmin (300, r.getHeight() / 2));
    r.removeFromBottom (16);
    orb = r;
    keys.clear();
    const int kw = (keysArea.getWidth() - 5 * 14) / 6, kh = (keysArea.getHeight() - 14) / 2;
    for (int k = 0; k < 12; ++k)
        keys.push_back (juce::Rectangle<int> (keysArea.getX() + (k % 6) * (kw + 14), keysArea.getY() + (k / 6) * (kh + 14), kw, kh));
}

void StageOverlay::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (quit.contains (p)) { if (onClose) onClose(); return; }
    for (int k = 0; k < (int) keys.size(); ++k)
        if (keys[(size_t) k].contains (p)) { proc.pressKey (k); repaint(); return; }
    if (orb.contains (p)) { if (proc.padActive()) proc.stopPad(); else proc.playPad(); }
}

void StageOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff04060c));
    const int key = proc.currentKey(), m = proc.currentMode();
    const bool on = proc.padActive();
    mode.setButtonText (m == 1 ? L ("PASSER EN MAJEUR", "TO MAJOR", "A MAYOR") : L ("PASSER EN MINEUR", "TO MINOR", "A MENOR"));

    // tonalité géante + chant en cours
    auto o = orb.toFloat();
    const float d = juce::jmin (o.getWidth(), o.getHeight());
    auto circle = o.withSizeKeepingCentre (d, d);
    if (on) softGlow (g, circle.getCentre(), d * 0.3f, d * 0.6f, theme::accent.withAlpha (0.28f));
    g.setColour (juce::Colour (0xff0d1322));
    g.fillEllipse (circle.reduced (d * 0.04f));
    g.setColour ((on ? theme::accent : juce::Colours::white).withAlpha (on ? 0.9f : 0.2f));
    g.drawEllipse (circle.reduced (d * 0.04f), 3.0f);
    g.setColour (juce::Colours::white);
    g.setFont (theme::title (d * 0.36f));
    g.drawText (keyText (key) + (m == 1 ? "m" : ""), circle.translated (0, -d * 0.06f), juce::Justification::centred);
    g.setFont (theme::ui (d * 0.07f, 1));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (on ? L ("touchez pour arr\xc3\xaater", "tap to stop", "toca para detener") : L ("touchez pour jouer", "tap to play", "toca para tocar"),
                circle.withTrimmedTop (d * 0.62f).withHeight (d * 0.12f), juce::Justification::centred);

    const int cur = proc.setlist().current();
    const auto list = proc.setlist().songs();
    if (cur >= 0 && cur < (int) list.size())
    {
        g.setColour (theme::green);
        g.setFont (theme::ui (28.0f, 2));
        g.drawText (juce::String (cur + 1) + U (" \xc2\xb7 ") + list[(size_t) cur].name, o.removeFromTop (40).withTrimmedRight (90), juce::Justification::centredLeft, true);
        if (cur + 1 < (int) list.size())
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (theme::ui (20.0f));
            g.drawText (L ("ensuite : ", "next: ", "despu\xc3\xa9s: ") + list[(size_t) cur + 1].name, o.removeFromTop (30), juce::Justification::centredLeft, true);
        }
    }

    // les 12 tonalités
    for (int k = 0; k < (int) keys.size(); ++k)
    {
        auto kr = keys[(size_t) k].toFloat();
        const bool sel = k == key;
        g.setColour (sel ? theme::accent.withAlpha (on ? 1.0f : 0.45f) : juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (kr, 24.0f);
        g.setColour (juce::Colours::white.withAlpha (sel ? 0.0f : 0.14f));
        g.drawRoundedRectangle (kr.reduced (0.5f), 24.0f, 1.0f);
        g.setColour (sel ? juce::Colour (0xff04121c) : juce::Colours::white);
        g.setFont (theme::title (juce::jmin (kr.getHeight() * 0.5f, 64.0f)));
        g.drawText (keyText (k) + (m == 1 ? "m" : ""), kr, juce::Justification::centred);
    }

    // quitter
    auto q = quit.toFloat().reduced (6);
    glassShape (g, q, q.getHeight() * 0.5f, false);
    g.setColour (juce::Colours::white);
    juce::Path x;
    auto xr = q.reduced (q.getWidth() * 0.33f);
    x.startNewSubPath (xr.getTopLeft()); x.lineTo (xr.getBottomRight());
    x.startNewSubPath (xr.getTopRight()); x.lineTo (xr.getBottomLeft());
    g.strokePath (x, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================================================================================
// TaskOverlay
// ============================================================================================
TaskOverlay::TaskOverlay (juce::String t, std::function<bool (ProgressFn)> w)
    : juce::Thread ("Spacepad travail"), title (std::move (t)), work (std::move (w))
{
    startThread();
    startTimerHz (20);
}

TaskOverlay::~TaskOverlay()
{
    stopTimer();
    signalThreadShouldExit();
    stopThread (20000);
}

void TaskOverlay::run()
{
    const bool ok = work ([this] (double pr) { progress = pr; return ! threadShouldExit(); });
    result = ok ? 1 : 0;
}

void TaskOverlay::timerCallback()
{
    repaint();
    if (result.load() < 0) return;
    stopTimer();
    if (auto cb = onDone) cb (result.load() == 1);
}

void TaskOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xd002040a));
    auto c = getLocalBounds().toFloat().withSizeKeepingCentre (560, 170);
    g.setColour (theme::light ? juce::Colours::white : theme::panel2.brighter (0.08f));
    g.fillRoundedRectangle (c, 26.0f);
    g.setColour (theme::ink.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 26.0f, 1.0f);
    auto r = c.reduced (30, 28);
    g.setColour (theme::text);
    g.setFont (theme::ui (19.0f, 2));
    g.drawText (title, r.removeFromTop (30), juce::Justification::centredLeft, true);
    g.setColour (theme::accent);
    g.setFont (theme::ui (13.0f, 1));
    g.drawText (juce::String (juce::roundToInt (progress.load() * 100)) + " %", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (14);
    auto bar = r.removeFromTop (12);
    g.setColour (theme::ink.withAlpha (0.08f));
    g.fillRoundedRectangle (bar, 6.0f);
    auto fill = bar.withWidth (juce::jmax (12.0f, bar.getWidth() * (float) progress.load()));
    g.setGradientFill (juce::ColourGradient (theme::accent, fill.getTopLeft(), theme::violet, fill.getTopRight(), false));
    g.fillRoundedRectangle (fill, 6.0f);
}
}
