// Spacepad by JB Praise — panneaux : bibliothèques (PADS, CROWDS, SETLIST), analyse d'un dossier, Paramètres.
#include "UIHelpers.h"

namespace spacepad
{
static void styleSwitch (juce::TextButton& b, juce::Colour on)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonOnColourId, on);
    b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10131a));
    b.getProperties().set ("fontSize", 12.0f);
    b.getProperties().set ("weight", 2);
}

// ============================================================================================
// LibraryPanel
// ============================================================================================
LibraryPanel::LibraryPanel (SpacepadProcessor& p) : proc (p)
{
    for (auto* t : { &padTab, &crowdTab, &setTab })
    {
        t->getProperties().set ("fontSize", 12.5f);
        t->getProperties().set ("weight", 2);
        t->getProperties().set ("kerning", 0.06f);
        t->getProperties().set ("flat", true);
        addAndMakeVisible (*t);
    }
    padTab.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    crowdTab.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    crowdTab.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
    setTab.setColour (juce::TextButton::buttonOnColourId, theme::green);
    setTab.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff06210f));
    padTab.onClick = [this] { showTabIndex (0); };
    crowdTab.onClick = [this] { showTabIndex (1); };
    setTab.onClick = [this] { showTabIndex (2); };

    chooseBtn.getProperties().set ("dashed", true);
    chooseBtn.getProperties().set ("icon", "folder");
    chooseBtn.getProperties().set ("fontSize", 14.0f);
    chooseBtn.onClick = [this] { if (onChooseFolder) onChooseFolder (tab == 1); };
    addAndMakeVisible (chooseBtn);
    filesBtn.getProperties().set ("dashed", true);
    filesBtn.getProperties().set ("fontSize", 14.0f);
    filesBtn.onClick = [this] { if (onChooseFiles) onChooseFiles (tab == 1); };
    addAndMakeVisible (filesBtn);
    addChildComponent (crowdPicker);
    crowdPicker.onChange = [this]
    {
        int i = 0;
        for (auto& l : libs)
            if (l.type == LibType::Crowd && i++ == crowdPicker.getSelectedItemIndex()) { crowdLibId = l.id; category = 0; filter = 0; }
        rebuildChips();
        rebuildRows();
    };
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    // pads : mélange A / B
    blend.setSliderStyle (juce::Slider::LinearHorizontal);
    blend.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    blend.setColour (juce::Slider::trackColourId, theme::accent);
    addChildComponent (blend);
    blendAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "blend", blend);

    // crowds
    for (int s = 0; s < 2; ++s)
    {
        slotBtn[s].setButtonText (L ("Crowd ", "Crowd ", "Crowd ") + juce::String (s + 1));
        slotBtn[s].setColour (juce::TextButton::buttonOnColourId, s == 0 ? theme::amber : juce::Colour (0xffff8a6b));
        slotBtn[s].setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
        slotBtn[s].getProperties().set ("fontSize", 12.0f);
        slotBtn[s].onClick = [this, s] { proc.setCrowdTarget (s); slotBtn[0].setToggleState (s == 0, juce::dontSendNotification); slotBtn[1].setToggleState (s == 1, juce::dontSendNotification); };
        addChildComponent (slotBtn[s]);
        crowdStop[s].getProperties().set ("icon", "stop");
        crowdStop[s].onClick = [this, s] { proc.stopCrowd (s); rebuildRows(); };
        addChildComponent (crowdStop[s]);
        crowdVol[s].setSliderStyle (juce::Slider::LinearHorizontal);
        crowdVol[s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        crowdVol[s].setColour (juce::Slider::trackColourId, s == 0 ? theme::amber : juce::Colour (0xffff8a6b));
        addChildComponent (crowdVol[s]);
        crowdVolAtt[s] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, s == 0 ? "crowdvol" : "crowdvol2", crowdVol[s]);
    }
    slotBtn[0].setToggleState (true, juce::dontSendNotification);
    styleSwitch (linkBtn, theme::green);
    styleSwitch (followBtn, theme::green);
    addChildComponent (linkBtn);
    addChildComponent (followBtn);
    linkAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "crowdlink", linkBtn);
    followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "crowdfollow", followBtn);

    // classement des crowds : recherche et tri
    search.setTextToShowWhenEmpty (L ("Rechercher un crowd\xe2\x80\xa6", "Search a crowd\xe2\x80\xa6", "Buscar un crowd\xe2\x80\xa6"), theme::muted);
    search.setFont (theme::ui (13.0f));
    search.setIndents (14, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onTextChange = [this] { rebuildRows(); };
    search.onEscapeKey = [this] { search.clear(); rebuildRows(); };
    search.onFocusLost = [this] { repaint(); };
    addChildComponent (search);
    sortBtn.getProperties().set ("fontSize", 12.0f);
    sortBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader (L ("Trier les crowds", "Sort crowds", "Ordenar los crowds"));
        const juce::String names[] = { L ("Par cat\xc3\xa9gorie", "By category", "Por categor\xc3\xad""a"), L ("Par nom", "By name", "Por nombre"),
                                       L ("Par tonalit\xc3\xa9", "By key", "Por tonalidad"), L ("Par dur\xc3\xa9""e", "By length", "Por duraci\xc3\xb3n"),
                                       L ("Derniers jou\xc3\xa9s", "Recently played", "\xc3\x9altimos tocados") };
        for (int i = 0; i < 5; ++i) m.addItem (i + 1, names[i], true, i == sortMode);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sortBtn), [this] (int r) { if (r > 0) { sortMode = r - 1; retranslate(); } });
    };
    addChildComponent (sortBtn);

    // setlist
    addSong.onClick = [this]
    {
        proc.addSongFromCurrent (L ("Chant ", "Song ", "Canci\xc3\xb3n ") + juce::String ((int) proc.setlist().songs().size() + 1));
        rebuildRows();
    };
    prevSongBtn.setButtonText (U ("\xe2\x97\x80"));
    prevSongBtn.onClick = [this] { proc.prevSong(); rebuildRows(); };
    nextSongBtn.setColour (juce::TextButton::buttonOnColourId, theme::green);
    nextSongBtn.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff06210f));
    nextSongBtn.setToggleState (true, juce::dontSendNotification);
    nextSongBtn.getProperties().set ("weight", 2);
    nextSongBtn.getProperties().set ("kerning", 0.08f);
    nextSongBtn.onClick = [this] { proc.nextSong(); rebuildRows(); };
    for (auto* b : { &addSong, &prevSongBtn, &nextSongBtn }) addChildComponent (*b);

    retranslate();
    refresh();
    showTabIndex (0);
    startTimerHz (10);
}

LibraryPanel::~LibraryPanel() { stopTimer(); }

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
    padTab.setButtonText ("PADS");
    crowdTab.setButtonText ("CROWDS");
    setTab.setButtonText ("SETLIST");
    chooseBtn.setButtonText (L ("Dossier\xe2\x80\xa6", "Folder\xe2\x80\xa6", "Carpeta\xe2\x80\xa6"));
    filesBtn.setButtonText (U ("\xe2\x99\xaa ") + L ("Fichiers\xe2\x80\xa6", "Files\xe2\x80\xa6", "Archivos\xe2\x80\xa6"));
    chooseBtn.setTooltip (tab == 1 ? L ("Ajouter un ou plusieurs dossiers de crowds", "Add one or more crowds folders", "A\xc3\xb1""adir una o varias carpetas de crowds")
                                   : L ("Ajouter un ou plusieurs dossiers de pads", "Add one or more pads folders", "A\xc3\xb1""adir una o varias carpetas de pads"));
    filesBtn.setTooltip (L ("Choisir des sons un par un (plusieurs \xc3\xa0 la fois). Vous pouvez aussi les glisser sur Spacepad.",
                            "Pick sounds one by one (several at once). You can also drag them onto Spacepad.",
                            "Elegir sonidos uno a uno (varios a la vez). Tambi\xc3\xa9n puedes arrastrarlos a Spacepad."));
    for (int s = 0; s < 2; ++s) { crowdStop[s].setTooltip (L ("Arr\xc3\xaater ce crowd", "Stop this crowd", "Detener este crowd")); }
    linkBtn.setButtonText (L ("Li\xc3\xa9 au pad", "Linked to the pad", "Unido al pad"));
    linkBtn.setTooltip (L ("Le crowd d\xc3\xa9marre et s'arr\xc3\xaate avec le pad", "The crowd starts and stops with the pad", "El crowd empieza y se detiene con el pad"));
    followBtn.setButtonText (L ("Suit la tonalit\xc3\xa9", "Follows the key", "Sigue la tonalidad"));
    followBtn.setTooltip (L ("Un crowd qui a une tonalit\xc3\xa9 est transpos\xc3\xa9 dans celle du pad", "A crowd with a key is transposed to the pad's key", "Un crowd con tonalidad se transpone a la del pad"));
    {
        const juce::String sn[] = { L ("Cat\xc3\xa9gorie", "Category", "Categor\xc3\xad""a"), L ("Nom", "Name", "Nombre"), L ("Tonalit\xc3\xa9", "Key", "Tonalidad"),
                                    L ("Dur\xc3\xa9""e", "Length", "Duraci\xc3\xb3n"), L ("R\xc3\xa9""cents", "Recent", "Recientes") };
        sortBtn.setButtonText (L ("Trier : ", "Sort: ", "Orden: ") + sn[juce::jlimit (0, 4, sortMode)]);
    }
    addSong.setButtonText (L ("+ Ajouter", "+ Add", "+ A\xc3\xb1""adir"));
    addSong.setTooltip (L ("Enregistre le r\xc3\xa9glage actuel comme nouveau chant", "Saves the current setup as a new song", "Guarda el ajuste actual como nueva canci\xc3\xb3n"));
    nextSongBtn.setButtonText (L ("CHANT SUIVANT \xe2\x96\xb6", "NEXT SONG \xe2\x96\xb6", "SIGUIENTE \xe2\x96\xb6"));
    prevSongBtn.setTooltip (L ("Chant pr\xc3\xa9""c\xc3\xa9""dent", "Previous song", "Canci\xc3\xb3n anterior"));
    rows.emptyText = tab == 2 ? L ("La setlist est vide.\n\nR\xc3\xa9glez la tonalit\xc3\xa9, les pads, les crowds et les effets du premier chant, puis touchez \xc2\xab + Ajouter \xc2\xbb. Recommencez pour chaque chant.",
                                    "The setlist is empty.\n\nSet the key, pads, crowds and effects of the first song, then tap \xe2\x80\x9c+ Add\xe2\x80\x9d. Repeat for each song.",
                                    "La setlist est\xc3\xa1 vac\xc3\xad""a.\n\nAjusta la tonalidad, los pads, los crowds y los efectos de la primera canci\xc3\xb3n y toca \xc2\xab+ A\xc3\xb1""adir\xc2\xbb. Repite para cada canci\xc3\xb3n.")
                   : tab == 1 ? L ("Aucune Crowd Library.\n\nTouchez \xc2\xab Dossier\xe2\x80\xa6 \xc2\xbb ou \xc2\xab Fichiers\xe2\x80\xa6 \xc2\xbb, ou glissez vos sons (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) sur Spacepad, depuis l'Explorateur ou le navigateur de votre logiciel de musique. Le nom du dossier est gard\xc3\xa9, chaque sous-dossier devient une cat\xc3\xa9gorie.",
                                   "No Crowd Library.\n\nTap \xe2\x80\x9c""Folder\xe2\x80\xa6\xe2\x80\x9d or \xe2\x80\x9c""Files\xe2\x80\xa6\xe2\x80\x9d, or drag your sounds (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) onto Spacepad, from Explorer or your music software's browser. The folder name is kept, each subfolder becomes a category.",
                                   "Ninguna Crowd Library.\n\nToca \xc2\xab""Carpeta\xe2\x80\xa6\xc2\xbb o \xc2\xab""Archivos\xe2\x80\xa6\xc2\xbb, o arrastra tus sonidos (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) a Spacepad, desde el Explorador o el navegador de tu programa de m\xc3\xbasica.")
                              : L ("Aucune Pad Library.\n\nTouchez \xc2\xab Dossier\xe2\x80\xa6 \xc2\xbb ou \xc2\xab Fichiers\xe2\x80\xa6 \xc2\xbb, ou glissez vos sons (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) sur Spacepad, depuis l'Explorateur ou le navigateur de votre logiciel de musique. Spacepad trouve la tonalit\xc3\xa9 de chaque son et range la librairie dans PADS MAJEURS et/ou PADS MINEURS.",
                                   "No Pad Library.\n\nTap \xe2\x80\x9c""Folder\xe2\x80\xa6\xe2\x80\x9d or \xe2\x80\x9c""Files\xe2\x80\xa6\xe2\x80\x9d, or drag your sounds (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) onto Spacepad, from Explorer or your music software's browser. Spacepad finds the key of every sound and files the library under MAJOR PADS and/or MINOR PADS.",
                                   "Ninguna Pad Library.\n\nToca \xc2\xab""Carpeta\xe2\x80\xa6\xc2\xbb o \xc2\xab""Archivos\xe2\x80\xa6\xc2\xbb, o arrastra tus sonidos (WAV, AIFF, FLAC, MP3\xe2\x80\xa6) a Spacepad. Spacepad encuentra la tonalidad de cada sonido y guarda la librer\xc3\xad""a en PADS MAYORES y/o PADS MENORES.");
    for (auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId, juce::TextEditor::shadowColourId })
        search.setColour (id, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, theme::text);
    search.setColour (juce::CaretComponent::caretColourId, theme::amber);
    search.setTextToShowWhenEmpty (L ("Rechercher un crowd\xe2\x80\xa6", "Search a crowd\xe2\x80\xa6", "Buscar un crowd\xe2\x80\xa6"), theme::muted);
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

void LibraryPanel::showTabIndex (int t)
{
    tab = juce::jlimit (0, 2, t);
    padTab.setToggleState (tab == 0, juce::dontSendNotification);
    crowdTab.setToggleState (tab == 1, juce::dontSendNotification);
    setTab.setToggleState (tab == 2, juce::dontSendNotification);
    chooseBtn.setColour (juce::TextButton::buttonOnColourId, tab == 1 ? theme::amber : theme::accent);
    chooseBtn.setVisible (tab != 2);
    filesBtn.setVisible (tab != 2);
    filesBtn.setColour (juce::TextButton::buttonOnColourId, tab == 1 ? theme::amber : theme::accent);
    for (int s = 0; s < 2; ++s) { slotBtn[s].setVisible (tab == 1); crowdStop[s].setVisible (tab == 1); crowdVol[s].setVisible (tab == 1); }
    search.setVisible (tab == 1);
    sortBtn.setVisible (tab == 1);
    linkBtn.setVisible (tab == 1);
    followBtn.setVisible (tab == 1);
    for (auto* b : { &addSong, &prevSongBtn, &nextSongBtn }) b->setVisible (tab == 2);
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

void LibraryPanel::crowdSoundMenu (const juce::String& libId, int flat)
{
    auto lib = proc.libraries().find (libId);
    if (lib == nullptr) return;
    auto* snd = lib->sound (flat);
    if (snd == nullptr) return;
    const auto key = CrowdPrefs::keyFor (libId, snd->path);
    const bool fav = proc.crowdPrefs().isFavourite (key);
    juce::PopupMenu m;
    m.addSectionHeader (snd->name);
    m.addItem (1, L ("Jouer dans le Crowd 1", "Play in Crowd 1", "Tocar en el Crowd 1"));
    m.addItem (2, L ("Jouer dans le Crowd 2", "Play in Crowd 2", "Tocar en el Crowd 2"));
    m.addSeparator();
    m.addItem (3, fav ? L ("Retirer des favoris", "Remove from favourites", "Quitar de favoritos") : L ("Ajouter aux favoris", "Add to favourites", "A\xc3\xb1""adir a favoritos"));
    m.addItem (4, L ("Oublier les effets r\xc3\xa9gl\xc3\xa9s pour ce son", "Forget this sound's effects", "Olvidar los efectos de este sonido"), proc.crowdPrefs().hasFx (key));
    m.addSeparator();
    m.addItem (5, L ("Afficher le fichier", "Show file", "Mostrar el archivo"));
    const auto path = snd->path;
    m.showMenuAsync (juce::PopupMenu::Options(), [this, libId, flat, key, fav, path] (int r)
    {
        if (r == 1 || r == 2) proc.playCrowd (libId, flat, r - 1);
        if (r == 3) proc.crowdPrefs().setFavourite (key, ! fav);
        if (r == 4) proc.crowdPrefs().clearFx (key);
        if (r == 5) juce::File (path).revealToUser();
        rebuildChips();
        rebuildRows();
    });
}

void LibraryPanel::songMenu (int index, const Song& s)
{
    juce::PopupMenu m;
    m.addSectionHeader (s.name);
    m.addItem (1, L ("Renommer\xe2\x80\xa6", "Rename\xe2\x80\xa6", "Cambiar nombre\xe2\x80\xa6"));
    m.addItem (2, L ("Remplacer par le r\xc3\xa9glage actuel", "Replace with the current setup", "Reemplazar por el ajuste actual"));
    m.addItem (3, L ("Monter", "Move up", "Subir"), index > 0);
    m.addItem (4, L ("Descendre", "Move down", "Bajar"), index < (int) proc.setlist().songs().size() - 1);
    m.addSeparator();
    m.addItem (5, L ("Supprimer ce chant", "Delete this song", "Eliminar esta canci\xc3\xb3n"));
    m.showMenuAsync (juce::PopupMenu::Options(), [this, index, name = s.name] (int r)
    {
        if (r == 1)
        {
            auto* w = new juce::AlertWindow (L ("Renommer le chant", "Rename the song", "Cambiar el nombre"), {}, juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", name);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (L ("Annuler", "Cancel", "Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, index] (int res)
            {
                if (res == 1) { proc.setlist().rename (index, w->getTextEditorContents ("name").trim()); rebuildRows(); }
            }), true);
        }
        if (r == 2) proc.updateSongFromCurrent (index);
        if (r == 3) proc.setlist().move (index, -1);
        if (r == 4) proc.setlist().move (index, 1);
        if (r == 5) proc.setlist().remove (index);
        rebuildRows();
    });
}

void LibraryPanel::rebuildChips()
{
    for (auto& c : chips) removeChildComponent (c.get());
    chips.clear();
    if (tab == 1)
        if (auto* l = currentCrowd())
        {
            // filtres : Tous, Favoris, puis chaque catégorie (vos sous-dossiers), avec leur nombre de sons
            int total = 0, favs = 0;
            for (auto& c : l->categories)
                for (auto& snd : c.sounds) { ++total; if (proc.crowdPrefs().isFavourite (CrowdPrefs::keyFor (l->id, snd.path))) ++favs; }
            if (filter > 1 + (int) l->categories.size()) filter = 0;
            auto addChip = [this] (const juce::String& n, int f)
            {
                auto b = std::make_unique<juce::TextButton> (n);
                b->setToggleState (f == filter, juce::dontSendNotification);
                b->setColour (juce::TextButton::buttonOnColourId, theme::amber);
                b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff2a1300));
                b->getProperties().set ("fontSize", 12.0f);
                b->onClick = [this, f] { filter = f; rebuildChips(); rebuildRows(); };
                addAndMakeVisible (*b);
                chips.push_back (std::move (b));
            };
            addChip (L ("Tous", "All", "Todos") + U (" \xc2\xb7 ") + juce::String (total), 0);
            addChip (L ("Favoris", "Favourites", "Favoritos") + U (" \xc2\xb7 ") + juce::String (favs), 1);
            for (int i = 0; i < (int) l->categories.size() && i < 24; ++i)
            {
                auto& c = l->categories[(size_t) i];
                addChip ((c.name.isEmpty() ? l->name : c.name) + U (" \xc2\xb7 ") + juce::String ((int) c.sounds.size()), 2 + i);
            }
        }
    int crowdCount = 0;
    for (auto& l : libs) if (l.type == LibType::Crowd) ++crowdCount;
    crowdPicker.setVisible (tab == 1 && crowdCount > 1);
    resized();
}

void LibraryPanel::rebuildRows()
{
    std::vector<RowList::Row> out;
    const int curMode = proc.currentMode();
    if (tab == 0)
    {
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
                r.colour = mode == 0 ? theme::gold : theme::lilac;
                r.aOn = l->id == proc.padLibraryId (mode);
                r.bOn = l->id == proc.padLibraryIdB (mode);
                r.on = r.aOn || r.bOn;
                r.playing = r.on && mode == curMode && proc.padActive();
                r.chips = true;
                const auto id = l->id; const auto copy = *l;
                r.onClick = [this, id, mode]
                {
                    proc.selectPadLibrary (id, mode);
                    if (proc.padLibraryIdB (mode) == id) proc.selectPadLibraryB ({}, mode);
                    proc.setMode (mode);
                    if (! proc.padActive()) proc.playPad();
                    rebuildRows();
                };
                r.onA = r.onClick;
                r.onB = [this, id, mode]
                {
                    // second pad mélangé : toucher B à nouveau l'enlève
                    proc.selectPadLibraryB (proc.padLibraryIdB (mode) == id || proc.padLibraryId (mode) == id ? juce::String() : id, mode);
                    proc.setMode (mode);
                    rebuildRows();
                    resized();
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
    else if (tab == 1)
    {
        if (auto* l = currentCrowd())
        {
            struct E { int flat, cat; const Sound* s; juce::String key; juce::int64 played; bool fav; };
            std::vector<E> list;
            const auto q = search.getText().trim();
            int flat = 0;
            for (int ci = 0; ci < (int) l->categories.size(); ++ci)
            {
                auto& c = l->categories[(size_t) ci];
                for (auto& snd : c.sounds)
                {
                    const int f = flat++;
                    const auto key = CrowdPrefs::keyFor (l->id, snd.path);
                    const bool fav = proc.crowdPrefs().isFavourite (key);
                    if (filter == 1 && ! fav) continue;
                    if (filter >= 2 && ci != filter - 2) continue;
                    if (q.isNotEmpty() && ! snd.name.containsIgnoreCase (q) && ! c.name.containsIgnoreCase (q)) continue;
                    list.push_back ({ f, ci, &snd, key, proc.crowdPrefs().lastPlayed (key), fav });
                }
            }
            std::stable_sort (list.begin(), list.end(), [this] (const E& a, const E& b)
            {
                switch (sortMode)
                {
                    case 1: return a.s->name.compareNatural (b.s->name) < 0;
                    case 2: { const int ka = a.s->key < 0 ? 99 : a.s->key * 2 + a.s->mode, kb = b.s->key < 0 ? 99 : b.s->key * 2 + b.s->mode; return ka < kb; }
                    case 3: return a.s->seconds < b.s->seconds;
                    case 4: return a.played > b.played;
                    default: return a.cat < b.cat;
                }
            });
            int lastCat = -1;
            for (auto& e : list)
            {
                if (sortMode == 0 && filter < 2 && e.cat != lastCat)
                {
                    lastCat = e.cat;
                    RowList::Row h;
                    h.header = true;
                    auto& c = l->categories[(size_t) e.cat];
                    h.name = (c.name.isEmpty() ? l->name : c.name).toUpperCase();
                    int n = 0;
                    for (auto& x : list) if (x.cat == e.cat) ++n;
                    h.tag = juce::String (n);
                    h.colour = theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a);
                    out.push_back (h);
                }
                auto& snd = *e.s;
                RowList::Row r;
                r.name = snd.name;
                int inSlot = -1;
                for (int k = 0; k < 2; ++k) if (proc.crowdSounding (k) && proc.crowdSoundIndex (k) == e.flat && proc.crowdLibraryId (k) == l->id) inSlot = k;
                juce::String keyTxt = snd.key >= 0 ? keyText (snd.key) + (snd.mode == 1 ? "m" : "") : L ("Sans tonalit\xc3\xa9", "No key", "Sin tonalidad");
                if (inSlot >= 0 && proc.crowdSemitones (inSlot) != 0) keyTxt << L (" \xc2\xb7 jou\xc3\xa9 en ", " \xc2\xb7 played in ", " \xc2\xb7 en ") << keyText (proc.currentKey()) << (snd.mode == 1 ? "m" : "");
                r.sub = keyTxt + U (" \xc2\xb7 ") + juce::String (snd.seconds, 1) + " s";
                if (sortMode != 0 || filter >= 2) r.sub << U (" \xc2\xb7 ") << l->categories[(size_t) e.cat].name;
                r.tag = inSlot >= 0 ? juce::String (inSlot + 1) : (proc.loopOn() ? juce::String ("LOOP") : L ("1 COUP", "ONE SHOT", "1 GOLPE"));
                r.colour = inSlot == 1 ? juce::Colour (0xffff8a6b) : theme::amber;
                r.on = r.playing = inSlot >= 0;
                r.star = e.fav ? 2 : 1;
                r.fx = proc.crowdPrefs().hasFx (e.key);
                const auto id = l->id; const int fl = e.flat; const auto key = e.key; const bool fav = e.fav;
                r.onClick = [this, id, fl, inSlot]
                {
                    proc.playCrowd (id, fl, inSlot >= 0 ? inSlot : proc.crowdTarget());
                    rebuildRows();
                };
                r.onStar = [this, key, fav] { proc.crowdPrefs().setFavourite (key, ! fav); rebuildChips(); rebuildRows(); };
                r.onMenu = [this, id, fl] { crowdSoundMenu (id, fl); };
                out.push_back (r);
            }
            if (list.empty())
            {
                RowList::Row r;
                r.name = filter == 1 ? L ("Aucun favori", "No favourites", "Ning\xc3\xban favorito") : L ("Aucun son trouv\xc3\xa9", "No sound found", "Ning\xc3\xban sonido");
                r.sub = filter == 1 ? L ("Touchez l'\xc3\xa9toile d'un son pour l'ajouter", "Tap a sound's star to add it", "Toca la estrella de un sonido") : L ("Changez la recherche ou le filtre", "Change the search or the filter", "Cambia la b\xc3\xbasqueda o el filtro");
                r.colour = theme::muted;
                out.push_back (r);
            }
        }
    }
    else
    {
        auto list = proc.setlist().songs();
        const int cur = proc.setlist().current();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            auto& s = list[(size_t) i];
            RowList::Row r;
            r.badge = juce::String (i + 1);
            r.name = s.name;
            juce::String sub;
            if (auto lib = proc.libraries().find (s.padA[s.mode])) sub = lib->name;
            if (auto libB = proc.libraries().find (s.padB[s.mode])) sub << " + " << libB->name;
            for (int c = 0; c < 2; ++c)
                if (auto cl = proc.libraries().find (s.crowdLib[c]))
                    if (auto* snd = cl->sound (s.crowdIndex[c])) sub << " + " << snd->name;
            if (i == cur) sub << U (" \xc2\xb7 ") << L ("en cours", "now", "ahora");
            r.sub = sub;
            r.tag = keyText (s.key) + (s.mode == 1 ? "m" : "");
            r.colour = theme::green;
            r.on = i == cur;
            r.onClick = [this, i] { proc.gotoSong (i); rebuildRows(); };
            r.onMenu = [this, i, s] { songMenu (i, s); };
            out.push_back (r);
        }
        lastSetlistChanges = proc.setlist().changeCount();
    }
    rows.setRows (std::move (out));
    rows.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (rows.contentHeight(), viewport.getHeight()));
    blend.setVisible (tab == 0 && proc.padLibraryIdB (curMode).isNotEmpty());
}

void LibraryPanel::updateState()
{
    const auto sig = proc.padLibraryId (0) + "|" + proc.padLibraryId (1) + "|" + proc.padLibraryIdB (0) + "|" + proc.padLibraryIdB (1) + "|" + juce::String (proc.currentMode())
                   + "|" + juce::String (proc.currentKey()) + "|" + juce::String ((int) proc.padActive())
                   + "|" + proc.crowdLibraryId (0) + juce::String (proc.crowdSoundIndex (0)) + juce::String ((int) proc.crowdSounding (0))
                   + "|" + proc.crowdLibraryId (1) + juce::String (proc.crowdSoundIndex (1)) + juce::String ((int) proc.crowdSounding (1))
                   + "|" + juce::String ((int) proc.loopOn()) + "|" + juce::String (proc.setlist().changeCount()) + "|" + juce::String (proc.setlist().current())
                   + "|" + juce::String (proc.crowdPrefs().changeCount()) + "|" + juce::String (proc.crowdTarget());
    if (sig != lastStateSig)
    {
        lastStateSig = sig;
        for (int s = 0; s < 2; ++s) slotBtn[s].setToggleState (proc.crowdTarget() == s, juce::dontSendNotification);
        rebuildRows();
        resized();
    }
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto tabs = r.removeFromTop (52);
    auto tl = tabs.reduced (5);
    const int tw = tl.getWidth() / 3;
    padTab.setBounds (tl.removeFromLeft (tw).reduced (1));
    crowdTab.setBounds (tl.removeFromLeft (tw).reduced (1));
    setTab.setBounds (tl.reduced (1));
    r.removeFromTop (10);
    statusArea = r.removeFromTop (36);
    r.removeFromTop (10);
    playerArea = {}; blendArea = {};
    if (tab == 2)
    {
        auto bottom = r.removeFromBottom (52);
        addSong.setBounds (bottom.removeFromLeft (110).withSizeKeepingCentre (110, 48));
        bottom.removeFromLeft (8);
        prevSongBtn.setBounds (bottom.removeFromLeft (48).withSizeKeepingCentre (48, 48));
        bottom.removeFromLeft (8);
        nextSongBtn.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth(), 52));
        r.removeFromBottom (12);
    }
    else
    {
        auto cb = r.removeFromBottom (48);
        chooseBtn.setBounds (cb.removeFromLeft (cb.getWidth() / 2 - 4));
        cb.removeFromLeft (8);
        filesBtn.setBounds (cb);
        r.removeFromBottom (12);
    }
    if (tab == 0 && blend.isVisible())
    {
        blendArea = r.removeFromTop (66);
        blend.setBounds (blendArea.reduced (44, 0).withTrimmedTop (30).withHeight (24));
        r.removeFromTop (8);
    }
    if (tab == 1)
    {
        playerArea = r.removeFromBottom (178);
        auto pa = playerArea.reduced (14, 12);
        pa.removeFromTop (20);
        auto sw = pa.removeFromTop (32);
        linkBtn.setBounds (sw.removeFromLeft (sw.getWidth() / 2).reduced (2, 0));
        followBtn.setBounds (sw.reduced (2, 0));
        pa.removeFromTop (8);
        for (int s = 0; s < 2; ++s)
        {
            auto line = pa.removeFromTop (46);
            crowdStop[s].setBounds (line.removeFromLeft (40).withSizeKeepingCentre (40, 40));
            line.removeFromLeft (10);
            crowdVol[s].setBounds (line.removeFromBottom (22).withTrimmedRight (46));
            pa.removeFromTop (4);
        }
        r.removeFromBottom (10);
        search.setBounds (statusArea.withTrimmedRight (128).reduced (0, 1));
        sortBtn.setBounds (statusArea.removeFromRight (120));
        if (crowdPicker.isVisible()) { crowdPicker.setBounds (r.removeFromTop (36)); r.removeFromTop (8); }
        auto slots = r.removeFromTop (34);
        auto sl = slots.removeFromRight (190);
        slotBtn[0].setBounds (sl.removeFromLeft (94).reduced (2, 0));
        slotBtn[1].setBounds (sl.reduced (2, 0));
        r.removeFromTop (8);
        // filtres sur deux lignes au plus
        int x = r.getX(), y = r.getY(), lines = 1;
        for (auto& c : chips)
        {
            const int w = juce::jmin (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (theme::ui (12.0f, 1), c->getButtonText()) + 26);
            if (x + w > r.getRight() && x > r.getX()) { x = r.getX(); y += 36; ++lines; }
            c->setVisible (lines <= 2);
            c->setBounds (x, y, w, 30);
            x += w + 6;
        }
        if (! chips.empty()) r.removeFromTop (juce::jmin (lines, 2) * 36 + 4);
    }
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

    auto tabs = padTab.getBounds().getUnion (setTab.getBounds()).toFloat().expanded (4);
    g.setColour (theme::ink.withAlpha (0.06f));
    g.fillRoundedRectangle (tabs, tabs.getHeight() * 0.5f);

    // champ de recherche (verre arrondi)
    if (tab == 1 && search.isVisible())
    {
        auto sb = search.getBounds().toFloat();
        glassShape (g, sb, sb.getHeight() * 0.5f, search.hasKeyboardFocus (true), theme::ink);
        if (search.hasKeyboardFocus (true)) { g.setColour (theme::amber.withAlpha (0.7f)); g.drawRoundedRectangle (sb.reduced (0.5f), sb.getHeight() * 0.5f, 1.2f); }
    }
    // ligne d'état
    auto st = statusArea.toFloat();
    int count = 0;
    for (auto& l : libs) if ((l.type == LibType::Crowd) == (tab == 1)) ++count;
    const int songsN = (int) proc.setlist().songs().size();
    if (tab == 2) count = songsN;
    const auto col = count > 0 ? (tab == 1 ? theme::amber : theme::green) : theme::muted;
    if (tab != 1) {
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
    juce::String txt;
    if (tab == 2)
    {
        const int cur = proc.setlist().current();
        txt = juce::String (songsN) + " " + (songsN > 1 ? L ("chants", "songs", "canciones") : L ("chant", "song", "canci\xc3\xb3n"));
        if (cur >= 0) txt << U (" \xc2\xb7 ") << L ("en cours : ", "now: ", "ahora: ") << juce::String (cur + 1) << " / " << juce::String (songsN);
    }
    else
    {
        const auto plural = count > 1 ? L ("librairies", "libraries", "librer\xc3\xad""as") : L ("librairie", "library", "librer\xc3\xad""a");
        txt = tab == 1 ? (L ("Crowds seulement \xc2\xb7 ", "Crowds only \xc2\xb7 ", "Solo crowds \xc2\xb7 ") + juce::String (count) + " " + plural)
                       : (L ("Pads seulement \xc2\xb7 ", "Pads only \xc2\xb7 ", "Solo pads \xc2\xb7 ") + juce::String (count) + " " + plural);
    }
    g.drawText (txt, st.withTrimmedLeft (30).withTrimmedRight (10), juce::Justification::centredLeft, true);
    }

    auto smallTitle = [&] (juce::Rectangle<float> rr, const juce::String& t, juce::Colour c)
    {
        auto f = theme::ui (10.5f, 2); f.setExtraKerningFactor (0.14f);
        g.setFont (f); g.setColour (c);
        g.drawText (t, rr, juce::Justification::centredLeft);
    };
    // mélange A / B
    if (! blendArea.isEmpty())
    {
        auto ba = blendArea.toFloat();
        g.setColour (theme::ink.withAlpha (0.05f));
        g.fillRoundedRectangle (ba, 20.0f);
        smallTitle (ba.reduced (14, 8).removeFromTop (16), L ("M\xc3\x89LANGE DE DEUX PADS", "BLEND OF TWO PADS", "MEZCLA DE DOS PADS"), theme::accent);
        const int pb = juce::roundToInt (proc.params.getParameter ("blend")->getValue() * 100);
        g.setColour (theme::muted);
        g.setFont (theme::ui (11.5f));
        g.drawText ("A " + juce::String (100 - pb) + U (" % \xc2\xb7 B ") + juce::String (pb) + " %", ba.reduced (14, 8).removeFromTop (16), juce::Justification::centredRight);
        for (int k = 0; k < 2; ++k)
        {
            auto chip = juce::Rectangle<float> (k == 0 ? ba.getX() + 12 : ba.getRight() - 38, ba.getY() + 32, 26, 26);
            g.setColour (k == 0 ? theme::accent : theme::violet);
            g.fillEllipse (chip);
            g.setColour (juce::Colour (0xff04121c));
            g.setFont (theme::ui (11.5f, 2));
            g.drawText (k == 0 ? "A" : "B", chip, juce::Justification::centred);
        }
    }
    // crowds : à quel crowd va le prochain son
    if (tab == 1 && slotBtn[0].isVisible())
    {
        auto sb = slotBtn[0].getBounds().toFloat();
        g.setColour (theme::muted);
        g.setFont (theme::ui (12.0f, 1));
        g.drawText (L ("Jouer dans :", "Play in:", "Tocar en:"), juce::Rectangle<float> ((float) statusArea.getX(), sb.getY(), sb.getX() - statusArea.getX() - 6, sb.getHeight()), juce::Justification::centredLeft);
    }
    // combinaison et lecteurs de crowds
    if (tab == 1 && ! playerArea.isEmpty())
    {
        auto pa = playerArea.toFloat();
        g.setColour (theme::ink.withAlpha (0.05f));
        g.fillRoundedRectangle (pa, 20.0f);
        g.setColour (theme::ink.withAlpha (0.09f));
        g.drawRoundedRectangle (pa.reduced (0.5f), 20.0f, 1.0f);
        auto in = pa.reduced (14, 12);
        smallTitle (in.removeFromTop (16), L ("COMBINAISON AVEC LE PAD", "COMBINED WITH THE PAD", "COMBINACI\xc3\x93N CON EL PAD"), theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a));
        for (int s = 0; s < 2; ++s)
        {
            auto vr = crowdVol[s].getBounds().toFloat();
            const bool on = proc.crowdSounding (s);
            auto nameR = juce::Rectangle<float> (vr.getX() + 2, vr.getY() - 22, vr.getWidth(), 18);
            g.setColour (on ? theme::text : theme::muted);
            g.setFont (theme::ui (12.5f, 1));
            g.drawText (juce::String (s + 1) + U (" \xc2\xb7 ") + (on ? proc.crowdSoundName (s) : L ("aucun", "none", "ninguno")), nameR, juce::Justification::centredLeft, true);
            if (on)
            {
                auto bar = juce::Rectangle<float> (nameR.getRight() - 70, nameR.getCentreY() - 2, 64, 4);
                g.setColour (theme::ink.withAlpha (0.1f));
                g.fillRoundedRectangle (bar, 2);
                g.setColour (s == 0 ? theme::amber : juce::Colour (0xffff8a6b));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc.crowdPosition (s))), 2);
            }
            auto* prm = proc.params.getParameter (s == 0 ? "crowdvol" : "crowdvol2");
            g.setColour (theme::muted);
            g.setFont (theme::ui (11.0f));
            g.drawText (prm->getText (prm->getValue(), 8), juce::Rectangle<float> (vr.getRight() + 2, vr.getY(), 44, vr.getHeight()), juce::Justification::centredRight);
        }
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
    result = a.analyzeItems (items, type);
    skipped = a.skipped;
    finished = true;
}

ScanOverlay::ScanOverlay (SpacepadProcessor& p, const juce::File& folder, LibType type) : ScanOverlay (p, juce::Array<juce::File> { folder }, type) {}

ScanOverlay::ScanOverlay (SpacepadProcessor& p, const juce::Array<juce::File>& items, LibType type) : proc (p), job (items, type)
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
        if (uiSettings().copySounds && onCopyAndSave) { onCopyAndSave (results); return; }
        if (! proc.libraries().addOrReplace (results))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Spacepad",
                L ("Impossible d'enregistrer les librairies dans le dossier de Spacepad :\n", "Could not save the libraries in the Spacepad folder:\n", "No se pudieron guardar las librer\xc3\xad""as en la carpeta de Spacepad:\n")
                + LibraryStore::storageFolder().getFullPathName()
                + L ("\n\nV\xc3\xa9rifiez que ce dossier n'est pas en lecture seule.", "\n\nCheck that this folder is not read-only.", "\n\nComprueba que esta carpeta no sea de solo lectura."));
            return;
        }
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
    if (! done && ! job.finished.load()) { repaint(); return; }
    if (! done)
    {
        done = true;
        results = job.result;
        saveBtn.setButtonText (results.empty() ? L ("Enregistrer", "Save", "Guardar")
                                               : L ("Enregistrer ", "Save ", "Guardar ") + juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies", " libraries", " librer\xc3\xad""as") : L (" librairie", " library", " librer\xc3\xad""a")));
        saveBtn.setVisible (! results.empty());
        list.setVisible (true);
        cancelBtn.setButtonText (results.empty() ? L ("Fermer", "Close", "Cerrar") : L ("Annuler", "Cancel", "Cancelar"));
        stopTimer();
        resized();
    }
    std::vector<RowList::Row> rows;
    for (int i = 0; i < (int) results.size(); ++i)
    {
        RowList::Row r;
        r.name = results[(size_t) i].name;
        r.sub = results[(size_t) i].summary();
        r.tag = results[(size_t) i].type == LibType::Pad ? "PAD" : "CROWD";
        r.colour = results[(size_t) i].type == LibType::Pad ? theme::accent : theme::amber;
        r.on = i == selected;
        r.onClick = [this, i] { selected = i; timerCallback(); };
        rows.push_back (r);
    }
    list.setRows (rows);
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
    g.setColour (theme::panel2.brighter (theme::light ? 0.0f : 0.08f));
    g.fillRoundedRectangle (c, 28.0f);
    g.setColour (theme::ink.withAlpha (0.12f));
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
    g.setColour (done ? theme::green.darker (theme::light ? 0.6f : 0.0f) : theme::accent);
    g.drawText (done ? (juce::String ((int) results.size()) + (results.size() > 1 ? L (" librairies trouv\xc3\xa9""es", " libraries found", " librer\xc3\xad""as encontradas") : L (" librairie trouv\xc3\xa9""e", " library found", " librer\xc3\xad""a encontrada")))
                     : juce::String (juce::roundToInt (job.progress.load() * 100)) + " %",
                head, juce::Justification::centredRight);
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    {
        juce::String where;
        if (job.items.size() == 1) where = job.items[0].getFullPathName();
        else
        {
            juce::StringArray names;
            for (int i = 0; i < job.items.size() && i < 4; ++i) names.add (job.items[i].getFileName());
            where = juce::String (job.items.size()) + L (" \xc3\xa9l\xc3\xa9ments : ", " items: ", " elementos: ") + names.joinIntoString (", ") + (job.items.size() > 4 ? U ("\xe2\x80\xa6") : juce::String());
        }
        g.drawText (where, r.removeFromTop (22), juce::Justification::centredLeft, true);
    }
    r.removeFromTop (10);

    if (! done)
    {
        auto bar = r.removeFromTop (12);
        g.setColour (theme::ink.withAlpha (0.08f));
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
        g.drawFittedText (job.skipped.isEmpty() ? L ("Aucun son trouv\xc3\xa9 ici (WAV, AIFF, FLAC, MP3, OGG\xe2\x80\xa6).", "No sounds found here (WAV, AIFF, FLAC, MP3, OGG\xe2\x80\xa6).", "No se encontraron sonidos aqu\xc3\xad (WAV, AIFF, FLAC, MP3, OGG\xe2\x80\xa6).")
                                                : L ("Ces fichiers n'ont pas pu \xc3\xaatre lus :", "These files could not be read:", "Estos archivos no se pudieron leer:"),
                          r.removeFromTop (30).toNearestInt(), juce::Justification::centredLeft, 2);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::ui (12.5f));
        for (int i = 0; i < job.skipped.size() && i < 6; ++i) g.drawText (U ("\xe2\x80\xa2 ") + job.skipped[i], r.removeFromTop (20), juce::Justification::centredLeft, true);
        if (job.skipped.size() > 6) g.drawText (L ("et ", "and ", "y ") + juce::String (job.skipped.size() - 6) + L (" autres", " more", " m\xc3\xa1s"), r.removeFromTop (20), juce::Justification::centredLeft);
        return;
    }
    if (! job.skipped.isEmpty())
    {
        // fichiers refusés : signalés en bas, sans bloquer l'enregistrement des autres
        auto foot = card().reduced (30, 26).removeFromBottom (46).withTrimmedRight (430).toFloat();
        g.setColour (theme::amber);
        g.setFont (theme::ui (12.0f, 1));
        g.drawFittedText (juce::String (job.skipped.size()) + L (" fichier(s) non lu(s) : ", " file(s) not read: ", " archivo(s) no le\xc3\xad""do(s): ") + job.skipped.joinIntoString (U (" \xc2\xb7 ")),
                          foot.toNearestInt(), juce::Justification::centredLeft, 2);
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.5f));
    juce::String info = crowd ? L ("Rang\xc3\xa9 dans la Crowd Library uniquement.", "Stored in the Crowd Library only.", "Guardado solo en la Crowd Library.")
                              : L ("Rang\xc3\xa9 dans la Pad Library uniquement (PADS MAJEURS et/ou PADS MINEURS).", "Stored in the Pad Library only (MAJOR PADS and/or MINOR PADS).", "Guardado solo en la Pad Library (PADS MAYORES y/o PADS MENORES).");
    info << " " << (uiSettings().copySounds ? L ("Les sons seront copi\xc3\xa9s dans Spacepad.", "The sounds will be copied into Spacepad.", "Los sonidos se copiar\xc3\xa1n en Spacepad.")
                                            : L ("Reconnu ensuite dans l'application et le plugin.", "Then recognized in the app and the plugin.", "Despu\xc3\xa9s se reconoce en la aplicaci\xc3\xb3n y en el plugin."));
    g.drawFittedText (info, r.removeFromTop (36).toNearestInt(), juce::Justification::topLeft, 2);

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
            const auto col = mode == 0 ? theme::gold : theme::lilac;
            g.setColour (theme::light ? col.darker (0.6f) : col);
            g.setFont (theme::ui (12.0f, 2));
            g.drawText (mode == 0 ? L ("PADS MAJEURS", "MAJOR PADS", "PADS MAYORES") : L ("PADS MINEURS", "MINOR PADS", "PADS MENORES"), d.removeFromTop (20), juce::Justification::centredLeft);
            auto row = d.removeFromTop (70);
            const float cw = (row.getWidth() - 5 * 4) / 6.0f;
            const bool any = lib.countMode (mode) > 0;
            for (int k = 0; k < 12; ++k)
            {
                auto cell = juce::Rectangle<float> (row.getX() + (k % 6) * (cw + 4), row.getY() + (k / 6) * 34.0f, cw, 30);
                const bool exact = any && lib.slotFor (k, mode).exact;
                if (exact)
                {
                    g.setColour (col.withAlpha (0.18f)); g.fillRoundedRectangle (cell, 15);
                    g.setColour (col); g.drawRoundedRectangle (cell.reduced (0.5f), 15, 1);
                    g.setColour (theme::text);
                }
                else
                {
                    juce::Path p; p.addRoundedRectangle (cell.reduced (0.5f), 15);
                    juce::Path dsh; const float dl[] = { 3, 3 };
                    juce::PathStrokeType (1).createDashedStroke (dsh, p, dl, 2);
                    g.setColour (theme::ink.withAlpha (0.22f)); g.fillPath (dsh);
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
        g.setColour (theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a));
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
// Paramètres → Librairies : toutes les librairies enregistrées, à afficher ou supprimer
// ============================================================================================
namespace
{
class LibListComp : public juce::Component
{
public:
    explicit LibListComp (SpacepadProcessor& p) : proc (p) {}
    void rebuild()
    {
        rows.clear();
        removeAllChildren();
        auto libs = proc.libraries().snapshot();
        std::stable_sort (libs.begin(), libs.end(), [] (const Library& a, const Library& b)
        {
            if (a.type != b.type) return a.type == LibType::Pad;
            return a.name.compareNatural (b.name) < 0;
        });
        int lastType = -1;
        for (auto& l : libs)
        {
            if ((int) l.type != lastType)
            {
                lastType = (int) l.type;
                Row h; h.header = true; h.title = l.type == LibType::Pad ? "PADS" : "CROWDS";
                rows.push_back (std::move (h));
            }
            Row r;
            r.lib = l;
            r.show = std::make_unique<juce::TextButton> (L ("Afficher", "Show", "Mostrar"));
            r.del = std::make_unique<juce::TextButton> (L ("Supprimer", "Delete", "Eliminar"));
            r.del->setColour (juce::TextButton::buttonOnColourId, theme::red);
            r.show->getProperties().set ("fontSize", 12.0f);
            r.del->getProperties().set ("fontSize", 12.0f);
            const auto folder = l.folder;
            r.show->onClick = [folder] { juce::File (folder).revealToUser(); };
            const auto lib = l;
            r.del->onClick = [this, lib] { confirmDelete (lib); };
            addAndMakeVisible (*r.show);
            addAndMakeVisible (*r.del);
            rows.push_back (std::move (r));
        }
        setSize (getWidth(), juce::jmax (60, contentHeight()));
        resized();
        repaint();
    }
    int contentHeight() const { int h = 0; for (auto& r : rows) h += r.header ? 34 : 58; return h; }
    void resized() override
    {
        int y = 0;
        for (auto& r : rows)
        {
            if (r.header) { y += 34; continue; }
            auto row = juce::Rectangle<int> (0, y, getWidth(), 58).reduced (0, 4);
            r.del->setBounds (row.removeFromRight (110).reduced (4, 8));
            r.show->setBounds (row.removeFromRight (100).reduced (4, 8));
            y += 58;
        }
    }
    void paint (juce::Graphics& g) override
    {
        if (rows.empty())
        {
            g.setColour (theme::muted);
            g.setFont (theme::ui (14.0f));
            g.drawText (L ("Aucune librairie enregistr\xc3\xa9""e.", "No saved library.", "Ninguna librer\xc3\xad""a guardada."), getLocalBounds().withHeight (40), juce::Justification::centred);
            return;
        }
        int y = 0;
        for (auto& r : rows)
        {
            if (r.header)
            {
                auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.16f);
                g.setFont (f);
                g.setColour (r.title == "PADS" ? theme::accent : theme::amber);
                g.drawText (r.title, juce::Rectangle<int> (4, y + 10, 200, 20), juce::Justification::centredLeft);
                y += 34;
                continue;
            }
            auto row = juce::Rectangle<int> (0, y, getWidth(), 58).reduced (0, 4).toFloat();
            g.setColour (theme::ink.withAlpha (0.045f));
            g.fillRoundedRectangle (row, 16.0f);
            auto tr = row.withTrimmedLeft (16).withTrimmedRight (220);
            g.setColour (theme::text);
            g.setFont (theme::ui (14.0f, 1));
            g.drawText (r.lib.name, tr.withTrimmedBottom (row.getHeight() * 0.45f), juce::Justification::bottomLeft, true);
            g.setColour (theme::muted);
            g.setFont (theme::ui (11.5f));
            juce::String sub = juce::String (r.lib.numSounds()) + " " + (r.lib.numSounds() > 1 ? L ("sons", "sounds", "sonidos") : L ("son", "sound", "sonido"));
            if (r.lib.type == LibType::Pad) sub << U (" \xc2\xb7 ") << juce::String (r.lib.countMode (0)) << L (" majeur(s), ", " major, ", " mayor(es), ") << juce::String (r.lib.countMode (1)) << L (" mineur(s)", " minor", " menor(es)");
            sub << U (" \xc2\xb7 ") << r.lib.folder;
            if (! juce::File (r.lib.folder).isDirectory()) sub = L ("DOSSIER INTROUVABLE \xc2\xb7 ", "FOLDER NOT FOUND \xc2\xb7 ", "CARPETA NO ENCONTRADA \xc2\xb7 ") + sub;
            g.drawText (sub, tr.withTrimmedTop (row.getHeight() * 0.56f), juce::Justification::topLeft, true);
            y += 58;
        }
    }
    void confirmDelete (const Library& lib)
    {
        const bool copied = juce::File (lib.folder).isAChildOf (soundsFolder());
        juce::String msg = L ("Retirer \xc2\xab ", "Remove \xe2\x80\x9c", "\xc2\xbfQuitar \xc2\xab") + lib.name + L (" \xc2\xbb de Spacepad ?\n\n", "\xe2\x80\x9d from Spacepad?\n\n", "\xc2\xbb de Spacepad?\n\n");
        msg << (copied ? L ("Les sons copi\xc3\xa9s dans Spacepad pour cette librairie seront aussi effac\xc3\xa9s.", "The sounds copied into Spacepad for this library will also be deleted.", "Los sonidos copiados en Spacepad para esta librer\xc3\xad""a tambi\xc3\xa9n se borrar\xc3\xa1n.")
                       : L ("Vos fichiers ne sont pas effac\xc3\xa9s : seule la librairie dispara\xc3\xaet de Spacepad.", "Your files are not deleted: only the library disappears from Spacepad.", "Tus archivos no se borran: solo la librer\xc3\xad""a desaparece de Spacepad."));
        const auto id = lib.id; const auto folder = lib.folder;
        juce::Component::SafePointer<LibListComp> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Spacepad", msg, L ("Supprimer", "Delete", "Eliminar"), L ("Annuler", "Cancel", "Cancelar"), nullptr,
            juce::ModalCallbackFunction::create ([safe, id, folder, copied] (int r)
            {
                if (r != 1 || safe == nullptr) return;
                safe->proc.libraries().remove (id);
                if (copied) juce::File (folder).deleteRecursively();
                safe->rebuild();
            }));
    }
    SpacepadProcessor& proc;
private:
    struct Row { bool header = false; juce::String title; Library lib; std::unique_ptr<juce::TextButton> show, del; };
    std::vector<Row> rows;
};
}

void SettingsOverlay::rebuildLibList()
{
    lastLibChanges = proc.libraries().changeCount();
    if (auto* l = dynamic_cast<LibListComp*> (libList.get()))
    {
        l->setSize (libView.getMaximumVisibleWidth(), l->getHeight());
        l->rebuild();
    }
    deleteAllBtn.setEnabled (! proc.libraries().snapshot().empty());
    repaint();
}

// ============================================================================================
// SettingsOverlay : « Personnaliser Spacepad »
// ============================================================================================
static const float fadeChoices[] = { 4.0f, 8.0f, 12.0f, 16.0f };

SettingsOverlay::SettingsOverlay (SpacepadProcessor& p, std::function<void()> c) : proc (p), changed (std::move (c))
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
        const auto map = s.midiMap; const int km = s.midiKeyMode, zl = s.zoneLow;    // les pédales assignées sont gardées
        s = UiSettings();
        s.midiMap = map; s.midiKeyMode = km; s.zoneLow = zl;
        accent = themeIdx = bgIdx = vizIdx = namesIdx = 0; sizeIdx = 1; animIdx = awakeIdx = 1; fadeIdx = 1; copyIdx = 0; crowdBgIdx = 0;
        brightness.setValue (s.brightness, juce::dontSendNotification);
        speed.setValue (s.speed, juce::dontSendNotification);
        save();
    };
    doneBtn.onClick = [this] { if (onClose) onClose(); };
    zoneBtn.onClick = [this] { proc.learnZone(); repaint(); };
    exportBtn.onClick = [this] { if (onExport) onExport(); };
    importBtn.onClick = [this] { if (onImport) onImport(); };
    for (int a = 0; a < actCount; ++a)
    {
        assignBtn[a].onClick = [this, a] { proc.learnMidi (proc.learningAction() == a ? -1 : a); refreshMidiButtons(); };
        clearBtn[a].onClick = [this, a] { auto u = uiSettings(); u.midiMap[(size_t) a] = -1; setUiSettings (u); s = u; refreshMidiButtons(); repaint(); };
        assignBtn[a].getProperties().set ("fontSize", 12.0f);
        clearBtn[a].getProperties().set ("fontSize", 12.0f);
        assignBtn[a].setColour (juce::TextButton::buttonOnColourId, theme::accent);
        addChildComponent (assignBtn[a]);
        addChildComponent (clearBtn[a]);
    }
    for (auto* b : { &resetBtn, &doneBtn }) addAndMakeVisible (*b);
    for (auto* b : { &zoneBtn, &exportBtn, &importBtn }) addChildComponent (*b);
    // Librairies enregistrées
    inPlugin = proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
    libList = std::make_unique<LibListComp> (proc);
    libView.setViewedComponent (libList.get(), false);
    libView.setScrollBarsShown (true, false);
    libView.setScrollBarThickness (8);
    addChildComponent (libView);
    deleteAllBtn.setButtonText (L ("Tout supprimer\xe2\x80\xa6", "Delete all\xe2\x80\xa6", "Eliminar todo\xe2\x80\xa6"));
    deleteAllBtn.getProperties().set ("fontSize", 12.5f);
    deleteAllBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Spacepad",
            L ("Retirer toutes les librairies de Spacepad ?\n\nVos fichiers ne sont pas effac\xc3\xa9s (sauf les sons copi\xc3\xa9s dans Spacepad).",
               "Remove every library from Spacepad?\n\nYour files are not deleted (except sounds copied into Spacepad).",
               "\xc2\xbfQuitar todas las librer\xc3\xad""as de Spacepad?\n\nTus archivos no se borran (salvo los sonidos copiados en Spacepad)."),
            L ("Tout supprimer", "Delete all", "Eliminar todo"), L ("Annuler", "Cancel", "Cancelar"), nullptr,
            juce::ModalCallbackFunction::create ([safe] (int r)
            {
                if (r != 1 || safe == nullptr) return;
                for (auto& l : safe->proc.libraries().snapshot())
                {
                    safe->proc.libraries().remove (l.id);
                    if (juce::File (l.folder).isAChildOf (soundsFolder())) juce::File (l.folder).deleteRecursively();
                }
                safe->rebuildLibList();
            }));
    };
    addChildComponent (deleteAllBtn);
    // enregistrement avec le logiciel de musique
    clearRecBtn.setButtonText (L ("Effacer cet enregistrement\xe2\x80\xa6", "Clear this recording\xe2\x80\xa6", "Borrar esta grabaci\xc3\xb3n\xe2\x80\xa6"));
    keepKeysBtn.setButtonText (L ("Ne garder que les tonalit\xc3\xa9s", "Keep only the keys", "Conservar solo las tonalidades"));
    for (auto* b : { &clearRecBtn, &keepKeysBtn }) { b->getProperties().set ("fontSize", 12.5f); addChildComponent (*b); }
    clearRecBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Spacepad",
            L ("Effacer toutes les actions enregistr\xc3\xa9""es avec le logiciel de musique dans ce projet ?", "Clear every action recorded with the music software in this project?", "\xc2\xbf""Borrar todas las acciones grabadas con el programa de m\xc3\xbasica en este proyecto?"),
            L ("Effacer", "Clear", "Borrar"), L ("Annuler", "Cancel", "Cancelar"), nullptr,
            juce::ModalCallbackFunction::create ([safe] (int r) { if (r == 1 && safe != nullptr) { safe->proc.clearRecording(); safe->repaint(); } }));
    };
    keepKeysBtn.onClick = [this] { proc.clearRecording (true); repaint(); };
    showPage (0);
    startTimerHz (8);
}

void SettingsOverlay::load()
{
    s = uiSettings();
    accent = s.accent; themeIdx = s.theme; bgIdx = s.background; vizIdx = s.viz; namesIdx = s.solfegeNames ? 1 : 0;
    sizeIdx = s.scale <= 90 ? 0 : s.scale <= 100 ? 1 : s.scale <= 125 ? 2 : 3;
    langIdx = (int) lang(); animIdx = s.animations ? 1 : 0; awakeIdx = s.keepAwake ? 1 : 0;
    keyModeIdx = s.midiKeyMode;
    fadeIdx = 1;
    for (int i = 0; i < 4; ++i) if (std::abs (fadeChoices[i] - s.fadeSeconds) < 0.5f) fadeIdx = i;
    copyIdx = s.copySounds ? 1 : 0;
    crowdBgIdx = s.crowdBg;
    dawRecIdx = s.dawRecord;
}

void SettingsOverlay::save()
{
    static const int sizes[] = { 90, 100, 125, 150 };
    const auto keep = uiSettings();             // valeurs changées ailleurs pendant que la fenêtre est ouverte (MIDI appris)
    s.midiMap = keep.midiMap; s.zoneLow = keep.zoneLow;
    s.accent = accent; s.theme = themeIdx; s.background = bgIdx; s.viz = vizIdx; s.solfegeNames = namesIdx == 1;
    s.scale = sizes[juce::jlimit (0, 3, sizeIdx)]; s.animations = animIdx == 1; s.keepAwake = awakeIdx == 1;
    s.brightness = (float) brightness.getValue(); s.speed = (float) speed.getValue();
    s.midiKeyMode = keyModeIdx; s.fadeSeconds = fadeChoices[juce::jlimit (0, 3, fadeIdx)]; s.copySounds = copyIdx == 1;
    s.crowdBg = crowdBgIdx;
    s.dawRecord = dawRecIdx;
    setUiSettings (s);
    if ((int) lang() != langIdx) setLang ((Lang) langIdx);
    brightness.setColour (juce::Slider::trackColourId, theme::accentChoice (accent));
    speed.setColour (juce::Slider::trackColourId, theme::accentChoice (accent));
    if (changed) changed();
    resized();
    repaint();
}

void SettingsOverlay::showPage (int p)
{
    page = juce::jlimit (0, 4, p);
    clearRecBtn.setVisible (page == 4);
    keepKeysBtn.setVisible (page == 4);
    libView.setVisible (page == 3);
    deleteAllBtn.setVisible (page == 3);
    if (page == 3) { resized(); rebuildLibList(); }
    brightness.setVisible (page == 0);
    speed.setVisible (page == 0);
    zoneBtn.setVisible (page == 1);
    for (int a = 0; a < actCount; ++a) { assignBtn[a].setVisible (page == 1); clearBtn[a].setVisible (page == 1); }
    exportBtn.setVisible (page == 2);
    importBtn.setVisible (page == 2);
    refreshMidiButtons();
    resized();
    repaint();
}

void SettingsOverlay::refreshMidiButtons()
{
    const auto u = uiSettings();
    const int learning = proc.learningAction();
    for (int a = 0; a < actCount; ++a)
    {
        assignBtn[a].setToggleState (learning == a, juce::dontSendNotification);
        assignBtn[a].setButtonText (learning == a ? L ("Appuyez sur la p\xc3\xa9""dale\xe2\x80\xa6", "Press the pedal\xe2\x80\xa6", "Pulsa el pedal\xe2\x80\xa6") : L ("Assigner", "Assign", "Asignar"));
        clearBtn[a].setButtonText (L ("Effacer", "Clear", "Borrar"));
        clearBtn[a].setEnabled (u.midiMap[(size_t) a] >= 0);
    }
    zoneBtn.setToggleState (proc.learningZone(), juce::dontSendNotification);
    zoneBtn.setButtonText (proc.learningZone() ? L ("Appuyez sur la premi\xc3\xa8re touche\xe2\x80\xa6", "Press the first key\xe2\x80\xa6", "Pulsa la primera tecla\xe2\x80\xa6") : L ("D\xc3\xa9""finir la zone", "Set the zone", "Definir la zona"));
    exportBtn.setButtonText (L ("Exporter mes librairies\xe2\x80\xa6", "Export my libraries\xe2\x80\xa6", "Exportar mis librer\xc3\xad""as\xe2\x80\xa6"));
    importBtn.setButtonText (L ("Importer\xe2\x80\xa6", "Import\xe2\x80\xa6", "Importar\xe2\x80\xa6"));
    if (keyModeIdx != u.midiKeyMode) { keyModeIdx = u.midiKeyMode; }
}

juce::Rectangle<int> SettingsOverlay::card() const { return getLocalBounds().withSizeKeepingCentre (900, 700); }

void SettingsOverlay::resized()
{
    auto c = card().reduced (32, 24);
    auto foot = c.removeFromBottom (44);
    doneBtn.setBounds (foot.removeFromRight (130));
    foot.removeFromRight (10);
    resetBtn.setBounds (foot.removeFromRight (150));
    resetBtn.setVisible (page == 0);
    c.removeFromTop (44);
    // onglets de pages
    pageTabs.clear();
    auto pt = c.removeFromTop (40);
    const int tabW = (pt.getWidth() - 8) / 5;
    for (int i = 0; i < 5; ++i) pageTabs.push_back (juce::Rectangle<int> (pt.getX() + 4 + i * tabW, pt.getY() + 3, tabW - 4, 34));
    c.removeFromTop (14);
    groups.clear(); swatches.clear(); tiles.clear();
    const int colW = (c.getWidth() - 32) / 2;
    auto left = c.withWidth (colW), right = c.withTrimmedLeft (colW + 32);
    auto seg = [&] (Group gr) { groups.push_back (std::move (gr)); };

    if (page == 0)
    {
        accentArea = left.removeFromTop (60);
        for (int i = 0; i < 7; ++i) swatches.push_back (juce::Rectangle<int> (accentArea.getX() + i * 46, accentArea.getY() + 22, 36, 36));
        seg ({ L ("TH\xc3\x88ME", "THEME", "TEMA"), { { L ("Nuit", "Night", "Noche"), {} }, { L ("Minuit bleu", "Midnight", "Medianoche"), {} }, { "Graphite", {} }, { L ("Clair", "Light", "Claro"), {} } }, &themeIdx, right.removeFromTop (60) });
        left.removeFromTop (12); right.removeFromTop (12);
        bgArea = c.withY (left.getY()).withHeight (86);
        const int tw = (bgArea.getWidth() - 16) / 3;
        for (int i = 0; i < 3; ++i) tiles.push_back (juce::Rectangle<int> (bgArea.getX() + i * (tw + 8), bgArea.getY() + 24, tw, 60));
        left.removeFromTop (98); right.removeFromTop (98);
        auto bArea = left.removeFromTop (52), sArea = right.removeFromTop (52);
        brightness.setBounds (bArea.withTrimmedTop (22));
        speed.setBounds (sArea.withTrimmedTop (22));
        left.removeFromTop (10); right.removeFromTop (10);
        seg ({ L ("VISUALISEUR AUDIO", "AUDIO VISUALIZER", "VISUALIZADOR DE AUDIO"), { { L ("Barres + bulles", "Bars + bubbles", "Barras + burbujas"), {} }, { L ("Barres", "Bars", "Barras"), {} }, { L ("Onde", "Wave", "Onda"), {} }, { L ("Masqu\xc3\xa9", "Hidden", "Oculto"), {} } }, &vizIdx, left.removeFromTop (60) });
        seg ({ L ("NOMS DES NOTES", "NOTE NAMES", "NOMBRES DE LAS NOTAS"), { { "C D E", {} }, { L ("Do R\xc3\xa9 Mi", "Do Re Mi", "Do Re Mi"), {} } }, &namesIdx, right.removeFromTop (60) });
        left.removeFromTop (10); right.removeFromTop (10);
        seg ({ L ("TAILLE DE L'INTERFACE", "INTERFACE SIZE", "TAMA\xc3\x91O DE LA INTERFAZ"), { { "90 %", {} }, { "100 %", {} }, { "125 %", {} }, { "150 %", {} } }, &sizeIdx, left.removeFromTop (60) });
        seg ({ L ("LANGUE", "LANGUAGE", "IDIOMA"), { { U ("Fran\xc3\xa7""ais"), {} }, { "English", {} }, { U ("Espa\xc3\xb1ol"), {} } }, &langIdx, right.removeFromTop (60) });
        left.removeFromTop (10); right.removeFromTop (10);
        seg ({ L ("ANIMATIONS", "ANIMATIONS", "ANIMACIONES"), { { L ("D\xc3\xa9sactiv\xc3\xa9""es", "Off", "Desactivadas"), {} }, { L ("Activ\xc3\xa9""es", "On", "Activadas"), {} } }, &animIdx, left.removeFromTop (60) });
        if (! inPlugin)   // dans un logiciel de musique, c'est lui (et Windows) qui gèrent l'écran
            seg ({ L ("GARDER L'\xc3\x89""CRAN ALLUM\xc3\x89", "KEEP THE SCREEN ON", "MANTENER LA PANTALLA ENCENDIDA"), { { L ("Non", "No", "No"), {} }, { L ("Oui, pendant le jeu", "Yes, while playing", "S\xc3\xad, al tocar"), {} } }, &awakeIdx, right.removeFromTop (60) });
        else right.removeFromTop (60);
        left.removeFromTop (10);
        seg ({ L ("FOND DES CROWDS (MODE CROWD)", "CROWDS BACKGROUND (CROWD MODE)", "FONDO DE LOS CROWDS (MODO CROWD)"),
               { { L ("Photo douce", "Soft photo", "Foto suave"), {} }, { L ("Photo naturelle", "Natural photo", "Foto natural"), {} },
                 { L ("Photo teinte Spacepad", "Spacepad-tinted photo", "Foto tono Spacepad"), {} }, { L ("Salle dessin\xc3\xa9""e", "Drawn hall", "Sala dibujada"), {} } },
               &crowdBgIdx, c.withY (left.getY()).withHeight (60) });
    }
    else if (page == 1)
    {
        auto top = c.removeFromTop (60);
        seg ({ L ("QUELLES NOTES MIDI CHANGENT LA TONALIT\xc3\x89", "WHICH MIDI NOTES CHANGE THE KEY", "QU\xc3\x89 NOTAS MIDI CAMBIAN LA TONALIDAD"),
               { { L ("Zone de commande", "Command zone", "Zona de mando"), {} }, { L ("Jamais", "Never", "Nunca"), {} }, { L ("Toutes les notes", "All notes", "Todas las notas"), {} } }, &keyModeIdx, top.removeFromLeft (520) });
        zoneArea = top;
        zoneBtn.setBounds (top.withTrimmedTop (22).withHeight (36).withTrimmedLeft (16));
        c.removeFromTop (8);
        infoArea = c.removeFromTop (inPlugin ? 38 : 22);
        c.removeFromTop (6);
        midiListArea = c;
        const int rowH = juce::jmin (40, c.getHeight() / actCount);
        for (int a = 0; a < actCount; ++a)
        {
            auto row = juce::Rectangle<int> (c.getX(), c.getY() + a * rowH, c.getWidth(), rowH);
            clearBtn[a].setBounds (row.removeFromRight (100).reduced (2, 3));
            assignBtn[a].setBounds (row.removeFromRight (200).reduced (2, 3));
        }
    }
    else if (page == 2)
    {
        seg ({ L ("DUR\xc3\x89""E DE \xc2\xab FIN DU CHANT \xc2\xbb", "\xe2\x80\x9c""END OF SONG\xe2\x80\x9d DURATION", "DURACI\xc3\x93N DEL \xc2\xab""FIN DE LA CANCI\xc3\x93N\xc2\xbb"), { { "4 s", {} }, { "8 s", {} }, { "12 s", {} }, { "16 s", {} } }, &fadeIdx, left.removeFromTop (60) });
        seg ({ L ("COPIER LES SONS DANS SPACEPAD", "COPY SOUNDS INTO SPACEPAD", "COPIAR LOS SONIDOS EN SPACEPAD"), { { L ("Non", "No", "No"), {} }, { L ("Oui, \xc3\xa0 l'enregistrement", "Yes, when saving", "S\xc3\xad, al guardar"), {} } }, &copyIdx, right.removeFromTop (60) });
        left.removeFromTop (70); right.removeFromTop (70);
        auto ex = c.withY (left.getY()).withHeight (64);
        exportBtn.setBounds (ex.withTrimmedTop (24).withWidth (260).withHeight (40));
        importBtn.setBounds (ex.withTrimmedTop (24).withTrimmedLeft (272).withWidth (150).withHeight (40));
        infoArea = c.withY (ex.getBottom() + 20).withHeight (220);
    }
    if (page == 4)
    {
        seg ({ L ("ENREGISTRER AVEC LE LOGICIEL DE MUSIQUE", "RECORD WITH THE MUSIC SOFTWARE", "GRABAR CON EL PROGRAMA DE M\xc3\x9aSICA"),
               { { L ("Automatique", "Automatic", "Autom\xc3\xa1tico"), {} }, { L ("D\xc3\xa9sactiv\xc3\xa9", "Off", "Desactivado"), {} } }, &dawRecIdx, c.removeFromTop (60).withWidth (420) });
        c.removeFromTop (8);
        recInfoArea = c.removeFromTop (40);
        c.removeFromTop (8);
        auto side = c.removeFromRight (270);
        clearRecBtn.setBounds (side.removeFromTop (40));
        side.removeFromTop (10);
        keepKeysBtn.setBounds (side.removeFromTop (40));
        c.removeFromRight (24);
        recListArea = c;
    }
    if (page == 3)
    {
        libHeadArea = c.removeFromTop (40);
        deleteAllBtn.setBounds (libHeadArea.removeFromRight (170).withSizeKeepingCentre (170, 36));
        c.removeFromTop (6);
        libView.setBounds (c);
        if (libList != nullptr) libList->setSize (libView.getMaximumVisibleWidth(), juce::jmax (c.getHeight(), libList->getHeight()));
    }
    for (auto& gr : groups)
    {
        auto sg = gr.area.withTrimmedTop (22).withHeight (36);
        const int n = (int) gr.choices.size();
        for (int i = 0; i < n; ++i)
            gr.choices[(size_t) i].r = juce::Rectangle<int> (sg.getX() + 4 + i * (sg.getWidth() - 8) / n, sg.getY() + 3, (sg.getWidth() - 8) / n, sg.getHeight() - 6);
    }
}

void SettingsOverlay::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int i = 0; i < (int) pageTabs.size(); ++i)
        if (pageTabs[(size_t) i].contains (p)) { showPage (i); return; }
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
    g.setColour (theme::light ? juce::Colours::white : theme::panel2.brighter (0.08f).withAlpha (0.97f));
    g.fillRoundedRectangle (c, 28.0f);
    g.setColour (theme::ink.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 28.0f, 1.0f);
    const auto acc = theme::accentChoice (accent);

    auto r = c.reduced (32, 24);
    g.setColour (theme::text);
    g.setFont (theme::ui (22.0f, 2));
    g.drawText (L ("Personnaliser Spacepad", "Customize Spacepad", "Personalizar Spacepad"), r.removeFromTop (40), juce::Justification::centredLeft);

    // onglets des pages
    if (! pageTabs.empty())
    {
        auto all = pageTabs.front().getUnion (pageTabs.back()).toFloat().expanded (4, 3);
        g.setColour (theme::ink.withAlpha (0.06f));
        g.fillRoundedRectangle (all, all.getHeight() * 0.5f);
        const juce::String names[] = { L ("Apparence", "Appearance", "Apariencia"), L ("MIDI & p\xc3\xa9""dales", "MIDI & pedals", "MIDI y pedales"), L ("Sons & sorties", "Sounds & outputs", "Sonidos y salidas"),
                                       L ("Librairies", "Libraries", "Librer\xc3\xad""as"), L ("Enregistrement", "Recording", "Grabaci\xc3\xb3n") };
        for (int i = 0; i < 5; ++i)
        {
            auto tr = pageTabs[(size_t) i].toFloat();
            if (i == page) { g.setColour (acc); g.fillRoundedRectangle (tr, tr.getHeight() * 0.5f); }
            g.setColour (i == page ? juce::Colour (0xff04121c) : theme::text.withAlpha (0.85f));
            g.setFont (theme::ui (13.0f, i == page ? 2 : 1));
            g.drawText (names[i], tr, juce::Justification::centred);
        }
    }

    auto label = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.16f);
        g.setFont (f);
        g.setColour (theme::muted);
        g.drawText (t, area.removeFromTop (18), juce::Justification::centredLeft);
    };
    if (page == 0)
    {
        label (accentArea, L ("COULEUR PRINCIPALE", "MAIN COLOUR", "COLOR PRINCIPAL"));
        for (int i = 0; i < (int) swatches.size(); ++i)
        {
            auto sw = swatches[(size_t) i].toFloat();
            const auto col = theme::accentChoice (i);
            if (i == accent) { softGlow (g, sw.getCentre(), 18, 28, col.withAlpha (0.4f)); g.setColour (theme::light ? juce::Colour (0xff1b2233) : juce::Colours::white); g.fillEllipse (sw.expanded (3)); }
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
            else g.setColour (juce::Colour (0xff0b0f1a));
            g.fillRoundedRectangle (t, 16.0f);
            g.setColour (i == bgIdx ? acc : theme::ink.withAlpha (0.12f));
            g.drawRoundedRectangle (t.reduced (1), 16.0f, i == bgIdx ? 2.5f : 1.0f);
            g.setColour (juce::Colour (0xffe8edf7));
            g.setFont (theme::ui (12.5f, 1));
            g.drawText (names[i], t.reduced (12, 8), juce::Justification::bottomLeft);
        }
        label (brightness.getBounds().withY (brightness.getY() - 22).withHeight (22), L ("LUMINOSIT\xc3\x89 DE L'ANIMATION", "ANIMATION BRIGHTNESS", "BRILLO DE LA ANIMACI\xc3\x93N"));
        label (speed.getBounds().withY (speed.getY() - 22).withHeight (22), L ("VITESSE DE ROTATION", "ROTATION SPEED", "VELOCIDAD DE ROTACI\xc3\x93N"));
    }
    else if (page == 1)
    {
        const auto u = uiSettings();
        static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        auto noteTxt = [] (int note) { return juce::String (n[note % 12]) + juce::String (note / 12 - 1); };
        g.setColour (theme::muted);
        g.setFont (theme::ui (12.0f));
        juce::String info;
        if (u.midiKeyMode == 0) info = L ("Seules les touches ", "Only the keys ", "Solo las teclas ") + noteTxt (u.zoneLow) + U (" \xe2\x86\x92 ") + noteTxt (u.zoneLow + 11)
                                       + L (" changent la tonalit\xc3\xa9. Le reste du clavier est libre : le pad reste net.", " change the key. The rest of the keyboard is free: the pad stays put.", " cambian la tonalidad. El resto del teclado queda libre: el pad no se mueve.");
        else if (u.midiKeyMode == 1) info = L ("Le MIDI ne change jamais la tonalit\xc3\xa9 : seulement l'\xc3\xa9""cran, le crochet et les p\xc3\xa9""dales assign\xc3\xa9""es.", "MIDI never changes the key: only the screen, the hook and assigned pedals.", "El MIDI nunca cambia la tonalidad: solo la pantalla, el gancho y los pedales asignados.");
        else info = L ("Chaque note MIDI change la tonalit\xc3\xa9 (clavier r\xc3\xa9serv\xc3\xa9 \xc3\xa0 Spacepad).", "Every MIDI note changes the key (keyboard dedicated to Spacepad).", "Cada nota MIDI cambia la tonalidad (teclado reservado a Spacepad).");
        if (inPlugin)
            info << "\n" << L ("Dans votre logiciel de musique : armez la piste de Spacepad (ou moniteur sur \xc2\xab In \xc2\xbb) pour qu'il re\xc3\xa7oive le clavier et les p\xc3\xa9""dales.",
                               "In your music software: arm the Spacepad track (or set monitoring to \xe2\x80\x9cIn\xe2\x80\x9d) so it receives the keyboard and pedals.",
                               "En tu programa de m\xc3\xbasica: arma la pista de Spacepad (o monitor en \xc2\xab""In\xc2\xbb) para que reciba el teclado y los pedales.");
        g.drawFittedText (info, infoArea, juce::Justification::centredLeft, inPlugin ? 2 : 1, 0.8f);
        const int rowH = juce::jmin (40, midiListArea.getHeight() / actCount);
        for (int a = 0; a < actCount; ++a)
        {
            auto row = juce::Rectangle<int> (midiListArea.getX(), midiListArea.getY() + a * rowH, midiListArea.getWidth(), rowH).toFloat();
            g.setColour (theme::ink.withAlpha (0.06f));
            g.drawLine (row.getX(), row.getBottom(), row.getRight(), row.getBottom(), 1.0f);
            g.setColour (theme::text);
            g.setFont (theme::ui (13.5f));
            g.drawText (midiActionName (a), row.withWidth (300), juce::Justification::centredLeft);
            const int code = u.midiMap[(size_t) a];
            g.setColour (code >= 0 ? theme::green.darker (theme::light ? 0.7f : 0.0f) : theme::muted);
            g.setFont (theme::ui (13.0f, 1));
            g.drawText (midiCodeName (code), row.withX (row.getX() + 300).withWidth (240), juce::Justification::centredLeft);
        }
    }
    else if (page == 4)
    {
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::ui (12.5f));
        g.drawFittedText (L ("Quand le logiciel de musique enregistre, Spacepad garde tes actions (tonalit\xc3\xa9, Play/Stop, majeur/mineur, chants, crowds) \xc3\xa0 leur place dans le morceau. Quand il joue, Spacepad les rejoue. C'est gard\xc3\xa9 dans le projet.",
                             "When the music software records, Spacepad keeps your actions (key, Play/Stop, major/minor, songs, crowds) at their place in the song. When it plays, Spacepad replays them. Saved in the project.",
                             "Cuando el programa de m\xc3\xbasica graba, Spacepad guarda tus acciones (tonalidad, Play/Stop, mayor/menor, canciones, crowds) en su lugar en la canci\xc3\xb3n. Al reproducir, Spacepad las repite. Se guarda en el proyecto."),
                          recInfoArea, juce::Justification::topLeft, 2);
        const auto ev = proc.recording();
        auto lr = recListArea.toFloat();
        auto f = theme::ui (11.0f, 2); f.setExtraKerningFactor (0.16f);
        g.setFont (f);
        g.setColour (theme::muted);
        g.drawText (L ("ACTIONS ENREGISTR\xc3\x89""ES DANS CE PROJET \xc2\xb7 ", "ACTIONS RECORDED IN THIS PROJECT \xc2\xb7 ", "ACCIONES GRABADAS EN ESTE PROYECTO \xc2\xb7 ") + juce::String ((int) ev.size()), lr.removeFromTop (22), juce::Justification::centredLeft);
        if (ev.empty())
        {
            g.setFont (theme::ui (13.0f));
            g.drawFittedText (inPlugin ? L ("Rien pour l'instant : lance l'enregistrement dans ton logiciel de musique et joue sur Spacepad.", "Nothing yet: start recording in your music software and play Spacepad.", "Nada todav\xc3\xad""a: graba en tu programa de m\xc3\xbasica y toca Spacepad.")
                                       : L ("Cette fonction marche dans le plugin (Ableton, Logic\xe2\x80\xa6), pas dans l'application seule.", "This works in the plugin (Ableton, Logic\xe2\x80\xa6), not in the standalone app.", "Funciona en el plugin (Ableton, Logic\xe2\x80\xa6), no en la aplicaci\xc3\xb3n sola."),
                              lr.removeFromTop (40).toNearestInt(), juce::Justification::topLeft, 2);
        }
        const double bpb = juce::jmax (1.0, proc.beatsPerBar());
        const int rowsMax = (int) (lr.getHeight() / 30) - 1;
        for (int i = 0; i < (int) ev.size() && i < rowsMax; ++i)
        {
            auto row = lr.removeFromTop (30);
            const int bar = (int) std::floor (ev[(size_t) i].ppq / bpb) + 1;
            const double beat = std::fmod (ev[(size_t) i].ppq, bpb) + 1.0;
            g.setColour (theme::light ? juce::Colour (0xffb86a14) : juce::Colour (0xffffcf8a));
            g.setFont (theme::ui (13.0f, 2));
            g.drawText (L ("Mesure ", "Bar ", "Comp\xc3\xa1s ") + juce::String (bar) + (beat > 1.05 ? "." + juce::String ((int) beat) : juce::String()), row.removeFromLeft (140), juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.setFont (theme::ui (13.0f));
            g.drawText (SpacepadProcessor::describeEvent (ev[(size_t) i], proc.setlist(), proc.libraries()), row, juce::Justification::centredLeft, true);
            g.setColour (theme::ink.withAlpha (0.06f));
            g.drawLine (row.getX() - 140, row.getBottom(), row.getRight(), row.getBottom());
        }
        if ((int) ev.size() > rowsMax && rowsMax > 0)
        {
            g.setColour (theme::muted);
            g.setFont (theme::ui (12.5f));
            g.drawText (U ("\xe2\x80\xa6 ") + L ("et ", "and ", "y ") + juce::String ((int) ev.size() - rowsMax) + L (" autres", " more", " m\xc3\xa1s"), lr.removeFromTop (26), juce::Justification::centredLeft);
        }
    }
    else if (page == 3)
    {
        auto h = libHeadArea.toFloat();
        const auto all = proc.libraries().snapshot();
        int pads = 0; for (auto& l : all) if (l.type == LibType::Pad) ++pads;
        g.setColour (theme::text);
        g.setFont (theme::ui (14.0f, 1));
        g.drawText (juce::String (pads) + L (" librairie(s) de pads \xc2\xb7 ", " pad librarie(s) \xc2\xb7 ", " librer\xc3\xad""a(s) de pads \xc2\xb7 ") + juce::String ((int) all.size() - pads) + L (" de crowds", " crowd", " de crowds"),
                    h.withTrimmedTop (2).withHeight (20), juce::Justification::centredLeft);
        g.setColour (theme::muted);
        g.setFont (theme::ui (12.0f));
        g.drawText (L ("Supprimer retire la librairie de Spacepad (application et plugin). Vos fichiers restent sur l'ordinateur.",
                       "Delete removes the library from Spacepad (app and plugin). Your files stay on the computer.",
                       "Eliminar quita la librer\xc3\xad""a de Spacepad (aplicaci\xc3\xb3n y plugin). Tus archivos se quedan en el ordenador."),
                    h.withTrimmedTop (22).withHeight (18), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (theme::muted);
        g.setFont (theme::ui (11.0f, 2));
        g.drawText (L ("TRANSPORTER SUR UN AUTRE ORDINATEUR", "MOVE TO ANOTHER COMPUTER", "LLEVAR A OTRO ORDENADOR"), exportBtn.getBounds().translated (0, -24).withHeight (18).withWidth (500).toFloat(), juce::Justification::centredLeft);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::ui (13.0f));
        g.drawFittedText (L ("\xe2\x80\xa2 Exporter copie vos librairies et leurs sons dans un dossier (cl\xc3\xa9 USB, disque\xe2\x80\xa6). Importer les ajoute sur l'autre ordinateur.\n"
                             "\xe2\x80\xa2 Copier les sons : vos librairies restent disponibles m\xc3\xaame si vous d\xc3\xa9placez ou supprimez vos dossiers.\n"
                             "\xe2\x80\xa2 Sorties s\xc3\xa9par\xc3\xa9""es : dans votre logiciel de musique, activez la sortie \xc2\xab Crowds \xc2\xbb du plugin Spacepad. Le pad sort alors sur la sortie principale et les crowds sur leur propre sortie, pour l'ing\xc3\xa9nieur du son.",
                             "\xe2\x80\xa2 Export copies your libraries and their sounds into a folder (USB key, drive\xe2\x80\xa6). Import adds them on the other computer.\n"
                             "\xe2\x80\xa2 Copy sounds: your libraries stay available even if you move or delete your folders.\n"
                             "\xe2\x80\xa2 Separate outputs: in your music software, enable the \xe2\x80\x9c""Crowds\xe2\x80\x9d output of the Spacepad plugin. The pad then goes to the main output and the crowds to their own output, for the sound engineer.",
                             "\xe2\x80\xa2 Exportar copia tus librer\xc3\xad""as y sus sonidos en una carpeta (USB, disco\xe2\x80\xa6). Importar las a\xc3\xb1""ade en el otro ordenador.\n"
                             "\xe2\x80\xa2 Copiar los sonidos: tus librer\xc3\xad""as siguen disponibles aunque muevas o borres tus carpetas.\n"
                             "\xe2\x80\xa2 Salidas separadas: en tu programa de m\xc3\xba""sica, activa la salida \xc2\xab""Crowds\xc2\xbb del plugin Spacepad. El pad sale por la salida principal y los crowds por la suya, para el t\xc3\xa9""cnico de sonido."),
                          infoArea, juce::Justification::topLeft, 10);
        g.setColour (inPlugin ? theme::accent : theme::muted);
        g.setFont (theme::ui (12.5f, inPlugin ? 1 : 0));
        g.drawFittedText (inPlugin ? L ("Vous \xc3\xaates dans le plugin : la carte son, la fr\xc3\xa9quence, la latence, le tempo et le MIDI sont r\xc3\xa9gl\xc3\xa9s par votre logiciel de musique (",
                                        "You are in the plugin: the audio device, sample rate, latency, tempo and MIDI are set by your music software (",
                                        "Est\xc3\xa1s en el plugin: la tarjeta de sonido, la frecuencia, la latencia, el tempo y el MIDI los ajusta tu programa de m\xc3\xbasica (") + juce::PluginHostType().getHostDescription() + L ("). Spacepad suit ses r\xc3\xa9glages.", "). Spacepad follows its settings.", "). Spacepad sigue sus ajustes.")
                                   : L ("Application seule : la carte son et la sortie se r\xc3\xa8glent avec le bouton \xc2\xab Options \xc2\xbb en haut de la fen\xc3\xaatre.", "Standalone app: the audio device and output are set with the \xe2\x80\x9cOptions\xe2\x80\x9d button at the top of the window.", "Aplicaci\xc3\xb3n sola: la tarjeta de sonido y la salida se ajustan con el bot\xc3\xb3n \xc2\xab""Options\xc2\xbb arriba de la ventana."),
                          infoArea.withTrimmedTop (170).withHeight (44), juce::Justification::topLeft, 2);
    }
    for (auto& gr : groups)
    {
        label (gr.area, gr.title);
        auto sg = gr.area.withTrimmedTop (22).withHeight (36).toFloat();
        g.setColour (theme::ink.withAlpha (0.06f));
        g.fillRoundedRectangle (sg, 18.0f);
        for (int i = 0; i < (int) gr.choices.size(); ++i)
        {
            auto cr = gr.choices[(size_t) i].r.toFloat();
            const bool on = i == *gr.value;
            if (on) { g.setColour (acc); g.fillRoundedRectangle (cr, cr.getHeight() * 0.5f); }
            g.setColour (on ? juce::Colour (0xff04121c) : theme::text.withAlpha (0.85f));
            g.setFont (theme::ui (12.5f, on ? 2 : 1));
            g.drawFittedText (gr.choices[(size_t) i].label, cr.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
    }
    g.setColour (theme::muted);
    g.setFont (theme::ui (12.0f));
    g.drawText (L ("R\xc3\xa9glages enregistr\xc3\xa9s, repris dans l'application et le plugin.", "Settings are saved and shared by the app and the plugin.", "Ajustes guardados, compartidos por la aplicaci\xc3\xb3n y el plugin."),
                card().reduced (32, 24).removeFromBottom (44).withTrimmedRight (300).toFloat(), juce::Justification::centredLeft, true);
}
}
