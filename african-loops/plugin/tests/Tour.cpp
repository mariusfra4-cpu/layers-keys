// Visite guidée : pilote la vraie application étape par étape et photographie
// chaque écran (interface vide, analyse d'un dossier, loops rangées, lecture…).
#include "TestLoops.h"
#include "../Licence.h"

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

void snapshot(juce::Component& c, const juce::File& out) {
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat png;
    if (os.openedOk()) png.writeImageToStream(img, os);
    std::printf("capture : %s\n", out.getFileName().toRawUTF8());
}

template <typename T>
T* findChild(juce::Component& root, const juce::String& name = {}) {
    for (auto* c : root.getChildren()) {
        if (auto* t = dynamic_cast<T*>(c); t && (name.isEmpty() || c->getName() == name)) return t;
        if (auto* r = findChild<T>(*c, name)) return r;
    }
    return nullptr;
}

void setParam(AfricanLoopsProcessor& p, const juce::String& id, float plain) {
    auto* prm = p.params.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

void pause(int ms) { waitFor([] { return false; }, ms); }

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    Licence::get().forceForTests(true);
    const juce::File shots = argc > 1 ? juce::File(argv[1]) : juce::File::getCurrentWorkingDirectory();
    shots.createDirectory();

    // un dossier de loops comme celui d'un musicien
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Mes Loops");
    dir.deleteRecursively();
    struct L { const char* path; double bpm; int bars; };
    const L loops[] = {
        {"Worship/Praise Night 72bpm.wav", 72, 4},       {"Worship/Adoration Groove.wav", 68, 4},
        {"Worship/Louange Live Drums.wav", 76, 4},       {"Worship/Holy Spirit Shaker.wav", 70, 2},
        {"Worship/Worship Slow 64bpm.wav", 64, 4},       {"Rumba/Kinshasa Soft 90bpm.wav", 90, 4},
        {"Rumba/Sebene Groove.wav", 84, 4},          {"Rumba/Rumba Kongo.wav", 88, 2},
        {"Afrobeats/Lagos Street 104bpm.wav", 104, 4},   {"Afrobeats/Naija Bounce.wav", 108, 4},
        {"Afrobeats/Accra Highlife Mix.wav", 112, 4},    {"Gospel/Gospel Choir Drums.wav", 96, 4},
        {"Gospel/Sunday Service 100bpm.wav", 100, 4},    {"Coupe-Decale/Abidjan Night.wav", 124, 4},
        {"Coupe-Decale/Decale Chic 128bpm.wav", 128, 4}, {"Amapiano/Log Drum Soweto 112bpm.wav", 112, 4},
        {"amapiano_private_school.wav", 113, 4},         {"makossa_douala_groove.wav", 118, 4},
        {"loop_sans_nom_01.wav", 95, 2},                 {"enregistrement 2024.wav", 80, 2},
    };
    unsigned seed = 1;
    for (auto& l : loops) writeLoop(dir.getChildFile(juce::String::fromUTF8(l.path)), l.bpm, l.bars, seed++);
    // quelques copies pour que l'analyse se voie à l'écran
    for (int i = 1; i <= 16; ++i)
        writeLoop(dir.getChildFile("Worship/Archives/Praise " + juce::String(i).paddedLeft('0', 2) + ".wav"), 70 + i, 4, seed++);

    AfricanLoopsProcessor proc;
    proc.setPlayConfigDetails(0, 2, 48000, 512);
    proc.prepareToPlay(48000, 512);
    setParam(proc, ids::syncHost, 0);
    std::atomic<bool> run{true};
    std::thread audio([&] {  // fil audio comme dans un logiciel de musique
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        while (run) {
            proc.processBlock(buf, midi);
            std::this_thread::sleep_for(std::chrono::microseconds(10667));
        }
    });

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(AfricanLoopsEditor::kW, AfricanLoopsEditor::kH);
    pause(300);

    // 1. l'interface au démarrage
    snapshot(*ed, shots.getChildFile("01_interface_au_demarrage.png"));

    // 2. on choisit le dossier « Mes Loops » : l'analyse commence
    proc.scanFolderAsync(dir);
    waitFor([&] { int d, n; proc.scanProgress(d, n); return n > 0 && d >= n / 3; }, 30000);
    pause(60);
    snapshot(*ed, shots.getChildFile("02_analyse_du_dossier.png"));

    // 3. analyse terminée : les loops sont rangées par style
    waitFor([&] { return !proc.isScanning(); }, 60000);
    pause(300);
    snapshot(*ed, shots.getChildFile("03_loops_rangees_par_style.png"));

    auto* styles = findChild<juce::ListBox>(*ed, "styles");
    auto* list = findChild<juce::ListBox>(*ed, "loops");
    auto selectStyle = [&](const juce::String& name) {
        auto lib = proc.library();
        auto cats = lib->categories();
        for (size_t i = 0; i < cats.size(); ++i)
            if (juce::String::fromUTF8(cats[i].name.c_str()) == name) styles->selectRow((int)i + 1);
        pause(200);
    };

    // 4. on clique sur le style « Worship »
    selectStyle("Worship");
    snapshot(*ed, shots.getChildFile("04_style_worship.png"));

    // 5. on clique sur une loop : analyse, séparation, lecture
    proc.loadLoopAsync(dir.getChildFile("Worship/Praise Night 72bpm.wav"));
    waitFor([&] { return !proc.isLoading(); }, 20000);
    setParam(proc, ids::bpm, 72);
    proc.setPlaying(true);
    pause(200);
    list->repaint();
    pause(2600);
    snapshot(*ed, shots.getChildFile("05_loop_en_lecture.png"));

    // 6. on coupe une percussion (M) et on écoute une piste seule (S)
    setParam(proc, ids::mute(2), 1);
    setParam(proc, ids::gain(3), 0.6f);
    setParam(proc, ids::pan(3), -0.4f);
    pause(1500);
    snapshot(*ed, shots.getChildFile("06_mixer_mute_volume_pan.png"));

    // 7. style « Rumba », une autre loop, tempo changé et effets
    setParam(proc, ids::mute(2), 0);
    setParam(proc, ids::gain(3), 1);
    setParam(proc, ids::pan(3), 0);
    selectStyle("Rumba");
    proc.loadLoopAsync(dir.getChildFile("Rumba/Kinshasa Soft 90bpm.wav"));
    waitFor([&] { return !proc.isLoading(); }, 20000);
    setParam(proc, ids::bpm, 96);
    setParam(proc, ids::sendFx, 1);
    setParam(proc, ids::warmth, 0.5f);
    setParam(proc, ids::metronome, 1);
    pause(2200);
    snapshot(*ed, shots.getChildFile("07_style_rumba_tempo_96.png"));

    // 8. recherche dans toute la bibliothèque
    selectStyle(juce::String::fromUTF8("Toutes les loops"));
    styles->selectRow(0);
    if (auto* search = findChild<juce::TextEditor>(*ed)) search->setText("groove", true);
    pause(400);
    snapshot(*ed, shots.getChildFile("08_recherche_groove.png"));

    // mesure : coût d'un rafraîchissement complet de l'interface (100 fois)
    {
        juce::Image img(juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 100; ++i) {
            juce::Graphics g(img);
            ed->paintEntireComponent(g, true);
        }
        std::printf("interface complète : %.1f ms par image\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 100);
        auto* wave = findChild<WaveformView>(*ed);
        t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 300; ++i) {
            juce::Graphics g(img);
            g.reduceClipRegion(wave->getScreenBounds() - ed->getScreenPosition());
            ed->paintEntireComponent(g, true);
        }
        std::printf("forme d'onde (rafraîchie pendant la lecture) : %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 300);
    }
    run = false;
    audio.join();
    ed.reset();
    dir.deleteRecursively();
    return 0;
}
