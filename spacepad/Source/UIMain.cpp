// Spacepad by JB Praise — la fenêtre principale : assemble l'en-tête, la bibliothèque, l'espace, l'anneau, le crochet,
// le visualiseur, la lecture, les effets et les fenêtres (analyse, Paramètres). Anime le tout 30 fois par seconde.
#include "UIHelpers.h"

namespace spacepad
{
MainView::MainView (SpacepadProcessor& p) : proc (p)
{
    theme::apply (uiSettings());
    look.refreshColours();
    setLookAndFeel (&look);
    addAndMakeVisible (header);
    addAndMakeVisible (gear);
    addAndMakeVisible (library);
    addAndMakeVisible (space);
    addAndMakeVisible (ring);
    addAndMakeVisible (hook);
    addAndMakeVisible (viz);
    library.onChooseFolder = [this] (bool crowdTab) { chooseFolder (crowdTab); };
    gear.onClick = [this] { openSettings(); };
    hook.onClick = [this] { proc.setMode (proc.currentMode() == 1 ? 0 : 1); };

    playBtn.getProperties().set ("icon", "play");
    stopBtn.getProperties().set ("icon", "stop");
    playBtn.onClick = [this] { proc.playPad(); };
    stopBtn.onClick = [this] { proc.stopPad(); };
    stopAllBtn.getProperties().set ("fontSize", 12.5f);
    stopAllBtn.getProperties().set ("kerning", 0.1f);
    stopAllBtn.onClick = [this] { proc.stopAll(); };
    for (auto* b : { &loopBtn, &freezeBtn, &swellBtn })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("weight", 2);
        b->getProperties().set ("kerning", 0.18f);
    }
    loopBtn.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    loopBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    swellBtn.setColour (juce::TextButton::buttonOnColourId, theme::violet);
    swellBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff12082e));
    for (auto* b : { &playBtn, &stopBtn, &loopBtn, &stopAllBtn, &freezeBtn, &swellBtn }) addAndMakeVisible (*b);
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "loop", loopBtn);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "freeze", freezeBtn);
    swellAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "swell", swellBtn);

    struct K { const char* id; const char* name; juce::uint32 col; bool accent; };
    const K ks[] = { { "shimmer", "Shimmer", 0xffbdf1ff, false }, { "warm", "Warm", 0xffffb86b, false }, { "air", "Air", 0, true },
                     { "reverb", "Reverb", 0xffa78bfa, false }, { "motion", "Motion", 0xff5cffb0, false }, { "width", "Width", 0, true },
                     { "sub", "Sub oct.", 0xff3d7bff, false }, { "lowcut", "Low cut", 0xff9aa6bd, false }, { "attack", "Attack", 0xffffb3d9, false },
                     { "xfade", "Fondu", 0xffffcf8a, false }, { "padvol", "Vol. Pad", 0, true }, { "crowdvol", "Vol. Crowd", 0xffffb86b, false } };
    for (auto& k : ks)
    {
        knobs.push_back (std::make_unique<Knob> (proc, k.id, U (k.name), k.accent ? theme::accent : juce::Colour (k.col)));
        knobs.back()->usesAccent = k.accent;
        addAndMakeVisible (*knobs.back());
    }
    lastScale = uiSettings().scale;
    lastSettings = settingsVersion();
    retranslate();
    setSize (W, H);
    lastMs = juce::Time::getMillisecondCounter();
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    if (screenSaverOff) juce::Desktop::setScreenSaverEnabled (true);
    scan.reset();
    settings.reset();
    setLookAndFeel (nullptr);
}

void MainView::retranslate()
{
    stopAllBtn.setButtonText (L ("TOUT ARR\xc3\x8aTER", "STOP ALL", "DETENER TODO"));
    playBtn.setTooltip (L ("Jouer le pad", "Play the pad", "Tocar el pad"));
    stopBtn.setTooltip (L ("Arr\xc3\xaater le pad", "Stop the pad", "Detener el pad"));
    gear.setTooltip (L ("Personnaliser Spacepad", "Customize Spacepad", "Personalizar Spacepad"));
    hook.setTooltip (proc.currentMode() == 1 ? L ("Passer en majeur", "Switch to major", "Pasar a mayor") : L ("Passer en mineur", "Switch to minor", "Pasar a menor"));
    if (knobs.size() >= 12)
    {
        knobs[9]->setLabel (L ("Fondu", "Crossfade", "Fundido"));
        knobs[8]->setLabel (L ("Attack", "Attack", "Ataque"));
    }
    library.retranslate();
    repaint();
}

void MainView::applySettings()
{
    const auto st = uiSettings();
    theme::apply (st);
    look.refreshColours();
    for (auto& k : knobs) if (k->usesAccent) k->setColour (theme::accent);
    header.invalidate();
    space.resized();
    library.showTab (library.crowdTabShown());
    viz.setVisible (st.viz != 3);
    if (st.scale != lastScale) { lastScale = st.scale; if (onScaleChange) onScaleChange (st.scale); }
    lastSettings = settingsVersion();
    retranslate();
    repaint();
}

void MainView::chooseFolder (bool crowdTab)
{
    chooser = std::make_unique<juce::FileChooser> (crowdTab ? L ("Choisissez le dossier de vos crowds", "Choose your crowds folder", "Elige la carpeta de tus crowds")
                                                            : L ("Choisissez le dossier de vos pads", "Choose your pads folder", "Elige la carpeta de tus pads"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, crowdTab] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.isDirectory()) openScan (f, crowdTab ? LibType::Crowd : LibType::Pad);
                          });
}

void MainView::openScan (const juce::File& folder, LibType type)
{
    scan = std::make_unique<ScanOverlay> (proc, folder, type);
    scan->onClose = [this, type] (bool saved)
    {
        juce::MessageManager::callAsync ([this, saved, type]
        {
            scan.reset();
            if (saved) { library.refresh(); library.showTab (type == LibType::Crowd); }
        });
    };
    addAndMakeVisible (*scan);
    scan->setBounds (getLocalBounds());
}

void MainView::openSettings()
{
    settings = std::make_unique<SettingsOverlay> ([this] { applySettings(); });
    settings->onClose = [this] { juce::MessageManager::callAsync ([this] { settings.reset(); repaint(); }); };
    addAndMakeVisible (*settings);
    settings->setBounds (getLocalBounds());
}

void MainView::step (float dt)
{
    const auto st = uiSettings();
    anim.t += dt;
    const float target = proc.padActive() ? 1.0f : 0.0f;
    anim.env += (target - anim.env) * (1.0f - std::exp (-dt / (target > anim.env ? 1.3f : 1.8f)));
    // un petit élan à chaque changement de tonalité ou de mode
    if (proc.currentKey() != lastKey || proc.currentMode() != lastMode)
    {
        if (lastKey >= 0 && proc.padActive()) anim.surge = 1.0f;
        lastKey = proc.currentKey();
        lastMode = proc.currentMode();
        hook.setTooltip (lastMode == 1 ? L ("Passer en majeur", "Switch to major", "Pasar a mayor") : L ("Passer en mineur", "Switch to minor", "Pasar a menor"));
        library.updateState();
    }
    anim.surge *= std::exp (-dt / 0.9f);
    if (st.animations)
    {
        const float spin = (0.25f + 1.0f * anim.env + 2.2f * anim.surge) * (0.3f + 1.4f * st.speed);
        anim.phi -= dt * 0.075f * spin;
        anim.orb -= dt * 0.22f * spin;
    }
    space.tick (dt);
    hook.tick (dt);
    if (st.viz != 3) viz.tick (dt);
}

void MainView::animateForTests (float seconds)
{
    for (float t = 0; t < seconds; t += 1.0f / 30.0f) step (1.0f / 30.0f);
}

void MainView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = juce::jlimit (0.001f, 0.1f, (float) (now - lastMs) / 1000.0f);
    lastMs = now;
    step (dt);
    if (++tick % 30 == 0 && reloadLangIfChanged()) applySettings();        // changé dans une autre fenêtre (plugin / application)
    if (settingsVersion() != lastSettings) applySettings();
    if (proc.libraries().changeCount() != lastChanges) { lastChanges = proc.libraries().changeCount(); library.refresh(); }
    if (proc.midiActivity()) midiFlashUntil = now + 150;
    library.updateState();
    // garder l'écran allumé pendant le jeu (réglage)
    const bool wantAwake = uiSettings().keepAwake && proc.padActive();
    if (wantAwake != screenSaverOff) { screenSaverOff = wantAwake; juce::Desktop::setScreenSaverEnabled (! wantAwake); }
    if (scan == nullptr && settings == nullptr)
    {
        if (uiSettings().animations || anim.env > 0.01f || anim.surge > 0.01f) space.repaint();
        ring.repaint();
        if (tick % 2 == 0) header.repaint();
        repaint (getLocalBounds().removeFromBottom (40));
    }
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (84);
    header.setBounds (top);
    gear.setBounds (top.getRight() - 64, top.getY() + 18, 48, 48);
    r.removeFromBottom (38);
    library.setBounds (r.removeFromLeft (370));
    auto fx = r.removeFromRight (330);
    auto fr = fx.reduced (18, 14);
    fr.removeFromTop (26);
    auto toggles = fr.removeFromBottom (50);
    freezeBtn.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (5, 0));
    swellBtn.setBounds (toggles.reduced (5, 0));
    fr.removeFromBottom (8);
    const int cw = fr.getWidth() / 3, ch = fr.getHeight() / 4;
    for (int i = 0; i < (int) knobs.size(); ++i)
        knobs[(size_t) i]->setBounds (fr.getX() + (i % 3) * cw, fr.getY() + (i / 3) * ch, cw, ch);

    // centre : l'espace couvre toute la zone, l'anneau flotte dessus
    space.setBounds (r);
    auto c = r.reduced (20, 10);
    auto bottom = c.removeFromBottom (170);
    const int d = juce::jmin (c.getWidth(), c.getHeight());
    ring.setBounds (c.withSizeKeepingCentre (d, d));
    const float coreR = ring.coreRadius();
    const auto rc = ring.getBounds().toFloat().getCentre();
    space.centre = rc - r.getPosition().toFloat();
    space.coreRadius = coreR;
    const auto hp = rc + juce::Point<float> (std::cos (juce::MathConstants<float>::pi / 4), std::sin (juce::MathConstants<float>::pi / 4)) * coreR * 1.02f;
    hook.setBounds (juce::Rectangle<int> (58, 58).withCentre (hp.toInt()));
    auto row = bottom.removeFromBottom (60);
    viz.setBounds (bottom.withSizeKeepingCentre (380, 80).translated (0, -4));
    auto tr = row.withSizeKeepingCentre (56 + 12 + 56 + 14 + 128 + 14 + 180, 56);
    playBtn.setBounds (tr.removeFromLeft (56)); tr.removeFromLeft (12);
    stopBtn.setBounds (tr.removeFromLeft (56)); tr.removeFromLeft (14);
    loopBtn.setBounds (tr.removeFromLeft (128).withSizeKeepingCentre (128, 48)); tr.removeFromLeft (14);
    stopAllBtn.setBounds (tr.withSizeKeepingCentre (180, 48));
    if (scan) scan->setBounds (getLocalBounds());
    if (settings) settings->setBounds (getLocalBounds());
}

void MainView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (theme::bg);
    // châssis
    auto ch = b.reduced (6);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6378), ch.getTopLeft(), juce::Colour (0xff3b4256), ch.getBottomRight(), false));
    g.fillRoundedRectangle (ch, 26.0f);
    g.setGradientFill (juce::ColourGradient (theme::chassisTop, ch.getX(), ch.getY(), theme::chassisBottom, ch.getX(), ch.getBottom(), false));
    g.fillRoundedRectangle (ch.reduced (2), 24.0f);

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
    auto ft = juce::Rectangle<float> (18, (float) getHeight() - 12 - 38, (float) getWidth() - 36, 38);
    g.setColour (theme::line);
    g.drawLine (ft.getX(), ft.getY(), ft.getRight(), ft.getY());
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    if (scan || settings) return;
    auto ft = juce::Rectangle<float> (40, (float) getHeight() - 12 - 38, (float) getWidth() - 80, 38);
    const bool midi = juce::Time::getMillisecondCounter() < midiFlashUntil;
    auto dot = juce::Rectangle<float> (ft.getX(), ft.getCentreY() - 4, 8, 8);
    if (midi) softGlow (g, dot.getCentre(), 3, 10, theme::green.withAlpha (0.6f));
    g.setColour (midi ? theme::green : theme::muted.withAlpha (0.5f));
    g.fillEllipse (dot);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (L ("MIDI \xc2\xb7 une touche choisit la tonalit\xc3\xa9", "MIDI \xc2\xb7 a key picks the key", "MIDI \xc2\xb7 una tecla elige la tonalidad"), ft.withTrimmedLeft (16), juce::Justification::centredLeft);
    auto tf = theme::title (11.5f); tf.setExtraKerningFactor (0.3f);
    g.setFont (tf);
    g.setColour (juce::Colour (0xffaab5ca));
    g.drawText (U ("SPACEPAD \xc2\xb7 BY JB PRAISE"), ft, juce::Justification::centred);
    g.setFont (theme::ui (12.0f));
    g.setColour (theme::muted);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), ft, juce::Justification::centredRight);
}
}
