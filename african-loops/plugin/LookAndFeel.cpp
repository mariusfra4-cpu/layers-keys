#include "LookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace ui {

juce::Font font(float size, bool bold) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

void drawRecess(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    g.setColour(juce::Colour(0xff3d3b38));
    g.drawRoundedRectangle(r.translated(0, 1), radius, 1.f);  // reflet du bord bas
    g.setColour(col::recess);
    g.fillRoundedRectangle(r, radius);
    juce::ColourGradient shade(juce::Colours::black.withAlpha(0.7f), r.getX(), r.getY(),
                               juce::Colours::transparentBlack, r.getX(), r.getY() + 8, false);
    g.setGradientFill(shade);
    g.fillRoundedRectangle(r, radius);
}

void drawLcd(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    juce::ColourGradient bg(juce::Colour(0xff1b150d), r.getCentreX(), r.getY(), col::lcd, r.getCentreX(),
                            r.getY() + r.getHeight() * 0.7f, true);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, radius);
    // fines lignes horizontales de l'écran
    g.setColour(juce::Colours::white.withAlpha(0.015f));
    for (float y = r.getY(); y < r.getBottom(); y += 3) g.fillRect(r.getX(), y, r.getWidth(), 1.f);
    // reflet de la vitre
    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.06f), r.getX(), r.getY(),
                               juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.5f, false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(r, radius);
}

void drawEngraved(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, float size,
                  juce::Justification j, juce::Colour c) {
    auto f = font(size, true).withExtraKerningFactor(0.16f);
    g.setFont(f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawText(text, r.translated(0, -1), j, false);
    g.setColour(c);
    g.drawText(text, r, j, false);
}

void drawGlowText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Font f,
                  juce::Colour c, juce::Justification j) {
    g.setFont(f);
    g.setColour(c.withAlpha(0.18f));
    for (auto d : {juce::Point<float>(-1, 0), {1, 0}, {0, -1}, {0, 1}}) g.drawText(text, r.translated(d.x, d.y), j, true);
    g.setColour(c);
    g.drawText(text, r, j, true);
}

void drawLedBar(juce::Graphics& g, juce::Rectangle<float> r, float level, int segments, bool peakHold, float peak) {
    g.setColour(juce::Colour(0xff050505));
    g.fillRoundedRectangle(r, 2.f);
    auto inner = r.reduced(2.f);
    const float gap = 1.5f, h = (inner.getHeight() - gap * (segments - 1)) / segments;
    // niveau en dB : -60 .. +6 réparti sur les segments
    auto toSeg = [&](float v) {
        float db = juce::Decibels::gainToDecibels(v, -60.f);
        return (int)std::round(juce::jmap(db, -60.f, 6.f, 0.f, (float)segments));
    };
    const int lit = toSeg(level), pk = peakHold ? toSeg(peak) : -1;
    for (int s = 0; s < segments; ++s) {
        float y = inner.getBottom() - (s + 1) * h - s * gap;
        juce::Colour c = s >= segments * 0.88f ? col::red : s >= segments * 0.72f ? col::amber : juce::Colour(0xff4fcf5f);
        bool on = s < lit || s == pk - 1;
        g.setColour(on ? c : juce::Colour(0xff1a1410));
        g.fillRect(inner.getX(), y, inner.getWidth(), h);
    }
}

LookAndFeel::LookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff15120f));
    setColour(juce::PopupMenu::textColourId, col::lcdText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::amber.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextEditor::textColourId, col::lcdText);
    setColour(juce::TextEditor::highlightColourId, col::amber.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, col::amber);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff1d1c1b));
    setColour(juce::AlertWindow::textColourId, col::lcdText);
    setColour(juce::AlertWindow::outlineColourId, col::amber.withAlpha(0.4f));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e2d2b));
    setColour(juce::TextButton::textColourOffId, col::engrave);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff15120f));
    setColour(juce::TooltipWindow::textColourId, col::lcdText);
    setColour(juce::TooltipWindow::outlineColourId, col::amber.withAlpha(0.5f));
    setColour(juce::ScrollBar::thumbColourId, col::amber.withAlpha(0.4f));
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                   juce::Slider& s) {
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2.f);
    const float r = std::min(b.getWidth(), b.getHeight()) / 2;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties()["bipolar"];
    // anneau lumineux de la valeur
    {
        juce::Path bg;
        bg.addCentredArc(c.x, c.y, r * 0.9f, r * 0.9f, 0, start, end, true);
        g.setColour(juce::Colour(0xff2a2825));
        g.strokePath(bg, juce::PathStrokeType(r * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float a0 = bipolar ? (start + end) / 2 : start;
        juce::Path v;
        v.addCentredArc(c.x, c.y, r * 0.9f, r * 0.9f, 0, std::min(a0, angle), std::max(a0, angle), true);
        g.setColour(col::amber.withAlpha(0.25f));
        g.strokePath(v, juce::PathStrokeType(r * 0.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col::amber);
        g.strokePath(v, juce::PathStrokeType(r * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // corps cranté
    const float rb = r * 0.68f;
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(c.x - rb, c.y - rb + 3, rb * 2, rb * 2);
    g.setColour(juce::Colour(0xff5a5751));
    g.fillEllipse(c.x - rb, c.y - rb, rb * 2, rb * 2);
    g.setColour(juce::Colour(0xff8e8a83));
    for (int i = 0; i < 45; ++i) {
        float a = juce::MathConstants<float>::twoPi * i / 45;
        g.drawLine(c.x + std::sin(a) * rb * 0.82f, c.y - std::cos(a) * rb * 0.82f, c.x + std::sin(a) * rb,
                   c.y - std::cos(a) * rb, 1.4f);
    }
    // capuchon en aluminium
    const float rc = r * 0.54f;
    juce::ColourGradient cap(juce::Colour(0xfff2f0ec), c.x - rc * 0.3f, c.y - rc * 0.5f, juce::Colour(0xff6e6a64),
                             c.x + rc * 0.4f, c.y + rc, true);
    cap.addColour(0.45, juce::Colour(0xffb8b4ac));
    g.setGradientFill(cap);
    g.fillEllipse(c.x - rc, c.y - rc, rc * 2, rc * 2);
    // repère
    g.setColour(juce::Colour(0xff1a1a1a));
    juce::Path ind;
    ind.addRoundedRectangle(-rc * 0.07f, -rc * 0.95f, rc * 0.14f, rc * 0.5f, rc * 0.05f);
    g.fillPath(ind, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s) {
    if (style != juce::Slider::LinearVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cx = x + w * 0.5f;
    // rainure
    auto slot = juce::Rectangle<float>(cx - 3, (float)y, 6, (float)h);
    g.setColour(juce::Colour(0xff3a3936));
    g.fillRoundedRectangle(slot.translated(0, 1), 3);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(slot, 3);
    // capuchon métallique
    auto capR = juce::Rectangle<float>(28, 30).withCentre({cx, pos});
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(capR.translated(0, 4), 3);
    juce::ColourGradient m(juce::Colour(0xffd9d5cf), 0, capR.getY(), juce::Colour(0xff6e6a64), 0, capR.getBottom(), false);
    m.addColour(0.18, juce::Colour(0xff9c978f));
    m.addColour(0.2, juce::Colour(0xffd0ccc5));
    m.addColour(0.5, juce::Colour(0xff8b867f));
    m.addColour(0.8, juce::Colour(0xffc6c1ba));
    g.setGradientFill(m);
    g.fillRoundedRectangle(capR, 3);
    g.setColour(col::amber);
    g.fillRect(capR.getX(), capR.getCentreY() - 1, capR.getWidth(), 2.f);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(col::amber.withAlpha(0.35f));
    g.drawRect(0, 0, w, h);
}

void LookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(juce::Colour(0xff0b0906));
    g.fillRoundedRectangle(0, 0, (float)w, (float)h, 3);
}

void LookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& te) {
    g.setColour(te.hasKeyboardFocus(true) ? col::amber : juce::Colour(0xff6b4a1c));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.f, h - 1.f, 3, 1);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                int thumbPos, int thumbSize, bool over, bool) {
    auto r = vertical ? juce::Rectangle<int>(x + w / 2 - 2, thumbPos, 4, thumbSize)
                      : juce::Rectangle<int>(thumbPos, y + h / 2 - 2, thumbSize, 4);
    g.setColour(col::amber.withAlpha(over ? 0.6f : 0.35f));
    g.fillRoundedRectangle(r.toFloat(), 2);
}

void HwButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto b = getLocalBounds().toFloat();
    float ledSpace = led ? 14.f : 0.f;
    auto face = b.withTrimmedLeft(ledSpace).reduced(0.5f, 1.f);
    const bool on = getToggleState();
    if (led) {
        auto lr = juce::Rectangle<float>(7, 7).withCentre({4.5f, b.getCentreY()});
        g.setColour(on ? col::red : juce::Colour(0xff3a0c08));
        g.fillEllipse(lr);
        if (on) {
            g.setColour(col::red.withAlpha(0.35f));
            g.fillEllipse(lr.expanded(3));
        }
    }
    juce::Colour top{0xff2e2d2b}, bottom{0xff191817}, ink{0xff9b958b};
    if (on) {
        switch (style) {
            case Style::Mute: top = juce::Colour(0xffff9b8a); bottom = juce::Colour(0xffe23a24); ink = juce::Colours::white; break;
            case Style::Solo: top = juce::Colour(0xffffe27a); bottom = col::yellow; ink = juce::Colour(0xff231a00); break;
            case Style::Play: top = juce::Colour(0xff3fae55); bottom = juce::Colour(0xff23713a); ink = juce::Colours::white; break;
            case Style::Plain: top = juce::Colour(0xff4a4845); bottom = juce::Colour(0xff2a2826); ink = col::lcdText; break;
            case Style::Amber:
            case Style::Small:
            default: top = juce::Colour(0xffffcd7a); bottom = juce::Colour(0xffff9f1c); ink = juce::Colour(0xff2a1600); break;
        }
        if (style != Style::Plain) {
            g.setColour(bottom.withAlpha(0.35f));
            g.fillRoundedRectangle(face.expanded(3), 6);
        }
    }
    if (down) std::swap(top, bottom);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(face.translated(0, 2), 4);
    juce::ColourGradient grad(top, 0, face.getY(), bottom, 0, face.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(face, 4);
    g.setColour(juce::Colours::white.withAlpha(on ? 0.4f : 0.12f));
    g.drawLine(face.getX() + 3, face.getY() + 0.5f, face.getRight() - 3, face.getY() + 0.5f, 1);
    if (over && !on) {
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRoundedRectangle(face, 4);
    }
    const auto t = getButtonText();
    if (style == Style::Play && t.isEmpty()) {  // triangle Play
        juce::Path p;
        auto c = face.getCentre();
        p.addTriangle(c.x - 6, c.y - 8, c.x - 6, c.y + 8, c.x + 9, c.y);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff7cff8a));
        g.fillPath(p);
        return;
    }
    g.setColour(ink);
    g.setFont(font(style == Style::Small || style == Style::Mute || style == Style::Solo ? 10.5f : 9.5f, true)
                  .withExtraKerningFactor(0.1f));
    g.drawText(t, face.toNearestInt(), juce::Justification::centred, false);
}

}  // namespace ui
