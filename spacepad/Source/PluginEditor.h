// Spacepad by JB Praise — fenêtre du plugin : l'interface Orbit, agrandissable sans perdre ses proportions.
#pragma once
#include "UI.h"

namespace spacepad
{
class SpacepadEditor : public juce::AudioProcessorEditor
{
public:
    explicit SpacepadEditor (SpacepadProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }
    MainView& view() { return main; }
    void applyScale (int percent);

private:
    MainView main;
};
}
