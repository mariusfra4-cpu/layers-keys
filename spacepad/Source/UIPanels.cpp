// Spacepad by JB Praise — panneaux : bibliothèques (pads et crowds séparés), analyse d'un dossier, Paramètres.
#include "UIHelpers.h"

namespace spacepad
{
// ============================================================================================
// LibraryPanel
// ============================================================================================
LibraryPanel::LibraryPanel (SpacepadProcessor& p) : proc (p)
{
    for (auto* t : { &padTab, &crowdTab })
    {
        t->getProperties().set ("fontSize", 13.0f);
        t->getProperties().set ("weight", 2);
        t->getProperties().set ("kerning", 0.06f);
        t->getProperties().set ("flat", true);
        addAndMakeVisible (*t);
    }
    padTab.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    crowdTab.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    crowdTab.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    padTab.onClick = [this] { showTab (false); };
    crowdTab.onClick = [this] { showTab (true); };
    chooseBtn.getProperties().set ("dashed", true);
    chooseBtn.getProperties().set ("icon", "folder");
    chooseBtn.getProperties().set ("fontSize", 14.0f);
    chooseBtn.onClick = [this] { if (onChooseFolder) onChooseFolder (crowd); };
    addAndMakeVisible (chooseBtn);
    addChildComponent (crowdPicker);
    crowdPicker.onChange = [this]
    {
        int i = 0;
        for (auto& l : libs)
            if (l.type == LibType::Crowd && i++ == crowdPicker.getSelectedItemIndex()) { crowdLibId = l.id; category = 0; }
        rebuildChips();
        rebuildRows();
    };
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    crowdStop.getProperties().set ("icon", "stop");
    crowdStop.onClick = [this] { proc.stopCrowd(); };
    addChildComponent (crowdStop);
    crowdVol.setSliderStyle (juce::Slider::LinearHorizontal);
    crowdVol.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    crowdVol.setColour (juce::Slider::trackColourId, theme::amber);
    addChildComponent (crowdVol);
    crowdVolAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "crowdvol", crowdVol);
    retranslate();
    refresh();
    showTab (false);
}

const Library* LibraryPanel::currentCrowd() const
{
    const Library* first = nullptr;
    for (auto& l : libs)
        if (l.type == LibType::Crowd)
        {
            if (l.id == crowdLibId) return &l;
            if (first == nullptr) first = &l;
        }
    return first;
}

const Category* LibraryPanel::currentCategory() const
{
    auto* l = currentCrowd();
    if (l == nullptr || l->categories.empty()) return nullptr;
    return &l->categories[(size_t) juce::jlimit (0, (int) l->categories.size() - 1, category)];
}

void LibraryPanel::retranslate()
{
    chooseBtn.setButtonText (crowd ? L ("Choisir un dossier de crowds\xe2\x80\xa6", "Choose a crowds folder\xe2\x80\xa6", "Elegir una carpeta de crowds\xe2\x80\xa6")
                                   : L ("Choisir un dossier de pads\xe2\x80\xa6", "Choose a pads folder\xe2\x80\xa6", "Elegir una carpeta de pads\xe2\x80\xa6"));
    crowdStop.setTitle (L ("Arr\xc3\xaater le crowd", "Stop the crowd", "Detener el crowd"));
    crowdStop.setTooltip (crowdStop.getTitle());
    rows.emptyText = crowd ? L ("Aucune Crowd Library.\n\nCliquez sur \xc2\xab Choisir un dossier de crowds\xe2\x80\xa6 \xc2\xbb : Spacepad garde le nom du dossier et range chaque sous-dossier (Worship FX, Natural\xe2\x80\xa6) en cat\xc3\xa9gorie.",
                               "No Crowd Library.\n\nClick \xe2\x80\x9c""Choose a crowds folder\xe2\x80\xa6\xe2\x80\x9d: Spacepad keeps the folder name and turns each subfolder (Worship FX, Natural\xe2\x80\xa6) into a category.",
                               "Ninguna Crowd Library.\n\nHaz clic en \xc2\xab""Elegir una carpeta de crowds\xe2\x80\xa6\xc2\xbb: Spacepad conserva el nombre de la carpeta y convierte cada subcarpeta (Worship FX, Natural\xe2\x80\xa6) en una categor\xc3\xad""a.")
                           : L ("Aucune Pad Library.\n\nCliquez sur \xc2\xab Choisir un dossier de pads\xe2\x80\xa6 \xc2\xbb : Spacepad garde le nom du dossier, trouve la tonalit\xc3\xa9 de chaque son et range la librairie dans PADS MAJEURS et/ou PADS MINEURS.",
                                "No Pad Library.\n\nClick \xe2\x80\x9c""Choose a pads folder\xe2\x80\xa6\xe2\x80\x9d: Spacepad keeps the folder name, finds the key of every sound and files the library under MAJOR PADS and/or MINOR PADS.",
                                "Ninguna Pad Library.\n\nHaz clic en \xc2\xab""Elegir una carpeta de pads\xe2\x80\xa6\xc2\xbb: Spacepad conserva el nombre de la carpeta, encuentra la tonalidad de cada sonido y guarda la librer\xc3\xad""a en PADS MAYORES y/o PADS MENORES.");
    rebuildRows();
    repaint();
}

void LibraryPanel::refresh()
{
    libs = proc.libraries().snapshot();
    if (auto* c = currentCrowd()) crowdLibId = c->id;
    crowdPicker.clear (juce::dontSendNotification);
    int i = 0;
    for (auto& l : libs)
        if (l.type == LibType::Crowd)
        {
            crowdPicker.addItem (l.name, ++i);
            if (l.id == crowdLibId) crowdPicker.setSelectedItemIndex (i - 1, juce::dontSendNotification);
        }
    rebuildChips();
    rebuildRows();
    repaint();
}

void LibraryPanel::showTab (bool c)
{
    crowd = c;
    padTab.setToggleState (! c, juce::dontSendNotification);
    crowdTab.setToggleState (c, juce::dontSendNotification);
    chooseBtn.setColour (juce::TextButton::buttonOnColourId, c ? theme::amber : theme::accent);
    crowdStop.setVisible (c);
    crowdVol.setVisible (c);
    retranslate();
    rebuildChips();
    rebuildRows();
    viewport.setViewPosition (0, 0);
    repaint();
}

void LibraryPanel::libraryMenu (const Library& lib)
{
    juce::PopupMenu m;
    m.addSectionHeader (lib.name);
    m.addItem (1, L ("Afficher le dossier", "Show folder", "Mostrar la carpeta"));
    m.addSeparator();
    m.addItem (2, L ("Retirer de Spacepad (vos fichiers ne sont pas effac\xc3\xa9s)", "Remove from Spacepad (your files are not deleted)", "Quitar de Spacepad (tus archivos no se borran)"));
    const auto id = lib.id; const auto folder = lib.folder;
    m.showMenuAsync (juce::PopupMenu::Options(), [this, id, folder] (int r)
    {
        if (r == 1) juce::File (folder).revealToUser();
        if (r == 2) { proc.libraries().remove (id); refresh(); }
    });
}

void LibraryPanel::rebuildChips()
{
    for (auto& c : chips) removeChildComponent (c.get());
    chips.clear();
    if (crowd)
        if (auto* l = currentCrowd())
            for (int i = 0; i < (int) l->categories.size() && i < 12; ++i)
            {
                auto n = l->categories[(size_t) i].name.isEmpty() ? l->name : l->categories[(size_t) i].name;
                auto b = std::make_unique<juce::TextButton> (n);
                b->setToggleState (i == category, juce::dontSendNotification);
                b->setColour (juce::TextButton::buttonOnColourId, theme::amber);
                b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
                b->getProperties().set ("fontSize", 12.5f);
                b->onClick = [this, i] { category = i; rebuildChips(); rebuildRows(); };
                addAndMakeVisible (*b);
                chips.push_back (std::move (b));
            }
    int crowdCount = 0;
    for (auto& l : libs) if (l.type == LibType::Crowd) ++crowdCount;
    crowdPicker.setVisible (crowd && crowdCount > 1);
    resized();
}

void LibraryPanel::rebuildRows()
{
    std::vector<RowList::Row> out;
    if (! crowd)
    {
        const int curMode = proc.currentMode();
        for (int mode = 0; mode < 2; ++mode)
        {
            std::vector<const Library*> list;
            for (auto& l : libs) if (l.type == LibType::Pad && l.countMode (mode) > 0) list.push_back (&l);
            if (list.empty() && libs.empty()) continue;
            RowList::Row h;
            h.header = true;
            h.name = mode == 0 ? L ("PADS MAJEURS", "MAJOR PADS", "PADS MAYORES") : L ("PADS MINEURS", "MINOR PADS", "PADS MENORES");
            h.tag = juce::String ((int) list.size());
            h.colour = mode == 0 ? theme::gold : theme::lilac;
            out.push_back (h);
            for (auto* l : list)
            {
                RowList::Row r;
                r.name = l->name;
                const int ex = l->exactSlots (mode);
                r.sub = juce::String (l->countMode (mode)) + " " + (l->countMode (mode) > 1 ? L ("sons", "sounds", "sonidos") : L ("son", "sound", "sonido")) + U (" \xc2\xb7 ") + juce::String (ex) + "/12 "
                        + (mode == 0 ? L ("tonalit\xc3\xa9s majeures", "major keys", "tonalidades mayores") : L ("tonalit\xc3\xa9s mineures", "minor keys", "tonalidades menores"));
                r.tag = juce::String (ex) + "/12";
                r.colour = mode == 0 ? theme::gold : theme::lilac;
                r.on = l->id == proc.padLibraryId (mode);
                r.playing = r.on && mode == curMode && proc.padActive();
                const auto id = l->id; const auto copy = *l;
                r.onClick = [this, id, mode]
                {
                    proc.selectPadLibrary (id, mode);
                    proc.setMode (mode);
                    if (! proc.padActive()) proc.playPad();
                    rebuildRows();
                };
                r.onMenu = [this, copy] { libraryMenu (copy); };
                out.push_back (r);
            }
            if (list.empty())
            {
                RowList::Row r;
                r.name = mode == 0 ? L ("Aucun pad majeur", "No major pads", "Ning\xc3\xban pad mayor") : L ("Aucun pad mineur", "No minor pads", "Ning\xc3\xban pad menor");
                r.sub = L ("Ajoutez un dossier de pads", "Add a pads folder", "A\xc3\xb1""ade una carpeta de pads");
                r.colour = theme::muted;
                out.push_back (r);
            }
        }
    }
    else if (auto* l = currentCrowd())
    {
        auto* c = currentCategory();
        int flat0 = 0;
        for (auto& cc : l->categories) { if (&cc == c) break; flat0 += (int) cc.sounds.size(); }
        if (c != nullptr)
            for (int i = 0; i < (int) c->sounds.size(); ++i)
            {
                auto& s = c->sounds[(size_t) i];
                RowList::Row r;
                r.name = s.name;
                r.sub = (s.key >= 0 ? L ("Tonalit\xc3\xa9 : ", "Key: ", "Tonalidad: ") + keyText (s.key) + (s.mode == 1 ? "m" : "") : L ("Sans tonalit\xc3\xa9", "No key", "Sin tonalidad"))
                        + U (" \xc2\xb7 ") + juce::String (s.seconds, 1) + " s";
                r.tag = proc.loopOn() ? juce::String ("LOOP") : L ("1 COUP", "ONE SHOT", "1 GOLPE");
                r.colour = theme::amber;
                const int flat = flat0 + i;
                r.on = proc.crowdSounding() && proc.crowdSoundIndex() == flat && proc.crowdLibraryId() == l->id;
                r.playing = r.on;
                const auto id = l->id;
                r.onClick = [this, id, flat] { proc.playCrowd (id, flat); rebuildRows(); };
                const auto copy = *l;
                r.onMenu = [this, copy] { libraryMenu (copy); };
                out.push_back (r);
            }
    }
    rows.setRows (std::move (out));
    rows.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (rows.contentHeight(), viewport.getHeight()));
}

void LibraryPanel::updateState()
{
    const auto sig = proc.padLibraryId (0) + "|" + proc.padLibraryId (1) + "|" + juce::String (proc.currentMode()) + "|" + juce::String ((int) proc.padActive())
                   + "|" + proc.crowdLibraryId() + "|" + juce::String (proc.crowdSoundIndex()) + "|" + juce::String ((int) proc.crowdSounding()) + "|" + juce::String ((int) proc.loopOn());
    if (sig != lastStateSig) { lastStateSig = sig; rebuildRows(); }
    if (crowd) repaint (playerArea);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto tabs = r.removeFromTop (52);
    auto tl = tabs.reduced (5);
    padTab.setBounds (tl.removeFromLeft (tl.getWidth() / 2).reduced (1));
    crowdTab.setBounds (tl.reduced (1));
    r.removeFromTop (10);
    statusArea = r.removeFromTop (36);
    r.removeFromTop (10);
    chooseBtn.setBounds (r.removeFromBottom (48));
    r.removeFromBottom (12);
    if (crowd)
    {
        playerArea = r.removeFromBottom (96);
        auto pa = playerArea.reduced (14, 12);
        pa.removeFromTop (22);
        crowdStop.setBounds (pa.removeFromLeft (44).withSizeKeepingCentre (44, 44));
        pa.removeFromLeft (12);
        crowdVol.setBounds (pa.removeFromBottom (24).withTrimmedRight (54));
        r.removeFromBottom (10);
        if (crowdPicker.isVisible()) { crowdPicker.setBounds (r.removeFromTop (36)); r.removeFromTop (8); }
        int x = r.getX(), y = r.getY(), lines = 1;
        for (auto& c : chips)
        {
            const int w = juce::jmin (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (theme::ui (12.5f, 1), c->getButtonText()) + 32);
            if (x + w > r.getRight() && x > r.getX()) { x = r.getX(); y += 40; ++lines; }
            c->setVisible (lines <= 3);
            c->setBounds (x, y, w, 34);
            x += w + 6;
        }
        if (! chips.empty()) r.removeFromTop (y - r.getY() + 34 + 10);
    }
    else
        playerArea = {};
    viewport.setBounds (r);
    rows.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (rows.contentHeight(), r.getHeight()));
}

void LibraryPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (theme::panel, 0, 0, theme::panel2, b.getRight(), 0, false));
    g.fillRect (b);
    g.setColour (theme::line);
    g.drawLine (b.getRight() - 0.5f, 0, b.getRight() - 0.5f, b.getBottom());

    // onglets dans une pastille
    auto tabs = padTab.getBounds().getUnion (crowdTab.getBounds()).toFloat().expanded (4);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (tabs, tabs.getHeight() * 0.5f);

    // état
    auto st = statusArea.toFloat();
    int count = 0;
    for (auto& l : libs) if ((l.type == LibType::Crowd) == crowd) ++count;
    const auto col = count > 0 ? (crowd ? theme::amber : theme::green) : theme::muted;
    g.setColour (col.withAlpha (0.08f));
    g.fillRoundedRectangle (st, st.getHeight() * 0.5f);
    g.setColour (col.withAlpha (0.3f));
    g.drawRoundedRectangle (st.reduced (0.5f), st.getHeight() * 0.5f, 1.0f);
    auto dot = juce::Rectangle<float> (st.getX() + 14, st.getCentreY() - 4, 8, 8);
    softGlow (g, dot.getCentre(), 3, 9, col.withAlpha (0.6f));
    g.setColour (col);
    g.fillEllipse (dot);
    g.setColour (theme::text.withAlpha (0.9f));
    g.setFont (theme::ui (12.5f, 1));
    const auto plural = count > 1 ? L ("librairies", "libraries", "librer\xc3\xad""as") : L ("librairie", "library", "librer\xc3\xad""a");
    juce::String txt = crowd ? (L ("Crowds seulement \xc2\xb7 ", "Crowds only \xc2\xb7 ", "Solo crowds \xc2\xb7 ") + juce::String (count) + " " + plural)
                             : (L ("Pads seulement \xc2\xb7 ", "Pads only \xc2\xb7 ", "Solo pads \xc2\xb7 ") + juce::String (count) + " " + plural);
    g.drawText (txt, st.withTrimmedLeft (30).withTrimmedRight (10), juce::Justification::centredLeft, true);

    // lecteur de crowd, indépendant du pad
    if (crowd && ! playerArea.isEmpty())
    {
        auto pa = playerArea.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (pa, 20.0f);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawRoundedRectangle (pa.reduced (0.5f), 20.0f, 1.0f);
        auto in = pa.reduced (14, 12);
        auto f = theme::ui (10.5f, 2); f.setExtraKerningFactor (0.14f);
        g.setFont (f);
        g.setColour (juce::Colour (0xffffcf8a));
        g.drawText (L ("CROWD \xc2\xb7 IND\xc3\x89PENDANT DU PAD", "CROWD \xc2\xb7 INDEPENDENT OF THE PAD", "CROWD \xc2\xb7 INDEPENDIENTE DEL PAD"), in.removeFromTop (16), juce::Justification::centredLeft);
        in.removeFromTop (6);
        in.removeFromLeft (56);
        const bool on = proc.crowdSounding();
        g.setColour (on ? theme::text : theme::muted);
        g.setFont (theme::ui (13.0f, 1));
        g.drawText (on ? proc.crowdSoundName() : L ("Aucun crowd en lecture", "No crowd playing", "Ning\xc3\xban crowd sonando"), in.removeFromTop (20).withTrimmedRight (50), juce::Justification::centredLeft, true);
        auto bar = in.removeFromTop (8).withTrimmedRight (54).withSizeKeepingCentre (in.getWidth() - 54, 5);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 2.5f);
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (theme::amber, bar.getTopLeft(), juce::Colour (0xffffd9a8), bar.getTopRight(), false));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc.crowdPosition())), 2.5f);
        }
        g.setColour (theme::muted);
        g.setFont (theme::ui (11.0f));
        auto* prm = proc.params.getParameter ("crowdvol");
        g.drawText (prm->getText (prm->getValue(), 8), juce::Rectangle<float> (pa.getRight() - 66, crowdVol.getY() - 2.0f, 52, 28), juce::Justification::centredRight);
    }
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
    result = a.analyzeAs (folder, type);
    finished = true;
}

ScanOverlay::ScanOverlay (SpacepadProcessor& p, const juce::File& folder, LibType type) : proc (p), job (folder, type)
{
    cancelBtn.setButtonText (L ("Annuler", "Cancel", "Cancelar"));
    cancelBtn.onClick = [this]
    {
        job.signalThreadShouldExit();
        job.stopThread (5000);
        if (onClose) onClose (false);
    };
    saveBtn.setColour (juce::TextButton::buttonOnColourId, type == LibType::Crowd ? theme::amber : theme::accent);
    saveBtn.setToggleState (true, juce::dontSendNotification);
    saveBtn.getProperties().set ("weight", 2);
    saveBtn.onClick = [this]
    {
        if (results.empty()) return;
        proc.libraries().addOrReplace (results);
        // si rien n'était choisi pour le majeur ou le mineur, on prend la nouvelle librairie
        for (auto& l : results)
            if (l.type == LibType::Pad)
                for (int m = 0; m < 2; ++m)
                    if (proc.libraries().find (proc.padLibraryId (m)) == nullptr && l.countMode (m) > 0)
                        proc.selectPadLibrary (l.id, m);
        if (onClose) onClose (true);
    };
    addAndMakeVisible (cancelBtn);
    addChildComponent (saveBtn);
    addChildComponent (list);
    job.startThread();
    startTimerHz (20);
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
        std::vector<RowList::Row> rows;
        for (int i = 0; i < (int) results.size(); ++i)
        {
            RowList::Row r;
            r.name = results[(size_t) i].name;
            r.sub = results[(size_t) i].summary();
            r.tag = results[(size_t) i].type == LibType::Pad ? "PAD" : "CROWD";
            r.colour = results[(size_t) i].type == LibType::Pad ? theme::accent : theme::amber;
            r.on = i == 0;
            r.onClick = [this, i] { selected = i; timerCallback(); repaint(); };
            rows.push_back (r);
        }
        list.setRows (rows);
        list.setVisible (true);
        saveBtn.setButtonText (results.empty() ? L ("Enregistrer", "Save", "Guardar")
                                               : L ("Enregistrer ", "Save ", "Guardar ") + juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies", " libraries", " librer\xc3\xad""as") : L (" librairie", " library", " librer\xc3\xad""a")));
        saveBtn.setVisible (! results.empty());
        cancelBtn.setButtonText (results.empty() ? L ("Fermer", "Close", "Cerrar") : L ("Annuler", "Cancel", "Cancelar"));
        stopTimer();
        resized();
    }
    else if (done)
    {
        // nouvelle sélection : on remet la liste à jour
        std::vector<RowList::Row> rows;
        for (int i = 0; i < (int) results.size(); ++i)
        {
            RowList::Row r;
            r.name = results[(size_t) i].name;
            r.sub = results[(size_t) i].summary();
            r.tag = results[(size_t) i].type == LibType::Pad ? "PAD" : "CROWD";
            r.colour = results[(size_t) i].type == LibType::Pad ? theme::accent : theme::amber;
            r.on = i == selected;
            r.onClick = [this, i] { selected = i; timerCallback(); repaint(); };
            rows.push_back (r);
        }
        list.setRows (rows);
    }
    repaint();
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
        list.setBounds (c.removeFromLeft (470));
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
        g.fillRoundedRectangle (c.expanded ((float) i * 4).translated (0, (float) i * 2), 28.0f + i * 4);
    }
    g.setColour (theme::panel2.brighter (0.08f));
    g.fillRoundedRectangle (c, 28.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 28.0f, 1.0f);

    auto r = c.reduced (30, 26);
    g.setColour (theme::text);
    g.setFont (theme::ui (21.0f, 2));
    auto head = r.removeFromTop (30);
    const bool crowd = job.type == LibType::Crowd;
    g.drawText (done ? L ("Analyse termin\xc3\xa9""e", "Analysis complete", "An\xc3\xa1lisis terminado")
                     : (crowd ? L ("Analyse du dossier de crowds", "Analyzing crowds folder", "Analizando la carpeta de crowds")
                              : L ("Analyse du dossier de pads", "Analyzing pads folder", "Analizando la carpeta de pads")),
                head, juce::Justification::centredLeft);
    g.setFont (theme::ui (13.0f, 1));
    g.setColour (done ? theme::green : theme::accentLight);
    g.drawText (done ? (juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies trouv\xc3\xa9""es", " libraries found", " librer\xc3\xad""as encontradas") : L (" librairie trouv\xc3\xa9""e", " library found", " librer\xc3\xad""a encontrada")))
                     : juce::String (juce::roundToInt (job.progress.load() * 100)) + " %",
                head, juce::Justification::centredRight);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    g.drawText (job.folder.getFullPathName(), r.removeFromTop (22), juce::Justification::centredLeft, true);
    r.removeFromTop (10);

    if (! done)
    {
        auto bar = r.removeFromTop (12);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 6.0f);
        auto fill = bar.withWidth (juce::jmax (12.0f, bar.getWidth() * (float) job.progress.load()));
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
        g.drawFittedText (crowd ? L ("Tout ce dossier devient une Crowd Library \xc3\xa0 son nom ; chaque sous-dossier devient une cat\xc3\xa9gorie.",
                                     "This whole folder becomes one Crowd Library with its name; each subfolder becomes a category.",
                                     "Toda esta carpeta se convierte en una Crowd Library con su nombre; cada subcarpeta se convierte en una categor\xc3\xad""a.")
                                : L ("Chaque dossier de pads garde son nom ; Spacepad trouve la tonalit\xc3\xa9 de chaque son (par le nom du fichier, sinon \xc3\xa0 l'\xc3\xa9""coute).",
                                     "Each pads folder keeps its name; Spacepad finds the key of every sound (from the file name, otherwise by listening).",
                                     "Cada carpeta de pads conserva su nombre; Spacepad encuentra la tonalidad de cada sonido (por el nombre del archivo o escuch\xc3\xa1ndolo)."),
                          r.removeFromTop (44).toNearestInt(), juce::Justification::topLeft, 2);
        return;
    }
    if (results.empty())
    {
        g.setColour (theme::amber);
        g.setFont (theme::ui (15.0f));
        g.drawFittedText (L ("Aucun son trouv\xc3\xa9 dans ce dossier (WAV, AIFF, FLAC, MP3, OGG).", "No sounds found in this folder (WAV, AIFF, FLAC, MP3, OGG).", "No se encontraron sonidos en esta carpeta (WAV, AIFF, FLAC, MP3, OGG)."),
                          r.removeFromTop (60).toNearestInt(), juce::Justification::centredLeft, 2);
        return;
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    g.drawFittedText (crowd ? L ("Rang\xc3\xa9 dans la Crowd Library uniquement. Reconnu ensuite dans l'application et le plugin.", "Stored in the Crowd Library only. Then recognized in the app and the plugin.", "Guardado solo en la Crowd Library. Despu\xc3\xa9s se reconoce en la aplicaci\xc3\xb3n y en el plugin.")
                            : L ("Rang\xc3\xa9 dans la Pad Library uniquement (PADS MAJEURS et/ou PADS MINEURS). Reconnu ensuite dans l'application et le plugin.", "Stored in the Pad Library only (MAJOR PADS and/or MINOR PADS). Then recognized in the app and the plugin.", "Guardado solo en la Pad Library (PADS MAYORES y/o PADS MENORES). Despu\xc3\xa9s se reconoce en la aplicaci\xc3\xb3n y en el plugin."),
                      r.removeFromTop (36).toNearestInt(), juce::Justification::topLeft, 2);

    if (selected < 0 || selected >= (int) results.size()) return;
    auto& lib = results[(size_t) selected];
    auto d = list.getBounds().toFloat().withX ((float) list.getRight() + 20).withRight (r.getRight());
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
            g.setColour (mode == 0 ? theme::gold : theme::lilac);
            g.setFont (theme::ui (12.0f, 2));
            g.drawText (mode == 0 ? L ("PADS MAJEURS", "MAJOR PADS", "PADS MAYORES") : L ("PADS MINEURS", "MINOR PADS", "PADS MENORES"), d.removeFromTop (20), juce::Justification::centredLeft);
            auto row = d.removeFromTop (70);
            const float cw = (row.getWidth() - 5 * 4) / 6.0f;
            const bool any = lib.countMode (mode) > 0;
            for (int k = 0; k < 12; ++k)
            {
                auto cell = juce::Rectangle<float> (row.getX() + (k % 6) * (cw + 4), row.getY() + (k / 6) * 34.0f, cw, 30);
                const auto slot = lib.slotFor (k, mode);
                const bool exact = any && slot.exact;
                const auto col = mode == 0 ? theme::gold : theme::lilac;
                if (exact)
                {
                    g.setColour (col.withAlpha (0.15f)); g.fillRoundedRectangle (cell, 15);
                    g.setColour (col); g.drawRoundedRectangle (cell.reduced (0.5f), 15, 1);
                    g.setColour (theme::text);
                }
                else
                {
                    juce::Path p; p.addRoundedRectangle (cell.reduced (0.5f), 15);
                    juce::Path dsh; const float dl[] = { 3, 3 };
                    juce::PathStrokeType (1).createDashedStroke (dsh, p, dl, 2);
                    g.setColour (juce::Colours::white.withAlpha (0.22f)); g.fillPath (dsh);
                    g.setColour (theme::muted);
                }
                g.setFont (theme::ui (11.5f, 2));
                g.drawText (keyText (k) + (mode == 1 ? "m" : ""), cell, juce::Justification::centred);
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
        g.setColour (juce::Colour (0xffffcf8a));
        g.setFont (theme::ui (12.0f, 2));
        g.drawText (L ("CAT\xc3\x89GORIES (VOS SOUS-DOSSIERS)", "CATEGORIES (YOUR SUBFOLDERS)", "CATEGOR\xc3\x8d""AS (TUS SUBCARPETAS)"), d.removeFromTop (22), juce::Justification::centredLeft);
        for (auto& c2 : lib.categories)
        {
            if (d.getHeight() < 22) break;
            g.setColour (theme::text);
            g.setFont (theme::ui (13.0f));
            g.drawText (U ("\xe2\x80\xa2 ") + (c2.name.isEmpty() ? lib.name : c2.name) + U ("  \xc2\xb7  ") + juce::String ((int) c2.sounds.size()) + " " + L ("sons", "sounds", "sonidos"), d.removeFromTop (22), juce::Justification::centredLeft, true);
        }
    }
}

// ============================================================================================
// SettingsOverlay : « Personnaliser Spacepad »
// ============================================================================================
SettingsOverlay::SettingsOverlay (std::function<void()> c) : changed (std::move (c))
{
    load();
    for (auto* sl : { &brightness, &speed })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl->setRange (0.0, 1.0);
        sl->setColour (juce::Slider::trackColourId, theme::accent);
        addAndMakeVisible (*sl);
    }
    brightness.setValue (s.brightness, juce::dontSendNotification);
    speed.setValue (s.speed, juce::dontSendNotification);
    brightness.onValueChange = [this] { s.brightness = (float) brightness.getValue(); save(); };
    speed.onValueChange = [this] { s.speed = (float) speed.getValue(); save(); };
    resetBtn.setButtonText (L ("R\xc3\xa9initialiser", "Reset", "Restablecer"));
    doneBtn.setButtonText (L ("Termin\xc3\xa9", "Done", "Listo"));
    doneBtn.setToggleState (true, juce::dontSendNotification);
    doneBtn.getProperties().set ("weight", 2);
    resetBtn.onClick = [this]
    {
        s = UiSettings();
        accent = themeIdx = bgIdx = vizIdx = namesIdx = 0; sizeIdx = 1; animIdx = awakeIdx = 1;
        brightness.setValue (s.brightness, juce::dontSendNotification);
        speed.setValue (s.speed, juce::dontSendNotification);
        save();
    };
    doneBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (resetBtn);
    addAndMakeVisible (doneBtn);
}

void SettingsOverlay::load()
{
    s = uiSettings();
    accent = s.accent; themeIdx = s.theme; bgIdx = s.background; vizIdx = s.viz; namesIdx = s.solfegeNames ? 1 : 0;
    sizeIdx = s.scale <= 90 ? 0 : s.scale <= 100 ? 1 : s.scale <= 125 ? 2 : 3;
    langIdx = (int) lang(); animIdx = s.animations ? 1 : 0; awakeIdx = s.keepAwake ? 1 : 0;
}

void SettingsOverlay::save()
{
    static const int sizes[] = { 90, 100, 125, 150 };
    s.accent = accent; s.theme = themeIdx; s.background = bgIdx; s.viz = vizIdx; s.solfegeNames = namesIdx == 1;
    s.scale = sizes[juce::jlimit (0, 3, sizeIdx)]; s.animations = animIdx == 1; s.keepAwake = awakeIdx == 1;
    s.brightness = (float) brightness.getValue(); s.speed = (float) speed.getValue();
    setUiSettings (s);
    if ((int) lang() != langIdx) setLang ((Lang) langIdx);
    brightness.setColour (juce::Slider::trackColourId, theme::accentChoice (accent));
    speed.setColour (juce::Slider::trackColourId, theme::accentChoice (accent));
    if (changed) changed();
    resized();
    repaint();
}

juce::Rectangle<int> SettingsOverlay::card() const { return getLocalBounds().withSizeKeepingCentre (880, 650); }

void SettingsOverlay::resized()
{
    auto c = card().reduced (32, 26);
    auto foot = c.removeFromBottom (44);
    doneBtn.setBounds (foot.removeFromRight (130));
    foot.removeFromRight (10);
    resetBtn.setBounds (foot.removeFromRight (150));
    c.removeFromTop (58);
    const int colW = (c.getWidth() - 32) / 2;
    groups.clear();
    swatches.clear();
    tiles.clear();
    auto left = c.withWidth (colW), right = c.withTrimmedLeft (colW + 32);

    // ligne 1 : couleur principale | thème
    accentArea = left.removeFromTop (60);
    for (int i = 0; i < 7; ++i) swatches.push_back (juce::Rectangle<int> (accentArea.getX() + i * 46, accentArea.getY() + 22, 36, 36));
    auto themeArea = right.removeFromTop (60);
    groups.push_back ({ L ("TH\xc3\x88ME", "THEME", "TEMA"), { { L ("Nuit", "Night", "Noche"), {} }, { L ("Minuit bleu", "Midnight blue", "Medianoche azul"), {} }, { "Graphite", {} } }, &themeIdx, themeArea });
    left.removeFromTop (14); right.removeFromTop (14);
    // ligne 2 : fond animé (toute la largeur)
    bgArea = c.withY (left.getY()).withHeight (86);
    const int tw = (bgArea.getWidth() - 16) / 3;
    for (int i = 0; i < 3; ++i) tiles.push_back (juce::Rectangle<int> (bgArea.getX() + i * (tw + 8), bgArea.getY() + 24, tw, 60));
    left.removeFromTop (100); right.removeFromTop (100);
    // ligne 3 : curseurs
    auto bArea = left.removeFromTop (52), sArea = right.removeFromTop (52);
    brightness.setBounds (bArea.withTrimmedTop (22));
    speed.setBounds (sArea.withTrimmedTop (22));
    left.removeFromTop (12); right.removeFromTop (12);
    groups.push_back ({ L ("VISUALISEUR AUDIO", "AUDIO VISUALIZER", "VISUALIZADOR DE AUDIO"), { { L ("Barres + bulles", "Bars + bubbles", "Barras + burbujas"), {} }, { L ("Barres", "Bars", "Barras"), {} }, { L ("Onde", "Wave", "Onda"), {} }, { L ("Masqu\xc3\xa9", "Hidden", "Oculto"), {} } }, &vizIdx, left.removeFromTop (60) });
    groups.push_back ({ L ("NOMS DES NOTES", "NOTE NAMES", "NOMBRES DE LAS NOTAS"), { { "C D E", {} }, { L ("Do R\xc3\xa9 Mi", "Do Re Mi", "Do Re Mi"), {} } }, &namesIdx, right.removeFromTop (60) });
    left.removeFromTop (12); right.removeFromTop (12);
    groups.push_back ({ L ("TAILLE DE L'INTERFACE", "INTERFACE SIZE", "TAMA\xc3\x91O DE LA INTERFAZ"), { { "90 %", {} }, { "100 %", {} }, { "125 %", {} }, { "150 %", {} } }, &sizeIdx, left.removeFromTop (60) });
    groups.push_back ({ L ("LANGUE", "LANGUAGE", "IDIOMA"), { { U ("Fran\xc3\xa7""ais"), {} }, { "English", {} }, { U ("Espa\xc3\xb1ol"), {} } }, &langIdx, right.removeFromTop (60) });
    left.removeFromTop (12); right.removeFromTop (12);
    groups.push_back ({ L ("ANIMATIONS", "ANIMATIONS", "ANIMACIONES"), { { L ("D\xc3\xa9sactiv\xc3\xa9""es", "Off", "Desactivadas"), {} }, { L ("Activ\xc3\xa9""es", "On", "Activadas"), {} } }, &animIdx, left.removeFromTop (60) });
    groups.push_back ({ L ("GARDER L'\xc3\x89""CRAN ALLUM\xc3\x89", "KEEP THE SCREEN ON", "MANTENER LA PANTALLA ENCENDIDA"), { { L ("Non", "No", "No"), {} }, { L ("Oui, pendant le jeu", "Yes, while playing", "S\xc3\xad, al tocar"), {} } }, &awakeIdx, right.removeFromTop (60) });

    for (auto& gr : groups)
    {
        auto seg = gr.area.withTrimmedTop (22).withHeight (36);
        const int n = (int) gr.choices.size();
        for (int i = 0; i < n; ++i)
            gr.choices[(size_t) i].r = juce::Rectangle<int> (seg.getX() + 4 + i * (seg.getWidth() - 8) / n, seg.getY() + 3, (seg.getWidth() - 8) / n, seg.getHeight() - 6);
    }
}

void SettingsOverlay::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int i = 0; i < (int) swatches.size(); ++i)
        if (swatches[(size_t) i].expanded (4).contains (p)) { accent = i; save(); return; }
    for (int i = 0; i < (int) tiles.size(); ++i)
        if (tiles[(size_t) i].contains (p)) { bgIdx = i; save(); return; }
    for (auto& gr : groups)
        for (int i = 0; i < (int) gr.choices.size(); ++i)
            if (gr.choices[(size_t) i].r.contains (p)) { *gr.value = i; save(); return; }
    if (! card().contains (p) && onClose) onClose();
}

void SettingsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xb002040a));
    auto c = card().toFloat();
    for (int i = 1; i <= 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (c.expanded ((float) i * 4).translated (0, (float) i * 2), 28.0f + i * 4);
    }
    g.setColour (theme::panel2.brighter (0.08f).withAlpha (0.97f));
    g.fillRoundedRectangle (c, 28.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 28.0f, 1.0f);

    auto r = c.reduced (32, 26);
    g.setColour (theme::text);
    g.setFont (theme::ui (22.0f, 2));
    g.drawText (L ("Personnaliser Spacepad", "Customize Spacepad", "Personalizar Spacepad"), r.removeFromTop (40), juce::Justification::centredLeft);

    auto label = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.16f);
        g.setFont (f);
        g.setColour (theme::muted);
        g.drawText (t, area.removeFromTop (18), juce::Justification::centredLeft);
    };
    label (accentArea, L ("COULEUR PRINCIPALE", "MAIN COLOUR", "COLOR PRINCIPAL"));
    for (int i = 0; i < (int) swatches.size(); ++i)
    {
        auto sw = swatches[(size_t) i].toFloat();
        const auto col = theme::accentChoice (i);
        if (i == accent) { softGlow (g, sw.getCentre(), 18, 28, col.withAlpha (0.4f)); g.setColour (juce::Colours::white); g.fillEllipse (sw.expanded (3)); }
        g.setColour (col);
        g.fillEllipse (sw);
    }
    label (bgArea, L ("FOND ANIM\xc3\x89 DERRI\xc3\x88RE L'ORBE", "ANIMATED BACKGROUND BEHIND THE ORB", "FONDO ANIMADO DETR\xc3\x81S DEL ORBE"));
    const juce::String names[] = { L ("Galaxie", "Galaxy", "Galaxia"), L ("Tourbillon d'\xc3\xa9toiles", "Star swirl", "Remolino de estrellas"), L ("Aucun (sobre)", "None (plain)", "Ninguno (sobrio)") };
    for (int i = 0; i < (int) tiles.size(); ++i)
    {
        auto t = tiles[(size_t) i].toFloat();
        if (i == 0) { juce::ColourGradient gr (juce::Colour (0xffc9b8ff), t.getCentreX(), t.getCentreY(), juce::Colour (0xff141a33), t.getRight(), t.getCentreY(), true); gr.addColour (0.3, juce::Colour (0xff4a4fa8)); g.setGradientFill (gr); }
        else if (i == 1) { juce::ColourGradient gr (juce::Colour (0xff6fd8ff), t.getCentreX(), t.getCentreY(), juce::Colour (0xff0a0e1a), t.getRight(), t.getCentreY(), true); gr.addColour (0.35, juce::Colour (0xff1b3c6a)); g.setGradientFill (gr); }
        else g.setColour (theme::bg);
        g.fillRoundedRectangle (t, 16.0f);
        g.setColour (i == bgIdx ? theme::accentChoice (accent) : juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (t.reduced (1), 16.0f, i == bgIdx ? 2.5f : 1.0f);
        g.setColour (theme::text);
        g.setFont (theme::ui (12.5f, 1));
        g.drawText (names[i], t.reduced (12, 8), juce::Justification::bottomLeft);
    }
    label (brightness.getBounds().withY (brightness.getY() - 22).withHeight (22), L ("LUMINOSIT\xc3\x89 DE L'ANIMATION", "ANIMATION BRIGHTNESS", "BRILLO DE LA ANIMACI\xc3\x93N"));
    label (speed.getBounds().withY (speed.getY() - 22).withHeight (22), L ("VITESSE DE ROTATION", "ROTATION SPEED", "VELOCIDAD DE ROTACI\xc3\x93N"));
    for (auto& gr : groups)
    {
        label (gr.area, gr.title);
        auto seg = gr.area.withTrimmedTop (22).withHeight (36).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (seg, 18.0f);
        for (int i = 0; i < (int) gr.choices.size(); ++i)
        {
            auto cr = gr.choices[(size_t) i].r.toFloat();
            const bool on = i == *gr.value;
            if (on)
            {
                g.setColour (theme::accentChoice (accent));
                g.fillRoundedRectangle (cr, cr.getHeight() * 0.5f);
            }
            g.setColour (on ? juce::Colour (0xff04121c) : juce::Colour (0xffcfd8ea));
            g.setFont (theme::ui (12.5f, on ? 2 : 1));
            g.drawFittedText (gr.choices[(size_t) i].label, cr.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (L ("R\xc3\xa9glages enregistr\xc3\xa9s, repris dans l'application et le plugin.", "Settings are saved and shared by the app and the plugin.", "Ajustes guardados, compartidos por la aplicaci\xc3\xb3n y el plugin."),
                card().reduced (32, 26).removeFromBottom (44).withTrimmedRight (300).toFloat(), juce::Justification::centredLeft, true);
}
}
