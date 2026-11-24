// Tests du plugin complet, comme dans un vrai logiciel de musique : un fil
// audio qui tourne sans arrêt, l'interface ouverte, des loops chargées
// pendant la lecture, la sauvegarde du projet, le glisser-déposer…
#include "TestLoops.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <thread>


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

// Fil audio qui tourne comme celui d'un logiciel de musique.
struct AudioThread {
    AfricanLoopsProcessor& p;
    int block;
    std::atomic<bool> run{true};
    std::atomic<double> energy{0};
    std::atomic<int> bad{0}, blocks{0};
    std::atomic<float> maxAbs{0};
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
                        if (std::abs(v) > maxAbs) maxAbs = std::abs(v);
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
    // petits fichiers cachés créés par les Mac : ignorés, jamais « illisibles »
    dir.getChildFile("Worship/._Praise Drums.wav").replaceWithText("Mac OS X        ATTR");
    dir.getChildFile("__MACOSX/Worship/._Praise Drums.wav").create();

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

        // 1 bis. supprimer un style : ses loops vont dans « Non classés », rien n'est effacé
        proc->removeCategory("I Love");
        lib = proc->library();
        int nonClasses = 0;
        for (auto& e : lib->entries) {
            CHECK(e.category != "I Love", "style supprimé encore présent");
            if (e.category == kUncategorized) ++nonClasses;
        }
        CHECK(nonClasses >= 2, "les loops du style supprimé ne sont pas dans Non classés");
        CHECK(dir.getChildFile("I Love/Soft Groove 96bpm.wav").existsAsFile(), "un fichier a été effacé !");

        // 2. charger une loop et la jouer
        proc->loadLoopAsync(dir.getChildFile("Worship/Praise Drums.wav"));
        CHECK(waitFor([&] { return !proc->isLoading() && proc->current(); }, 20000), "chargement trop long");
        auto cur = proc->current();
        CHECK(cur && cur->stems.size() >= 3, "%d pistes", cur ? (int)cur->stems.size() : -1);
        CHECK(proc->params.getRawParameterValue(ids::presetBpm)->load() > 0.5f, "PRESET doit être allumé par défaut");
        setParam(*proc, ids::syncHost, 0);
        waitFor([] { return false; }, 100);
        CHECK(std::abs(proc->displayedBPM() - 72) < 0.01, "la loop ne joue pas à son tempo : %.2f", proc->displayedBPM());
        setParam(*proc, ids::presetBpm, 0);
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
        // tout à fond : volume master 150 %, pistes à +3 dB, effets, chaleur → jamais au-dessus de 0 dB
        setParam(*proc, ids::master, 1.5f);
        for (int i = 0; i < kNumTracks; ++i) setParam(*proc, ids::gain(i), 1.5f);
        setParam(*proc, ids::lp, 1);
        setParam(*proc, ids::hp, 0);
        waitFor([] { return false; }, 300);
        audio->maxAbs = 0;
        waitFor([] { return false; }, 800);
        CHECK(audio->maxAbs.load() <= 0.9661f, "le son dépasse le maximum : crête %.3f", audio->maxAbs.load());
        setParam(*proc, ids::master, 1);
        for (int i = 0; i < kNumTracks; ++i) setParam(*proc, ids::gain(i), 1);
        for (auto id : {ids::masterFx, ids::sendFx, ids::modFx, ids::metronome, ids::mono}) setParam(*proc, id, 0);
        setParam(*proc, ids::lp, 1);
        setParam(*proc, ids::hp, 0);
        setParam(*proc, ids::warmth, 0);

        // 4 bis. clic sur une loop déjà analysée avec le dossier : elle doit jouer tout de suite
        {
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            proc->loadLoopAsync(dir.getChildFile("Afrobeats/Lagos Street.wav"));
            waitForFast([&] { return !proc->isLoading(); }, 5000);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf("chargement d'une loop analysée : %.0f ms\n", ms);
#ifdef NDEBUG
            CHECK(ms < 250, "trop lent : %.0f ms", ms);
#else
            CHECK(ms < 8000, "trop lent : %.0f ms (version de mise au point, plus lente)", ms);
#endif
            CHECK(proc->current() && proc->current()->file.getFileName() == "Lagos Street.wav", "mauvaise loop");
        }

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

        // 5 bis. favoris, nouveaux styles, import
        proc->setFavorite("Afrobeats/Lagos Street.wav", true);
        CHECK(proc->library()->favoriteCount() == 1, "favori non enregistré");
        CHECK(proc->addStyle("Makossa") && proc->styleFolder("Makossa").isDirectory(), "nouveau style non créé");
        CHECK(!proc->addStyle("Makossa"), "style en double accepté");
        {
            auto lib2 = proc->library();
            bool seen = false;
            for (auto& c : lib2->categories()) seen |= c.name == "Makossa";
            CHECK(seen, "le style vide n'apparaît pas");
        }
        {
            auto ext = dir.getSiblingFile(dir.getFileName() + "-import");
            writeLoop(ext.getChildFile("Douala Night.wav"), 118, 4, 9);
            CHECK(proc->importLoops({ext.getChildFile("Douala Night.wav")}, "Makossa") == 1, "import refusé");
            CHECK(waitFor([&] {
                      auto l = proc->library();
                      if (proc->isScanning() || !l) return false;
                      for (auto& e : l->entries)
                          if (e.category == "Makossa" && e.error.empty()) return true;
                      return false;
                  }, 20000),
                  "la loop importée n'est pas rangée dans son style");
            CHECK(ext.getChildFile("Douala Night.wav").existsAsFile(), "l'original importé a été déplacé");
            ext.deleteRecursively();
        }
        CHECK(proc->library()->favoriteCount() == 1, "favori perdu après l'analyse");
        for (auto& c : proc->library()->categories()) CHECK(c.name != "I Love", "le style supprimé est revenu après l'analyse");

        // 5 ter. toute la page des effets à fond : jamais de son invalide ni trop fort
        proc->loadLoopAsync(dir.getChildFile("Afrobeats/Lagos Street.wav"));
        waitFor([&] { return !proc->isLoading(); }, 10000);
        for (auto id : {ids::masterFx, ids::sendFx, ids::modFx, ids::echo, ids::fade, ids::xfade}) setParam(*proc, id, 1);
        for (auto id : {ids::eqLow, ids::eqLowMid, ids::eqHighMid, ids::eqHigh}) setParam(*proc, id, 12);
        setParam(*proc, ids::revLevel, 1);
        setParam(*proc, ids::echoFb, 0.9f);
        setParam(*proc, ids::modFb, 0.9f);
        for (int i = 0; i < kNumTracks; ++i) {
            setParam(*proc, ids::send(i), 1);
            setParam(*proc, ids::tLow(i), 12);
            setParam(*proc, ids::tHigh(i), 12);
        }
        audio->maxAbs = 0;
        for (float mt : {0.f, 1.f, 2.f, 3.f})
            for (float rt : {0.f, 1.f, 2.f, 3.f}) {
                setParam(*proc, ids::modType, mt);
                setParam(*proc, ids::revType, rt);
                waitFor([] { return false; }, 60);
            }
        audio->take();
        waitFor([] { return false; }, 300);
        CHECK(audio->take() > 1, "pas de son avec les effets");
        CHECK(audio->bad == 0 && audio->maxAbs.load() <= 0.9661f, "effets : %d invalides, crête %.3f", audio->bad.load(), audio->maxAbs.load());
        // crossfade pendant le changement de loop
        proc->loadLoopAsync(dir.getChildFile("Worship/Adoration Groove.wav"));
        waitFor([&] { return !proc->isLoading(); }, 10000);
        waitFor([] { return false; }, 600);
        CHECK(audio->bad == 0, "fondu enchaîné : son invalide");
        // la page des effets s'ouvre depuis le rack
        {
            juce::Component* page = nullptr;
            std::function<void(juce::Component&)> find = [&](juce::Component& c) {
                for (auto* ch : c.getChildren()) {
                    if (dynamic_cast<FxPage*>(ch)) page = ch;
                    find(*ch);
                }
            };
            find(*ed);
            CHECK(page != nullptr, "page des effets absente");
            if (auto* fx = dynamic_cast<FxPage*>(page)) {
                const char* names[] = {"06_fx_master.png", "07_fx_send.png", "08_fx_mod.png", "09_fx_eq_piste.png", "10_fx_fade.png"};
                for (int t = 0; t < 5; ++t) {
                    fx->show((FxPage::Tab)t);
                    fx->setVisible(true);
                    waitFor([] { return false; }, 120);
                    snapshot(*ed, shots.getChildFile(names[t]));
                }
                fx->setVisible(false);
            }
        }

        // 5 quater. setups : enregistrer, changer tout, rappeler, renommer, supprimer
        {
            const juce::String nm = "Test setup " + juce::String(juce::Random::getSystemRandom().nextInt(1 << 20));
            setParam(*proc, ids::gain(3), 0.25f);
            setParam(*proc, ids::revSize, 0.8f);
            CHECK(proc->saveSetup(nm), "setup non enregistré");
            bool listed = false;
            for (auto& si : proc->setups()) listed |= si.name == nm && si.loop.contains("Adoration");
            CHECK(listed, "setup absent de la liste");
            setParam(*proc, ids::gain(3), 1);
            setParam(*proc, ids::revSize, 0.2f);
            proc->loadLoopAsync(dir.getChildFile("coupe-decale groove.wav"));
            waitFor([&] { return !proc->isLoading(); }, 10000);
            CHECK(proc->loadSetup(nm), "setup non rappelé");
            CHECK(waitFor([&] { return !proc->isLoading() && proc->current() && proc->current()->file.getFileName() == "Adoration Groove.wav"; }, 10000),
                  "le setup n'a pas rappelé sa loop");
            CHECK(std::abs(proc->params.getRawParameterValue(ids::gain(3))->load() - 0.25f) < 1e-3, "volume du setup non rappelé");
            CHECK(std::abs(proc->params.getRawParameterValue(ids::revSize)->load() - 0.8f) < 1e-3, "réverbération du setup non rappelée");
            CHECK(proc->renameSetup(nm, nm + " bis"), "renommage refusé");
            CHECK(!proc->loadSetup(nm) && proc->loadSetup(nm + " bis"), "renommage raté");
            CHECK(proc->deleteSetup(nm + " bis"), "suppression refusée");
            for (auto& si : proc->setups()) CHECK(!si.name.startsWith(nm), "setup supprimé encore listé");
            CHECK(audio->bad == 0, "setups : son invalide");
        }
        for (auto id : {ids::masterFx, ids::sendFx, ids::modFx, ids::echo, ids::fade, ids::xfade}) setParam(*proc, id, 0);
        for (auto id : {ids::eqLow, ids::eqLowMid, ids::eqHighMid, ids::eqHigh}) setParam(*proc, id, 0);
        for (int i = 0; i < kNumTracks; ++i) {
            setParam(*proc, ids::send(i), 0);
            setParam(*proc, ids::tLow(i), 0);
            setParam(*proc, ids::tHigh(i), 0);
            setParam(*proc, ids::gain(i), 1);
        }

        proc->loadLoopAsync(dir.getChildFile("I Love/Soft Groove 96bpm.wav"));
        waitFor([&] { return !proc->isLoading(); }, 10000);

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
        CHECK(waitFor([&] { return !proc2->isScanning(); }, 10000) && proc2->library() && proc2->library()->entries.size() == 8,
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
    // Ableton Link : deux logiciels Link dans le même ordinateur
    {
        LinkSync other;
        other.setEnabled(true);
        auto proc3 = std::make_unique<AfricanLoopsProcessor>();
        proc3->setPlayConfigDetails(0, 2, 48000, 512);
        proc3->prepareToPlay(48000, 512);
        setParam(*proc3, ids::link, 1);
        CHECK(waitFor([&] { return proc3->isLinkActive(); }, 3000), "Link ne s'active pas");
        auto audio3 = std::make_unique<AudioThread>(*proc3, 512);
        const bool found = waitFor([&] { return proc3->linkPeers() >= 1 && other.peers() >= 1; }, 8000);
        std::printf("Ableton Link : %s\n", found ? "les deux logiciels se voient" : "pas de réseau local ici (normal dans ce serveur)");
        proc3->loadLoopAsync(dir.getChildFile("Afrobeats/Lagos Street.wav"));
        waitFor([&] { return !proc3->isLoading() && proc3->current(); }, 10000);
        waitFor([] { return false; }, 300);
        // la loop impose son tempo à toute la session Link
        CHECK(std::abs(proc3->displayedBPM() - 104) < 0.05, "Link : le tempo affiché n'est pas celui de la loop (%.2f)", proc3->displayedBPM());
        if (found) {
            CHECK(waitFor([&] { return std::abs(other.audioState(4, 0).tempo - 104) < 0.05; }, 3000),
                  "Link : l'autre logiciel n'a pas pris le tempo de la loop (%.2f)", other.audioState(4, 0).tempo);
            // départ demandé par l'autre logiciel → on joue, calés sur sa mesure
            other.audioRequestPlay(true, 4, 0);
            CHECK(waitFor([&] { return proc3->isPlaying(); }, 3000), "Link : le départ d'Ableton n'est pas suivi");
            other.audioRequestPlay(false, 4, 0);
            CHECK(waitFor([&] { return !proc3->isPlaying(); }, 3000), "Link : l'arrêt d'Ableton n'est pas suivi");
        }
        // départ demandé ici : silence jusqu'à la prochaine mesure, puis la loop
        proc3->setPlaying(true);
        CHECK(waitFor([&] { return proc3->isPlaying(); }, 3000), "Link : Play ne démarre pas");
        audio3->take();
        waitFor([] { return false; }, 2500);
        CHECK(audio3->take() > 1 && audio3->bad == 0, "Link : pas de son après le départ");
        // la mesure 3/4 avec Link : toujours du son valide
        setParam(*proc3, ids::signature, 1);
        waitFor([] { return false; }, 800);
        CHECK(audio3->take() > 1 && audio3->bad == 0, "Link en 3/4 : pas de son");
        proc3->setPlaying(false);
        setParam(*proc3, ids::link, 0);
        CHECK(waitFor([&] { return !proc3->isLinkActive(); }, 3000), "Link ne se désactive pas");
        audio3.reset();
    }

    // fichiers abîmés (si fournis) : jamais de plantage, juste « illisible » ou un son partiel
    if (auto* d = std::getenv("AL_TEST_BROKEN")) {
        int ok = 0, refused = 0;
        for (const auto& f : juce::File(d).findChildFiles(juce::File::findFiles, false, "*")) {
            Audio a;
            juce::String err;
            if (AfricanLoopsProcessor::loadAudio(f, a, err)) {
                auto info = analyse(a, f.getFileName().toStdString());
                auto stems = separateWith(a, info);
                Looper l(44100);
                l.load(info, stems);
                l.play();
                std::vector<float> L(8192), R(8192);
                l.render(L.data(), R.data(), 8192);
                for (float v : L) CHECK(std::isfinite(v), "%s : son invalide", f.getFileName().toRawUTF8());
                ++ok;
            } else {
                CHECK(err.isNotEmpty(), "%s : refusé sans raison", f.getFileName().toRawUTF8());
                ++refused;
            }
        }
        std::printf("fichiers abîmés : %d lus en partie, %d refusés proprement, aucun plantage\n", ok, refused);
    }

    // MP3 réels (si fournis) : nom « nom_BPM_TEMPS[_tag].mp3 » → tempo et longueur attendus
    if (auto* d = std::getenv("AL_TEST_MP3")) {
        for (const auto& f : juce::File(d).findChildFiles(juce::File::findFiles, false, "*.mp3;*.wav")) {
            auto parts = juce::StringArray::fromTokens(f.getFileNameWithoutExtension(), "_", "");
            const double bpm = parts[1].getDoubleValue();
            const int beats = parts[2].getIntValue();
            Audio a;
            juce::String err;
            CHECK(AfricanLoopsProcessor::loadAudio(f, a, err), "%s : %s", f.getFileName().toRawUTF8(), err.toRawUTF8());
            auto info = analyse(a, "loop.wav");  // nom neutre : le tempo doit venir du son ou de l'étiquette
            juce::String kinds;
            for (auto& p : info.percussions) kinds << juce::String(p.kind) << " ";
            std::printf("%s : %.2f BPM (%s), %d temps, %.3f s, %s\n", f.getFileName().toRawUTF8(), info.tempo.bpm,
                        info.tempo.source.c_str(), info.tempo.beats, a.seconds(), kinds.toRawUTF8());
            CHECK(std::abs(info.tempo.bpm - bpm) < 0.05, "%s : tempo %.2f au lieu de %.2f", f.getFileName().toRawUTF8(), info.tempo.bpm, bpm);
            CHECK(info.tempo.beats == beats, "%s : %d temps au lieu de %d", f.getFileName().toRawUTF8(), info.tempo.beats, beats);
            CHECK(info.percussions.size() >= 3, "%s : percussions non reconnues", f.getFileName().toRawUTF8());
            if (parts.contains("tag")) CHECK(info.tempo.source == tempo_source::File, "%s : étiquette BPM ignorée", f.getFileName().toRawUTF8());
            // boucle : longueur exacte au tempo, pas de trou à la jonction
            std::vector<Stem> st = separateWith(a, info);
            Looper l(44100);
            l.load(info, st);
            l.setPresetBPM(true);
            const double want = beats * 60.0 / bpm * 44100;
            CHECK(std::abs(l.loopLength() - want) < 3, "%s : boucle %.0f au lieu de %.0f", f.getFileName().toRawUTF8(), l.loopLength(), want);
            // énergie juste avant et juste après la jonction : pas de silence (trou)
            l.play();
            std::vector<float> L((size_t)want + 4410), R(L.size());
            l.render(L.data(), R.data(), (int)L.size());
            double endE = 0, startE = 0;
            for (int i = (int)want - 2205; i < (int)want; ++i) endE += L[(size_t)i] * L[(size_t)i];
            for (int i = (int)want; i < (int)want + 2205; ++i) startE += L[(size_t)i] * L[(size_t)i];
            CHECK(startE > 1e-3, "%s : trou au début du 2e tour", f.getFileName().toRawUTF8());
        }
    }

    dir.deleteRecursively();
    if (failures) {
        std::printf("%d test(s) du plugin en échec\n", failures);
        return 1;
    }
    std::printf("Tous les tests du plugin passent.\n");
    return 0;
}
