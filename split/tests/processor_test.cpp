// Test du processeur : ouverture, cache, lecture à 48 kHz, solo/muet, fin de morceau, état du projet.
#include "../Source/PluginProcessor.h"
#include <iostream>

static int failures = 0;
#define CHECK(c, msg) do { if (! (c)) { std::cout << "ECHEC : " << msg << "\n"; ++failures; } else std::cout << "ok : " << msg << "\n"; } while (0)

// exécute fn sur le fil des messages et attend la fin
static void onMsg (std::function<void()> fn)
{
    juce::WaitableEvent done;
    juce::MessageManager::callAsync ([&] { fn(); done.signal(); });
    done.wait();
}

static void waitIdle (LayersSplitProcessor& p, double maxSec)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    juce::Thread::sleep (300);
    while (p.isBusy() && juce::Time::getMillisecondCounterHiRes() - t0 < maxSec * 1000)
        juce::Thread::sleep (100);
    juce::Thread::sleep (300);
    onMsg ([] {});
}

static double renderRms (LayersSplitProcessor& p, int blocks, int bs, bool& finite)
{
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), bs);
    juce::MidiBuffer midi;
    double s = 0; long n = 0; finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; s += v * v; ++n; }
    }
    return 10 * std::log10 (s / std::max (1L, n) + 1e-20);
}

static void runTests (juce::File song)
{
    std::unique_ptr<LayersSplitProcessor> p;
    onMsg ([&] { p = std::make_unique<LayersSplitProcessor>(); });
    p->settings.quality = lsplit::Quality::fast;
    p->settings.useGpu = false;

    onMsg ([&] { p->openFile (song); });
    waitIdle (*p, 60);
    CHECK (p->getData() != nullptr, "morceau ouvert");
    onMsg ([&] { p->startSeparation(); });
    waitIdle (*p, 600);
    auto d = p->getData();
    CHECK (d != nullptr && d->hasStems, "stems disponibles : " + p->getStatus().toStdString());
    CHECK (d != nullptr && d->cacheDir.getChildFile ("Voix lead.wav").existsAsFile(), "stems enregistrés dans le cache");

    p->prepareToPlay (48000.0, 512);
    bool fin = true;
    p->play();
    const double all = renderRms (*p, 200, 512, fin);
    CHECK (fin, "aucune valeur invalide");
    CHECK (all > -40 && all < 0, "niveau de lecture normal : " + std::to_string (all) + " dB");

    p->seekSeconds (0); p->play();
    p->listenOriginal = true;
    const double orig = renderRms (*p, 200, 512, fin);
    p->listenOriginal = false;
    CHECK (std::abs (orig - all) < 0.5, "stems additionnés = original (écart " + std::to_string (orig - all) + " dB)");

    // chœurs détachés en pupitres : le mixage ne change pas
    CHECK (d->stems.present[lsplit::stemSoprano] && d->stems.present[lsplit::stemBassVoice], "pupitres du chœur calculés");
    p->choirDetached = true;
    p->seekSeconds (0); p->play();
    const double detached = renderRms (*p, 200, 512, fin);
    CHECK (std::abs (detached - all) < 0.5, "chœurs détachés = même mixage (écart " + std::to_string (detached - all) + " dB)");
    p->controls[lsplit::stemSoprano].solo = true;
    p->seekSeconds (3); p->play();
    const double sop = renderRms (*p, 100, 512, fin);
    CHECK (fin && sop < all, "solo soprano : " + std::to_string (sop) + " dB");
    p->controls[lsplit::stemSoprano].solo = false;
    p->choirDetached = false;
    // un solo resté sur un pupitre caché ne doit pas couper le son une fois regroupé
    p->controls[lsplit::stemAlto].solo = true;
    p->seekSeconds (0); p->play();
    const double regroup = renderRms (*p, 100, 512, fin);
    CHECK (regroup > -40, "solo sur pupitre caché ignoré : " + std::to_string (regroup) + " dB");
    p->controls[lsplit::stemAlto].solo = false;

    for (int k = 0; k < lsplit::numStems; ++k) p->controls[(size_t) k].mute = true;
    p->seekSeconds (0); p->play();
    const double muted = renderRms (*p, 50, 512, fin);
    CHECK (muted < -100, "tout en muet = silence");
    for (int k = 0; k < lsplit::numStems; ++k) p->controls[(size_t) k].mute = false;

    p->controls[lsplit::stemLead].solo = true;
    p->seekSeconds (3); p->play();
    const double lead = renderRms (*p, 100, 512, fin);
    p->controls[lsplit::stemLead].solo = false;
    CHECK (lead > -50 && lead < all, "solo voix lead : " + std::to_string (lead) + " dB");

    // fin du morceau : la lecture s'arrête toute seule
    p->seekSeconds (d->durationSec() - 0.05); p->play();
    renderRms (*p, 20, 512, fin);
    CHECK (! p->isPlaying(), "arrêt automatique en fin de morceau");

    // blocs de taille irrégulière (certains DAW) et fréquence 96 kHz
    p->prepareToPlay (96000.0, 256);
    p->seekSeconds (0); p->play();
    juce::AudioBuffer<float> big (p->getTotalNumOutputChannels(), 3000); juce::MidiBuffer m;
    p->processBlock (big, m);
    CHECK (big.getMagnitude (0, 0, 3000) > 0.0f, "bloc plus grand que prévu traité sans erreur");

    // sauvegarde / rechargement du projet (le plugin rouvre les stems depuis le cache)
    p->controls[lsplit::stemBass].gainDb = -6.0f;
    juce::MemoryBlock state;
    p->getStateInformation (state);
    onMsg ([&] { p.reset(); });
    std::unique_ptr<LayersSplitProcessor> q;
    onMsg ([&] { q = std::make_unique<LayersSplitProcessor>(); q->setStateInformation (state.getData(), (int) state.getSize()); });
    waitIdle (*q, 60);
    CHECK (q->getData() != nullptr && q->getData()->hasStems, "projet rechargé avec ses stems (cache)");
    CHECK (std::abs (q->controls[lsplit::stemBass].gainDb.load() + 6.0f) < 0.01f, "réglages du projet conservés");
    onMsg ([&] { q.reset(); });
}

int main (int, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File song (juce::String::fromUTF8 (argv[1]));
    juce::Thread::launch ([song]
    {
        runTests (song);
        std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT\n" : "DES TESTS ECHOUENT\n");
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    return failures == 0 ? 0 : 1;
}
