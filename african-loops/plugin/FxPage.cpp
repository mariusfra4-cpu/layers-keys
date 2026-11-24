#include "FxPage.h"

#include "Effects.h"

#include <complex>

namespace col = ui::col;
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

// ====================================================================== bouton avec nom et valeur

// Majuscules avec accents (é → É), quelle que soit la langue du système.
static juce::String upper(const juce::String& s) {
    juce::String out;
    for (auto c : s) {
        if (c >= 0xe0 && c <= 0xfe && c != 0xf7) c -= 0x20;
        else c = juce::CharacterFunctions::toUpperCase(c);
        out += juce::String::charToString(c);
    }
    return out;
}

LabeledKnob::LabeledKnob(AfricanLoopsProcessor& p, const juce::String& id, const juce::String& l, bool bipolar, juce::Colour c)
    : label(l), param(p.params.getParameter(id)), labelColour(c) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set("bipolar", bipolar);
    slider.setMouseDragSensitivity(180);
    if (param) slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    addAndMakeVisible(slider);
    att = std::make_unique<SliderAtt>(p.params, id, slider);
    slider.onValueChange = [this] { repaint(); };
}

void LabeledKnob::resized() {
    const int d = std::min(getWidth() - 8, getHeight() - 30);
    slider.setBounds((getWidth() - d) / 2, 0, d, d);
}

void LabeledKnob::paint(juce::Graphics& g) {
    auto r = getLocalBounds().withTrimmedTop(slider.getBottom() + 1);
    g.setFont(ui::font(8.f, true).withExtraKerningFactor(0.1f));
    g.setColour(labelColour);
    g.drawText(upper(label), r.removeFromTop(12), juce::Justification::centred, true);
    if (param) ui::drawGlowText(g, param->getCurrentValueAsText(), r.removeFromTop(13).toFloat(), ui::font(9.5f, true), col::amber);
}

// ====================================================================== choix

ChoiceRow::ChoiceRow(AfricanLoopsProcessor& p, const juce::String& id, juce::StringArray l)
    : proc(p), param(p.params.getParameter(id)), labels(std::move(l)) {}

void ChoiceRow::paint(juce::Graphics& g) {
    const int n = labels.size();
    if (n == 0 || !param) return;
    const int sel = juce::roundToInt(param->convertFrom0to1(param->getValue()));
    const float w = (getWidth() - (n - 1) * 6.f) / n;
    for (int i = 0; i < n; ++i) {
        auto r = juce::Rectangle<float>(i * (w + 6), 0, w, (float)getHeight()).reduced(0, 1);
        const bool on = i == sel;
        if (on) {
            g.setColour(col::amber.withAlpha(0.3f));
            g.fillRoundedRectangle(r.expanded(2), 5);
        }
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRoundedRectangle(r.translated(0, 2), 4);
        g.setGradientFill(juce::ColourGradient(on ? juce::Colour(0xffffcd7a) : juce::Colour(0xff2e2d2b), 0, r.getY(),
                                               on ? juce::Colour(0xffff9f1c) : juce::Colour(0xff191817), 0, r.getBottom(), false));
        g.fillRoundedRectangle(r, 4);
        g.setColour(on ? juce::Colour(0xff2a1600) : juce::Colour(0xff9b958b));
        g.setFont(ui::font(9.5f, true).withExtraKerningFactor(0.08f));
        g.drawText(labels[i], r, juce::Justification::centred, true);
    }
}

void ChoiceRow::mouseDown(const juce::MouseEvent& e) {
    const int n = labels.size();
    if (n == 0 || !param) return;
    const int i = juce::jlimit(0, n - 1, (int)(e.position.x / getWidth() * n));
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float)i));
    param->endChangeGesture();
    if (onChange) onChange(i);
    repaint();
}

// ====================================================================== page des effets

FxPage::FxPage(AfricanLoopsProcessor& p) : proc(p) {
    setOpaque(true);
    const char* names[] = {"MASTER FX", "SEND FX  ·  RÉVERB", "MOD FX  ·  ÉCHO", "EQ PAR PISTE", "FADE"};
    for (int i = 0; i < 5; ++i) {
        auto b = std::make_unique<ui::HwButton>(juce::String::fromUTF8(names[i]), ui::HwButton::Style::Amber);
        b->onClick = [this, i] { show((Tab)i); };
        addAndMakeVisible(*b);
        tabs.push_back(std::move(b));
    }
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(close);

    using namespace ids;
    // --- master
    toggle(masterFx, juce::String::fromUTF8("COMPRESSEUR + LIMITEUR : ACTIFS"), Master);
    knob(eqLow, "Graves", Master, true);
    knob(eqLowMid, juce::String::fromUTF8("Bas-médium"), Master, true);
    knob(eqHighMid, juce::String::fromUTF8("Haut-médium"), Master, true);
    knob(eqHigh, "Aigus", Master, true);
    knob(eqLowFreq, juce::String::fromUTF8("Fréq. graves"), Master);
    knob(compThr, "Seuil", Master);
    knob(compRatio, "Ratio", Master);
    knob(compAtk, "Attaque", Master);
    knob(compRel, juce::String::fromUTF8("Relâchement"), Master);
    knob(ceiling, "Plafond", Master);
    knob(makeup, "Gain", Master);
    // --- réverbération
    toggle(sendFx, "ACTIVE", Send);
    auto* types = new ChoiceRow(p, revType, {"STUDIO", "SALLE", juce::String::fromUTF8("ÉGLISE"), "PLAQUE"});
    owned.emplace_back(types);
    items.push_back({types, Send});
    addChildComponent(types);
    types->onChange = [this](int t) {
        // chaque type de salle règle les boutons sur un point de départ, ajustable ensuite
        struct P { float size, damp, pre, lo, hi, width; };
        static constexpr P presets[4] = {{0.35f, 0.55f, 10, 200, 9000, 0.8f},
                                         {0.6f, 0.45f, 20, 150, 8000, 1.f},
                                         {0.85f, 0.3f, 35, 120, 7000, 1.f},
                                         {0.5f, 0.2f, 5, 250, 12000, 1.f}};
        const auto& pr = presets[juce::jlimit(0, 3, t)];
        auto set = [this](const juce::String& id, float v) {
            if (auto* prm = proc.params.getParameter(id)) {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(prm->convertTo0to1(v));
                prm->endChangeGesture();
            }
        };
        set(ids::revSize, pr.size); set(ids::revDamp, pr.damp); set(ids::revPre, pr.pre);
        set(ids::revLowCut, pr.lo); set(ids::revHighCut, pr.hi); set(ids::revWidth, pr.width);
    };
    knob(revLevel, "Niveau", Send);
    knob(revWidth, "Largeur", Send);
    knob(revSize, "Taille", Send);
    knob(revDamp, "Amorti", Send);
    knob(revPre, juce::String::fromUTF8("Pré-délai"), Send);
    knob(revLowCut, "Coupe-bas", Send);
    knob(revHighCut, "Coupe-haut", Send);
    for (int i = 0; i < africanloops::kNumTracks; ++i) sendKnobs[(size_t)i] = knob(ids::send(i), juce::String(i + 1), Send);
    // --- modulation et écho
    toggle(modFx, "ACTIVE", Mod);
    auto* mt = new ChoiceRow(p, modType, {"CHORUS", "FLANGER", "PHASER", juce::String::fromUTF8("TRÉMOLO")});
    owned.emplace_back(mt);
    items.push_back({mt, Mod});
    addChildComponent(mt);
    knob(modRate, "Vitesse", Mod);
    knob(modDepth, "Profondeur", Mod);
    knob(modFb, "Retour", Mod);
    knob(modMix, "Mix", Mod);
    toggle(ids::echo, "ACTIF", Mod);
    auto* ed = new ChoiceRow(p, echoDiv, {"1/4", juce::String::fromUTF8("1/8 POINTÉE"), "1/8", "1/16"});
    owned.emplace_back(ed);
    items.push_back({ed, Mod});
    addChildComponent(ed);
    knob(echoFb, "Retour", Mod);
    knob(echoMix, "Mix", Mod);
    knob(echoLowCut, "Coupe-bas", Mod);
    toggle(echoPing, "PING-PONG", Mod);
    // --- égaliseur de chaque piste
    for (int i = 0; i < africanloops::kNumTracks; ++i) {
        lowKnobs[(size_t)i] = knob(ids::tLow(i), "Graves", TrackEq, true);
        midKnobs[(size_t)i] = knob(ids::tMid(i), juce::String::fromUTF8("Médium"), TrackEq, true);
        highKnobs[(size_t)i] = knob(ids::tHigh(i), "Aigus", TrackEq, true);
    }
    // --- fondus
    toggle(fade, "ACTIFS", Fade);
    knob(fadeIn, juce::String::fromUTF8("Entrée"), Fade);
    knob(fadeOut, "Sortie", Fade);
    toggle(xfade, "ACTIF", Fade);
    knob(xfadeLen, juce::String::fromUTF8("Durée"), Fade);

    show(Master);
}

FxPage::~FxPage() { stopTimer(); }

LabeledKnob* FxPage::knob(const juce::String& id, const juce::String& label, Tab t, bool bipolar, juce::Colour c) {
    auto* k = new LabeledKnob(proc, id, label, bipolar, c);
    owned.emplace_back(k);
    items.push_back({k, t});
    addChildComponent(k);
    return k;
}

ui::HwButton* FxPage::toggle(const juce::String& id, const juce::String& text, Tab t) {
    auto* b = new ui::HwButton(text, ui::HwButton::Style::Amber);
    b->setClickingTogglesState(true);
    owned.emplace_back(b);
    items.push_back({b, t});
    addChildComponent(b);
    buttonAtts.push_back(std::make_unique<ButtonAtt>(proc.params, id, *b));
    return b;
}

float FxPage::value(const juce::String& id) const { return proc.params.getRawParameterValue(id)->load(); }

void FxPage::show(Tab t) {
    current = t;
    for (size_t i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState((int)i == (int)t, juce::dontSendNotification);
    for (auto& it : items) it.c->setVisible(it.t == t);
    layout();
    repaint();
    startTimerHz(15);
}

void FxPage::setTrackNames(const juce::StringArray& names) {
    trackNames = names;
    for (int i = 0; i < africanloops::kNumTracks; ++i) {
        const auto n = i < names.size() && names[i].isNotEmpty() ? names[i] : juce::String::fromUTF8("—");
        if (sendKnobs[(size_t)i]) sendKnobs[(size_t)i]->label = n;
        for (auto* k : {lowKnobs[(size_t)i], midKnobs[(size_t)i], highKnobs[(size_t)i]})
            if (k) k->repaint();
        if (sendKnobs[(size_t)i]) sendKnobs[(size_t)i]->repaint();
    }
    repaint();
}

void FxPage::timerCallback() {
    if (!isShowing()) return;
    for (auto r : {eqGraph, revGraph, modGraph, echoGraph, fadeGraph, reductionBar})
        if (!r.isEmpty()) repaint(r.toNearestInt().expanded(2));
}

void FxPage::resized() { layout(); }

void FxPage::layout() {
    // onglets
    const int widths[] = {96, 140, 126, 116, 70};
    int x = 10;
    for (size_t i = 0; i < tabs.size(); ++i) {
        tabs[i]->setBounds(x, 8, widths[i], 24);
        x += widths[i] + 6;
    }
    close.setBounds(getWidth() - 166, 8, 156, 24);
    eqGraph = revGraph = modGraph = echoGraph = fadeGraph = reductionBar = {};
    // placement par onglet, dans l'ordre de création des éléments
    std::vector<juce::Component*> v;
    for (auto& it : items)
        if (it.t == current) v.push_back(it.c);
    auto at = [&](size_t i, int bx, int by, int w, int h) { if (i < v.size()) v[i]->setBounds(bx, by, w, h); };
    switch (current) {
        case Master:
            at(0, 428, 292, 344, 26);  // compresseur + limiteur actifs
            eqGraph = {12, 62, 398, 118};
            for (size_t k = 0; k < 5; ++k) at(1 + k, 12 + (int)k * 80, 190, 76, 90);
            at(6, 428, 58, 80, 86);  at(7, 512, 58, 80, 86);
            at(8, 428, 148, 80, 86); at(9, 512, 148, 80, 86);
            reductionBar = {428, 258, 164, 10};
            at(10, 620, 58, 140, 100);
            at(11, 620, 170, 140, 100);
            break;
        case Send:
            at(0, 256, 40, 86, 20);
            at(1, 12, 62, 330, 24);
            revGraph = {12, 94, 330, 96};
            at(2, 12, 200, 76, 90);
            at(3, 96, 200, 76, 90);
            for (size_t k = 0; k < 5; ++k) at(4 + k, 356 + (int)k * 82, 58, 78, 90);
            for (size_t k = 0; k < (size_t)africanloops::kNumTracks; ++k) at(9 + k, 356 + (int)k * 52, 186, 52, 84);
            break;
        case Mod:
            at(0, 300, 40, 80, 20);
            at(1, 12, 62, 370, 24);
            modGraph = {12, 94, 370, 70};
            for (size_t k = 0; k < 4; ++k) at(2 + k, 12 + (int)k * 92, 176, 80, 90);
            at(6, 690, 40, 80, 20);
            at(7, 400, 62, 370, 24);
            echoGraph = {400, 94, 370, 70};
            for (size_t k = 0; k < 3; ++k) at(8 + k, 400 + (int)k * 92, 176, 80, 90);
            at(11, 676, 210, 94, 26);
            break;
        case TrackEq:
            for (size_t i = 0; i < (size_t)africanloops::kNumTracks; ++i)
                for (size_t r = 0; r < 3; ++r) at(i * 3 + r, 92 + (int)i * 84, 62 + (int)r * 86, 80, 82);
            break;
        case Fade:
            at(0, 300, 40, 80, 20);
            fadeGraph = {12, 62, 370, 110};
            at(1, 12, 184, 84, 90);
            at(2, 104, 184, 84, 90);
            at(3, 690, 40, 80, 20);
            at(4, 400, 150, 84, 90);
            break;
    }
}

void FxPage::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2927), 0, 0, juce::Colour(0xff1b1a19), 0, b.getHeight(), false));
    g.fillRect(b);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(b.reduced(0.5f), 8, 1);
    g.setColour(juce::Colour(0xff444444));
    g.drawLine(8, 0.5f, b.getRight() - 8, 0.5f);
    auto title = [&](const char* t, int x, int y, int w = 300) {
        ui::drawEngraved(g, juce::String::fromUTF8(t), {x, y, w, 14}, 9.5f, juce::Justification::centredLeft);
    };
    auto screen = [&](juce::Rectangle<float> r) {
        ui::drawRecess(g, r.expanded(4), 6);
        ui::drawLcd(g, r);
    };
    switch (current) {
        case Master:
            title("ÉGALISEUR 4 BANDES  ·  MASTER", 12, 42);
            title("COMPRESSEUR", 428, 42, 170);
            title("LIMITEUR", 620, 42, 140);
            screen(eqGraph);
            drawEqCurve(g, eqGraph);
            {
                g.setColour(col::engraveDark);
                g.setFont(ui::font(8.f, true));
                g.drawText(juce::String::fromUTF8("RÉDUCTION"), reductionBar.translated(0, -13).withHeight(12), juce::Justification::left);
                g.setColour(juce::Colour(0xff050505));
                g.fillRoundedRectangle(reductionBar, 4);
                const float red = juce::jlimit(0.f, 20.f, proc.gainReduction()) / 20.f;
                if (red > 0.001f) {
                    g.setGradientFill(juce::ColourGradient(col::amber, reductionBar.getRight() - reductionBar.getWidth() * red, 0, col::red,
                                                           reductionBar.getRight(), 0, false));
                    g.fillRoundedRectangle(reductionBar.withTrimmedLeft(reductionBar.getWidth() * (1 - red)), 4);
                }
                g.setColour(col::amber);
                g.drawText(juce::String(proc.gainReduction(), 1) + " dB", reductionBar.translated(0, -13).withHeight(12),
                           juce::Justification::right);
            }
            break;
        case Send:
            title("RÉVERBÉRATION", 12, 42, 240);
            title("RÉGLAGES DE LA RÉVERBÉRATION", 356, 42, 400);
            title("ENVOI DE CHAQUE PISTE VERS LA RÉVERB", 356, 166, 400);
            screen(revGraph);
            drawReverb(g, revGraph);
            break;
        case Mod:
            title("MODULATION", 12, 42, 280);
            title("ÉCHO CALÉ SUR LE TEMPO", 400, 42, 280);
            screen(modGraph);
            drawMod(g, modGraph);
            screen(echoGraph);
            drawEcho(g, echoGraph);
            break;
        case TrackEq: {
            ui::drawEngraved(g, "PISTE", {12, 44, 74, 14}, 8.5f, juce::Justification::centredRight);
            const char* rowsTxt[] = {"GRAVES", "MÉDIUM", "AIGUS"};
            for (int r = 0; r < 3; ++r)
                ui::drawEngraved(g, juce::String::fromUTF8(rowsTxt[r]), {12, 86 + r * 86, 74, 14}, 8.5f, juce::Justification::centredRight);
            for (int i = 0; i < africanloops::kNumTracks; ++i) {
                const auto n = i < trackNames.size() && trackNames[i].isNotEmpty() ? trackNames[i] : juce::String::fromUTF8("—");
                ui::drawGlowText(g, n, juce::Rectangle<float>(92.f + i * 84, 44, 80, 14), ui::font(9.f, true),
                                 n == juce::String::fromUTF8("—") ? col::lcdDim : col::amber);
            }
            break;
        }
        case Fade:
            title("FONDUS AU DÉPART ET À L'ARRÊT", 12, 42, 280);
            title("FONDU ENCHAÎNÉ ENTRE DEUX LOOPS", 400, 42, 280);
            screen(fadeGraph);
            drawFade(g, fadeGraph);
            g.setColour(col::lcdText);
            g.setFont(ui::font(11.5f));
            g.drawFittedText(juce::String::fromUTF8("Quand vous choisissez une autre loop pendant la lecture, l'ancienne "
                                                    "s'efface pendant que la nouvelle monte, à partir de la mesure suivante."),
                             juce::Rectangle<int>(400, 70, 370, 60), juce::Justification::topLeft, 4);
            break;
    }
}

// ---------------------------------------------------------------- graphiques

void FxPage::drawEqCurve(juce::Graphics& g, juce::Rectangle<float> r) {
    const double fs = 48000;
    Biquad q[4];
    q[0].lowShelf(fs, value(ids::eqLowFreq), value(ids::eqLow));
    q[1].peak(fs, 300, 0.9, value(ids::eqLowMid));
    q[2].peak(fs, 2500, 0.9, value(ids::eqHighMid));
    q[3].highShelf(fs, 10000, value(ids::eqHigh));
    auto db = [&](double f) {
        const std::complex<double> z = std::polar(1.0, -2 * juce::MathConstants<double>::pi * f / fs);
        double sum = 0;
        for (auto& b : q) {
            const auto h = ((double)b.b0 + (double)b.b1 * z + (double)b.b2 * z * z) / (1.0 + (double)b.a1 * z + (double)b.a2 * z * z);
            sum += 20 * std::log10(std::max(1e-9, std::abs(h)));
        }
        return sum;
    };
    auto xOf = [&](double f) { return r.getX() + (float)(std::log10(f / 20) / std::log10(1000.0)) * r.getWidth(); };
    auto yOf = [&](double d) { return r.getCentreY() - (float)(juce::jlimit(-15.0, 15.0, d) / 15.0) * r.getHeight() * 0.45f; };
    g.setColour(col::line);
    for (int d : {-12, -6, 0, 6, 12}) g.drawHorizontalLine((int)yOf(d), r.getX(), r.getRight());
    g.setFont(ui::font(8.f));
    for (double f : {30.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0}) {
        g.setColour(col::line);
        g.drawVerticalLine((int)xOf(f), r.getY(), r.getBottom());
        g.setColour(col::engraveDark);
        g.drawText(f >= 1000 ? juce::String(f / 1000, 0) + "k" : juce::String((int)f), (int)xOf(f) + 2, (int)r.getBottom() - 11, 30, 10,
                   juce::Justification::left);
    }
    juce::Path p, fill;
    for (int i = 0; i <= 200; ++i) {
        const double f = 20 * std::pow(1000.0, i / 200.0);
        const float x = xOf(f), y = yOf(db(f));
        if (i == 0) { p.startNewSubPath(x, y); fill.startNewSubPath(x, r.getCentreY()); }
        else p.lineTo(x, y);
        fill.lineTo(x, y);
    }
    fill.lineTo(r.getRight(), r.getCentreY());
    fill.closeSubPath();
    g.setColour(col::amber.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(col::amber);
    g.strokePath(p, juce::PathStrokeType(2.f));
    const juce::Colour dots[4] = {juce::Colour(0xffff7a45), juce::Colour(0xffffd04a), juce::Colour(0xff5fb0ff), juce::Colour(0xffc79bff)};
    const double fq[4] = {value(ids::eqLowFreq), 300, 2500, 10000};
    for (int i = 0; i < 4; ++i) {
        g.setColour(dots[i]);
        g.fillEllipse(juce::Rectangle<float>(10, 10).withCentre({xOf(fq[i]), yOf(db(fq[i]))}));
    }
}

void FxPage::drawReverb(juce::Graphics& g, juce::Rectangle<float> r) {
    const float size = value(ids::revSize), damp = value(ids::revDamp), pre = value(ids::revPre), level = value(ids::revLevel);
    const bool on = value(ids::sendFx) > 0.5f;
    const float decaySec = 0.3f + 4.2f * size * size;  // durée ressentie
    const int bars = 110;
    const float preW = juce::jlimit(0.f, 0.3f, pre / 500.f) * r.getWidth();
    for (int i = 0; i < bars; ++i) {
        const float x = r.getX() + 8 + preW + i * (r.getWidth() - 16 - preW) / bars;
        const float t = i / (float)bars * 5.f;
        const float a = std::exp(-t / decaySec * 3.f) * (0.55f + 0.45f * std::abs(std::sin(i * 1.7f))) * (1 - 0.4f * damp * i / bars);
        const float h = a * (r.getHeight() - 22) * (0.4f + 0.6f * level);
        g.setColour((on ? col::amber : col::lcdDim).withAlpha(0.85f));
        g.fillRect(x, r.getBottom() - 6 - h, 1.6f, h);
    }
    g.setColour(juce::Colours::white);
    g.fillRect(r.getX() + 6, r.getY() + 14, 2.f, r.getHeight() - 20);  // le son direct
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText(juce::String::fromUTF8("PRÉ-DÉLAI ") + juce::String(juce::roundToInt(pre)) + " ms", r.reduced(8, 3), juce::Justification::topLeft);
    g.drawText(juce::String::fromUTF8("DURÉE ≈ ") + juce::String(decaySec, 1) + " s", r.reduced(8, 3), juce::Justification::topRight);
}

void FxPage::drawMod(juce::Graphics& g, juce::Rectangle<float> r) {
    const float rateHz = value(ids::modRate), depth = value(ids::modDepth);
    const bool on = value(ids::modFx) > 0.5f;
    juce::Path p;
    const float cycles = juce::jlimit(0.5f, 12.f, rateHz * 2);
    for (int i = 0; i <= 200; ++i) {
        const float x = r.getX() + 6 + i * (r.getWidth() - 12) / 200;
        const float y = r.getCentreY() - std::sin(i / 200.f * cycles * juce::MathConstants<float>::twoPi) * depth * (r.getHeight() * 0.4f);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(on ? col::amber : col::lcdDim);
    g.strokePath(p, juce::PathStrokeType(2.f));
}

void FxPage::drawEcho(juce::Graphics& g, juce::Rectangle<float> r) {
    static constexpr float divs[4] = {1.f, 0.75f, 0.5f, 0.25f};
    const float beats = divs[juce::jlimit(0, 3, (int)value(ids::echoDiv))];
    const float fb = value(ids::echoFb);
    const bool on = value(ids::echo) > 0.5f, ping = value(ids::echoPing) > 0.5f;
    const float bpm = (float)std::max(40.0, proc.displayedBPM());
    const float ms = beats * 60000.f / bpm;
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText(juce::String(juce::roundToInt(ms)) + " ms  @  " + juce::String(bpm, 0) + " BPM", r.reduced(8, 3), juce::Justification::topRight);
    for (int k = 0; k < 7; ++k) {
        const float x = r.getX() + 10 + k * beats * 40;
        if (x > r.getRight() - 8) break;
        const float h = (r.getHeight() - 22) * std::pow(fb, (float)k) * (k == 0 ? 1.f : 0.9f);
        g.setColour(k == 0 ? juce::Colours::white : (on ? col::amber : col::lcdDim));
        const float off = ping && k > 0 ? (k % 2 ? -4.f : 4.f) : 0.f;
        g.fillRect(x, r.getBottom() - 6 - h + off, 6.f, h);
    }
}

void FxPage::drawFade(juce::Graphics& g, juce::Rectangle<float> r) {
    const float in = value(ids::fadeIn), out = value(ids::fadeOut);
    const bool on = value(ids::fade) > 0.5f;
    const float total = 4 + in + out;
    const float x0 = r.getX() + 10, w = r.getWidth() - 20, yTop = r.getY() + 18, yBot = r.getBottom() - 16;
    juce::Path p;
    p.startNewSubPath(x0, yBot);
    const float xa = x0 + w * in / total, xb = x0 + w * (in + 4) / total, xc = x0 + w;
    p.cubicTo(x0 + (xa - x0) * 0.5f, yBot, x0 + (xa - x0) * 0.5f, yTop, xa, yTop);
    p.lineTo(xb, yTop);
    p.cubicTo(xb + (xc - xb) * 0.5f, yTop, xb + (xc - xb) * 0.5f, yBot, xc, yBot);
    g.setColour((on ? col::amber : col::lcdDim).withAlpha(0.15f));
    juce::Path fill(p);
    fill.closeSubPath();
    g.fillPath(fill);
    g.setColour(on ? col::amber : col::lcdDim);
    g.strokePath(p, juce::PathStrokeType(2.5f));
    g.setColour(col::lcdDim);
    g.setFont(ui::font(8.5f, true));
    g.drawText(juce::String::fromUTF8("ENTRÉE"), juce::Rectangle<float>(x0, yBot - 2, 80, 12), juce::Justification::left);
    g.drawText("SORTIE", juce::Rectangle<float>(xc - 80, yBot - 2, 80, 12), juce::Justification::right);
}
