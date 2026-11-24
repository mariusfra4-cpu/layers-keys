#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

// Bouton rotatif avec son nom et sa valeur, relié à un réglage du plugin.
class LabeledKnob : public juce::Component {
public:
    LabeledKnob(AfricanLoopsProcessor& p, const juce::String& paramId, const juce::String& label, bool bipolar = false,
                juce::Colour labelColour = ui::col::engraveDark);
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
    juce::String label;

private:
    juce::RangedAudioParameter* param;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    juce::Colour labelColour;
};

// Rangée de boutons « au choix » (type de réverbération, division de l'écho…).
class ChoiceRow : public juce::Component {
public:
    ChoiceRow(AfricanLoopsProcessor& p, const juce::String& paramId, juce::StringArray labels);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(int)> onChange;

private:
    AfricanLoopsProcessor& proc;
    juce::RangedAudioParameter* param;
    juce::StringArray labels;
};

// La page des effets : elle prend la place du mixer quand on clique sur un bouton
// du Pro Rack FX. Cinq onglets.
class FxPage : public juce::Component, private juce::Timer {
public:
    enum Tab { Master = 0, Send, Mod, TrackEq, Fade };
    explicit FxPage(AfricanLoopsProcessor& p);
    ~FxPage() override;
    void show(Tab t);
    Tab tab() const { return current; }
    std::function<void()> onClose;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setTrackNames(const juce::StringArray& names);

private:
    void timerCallback() override;
    void layout();
    LabeledKnob* knob(const juce::String& id, const juce::String& label, Tab t, bool bipolar = false,
                      juce::Colour c = ui::col::engraveDark);
    ui::HwButton* toggle(const juce::String& id, const juce::String& text, Tab t);
    void drawEqCurve(juce::Graphics&, juce::Rectangle<float>);
    void drawReverb(juce::Graphics&, juce::Rectangle<float>);
    void drawMod(juce::Graphics&, juce::Rectangle<float>);
    void drawEcho(juce::Graphics&, juce::Rectangle<float>);
    void drawFade(juce::Graphics&, juce::Rectangle<float>);
    float value(const juce::String& id) const;

    AfricanLoopsProcessor& proc;
    Tab current = Master;
    std::vector<std::unique_ptr<ui::HwButton>> tabs;
    ui::HwButton close{juce::String::fromUTF8("✕  RETOUR AU MIXER"), ui::HwButton::Style::Plain};
    struct Item {
        juce::Component* c;
        Tab t;
    };
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::array<LabeledKnob*, africanloops::kNumTracks> sendKnobs{}, lowKnobs{}, midKnobs{}, highKnobs{};
    juce::StringArray trackNames;
    // positions des graphiques (calculées dans resized)
    juce::Rectangle<float> eqGraph, revGraph, modGraph, echoGraph, fadeGraph, reductionBar;
};
