#include "LicencePanel.h"

#include "Lang.h"

using namespace ui;

LicencePanel::LicencePanel() {
    setOpaque(false);
    setInterceptsMouseClicks(true, true);
    key.setFont(ui::font(15.f));
    key.setIndents(12, 10);
    key.setTextToShowWhenEmpty("XXXXXXXX-XXXXXXXX-XXXXXXXX-...", col::lcdDim.withAlpha(0.5f));
    key.setColour(juce::TextEditor::textColourId, col::lcdText);
    key.onReturnKey = [this] { activate.triggerClick(); };
    for (auto* c : std::initializer_list<juce::Component*>{&key, &copy, &paste, &activate, &demo, &close}) addAndMakeVisible(c);
    activate.setToggleState(true, juce::dontSendNotification);  // bouton allumé
    copy.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(Licence::get().code());
        message = TR("Code copié : collez-le dans votre message à JB Praise.");
        messageOk = true;
        repaint();
    };
    paste.onClick = [this] { key.setText(juce::SystemClipboard::getTextFromClipboard().trim(), false); };
    activate.onClick = [this] {
        const auto err = Licence::get().activate(key.getText());
        if (err.isNotEmpty()) {
            message = lang::tr(err);
            messageOk = false;
            repaint();
            return;
        }
        message = TR("Merci ! African Loops Pro est activé sur cet ordinateur.");
        messageOk = true;
        if (onDone) onDone();
    };
    demo.onClick = [this] {
        Licence::get().startDemo();
        if (onDone) onDone();
    };
    close.onClick = [this] {
        if (onDone) onDone();
    };
}

juce::Rectangle<int> LicencePanel::box() const { return getLocalBounds().withSizeKeepingCentre(700, 540); }

void LicencePanel::refresh(const Licence::Status& st) {
    demoAvailable = !st.demoUsed;
    demoEnded = st.demoUsed && st.state == Licence::State::Locked;
    closable = st.state != Licence::State::Locked;
    demo.setVisible(demoAvailable);
    close.setVisible(closable);
    repaint();
}

void LicencePanel::resized() {
    auto b = box();
    const int x = b.getX() + 92, w = b.getWidth() - 92 - 46;
    copy.setBounds(x + w - 110, b.getY() + 156, 110, 42);
    key.setBounds(x, b.getY() + 248, w - 122, 42);
    paste.setBounds(x + w - 110, b.getY() + 248, 110, 42);
    activate.setBounds(b.getCentreX() - 150, b.getY() + 316, 300, 46);
    demo.setBounds(b.getX() + 46, b.getY() + 420, 220, 40);
    close.setBounds(b.getRight() - 46, b.getY() + 12, 32, 30);
}

void LicencePanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.8f));
    auto b = box().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(b.translated(0, 10).expanded(6), 16);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2b29), 0, b.getY(), juce::Colour(0xff171615), 0, b.getBottom(), false));
    g.fillRoundedRectangle(b, 12);
    g.setColour(juce::Colour(0xff444444));
    g.drawLine(b.getX() + 12, b.getY() + 0.5f, b.getRight() - 12, b.getY() + 0.5f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(b, 12, 1);

    // titre
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffe0a0), 0, b.getY() + 30, juce::Colour(0xffd18a2c), 0, b.getY() + 62, false));
    g.setFont(ui::font(26.f, true).withExtraKerningFactor(0.14f));
    g.drawText(TR("AUTORISER LE LOGICIEL"), b.withTrimmedTop(28).withHeight(34), juce::Justification::centred);
    g.setColour(col::engraveDark.brighter(0.4f));
    g.setFont(ui::font(11.f, true).withExtraKerningFactor(0.2f));
    g.drawText("AFRICAN LOOPS PRO  -  BY JB PRAISE", b.withTrimmedTop(66).withHeight(16), juce::Justification::centred);

    const float x = b.getX() + 46, tx = b.getX() + 92;
    auto step = [&](int n, float y, const juce::String& text) {
        g.setColour(col::amber);
        g.fillEllipse(x, y, 26, 26);
        g.setColour(juce::Colour(0xff2a1600));
        g.setFont(ui::font(14.f, true));
        g.drawText(juce::String(n), juce::Rectangle<float>(x, y, 26, 26), juce::Justification::centred);
        g.setColour(juce::Colour(0xfff3e7d3));
        g.setFont(ui::font(14.f));
        g.drawText(text, juce::Rectangle<float>(tx, y, b.getRight() - tx - 40, 26), juce::Justification::centredLeft, true);
    };
    if (demoEnded) {
        g.setColour(col::red.withAlpha(0.9f));
        g.setFont(ui::font(12.5f, true));
        g.drawText(TR("La démo de 3 jours est terminée. Activez le logiciel pour continuer."), b.withTrimmedTop(92).withHeight(18),
                   juce::Justification::centred);
    }
    step(1, b.getY() + 120, TR("Envoyez ce code à JB Praise (WhatsApp, e-mail...) :"));
    auto codeBox = juce::Rectangle<float>(tx, b.getY() + 156, copy.getX() - tx - 12, 42);
    ui::drawLcd(g, codeBox, 6);
    g.setColour(juce::Colour(0x55ffb23e));
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 23.f, juce::Font::bold)));
    const auto c = Licence::get().code();
    g.drawText(c, codeBox.translated(1, 1).reduced(16, 0), juce::Justification::centredLeft);
    g.setColour(col::lcdText);
    g.drawText(c, codeBox.reduced(16, 0), juce::Justification::centredLeft);
    step(2, b.getY() + 212, TR("Collez ici la clé de licence reçue :"));

    if (message.isNotEmpty()) {
        g.setColour(messageOk ? col::green : col::red);
        g.setFont(ui::font(12.5f));
        g.drawFittedText(message, juce::Rectangle<int>((int)tx - 40, (int)b.getY() + 368, (int)b.getWidth() - 100, 32),
                         juce::Justification::centred, 2);
    }
    g.setColour(juce::Colour(0xff3a3936));
    g.drawLine(x, b.getY() + 404, b.getRight() - 46, b.getY() + 404);
    g.setColour(juce::Colour(0xffa49a8a));
    g.setFont(ui::font(12.5f));
    if (demoAvailable)
        g.drawFittedText(TR("Pas encore de clé ? Testez tout le logiciel pendant 3 jours. Une seule fois par ordinateur."),
                         juce::Rectangle<int>((int)x + 240, (int)b.getY() + 416, (int)b.getWidth() - 46 * 2 - 240, 48),
                         juce::Justification::centredLeft, 2);
    g.setFont(ui::font(11.5f));
    g.drawFittedText(TR("Une clé = un ordinateur. Aucune connexion Internet n'est nécessaire.") + "\n" +
                         TR("La même clé active la version seule et le plugin."),
                     juce::Rectangle<int>((int)b.getX() + 20, (int)b.getBottom() - 62, (int)b.getWidth() - 40, 40),
                     juce::Justification::centred, 2);
}

void DemoBadge::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    ui::drawLcd(g, b, 5);
    g.setColour(juce::Colour(0xff5a3a10));
    g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
    const float d = 8;
    g.setColour(col::amber.withAlpha(0.35f));
    g.fillEllipse(12 - 3, b.getCentreY() - d / 2 - 3, d + 6, d + 6);
    g.setColour(col::amber);
    g.fillEllipse(12, b.getCentreY() - d / 2, d, d);
    ui::drawGlowText(g, TR("DÉMO") + "  -  " + Licence::formatLeft(left), b.withTrimmedLeft(26), ui::font(11.f, true), col::amber);
}
