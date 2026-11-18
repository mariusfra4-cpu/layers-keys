// Tests du plugin complet, comme dans un vrai logiciel de musique : un fil
// audio qui tourne sans arrêt, l'interface ouverte, des loops chargées
// pendant la lecture, la sauvegarde du projet, le glisser-déposer…
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdio>
#include <random>
#include <thread>

using namespace africanloops;

static int failures = 0;
#define CHECK(cond, ...)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            ++failures;                                           \
            std::printf("ÉCHEC ligne %d : ", __LINE__);           \
            std::printf(__VA_ARGS__);                             \
            std::printf("\n");                                    \
        }                                                         \
    } while (0)

// petite loop de batterie (kick, caisse claire, charleston, conga)
static Audio drums(double bpm, int bars, unsigned seed) {
    const int rate = 44100;
    const double six = 60 / bpm / 4;
    const int total = (int)std::lround(bars * 16 * six * rate);
    Audio a;
    a.rate = rate;
    a.L.assign((size_t)total, 0.f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> nz(-1, 1);
    for (int s = 0; s < bars * 16; ++s) {
        int st = (int)std::lround(s * six * rate);
        auto add = [&](double dur, auto f) {
            for (int i = 0; i < (int)(dur * rate) && st + i < total; ++i) a.L[(size_t)(st + i)] += (float)f(i / (double)rate);
        };
        if (s % 8 == 0 || s % 16 == 10) add(0.4, [](double t) { return 0.8 * std::sin(2 * 3.14159 * (50 * t + 2.1 * (1 - std::exp(-t / 0.03)))) * std::exp(-t / 0.12); });
        if (s % 8 == 4) add(0.25, [&](double t) { return 0.45 * nz(rng) * std::exp(-t / 0.06) + 0.35 * std::sin(2 * 3.14159 * 190 * t) * std::exp(-t / 0.05); });
        if (s % 2 == 0) { double p1 = 0, p2 = 0; add(0.1, [&](double t) { double x = nz(rng), h = x - 2 * p1 + p2; p2 = p1; p1 = x; return 0.12 * h * std::exp(-t / 0.025); }); }
        if (s % 4 == 3) add(0.4, [](double t) { return 0.5 * std::sin(2 * 3.14159 * 330 * t) * std::exp(-t / 0.09); });
    }
    a.R = a.L;
    return a;
}

static void writeLoop(const juce::File& f, double bpm, int bars, unsigned seed) {
    f.getParentDirectory().createDirectory();
    auto b = encodeWAV(drums(bpm, bars, seed), 0, 0);
    f.replaceWithData(b.data(), b.size());
}

template <typename Pred>
static bool waitFor(Pred p, int ms) {
    for (int t = 0; t < ms; t += 20) {
        if (p()) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return p();
}

// Fil audio qui tourne comme celui d'un logiciel de musique.
struct AudioThread {
    AfricanLoopsProcessor& p;
    int block;
    std::atomic<bool> run{true};
    std::atomic<double> energy{0};
    std::atomic<int> bad{0}, blocks{0};
    std::thread th;
    AudioThread(AfricanLoopsProcessor& proc, int b) : p(proc), block(b) {
        th = std::thread([this] {
            juce::AudioBuffer<float> buf(2, block);
            juce::MidiBuffer midi;
            while (run) {
                p.processBlock(buf, midi);
                double e = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i) {
                        float v = buf.getSample(c, i);
                        if (!std::isfinite(v) || std::abs(v) > 4) ++bad;
                        e += v * v;
                    }
                energy = energy + e;
                ++blocks;
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            }
        });
    }
    ~AudioThread() { run = false; th.join(); }
    double take() { double e = energy; energy = 0; return e; }
};

static void setParam(AfricanLoopsProcessor& p, const juce::String& id, float plain) {
    auto* prm = p.params.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

static void snapshot(juce::Component& c, const juce::File& out) {
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os(out);
    if (os.openedOk()) png.writeImageToStream(img, os);
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File shots = argc > 1 ? juce::File(argv[1]) : juce::File::getCurrentWorkingDirectory();
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("al-plugin-test-" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
    writeLoop(dir.getChildFile("Worship/Praise Drums.wav"), 72, 4, 1);
    writeLoop(dir.getChildFile("Worship/Adoration Groove.wav"), 88, 2, 2);
    writeLoop(dir.getChildFile("I Love/Soft Groove 96bpm.wav"), 96, 4, 3);
    writeLoop(dir.getChildFile("Afrobeats/Lagos Street.wav"), 104, 4, 4);
    writeLoop(dir.getChildFile("coupe-decale groove.wav"), 128, 4, 5);
    dir.getChildFile("I Love/abime.wav").replaceWithText("pas un vrai fichier audio");

    {
        auto proc = std::make_unique<AfricanLoopsProcessor>();
        proc->setPlayConfigDetails(0, 2, 48000, 512);
        proc->prepareToPlay(48000, 512);
        auto audio = std::make_unique<AudioThread>(*proc, 512);

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
        ed->setSize(AfricanLoopsEditor::kW, AfricanLoopsEditor::kH);
        snapshot(*ed, shots.getChildFile("01_accueil.png"));

        // 1. bibliothèque : analyse du dossier pendant que le son tourne
        proc->scanFolderAsync(dir);
        CHECK(waitFor([&] { return !proc->isScanning() && proc->library(); }, 60000), "analyse du dossier trop longue");
        auto lib = proc->library();
        CHECK(lib && lib->entries.size() == 6, "%d loops dans la bibliothèque", lib ? (int)lib->entries.size() : -1);
        int broken = 0;
        if (lib)
            for (auto& e : lib->entries) {
                if (!e.error.empty()) ++broken;
                if (e.path == "I Love/Soft Groove 96bpm.wav") CHECK(e.category == "I Love" && e.info.tempo.bpm == 96, "I Love : %s %.2f", e.category.c_str(), e.info.tempo.bpm);
                if (e.path == "Worship/Praise Drums.wav") CHECK(std::abs(e.info.tempo.bpm - 72) < 0.01, "Praise : %.2f BPM", e.info.tempo.bpm);
            }
        CHECK(broken == 1, "le fichier abîmé n'est pas signalé (%d)", broken);

        // 2. charger une loop et la jouer
        proc->loadLoopAsync(dir.getChildFile("Worship/Praise Drums.wav"));
        CHECK(waitFor([&] { return !proc->isLoading() && proc->current(); }, 20000), "chargement trop long");
        auto cur = proc->current();
        CHECK(cur && cur->stems.size() >= 3, "%d pistes", cur ? (int)cur->stems.size() : -1);
        setParam(*proc, ids::syncHost, 0);
        setParam(*proc, ids::bpm, 72);
        proc->setPlaying(true);
        waitFor([] { return false; }, 400);
        audio->take();
        waitFor([] { return false; }, 400);
        CHECK(audio->take() > 1, "pas de son à la lecture");
        snapshot(*ed, shots.getChildFile("02_lecture.png"));

        // 3. tout couper → silence ; solo d'une piste → seulement elle
        for (int i = 0; i < kNumTracks; ++i) setParam(*proc, ids::mute(i), 1);
        waitFor([] { return false; }, 300);
        audio->take();
        waitFor([] { return false; }, 300);
        CHECK(audio->take() < 1e-9, "son entendu alors que tout est coupé");
        for (int i = 0; i < kNumTracks; ++i) setParam(*proc, ids::mute(i), 0);
        setParam(*proc, ids::mute(4), 1);
        setParam(*proc, ids::solo(1), 1);
        waitFor([] { return false; }, 200);
        snapshot(*ed, shots.getChildFile("03_mute_solo.png"));
        setParam(*proc, ids::solo(1), 0);
        setParam(*proc, ids::mute(4), 0);

        // 4. tous les effets, filtres, Double/Half, métronome : jamais de son invalide
        for (auto id : {ids::masterFx, ids::sendFx, ids::modFx, ids::metronome, ids::mono}) setParam(*proc, id, 1);
        setParam(*proc, ids::lp, 0.4f);
        setParam(*proc, ids::hp, 0.3f);
        setParam(*proc, ids::warmth, 0.8f);
        for (float feel : {1.f, 2.f, 0.f}) {
            setParam(*proc, ids::feel, feel);
            waitFor([] { return false; }, 250);
        }
        for (float bpm : {60.f, 140.f, 200.f, 90.f}) {
            setParam(*proc, ids::bpm, bpm);
            waitFor([] { return false; }, 150);
        }
        CHECK(audio->bad == 0, "%d échantillons invalides", audio->bad.load());
        for (auto id : {ids::masterFx, ids::sendFx, ids::modFx, ids::metronome, ids::mono}) setParam(*proc, id, 0);
        setParam(*proc, ids::lp, 1);
        setParam(*proc, ids::hp, 0);
        setParam(*proc, ids::warmth, 0);

        // 5. changer de loop pendant la lecture, très vite (comme avec les flèches)
        for (auto name : {"Worship/Adoration Groove.wav", "Afrobeats/Lagos Street.wav", "coupe-decale groove.wav", "I Love/Soft Groove 96bpm.wav"})
            proc->loadLoopAsync(dir.getChildFile(name));
        CHECK(waitFor([&] { return !proc->isLoading(); }, 20000), "chargements en rafale bloqués");
        cur = proc->current();
        CHECK(cur && cur->file.getFileName() == "Soft Groove 96bpm.wav", "la dernière loop choisie n'est pas celle jouée : %s",
              cur ? cur->file.getFileName().toRawUTF8() : "-");
        proc->loadLoopAsync(dir.getChildFile("I Love/abime.wav"));
        waitFor([&] { return !proc->isLoading(); }, 5000);
        CHECK(proc->loadError().isNotEmpty() && proc->current() == cur, "fichier abîmé mal géré");

        // 6. glisser vers le logiciel de musique
        setParam(*proc, ids::bpm, 120);
        waitFor([] { return false; }, 100);
        auto f = proc->bounceToFile(-1);
        Audio back;
        std::string err;
        CHECK(f.existsAsFile() && loadWAV(f.getFullPathName().toStdString(), back, err), "fichier glissé absent : %s", err.c_str());
        CHECK(back.metaBPM == 120 && std::abs(back.seconds() - 16 * 0.5) < 0.01, "fichier glissé : %.2f BPM, %.3f s", back.metaBPM, back.seconds());
        CHECK(f.getFileName().contains("120 BPM"), "nom : %s", f.getFileName().toRawUTF8());
        auto one = proc->bounceToFile(0);
        CHECK(one.existsAsFile() && one.getFileName().contains("KICK"), "piste seule : %s", one.getFileName().toRawUTF8());

        // 7. son très court : rejoué en boucle sur la grille
        {
            Audio shot;
            shot.L.assign(8820, 0.f);
            for (size_t i = 0; i < shot.L.size(); ++i) shot.L[i] = 0.6f * std::sin((float)i * 0.03f) * std::exp(-(float)i / 2500.f);
            shot.R = shot.L;
            auto sf = dir.getChildFile("coup.wav");
            auto bytes = encodeWAV(shot, 0, 0);
            sf.replaceWithData(bytes.data(), bytes.size());
            proc->loadLoopAsync(sf);
            waitFor([&] { return !proc->isLoading(); }, 5000);
            audio->take();
            waitFor([] { return false; }, 600);
            CHECK(audio->take() > 0.01, "le son court ne se répète pas");
        }

        // 8. sauvegarde du projet puis réouverture
        proc->loadLoopAsync(dir.getChildFile("Afrobeats/Lagos Street.wav"));
        waitFor([&] { return !proc->isLoading(); }, 10000);
        setParam(*proc, ids::gain(2), 0.5f);
        juce::MemoryBlock state;
        proc->getStateInformation(state);
        audio.reset();
        ed.reset();

        auto proc2 = std::make_unique<AfricanLoopsProcessor>();
        proc2->setPlayConfigDetails(0, 2, 44100, 256);
        proc2->setStateInformation(state.getData(), (int)state.getSize());
        proc2->prepareToPlay(44100, 256);  // autre fréquence d'échantillonnage
        CHECK(waitFor([&] { return !proc2->isLoading() && proc2->current(); }, 10000), "projet rouvert sans sa loop");
        CHECK(proc2->current() && proc2->current()->file.getFileName() == "Lagos Street.wav", "mauvaise loop rouverte");
        CHECK(proc2->library() != nullptr, "bibliothèque non rouverte tout de suite");
        CHECK(waitFor([&] { return !proc2->isScanning(); }, 10000) && proc2->library() && proc2->library()->entries.size() == 7,
              "la loop ajoutée depuis n'est pas trouvée");
        CHECK(std::abs(proc2->params.getRawParameterValue(ids::gain(2))->load() - 0.5f) < 1e-4, "réglage perdu");
        auto audio2 = std::make_unique<AudioThread>(*proc2, 256);
        proc2->setPlaying(true);
        waitFor([] { return false; }, 300);
        audio2->take();
        waitFor([] { return false; }, 300);
        CHECK(audio2->take() > 1 && audio2->bad == 0, "pas de son après réouverture à 44,1 kHz");

        // 9. interface rouverte : capture d'écran avec une loop en lecture
        std::unique_ptr<juce::AudioProcessorEditor> ed2(proc2->createEditor());
        ed2->setSize(AfricanLoopsEditor::kW, AfricanLoopsEditor::kH);
        waitFor([] { return false; }, 500);
        snapshot(*ed2, shots.getChildFile("04_projet_rouvert.png"));
        ed2->setSize(AfricanLoopsEditor::kW * 3 / 4, AfricanLoopsEditor::kH * 3 / 4);  // fenêtre plus petite
        waitFor([] { return false; }, 100);
        snapshot(*ed2, shots.getChildFile("05_petite_fenetre.png"));
        audio2.reset();
        ed2.reset();
    }
    dir.deleteRecursively();
    if (failures) {
        std::printf("%d test(s) du plugin en échec\n", failures);
        return 1;
    }
    std::printf("Tous les tests du plugin passent.\n");
    return 0;
}
