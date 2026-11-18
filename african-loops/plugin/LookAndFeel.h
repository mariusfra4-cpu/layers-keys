#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

namespace col {
const juce::Colour amber{0xffffb23e}, amberDim{0xff7a4a12}, lcd{0xff0b0906}, lcdText{0xffffcf86},
    lcdDim{0xffa4865a}, engrave{0xffb9b2a6}, engraveDark{0xff6f6a61}, green{0xff5fd16a}, red{0xffff4a3a},
    yellow{0xfff0b400}, panelTop{0xff2c2b29}, panelBottom{0xff171615}, recess{0xff0e0d0c}, line{0xff2a2118};
}

juce::Font font(float size, bool bold = false);

// Écran LCD ambré, encastré dans la façade.
void drawRecess(juce::Graphics& g, juce::Rectangle<float> r, float radius = 6.f);
void drawLcd(juce::Graphics& g, juce::Rectangle<float> r, float radius = 3.f);
// Texte gravé sur la façade.
void drawEngraved(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, float size = 9.5f,
                  juce::Justification j = juce::Justification::centred, juce::Colour c = col::engrave);
// Texte lumineux sur LCD.
void drawGlowText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Font f,
                  juce::Colour c, juce::Justification j = juce::Justification::centred);
// Barre de LED (vumètre) verticale.
void drawLedBar(juce::Graphics& g, juce::Rectangle<float> r, float level, int segments, bool peakHold = false,
                float peak = 0);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 15; }
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override { return font(14.f); }
    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos,
                       int thumbSize, bool over, bool down) override;
    juce::Font getAlertWindowMessageFont() override { return font(14.f); }
};

// Bouton de façade en caoutchouc, allumé quand il est actif.
class HwButton : public juce::Button {
public:
    enum class Style { Plain, Amber, Mute, Solo, Play, Small };
    HwButton(const juce::String& label, Style s = Style::Amber) : juce::Button(label), style(s) {}
    void setLed(bool hasLed) { led = hasLed; repaint(); }
    Style style;

protected:
    void paintButton(juce::Graphics&, bool over, bool down) override;

private:
    bool led = false;
};

}  // namespace ui
