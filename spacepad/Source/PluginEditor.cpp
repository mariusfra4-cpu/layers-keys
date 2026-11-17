#include "PluginEditor.h"

namespace spacepad
{
SpacepadEditor::SpacepadEditor (SpacepadProcessor& p)
    : juce::AudioProcessorEditor (p), main (p)
{
    addAndMakeVisible (main);
    setResizable (true, true);
    setResizeLimits (MainView::W / 2, MainView::H / 2, MainView::W * 2, MainView::H * 2);
    getConstrainer()->setFixedAspectRatio ((double) MainView::W / MainView::H);
    // taille de départ : adaptée à l'écran (portable compris)
    int w = MainView::W, h = MainView::H;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userArea;
        const double s = juce::jmin (1.0, (area.getWidth() - 40) / (double) w, (area.getHeight() - 80) / (double) h);
        w = juce::roundToInt (w * s); h = juce::roundToInt (h * s);
    }
    setSize (w, h);
}

void SpacepadEditor::resized()
{
    const float s = (float) getWidth() / (float) MainView::W;
    main.setBounds (0, 0, MainView::W, MainView::H);
    main.setTransform (juce::AffineTransform::scale (s));
}
}
