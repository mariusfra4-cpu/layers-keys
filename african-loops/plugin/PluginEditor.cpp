#include "PluginEditor.h"
#include "Lang.h"

using namespace africanloops;
namespace col = ui::col;

namespace {
constexpr int X0 = 34;  // largeur des joues en bois

juce::String fileStem(const std::string& path) {
    return juce::File::createFileWithoutCheckingPath(juce::String::fromUTF8(path.c_str())).getFileNameWithoutExtension();
}

juce::String bpmText(double bpm) {
    if (bpm <= 0) return "--";
    return std::abs(bpm - std::round(bpm)) < 0.005 ? juce::String((int)std::lround(bpm)) : juce::String(bpm, 2);
}

void setParam(AfricanLoopsProcessor& p, const juce::String& id, float plainValue) {
    if (auto* prm = p.params.getParameter(id)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(plainValue));
        prm->endChangeGesture();
    }
}

float paramValue(const AfricanLoopsProcessor& p, const juce::String& id) {
    return p.params.getRawParameterValue(id)->load();
}

void styleKnob(juce::Slider& s, bool bipolar = false) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.getProperties().set("bipolar", bipolar);
    s.setPopupDisplayEnabled(true, true, nullptr);
    s.setMouseDragSensitivity(180);
}

// Petite fenêtre « nom » (nouveau style, renommer, setup…).
void askText(const juce::String& title, const juce::String& prompt, const juce::String& initial,
             std::function<void(const juce::String&)> done) {
    auto* w = new juce::AlertWindow(title, prompt, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("t", initial);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(TR("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, done](int res) {
                           auto t = w->getTextEditorContents("t").trim();
                           if (res == 1 && t.isNotEmpty()) done(t);
                       }),
                       true);
}

// Lance le glisser-déposer d'un fichier vers le logiciel de musique.
void dragOut(const juce::File& f, juce::Component* source) {
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, source);
}
}  // namespace

juce::Colour kindColour(const juce::String& k) {
    if (k == perc::Kick) return juce::Colour(0xffff7a45);
    if (k == perc::Snare) return juce::Colour(0xffffd04a);
    if (k == perc::Hat) return juce::Colour(0xffe8f06a);
    if (k == perc::Hand) return juce::Colour(0xffff9fd0);
    if (k == perc::Talking) return juce::Colour(0xffc79bff);
    return col::amber;
}

// ====================================================================== forme d'onde

WaveformView::WaveformView(AfricanLoopsProcessor& p) : proc(p) {
    setOpaque(true);
    setTooltip(juce::String::fromUTF8("Attrapez la forme d'onde et déposez-la sur une piste de votre logiciel de musique"));
}

void WaveformView::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    g.fillAll(col::lcd);
    g.setColour(col::line);
    g.drawLine(b.getX(), b.getY() + 0.5f, b.getRight(), b.getY() + 0.5f);
    auto c = proc.current();
    const float mid = b.getCentreY(), half = b.getHeight() * 0.42f;
    const float play = (float)proc.playheadPosition();
    if (!c || c->overview.empty()) {
        g.setColour(col::lcdDim.withAlpha(0.6f));
        g.setFont(ui::font(11.f));
        g.drawText(TR("Choisissez une loop, ou déposez un fichier WAV / MP3 ici"), b,
                   juce::Justification::centred);
        return;
    }
    const auto& ov = c->overview;
    float mx = 1e-6f;
    for (float v : ov) mx = std::max(mx, v);
    const int w = (int)b.getWidth();
    for (int x = 0; x < w; x += 2) {
        int i0 = (int)((juce::int64)x * (juce::int64)ov.size() / w), i1 = (int)((juce::int64)(x + 2) * (juce::int64)ov.size() / w);
        float v = 0;
        for (int i = i0; i < std::max(i0 + 1, i1) && i < (int)ov.size(); ++i) v = std::max(v, ov[(size_t)i]);
        float a = std::max(0.6f, v / mx * half);
        bool past = (float)x / w <= play && proc.isPlaying();
        g.setColour(past ? col::amber : juce::Colour(0xff8a6230));
        g.fillRect(b.getX() + x, mid - a, 1.2f, a * 2);
    }
    if (proc.isPlaying()) {
        float px = b.getX() + play * b.getWidth();
        g.setColour(juce::Colours::white);
        g.fillRect(px - 0.75f, b.getY() + 1, 1.5f, b.getHeight() - 1);
    }
    auto hint = juce::Rectangle<float>(b.getRight() - 190, b.getY() + 3, 184, 12);
    g.setColour(col::lcd.withAlpha(0.8f));
    g.fillRoundedRectangle(hint, 2);
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText(TR("GLISSER VERS VOTRE LOGICIEL  >>"), hint, juce::Justification::centred);
}

void WaveformView::mouseDown(const juce::MouseEvent&) { dragStarted = false; }

void WaveformView::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    dragOut(proc.bounceToFile(-1), this);
}

// ====================================================================== navigateur

BrowserPanel::BrowserPanel(AfricanLoopsProcessor& p) : proc(p), wave(p) {
    for (juce::ListBox* lb : {static_cast<juce::ListBox*>(&catList), &loopList}) {
        lb->setRowHeight(22);
        lb->setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        lb->setOutlineThickness(0);
        addAndMakeVisible(*lb);
    }
    search.setTextToShowWhenEmpty(TR("Rechercher une loop…"), col::lcdDim.withAlpha(0.7f));
    search.setFont(ui::font(12.f));
    search.setIndents(8, 4);
    search.onTextChange = [this] { rebuildLoops(); };
    search.onEscapeKey = [this] { search.clear(); rebuildLoops(); };
    addAndMakeVisible(search);
    folderButton.setTooltip(juce::String::fromUTF8("Choisir le dossier de vos loops : elles sont toutes analysées et classées par style"));
    folderButton.onClick = [this] { chooseFolder(); };
    fileButton.setTooltip(juce::String::fromUTF8("Ouvrir un fichier WAV ou MP3"));
    fileButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible(folderButton);
    addAndMakeVisible(fileButton);
    styleButton.setTooltip(juce::String::fromUTF8("Créer un nouveau style"));
    styleButton.onClick = [this] { newStyle(); };
    importButton.setTooltip(juce::String::fromUTF8("Copier des loops (WAV, MP3) dans un style"));
    importButton.onClick = [this] { chooseImport(); };
    addAndMakeVisible(styleButton);
    addAndMakeVisible(importButton);
    addAndMakeVisible(wave);
    loopList.setMultipleSelectionEnabled(false);
    catList.canDrop = [this](int row) { return isRealStyle(row); };
    catList.onDrop = [this](int row, const juce::String& path) {
        if (isRealStyle(row)) proc.setCategory(path, categories[row]);
    };
    refreshLibrary();
}

juce::String BrowserPanel::favoritesLabel() { return TR("★ Favoris"); }

// ligne 0 : Favoris, ligne 1 : toutes les loops, ensuite les vrais styles
bool BrowserPanel::isRealStyle(int row) const { return row >= 2 && row < categories.size(); }

void BrowserPanel::newStyle() {
    juce::Component::SafePointer<BrowserPanel> safe(this);
    askText(TR("Nouveau style"), TR("Nom du style :"), "", [safe](const juce::String& n) {
        if (safe && safe->proc.addStyle(n)) {
            safe->refreshLibrary();
            int r = safe->categories.indexOf(n);
            if (r >= 0) safe->catList.selectRow(r);
        }
    });
}

void BrowserPanel::importInto(const juce::String& style) {
    chooser = std::make_unique<juce::FileChooser>(TR("Loops à copier dans « ") + style + TR(" »"),
                                                  juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.wave;*.mp3");
    juce::Component::SafePointer<BrowserPanel> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectMultipleItems,
                         [safe, style](const juce::FileChooser& fc) {
                             if (safe) safe->proc.importLoops(fc.getResults(), style);
                         });
}

void BrowserPanel::chooseImport() {
    if (!lib) { chooseFolder(); return; }  // pas encore de dossier de loops
    const int r = catList.getSelectedRow();
    if (isRealStyle(r) && categories[r] != juce::String::fromUTF8(kUncategorized)) {
        importInto(categories[r]);
        return;
    }
    // aucun style choisi : on demande lequel
    juce::PopupMenu m;
    m.addSectionHeader(TR("Importer dans le style…"));
    for (int i = 2; i < categories.size(); ++i)
        if (categories[i] != juce::String::fromUTF8(kUncategorized)) m.addItem(100 + i, categories[i]);
    m.addSeparator();
    m.addItem(1, TR("Nouveau style…"));
    juce::Component::SafePointer<BrowserPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&importButton), [safe](int res) {
        if (!safe || res == 0) return;
        if (res >= 100) { safe->importInto(safe->categories[res - 100]); return; }
        askText(TR("Nouveau style"), TR("Nom du style :"), "", [safe](const juce::String& n) {
            if (safe && safe->proc.addStyle(n)) safe->importInto(n);
        });
    });
}

juce::var BrowserPanel::getDragSourceDescription(const juce::SparseSet<int>& sel) {
    if (sel.size() != 1) return {};
    const int r = sel[0];
    if (r < 0 || r >= (int)rows.size()) return {};
    return "loop:" + juce::String::fromUTF8(rows[(size_t)r]->path.c_str());
}

void StyleListBox::itemDragMove(const SourceDetails& d) {
    const int r = getRowContainingPosition(d.localPosition.x, d.localPosition.y);
    const int nr = (canDrop && canDrop(r)) ? r : -1;
    if (nr != dropRow) { dropRow = nr; repaint(); }
}

void StyleListBox::itemDropped(const SourceDetails& d) {
    const int r = getRowContainingPosition(d.localPosition.x, d.localPosition.y);
    dropRow = -1;
    repaint();
    if (onDrop && canDrop && canDrop(r)) onDrop(r, d.description.toString().fromFirstOccurrenceOf("loop:", false, false));
}

BrowserPanel::~BrowserPanel() {
    catList.setModel(nullptr);
    loopList.setModel(nullptr);
}

void BrowserPanel::chooseFolder() {
    chooser = std::make_unique<juce::FileChooser>(TR("Dossier de vos loops"), proc.libraryFolder());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f.isDirectory()) proc.scanFolderAsync(f);
                         });
}

void BrowserPanel::chooseFile() {
    chooser = std::make_unique<juce::FileChooser>(TR("Ouvrir une loop"), proc.libraryFolder(), "*.wav;*.wave;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f.existsAsFile()) proc.loadLoopAsync(f);
                         });
}

void BrowserPanel::refreshLibrary() {
    const auto previous = selectedCategory();
    lib = proc.library();
    categories.clear();
    categoryCounts.clear();
    categories.add(favoritesLabel());
    categoryCounts.push_back(lib ? lib->favoriteCount() : 0);
    categories.add(TR("Toutes les loops"));
    categoryCounts.push_back(lib ? (int)lib->entries.size() : 0);
    if (lib)
        for (const auto& c : lib->categories()) {
            categories.add(juce::String::fromUTF8(c.name.c_str()));
            categoryCounts.push_back(c.count);
        }
    catList.updateContent();
    int sel = previous.isEmpty() ? (catList.getSelectedRow() == 0 ? 0 : 1) : categories.indexOf(previous);
    if (sel < 0) sel = 1;
    catList.selectRow(sel, true, true);
    catList.scrollToEnsureRowIsOnscreen(sel);
    rebuildLoops();
    repaint();
}

juce::String BrowserPanel::selectedCategory() const {
    int r = catList.getSelectedRow();
    return isRealStyle(r) ? categories[r] : juce::String();
}

void BrowserPanel::rebuildLoops() {
    rows.clear();
    if (lib) {
        rows = lib->search(selectedCategory().toStdString(), search.getText().toStdString());
        if (catList.getSelectedRow() == 0)  // ★ Favoris
            rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Entry* e) { return !e->favorite; }), rows.end());
    }
    loopList.updateContent();
    // garde la loop jouée en surbrillance
    int sel = -1;
    if (auto c = proc.current(); c && lib)
        for (size_t i = 0; i < rows.size(); ++i)
            if (juce::File(juce::String::fromUTF8(lib->root.c_str())).getChildFile(juce::String::fromUTF8(rows[i]->path.c_str())) == c->file)
                sel = (int)i;
    keyboardLoad = false;
    if (sel >= 0) {
        loopList.selectRow(sel, true, true);
        loopList.scrollToEnsureRowIsOnscreen(sel);  // la loop jouée reste visible
    }
    else loopList.deselectAllRows();
    loopList.repaint();
}

void BrowserPanel::CategoryModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= owner.categories.size()) return;
    if (selected) {
        g.setGradientFill(juce::ColourGradient(col::amber.withAlpha(0.3f), 0, 0, col::amber.withAlpha(0.06f), (float)w, 0, false));
        g.fillRect(0, 0, w, h);
        g.setColour(col::amber);
        g.fillRect(0, 0, 3, h);
    }
    if (row == owner.catList.dropRow) {  // une loop survole ce style
        g.setColour(col::amber.withAlpha(0.35f));
        g.fillRect(0, 0, w, h);
        g.setColour(col::amber);
        g.drawRect(0, 0, w, h, 1);
    }
    g.setFont(ui::font(12.f));
    g.setColour(selected ? juce::Colours::white : col::lcdText);
    if (row == 0) {
        g.setColour(col::amber);
        g.drawText(juce::String::fromUTF8("★"), 10, 0, 14, h, juce::Justification::centredLeft);
        g.setColour(selected ? juce::Colours::white : col::lcdText);
        g.drawText(TR("Favoris"), 26, 0, w - 60, h, juce::Justification::centredLeft, true);
    } else {
        g.drawText(row == 1 || owner.categories[row] == juce::String::fromUTF8(kUncategorized) ? lang::tr(owner.categories[row]) : owner.categories[row], 10, 0, w - 44, h, juce::Justification::centredLeft, true);
    }
    g.setColour(col::lcdDim);
    g.drawText(juce::String(owner.categoryCounts[(size_t)row]), w - 40, 0, 32, h, juce::Justification::centredRight);
}

void BrowserPanel::CategoryModel::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    // clic droit sur un style : tout gérer
    if (!e.mods.isPopupMenu()) return;
    auto& o = owner;
    const bool real = o.isRealStyle(row) && o.categories[row] != juce::String::fromUTF8(kUncategorized);
    const juce::String name = real ? o.categories[row] : juce::String();
    juce::PopupMenu m;
    if (real) {
        m.addSectionHeader(name);
        m.addItem(1, TR("Renommer ce style…"));
        m.addItem(3, TR("Importer des loops dans ce style…"));
        m.addItem(4, TR("Ouvrir le dossier"), o.proc.styleFolder(name).isDirectory());
        m.addSeparator();
    }
    m.addItem(5, TR("Nouveau style…"), o.lib != nullptr);
    if (real) {
        m.addSeparator();
        m.addItem(2, TR("Supprimer ce style (ses loops vont dans « Non classés »)"));
    }
    juce::Component::SafePointer<BrowserPanel> safe(&o);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&o.catList), [safe, name](int r) {
        if (!safe || r == 0) return;
        switch (r) {
            case 1:
                askText(TR("Renommer le style"), TR("Nouveau nom :"), name,
                        [safe, name](const juce::String& to) {
                            if (safe && to != name) safe->proc.renameCategory(name, to);
                        });
                break;
            case 2: safe->proc.removeCategory(name); break;
            case 3: safe->importInto(name); break;
            case 4: safe->proc.styleFolder(name).startAsProcess(); break;
            case 5: safe->newStyle(); break;
            default: break;
        }
    });
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= (int)rows.size()) return;
    const Entry& e = *rows[(size_t)row];
    if (selected) {
        g.setColour(col::amber.withAlpha(0.14f));
        g.fillRect(0, 0, w, h);
    }
    g.setColour(juce::Colour(0xff1f1810));
    g.fillRect(0, h - 1, w, 1);
    const bool bad = !e.error.empty();
    g.setFont(ui::font(12.f));
    g.setColour(bad ? col::red.withAlpha(0.8f) : selected ? juce::Colours::white : col::lcdText);
    juce::String name = (selected && proc.isPlaying() ? juce::String::fromUTF8("▶ ") : juce::String()) + fileStem(e.path);
    const int chipsW = 162, bpmW = 40;
    // étoile : un clic dessus = favori
    g.setFont(ui::font(14.f));
    g.setColour(e.favorite ? col::amber : juce::Colour(0xff4a3c2c));
    g.drawText(juce::String::fromUTF8(e.favorite ? "★" : "☆"), 4, 0, 18, h, juce::Justification::centred);
    g.setFont(ui::font(12.f));
    g.setColour(bad ? col::red.withAlpha(0.8f) : selected ? juce::Colours::white : col::lcdText);
    g.drawText(name, 24, 0, w - chipsW - bpmW - 30, h, juce::Justification::centredLeft, true);
    if (bad) {
        g.setFont(ui::font(10.f));
        g.drawText(TR("illisible (survoler)"), w - chipsW - 4, 0, chipsW, h, juce::Justification::centredRight);
        return;
    }
    ui::drawGlowText(g, bpmText(e.info.tempo.bpm), juce::Rectangle<float>((float)(w - chipsW - bpmW - 6), 0, (float)bpmW, (float)h),
                     ui::font(11.f, true), col::amber, juce::Justification::centredRight);
    // pastilles des percussions trouvées
    float x = (float)(w - chipsW);
    for (const auto& p : e.info.percussions) {
        juce::String k = juce::String::fromUTF8(p.kind.c_str());
        juce::String shortName = k == perc::Kick ? "KICK" : k == perc::Snare ? "SNR" : k == perc::Hat ? "HAT" : k == perc::Hand ? "DJB" : "TALK";
        float cw = shortName.length() * 6.0f + 8;
        if (x + cw > w - 2) break;
        auto r = juce::Rectangle<float>(x, h * 0.5f - 7, cw, 14);
        g.setColour(kindColour(k));
        g.drawRoundedRectangle(r, 3, 1);
        g.setFont(ui::font(8.5f, true));
        g.drawText(shortName, r, juce::Justification::centred);
        x += cw + 3;
    }
}

juce::String BrowserPanel::getTooltipForRow(int row) {
    if (row < 0 || row >= (int)rows.size()) return {};
    const Entry& e = *rows[(size_t)row];
    if (!e.error.empty()) return TR("Illisible : ") + lang::tr(juce::String::fromUTF8(e.error.c_str()));
    return juce::String::fromUTF8(e.path.c_str());
}

void BrowserPanel::loadRow(int row) {
    if (!lib || row < 0 || row >= (int)rows.size()) return;
    auto f = juce::File(juce::String::fromUTF8(lib->root.c_str())).getChildFile(juce::String::fromUTF8(rows[(size_t)row]->path.c_str()));
    if (auto c = proc.current(); c && c->file == f) return;
    proc.loadLoopAsync(f);
}

void BrowserPanel::step(int delta) {
    if (rows.empty()) return;
    int r = loopList.getSelectedRow();
    r = r < 0 ? (delta > 0 ? 0 : (int)rows.size() - 1) : juce::jlimit(0, (int)rows.size() - 1, r + delta);
    keyboardLoad = false;
    loopList.selectRow(r);
    loopList.scrollToEnsureRowIsOnscreen(r);
    loadRow(r);
}

void BrowserPanel::selectedRowsChanged(int row) {
    // flèches du clavier : la loop se charge aussitôt
    if (keyboardLoad && row >= 0) loadRow(row);
    keyboardLoad = true;
}

void BrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= (int)rows.size()) return;
    const std::string path = rows[(size_t)row]->path;
    const bool fav = rows[(size_t)row]->favorite;
    if (!e.mods.isPopupMenu()) {
        if (e.x < 22) {  // clic sur l'étoile : favori ou non, sans changer de loop
            proc.setFavorite(juce::String::fromUTF8(path.c_str()), !fav);
            return;
        }
        loadRow(row);
        return;
    }
    // clic droit : favori, ranger dans un style, montrer le fichier
    juce::PopupMenu m, into;
    for (int i = 2; i < categories.size(); ++i)
        into.addItem(100 + i, categories[i] == juce::String::fromUTF8(kUncategorized) ? lang::tr(categories[i]) : categories[i]);
    into.addSeparator();
    into.addItem(1, TR("Nouveau style…"));
    m.addSectionHeader(fileStem(path));
    m.addItem(2, fav ? TR("☆  Retirer des favoris") : TR("★  Ajouter aux favoris"));
    m.addSubMenu(TR("Ranger dans le style"), into);
    m.addItem(3, TR("Montrer le fichier"));
    juce::Component::SafePointer<BrowserPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loopList), [safe, path, fav](int r) {
        if (!safe || r == 0) return;
        auto& self = *safe;
        const auto p = juce::String::fromUTF8(path.c_str());
        if (r >= 100) { self.proc.setCategory(p, self.categories[r - 100]); return; }
        if (r == 2) { self.proc.setFavorite(p, !fav); return; }
        if (r == 3) {
            if (self.lib) juce::File(juce::String::fromUTF8(self.lib->root.c_str())).getChildFile(p).revealToUser();
            return;
        }
        askText(TR("Nouveau style"), TR("Nom du style :"), "", [safe, p](const juce::String& n) {
            if (safe) safe->proc.setCategory(p, n);
        });
    });
}

void BrowserPanel::resized() {
    auto b = getLocalBounds();
    auto top = b.removeFromTop(36).reduced(10, 7);
    fileButton.setBounds(top.removeFromRight(70));
    top.removeFromRight(5);
    folderButton.setBounds(top.removeFromRight(70));
    top.removeFromRight(5);
    importButton.setBounds(top.removeFromRight(72));
    top.removeFromRight(5);
    styleButton.setBounds(top.removeFromRight(64));
    top.removeFromRight(8);
    search.setBounds(top);
    titleArea = b.removeFromTop(28);
    wave.setBounds(b.removeFromBottom(38));
    catList.setBounds(b.removeFromLeft(168).reduced(0, 4));
    b.removeFromLeft(1);
    loopList.setBounds(b.reduced(0, 4));
}

void BrowserPanel::paint(juce::Graphics& g) {
    ui::drawLcd(g, getLocalBounds().toFloat());
    // barre de titre : la loop en cours
    auto t = titleArea.toFloat();
    g.setColour(col::amber.withAlpha(0.08f));
    g.fillRect(t);
    g.setColour(col::line);
    g.drawLine(t.getX(), t.getY(), t.getRight(), t.getY());
    g.drawLine(t.getX(), t.getBottom(), t.getRight(), t.getBottom());
    juce::String title;
    juce::Colour tc = juce::Colours::white;
    auto c = proc.current();
    if (proc.isLoading()) {
        title = TR("Analyse de la loop… (tempo, percussions, séparation)");
        tc = col::amber;
    } else if (auto err = proc.loadError(); err.isNotEmpty()) {
        title = lang::tr(err);
        tc = col::red;
    } else if (c) {
        title = bpmText(c->info.tempo.bpm) + " BPM  -  " + c->file.getFileNameWithoutExtension();
    } else {
        title = TR("Bienvenue dans African Loops Pro");
    }
    ui::drawGlowText(g, title, t.reduced(10, 0), ui::font(14.f, true), tc);
    // séparation entre styles et loops
    g.setColour(col::line);
    g.fillRect(catList.getRight(), catList.getY(), 1, catList.getHeight());
    // bibliothèque vide / analyse en cours
    if (proc.isScanning() && lib) {
        // simple vérification du dossier : la liste reste utilisable
        int d, n;
        proc.scanProgress(d, n);
        if (n > 0) {
            g.setColour(col::amber);
            g.setFont(ui::font(9.5f, true));
            g.drawText(TR("Nouvelles loops : ") + juce::String(d) + " / " + juce::String(n),
                       wave.getBounds().translated(0, -14).withHeight(12).reduced(8, 0), juce::Justification::left);
        }
    } else if (proc.isScanning()) {
        int d, n;
        proc.scanProgress(d, n);
        auto r = loopList.getBounds().toFloat();
        g.setColour(col::lcd.withAlpha(0.85f));
        g.fillRect(r);
        ui::drawGlowText(g, TR("Analyse du dossier… ") + juce::String(d) + " / " + juce::String(n),
                         r.withTrimmedBottom(r.getHeight() * 0.3f), ui::font(13.f, true), col::amber);
        auto bar = r.withSizeKeepingCentre(r.getWidth() * 0.7f, 8).translated(0, 14);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(bar, 4);
        g.setColour(col::amber);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * (n > 0 ? (float)d / n : 0.f)), 4);
        g.setColour(col::lcdDim);
        g.setFont(ui::font(10.f));
        g.drawText(TR("Fait une seule fois : ensuite chaque loop se charge aussitôt"),
                   bar.translated(0, 18).withHeight(16).expanded(60, 0), juce::Justification::centred);
    } else if (!lib) {
        auto r = loopList.getBounds().toFloat().reduced(10);
        g.setColour(col::lcdDim);
        g.setFont(ui::font(12.f));
        g.drawFittedText(TR("Cliquez sur DOSSIER pour choisir le dossier de vos loops.\n"
                                                "Elles seront analysées (tempo, percussions) et classées par style."),
                         r.toNearestInt(), juce::Justification::centred, 4);
    } else if (rows.empty()) {
        g.setColour(col::lcdDim);
        g.setFont(ui::font(12.f));
        g.drawText(TR("Aucune loop"), loopList.getBounds(), juce::Justification::centred);
    }
}

// ====================================================================== tempo

BpmDisplay::BpmDisplay(AfricanLoopsProcessor& p) : proc(p) {
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity(400);
    setVelocityBasedMode(false);
    setTooltip(juce::String::fromUTF8("Tempo : glissez vers le haut ou le bas, double-cliquez pour le taper. "
                                      "PRESET allumé : la loop joue à son propre tempo."));
    // on règle le tempo alors que la loop joue à son propre tempo : on part de
    // celui-ci et on passe en tempo global
    onDragStart = [this] { leavePreset(); };
}

void BpmDisplay::leavePreset() {
    if (paramValue(proc, ids::presetBpm) < 0.5f) return;
    if (auto c = proc.current(); c && c->info.tempo.bpm > 0) setValue(c->info.tempo.bpm, juce::sendNotificationSync);
    setParam(proc, ids::presetBpm, 0);
}

void BpmDisplay::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    ui::drawRecess(g, b, 6);
    auto lcd = b.reduced(4);
    ui::drawLcd(g, lcd);
    ui::drawGlowText(g, bpmText(proc.displayedBPM()), lcd, ui::font(lcd.getHeight() * 0.72f, true),
                     isEnabled() ? col::amber : col::lcdText);
}

void BpmDisplay::mouseDoubleClick(const juce::MouseEvent&) {
    if (!isEnabled()) return;
    auto* w = new juce::AlertWindow(TR("Global BPM"), TR("Tempo (40 à 240) :"), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("bpm", bpmText(proc.displayedBPM()));
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(TR("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BpmDisplay> safe(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int r) {
                           double v = w->getTextEditorContents("bpm").replaceCharacter(',', '.').getDoubleValue();
                           if (safe && r == 1 && v >= 40 && v <= 240) {
                               safe->leavePreset();
                               safe->setValue(v, juce::sendNotificationSync);
                           }
                       }),
                       true);
}

// ====================================================================== tranche du mixer

ChannelStrip::ChannelStrip(AfricanLoopsProcessor& p, int index) : proc(p), idx(index) {
    styleKnob(pan, true);
    pan.setDoubleClickReturnValue(true, 0.0);
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setPopupDisplayEnabled(true, true, nullptr);
    fader.setDoubleClickReturnValue(true, 1.0);
    for (auto* c : std::initializer_list<juce::Component*>{&pan, &fader, &mute, &solo}) addAndMakeVisible(c);
    mute.setClickingTogglesState(true);
    solo.setClickingTogglesState(true);
    panAtt = std::make_unique<SliderAtt>(p.params, ids::pan(idx), pan);
    faderAtt = std::make_unique<SliderAtt>(p.params, ids::gain(idx), fader);
    muteAtt = std::make_unique<ButtonAtt>(p.params, ids::mute(idx), mute);
    soloAtt = std::make_unique<ButtonAtt>(p.params, ids::solo(idx), solo);
    setTooltip(juce::String::fromUTF8("Attrapez le numéro de la piste pour la déposer seule dans votre logiciel"));
}

void ChannelStrip::resized() {
    pan.setBounds(14, 36, 50, 50);
    mute.setBounds(12, 92, 26, 19);
    solo.setBounds(42, 92, 26, 19);
    fader.setBounds(4, 116, 40, 176);
    handle = {0, 296, getWidth(), 20};
}

void ChannelStrip::updateMeter() {
    float v = proc.trackPeak(idx);
    if (std::abs(v - level) > 0.002f) {
        level = v;
        repaint(52, 126, 14, 162);
    }
}

void ChannelStrip::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    const bool empty = name.isEmpty();
    g.setColour(juce::Colours::white.withAlpha(0.025f));
    g.fillRoundedRectangle(b, 5);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
    // nom de la piste
    auto nr = juce::Rectangle<float>(6, 8, b.getWidth() - 12, 22);
    ui::drawRecess(g, nr, 3);
    ui::drawLcd(g, nr.reduced(2));
    juce::String shown = name == perc::Hat ? "HI-HAT" : name == perc::Hand ? "DJEMBE" : name == perc::Talking ? "TALKING" : name;
    ui::drawGlowText(g, empty ? TR("—") : shown, nr.reduced(4, 0), ui::font(9.5f, true),
                     empty ? col::lcdDim : kindColour(name), juce::Justification::centred);
    // graduations du fader
    g.setFont(ui::font(7.f));
    g.setColour(col::engraveDark);
    for (auto [label, gain] : {std::pair<const char*, double>{"+3", 1.41}, {"0", 1.0}, {"-6", 0.5}, {"-12", 0.25},
                               {"-24", 0.063}, {"-48", 0.004}}) {
        double prop = fader.valueToProportionOfLength(gain);
        float y = (float)fader.getY() + 15 + (float)(1 - prop) * (fader.getHeight() - 30);
        g.drawText(label, 0, (int)y - 5, 14, 10, juce::Justification::centredLeft);
    }
    // vumètre
    ui::drawLedBar(g, {52, 126, 14, 160}, level, 30);
    // poignée de glisser-déposer + numéro
    g.setColour(dragStarted ? col::amber : col::engrave);
    g.setFont(ui::font(12.f, true));
    g.drawText(TR("☰ ") + juce::String(idx + 1), handle, juce::Justification::centred);
    if (empty) {
        g.setColour(juce::Colour(0xff171615).withAlpha(0.45f));
        g.fillRoundedRectangle(b, 5);
    }
}

void ChannelStrip::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted || name.isEmpty() || !handle.contains(e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    repaint();
    dragOut(proc.bounceToFile(idx), this);
}

// ====================================================================== signature

void SignatureTable::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    ui::drawRecess(g, b, 6);
    auto lcd = b.reduced(4);
    ui::drawLcd(g, lcd);
    const int feel = (int)paramValue(proc, ids::feel), sig = (int)paramValue(proc, ids::signature);
    const char* feels[] = {"STANDARD", "DOUBLE", "HALF"};
    const char* sigs[] = {"4/4", "3/4", "6/8"};
    const float rh = lcd.getHeight() / 3, cw = lcd.getWidth() / 2;
    for (int i = 0; i < 3; ++i) {
        auto l = juce::Rectangle<float>(lcd.getX(), lcd.getY() + i * rh, cw, rh);
        auto r = l.translated(cw, 0);
        if (i == feel) {
            g.setColour(col::amber.withAlpha(0.22f));
            g.fillRect(l);
            g.setColour(col::amber);
            g.fillRect(l.withWidth(3));
        }
        if (i == sig) {
            g.setColour(col::amber.withAlpha(0.22f));
            g.fillRect(r);
        }
        ui::drawGlowText(g, feels[i], l, ui::font(10.f, true), i == feel ? juce::Colours::white : col::lcdDim);
        ui::drawGlowText(g, sigs[i], r, ui::font(10.f, true), i == sig ? juce::Colours::white : col::lcdDim);
    }
}

void SignatureTable::mouseDown(const juce::MouseEvent& e) {
    auto lcd = getLocalBounds().toFloat().reduced(4);
    int row = juce::jlimit(0, 2, (int)((e.position.y - lcd.getY()) / (lcd.getHeight() / 3)));
    if (e.position.x < lcd.getCentreX()) setParam(proc, ids::feel, (float)row);
    else setParam(proc, ids::signature, (float)row);
    repaint();
}

// ====================================================================== éditeur

AfricanLoopsEditor::AfricanLoopsEditor(AfricanLoopsProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), browser(p), bpm(p), signature(p) {
    setLookAndFeel(&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);  // menus et fenêtres
    addAndMakeVisible(face);

    // texture fine de la façade (métal brossé)
    noise = juce::Image(juce::Image::ARGB, 256, 256, true);
    juce::Random rnd(7);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            noise.setPixelAt(x, y, juce::Colours::white.withAlpha((float)rnd.nextInt(24) / 255.f));

    for (auto* c : std::initializer_list<juce::Component*>{&browser, &bpm, &presetBpm, &hostSync, &linkButton, &lp, &hp, &warmth, &masterFx,
                                                            &sendFx, &modFx, &fade, &master, &masterPan, &metronome, &setupSave, &setupList, &settings,
                                                            &masterMute, &mono, &signature, &stop, &play})
        face.addAndMakeVisible(c);
    for (int i = 0; i < kNumTracks; ++i) {
        strips.push_back(std::make_unique<ChannelStrip>(p, i));
        face.addAndMakeVisible(*strips.back());
    }

    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::bpm, bpm));
    styleKnob(lp);
    styleKnob(hp);
    styleKnob(warmth);
    lp.setDoubleClickReturnValue(true, 1.0);
    hp.setDoubleClickReturnValue(true, 0.0);
    warmth.setDoubleClickReturnValue(true, 0.0);
    styleKnob(master);
    master.setDoubleClickReturnValue(true, 1.0);
    styleKnob(masterPan, true);
    masterPan.setDoubleClickReturnValue(true, 0.0);
    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::lp, lp));
    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::hp, hp));
    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::warmth, warmth));
    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::master, master));
    sliderAtts.push_back(std::make_unique<SliderAtt>(p.params, ids::masterPan, masterPan));

    for (auto [b, id] : {std::pair<ui::HwButton*, juce::String>{&presetBpm, ids::presetBpm}, {&hostSync, ids::syncHost}, {&linkButton, ids::link},

                         {&metronome, ids::metronome}, {&masterMute, ids::masterMute}, {&mono, ids::mono}}) {
        b->setClickingTogglesState(true);
        buttonAtts.push_back(std::make_unique<ButtonAtt>(p.params, id, *b));
    }
    presetBpm.setLed(true);
    presetBpm.setTooltip(juce::String::fromUTF8("Allumé : la loop joue à son tempo d'origine"));
    hostSync.setTooltip(juce::String::fromUTF8("Allumé : suit le tempo et la lecture de votre logiciel de musique"));
    linkButton.setTooltip(juce::String::fromUTF8("Ableton Link : Ableton et les autres logiciels Link prennent le tempo de "
                                                 "chaque loop, avec les mêmes mesures et le départ ensemble"));
    masterFx.setTooltip(juce::String::fromUTF8("Égaliseur, compresseur et limiteur du mix (ouvre la page)"));
    sendFx.setTooltip(juce::String::fromUTF8("Réverbération et envoi de chaque piste (ouvre la page)"));
    modFx.setTooltip(juce::String::fromUTF8("Chorus, flanger, phaser, trémolo et écho (ouvre la page)"));
    fade.setTooltip(juce::String::fromUTF8("Fondus et fondu enchaîné (ouvre la page)"));
    // les boutons du rack ouvrent la page des effets ; allumés = effet actif
    masterFx.onClick = [this] { openFx(FxPage::Master); };
    sendFx.onClick = [this] { openFx(FxPage::Send); };
    modFx.onClick = [this] { openFx(FxPage::Mod); };
    fade.onClick = [this] { openFx(FxPage::Fade); };
    fxPage.onClose = [this] { closeFx(); };
    face.addChildComponent(fxPage);
    setupSave.setTooltip(juce::String::fromUTF8("Enregistrer tout : la loop, le tempo, le mixer et les effets"));
    setupSave.onClick = [this] { saveSetupDialog(); };
    setupList.setTooltip(juce::String::fromUTF8("Rappeler un setup enregistré"));
    setupList.onClick = [this] { setupsMenu(); };
    settings.setTooltip(juce::String::fromUTF8("Ouvrir les paramètres (langue)"));
    settings.onClick = [this] { settingsMenu(); };
    lp.setTooltip(juce::String::fromUTF8("Filtre passe-bas : enlève les aigus"));
    hp.setTooltip(juce::String::fromUTF8("Filtre passe-haut : enlève les graves"));
    warmth.setTooltip(juce::String::fromUTF8("Chaleur : saturation douce"));
    signature.setTooltip(juce::String::fromUTF8("Gauche : vitesse (Double, Half). Droite : mesure — 3/4 rejoue chaque "
                                                "mesure en valse, 6/8 lui donne un balancement ternaire"));

    play.setClickingTogglesState(false);
    play.onClick = [this] { proc.setPlaying(true); };
    stop.onClick = [this] { proc.setPlaying(false); };

    constrainer.setFixedAspectRatio((double)kW / kH);
    constrainer.setSizeLimits(kW / 2, kH / 2, kW * 2, kH * 2);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setWantsKeyboardFocus(true);
    setSize(kW, kH);
    layout();
    startTimerHz(30);
}

AfricanLoopsEditor::~AfricanLoopsEditor() {
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AfricanLoopsEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }

void AfricanLoopsEditor::resized() {
    face.setBounds(0, 0, kW, kH);
    face.setTransform(juce::AffineTransform::scale((float)getWidth() / kW, (float)getHeight() / kH));
}

void AfricanLoopsEditor::layout() {
    browser.setBounds(X0 + 24, 92, 560, 274);
    bpm.setBounds(X0 + 608, 92, 84, 44);
    hostSync.setBounds(X0 + 608, 140, 41, 18);
    linkButton.setBounds(X0 + 651, 140, 41, 18);
    presetBpm.setBounds(X0 + 710, 98, 102, 26);
    lp.setBounds(X0 + 624, 182, 50, 50);
    hp.setBounds(X0 + 624, 246, 50, 50);
    warmth.setBounds(X0 + 624, 310, 50, 50);
    fxPage.setBounds(X0 + 6, 384, 782, 328);
    setupSave.setBounds(kW - X0 - 330, 17, 150, 26);
    setupList.setBounds(kW - X0 - 172, 17, 110, 26);
    settings.setBounds(kW - X0 - 56, 17, 32, 26);
    masterFx.setBounds(X0 + 724, 186, 88, 28);
    sendFx.setBounds(X0 + 724, 226, 88, 28);
    modFx.setBounds(X0 + 724, 266, 88, 28);
    fade.setBounds(X0 + 710, 306, 102, 28);
    for (int i = 0; i < kNumTracks; ++i) strips[(size_t)i]->setBounds(X0 + 66 + i * 90, 388, 79, 318);
    master.setBounds(X0 + 826, 410, 150, 150);
    metronome.setBounds(X0 + 1026, 408, 60, 24);
    masterPan.setBounds(X0 + 1034, 446, 44, 44);
    masterMute.setBounds(X0 + 1039, 506, 34, 22);
    mono.setBounds(X0 + 796, 574, 92, 18);
    signature.setBounds(X0 + 796, 596, 322, 72);
    stop.setBounds(X0 + 796, 676, 156, 34);
    play.setBounds(X0 + 962, 676, 156, 34);
}

void AfricanLoopsEditor::syncChannelNames() {
    auto c = proc.current();
    juce::StringArray names;
    for (int i = 0; i < kNumTracks; ++i) {
        const auto n = c && i < (int)c->stems.size() ? juce::String::fromUTF8(c->stems[(size_t)i].kind.c_str()) : juce::String();
        strips[(size_t)i]->setName(n);
        names.add(n == perc::Hat ? "HI-HAT" : n == perc::Hand ? "DJEMBE" : n == perc::Talking ? "TALKING" : n);
    }
    fxPage.setTrackNames(names);
}

juce::String AfricanLoopsEditor::Tooltips::getTipFor(juce::Component& c) { return lang::tr(juce::TooltipWindow::getTipFor(c)); }

void AfricanLoopsEditor::GearButton::paintButton(juce::Graphics& g, bool over, bool down) {
    ui::HwButton::paintButton(g, over, down);
    // roue dentée
    auto c = getLocalBounds().toFloat().withTrimmedBottom(2).getCentre().translated(0, down ? 1.f : 0.f);
    juce::Path p;
    const int teeth = 8;
    const float ro = 7.5f, ri = 5.6f;
    for (int i = 0; i < teeth * 4; ++i) {
        const float a = juce::MathConstants<float>::twoPi * (float)i / (teeth * 4);
        const float r = (i % 4 == 0 || i % 4 == 1) ? ro : ri;
        const auto pt = c + juce::Point<float>(std::sin(a) * r, -std::cos(a) * r);
        if (i == 0) p.startNewSubPath(pt);
        else p.lineTo(pt);
    }
    p.closeSubPath();
    p.addEllipse(c.x - 2.4f, c.y - 2.4f, 4.8f, 4.8f);
    p.setUsingNonZeroWinding(false);
    g.setColour(over ? col::amber : col::engrave);
    g.fillPath(p);
}

void AfricanLoopsEditor::settingsMenu() {
    juce::PopupMenu m;
    m.addSectionHeader(TR("LANGUE"));
    for (int l = 0; l < lang::Count; ++l) m.addItem(1 + l, lang::nativeName((lang::Id)l), true, l == lang::current());
    juce::Component::SafePointer<AfricanLoopsEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settings), [safe](int r) {
        if (!safe || r <= 0) return;
        lang::set((lang::Id)(r - 1));
        safe->languageChanged();
    });
}

// Nouvelle langue : tout est redessiné (les textes sont traduits au dessin).
void AfricanLoopsEditor::languageChanged() {
    shownLang = lang::current();
    face.background = {};
    browser.search.setTextToShowWhenEmpty(TR("Rechercher une loop…"), col::lcdDim.withAlpha(0.7f));
    browser.refreshLibrary();
    std::function<void(juce::Component&)> all = [&](juce::Component& c) {
        c.repaint();
        for (auto* ch : c.getChildren()) all(*ch);
    };
    all(*this);
}

void AfricanLoopsEditor::openFx(FxPage::Tab t) {
    fxPage.show(t);
    fxPage.setVisible(true);
    fxPage.toFront(false);
    for (auto& st : strips) st->setVisible(false);
}

void AfricanLoopsEditor::closeFx() {
    fxPage.setVisible(false);
    for (auto& st : strips) st->setVisible(true);
}

void AfricanLoopsEditor::saveSetupDialog() {
    auto c = proc.current();
    const auto suggestion = c ? c->file.getFileNameWithoutExtension() : juce::String("Mon setup");
    juce::Component::SafePointer<AfricanLoopsEditor> safe(this);
    askText(TR("Enregistrer le setup"),
            TR("Nom du setup (la loop, le tempo, le mixer et les effets sont gardés) :"), suggestion,
            [safe](const juce::String& name) {
                if (!safe) return;
                bool exists = false;
                for (const auto& si : safe->proc.setups()) exists |= si.name == name;
                if (!exists) { safe->proc.saveSetup(name); return; }
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, TR("Setup existant"),
                                                   TR("Remplacer le setup « ") + name + TR(" » ?"),
                                                   TR("Remplacer"), TR("Annuler"), nullptr,
                                                   juce::ModalCallbackFunction::create([safe, name](int r) {
                                                       if (safe && r == 1) safe->proc.saveSetup(name);
                                                   }));
            });
}

void AfricanLoopsEditor::setupsMenu() {
    const auto list = proc.setups();
    juce::PopupMenu m, ren, del;
    m.addSectionHeader(TR("CHARGER UN SETUP"));
    if (list.isEmpty()) m.addItem(-1, TR("Aucun setup enregistré pour l'instant"), false);
    for (int i = 0; i < list.size(); ++i) {
        const auto& si = list.getReference(i);
        juce::String t = si.name;
        if (si.loop.isNotEmpty()) t << "   -   " << si.loop;
        if (si.bpm > 0) t << "   " << bpmText(si.bpm) << " BPM";
        m.addItem(100 + i, t);
        ren.addItem(1000 + i, si.name);
        del.addItem(2000 + i, si.name);
    }
    if (!list.isEmpty()) {
        m.addSeparator();
        m.addSubMenu(TR("Renommer"), ren);
        m.addSubMenu(TR("Supprimer"), del);
    }
    m.addSeparator();
    m.addItem(1, TR("Ouvrir le dossier des setups"));
    juce::Component::SafePointer<AfricanLoopsEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&setupList), [safe, list](int r) {
        if (!safe || r <= 0) return;
        if (r == 1) {
            auto d = AfricanLoopsProcessor::setupsFolder();
            d.createDirectory();
            d.startAsProcess();
        } else if (r >= 2000) {
            const auto name = list[r - 2000].name;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, TR("Supprimer le setup"),
                                               TR("Supprimer « ") + name + TR(" » ?"), TR("Supprimer"),
                                               TR("Annuler"), nullptr, juce::ModalCallbackFunction::create([safe, name](int res) {
                                                   if (safe && res == 1) safe->proc.deleteSetup(name);
                                               }));
        } else if (r >= 1000) {
            const auto name = list[r - 1000].name;
            askText(TR("Renommer le setup"), TR("Nouveau nom :"), name,
                    [safe, name](const juce::String& to) {
                        if (safe && to != name) safe->proc.renameSetup(name, to);
                    });
        } else if (r >= 100) {
            safe->proc.loadSetup(list[r - 100].name);
        }
    });
}

void AfricanLoopsEditor::timerCallback() {
    if (shownLang != lang::current()) languageChanged();  // changée dans une autre fenêtre du plugin
    if (proc.libraryCounter() != lastLibs) {
        lastLibs = proc.libraryCounter();
        browser.refreshLibrary();
    }
    if (proc.loadCounter() != lastLoads) {
        lastLoads = proc.loadCounter();
        syncChannelNames();
        browser.rebuildLoops();
        browser.repaint();
    }
    const bool busy = proc.isScanning() || proc.isLoading();
    if (busy || wasBusy) browser.repaint();
    wasBusy = busy;
    const bool playing = proc.isPlaying();
    if (playing || playing != wasPlaying) browser.wave.repaint();
    if (playing != wasPlaying) browser.loopList.repaint();
    wasPlaying = playing;
    for (auto& s : strips) s->updateMeter();
    const bool synced = proc.isHostSynced();
    bpm.setEnabled(!synced);
    if (const double b = proc.displayedBPM(); std::abs(b - shownBpm) > 1e-6) {
        shownBpm = b;
        bpm.repaint();
    }
    play.setToggleState(playing, juce::dontSendNotification);
    masterFx.setToggleState(paramValue(proc, ids::masterFx) > 0.5f || std::abs(paramValue(proc, ids::eqLow)) + std::abs(paramValue(proc, ids::eqLowMid)) +
                                std::abs(paramValue(proc, ids::eqHighMid)) + std::abs(paramValue(proc, ids::eqHigh)) > 0.1f,
                            juce::dontSendNotification);
    sendFx.setToggleState(paramValue(proc, ids::sendFx) > 0.5f, juce::dontSendNotification);
    modFx.setToggleState(paramValue(proc, ids::modFx) > 0.5f || paramValue(proc, ids::echo) > 0.5f, juce::dontSendNotification);
    fade.setToggleState(paramValue(proc, ids::fade) > 0.5f || paramValue(proc, ids::xfade) > 0.5f, juce::dontSendNotification);
    if (const float sig = paramValue(proc, ids::signature) * 10 + paramValue(proc, ids::feel); std::abs(sig - shownSignature) > 1e-3f) {
        shownSignature = sig;
        signature.repaint();
    }
    if (const float mv = paramValue(proc, ids::master); std::abs(mv - shownMaster) > 1e-5f) {
        shownMaster = mv;
        face.repaint(master.getBounds());
    }
    if (const int peers = proc.isLinkActive() ? proc.linkPeers() : -1; synced != wasSynced || peers != shownPeers) {
        wasSynced = synced;
        shownPeers = peers;
        face.repaint(X0 + 600, kH - 22, kW - 2 * X0 - 620, 18);
    }
    float l, r;
    proc.meters(l, r);
    if (l < 0.0005f && r < 0.0005f && peakHoldL < 0.0005f && peakHoldR < 0.0005f && metersIdle) return;  // silence : rien à redessiner
    metersIdle = l < 0.0005f && r < 0.0005f;
    if (l >= peakHoldL || r >= peakHoldR || ++holdCount > 45) {
        peakHoldL = std::max(l, holdCount > 45 ? 0.f : peakHoldL);
        peakHoldR = std::max(r, holdCount > 45 ? 0.f : peakHoldR);
        if (holdCount > 45) holdCount = 0;
    }
    face.repaint(X0 + 830, 92, 300, 280);
}

bool AfricanLoopsEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey && fxPage.isVisible()) {
        closeFx();
        return true;
    }
    if (k == juce::KeyPress::spaceKey) {
        proc.setPlaying(!proc.isPlaying());
        return true;
    }
    if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey) {
        if (browser.search.hasKeyboardFocus(true) || browser.loopList.hasKeyboardFocus(true)) return false;  // gérées par la liste
        browser.step(k == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    return false;
}

bool AfricanLoopsEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        juce::File file(f);
        if (file.isDirectory() || file.hasFileExtension("wav;wave;mp3")) return true;
    }
    return false;
}

void AfricanLoopsEditor::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& f : files) {
        juce::File file(f);
        if (file.isDirectory()) { proc.scanFolderAsync(file); return; }
        if (file.hasFileExtension("wav;wave;mp3")) { proc.loadLoopAsync(file); return; }
    }
}

// ---------------------------------------------------------------- façade

void AfricanLoopsEditor::Face::paint(juce::Graphics& g) {
    // Le fond (bois, métal, logo, étiquettes) ne change jamais : il est dessiné une
    // seule fois, à la résolution réelle de l'écran, puis simplement recopié.
    const float sc = std::max(0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!background.isValid() || std::abs(sc - backgroundScale) > 0.01f) {
        backgroundScale = sc;
        background = juce::Image(juce::Image::RGB, std::max(1, juce::roundToInt(kW * sc)), std::max(1, juce::roundToInt(kH * sc)), false);
        juce::Graphics bg(background);
        bg.addTransform(juce::AffineTransform::scale(sc));
        paintStatic(bg);
    }
    g.drawImageTransformed(background, juce::AffineTransform::scale(1.f / sc));
    // texte d'état (change rarement)
    g.setColour(col::engraveDark);
    g.setFont(ui::font(9.f));
    juce::String status = juce::String((int)std::lround(ed.proc.getSampleRate() > 0 ? ed.proc.getSampleRate() : 44100)) + TR(" Hz  -  8 pistes");
    if (ed.proc.isLinkActive()) {
        const int n = ed.proc.linkPeers();
        status << TR("  -  Ableton Link : ") << (n == 0 ? TR("en attente d'un autre logiciel")
                                                                            : juce::String(n) + (n > 1 ? TR(" logiciels") : TR(" logiciel")));
    } else if (ed.proc.isHostSynced()) {
        status << TR("  -  calé sur le logiciel hôte");
    }
    g.drawText(status, X0 + 600, kH - 20, kW - 2 * X0 - 626, 14, juce::Justification::centredRight);
}

void AfricanLoopsEditor::Face::paintStatic(juce::Graphics& g) {
    const auto W = (float)kW, H = (float)kH;
    // joues en bois
    juce::Random rnd(3);
    for (float x0 : {0.f, W - X0}) {
        juce::ColourGradient wood(juce::Colour(0xff2b170d), x0, 0, juce::Colour(0xff22120a), x0 + X0, 0, false);
        wood.addColour(0.3, juce::Colour(0xff6b3e22));
        wood.addColour(0.55, juce::Colour(0xff80502e));
        wood.addColour(0.85, juce::Colour(0xff4a2a17));
        g.setGradientFill(wood);
        g.fillRect(x0, 0.f, (float)X0, H);
        for (int i = 0; i < 90; ++i) {  // veines du bois
            g.setColour(juce::Colours::black.withAlpha(0.08f + rnd.nextFloat() * 0.12f));
            float y = rnd.nextFloat() * H;
            g.fillRect(x0, y, (float)X0, 1.f + rnd.nextFloat() * 2);
        }
    }
    // façade en métal sombre
    auto panel = juce::Rectangle<float>((float)X0, 0, W - 2 * X0, H);
    juce::ColourGradient metal(col::panelTop, 0, 0, col::panelBottom, 0, H, false);
    metal.addColour(0.55, juce::Colour(0xff1d1c1b));
    g.setGradientFill(metal);
    g.fillRect(panel);
    g.setTiledImageFill(ed.noise, 0, 0, 0.5f);
    g.fillRect(panel);
    g.setColour(juce::Colour(0xff555555));
    g.drawLine(panel.getX(), 0.5f, panel.getRight(), 0.5f);
    // vis
    for (auto pt : {juce::Point<float>(X0 + 14, 14), {W - X0 - 14, 14}, {X0 + 14, H - 14}, {W - X0 - 14, H - 14}}) {
        juce::ColourGradient s(juce::Colour(0xffdddddd), pt.x - 2, pt.y - 2, juce::Colour(0xff333333), pt.x + 5, pt.y + 5, true);
        g.setGradientFill(s);
        g.fillEllipse(pt.x - 6, pt.y - 6, 12, 12);
        g.setColour(juce::Colour(0xff2a2a2a));
        g.drawLine(pt.x - 4, pt.y + 2.5f, pt.x + 4, pt.y - 2.5f, 2);
    }
    // en-tête : grilles d'aération, motifs, logo
    g.setColour(juce::Colour(0xff0a0a0a));
    g.drawLine(panel.getX(), 58, panel.getRight(), 58);
    g.setColour(juce::Colour(0xff3a3936));
    g.drawLine(panel.getX(), 59, panel.getRight(), 59);
    for (float vx : {X0 + 60.f}) {  // à droite : les boutons des setups
        for (int i = 0; i < 22; ++i) {
            auto r = juce::Rectangle<float>(vx + i * 9, 18, 4, 22);
            g.setColour(juce::Colour(0xff444444));
            g.fillRoundedRectangle(r.translated(0, 1), 2);
            g.setColour(juce::Colour(0xff050505));
            g.fillRoundedRectangle(r, 2);
        }
    }
    {
        const juce::String name = "AFRICAN LOOPS";
        auto f = ui::font(31.f, true).withExtraKerningFactor(0.09f);
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, name, 0, 0);
        juce::Path path;
        ga.createPath(path);
        auto pb = path.getBounds();
        const float proW = 58, total = pb.getWidth() + 14 + proW;
        const float x = (W - total) / 2, y = 41;
        path.applyTransform(juce::AffineTransform::translation(x - pb.getX(), y));
        pb = path.getBounds();  // position réelle du texte, pour placer « PRO » juste après
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillPath(path, juce::AffineTransform::translation(0, 2));
        juce::ColourGradient gold(juce::Colour(0xfffff1cf), 0, y - 26, juce::Colour(0xfff1c27a), 0, y + 2, false);
        gold.addColour(0.45, juce::Colour(0xffe0a54a));
        gold.addColour(0.55, juce::Colour(0xff8d5a1c));
        g.setGradientFill(gold);
        g.fillPath(path);
        auto pro = juce::Rectangle<float>(pb.getRight() + 14, pb.getCentreY() - 11, proW, 22);
        g.setColour(col::amber);
        g.drawRoundedRectangle(pro, 3, 1.5f);
        g.setFont(ui::font(16.f, true).withExtraKerningFactor(0.12f));
        g.drawText("PRO", pro, juce::Justification::centred);
        // signature
        {
            auto fBy = ui::font(11.f).italicised().withExtraKerningFactor(0.18f);
            auto fName = ui::font(11.f, true).withExtraKerningFactor(0.22f);
            const float wBy = juce::GlyphArrangement::getStringWidth(fBy, "by "), wName = juce::GlyphArrangement::getStringWidth(fName, "JB PRAISE");
            const float bx = (W - wBy - wName) / 2, by0 = 44;
            g.setColour(juce::Colours::black.withAlpha(0.8f));
            g.setFont(fBy);
            g.drawText("by ", juce::Rectangle<float>(bx, by0 + 1, wBy, 13), juce::Justification::left);
            g.setFont(fName);
            g.drawText("JB PRAISE", juce::Rectangle<float>(bx + wBy, by0 + 1, wName + 4, 13), juce::Justification::left);
            g.setColour(juce::Colour(0xffe8c690));
            g.setFont(fBy);
            g.drawText("by ", juce::Rectangle<float>(bx, by0, wBy, 13), juce::Justification::left);
            g.setColour(juce::Colour(0xfff1c27a));
            g.setFont(fName);
            g.drawText("JB PRAISE", juce::Rectangle<float>(bx + wBy, by0, wName + 4, 13), juce::Justification::left);
        }
        // motif en chevrons de chaque côté
        g.setColour(juce::Colour(0xffc89a55).withAlpha(0.5f));
        for (float side : {-1.f, 1.f}) {
            float cx = side < 0 ? x - 30 : x + total + 30;
            g.drawLine(cx, 29, cx - side * 150, 29, 1);
            for (int i = 0; i < 9; ++i) {
                float px = cx - side * (i * 16 + 4);
                g.drawLine(px, 23, px - side * 8, 29, 1);
                g.drawLine(px - side * 8, 29, px, 35, 1);
            }
        }
    }
    // étiquettes gravées
    auto label = [&](const char* t, int x, int y, int w, float size = 9.5f) {
        ui::drawEngraved(g, juce::String::fromUTF8(t), {x, y, w, 14}, size);
    };
    label("PRESETS / BIBLIOTHÈQUE", X0 + 24, 72, 560);
    label("GLOBAL BPM", X0 + 600, 72, 100);
    label("PRESET BPM", X0 + 710, 72, 102);
    label("GLOBAL FILTERS", X0 + 594, 164, 110);
    label("PRO RACK FX", X0 + 712, 164, 112);
    label("LP", X0 + 624, 232, 50, 8);
    label("HP", X0 + 624, 296, 50, 8);
    label("WARMTH", X0 + 614, 360, 70, 8);
    label("METERS", X0 + 830, 72, 300);
    label("MASTER", X0 + 826, 392, 150);
    label("METRONOME", X0 + 1006, 392, 100);
    label("PAN", X0 + 1006, 492, 100, 8);
    label("MUTE", X0 + 1006, 530, 100, 8);
    ui::drawEngraved(g, "SIGNATURE", {X0 + 1000, 576, 118, 14}, 8.f, juce::Justification::centredRight);
    for (auto [t, y] : {std::pair<const char*, int>{"LOOP", 402}, {"PAN", 450}, {"M/S", 496}, {"LEVEL", 588}})
        ui::drawEngraved(g, t, {X0 + 10, y, 50, 14}, 8.5f, juce::Justification::centredLeft);
    // séparation haut / bas
    g.setColour(juce::Colour(0xff0a0a0a));
    g.drawLine(X0 + 16, 378, W - X0 - 16, 378);
    g.setColour(juce::Colour(0xff3a3936));
    g.drawLine(X0 + 16, 379, W - X0 - 16, 379);
    // encastrements
    ui::drawRecess(g, juce::Rectangle<float>(X0 + 20.f, 88, 568, 282), 8);
    // vumètres
    auto mr = juce::Rectangle<float>(X0 + 830.f, 92, 300, 280);
    ui::drawRecess(g, mr, 6);
    g.setFont(ui::font(8.f));
    g.setColour(col::engraveDark);
    const char* marks[] = {"+6", "0", "-6", "-12", "-18", "-24", "-36", "-48", "-60"};
    const float dbs[] = {6, 0, -6, -12, -18, -24, -36, -48, -60};
    for (int i = 0; i < 9; ++i) {
        float y = juce::jmap(dbs[i], 6.f, -60.f, mr.getY() + 18, mr.getBottom() - 26);
        g.drawText(marks[i], (int)mr.getX() + 70, (int)y - 5, 24, 10, juce::Justification::centredRight);
        g.drawText(marks[i], (int)mr.getX() + 172, (int)y - 5, 24, 10, juce::Justification::centredLeft);
    }
}

void AfricanLoopsEditor::Face::paintOverChildren(juce::Graphics& g) {
    // vumètres de sortie
    auto mr = juce::Rectangle<float>(X0 + 830.f, 92, 300, 280);
    float l, r;
    ed.proc.meters(l, r);
    auto barL = juce::Rectangle<float>(mr.getX() + 100, mr.getY() + 16, 26, mr.getHeight() - 40);
    ui::drawLedBar(g, barL, l, 40, true, ed.peakHoldL);
    ui::drawLedBar(g, barL.translated(40, 0), r, 40, true, ed.peakHoldR);
    g.setColour(col::engrave);
    g.setFont(ui::font(9.f, true));
    g.drawText("L", barL.withY(mr.getBottom() - 22).withHeight(14), juce::Justification::centred);
    g.drawText("R", barL.translated(40, 0).withY(mr.getBottom() - 22).withHeight(14), juce::Justification::centred);
    auto peak = juce::Rectangle<float>(mr.getRight() - 90, mr.getY() + 14, 76, 82);
    ui::drawLcd(g, peak);
    const float pk = std::max(ed.peakHoldL, ed.peakHoldR);
    auto in = peak.reduced(7, 5);
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText("PEAK dB", in.withHeight(11), juce::Justification::left);
    ui::drawGlowText(g, pk > 0.00001f ? juce::String(juce::Decibels::gainToDecibels(pk), 1) : juce::String("-inf"),
                     in.withTrimmedTop(12).withHeight(20), ui::font(15.f, true), pk > 0.99f ? col::red : col::amber,
                     juce::Justification::left);
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText("BPM", in.withTrimmedTop(38).withHeight(11), juce::Justification::left);
    ui::drawGlowText(g, bpmText(ed.proc.displayedBPM()), in.withTrimmedTop(50).withHeight(20), ui::font(15.f, true),
                     col::amber, juce::Justification::left);
    // écran au centre du bouton Master
    auto mb = ed.master.getBounds().toFloat();
    auto c = mb.getCentre();
    auto lcd = juce::Rectangle<float>(mb.getWidth() * 0.36f, mb.getWidth() * 0.36f).withCentre(c);
    g.setColour(juce::Colour(0xff0b0906));
    g.fillEllipse(lcd);
    g.setColour(juce::Colours::black);
    g.drawEllipse(lcd, 2);
    const float mv = paramValue(ed.proc, ids::master);
    ui::drawGlowText(g, juce::String(juce::roundToInt(mv * 100)) + "%", lcd.withTrimmedBottom(lcd.getHeight() * 0.2f),
                     ui::font(18.f, true), col::amber);
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.f, true));
    g.drawText(mv > 0.0001f ? juce::String(juce::Decibels::gainToDecibels(mv), 1) + " dB" : juce::String("-inf dB"),
               lcd.withTrimmedTop(lcd.getHeight() * 0.58f), juce::Justification::centredTop);
}
