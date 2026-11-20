// Layers Split — fenêtre principale (identique en application autonome et en VST3).
#pragma once

#include "PluginProcessor.h"
#include "Core/Text.h"
#include "UI/Theme.h"
#include "UI/TrackView.h"

class LayersSplitEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit LayersSplitEditor (LayersSplitProcessor&);
    ~LayersSplitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return tracks.isInterestedInFileDrag (f); }
    void filesDropped (const juce::StringArray& f, int x, int y) override { tracks.filesDropped (f, x, y); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void chooseFile();
    void syncFromProcessor();
    void updateQualityButtons();
    void exportStems (int preset);
    void paintCard (juce::Graphics&, juce::Rectangle<int>, const juce::String& title,
                    const juce::String& value, const juce::String& sub) const;

    juce::Rectangle<int> sideArea() const;
    juce::Rectangle<int> cardsArea() const;

    LayersSplitProcessor& proc;
    lsplit::theme::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // barre du haut
    juce::TextButton openBtn { lsplit::U ("Ouvrir…") }, playBtn { "Lecture" }, stopBtn { "Stop" };
    juce::TextButton abBtn { "A/B : Original" }, loopBtn { "Boucle" }, followBtn { "Suivre le DAW" };
    juce::Slider master;
    juce::Label timeLabel;

    // centre
    lsplit::TrackView tracks;

    // panneau de droite
    juce::TextButton qFast { "Rapide" }, qHigh { "Haute" }, qUltra { "Ultra" };
    juce::Label qualityInfo;
    juce::ToggleButton optVocals { lsplit::U ("Voix lead / chœurs") }, optDrums { "Batterie et basse" }, optGpu { "Carte graphique (GPU)" };
    juce::TextButton separateBtn { lsplit::U ("SÉPARER") };
    juce::ComboBox format;
    juce::TextButton expAll { "Tous les stems" }, expAcapella { "Acapella" }, expInst { "Instrumental" },
                     expKaraoke { "Sans voix lead" }, expMix { "Mixage actuel" }, expFolder { "Dossier des stems" };

    std::unique_ptr<juce::FileChooser> chooser;
    lsplit::PlayDataPtr shownData;
    bool wasBusy = false;
    std::atomic<bool> exporting { false };
    juce::String exportStatus;
    bool exportError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayersSplitEditor)
};
