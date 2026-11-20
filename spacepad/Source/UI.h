// Spacepad by JB Praise — interface "Orbit" : espace animé derrière l'orbe, anneau des tonalités, crochet majeur/mineur,
// visualiseur audio, bibliothèques (pads et crowds séparés), effets, analyse des dossiers, paramètres.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "Lang.h"

namespace spacepad
{
namespace theme
{
    // couleurs qui changent avec les Paramètres (thème et couleur principale)
    inline juce::Colour bg { 0xff070a14 }, panel { 0xff0d1220 }, panel2 { 0xff0f1524 }, lcd { 0xff050912 };
    inline juce::Colour chassisTop { 0xff121828 }, chassisBottom { 0xff0b0f1a }, headerTop { 0xff1a2135 }, headerBottom { 0xff111727 };
    inline juce::Colour accent { 0xff6fd8ff }, accentLight { 0xffbdf1ff };
    // couleurs fixes
    const juce::Colour amber { 0xffffb86b }, violet { 0xffa78bfa }, gold { 0xffffcf7a }, lilac { 0xffc9b8ff };
    inline juce::Colour text { 0xffe8edf7 }, muted { 0xff8b97ad }, line { 0x18ffffff };
    inline juce::Colour ink { 0xffffffff };          // reflets et voiles : blanc sur fond sombre, noir sur le thème Clair
    inline bool light = false;
    const juce::Colour green { 0xff5cffb0 }, red { 0xffff6b6b };
    void apply (const UiSettings&);
    juce::Colour accentChoice (int index);
    juce::Font title (float h);                   // Michroma (logo, tonalités)
    juce::Font ui (float h, int weight = 0);      // Inter : 0 normal, 1 demi-gras, 2 gras
    juce::Colour colourFor (const juce::String& name);
}

juce::String keyText (int key);                    // nom de note selon le réglage (C D E ou Do Ré Mi)

class SpaceLook : public juce::LookAndFeel_V4
{
public:
    SpaceLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override { return theme::ui (15.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::ui (14.0f, 1); }
    juce::Font getLabelFont (juce::Label&) override { return theme::ui (14.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void refreshColours();
};

// --------------------------------------------------------------------------------------------
class Knob : public juce::Component
{
public:
    Knob (SpacepadProcessor&, const juce::String& paramId, const juce::String& label, juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setColour (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); repaint(); }
    bool usesAccent = false;
    std::function<juce::String()> valueText;     // texte de la valeur (sinon celui du paramètre)
private:
    SpacepadProcessor& proc;
    juce::String id, label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// --------------------------------------------------------------------------------------------
// État de l'animation, commun à l'espace, à l'orbe et au visualiseur
struct AnimState
{
    float env = 0;        // le pad joue (0..1, monte et descend en douceur)
    float surge = 0;      // élan au changement de tonalité ou de mode
    float t = 0;          // temps
    float phi = 0;        // rotation de la galaxie
    float orb = 0;        // rotation de l'orbe
};

// L'espace animé derrière l'orbe : galaxie qui tourne, tourbillon d'étoiles, ou fond sobre
class SpaceView : public juce::Component
{
public:
    SpaceView (SpacepadProcessor& p, AnimState& a) : proc (p), anim (a) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    void resized() override { farStars = {}; layer = {}; needsRender = true; }
    void tick (float dt);
    juce::Point<float> centre;   // centre de l'orbe (dans ce composant)
    float coreRadius = 110;      // rayon de l'orbe
private:
    void buildPlane (int mode);
    SpacepadProcessor& proc;
    AnimState& anim;
    void renderLayer (float px);
    juce::Image plane, farStars, layer;   // layer : galaxie + étoiles en orbite, recalculée une image sur deux
    int planeMode = -1, frames = 0;
    bool needsRender = true;
    struct Star { float r, th, b, s; };
    std::vector<Star> orbit;
    juce::Random rng { 17 };
};

class KeyRing : public juce::Component
{
public:
    KeyRing (SpacepadProcessor& p, AnimState& a) : proc (p), anim (a) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -2; repaint(); }
    bool hitTest (int x, int y) override { return keyAt ({ (float) x, (float) y }) >= -1; }
    float coreRadius() const { return juce::jmin (getWidth(), getHeight()) * 0.234f; }
private:
    int keyAt (juce::Point<float>) const;   // 0..11, -1 = centre, -2 = rien
    void paintKey (juce::Graphics&, int i, bool sel, bool active, bool hov, int mode, float D, juce::Point<float> c) const;
    SpacepadProcessor& proc;
    AnimState& anim;
    int hover = -2;
    juce::Image base, labelImg;              // décor fixe et nom de la tonalité, préparés une fois
    int baseKey = 0, labelW = 0;
    float baseScale = 0, labelScale = 0;
    juce::String labelText;
};

// Le crochet : passe du majeur au mineur (et inversement)
class HookButton : public juce::Button
{
public:
    explicit HookButton (SpacepadProcessor& p) : juce::Button ("mode"), proc (p) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void tick (float dt);
private:
    SpacepadProcessor& proc;
    float angle = 0;                          // la flèche fait un demi-tour à chaque changement
};

// Visualiseur : le son réel découpé en fréquences, qui bouillonne
class Visualizer : public juce::Component
{
public:
    Visualizer (SpacepadProcessor& p, AnimState& a);
    void paint (juce::Graphics&) override;
    void tick (float dt);
private:
    SpacepadProcessor& proc;
    AnimState& anim;
    static constexpr int order = 11, N = 1 << order, bands = 34;
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window { (size_t) N, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, N * 2> fftData {};
    std::array<float, N> wave {};
    std::array<float, bands> level {};
    struct Bubble { float x, y, r, v, a; };
    std::vector<Bubble> bubbles;
    juce::Random rng { 3 };
};

class Header : public juce::Component
{
public:
    explicit Header (SpacepadProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void invalidate() { logoImg = {}; nameImg = {}; }
private:
    SpacepadProcessor& proc;
    juce::Image logoImg, nameImg;            // lueurs préparées une fois
    float logoScale = 0, nameScale = 0;
    int nameW = 0;
    juce::String nameText;
};

// --------------------------------------------------------------------------------------------
// Liste à lignes de hauteurs différentes (titres de rangées + librairies ou sons)
class RowList : public juce::Component
{
public:
    struct Row
    {
        bool header = false;
        juce::String name, sub, tag;
        juce::Colour colour;
        bool on = false, playing = false;
        std::function<void()> onClick;
        std::function<void()> onMenu;
        juce::String badge;                    // numéro (setlist), à la place du point de couleur
        bool chips = false, aOn = false, bOn = false;   // pastilles A / B (deux pads mélangés)
        std::function<void()> onA, onB;
        int star = 0;                          // étoile des favoris : 0 aucune, 1 vide, 2 pleine
        bool fx = false;                       // badge FX : ce son a ses propres effets
        std::function<void()> onStar;
    };
    void setRows (std::vector<Row> r);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int contentHeight() const;
    juce::String emptyText;
private:
    int rowAt (int y) const;
    std::vector<Row> rows;
    int hover = -1;
};

class LibraryPanel : public juce::Component, private juce::Timer
{
public:
    explicit LibraryPanel (SpacepadProcessor&);
    ~LibraryPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                            // relit les bibliothèques
    void showTab (bool crowdTab) { showTabIndex (crowdTab ? 1 : 0); }
    void showTabIndex (int tab);               // 0 pads, 1 crowds, 2 setlist
    int tabShown() const { return tab; }
    bool crowdTabShown() const { return tab == 1; }
    void updateState();                        // états lumineux (son joué, mode)
    void retranslate();                        // textes dans la langue choisie
    std::function<void (bool crowdTab)> onChooseFolder, onChooseFiles;

private:
    void timerCallback() override { if (tab == 1) repaint (playerArea); }
    void rebuildChips();
    void rebuildRows();
    const Library* currentCrowd() const;
    const Category* currentCategory() const;
    void libraryMenu (const Library& l);
    void songMenu (int index, const Song& s);

    SpacepadProcessor& proc;
    juce::TextButton padTab { "PADS" }, crowdTab { "CROWDS" }, setTab { "SETLIST" };
    juce::TextButton chooseBtn, filesBtn;
    juce::ComboBox crowdPicker;
    std::vector<std::unique_ptr<juce::TextButton>> chips;
    juce::Viewport viewport;
    RowList rows;
    // pads : mélange A / B
    juce::Slider blend;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAtt;
    juce::Rectangle<int> blendArea;
    // crowds : deux à la fois, combinaison avec le pad
    juce::TextButton slotBtn[2], crowdStop[2], linkBtn, followBtn;
    juce::Slider crowdVol[2];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crowdVolAtt[2];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAtt, followAtt;
    int targetSlot = 0;
    juce::Rectangle<int> playerArea;
    // classement des crowds
    juce::TextEditor search;
    juce::TextButton sortBtn;
    int sortMode = 0;                          // 0 catégorie, 1 nom, 2 tonalité, 3 durée, 4 récents
    int filter = 0;                            // 0 tous, 1 favoris, 2+ catégorie
    juce::Rectangle<int> searchArea;
    void crowdSoundMenu (const juce::String& libId, int flat);
    // setlist
    juce::TextButton addSong, prevSongBtn, nextSongBtn;
    int tab = 0;
    std::vector<Library> libs;
    juce::String crowdLibId;
    int category = 0;
    juce::Rectangle<int> statusArea;
    juce::String lastStateSig;
    int lastSetlistChanges = -1;
};

// --------------------------------------------------------------------------------------------
class ScanOverlay : public juce::Component, private juce::Timer
{
public:
    ScanOverlay (SpacepadProcessor&, const juce::File& folder, LibType type);
    ScanOverlay (SpacepadProcessor&, const juce::Array<juce::File>& items, LibType type);   // dossiers et fichiers mélangés
    ~ScanOverlay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (bool saved)> onClose;
    std::function<void (std::vector<Library>)> onCopyAndSave;   // réglage « copier les sons dans Spacepad »

private:
    struct Job : juce::Thread
    {
        Job (juce::Array<juce::File> f, LibType t) : juce::Thread ("Spacepad analyse"), items (std::move (f)), type (t) {}
        void run() override;
        juce::Array<juce::File> items;
        LibType type;
        std::vector<Library> result;
        juce::StringArray skipped;
        std::atomic<double> progress { 0 };
        juce::CriticalSection lock;
        juce::String status;
        std::atomic<bool> finished { false };
    };
    void timerCallback() override;
    juce::Rectangle<int> card() const;

    SpacepadProcessor& proc;
    Job job;
    std::vector<Library> results;
    bool done = false;
    int selected = 0;
    juce::TextButton cancelBtn, saveBtn;
    RowList list;
};

// --------------------------------------------------------------------------------------------
// Fenêtre « Personnaliser Spacepad » : Apparence, MIDI & pédales, Sons & sorties
class SettingsOverlay : public juce::Component, private juce::Timer
{
public:
    SettingsOverlay (SpacepadProcessor&, std::function<void()> changed);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClose;
    std::function<void()> onExport, onImport;
    void showPage (int p);
private:
    void timerCallback() override
    {
        if (page == 1) { refreshMidiButtons(); repaint(); }
        if (page == 3 && proc.libraries().changeCount() != lastLibChanges) rebuildLibList();
    }
    void rebuildLibList();
    juce::Viewport libView;
    std::unique_ptr<juce::Component> libList;
    juce::TextButton deleteAllBtn;
    juce::Rectangle<int> libHeadArea;
    int lastLibChanges = -1;
    bool inPlugin = false;                      // dans un logiciel de musique : l'audio, le tempo et le MIDI passent par lui
    struct Choice { juce::String label; juce::Rectangle<int> r; };
    struct Group { juce::String title; std::vector<Choice> choices; int* value; juce::Rectangle<int> area; };
    void load();
    void save();
    void refreshMidiButtons();
    juce::Rectangle<int> card() const;
    SpacepadProcessor& proc;
    std::function<void()> changed;
    UiSettings s;
    int page = 0;
    int accent = 0, themeIdx = 0, bgIdx = 0, vizIdx = 0, namesIdx = 0, sizeIdx = 1, langIdx = 0, animIdx = 1, awakeIdx = 1;
    int keyModeIdx = 0, fadeIdx = 1, copyIdx = 0, crowdBgIdx = 0;
    std::vector<Group> groups;
    std::vector<juce::Rectangle<int>> swatches, tiles, pageTabs;
    juce::Rectangle<int> accentArea, bgArea, midiListArea, zoneArea, infoArea;
    juce::Slider brightness, speed;
    juce::TextButton resetBtn, doneBtn, zoneBtn, exportBtn, importBtn;
    juce::TextButton assignBtn[actCount], clearBtn[actCount];
};

class GearButton : public juce::Button
{
public:
    GearButton() : juce::Button ("settings") {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// Volume général (Master) dans l'en-tête
class MasterBar : public juce::Component
{
public:
    explicit MasterBar (SpacepadProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    SpacepadProcessor& proc;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// Curseur PAD ⇄ CROWD (combinaison)
class MixBar : public juce::Component
{
public:
    explicit MixBar (SpacepadProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    SpacepadProcessor& proc;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
public:
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (int page)> onLabel;      // toucher « PAD » ou « CROWD » ouvre ses effets
};

// Mode scène : plein écran, très gros boutons, lisible de loin
class StageOverlay : public juce::Component, private juce::Timer
{
public:
    explicit StageOverlay (SpacepadProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClose;
private:
    void timerCallback() override { repaint(); }
    SpacepadProcessor& proc;
    std::vector<juce::Rectangle<int>> keys;
    juce::Rectangle<int> orb, quit;
    juce::TextButton next, mode, fin, stop;
};

class IconButton : public juce::Button
{
public:
    explicit IconButton (juce::String kind) : juce::Button (kind), icon (std::move (kind)) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    juce::String icon;
};

// Travail long (copie, export, import) avec sa barre de progression
class TaskOverlay : public juce::Component, private juce::Timer, private juce::Thread
{
public:
    TaskOverlay (juce::String title, std::function<bool (ProgressFn)> work);
    ~TaskOverlay() override;
    void paint (juce::Graphics&) override;
    std::function<void (bool ok)> onDone;
private:
    void run() override;
    void timerCallback() override;
    juce::String title;
    std::function<bool (ProgressFn)> work;
    std::atomic<double> progress { 0 };
    std::atomic<int> result { -1 };
};

// Panneau des effets : EFFETS PAD (15 boutons, Freeze, Swell) ou EFFETS CROWD (12 boutons par crowd, TAP, Écouter seul)
class FxPanel : public juce::Component
{
public:
    explicit FxPanel (SpacepadProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void showPage (int page);                   // 0 pad, 1 crowd
    int page() const { return pageNow; }
    void showSlot (int slot);
    void retranslate();
    void applyColours();
    void tick();                                // nom du son, tempo
private:
    SpacepadProcessor& proc;
    int pageNow = 0, slot = 0;
    std::vector<std::unique_ptr<Knob>> padKnobs, crowdKnobs[2];
    juce::TextButton freezeBtn { "FREEZE" }, swellBtn { "SWELL" }, tapBtn, soloBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, swellAtt;
    juce::Rectangle<int> tabsArea, slotArea, nameArea;
    juce::String lastName;
};

// Bouton PAD / CROWD au-dessus de l'orbe : change le centre de l'écran
class CenterSwitch : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int value = 0;
    std::function<void (int)> onChange;
};

// La grande salle derrière les crowds : arches, vitrail, rayons de lumière, foule ; la lumière respire avec le son
class HallView : public juce::Component
{
public:
    explicit HallView (SpacepadProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    void resized() override { image = {}; }
    void tick (float dt);
private:
    void render (float px);
    SpacepadProcessor& proc;
    juce::Image image;
    float imageScale = 0, glow = 0, t = 0;
    int imageStyle = -1;                      // fond affiché (réglage « Fond des crowds »)
    struct Dust { float x, y, v, a, r; };
    std::vector<Dust> dust;
    juce::Random rng { 29 };
};

// Les deux crowds au centre, la pastille LIÉ et les sons à toucher
class CrowdStage : public juce::Component
{
public:
    explicit CrowdStage (SpacepadProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    void refreshTiles();
private:
    struct Tile { juce::String libId, name, sub; int index = 0; bool fav = false; juce::Rectangle<float> r; };
    juce::Rectangle<float> orbRect (int slot) const;
    juce::Rectangle<float> stopRect (int slot) const;
    juce::Rectangle<float> linkRect() const;
    float S() const { return juce::jmin (getWidth() / 676.0f, getHeight() / 734.0f); }
    SpacepadProcessor& proc;
    std::vector<Tile> tiles;
};

// --------------------------------------------------------------------------------------------
class MainView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MainView (SpacepadProcessor&);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1400, H = 880;
    void chooseFolder (bool crowdTab);
    void openScan (const juce::File& folder, LibType type = LibType::Pad);   // aussi utilisé par les tests
    void openScanItems (const juce::Array<juce::File>& items, LibType type);  // fichiers et dossiers, choisis ou déposés
    void chooseFiles (bool crowdTab);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void openSettings();
    void openStage();
    void retranslate();                         // textes dans la langue choisie (FR, EN, ES)
    void applySettings();                       // couleurs, thème, taille… depuis les Paramètres
    void animateForTests (float seconds);       // fait avancer l'animation sans attendre (tests, captures)
    std::function<void (int percent)> onScaleChange;

private:
    void timerCallback() override;
    void step (float dt);
    SpacepadProcessor& proc;
    SpaceLook look;
    AnimState anim;
    Header header { proc };
    GearButton gear;
    LibraryPanel library { proc };
    SpaceView space { proc, anim };
    KeyRing ring { proc, anim };
    HookButton hook { proc };
    Visualizer viz { proc, anim };
    juce::TextButton playBtn, stopBtn, loopBtn { "LOOP" }, stopAllBtn, finBtn;
    MixBar mixBar { proc };
    MasterBar masterBar { proc };
    FxPanel fxPanel { proc };
    CenterSwitch centerSwitch;
    HallView hall { proc };
    CrowdStage crowdStage { proc };
    void setCenterView (int v);
    IconButton stageBtn { "stage" };
    std::unique_ptr<StageOverlay> stage;
    std::unique_ptr<TaskOverlay> task;
    void runTask (const juce::String& title, std::function<bool (ProgressFn)> work, std::function<void (bool)> done);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt;
    std::unique_ptr<ScanOverlay> scan;
    std::unique_ptr<SettingsOverlay> settings;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };
    int lastChanges = -1, tick = 0, lastKey = -1, lastMode = -1, lastSettings = -1, lastScale = 100;
    juce::uint32 midiFlashUntil = 0, lastMs = 0;
    bool screenSaverOff = false, dragOver = false;
};
}
