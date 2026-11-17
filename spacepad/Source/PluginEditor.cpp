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
    main.onScaleChange = [this] (int percent) { applyScale (percent); };
    applyScale (uiSettings().scale);
}

// taille de l'interface (Paramètres) : 90 %, 100 %, 125 %, 150 %, sans jamais dépasser l'écran
void SpacepadEditor::applyScale (int percent)
{
    double s = percent / 100.0;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userArea;
        s = juce::jmin (s, (area.getWidth() - 40) / (double) MainView::W, (area.getHeight() - 80) / (double) MainView::H);
    }
    setSize (juce::roundToInt (MainView::W * s), juce::roundToInt (MainView::H * s));
}

void SpacepadEditor::resized()
{
    const float s = (float) getWidth() / (float) MainView::W;
    main.setBounds (0, 0, MainView::W, MainView::H);
    main.setTransform (juce::AffineTransform::scale (s));
}
}
