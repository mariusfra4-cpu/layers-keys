// Spacepad by JB Praise — la fenêtre principale : assemble l'en-tête, la bibliothèque, l'espace, l'anneau, le crochet,
// le visualiseur, la lecture, les effets et les fenêtres (analyse, Paramètres). Anime le tout 30 fois par seconde.
#include "UIHelpers.h"
#include "AudioOpen.h"

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
    addChildComponent (hall);
    addAndMakeVisible (viz);
    addChildComponent (crowdStage);
    addAndMakeVisible (centerSwitch);
    addAndMakeVisible (fxPanel);
    centerSwitch.onChange = [this] (int v) { setCenterView (v); };
    mixBar.onLabel = [this] (int pg) { fxPanel.showPage (pg); };
    addAndMakeVisible (mixBar);
    addAndMakeVisible (stageBtn);
    addAndMakeVisible (masterBar);
    stageBtn.onClick = [this] { openStage(); };
    library.onChooseFolder = [this] (bool crowdTab) { chooseFolder (crowdTab); };
    library.onChooseFiles = [this] (bool crowdTab) { chooseFiles (crowdTab); };
    gear.onClick = [this] { openSettings(); };
    header.onLicence = [this] { showLicence (true); };
    hook.onClick = [this] { proc.setMode (proc.currentMode() == 1 ? 0 : 1); };

    playBtn.getProperties().set ("icon", "play");
    stopBtn.getProperties().set ("icon", "stop");
    playBtn.onClick = [this] { proc.playPad(); };
    stopBtn.onClick = [this] { proc.stopPad(); };
    stopAllBtn.getProperties().set ("fontSize", 12.5f);
    stopAllBtn.getProperties().set ("kerning", 0.1f);
    stopAllBtn.onClick = [this] { proc.stopAll(); };
    finBtn.getProperties().set ("fontSize", 12.5f);
    finBtn.getProperties().set ("kerning", 0.1f);
    finBtn.getProperties().set ("weight", 2);
    finBtn.onClick = [this] { proc.fadeOutAll(); };
    for (auto* b : { &playBtn, &stopBtn, &loopBtn, &stopAllBtn, &finBtn })
    {
        b->getProperties().set ("onDark", true);
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    }
    for (auto* b : { &loopBtn })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("weight", 2);
        b->getProperties().set ("kerning", 0.18f);
    }
    loopBtn.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    loopBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    for (auto* b : { &playBtn, &stopBtn, &loopBtn, &stopAllBtn, &finBtn }) addAndMakeVisible (*b);
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "loop", loopBtn);
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
    licenceView.reset();
    task.reset();
    stage.reset();
    scan.reset();
    settings.reset();
    setLookAndFeel (nullptr);
}

void MainView::retranslate()
{
    stopAllBtn.setButtonText (L ("TOUT ARR\xc3\x8aTER", "STOP ALL", "DETENER TODO"));
    playBtn.setTooltip (L ("Jouer le pad", "Play the pad", "Tocar el pad"));
    stopBtn.setTooltip (L ("Arr\xc3\xaater le pad", "Stop the pad", "Detener el pad"));
    finBtn.setButtonText (L ("FIN ", "END ", "FIN ") + juce::String (juce::roundToInt (uiSettings().fadeSeconds)) + " s");
    finBtn.setTooltip (L ("Fin du chant : tout s'efface en douceur", "End of song: everything fades out gently", "Fin de la canci\xc3\xb3n: todo se desvanece suavemente"));
    stageBtn.setTooltip (L ("Mode sc\xc3\xa8ne (plein \xc3\xa9""cran)", "Stage mode (full screen)", "Modo escenario (pantalla completa)"));
    gear.setTooltip (L ("Personnaliser Spacepad", "Customize Spacepad", "Personalizar Spacepad"));
    hook.setTooltip (proc.currentMode() == 1 ? L ("Passer en majeur", "Switch to major", "Pasar a mayor") : L ("Passer en mineur", "Switch to minor", "Pasar a menor"));
    fxPanel.retranslate();
    library.retranslate();
    repaint();
}

void MainView::applySettings()
{
    const auto st = uiSettings();
    theme::apply (st);
    look.refreshColours();
    sendLookAndFeelChange();
    // boutons de lecture posés sur l'espace : texte blanc sur fond sombre, encre du thème en Clair
    for (auto* b : { &playBtn, &stopBtn, &loopBtn, &stopAllBtn, &finBtn })
    {
        b->getProperties().set ("onDark", ! theme::light);
        b->setColour (juce::TextButton::textColourOffId, theme::light ? theme::text : juce::Colours::white);
    }
    fxPanel.applyColours();
    centerSwitch.value = st.centerView;
    setCenterView (st.centerView);
    header.invalidate();
    space.resized();
    library.showTabIndex (library.tabShown());
    mixBar.repaint();
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
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, crowdTab] (const juce::FileChooser& fc)
                          {
                              auto items = fc.getResults();
                              if (! items.isEmpty()) openScanItems (items, crowdTab ? LibType::Crowd : LibType::Pad);
                          });
}

// des sons choisis un par un (plusieurs à la fois)
void MainView::chooseFiles (bool crowdTab)
{
    chooser = std::make_unique<juce::FileChooser> (crowdTab ? L ("Choisissez vos sons de crowds", "Choose your crowd sounds", "Elige tus sonidos de crowds")
                                                            : L ("Choisissez vos sons de pads", "Choose your pad sounds", "Elige tus sonidos de pads"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), audioWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, crowdTab] (const juce::FileChooser& fc)
                          {
                              auto items = fc.getResults();
                              if (! items.isEmpty()) openScanItems (items, crowdTab ? LibType::Crowd : LibType::Pad);
                          });
}

// glisser-déposer : depuis l'Explorateur, le Finder ou le navigateur du logiciel de musique (Ableton…)
bool MainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) { juce::File file (f); if (file.isDirectory() || isAudioFile (file)) return true; }
    return false;
}

void MainView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::Array<juce::File> items;
    for (auto& f : files) items.add (juce::File (f));
    if (scan == nullptr && task == nullptr) openScanItems (items, library.tabShown() == 1 ? LibType::Crowd : LibType::Pad);
    repaint();
}

void MainView::openScan (const juce::File& folder, LibType type) { openScanItems ({ folder }, type); }

void MainView::openScanItems (const juce::Array<juce::File>& items, LibType type)
{
    settings.reset();
    stage.reset();
    scan = std::make_unique<ScanOverlay> (proc, items, type);
    scan->onClose = [this, type] (bool saved)
    {
        juce::MessageManager::callAsync ([this, saved, type]
        {
            scan.reset();
            if (saved) { library.refresh(); library.showTab (type == LibType::Crowd); }
        });
    };
    scan->onCopyAndSave = [this, type] (std::vector<Library> libs)
    {
        juce::MessageManager::callAsync ([this, type, libs]
        {
            scan.reset();
            auto shared = std::make_shared<std::vector<Library>> (libs);
            runTask (L ("Copie des sons dans Spacepad\xe2\x80\xa6", "Copying sounds into Spacepad\xe2\x80\xa6", "Copiando los sonidos en Spacepad\xe2\x80\xa6"),
                     [shared] (ProgressFn pr) { return copySoundsInto (*shared, soundsFolder(), pr); },
                     [this, shared, type] (bool ok)
                     {
                         if (! ok) return;
                         proc.libraries().addOrReplace (*shared);
                         for (auto& l : *shared)
                             if (l.type == LibType::Pad)
                                 for (int m = 0; m < 2; ++m)
                                     if (proc.libraries().find (proc.padLibraryId (m)) == nullptr && l.countMode (m) > 0)
                                         proc.selectPadLibrary (l.id, m);
                         library.refresh();
                         library.showTab (type == LibType::Crowd);
                     });
        });
    };
    addAndMakeVisible (*scan);
    scan->setBounds (getLocalBounds());
}

void MainView::runTask (const juce::String& title, std::function<bool (ProgressFn)> work, std::function<void (bool)> done)
{
    task = std::make_unique<TaskOverlay> (title, std::move (work));
    task->onDone = [this, done] (bool ok)
    {
        juce::MessageManager::callAsync ([this, done, ok]
        {
            task.reset();
            if (done) done (ok);
            if (! ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Spacepad",
                                                        L ("L'op\xc3\xa9ration n'a pas pu se terminer (place sur le disque, droits ou dossier introuvable).",
                                                           "The operation could not finish (disk space, permissions or missing folder).",
                                                           "La operaci\xc3\xb3n no pudo terminar (espacio en disco, permisos o carpeta no encontrada)."));
            repaint();
        });
    };
    addAndMakeVisible (*task);
    task->setBounds (getLocalBounds());
}

void MainView::showLicence (bool show)
{
    if (! show) { licenceView.reset(); repaint(); return; }
    if (licenceView != nullptr) return;
    licenceView = std::make_unique<LicenceOverlay> (proc);
    licenceView->onClose = [this] { juce::MessageManager::callAsync ([this] { if (proc.licenceState() != Licence::State::Locked) showLicence (false); }); };
    addAndMakeVisible (*licenceView);
    licenceView->setBounds (getLocalBounds());
    licenceView->toFront (true);
}

void MainView::openStage()
{
    stage = std::make_unique<StageOverlay> (proc);
    stage->onClose = [this] { juce::MessageManager::callAsync ([this] { stage.reset(); repaint(); }); };
    addAndMakeVisible (*stage);
    stage->setBounds (getLocalBounds());
    stage->grabKeyboardFocus();
}

void MainView::openSettings()
{
    settings = std::make_unique<SettingsOverlay> (proc, [this] { applySettings(); });
    settings->onClose = [this] { juce::MessageManager::callAsync ([this] { proc.learnMidi (-1); settings.reset(); repaint(); }); };
    settings->onExport = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("O\xc3\xb9 exporter vos librairies ?", "Where to export your libraries?", "\xc2\xbf""D\xc3\xb3nde exportar tus librer\xc3\xad""as?"),
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            auto dest = fc.getResult();
            if (! dest.isDirectory()) return;
            auto libs = std::make_shared<std::vector<Library>> (proc.libraries().snapshot());
            runTask (L ("Export des librairies\xe2\x80\xa6", "Exporting libraries\xe2\x80\xa6", "Exportando las librer\xc3\xad""as\xe2\x80\xa6"),
                     [libs, dest] (ProgressFn pr) { return exportLibraries (*libs, dest, pr); },
                     [dest] (bool ok) { if (ok) dest.getChildFile ("Spacepad - librairies").revealToUser(); });
        });
    };
    settings->onImport = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Choisissez le dossier \xc2\xab Spacepad - librairies \xc2\xbb", "Choose the \xe2\x80\x9cSpacepad - librairies\xe2\x80\x9d folder", "Elige la carpeta \xc2\xab""Spacepad - librairies\xc2\xbb"),
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            auto src = fc.getResult();
            if (! src.isDirectory()) return;
            auto out = std::make_shared<std::vector<Library>>();
            runTask (L ("Import des librairies\xe2\x80\xa6", "Importing libraries\xe2\x80\xa6", "Importando las librer\xc3\xad""as\xe2\x80\xa6"),
                     [out, src] (ProgressFn pr) { *out = importLibraries (src, pr); return ! out->empty(); },
                     [this, out] (bool ok) { if (ok) { proc.libraries().addOrReplace (*out); library.refresh(); } });
        });
    };
    addAndMakeVisible (*settings);
    settings->setBounds (getLocalBounds());
}

void MainView::step (float dt)
{
    const auto st = uiSettings();
    anim.t += dt;
    const float target = proc.padActive() ? 1.0f : 0.0f;
    // la galaxie démarre dès qu'on appuie sur Play (élan immédiat), et ralentit doucement à l'arrêt
    if (proc.padActive() && ! lastPadActive) anim.surge = juce::jmax (anim.surge, 0.8f);
    lastPadActive = proc.padActive();
    anim.env += (target - anim.env) * (1.0f - std::exp (-dt / (target > anim.env ? 0.25f : 1.8f)));
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
    if (uiSettings().centerView == 1) hall.tick (dt);
    else space.tick (dt);
    hook.tick (dt);
    if (st.viz != 3) viz.tick (dt);
}

void MainView::setCenterView (int v)
{
    v = v == 1 ? 1 : 0;
    centerSwitch.value = v;
    centerSwitch.repaint();
    const bool crowdView = v == 1;
    space.setVisible (! crowdView);
    ring.setVisible (! crowdView);
    hook.setVisible (! crowdView);
    hall.setVisible (crowdView);
    crowdStage.setVisible (crowdView);
    if (crowdView) crowdStage.refreshTiles();
    if (uiSettings().centerView != v) { auto u = uiSettings(); u.centerView = v; setUiSettings (u); lastSettings = settingsVersion(); }
    repaint();
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
    // pas encore activé (ni démo) : l'écran d'activation s'affiche tout seul
    if (tick % 10 == 0 && proc.licenceState() == Licence::State::Locked && licenceView == nullptr) showLicence (true);
    library.updateState();
    // garder l'écran allumé pendant le jeu (réglage)
    const bool wantAwake = uiSettings().keepAwake && proc.padActive() && proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone;   // dans un DAW, l'écran est géré par lui
    if (wantAwake != screenSaverOff) { screenSaverOff = wantAwake; juce::Desktop::setScreenSaverEnabled (! wantAwake); }
    if (scan == nullptr && settings == nullptr && stage == nullptr && task == nullptr)
    {
        if (uiSettings().centerView == 1)
        {
            hall.repaint();
            if (tick % 15 == 0) crowdStage.refreshTiles();
        }
        else
        {
            if (uiSettings().animations || anim.env > 0.01f || anim.surge > 0.01f) space.repaint();
            ring.repaint();
        }
        if (tick % 3 == 0) fxPanel.tick();
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
    stageBtn.setBounds (top.getRight() - 120, top.getY() + 18, 48, 48);
    masterBar.setBounds (top.getRight() - 440, top.getY() + 20, 180, 46);     // à gauche du vumètre
    r.removeFromBottom (38);
    library.setBounds (r.removeFromLeft (370));
    fxPanel.setBounds (r.removeFromRight (330));

    // centre : l'espace couvre toute la zone, l'anneau flotte dessus
    space.setBounds (r);
    hall.setBounds (r);
    centerSwitch.setBounds (r.getX() + 16, r.getY() + 14, 196, 38);   // en haut à gauche : ne cache jamais la tonalité C
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
    crowdStage.setBounds (r.withTrimmedBottom (170 + 10));
    auto row = bottom.removeFromBottom (60);
    bottom.removeFromBottom (8);
    mixBar.setBounds (bottom.removeFromBottom (32).withSizeKeepingCentre (380, 32));
    viz.setBounds (bottom.withSizeKeepingCentre (380, 70).translated (0, -2));
    auto tr = row.withSizeKeepingCentre (56 + 12 + 56 + 14 + 110 + 12 + 120 + 12 + 160, 56);
    playBtn.setBounds (tr.removeFromLeft (56)); tr.removeFromLeft (12);
    stopBtn.setBounds (tr.removeFromLeft (56)); tr.removeFromLeft (14);
    loopBtn.setBounds (tr.removeFromLeft (110).withSizeKeepingCentre (110, 48)); tr.removeFromLeft (12);
    finBtn.setBounds (tr.removeFromLeft (120).withSizeKeepingCentre (120, 48)); tr.removeFromLeft (12);
    stopAllBtn.setBounds (tr.withSizeKeepingCentre (160, 48));
    if (scan) scan->setBounds (getLocalBounds());
    if (stage) stage->setBounds (getLocalBounds());
    if (task) task->setBounds (getLocalBounds());
    if (licenceView) licenceView->setBounds (getLocalBounds());
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

    auto ft = juce::Rectangle<float> (18, (float) getHeight() - 12 - 38, (float) getWidth() - 36, 38);
    g.setColour (theme::line);
    g.drawLine (ft.getX(), ft.getY(), ft.getRight(), ft.getY());
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        // on dépose des sons : tout l'écran les accepte
        auto b = getLocalBounds().toFloat().reduced (18);
        g.setColour (juce::Colour (0xc0040812));
        g.fillRoundedRectangle (b, 26.0f);
        juce::Path pth; pth.addRoundedRectangle (b.reduced (6), 22.0f);
        juce::Path dsh; const float dl[] = { 10.0f, 8.0f };
        juce::PathStrokeType (2.5f).createDashedStroke (dsh, pth, dl, 2);
        const bool crowdDrop = library.tabShown() == 1;
        g.setColour (crowdDrop ? theme::amber : theme::accent);
        g.fillPath (dsh);
        g.setFont (theme::ui (34.0f, 2));
        g.drawText (crowdDrop ? L ("D\xc3\xa9posez vos sons : ils iront dans CROWDS", "Drop your sounds: they go to CROWDS", "Suelta tus sonidos: ir\xc3\xa1n a CROWDS")
                              : L ("D\xc3\xa9posez vos sons : ils iront dans PADS", "Drop your sounds: they go to PADS", "Suelta tus sonidos: ir\xc3\xa1n a PADS"),
                    b.withSizeKeepingCentre (b.getWidth(), 60).translated (0, -20), juce::Justification::centred);
        g.setColour (juce::Colour (0xffcfd8ea));
        g.setFont (theme::ui (17.0f));
        g.drawText (L ("Fichiers et dossiers, plusieurs \xc3\xa0 la fois \xc2\xb7 choisissez l'onglet PADS ou CROWDS avant de d\xc3\xa9poser",
                       "Files and folders, several at once \xc2\xb7 pick the PADS or CROWDS tab before dropping",
                       "Archivos y carpetas, varios a la vez \xc2\xb7 elige la pesta\xc3\xb1""a PADS o CROWDS antes de soltar"),
                    b.withSizeKeepingCentre (b.getWidth(), 30).translated (0, 30), juce::Justification::centred);
        return;
    }
    if (scan || settings || stage || task || licenceView) return;
    auto ft = juce::Rectangle<float> (40, (float) getHeight() - 12 - 38, (float) getWidth() - 80, 38);
    const bool midi = juce::Time::getMillisecondCounter() < midiFlashUntil;
    auto dot = juce::Rectangle<float> (ft.getX(), ft.getCentreY() - 4, 8, 8);
    if (midi) softGlow (g, dot.getCentre(), 3, 10, theme::green.withAlpha (0.6f));
    g.setColour (midi ? theme::green : theme::muted.withAlpha (0.5f));
    g.fillEllipse (dot);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    const auto us = uiSettings();
    static const char* nn[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    auto nt = [] (int n) { return juce::String (nn[n % 12]) + juce::String (n / 12 - 1); };
    const juce::String midiTxt = us.midiKeyMode == 0 ? L ("MIDI \xc2\xb7 zone de tonalit\xc3\xa9 ", "MIDI \xc2\xb7 key zone ", "MIDI \xc2\xb7 zona de tonalidad ") + nt (us.zoneLow) + U ("\xe2\x80\x93") + nt (us.zoneLow + 11)
                               : us.midiKeyMode == 1 ? L ("MIDI \xc2\xb7 les notes ne changent pas la tonalit\xc3\xa9", "MIDI \xc2\xb7 notes don't change the key", "MIDI \xc2\xb7 las notas no cambian la tonalidad")
                                                     : L ("MIDI \xc2\xb7 chaque note choisit la tonalit\xc3\xa9", "MIDI \xc2\xb7 every note picks the key", "MIDI \xc2\xb7 cada nota elige la tonalidad");
    g.drawText (midiTxt, ft.withTrimmedLeft (16), juce::Justification::centredLeft);
    auto tf = theme::title (11.5f); tf.setExtraKerningFactor (0.3f);
    g.setFont (tf);
    g.setColour (theme::text.withAlpha (0.75f));
    g.drawText (U ("SPACEPAD \xc2\xb7 BY JB PRAISE"), ft, juce::Justification::centred);
    g.setFont (theme::ui (12.0f));
    g.setColour (theme::muted);
    g.drawText ("v" + juce::String (JucePlugin_VersionString), ft, juce::Justification::centredRight);
}
}
