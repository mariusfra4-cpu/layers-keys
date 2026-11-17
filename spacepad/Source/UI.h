// Spacepad by JB Praise — interface "Orbit" : bibliothèques, anneau des tonalités, forme d'onde, effets, analyse.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace spacepad
{
namespace theme
{
    const juce::Colour bg { 0xff070a14 }, panel { 0xff0d1220 }, panel2 { 0xff0f1524 }, lcd { 0xff050912 };
    const juce::Colour accent { 0xff6fd8ff }, accentLight { 0xffbdf1ff }, amber { 0xffffb86b }, violet { 0xffa78bfa };
    const juce::Colour green { 0xff5cffb0 }, text { 0xffe8edf7 }, muted { 0xff8b97ad }, line { 0x18ffffff }, red { 0xffff6b6b };
    juce::Font title (float h);                   // Michroma (logo, tonalités)
    juce::Font ui (float h, int weight = 0);      // Inter : 0 normal, 1 demi-gras, 2 gras
    juce::Colour colourFor (const juce::String& name);
}

class SpaceLook : public juce::LookAndFeel_V4
{
public:
    SpaceLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override { return theme::ui (15.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::ui (14.0f, 1); }
    juce::Font getLabelFont (juce::Label&) override { return theme::ui (14.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
};

// --------------------------------------------------------------------------------------------
class Knob : public juce::Component
{
public:
    Knob (SpacepadProcessor&, const juce::String& paramId, const juce::String& label, juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    SpacepadProcessor& proc;
    juce::String id, label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// --------------------------------------------------------------------------------------------
class KeyRing : public juce::Component
{
public:
    explicit KeyRing (SpacepadProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -2; repaint(); }
    float phase = 0;   // animation
private:
    int keyAt (juce::Point<float>) const;   // 0..11, -1 = centre, -2 = rien
    SpacepadProcessor& proc;
    int hover = -2;
};

class Waveform : public juce::Component
{
public:
    explicit Waveform (SpacepadProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    SpacepadProcessor& proc;
};

class Header : public juce::Component
{
public:
    explicit Header (SpacepadProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    SpacepadProcessor& proc;
};

// --------------------------------------------------------------------------------------------
class LibraryPanel : public juce::Component, private juce::ListBoxModel
{
public:
    explicit LibraryPanel (SpacepadProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                            // relit les bibliothèques
    void showTab (bool crowdTab);
    void updateState();                        // états lumineux (son joué, mode)
    std::function<void()> onChooseFolder;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void rebuildChips();
    const Library* currentCrowd() const;
    const Category* currentCategory() const;

    SpacepadProcessor& proc;
    juce::TextButton padTab { "PAD LIBRARY" }, crowdTab { "CROWD LIBRARY" };
    juce::TextButton chooseBtn { juce::String::fromUTF8 ("Choisir un dossier\xe2\x80\xa6") };
    juce::ComboBox crowdPicker;
    std::vector<std::unique_ptr<juce::TextButton>> chips;
    juce::ListBox list;
    bool crowd = false;
    std::vector<Library> libs;
    std::vector<int> padIdx, crowdIdx;          // positions dans libs
    juce::String crowdLibId;
    int category = 0;
    int chipsBottom = 0;
    juce::Rectangle<int> statusArea, titleArea;
    int lastMode = -1;
};

// --------------------------------------------------------------------------------------------
class ScanOverlay : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    ScanOverlay (SpacepadProcessor&, const juce::File& folder);
    ~ScanOverlay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (bool saved, bool crowdFirst)> onClose;

private:
    struct Job : juce::Thread
    {
        explicit Job (juce::File f) : juce::Thread ("Spacepad analyse"), folder (std::move (f)) {}
        void run() override;
        juce::File folder;
        std::vector<Library> result;
        std::atomic<double> progress { 0 };
        juce::CriticalSection lock;
        juce::String status;
        std::atomic<bool> finished { false };
    };
    void timerCallback() override;
    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { updateTypeButton(); repaint(); }
    void updateTypeButton();
    juce::Rectangle<int> card() const;

    SpacepadProcessor& proc;
    Job job;
    std::vector<Library> results;
    bool done = false;
    juce::TextButton cancelBtn { "Annuler" }, saveBtn { "Enregistrer" }, typeBtn { "Type : Pad" };
    juce::ListBox list;
};

// --------------------------------------------------------------------------------------------
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (SpacepadProcessor&);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1400, H = 880;
    void chooseFolder();
    void openScan (const juce::File& folder);   // aussi utilisé par les tests

private:
    void timerCallback() override;
    SpacepadProcessor& proc;
    SpaceLook look;
    Header header { proc };
    LibraryPanel library { proc };
    KeyRing ring { proc };
    Waveform wave { proc };
    juce::TextButton playBtn, stopBtn, loopBtn { "LOOP" }, stopAllBtn { juce::String::fromUTF8 ("TOUT ARR\xc3\x8aTER") };
    juce::TextButton freezeBtn { "FREEZE" }, swellBtn { "SWELL" };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, freezeAtt, swellAtt;
    std::unique_ptr<ScanOverlay> scan;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastChanges = -1;
    juce::uint32 midiFlashUntil = 0;
    std::vector<juce::Point<float>> stars;
};
}
