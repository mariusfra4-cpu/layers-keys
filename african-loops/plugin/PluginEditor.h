#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

juce::Colour kindColour(const juce::String& kind);

// Forme d'onde de la loop + tête de lecture. On l'attrape à la souris pour la
// déposer dans le logiciel de musique (au tempo et avec le mix entendus).
class WaveformView : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit WaveformView(AfricanLoopsProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }

private:
    AfricanLoopsProcessor& proc;
    bool dragStarted = false;
};

// Navigateur : dossier de loops, styles, recherche, liste des loops.
class BrowserPanel : public juce::Component, private juce::ListBoxModel {
public:
    explicit BrowserPanel(AfricanLoopsProcessor& p);
    ~BrowserPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refreshLibrary();
    void refreshTitle() { repaint(titleArea); }
    void chooseFolder();
    void chooseFile();

private:
    // liste des styles (catégories)
    struct CategoryModel : juce::ListBoxModel {
        BrowserPanel& owner;
        explicit CategoryModel(BrowserPanel& o) : owner(o) {}
        int getNumRows() override { return owner.categories.size(); }
        void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged(int) override { owner.rebuildLoops(); }
    };
    // ListBoxModel : liste des loops
    int getNumRows() override { return (int)rows.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override { loadRow(row); }
    void selectedRowsChanged(int row) override;

    void rebuildLoops();
    void loadRow(int row);
    juce::String selectedCategory() const;

    AfricanLoopsProcessor& proc;
    std::shared_ptr<const africanloops::Library> lib;
    juce::StringArray categories;
    std::vector<int> categoryCounts;
    std::vector<const africanloops::Entry*> rows;
    CategoryModel catModel{*this};
    juce::ListBox catList{"styles", &catModel}, loopList{"loops", this};
    juce::TextEditor search;
    ui::HwButton folderButton{"DOSSIER", ui::HwButton::Style::Plain}, fileButton{"+ FICHIER", ui::HwButton::Style::Plain};
    WaveformView wave;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> titleArea;
    bool keyboardLoad = false;

    friend class AfricanLoopsEditor;
};

// Affichage du tempo : glisser vers le haut / le bas, double-clic pour taper.
class BpmDisplay : public juce::Slider {
public:
    explicit BpmDisplay(AfricanLoopsProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    AfricanLoopsProcessor& proc;
};

// Une tranche du mixer.
class ChannelStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    ChannelStrip(AfricanLoopsProcessor& p, int index);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragStarted = false; }
    void setName(const juce::String& n) { name = n; repaint(); }
    void updateMeter();

private:
    AfricanLoopsProcessor& proc;
    int idx;
    juce::String name;
    juce::Slider pan, fader;
    ui::HwButton mute{"M", ui::HwButton::Style::Mute}, solo{"S", ui::HwButton::Style::Solo};
    std::unique_ptr<SliderAtt> panAtt, faderAtt;
    std::unique_ptr<ButtonAtt> muteAtt, soloAtt;
    float level = 0;
    bool dragStarted = false;
    juce::Rectangle<int> handle;
};

// Tableau Signature : STANDARD / DOUBLE / HALF et 4/4, 3/4, 6/8.
class SignatureTable : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit SignatureTable(AfricanLoopsProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    AfricanLoopsProcessor& proc;
};

class AfricanLoopsEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           public juce::FileDragAndDropTarget,
                           private juce::Timer {
public:
    explicit AfricanLoopsEditor(AfricanLoopsProcessor&);
    ~AfricanLoopsEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    static constexpr int kW = 1200, kH = 740;

private:
    // Toute la façade, dessinée à taille fixe puis mise à l'échelle.
    struct Face : juce::Component {
        AfricanLoopsEditor& ed;
        explicit Face(AfricanLoopsEditor& e) : ed(e) {}
        void paint(juce::Graphics&) override;
        void paintOverChildren(juce::Graphics&) override;
    };
    void timerCallback() override;
    void layout();
    void syncChannelNames();

    AfricanLoopsProcessor& proc;
    ui::LookAndFeel lnf;
    Face face{*this};
    juce::TooltipWindow tooltips{this, 600};
    juce::Image noise;

    BrowserPanel browser;
    BpmDisplay bpm;
    ui::HwButton presetBpm{"PRESET", ui::HwButton::Style::Amber}, hostSync{"HOST", ui::HwButton::Style::Plain};
    juce::Slider lp, hp, warmth;
    ui::HwButton masterFx{"MASTER FX"}, sendFx{"SEND FX"}, modFx{"MOD FX"}, fade{"FADE"};
    juce::Slider master, masterPan;
    ui::HwButton metronome{"METRO", ui::HwButton::Style::Amber}, masterMute{"M", ui::HwButton::Style::Mute},
        mono{"MONO", ui::HwButton::Style::Plain};
    SignatureTable signature;
    ui::HwButton stop{"STOP", ui::HwButton::Style::Plain}, play{"", ui::HwButton::Style::Play};
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::vector<std::unique_ptr<ButtonAtt>> buttonAtts;

    int lastLibs = -1, lastLoads = -1;
    float peakHoldL = 0, peakHoldR = 0;
    int holdCount = 0;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AfricanLoopsEditor)
};
