// « JB Générateur de clés » pour Windows et Mac : fabrique les clés de licence de
// Layers Keys et d'African Loops Pro. Personnel : ne jamais le donner à un client.
// Aucune connexion Internet. Mêmes clés que le générateur Android.
#include "LicenceCore.h"
#include "KeyCrypto.h"
#include "Secrets.h"

#include "../plugin/LookAndFeel.h"

#include <juce_gui_extra/juce_gui_extra.h>

using namespace ui;

namespace {

const licence::Product& productAt(int i) { return i == 0 ? licence::layersKeys() : licence::africanLoops(); }
const char* blobAt(int i) { return i == 0 ? secrets::kLayersKeys : secrets::kAfricanLoops; }

juce::File dataFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("JB Praise").getChildFile("Generateur de cles");
}

// état gardé sur cet ordinateur : graines déverrouillées (si demandé) et historique
struct Store {
    juce::File file;
    juce::var root;
    explicit Store(juce::File f) : file(std::move(f)) {
        root = juce::JSON::parse(file.loadFileAsString());
        if (!root.isObject()) root = new juce::DynamicObject();
        if (!root["history"].isArray()) root.getDynamicObject()->setProperty("history", juce::var(juce::Array<juce::var>()));
    }
    void save() {
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(root));
    }
    juce::String seedHex(int product) { return root[juce::Identifier("seed" + juce::String(product))].toString(); }
    void setSeed(int product, const juce::String& hex) {
        if (hex.isEmpty()) root.getDynamicObject()->removeProperty("seed" + juce::String(product));
        else root.getDynamicObject()->setProperty("seed" + juce::String(product), hex);
        save();
    }
    juce::Array<juce::var>& history() { return *root["history"].getArray(); }
};

class Field : public juce::TextEditor {
public:
    explicit Field(const juce::String& placeholder, bool mono = false) {
        setFont(mono ? juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 16.f, juce::Font::bold)) : ui::font(14.f));
        setIndents(12, 9);
        setTextToShowWhenEmpty(placeholder, col::lcdDim.withAlpha(0.6f));
        setColour(juce::TextEditor::textColourId, col::lcdText);
    }
};

}  // namespace

class KeygenComponent : public juce::Component, private juce::ListBoxModel {
public:
    explicit KeygenComponent(const juce::File& folder, bool testMode = false) : store(folder.getChildFile("etat.json")), test(testMode) {
        setLookAndFeel(&lnf);
        for (int i = 0; i < 2; ++i) {
            auto* b = products.add(new HwButton(productAt(i).name, HwButton::Style::Amber));
            b->setClickingTogglesState(false);
            b->onClick = [this, i] { selectProduct(i); };
            addAndMakeVisible(b);
            juce::MemoryBlock m;
            const auto hex = store.seedHex(i);
            keycrypto::Bytes s;
            if (keycrypto::fromHex(hex.toStdString(), s) && s.size() == 32) {
                std::copy(s.begin(), s.end(), seeds[(size_t)i].begin());
                unlocked[(size_t)i] = true;
            }
        }
        // déverrouillage
        addChildComponent(password);
        password.setPasswordCharacter((juce::juce_wchar)0x2022);
        password.onReturnKey = [this] { unlockButton.triggerClick(); };
        addChildComponent(keep);
        keep.setButtonText(juce::String::fromUTF8("Rester déverrouillé sur cet ordinateur"));
        keep.setToggleState(true, juce::dontSendNotification);
        keep.setColour(juce::ToggleButton::textColourId, col::lcdText);
        addChildComponent(unlockButton);
        unlockButton.setToggleState(true, juce::dontSendNotification);
        unlockButton.onClick = [this] { tryUnlock(); };
        // génération
        for (auto* c : std::initializer_list<juce::Component*>{&code, &paste, &clear, &name, &generate, &keyView, &copyKey, &copyMessage,
                                                               &histList, &exportCsv, &lockButton})
            addChildComponent(c);
        code.onReturnKey = [this] { generate.triggerClick(); };
        paste.onClick = [this] {
            const auto c = licence::normalizeCode(productAt(product), juce::SystemClipboard::getTextFromClipboard().toStdString());
            if (c.empty()) setStatus(juce::String::fromUTF8("Aucun code ") + productAt(product).codePrefix + juce::String::fromUTF8("-… dans le presse-papiers."), false);
            else code.setText(c, false);
        };
        clear.onClick = [this] {
            code.clear();
            name.clear();
            keyView.clear();
            setStatus({}, true);
        };
        generate.setToggleState(true, juce::dontSendNotification);
        generate.onClick = [this] { makeKey(); };
        keyView.setReadOnly(true);
        keyView.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.f, juce::Font::bold)));
        keyView.setMultiLine(true, true);
        copyKey.setToggleState(true, juce::dontSendNotification);
        copyKey.onClick = [this] {
            if (keyView.isEmpty()) return;
            juce::SystemClipboard::copyTextToClipboard(keyView.getText());
            setStatus(juce::String::fromUTF8("Clé copiée. Collez-la dans votre message au client."), true);
        };
        copyMessage.onClick = [this] {
            if (keyView.isEmpty()) return;
            juce::SystemClipboard::copyTextToClipboard(juce::String::fromUTF8("Voici votre clé de licence ") + productAt(product).name + " :\n\n" +
                                                        keyView.getText() + juce::String::fromUTF8("\n\nOuvrez ") + productAt(product).name +
                                                        juce::String::fromUTF8(", allez dans l'écran « Autoriser le logiciel », collez la clé et validez.\n"
                                                                               "Merci pour votre confiance !\n— JB Praise"));
            setStatus(juce::String::fromUTF8("Message complet copié (WhatsApp, e-mail…)."), true);
        };
        histList.setModel(this);
        histList.setRowHeight(24);
        histList.setColour(juce::ListBox::backgroundColourId, col::lcd);
        exportCsv.onClick = [this] { exportHistory(); };
        lockButton.onClick = [this] {
            unlocked[(size_t)product] = false;
            keycrypto::wipe(seeds[(size_t)product].data(), 32);
            store.setSeed(product, {});
            selectProduct(product);
        };
        selectProduct(1);
        setSize(920, 680);
    }

    ~KeygenComponent() override {
        histList.setModel(nullptr);
        for (auto& s : seeds) keycrypto::wipe(s.data(), 32);
        setLookAndFeel(nullptr);
    }

    // pour les tests : déverrouille avec une graine connue
    void unlockForTests(int p, const uint8_t seed[32]) {
        std::copy(seed, seed + 32, seeds[(size_t)p].begin());
        unlocked[(size_t)p] = true;
        selectProduct(p);
    }
    juce::String generateForTests(const juce::String& c, const juce::String& who) {
        code.setText(c, false);
        name.setText(who, false);
        makeKey();
        return keyView.getText();
    }
    juce::String statusText() const { return status; }
    void selectProduct(int p) {
        product = p;
        for (int i = 0; i < 2; ++i) products[i]->setToggleState(i == p, juce::dontSendNotification);
        const bool open = unlocked[(size_t)p];
        for (auto* c : std::initializer_list<juce::Component*>{&password, &keep, &unlockButton}) c->setVisible(!open);
        for (auto* c : std::initializer_list<juce::Component*>{&code, &paste, &clear, &name, &generate, &keyView, &copyKey, &copyMessage,
                                                               &histList, &exportCsv, &lockButton})
            c->setVisible(open);
        code.setTextToShowWhenEmpty(juce::String(productAt(p).codePrefix) + "-XXXX-XXXX-XXXX-XXXX", col::lcdDim.withAlpha(0.6f));
        keyView.clear();
        password.clear();
        setStatus({}, true);
        rebuildHistory();
        resized();
        repaint();
        if (!open && isShowing()) password.grabKeyboardFocus();
    }

    void paint(juce::Graphics& g) override {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2b29), 0, 0, juce::Colour(0xff151413), 0, (float)getHeight(), false));
        g.fillAll();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffe0a0), 0, 22, juce::Colour(0xffd18a2c), 0, 52, false));
        g.setFont(ui::font(26.f, true));
        g.drawText(juce::String::fromUTF8("JB Générateur de clés"), 30, 20, 500, 34, juce::Justification::left);
        g.setColour(juce::Colour(0xffa49a8a));
        g.setFont(ui::font(12.5f));
        g.drawText(juce::String::fromUTF8("By JB Praise  -  personnel : ne le donnez jamais à un client"), 30, 54, 600, 18, juce::Justification::left);
        auto label = [&](const juce::String& t, int y) {
            g.setColour(col::amber);
            g.setFont(ui::font(13.f, true));
            g.drawText(t, 30, y, 500, 18, juce::Justification::left);
        };
        label("Logiciel", 86);
        if (!unlocked[(size_t)product]) {
            label(juce::String::fromUTF8("Mot de passe du générateur"), 172);
            g.setColour(col::lcdText);
            g.setFont(ui::font(13.f));
            g.drawFittedText(juce::String::fromUTF8("Entrez le mot de passe de ") + productAt(product).name +
                                 juce::String::fromUTF8(" pour déverrouiller. Il n'est demandé qu'une fois si vous restez déverrouillé."),
                             30, 250, 600, 36, juce::Justification::topLeft, 2);
        } else {
            label(juce::String::fromUTF8("Code du client (Windows ou Mac)"), 172);
            label(juce::String::fromUTF8("Nom du client (facultatif)"), 248);
            label(juce::String::fromUTF8("Clé de licence"), 360);
        }
        if (status.isNotEmpty()) {
            g.setColour(statusOk ? col::green : col::red);
            g.setFont(ui::font(13.f));
            g.drawFittedText(status, statusArea, juce::Justification::topLeft, 2);
        }
        g.setColour(juce::Colour(0xff6f6a61));
        g.setFont(ui::font(11.f));
        g.drawText(juce::String::fromUTF8("Mêmes clés que le générateur Android. Aucune connexion Internet n'est utilisée."), 30,
                   getHeight() - 26, 700, 16, juce::Justification::left);
    }

    void resized() override {
        const int L = 30;
        products[0]->setBounds(L, 108, 190, 40);
        products[1]->setBounds(L + 196, 108, 190, 40);
        password.setBounds(L, 194, 380, 42);
        unlockButton.setBounds(L + 392, 194, 180, 42);
        keep.setBounds(L, 292, 400, 26);
        statusArea = unlocked[(size_t)product] ? juce::Rectangle<int>(L, 520, 470, 40) : juce::Rectangle<int>(L, 330, 600, 40);
        code.setBounds(L, 194, 300, 42);
        paste.setBounds(L + 308, 194, 84, 42);
        clear.setBounds(L + 398, 194, 84, 42);
        name.setBounds(L, 270, 482, 40);
        generate.setBounds(L, 318, 482, 36);
        keyView.setBounds(L, 382, 482, 64);
        copyKey.setBounds(L, 454, 236, 36);
        copyMessage.setBounds(L + 246, 454, 236, 36);
        const int hx = 540;
        histList.setBounds(hx, 108, getWidth() - hx - 30, getHeight() - 108 - 104);
        exportCsv.setBounds(hx, getHeight() - 88, 170, 34);
        lockButton.setBounds(getWidth() - 30 - 150, getHeight() - 88, 150, 34);
    }

    void paintOverChildren(juce::Graphics& g) override {
        if (!unlocked[(size_t)product]) return;
        g.setColour(col::amber);
        g.setFont(ui::font(13.f, true));
        g.drawText("Historique", 540, 86, 300, 18, juce::Justification::left);
    }

private:
    void setStatus(const juce::String& s, bool ok) {
        status = s;
        statusOk = ok;
        repaint();
    }

    void tryUnlock() {
        uint8_t s[32];
        if (!licence::unlockSeed(blobAt(product), password.getText().toStdString(), s)) {
            setStatus(juce::String::fromUTF8("Mot de passe incorrect."), false);
            return;
        }
        std::copy(s, s + 32, seeds[(size_t)product].begin());
        unlocked[(size_t)product] = true;
        if (keep.getToggleState()) store.setSeed(product, keycrypto::toHex(s, 32));
        keycrypto::wipe(s, 32);
        selectProduct(product);
    }

    void makeKey() {
        const auto& p = productAt(product);
        const auto c = licence::normalizeCode(p, code.getText().toStdString());
        if (c.empty()) {
            setStatus(juce::String::fromUTF8("Ce code n'est pas valide. Il doit ressembler à ") + p.codePrefix + "-1A2B-3C4D-5E6F-7A8B.", false);
            keyView.clear();
            return;
        }
        code.setText(c, false);
        const auto key = licence::makeKey(p, seeds[(size_t)product].data(), c);
        // contrôle : la clé est bien acceptée par le logiciel
        auto pub = keycrypto::ed25519PublicKey(seeds[(size_t)product].data());
        if (key.empty() || !licence::verifyKey(p, pub.data(), c, key)) {
            setStatus(juce::String::fromUTF8("Erreur : clé non vérifiée."), false);
            return;
        }
        keyView.setText(key, false);
        bool again = false;
        for (auto& h : store.history())
            again |= h["product"].toString() == p.codePrefix && h["code"].toString() == juce::String(c);
        if (!again) {
            auto* o = new juce::DynamicObject();
            o->setProperty("date", juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M"));
            o->setProperty("product", juce::String(p.codePrefix));
            o->setProperty("name", name.getText().trim());
            o->setProperty("code", juce::String(c));
            o->setProperty("key", juce::String(key));
            store.history().insert(0, juce::var(o));
            store.save();
        }
        setStatus(again ? juce::String::fromUTF8("Ce code avait déjà reçu une clé : voici la même.")
                        : juce::String::fromUTF8("Clé générée et enregistrée dans l'historique."),
                  true);
        rebuildHistory();
    }

    void rebuildHistory() {
        rows.clear();
        for (auto& h : store.history())
            if (h["product"].toString() == productAt(product).codePrefix) rows.add(h);
        histList.updateContent();
        histList.repaint();
    }

    int getNumRows() override { return rows.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= rows.size()) return;
        const auto& o = rows.getReference(row);
        if (selected) g.fillAll(col::amber.withAlpha(0.15f));
        g.setColour(juce::Colour(0xff1f1810));
        g.fillRect(0, h - 1, w, 1);
        g.setFont(ui::font(12.f));
        g.setColour(col::lcdDim);
        g.drawText(o["date"].toString().substring(0, 10), 8, 0, 80, h, juce::Justification::centredLeft);
        g.setColour(col::lcdText);
        const auto who = o["name"].toString();
        g.drawText((who.isNotEmpty() ? who + "  -  " : juce::String()) + o["code"].toString(), 92, 0, w - 100, h,
                   juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        if (row < 0 || row >= rows.size()) return;
        const auto& o = rows.getReference(row);
        code.setText(o["code"].toString(), false);
        name.setText(o["name"].toString(), false);
        keyView.setText(o["key"].toString(), false);
        setStatus({}, true);
    }

    void exportHistory() {
        chooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("Enregistrer l'historique"),
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                          .getChildFile(juce::String::fromUTF8("Clés JB Praise.csv")),
                                                      "*.csv");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& fc) {
                                 auto f = fc.getResult();
                                 if (f == juce::File()) return;
                                 juce::String csv = "date;logiciel;nom;code;cle\n";
                                 for (int i = store.history().size(); --i >= 0;) {
                                     const auto& o = store.history().getReference(i);
                                     csv << o["date"].toString() << ";"
                                         << (o["product"].toString() == "JBA" ? "African Loops Pro" : "Layers Keys") << ";"
                                         << o["name"].toString().replaceCharacter(';', ',') << ";" << o["code"].toString() << ";"
                                         << o["key"].toString() << "\n";
                                 }
                                 f.replaceWithText(csv);
                                 setStatus(juce::String::fromUTF8("Historique enregistré : ") + f.getFileName(), true);
                             });
    }

    ui::LookAndFeel lnf;
    Store store;
    bool test;
    int product = 1;
    std::array<std::array<uint8_t, 32>, 2> seeds{};
    std::array<bool, 2> unlocked{};
    juce::OwnedArray<HwButton> products;
    Field password{"XXXX-XXXX-XXXX-XXXX", true};
    juce::ToggleButton keep;
    HwButton unlockButton{juce::String::fromUTF8("DÉVERROUILLER"), HwButton::Style::Amber};
    Field code{"JBA-XXXX-XXXX-XXXX-XXXX", true}, name{"Nom du client (facultatif)"}, keyView{"", true};
    HwButton paste{"COLLER", HwButton::Style::Plain}, clear{"EFFACER", HwButton::Style::Plain},
        generate{juce::String::fromUTF8("GÉNÉRER LA CLÉ"), HwButton::Style::Amber},
        copyKey{juce::String::fromUTF8("COPIER LA CLÉ"), HwButton::Style::Amber},
        copyMessage{"COPIER LE MESSAGE", HwButton::Style::Plain},
        exportCsv{"EXPORTER (CSV)", HwButton::Style::Plain}, lockButton{"VERROUILLER", HwButton::Style::Plain};
    juce::ListBox histList{"historique"};
    juce::Array<juce::var> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    bool statusOk = true;
    juce::Rectangle<int> statusArea;
};

// ---------------------------------------------------------------- application

class KeygenApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return juce::String::fromUTF8("JB Générateur de clés"); }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        // --test <dossier> : contrôle automatique (graine de test), capture d'écran, puis fermeture
        if (cmd.contains("--test")) {
            const auto out = juce::File(cmd.fromFirstOccurrenceOf("--test", false, false).trim().unquoted());
            setApplicationReturnValue(runTest(out));
            quit();
            return;
        }
        window = std::make_unique<Window>(getApplicationName());
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    int runTest(const juce::File& out) {
        int fails = 0;
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("jbkeygen-test-" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
        {
            KeygenComponent k(tmp);
            uint8_t seed[32];
            for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)i;
            k.unlockForTests(0, seed);
            const auto lk = k.generateForTests("jbk 1a2b 3c4d 5e6f 7a8b", "Studio Grace");
            if (lk != "BIBB67FT-U5AOFOQQ-EJRTD5S3-ZOOQ7ZBW-ZKCCYMUX-STXPDKG3-3EL2DAT4-PY5XNHOU-DCAXCE52-6KSQXBIW-FXDEK5F5-JLYASQKQ-JVWJIBY") {
                std::printf("ÉCHEC : clé Layers Keys %s\n", lk.toRawUTF8());
                ++fails;
            }
            k.unlockForTests(1, seed);
            const auto al = k.generateForTests("Code : JBA-7F3C-91D2-4B8E-A05D", juce::String::fromUTF8("Frère Daniel - Kinshasa"));
            if (al != "2PVJM63G-KRRHQHXJ-34HS3UFF-YI4CXY7G-N5YXNCWH-CJBRLSRV-QUL4RNX6-V77XRIAN-5RHL435L-NPMVW7WT-SPNXRRYP-LXPQYXP5-CH4TICY") {
                std::printf("ÉCHEC : clé African Loops %s\n", al.toRawUTF8());
                ++fails;
            }
            if (k.generateForTests("JBK-1A2B-3C4D-5E6F-7A8B", "").isNotEmpty()) {
                std::printf("ÉCHEC : code Layers Keys accepté pour African Loops\n");
                ++fails;
            }
            k.generateForTests("JBA-7F3C-91D2-4B8E-A05D", "");
            if (!k.statusText().contains(juce::String::fromUTF8("déjà"))) {
                std::printf("ÉCHEC : code déjà servi non signalé\n");
                ++fails;
            }
            if (out != juce::File()) {
                out.createDirectory();
                auto img = k.createComponentSnapshot(k.getLocalBounds(), true, 1.f);
                juce::FileOutputStream os(out.getChildFile("generateur.png"));
                os.setPosition(0);
                os.truncate();
                juce::PNGImageFormat().writeImageToStream(img, os);
            }
        }
        {
            // nouvelle ouverture : l'historique est gardé, les graines de test aussi (non : jamais enregistrées)
            KeygenComponent k(tmp);
            k.selectProduct(1);
            if (out != juce::File()) {
                auto img = k.createComponentSnapshot(k.getLocalBounds(), true, 1.f);
                juce::FileOutputStream os(out.getChildFile("generateur_verrouille.png"));
                os.setPosition(0);
                os.truncate();
                juce::PNGImageFormat().writeImageToStream(img, os);
            }
        }
        tmp.deleteRecursively();
        std::printf(fails ? "Générateur : %d échec(s)\n" : "Générateur : tout est bon.\n", fails);
        return fails ? 1 : 0;
    }

    struct Window : juce::DocumentWindow {
        explicit Window(const juce::String& n) : juce::DocumentWindow(n, juce::Colour(0xff1d1c1a), juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new KeygenComponent(dataFolder()), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION(KeygenApp)
