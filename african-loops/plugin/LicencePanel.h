#pragma once

#include "Licence.h"
#include "LookAndFeel.h"

// Écran « Autoriser le logiciel » : par-dessus toute la façade quand il n'y a ni licence ni démo
// en cours (ou quand on clique sur ACTIVER pendant la démo).
class LicencePanel : public juce::Component {
public:
    LicencePanel();
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(const Licence::Status& st);  // textes selon l'état
    std::function<void()> onDone;             // licence activée, démo lancée, ou fermé

private:
    juce::Rectangle<int> box() const;
    juce::TextEditor key;
    ui::HwButton copy{"COPIER", ui::HwButton::Style::Plain}, paste{"COLLER", ui::HwButton::Style::Plain},
        activate{"ACTIVER", ui::HwButton::Style::Amber}, demo{"ESSAYER 3 JOURS", ui::HwButton::Style::Plain},
        close{juce::String::fromUTF8("✕"), ui::HwButton::Style::Plain};
    juce::String message;
    bool messageOk = false, demoAvailable = true, demoEnded = false, closable = false;
};

// Petit écran « DÉMO · 2 J 14 H » en haut à gauche pendant la démo.
class DemoBadge : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    void setLeft(juce::int64 s) {
        if (s / 60 != left / 60) { left = s; repaint(); }
    }

private:
    juce::int64 left = -1;
};
