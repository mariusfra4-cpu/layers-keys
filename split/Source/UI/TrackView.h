// Layers Split — vue des pistes : forme d'onde du mix, règle de temps, une piste par stem.
#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace lsplit
{

class StemLaneHeader : public juce::Component, public juce::SettableTooltipClient
{
public:
    StemLaneHeader (LayersSplitProcessor& p, int stemId);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void refresh();
    juce::File stemFile() const;

private:
    LayersSplitProcessor& proc;
    const int id;
    juce::TextButton soloBtn { "S" }, muteBtn { "M" };
    juce::Slider gain;
    bool dragStarted = false;
};

class TrackView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TrackView (LayersSplitProcessor& p);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void refresh();        // nouvelles données
    void updatePlayhead(); // appelé ~30 fois par seconde

    std::function<void()> onOpenRequested;

private:
    juce::Rectangle<int> overviewArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> laneArea (int stem) const;   // stem : identifiant ; zone vide si la piste est cachée
    void updateVisibleLanes();
    juce::Rectangle<int> waveColumn() const; // zone horizontale des formes d'onde
    void drawWave (juce::Graphics&, const Peaks&, juce::Rectangle<int>, juce::Colour, float gain) const;
    void seekFromX (int x);

    LayersSplitProcessor& proc;
    juce::OwnedArray<StemLaneHeader> headers;
    PlayDataPtr data;
    std::vector<int> visible;   // stems affichés, dans l'ordre
    int lastPlayheadX = -1;
    bool dragOver = false;

    static constexpr int headerWidth = 270;
    static constexpr int overviewH = 96;
    static constexpr int rulerH = 22;
};

} // namespace lsplit
