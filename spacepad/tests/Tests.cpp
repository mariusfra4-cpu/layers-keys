// Spacepad by JB Praise — tests automatiques.
// Lancer : SpacepadTests [dossier_de_sortie]   (capture de l'interface en PNG dans ce dossier)
#include <juce_audio_utils/juce_audio_utils.h>
#include "KeyDetect.h"
#include "Library.h"
#include "PadEngine.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Lang.h"
#include "AudioOpen.h"
#include <map>

using namespace spacepad;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  ECHEC : " << msg << "\n"; } } while (0)

static void section (const char* s) { std::cout << "\n== " << s << "\n"; }

// ---------------------------------------------------------------------------------------------
// sons de test
// ---------------------------------------------------------------------------------------------
static double midiHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// accord tenu façon pad : notes + harmoniques, léger désaccord, fondu d'entrée
static juce::AudioBuffer<float> padChord (std::vector<int> notes, double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rnd (42);
    for (int note : notes)
        for (int h = 1; h <= 4; ++h)
            for (int det = -1; det <= 1; det += 2)
            {
                const double f = midiHz (note) * h * (1.0 + det * 0.0015);
                const double ph = rnd.nextDouble() * 6.28;
                const float a = 0.12f / (float) (h * h) / (float) notes.size();
                for (int i = 0; i < n; ++i)
                {
                    const float v = a * (float) std::sin (ph + juce::MathConstants<double>::twoPi * f * i / sr);
                    b.addSample (det < 0 ? 0 : 1, i, v);
                }
            }
    for (int i = 0; i < juce::jmin (n, (int) sr); ++i)
        for (int c = 0; c < 2; ++c) b.setSample (c, i, b.getSample (c, i) * (float) i / (float) sr);
    return b;
}

static juce::AudioBuffer<float> noise (double sr, double seconds, bool sweep = false)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random r (3);
    float lp = 0;
    for (int i = 0; i < n; ++i)
    {
        const float a = sweep ? (float) i / n : 1.0f;
        const float x = (r.nextFloat() * 2 - 1);
        lp += (x - lp) * (sweep ? 0.05f + 0.9f * a : 0.3f);
        b.setSample (0, i, 0.3f * lp * a);
        b.setSample (1, i, 0.3f * (r.nextFloat() * 2 - 1) * 0.6f * a);
    }
    return b;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

static std::vector<int> triad (int root, int mode, int octave = 4)
{
    const int r = 12 * (octave + 1) + root;
    return mode == 0 ? std::vector<int> { r - 12, r, r + 4, r + 7 } : std::vector<int> { r - 12, r, r + 3, r + 7 };
}

// ---------------------------------------------------------------------------------------------
static void testNames()
{
    section ("Tonalite dans les noms de fichiers");
    struct T { const char* name; int key; int mode; };
    const T cases[] = {
        { "C", 0, -1 }, { "Pad - C#", 1, -1 }, { "Warm Pad Db", 1, -1 }, { "Em", 4, 1 }, { "Pad_E_minor", 4, 1 },
        { "Ambient Pad A - D minor", 2, 1 }, { "F# Major", 6, 0 }, { "Gbmaj", 6, 0 }, { "Bbm", 10, 1 }, { "Abm7", 8, 1 },
        { "Pad Do", 0, -1 }, { "Pad R\xc3\xa9 mineur", 2, 1 }, { "Mi b\xc3\xa9mol majeur", 3, 0 }, { "Sol#m", 8, 1 },
        { "Fa diese mineur", 6, 1 }, { "Pad Re menor", 2, 1 }, { "Sol mayor", 7, 0 }, { "Pad C3", 0, -1 }, { "E_m", 4, 1 }, { "(Am)", 9, 1 }, { "Pad A", 9, -1 },
        { "B flat minor", 10, 1 }, { "C sharp", 1, -1 }, { "Pad-D-Maj", 2, 0 }, { "AM", 9, 0 }, { "Shimmer Pad - Eb", 3, -1 },
        { "Choir Ahh", -1, -1 }, { "Applause Big", -1, -1 }, { "La Gloire Room", -1, -1 }, { "Riser 01", -1, -1 },
        { "A New Day", -1, -1 }, { "Bass Drop", -1, -1 }, { "Ambience", -1, -1 }, { "pad 7", -1, -1 },
    };
    for (auto& c : cases)
    {
        auto k = keyFromName (juce::String::fromUTF8 (c.name));
        const bool ok = k.key == c.key && (c.key < 0 || k.mode == c.mode);
        CHECK (ok, juce::String::fromUTF8 (c.name) << " -> " << keyLabel (k.key, k.mode) << " (mode " << k.mode << "), attendu " << keyLabel (c.key, c.mode) << " (mode " << c.mode << ")");
    }
    CHECK (modeFromFolderName ("Minor Keys") == 1, "dossier Minor");
    CHECK (modeFromFolderName ("Majeur") == 0, "dossier Majeur");
    CHECK (modeFromFolderName ("Warm Pads") == -1, "dossier sans mode");
}

static void testAudioKeys()
{
    section ("Tonalite a l'ecoute");
    const double sr = 48000;
    int good = 0, total = 0;
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode)
        {
            auto b = padChord (triad (root, mode), sr, 6);
            juce::AudioBuffer<float> mono (1, b.getNumSamples());
            mono.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
            mono.addFrom (0, 0, b, 1, 0, b.getNumSamples());
            auto r = keyFromAudio (mono, sr);
            ++total;
            if (r.key.key == root && r.key.mode == mode) ++good;
            else std::cout << "  (info) " << keyLabel (root, mode) << " entendu comme " << keyLabel (r.key.key, r.key.mode) << "\n";
            CHECK (r.tonalStrength > 0.45f, "force tonale d'un accord " << keyLabel (root, mode) << " = " << r.tonalStrength);
        }
    std::cout << "  accords reconnus : " << good << " / " << total << "\n";
    CHECK (good >= 22, "au moins 22 accords sur 24 reconnus (" << good << ")");
    auto n = noise (sr, 6);
    juce::AudioBuffer<float> mono (1, n.getNumSamples());
    mono.copyFrom (0, 0, n, 0, 0, n.getNumSamples());
    auto r = keyFromAudio (mono, sr);
    std::cout << "  bruit (foule/ambiance) : force tonale " << r.tonalStrength << "\n";
    CHECK (r.tonalStrength < 0.4f, "un bruit n'a pas de tonalite (" << r.tonalStrength << ")");
}

static juce::File makeTestLibrary (const juce::File& root)
{
    root.deleteRecursively();
    const double sr = 44100;
    // 1) pads nommés : majeur dans un dossier, mineur dans un autre
    for (int k = 0; k < 12; ++k)
        writeWav (root.getChildFile ("Mes Pads/Warm Pad/Major/Warm Pad " + keyName (k) + ".wav"), padChord (triad (k, 0), sr, 4), sr);
    for (int k = 0; k < 12; k += 2)
        writeWav (root.getChildFile ("Mes Pads/Warm Pad/Minor/Warm Pad " + keyName (k) + "m.wav"), padChord (triad (k, 1), sr, 4), sr);
    // 2) pads sans tonalité dans le nom (on écoute)
    writeWav (root.getChildFile ("Mes Pads/Pad Mystere/texture 1.wav"), padChord (triad (7, 0), sr, 5), sr);
    writeWav (root.getChildFile ("Mes Pads/Pad Mystere/texture 2.wav"), padChord (triad (9, 1), sr, 5), sr);
    // 3) crowds : sous-dossiers = catégories
    writeWav (root.getChildFile ("Mes Crowds/Worship FX/Riser 01.wav"), noise (sr, 4, true), sr);
    writeWav (root.getChildFile ("Mes Crowds/Worship FX/Swell Em.wav"), padChord (triad (4, 1), sr, 3), sr);
    writeWav (root.getChildFile ("Mes Crowds/Natural/Room 01.wav"), noise (sr, 4), sr);
    writeWav (root.getChildFile ("Mes Crowds/Natural/Applause.wav"), noise (sr, 3), sr);
    root.getChildFile ("Mes Crowds/Natural/notes.txt").replaceWithText ("pas un son");
    return root;
}

static void testFolders (const juce::File& root)
{
    section ("Analyse d'un dossier complet");
    FolderAnalyzer a;
    auto libs = a.analyze (root);
    for (auto& l : libs)
        std::cout << "  - " << l.name << " [" << (l.type == LibType::Pad ? "PAD" : "CROWD") << "] " << l.summary() << "\n";
    auto find = [&] (const char* n) -> const Library* { for (auto& l : libs) if (l.name == n) return &l; return nullptr; };
    CHECK (libs.size() == 3, "3 librairies trouvees (" << (int) libs.size() << ")");
    auto* warm = find ("Warm Pad");
    CHECK (warm && warm->type == LibType::Pad, "Warm Pad est une Pad Library");
    if (warm)
    {
        CHECK (warm->numSounds() == 18, "Warm Pad : 18 sons");
        CHECK (warm->exactSlots (0) == 12, "Warm Pad : 12 tonalites majeures exactes (" << warm->exactSlots (0) << ")");
        CHECK (warm->exactSlots (1) == 6, "Warm Pad : 6 tonalites mineures exactes (" << warm->exactSlots (1) << ")");
        auto s = warm->slotFor (1, 1); // C#m absent : transposé depuis Cm ou Dm
        CHECK (s.sound >= 0 && ! s.exact && std::abs (s.semitones) == 1, "C#m joue par transposition d'un demi-ton");
        auto* snd = warm->sound (s.sound);
        CHECK (snd && snd->mode == 1, "la transposition garde le mode mineur");
    }
    auto* myst = find ("Pad Mystere");
    CHECK (myst && myst->type == LibType::Pad, "Pad Mystere reconnu comme pad (nom)");
    if (myst)
    {
        auto* s1 = myst->sound (0); auto* s2 = myst->sound (1);
        CHECK (s1 && s1->key == 7 && s1->mode == 0 && s1->keySource == "son", "texture 1 entendue en G majeur (" << keyLabel (s1 ? s1->key : -1, s1 ? s1->mode : -1) << ")");
        CHECK (s2 && s2->key == 9 && s2->mode == 1, "texture 2 entendue en A mineur (" << keyLabel (s2 ? s2->key : -1, s2 ? s2->mode : -1) << ")");
    }
    auto* crowd = find ("Mes Crowds");
    CHECK (crowd && crowd->type == LibType::Crowd, "Mes Crowds est une Crowd Library");
    if (crowd)
    {
        CHECK (crowd->categories.size() == 2, "2 categories");
        bool hasFx = false, hasNat = false;
        for (auto& c : crowd->categories) { hasFx |= c.name == "Worship FX"; hasNat |= c.name == "Natural"; }
        CHECK (hasFx && hasNat, "categories Worship FX et Natural (noms des sous-dossiers)");
        for (auto& c : crowd->categories)
            for (auto& s : c.sounds)
            {
                if (s.name == "Swell Em") CHECK (s.key == 4 && s.mode == 1, "Swell Em : tonalite lue dans le nom");
                if (s.name == "Room 01") CHECK (s.key < 0, "Room 01 : ambiance sans tonalite");
            }
    }

    // un dossier de pads choisi directement
    FolderAnalyzer b;
    auto one = b.analyze (root.getChildFile ("Mes Pads/Pad Mystere"));
    CHECK (one.size() == 1 && one[0].name == "Pad Mystere", "un seul dossier choisi : une librairie a son nom");
}

static void testStore (const juce::File& root)
{
    section ("Enregistrement commun (application + VST3 + AU)");
    FolderAnalyzer a;
    auto libs = a.analyze (root);
    LibraryStore s1;
    CHECK (s1.file().getParentDirectory() == juce::File (juce::SystemStats::getEnvironmentVariable ("SPACEPAD_DATA", {})), "dossier commun utilise");
    s1.addOrReplace (libs);
    s1.addOrReplace (libs); // deux fois : remplace, ne double pas
    LibraryStore s2;       // un autre "plugin" ouvert ensuite
    CHECK (s2.snapshot().size() == libs.size(), "le plugin retrouve les " << (int) libs.size() << " librairies (" << (int) s2.snapshot().size() << ")");
    auto id = libs[0].id;
    CHECK (s2.find (id) != nullptr && s2.find (id)->numSounds() == libs[0].numSounds(), "contenu identique apres relecture");
    juce::Thread::sleep (1100);
    s1.remove (id);
    CHECK (s2.reloadIfChanged(), "le plugin voit le changement fait par l'application");
    CHECK (s2.find (id) == nullptr, "librairie retiree partout");
    s1.addOrReplace (libs);
}

static void testLoopSeam()
{
    section ("Boucle sans clic");
    auto d = std::make_shared<SampleData>();
    d->sampleRate = 48000;
    const int n = 48000 * 3 + 777;           // longueur volontairement "tordue"
    d->buffer.setSize (2, n);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c) d->buffer.setSample (c, i, 0.5f * (float) std::sin (i * 0.0371 + c));
    PadChannel ch;
    ch.prepare (48000);
    ch.start (d, 0, true, 0.01f, 0.01f);
    juce::AudioBuffer<float> out (2, 512);
    float maxJump = 0, prev = 0, peak = 0;
    bool first = true;
    for (int blk = 0; blk < 48000 * 10 / 512; ++blk)  // 10 s = plus de 3 tours de boucle
    {
        out.clear();
        ch.render (out, nullptr, nullptr, 0, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float v = out.getSample (0, i);
            if (! first && blk > 4) maxJump = juce::jmax (maxJump, std::abs (v - prev));
            prev = v; first = false;
            peak = juce::jmax (peak, std::abs (v));
        }
    }
    std::cout << "  plus grand saut entre deux echantillons : " << maxJump << " (sinus seul : ~0.019)\n";
    CHECK (maxJump < 0.05f, "pas de clic a la jointure de la boucle");
    CHECK (ch.isPlaying(), "le son tourne toujours en boucle apres 10 s");

    // un crowd qui monte puis s'éteint (comme une foule enregistrée) : la boucle ne doit jamais retomber ni « recommencer »
    {
        auto c = std::make_shared<SampleData>();
        c->sampleRate = 48000;
        const int len = 48000 * 8;
        c->buffer.setSize (2, len);
        juce::Random rnd (9);
        for (int i = 0; i < len; ++i)
        {
            const double t = i / 48000.0;
            const double envv = t < 0.4 ? 0.0 : t < 1.6 ? (t - 0.4) / 1.2 : t < 6.0 ? 1.0 : juce::jmax (0.0, 1.0 - (t - 6.0) / 2.0);   // silence, montée, plein, extinction
            for (int ch2 = 0; ch2 < 2; ++ch2) c->buffer.setSample (ch2, i, (float) (envv * (rnd.nextFloat() - 0.5f) * 0.6f));
        }
        computeLoopPoints (*c);
        std::cout << "  crowd : boucle de " << juce::String (c->loopStart / 48000.0, 2) << " s a " << juce::String ((c->loopEnd + c->loopXfade) / 48000.0, 2)
                  << " s, fondu " << juce::String (c->loopXfade / 48000.0, 2) << " s\n";
        CHECK (c->loopStart >= (int) (1.2 * 48000) && c->loopEnd + c->loopXfade <= (int) (6.7 * 48000), "la boucle evite la montee et l'extinction du son");
        PadChannel cc;
        cc.prepare (48000);
        cc.start (c, 0, true, 0.01f, 0.01f);
        std::vector<float> lv;
        for (int blk = 0; blk < 48000 * 30 / 512; ++blk)   // 30 s : plusieurs tours
        {
            out.clear();
            cc.render (out, nullptr, nullptr, 0, 512);
            if (blk * 512 > 48000 * 9) lv.push_back (out.getRMSLevel (0, 0, 512));   // après le premier passage
        }
        // niveau sur 0,2 s glissant
        float lo = 1e9f, hi = 0;
        for (size_t i = 0; i + 19 < lv.size(); i += 19)
        {
            double e = 0; for (size_t k = 0; k < 19; ++k) e += lv[i + k] * lv[i + k];
            const float r = (float) std::sqrt (e / 19);
            lo = juce::jmin (lo, r); hi = juce::jmax (hi, r);
        }
        std::cout << "  crowd en boucle : niveau mini / maxi = " << juce::String (lo / juce::jmax (1e-6f, hi), 2) << "\n";
        CHECK (lo > hi * 0.7f, "le crowd tourne en continu : pas de creux ni de reprise audible (" << lo / juce::jmax (1e-6f, hi) << ")");
        CHECK (cc.isPlaying(), "le crowd tourne toujours apres 30 s");
    }

    // sans boucle : le son s'arrête à la fin
    PadChannel once;
    once.prepare (48000);
    once.start (d, 0, false, 0.01f, 0.01f);
    for (int blk = 0; blk < 48000 * 4 / 512; ++blk) { out.clear(); once.render (out, nullptr, nullptr, 0, 512); }
    CHECK (! once.isPlaying(), "sans LOOP le son s'arrete a la fin");
}

static float rms (const juce::AudioBuffer<float>& b) { return (b.getRMSLevel (0, 0, b.getNumSamples()) + b.getRMSLevel (1, 0, b.getNumSamples())) * 0.5f; }

static void testProcessor()
{
    section ("Moteur complet (comme dans Logic / Ableton)");
    SpacepadProcessor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    juce::String warmId;
    for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") warmId = l.id;
    CHECK (warmId.isNotEmpty(), "Warm Pad present dans la bibliotheque du plugin");
    p.selectPadLibrary (warmId, 0);
    p.selectPadLibrary (warmId, 1);
    p.setMode (0);
    p.pressKey (0);
    p.waitIdleForTests (3000);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    float peak = 0; bool nan = false;
    juce::AudioBuffer<float> sec (2, 48000);
    for (int i = 0; i < 48000 * 3 / 256; ++i)
    {
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 256; ++s) { auto v = buf.getSample (c, s); nan |= ! std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }
        if (i >= (48000 * 2 / 256) && i < (48000 * 2 / 256) + 48000 / 256)
            sec.copyFrom (0, (i - 48000 * 2 / 256) * 256, buf, 0, 0, 256), sec.copyFrom (1, (i - 48000 * 2 / 256) * 256, buf, 1, 0, 256);
    }
    std::cout << "  niveau apres 2 s : " << rms (sec) << ", crete " << peak << "\n";
    CHECK (! nan, "aucune valeur invalide");
    CHECK (rms (sec) > 0.01f, "le pad joue");
    CHECK (peak <= 1.0f, "jamais de saturation en sortie");
    CHECK (p.padSoundName() == "Warm Pad C", "C majeur -> fichier Warm Pad C (" << p.padSoundName() << ")");

    // MIDI : une note jouée au-dessus de la zone de commande ne change pas la tonalité (le pad reste net)
    midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 10);
    buf.clear(); p.processBlock (buf, midi); midi.clear();
    juce::Thread::sleep (60);
    CHECK (p.currentKey() == 0, "une melodie jouee hors de la zone ne change pas la tonalite");
    // la touche Ré de la zone de commande (Ré2 = note 38) change la tonalité
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
    buf.clear(); p.processBlock (buf, midi); midi.clear();
    juce::Thread::sleep (60);
    p.waitIdleForTests (3000);
    CHECK (p.currentKey() == 2, "touche Re de la zone de commande -> tonalite D");
    CHECK (p.padSoundName() == "Warm Pad D", "fichier Warm Pad D (" << p.padSoundName() << ")");

    // passage en mineur : D mineur existe
    p.setMode (1);
    p.waitIdleForTests (3000);
    CHECK (p.padSoundName() == "Warm Pad Dm", "mode mineur -> Warm Pad Dm (" << p.padSoundName() << ")");
    // fondu : pas de trou de son pendant le changement
    float minRms = 1;
    for (int i = 0; i < 48000 * 2 / 256; ++i)
    {
        buf.clear(); p.processBlock (buf, midi);
        if (i > 10) minRms = juce::jmin (minRms, rms (buf));
    }
    CHECK (minRms > 0.002f, "fondu entre tonalites sans silence (" << minRms << ")");

    // effets au maximum : toujours propre
    for (auto id : { "shimmer", "warm", "air", "reverb", "motion", "width", "sub", "lowcut" })
        p.params.getParameter (id)->setValueNotifyingHost (1.0f);
    p.params.getParameter ("freeze")->setValueNotifyingHost (1.0f);
    p.params.getParameter ("swell")->setValueNotifyingHost (1.0f);
    peak = 0; nan = false;
    for (int i = 0; i < 48000 * 3 / 256; ++i)
    {
        buf.clear(); p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int s = 0; s < 256; ++s) { auto v = buf.getSample (c, s); nan |= ! std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }
    }
    CHECK (! nan && peak <= 1.0f, "tous les effets a fond : pas de valeur invalide ni de saturation (crete " << peak << ")");

    // sauvegarde dans un projet puis réouverture
    juce::MemoryBlock state;
    p.getStateInformation (state);
    SpacepadProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (q.padLibraryId (0) == warmId && q.padLibraryId (1) == warmId, "le projet rouvre les memes librairies (majeur et mineur)");
    CHECK (q.currentKey() == 2 && q.currentMode() == 1, "le projet rouvre la meme tonalite");
    CHECK (q.params.getParameter ("reverb")->getValue() > 0.99f, "le projet rouvre les memes effets");

    // arrêt
    p.stopAll();
    for (int i = 0; i < 48000 * 8 / 256; ++i) { buf.clear(); p.processBlock (buf, midi); }
    CHECK (! p.padSounding(), "Stop : le pad s'eteint en fondu");
}

static void snapshot (const juce::File& outDir, const juce::File& root)
{
    section ("Capture de l'interface");
    outDir.createDirectory();
    setLang (Lang::FR);
    SpacepadProcessor p;
    p.prepareToPlay (48000, 256);
    for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") { p.selectPadLibrary (l.id, 0); p.selectPadLibrary (l.id, 1); }
    p.setMode (1);
    p.pressKey (4);
    p.waitIdleForTests (3000);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < 400; ++i) { buf.clear(); p.processBlock (buf, midi); }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (MainView::W, MainView::H);
        auto& v = dynamic_cast<SpacepadEditor&> (*ed).view();
        v.animateForTests (4.0f);
        auto img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto f = outDir.getChildFile ("spacepad_pad.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        CHECK (f.getSize() > 10000, "capture Pad Library");

        // onglet Crowd
        for (auto* c : v.getChildren())
            if (auto* lp = dynamic_cast<LibraryPanel*> (c)) lp->showTab (true);
        for (auto& l : p.libraries().snapshot()) if (l.type == LibType::Crowd) p.playCrowd (l.id, 0);
        p.waitIdleForTests (2000);
        for (int i = 0; i < 50; ++i) { buf.clear(); p.processBlock (buf, midi); }
        img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
        f = outDir.getChildFile ("spacepad_crowd.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);

        // écran d'analyse (résultats)
        // le crochet passe du mineur au majeur
        for (auto* c : v.getChildren())
            if (auto* hk = dynamic_cast<HookButton*> (c)) { const int m0 = p.currentMode(); hk->onClick(); CHECK (p.currentMode() == 1 - m0, "le crochet change de mode"); hk->onClick(); }
        v.openScan (root.getChildFile ("Mes Pads"), LibType::Pad);
        for (int i = 0; i < 400; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            bool saveVisible = false;
            for (auto* c : v.getChildren())
                if (auto* so = dynamic_cast<ScanOverlay*> (c))
                    for (auto* cc : so->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*> (cc)) saveVisible |= b->isVisible() && b->getButtonText().startsWith ("Enregistrer");
            if (saveVisible) break;
        }
        img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
        f = outDir.getChildFile ("spacepad_analyse.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        CHECK (f.getSize() > 10000, "capture analyse");

        // langues : anglais puis espagnol (le réglage est partagé, comme entre l'application et les plugins)
        for (auto* c : v.getChildren())
            if (auto* so = dynamic_cast<ScanOverlay*> (c)) { so->setVisible (false); }
        // fenêtre Paramètres
        v.openSettings();
        img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
        f = outDir.getChildFile ("spacepad_parametres.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        CHECK (f.getSize() > 10000, "capture Parametres");
        for (auto* c : v.getChildren())
            if (auto* so = dynamic_cast<SettingsOverlay*> (c)) so->setVisible (false);
        // une autre couleur et le fond tourbillon, appliqués tout de suite
        {
            auto u = uiSettings(); u.accent = 2; u.background = 1; setUiSettings (u);
            v.applySettings(); v.animateForTests (2.0f);
            img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
            f = outDir.getChildFile ("spacepad_or_tourbillon.png");
            f.deleteFile();
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
            setUiSettings (UiSettings()); v.applySettings();
        }
        auto shot = [&] (const char* name)
        {
            auto im = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
            auto ff = outDir.getChildFile (name);
            ff.deleteFile();
            if (auto os = ff.createOutputStream()) png.writeImageToStream (im, *os);
            CHECK (ff.getSize() > 10000, "capture " << name);
        };
        auto panel = [&]() -> LibraryPanel* { for (auto* c : v.getChildren()) if (auto* lp = dynamic_cast<LibraryPanel*> (c)) return lp; return nullptr; };
        // setlist
        while (! p.setlist().songs().empty()) p.setlist().remove (0);
        const char* titles[] = { "Ouverture", "Louange 1", "Adoration", "Priere", "Final" };
        const int keys[] = { 4, 7, 2, 9, 0 };
        for (int i = 0; i < 5; ++i) { p.setMode (i % 2); p.pressKey (keys[i]); p.addSongFromCurrent (titles[i]); }
        p.gotoSong (1);
        panel()->showTabIndex (2);
        v.animateForTests (0.5f);
        shot ("spacepad_setlist.png");
        // deux pads mélangés
        for (auto& l : p.libraries().snapshot()) if (l.name == "Pad Mystere") p.selectPadLibraryB (l.id, p.currentMode());
        panel()->showTabIndex (0);
        shot ("spacepad_pads_ab.png");
        // pages des Paramètres
        v.openSettings();
        for (auto* c : v.getChildren())
            if (auto* so = dynamic_cast<SettingsOverlay*> (c))
            {
                so->showPage (1); shot ("spacepad_param_midi.png");
                so->showPage (2); shot ("spacepad_param_sons.png");
                so->showPage (3); shot ("spacepad_param_librairies.png");
                so->setVisible (false);
            }
        // mode scène
        v.openStage();
        shot ("spacepad_scene.png");
        for (auto* c : v.getChildren()) if (auto* so = dynamic_cast<StageOverlay*> (c)) so->setVisible (false);
        // thème clair
        {
            auto u = uiSettings(); u.theme = 3; setUiSettings (u);
            v.applySettings(); v.animateForTests (1.0f);
            panel()->showTabIndex (1);
            shot ("spacepad_clair.png");
            setUiSettings (UiSettings()); v.applySettings();
        }
        // côté crowd : grande salle, deux crowds, effets crowd, classement
        {
            juce::String cid;
            for (auto& l : p.libraries().snapshot()) if (l.type == LibType::Crowd && l.name == "Mes Crowds") cid = l.id;
            if (auto lib = p.libraries().find (cid)) if (auto* s0 = lib->sound (0)) p.crowdPrefs().setFavourite (CrowdPrefs::keyFor (cid, s0->path), true);
            p.playCrowd (cid, 1, 0);
            p.playCrowd (cid, 2, 1);
            p.waitIdleForTests (2000);
            for (int i = 0; i < 120; ++i) { buf.clear(); p.processBlock (buf, midi); }
            auto u = uiSettings(); u.centerView = 1; u.fxPage = 1; setUiSettings (u);
            v.applySettings();
            for (auto* c : v.getChildren()) if (auto* fp = dynamic_cast<FxPanel*> (c)) fp->showPage (1);
            panel()->showTabIndex (1);
            v.animateForTests (1.5f);
            shot ("spacepad_mode_crowd.png");
            for (int bgc = 1; bgc <= 3; ++bgc)
            {
                auto ub = uiSettings(); ub.crowdBg = bgc; setUiSettings (ub);
                v.repaint();
                shot ((juce::String ("spacepad_fond_crowd_") + juce::String (bgc) + ".png").toRawUTF8());
            }
            { auto ub = uiSettings(); ub.crowdBg = 0; setUiSettings (ub); }
            v.openSettings();
            shot ("spacepad_parametres_fond.png");
            for (auto* c : v.getChildren()) if (auto* so = dynamic_cast<SettingsOverlay*> (c)) so->setVisible (false);
            CHECK (p.crowdSounding (0) && p.crowdSounding (1), "deux crowds a l'ecran");
            u = uiSettings(); u.centerView = 0; u.fxPage = 0; setUiSettings (u);
            v.applySettings();
            for (auto* c : v.getChildren()) if (auto* fp = dynamic_cast<FxPanel*> (c)) fp->showPage (0);
            v.animateForTests (0.5f);
            shot ("spacepad_effets_pad.png");
            p.stopCrowd();
            if (auto lib = p.libraries().find (cid)) if (auto* s0 = lib->sound (0)) p.crowdPrefs().setFavourite (CrowdPrefs::keyFor (cid, s0->path), false);
        }
        while (! p.setlist().songs().empty()) p.setlist().remove (0);
        struct LC { Lang l; const char* file; const char* word; };
        for (auto lc : { LC { Lang::EN, "spacepad_en.png", "Folder" }, LC { Lang::ES, "spacepad_es.png", "Carpeta" } })
        {
            setLang (lc.l);
            v.retranslate();
            for (auto* c : v.getChildren())
                if (auto* lp = dynamic_cast<LibraryPanel*> (c)) lp->showTab (false);
            bool found = false;
            std::function<void (juce::Component*)> scanFor = [&] (juce::Component* c)
            {
                if (auto* b = dynamic_cast<juce::TextButton*> (c)) found |= b->getButtonText().startsWith (lc.word);
                for (auto* cc : c->getChildren()) scanFor (cc);
            };
            scanFor (&v);
            CHECK (found, "bouton traduit : " << lc.word);
            img = v.createComponentSnapshot (v.getLocalBounds(), true, 2.0f);
            f = outDir.getChildFile (lc.file);
            f.deleteFile();
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        }
        CHECK (reloadLangIfChanged() == false, "langue enregistree");
        setLang (Lang::FR);
    }
    std::cout << "  images : " << outDir.getFullPathName() << "\n";
}


static void benchPaint (const juce::File& root)
{
    section ("Vitesse de dessin (ms par image)");
    SpacepadProcessor p;
    p.prepareToPlay (48000, 256);
    for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") { p.selectPadLibrary (l.id, 0); p.selectPadLibrary (l.id, 1); }
    p.pressKey (4);
    p.waitIdleForTests (3000);
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
    for (int i = 0; i < 200; ++i) { buf.clear(); p.processBlock (buf, midi); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (MainView::W, MainView::H);
    auto& v = dynamic_cast<SpacepadEditor&> (*ed).view();
    v.animateForTests (3.0f);
    juce::Image img (juce::Image::ARGB, MainView::W, MainView::H, true);
    auto timeIt = [&] (juce::Component& c) -> double
    {
        { juce::Graphics g (img); c.paintEntireComponent (g, false); }   // préparation des images (une seule fois)
        // trois mesures de 10 images, on garde la meilleure : un pic de charge de la machine ne fausse pas le résultat
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            const int N = 10;
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < N; ++i) { v.animateForTests (1.0f / 30.0f); juce::Graphics g (img); c.paintEntireComponent (g, false); }
            best = juce::jmin (best, (juce::Time::getMillisecondCounterHiRes() - t0) / N);
        }
        return best;
    };
    for (int bgMode = 0; bgMode < 3; ++bgMode)
    {
        auto u = uiSettings(); u.background = bgMode; setUiSettings (u); v.applySettings();
        double perFrame = 0;
        juce::String detail;
        for (auto* c : v.getChildren())
        {
            juce::String n = dynamic_cast<SpaceView*> (c) ? "espace" : dynamic_cast<KeyRing*> (c) ? "anneau+orbe" : dynamic_cast<Visualizer*> (c) ? "visualiseur"
                           : dynamic_cast<HookButton*> (c) ? "crochet" : dynamic_cast<Header*> (c) ? "entete" : "";
            if (n.isEmpty()) continue;
            const double ms = timeIt (*c);
            perFrame += n == "entete" ? ms * 0.5 : ms;                     // l'en-tête est redessiné une image sur deux
            detail << n << " " << juce::String (ms, 2) << "  ";
        }
        const char* names[] = { "Galaxie", "Tourbillon", "Aucun" };
        std::cout << "  fond " << names[bgMode] << " : " << juce::String (perFrame, 2) << " ms par image  (" << detail << ")\n";
        // 30 images par seconde demandent moins de 33 ms ; la limite garde une large marge (machines de test plus ou moins rapides)
        CHECK (perFrame < 18.0, "animation legere (fond " << names[bgMode] << " : " << juce::String (perFrame, 2) << " ms par image, limite 18 ms)");
    }
    {
        auto u = uiSettings(); u.centerView = 1; setUiSettings (u); v.applySettings();
        double perFrame = 0;
        for (auto* c : v.getChildren())
            if (dynamic_cast<HallView*> (c) || dynamic_cast<CrowdStage*> (c) || dynamic_cast<Visualizer*> (c)) perFrame += timeIt (*c);
        std::cout << "  mode CROWD (salle + crowds + visualiseur) : " << juce::String (perFrame, 2) << " ms par image\n";
        CHECK (perFrame < 18.0, "mode CROWD leger (" << juce::String (perFrame, 2) << " ms par image)");
    }
    setUiSettings (UiSettings());
    juce::ignoreUnused (root);
}

// ---------------------------------------------------------------------------------------------
// Chaque effet, mesuré
// ---------------------------------------------------------------------------------------------
static juce::AudioBuffer<float> sineChord (std::vector<int> notes, double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    for (int note : notes)
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.08f * (float) std::sin (juce::MathConstants<double>::twoPi * midiHz (note) * i / sr);
            b.addSample (0, i, v); b.addSample (1, i, v * 0.9f);
        }
    return b;
}

static juce::AudioBuffer<float> brightChord (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random r (9);
    for (int note : { 48, 60, 64, 67 })
        for (int det = -1; det <= 1; det += 2)
        {
            const double f = midiHz (note) * (1.0 + det * 0.002);
            for (int i = 0; i < n; ++i)
            {
                const double ph = std::fmod (f * i / sr, 1.0);
                b.addSample (det < 0 ? 0 : 1, i, 0.03f * (float) (2.0 * ph - 1.0));     // dents de scie : riches en aigus
            }
        }
    for (int i = 0; i < n; ++i) { b.addSample (0, i, 0.004f * (r.nextFloat() * 2 - 1)); b.addSample (1, i, 0.004f * (r.nextFloat() * 2 - 1)); }
    return b;
}

static double bandEnergy (const juce::AudioBuffer<float>& b, int start, int len, double sr, double f0, double f1)
{
    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win (N, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> d ((size_t) N * 2);
    double sum = 0; int frames = 0;
    for (int s = start; s + N <= start + len && s + N <= b.getNumSamples(); s += N / 2)
    {
        std::fill (d.begin(), d.end(), 0.0f);
        for (int i = 0; i < N; ++i) d[(size_t) i] = 0.5f * (b.getSample (0, s + i) + b.getSample (1, s + i));
        win.multiplyWithWindowingTable (d.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (d.data());
        for (int k = (int) (f0 / sr * N); k <= (int) (f1 / sr * N) && k < N / 2; ++k) sum += (double) d[(size_t) k] * d[(size_t) k];
        ++frames;
    }
    return frames ? sum / frames : 0;
}

static double rmsOf (const juce::AudioBuffer<float>& b, int start, int len)
{
    double s = 0; len = juce::jmin (len, b.getNumSamples() - start);
    for (int i = 0; i < len; ++i) { const double m = 0.5 * (b.getSample (0, start + i) + b.getSample (1, start + i)); s += m * m; }
    return len > 0 ? std::sqrt (s / len) : 0;
}

struct FxRun
{
    std::map<juce::String, float> params;   // valeurs réelles (secondes pour attack/xfade, 0..1 sinon)
    juce::String lib;                        // nom de la librairie de test
    double seconds = 4;
    double stopAt = -1;                      // arrêt du pad à cet instant (s)
    std::function<void (SpacepadProcessor&, double)> atTime;   // action pendant le rendu
    bool crowd = false;
};

static juce::AudioBuffer<float> render (const FxRun& run)
{
    constexpr double sr = 48000; constexpr int block = 512;
    SpacepadProcessor p;
    p.prepareToPlay (sr, block);
    std::map<juce::String, float> base { { "shimmer", 0 }, { "warm", 0 }, { "air", 0 }, { "reverb", 0 }, { "motion", 0 }, { "width", 0.5f }, { "sub", 0 },
                                         { "lowcut", 0 }, { "padvol", 0.8f }, { "crowdvol", 0.8f }, { "attack", 0.05f }, { "xfade", 0.2f }, { "freeze", 0 }, { "swell", 0 }, { "loop", 1 } };
    for (auto& kv : run.params) base[kv.first] = kv.second;
    for (auto& kv : base)
    {
        auto* prm = p.params.getParameter (kv.first);
        prm->setValueNotifyingHost (prm->convertTo0to1 (kv.second));
    }
    juce::String id;
    for (auto& l : p.libraries().snapshot()) if (l.name == run.lib) id = l.id;
    CHECK (id.isNotEmpty(), "librairie de test presente : " << run.lib);
    if (run.crowd) { p.playCrowd (id, 0); }
    else { p.selectPadLibrary (id, 0); p.setMode (0); p.pressKey (0); }
    p.waitIdleForTests (5000);
    juce::Thread::sleep (30);
    const int total = (int) (run.seconds * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    juce::MidiBuffer midi;
    bool stopped = false;
    for (int pos = 0; pos < total; pos += block)
    {
        const double t = pos / sr;
        if (run.stopAt >= 0 && ! stopped && t >= run.stopAt) { p.stopPad(); stopped = true; }
        if (run.atTime) run.atTime (p, t);
        buf.clear();
        p.processBlock (buf, midi);
        const int n = juce::jmin (block, total - pos);
        out.copyFrom (0, pos, buf, 0, 0, n);
        out.copyFrom (1, pos, buf, 1, 0, n);
    }
    return out;
}

static void testEffects (const juce::File& tmp)
{
    section ("Chaque effet, mesure");
    constexpr double sr = 48000;
    // deux librairies de test : accord de sinus purs (C4 E4 G4) et accord riche en aigus (dents de scie + souffle)
    writeWav (tmp.getChildFile ("Effets/Sine Pad/Sine Pad C.wav"), sineChord ({ 60, 64, 67 }, sr, 6), sr);
    writeWav (tmp.getChildFile ("Effets/Bright Pad/Bright Pad C.wav"), brightChord (sr, 6), sr);
    writeWav (tmp.getChildFile ("Effets/Crowd Test/Room/Room.wav"), noise (sr, 6), sr);
    {
        LibraryStore store;
        FolderAnalyzer a;
        store.addOrReplace (a.analyzeAs (tmp.getChildFile ("Effets/Sine Pad"), LibType::Pad));
        store.addOrReplace (a.analyzeAs (tmp.getChildFile ("Effets/Bright Pad"), LibType::Pad));
        store.addOrReplace (a.analyzeAs (tmp.getChildFile ("Effets/Crowd Test"), LibType::Crowd));
    }
    const int s1 = (int) (1.5 * sr), len = (int) (2.0 * sr);
    auto ratio = [] (double a, double b) { return b > 1e-12 ? a / b : 0.0; };

    // Low cut : les basses baissent
    { auto b0 = render ({ {}, "Bright Pad" }), b1 = render ({ { { "lowcut", 1.0f } }, "Bright Pad" });
      const double r = ratio (bandEnergy (b1, s1, len, sr, 40, 200), bandEnergy (b0, s1, len, sr, 40, 200));
      std::cout << "  Low cut : basses x" << juce::String (r, 3) << "\n"; CHECK (r < 0.25, "Low cut enleve les basses (x" << r << ")"); }
    // Air : les aigus montent
    { auto b0 = render ({ {}, "Bright Pad" }), b1 = render ({ { { "air", 1.0f } }, "Bright Pad" });
      const double r = ratio (bandEnergy (b1, s1, len, sr, 9000, 16000), bandEnergy (b0, s1, len, sr, 9000, 16000));
      std::cout << "  Air : aigus x" << juce::String (r, 2) << "\n"; CHECK (r > 1.8, "Air ajoute de la brillance (x" << r << ")"); }
    // Warm : les aigus s'adoucissent
    { auto b0 = render ({ {}, "Bright Pad" }), b1 = render ({ { { "warm", 1.0f } }, "Bright Pad" });
      const double r = ratio (bandEnergy (b1, s1, len, sr, 6000, 16000), bandEnergy (b0, s1, len, sr, 6000, 16000));
      std::cout << "  Warm : aigus x" << juce::String (r, 2) << "\n"; CHECK (r < 0.6, "Warm adoucit le son (x" << r << ")"); }
    // Sub : l'octave en dessous apparaît (E3 = 164,8 Hz sous E4)
    { auto b0 = render ({ {}, "Sine Pad" }), b1 = render ({ { { "sub", 1.0f } }, "Sine Pad" });
      const double r = ratio (bandEnergy (b1, s1, len, sr, 158, 172), bandEnergy (b0, s1, len, sr, 158, 172));
      std::cout << "  Sub oct. : octave grave x" << juce::String (r, 1) << "\n"; CHECK (r > 20, "Sub ajoute l'octave en dessous (x" << r << ")"); }
    // Shimmer : l'octave au-dessus apparaît dans la réverbération (E5 = 659 Hz au-dessus de E4)
    { auto b0 = render ({ {}, "Sine Pad" }), b1 = render ({ { { "shimmer", 1.0f } }, "Sine Pad" });
      const double r = ratio (bandEnergy (b1, s1, len, sr, 650, 668), bandEnergy (b0, s1, len, sr, 650, 668));
      std::cout << "  Shimmer : octave aigue x" << juce::String (r, 1) << "\n"; CHECK (r > 20, "Shimmer ajoute l'octave au-dessus (x" << r << ")"); }
    // Reverb : le son continue après l'arrêt
    { auto b0 = render ({ {}, "Bright Pad", 6, 3 }), b1 = render ({ { { "reverb", 1.0f } }, "Bright Pad", 6, 3 });
      const double t0 = rmsOf (b0, (int) (4.5 * sr), (int) sr), t1 = rmsOf (b1, (int) (4.5 * sr), (int) sr);
      std::cout << "  Reverb : queue apres l'arret " << juce::String (t1, 5) << " (sans : " << juce::String (t0, 5) << ")\n";
      CHECK (t1 > 0.002 && t1 > t0 * 8, "Reverb prolonge le son apres l'arret"); }
    // Freeze : appuyé pendant le jeu, le son reste tenu après l'arrêt
    { FxRun f { { { "reverb", 0.6f } }, "Bright Pad", 9, 4 };
      f.atTime = [] (SpacepadProcessor& p, double t) { if (t >= 3.0 && p.params.getParameter ("freeze")->getValue() < 0.5f) p.params.getParameter ("freeze")->setValueNotifyingHost (1.0f); };
      auto b1 = render (f);
      auto b0 = render ({ { { "reverb", 0.6f } }, "Bright Pad", 9, 4 });
      const double t1 = rmsOf (b1, (int) (7.5 * sr), (int) sr), t0 = rmsOf (b0, (int) (7.5 * sr), (int) sr);
      std::cout << "  Freeze : son tenu 3,5 s apres l'arret " << juce::String (t1, 5) << " (sans : " << juce::String (t0, 5) << ")\n";
      CHECK (t1 > 0.003 && t1 > t0 * 5, "Freeze tient le son"); }
    // Swell : le volume monte et descend lentement
    { auto b1 = render ({ { { "swell", 1.0f } }, "Sine Pad", 18 }), b0 = render ({ {}, "Sine Pad", 18 });
      auto spread = [&] (const juce::AudioBuffer<float>& b) { double lo = 1e9, hi = 0; for (double t = 2; t < 17.5; t += 0.5) { const double v = rmsOf (b, (int) (t * sr), (int) (0.5 * sr)); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); } return hi / juce::jmax (lo, 1e-9); };
      const double r1 = spread (b1), r0 = spread (b0);
      std::cout << "  Swell : ecart de volume x" << juce::String (r1, 2) << " (sans : x" << juce::String (r0, 2) << ")\n";
      CHECK (r1 > 1.6 && r0 < 1.25, "Swell fait monter et descendre le volume"); }
    // Motion : les aigus bougent dans le temps
    { auto b1 = render ({ { { "motion", 1.0f } }, "Bright Pad", 14 }), b0 = render ({ {}, "Bright Pad", 14 });
      auto cv = [&] (const juce::AudioBuffer<float>& b) { std::vector<double> v; for (double t = 2; t < 13.5; t += 0.5) v.push_back (bandEnergy (b, (int) (t * sr), (int) (0.5 * sr), sr, 2500, 9000));
          double m = 0; for (auto x : v) m += x; m /= (double) v.size(); double s = 0; for (auto x : v) s += (x - m) * (x - m); return std::sqrt (s / (double) v.size()) / juce::jmax (m, 1e-12); };
      const double c1 = cv (b1), c0 = cv (b0);
      std::cout << "  Motion : variation des aigus " << juce::String (c1, 2) << " (sans : " << juce::String (c0, 2) << ")\n";
      CHECK (c1 > 0.25 && c1 > c0 * 3, "Motion fait respirer le son"); }
    // Width : l'écart gauche / droite change
    { auto bw0 = render ({ { { "width", 0.0f } }, "Bright Pad" }), bw1 = render ({ { { "width", 1.0f } }, "Bright Pad" }), bn = render ({ {}, "Bright Pad" });
      auto side = [&] (const juce::AudioBuffer<float>& b) { double s = 0, m = 0; for (int i = s1; i < s1 + len; ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); s += (l - r) * (l - r); m += (l + r) * (l + r); } return std::sqrt (s / juce::jmax (m, 1e-12)); };
      const double w0 = side (bw0), w1 = side (bw1), wn = side (bn);
      std::cout << "  Width : stereo " << juce::String (w0, 3) << " (mono) / " << juce::String (wn, 3) << " (normal) / " << juce::String (w1, 3) << " (large)\n";
      CHECK (w0 < 0.01 && w1 > wn * 1.6, "Width resserre et elargit la stereo"); }
    // Attack : la montée suit le réglage
    { auto slow = render ({ { { "attack", 4.0f } }, "Sine Pad", 7 }), fast = render ({ { { "attack", 0.3f } }, "Sine Pad", 7 });
      const double rs = rmsOf (slow, (int) (0.8 * sr), (int) (0.3 * sr)) / juce::jmax (1e-9, rmsOf (slow, (int) (6 * sr), (int) (0.5 * sr)));
      const double rf = rmsOf (fast, (int) (0.8 * sr), (int) (0.3 * sr)) / juce::jmax (1e-9, rmsOf (fast, (int) (6 * sr), (int) (0.5 * sr)));
      std::cout << "  Attack : niveau a 1 s = " << juce::String (rs * 100, 0) << " % (lent) / " << juce::String (rf * 100, 0) << " % (rapide)\n";
      CHECK (rs < 0.45 && rf > 0.8, "Attack regle la montee du son"); }
    // Fondu : le passage d'une tonalité à l'autre dure le temps réglé
    { FxRun f { { { "xfade", 4.0f } }, "Sine Pad", 8 };
      f.atTime = [] (SpacepadProcessor& p, double t) { if (t >= 3.0 && p.currentKey() == 0) p.pressKey (7); };
      auto b = render (f);
      // à mi-fondu, l'ancienne note (C) est encore là
      const double cMid = bandEnergy (b, (int) (4.0 * sr), (int) (0.6 * sr), sr, 258, 266), cEnd = bandEnergy (b, (int) (7.3 * sr), (int) (0.6 * sr), sr, 258, 266);
      const double cStart = bandEnergy (b, (int) (2.0 * sr), (int) (0.6 * sr), sr, 258, 266);
      std::cout << "  Fondu : note de depart a mi-fondu " << juce::String (ratio (cMid, cStart) * 100, 0) << " %, a la fin " << juce::String (ratio (cEnd, cStart) * 100, 1) << " %\n";
      CHECK (ratio (cMid, cStart) > 0.08 && ratio (cEnd, cStart) < 0.02, "Fondu enchaine en douceur"); }
    // Volumes
    { auto a = render ({ { { "padvol", 0.8f } }, "Sine Pad" }), b = render ({ { { "padvol", 0.4f } }, "Sine Pad" });
      const double r = rmsOf (b, s1, len) / juce::jmax (1e-9, rmsOf (a, s1, len));
      std::cout << "  Vol. Pad : 40 % / 80 % = " << juce::String (r, 2) << "\n"; CHECK (r > 0.42 && r < 0.58, "Vol. Pad regle le volume du pad"); }
    { FxRun c1 { { { "crowdvol", 0.8f } }, "Crowd Test" }; c1.crowd = true;
      FxRun c2 { { { "crowdvol", 0.4f } }, "Crowd Test" }; c2.crowd = true;
      auto a = render (c1), b = render (c2);
      const double r = rmsOf (b, s1, len) / juce::jmax (1e-9, rmsOf (a, s1, len));
      std::cout << "  Vol. Crowd : 40 % / 80 % = " << juce::String (r, 2) << "\n"; CHECK (r > 0.42 && r < 0.58, "Vol. Crowd regle le volume du crowd"); }
}

// ---------------------------------------------------------------------------------------------
// Pads et crowds séparés, majeur et mineur indépendants
// ---------------------------------------------------------------------------------------------
static void testSeparation (const juce::File& root)
{
    section ("Pads et crowds separes, majeur / mineur independants");
    FolderAnalyzer a;
    auto crowd = a.analyzeAs (root.getChildFile ("Mes Crowds"), LibType::Crowd);
    CHECK (crowd.size() == 1 && crowd[0].type == LibType::Crowd && crowd[0].name == "Mes Crowds", "onglet Crowd : une seule Crowd Library au nom du dossier");
    if (! crowd.empty()) CHECK (crowd[0].categories.size() == 2, "onglet Crowd : une categorie par sous-dossier");
    auto pads = a.analyzeAs (root.getChildFile ("Mes Pads"), LibType::Pad);
    bool allPads = ! pads.empty();
    for (auto& l : pads) allPads &= l.type == LibType::Pad;
    CHECK (pads.size() == 2 && allPads, "onglet Pad : uniquement des Pad Libraries (" << (int) pads.size() << ")");
    auto mixed = a.analyzeAs (root, LibType::Pad);
    bool noCrowd = true;
    for (auto& l : mixed) noCrowd &= l.type == LibType::Pad;
    CHECK (noCrowd, "un dossier choisi dans Pad ne cree jamais de crowd");

    SpacepadProcessor p;
    p.prepareToPlay (48000, 256);
    juce::String warm, myst;
    for (auto& l : p.libraries().snapshot()) { if (l.name == "Warm Pad") warm = l.id; if (l.name == "Pad Mystere") myst = l.id; }
    p.selectPadLibrary (warm, 0);
    p.selectPadLibrary (myst, 1);
    CHECK (p.padLibraryId (0) == warm && p.padLibraryId (1) == myst, "une librairie pour le majeur, une autre pour le mineur");
    p.setMode (1);
    CHECK (p.padLibraryId() == myst, "en mineur, la librairie mineure est utilisee");
    p.selectPadLibrary ({}, 1);
    CHECK (p.missingPadLibrary(), "sans librairie mineure : signale");
    p.pressKey (2);
    p.waitIdleForTests (2000);
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
    float peak = 0;
    for (int i = 0; i < 400; ++i) { buf.clear(); p.processBlock (buf, midi); peak = juce::jmax (peak, buf.getMagnitude (0, 256)); }
    CHECK (peak < 1e-4f, "sans librairie mineure, aucun accord du mauvais mode n'est joue");
}

static void setP (SpacepadProcessor& p, const juce::String& id, float real)
{
    auto* prm = p.params.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

static float runFor (SpacepadProcessor& p, double seconds, int channels = 2, float* tailRms = nullptr, float* auxRms = nullptr)
{
    juce::AudioBuffer<float> buf (channels, 256);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * 48000 / 256);
    double sum = 0, aux = 0; int cnt = 0;
    for (int i = 0; i < blocks; ++i)
    {
        buf.clear();
        p.processBlock (buf, midi);
        if (i >= blocks - 40)
        {
            sum += buf.getRMSLevel (0, 0, 256) + buf.getRMSLevel (1, 0, 256);
            if (channels >= 4) aux += buf.getRMSLevel (2, 0, 256) + buf.getRMSLevel (3, 0, 256);
            ++cnt;
        }
    }
    if (tailRms) *tailRms = (float) (sum / (2 * cnt));
    if (auxRms) *auxRms = (float) (aux / (2 * cnt));
    return (float) (sum / (2 * cnt));
}

static void sendMidi (SpacepadProcessor& p, const juce::MidiMessage& m)
{
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (m, 0);
    p.processBlock (buf, midi);
    p.runControlForTests();
}

static void testNewFeatures (const juce::File& root, const juce::File& tmp)
{
    section ("Setlist, pedales MIDI, 2 pads, 2 crowds, combinaison, fin du chant, sorties");
    setUiSettings (UiSettings());
    SpacepadProcessor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    juce::String warm, myst, crowdId;
    for (auto& l : p.libraries().snapshot())
    {
        if (l.name == "Warm Pad") warm = l.id;
        if (l.name == "Pad Mystere") myst = l.id;
        if (l.type == LibType::Crowd && l.name == "Mes Crowds") crowdId = l.id;
    }
    CHECK (warm.isNotEmpty() && myst.isNotEmpty() && crowdId.isNotEmpty(), "librairies de test presentes");
    while (! p.setlist().songs().empty()) p.setlist().remove (0);

    // --- setlist ---
    p.selectPadLibrary (warm, 0); p.selectPadLibrary (warm, 1);
    p.setMode (0); p.pressKey (2); setP (p, "shimmer", 0.1f);
    p.addSongFromCurrent ("Chant A");
    p.setMode (1); p.pressKey (9); setP (p, "shimmer", 0.9f);
    p.addSongFromCurrent ("Chant B");
    p.stopAll();
    CHECK (p.setlist().songs().size() == 2, "deux chants dans la setlist");
    p.gotoSong (0);
    CHECK (p.currentKey() == 2 && p.currentMode() == 0, "chant A : D majeur (" << p.currentKey() << "," << p.currentMode() << ")");
    CHECK (std::abs (p.params.getRawParameterValue ("shimmer")->load() - 0.1f) < 0.02f, "chant A : ses effets reviennent");
    p.nextSong();
    CHECK (p.setlist().current() == 1 && p.currentKey() == 9 && p.currentMode() == 1, "CHANT SUIVANT : Am");
    CHECK (std::abs (p.params.getRawParameterValue ("shimmer")->load() - 0.9f) < 0.02f, "chant B : ses effets reviennent");
    { Setlist other; other.load(); CHECK (other.songs().size() == 2 && other.songs()[1].name == "Chant B", "setlist enregistree (partagee app + plugin)"); }
    p.prevSong();
    CHECK (p.setlist().current() == 0, "chant precedent");

    // --- apprentissage d'une pédale (CC 64) pour « chant suivant » ---
    p.learnMidi (actNextSong);
    sendMidi (p, juce::MidiMessage::controllerEvent (1, 64, 127));
    sendMidi (p, juce::MidiMessage::controllerEvent (1, 64, 0));
    CHECK (uiSettings().midiMap[(size_t) actNextSong] == 1000 + 64, "pedale apprise : CC 64 -> chant suivant (" << uiSettings().midiMap[(size_t) actNextSong] << ")");
    CHECK (p.setlist().current() == 0, "la pression d'apprentissage ne declenche pas l'action");
    sendMidi (p, juce::MidiMessage::controllerEvent (1, 64, 127));
    sendMidi (p, juce::MidiMessage::controllerEvent (1, 64, 0));
    CHECK (p.setlist().current() == 1, "appui sur la pedale : chant suivant");
    // note assignée à « Fin du chant »
    p.learnMidi (actFadeEnd);
    sendMidi (p, juce::MidiMessage::noteOn (1, 96, (juce::uint8) 100));
    CHECK (uiSettings().midiMap[(size_t) actFadeEnd] == 2000 + 96, "touche C7 apprise pour la fin du chant");

    // --- quelles notes changent la tonalité ---
    p.stopAll();
    auto u = uiSettings(); u.midiKeyMode = 1; setUiSettings (u); p.runControlForTests();
    const int k0 = p.currentKey();
    sendMidi (p, juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100));
    CHECK (p.currentKey() == k0, "mode Jamais : une note ne change pas la tonalite");
    u.midiKeyMode = 2; setUiSettings (u); p.runControlForTests();
    sendMidi (p, juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100));
    CHECK (p.currentKey() == 7, "mode Toutes les notes : G4 -> G (" << p.currentKey() << ")");
    u.midiKeyMode = 0; u.zoneLow = 48; setUiSettings (u); p.runControlForTests();
    sendMidi (p, juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100));
    CHECK (p.currentKey() == 7, "zone C3-B3 : D2 ne change rien");
    sendMidi (p, juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100));
    CHECK (p.currentKey() == 4, "zone C3-B3 : E3 -> E (" << p.currentKey() << ")");
    p.learnZone();
    sendMidi (p, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    CHECK (uiSettings().zoneLow == 60 && uiSettings().midiKeyMode == 0, "zone apprise a partir de C4 (" << uiSettings().zoneLow << ")");
    setUiSettings (UiSettings()); p.runControlForTests();
    p.stopAll();
    runFor (p, 2.0);

    // --- deux pads mélangés ---
    p.selectPadLibrary (warm, 0);
    p.selectPadLibraryB (myst, 0);
    CHECK (p.padLibraryIdB (0) == myst, "pad B choisi");
    setP (p, "blend", 1.0f);
    p.setMode (0); p.pressKey (7);
    p.waitIdleForTests (3000);
    const float onlyB = runFor (p, 2.5);
    CHECK (onlyB > 0.005f, "melange tout a B : le pad B joue (" << onlyB << ")");
    p.selectPadLibraryB ({}, 0);
    setP (p, "blend", 0.5f);
    p.stopAll();
    runFor (p, 3.0);

    // --- deux crowds, suivre la tonalité, lié au pad ---
    auto lib = p.libraries().find (crowdId);
    int swell = -1, room = -1;
    for (int i = 0; lib && i < 8; ++i)
        if (auto* s = lib->sound (i)) { if (s->name.startsWith ("Swell")) swell = i; if (s->name.startsWith ("Room")) room = i; }
    CHECK (swell >= 0 && room >= 0, "sons de crowd trouves");
    p.setMode (1); p.pressKey (7);           // pad en Gm
    p.playCrowd (crowdId, swell, 0);
    p.playCrowd (crowdId, room, 1);
    p.waitIdleForTests (3000);
    runFor (p, 1.0);
    CHECK (p.crowdSounding (0) && p.crowdSounding (1), "deux crowds en meme temps");
    CHECK (p.crowdSemitones (0) == 3, "crowd en Em suit le pad en Gm : +3 demi-tons (" << p.crowdSemitones (0) << ")");
    CHECK (p.crowdSemitones (1) == 0, "un crowd sans tonalite n'est pas transpose");
    p.pressKey (2);                          // Dm
    p.waitIdleForTests (3000);
    runFor (p, 0.5);
    CHECK (p.crowdSemitones (0) == -2, "le pad passe en Dm : le crowd suit (-2) (" << p.crowdSemitones (0) << ")");
    setP (p, "crowdfollow", 0.0f);
    p.stopCrowd (0); runFor (p, 1.5);
    p.playCrowd (crowdId, swell, 0); p.waitIdleForTests (3000);
    CHECK (p.crowdSemitones (0) == 0, "suivre la tonalite desactive : pas de transposition");
    setP (p, "crowdfollow", 1.0f);

    setP (p, "crowdlink", 1.0f);
    p.stopPad();
    runFor (p, 4.0);
    CHECK (! p.crowdSounding (0) && ! p.crowdSounding (1), "lie au pad : les crowds s'arretent avec lui");
    p.playPad();
    p.waitIdleForTests (3000);
    runFor (p, 0.5);
    CHECK (p.crowdSounding (0) && p.crowdSounding (1), "lie au pad : les crowds repartent avec lui");
    setP (p, "crowdlink", 0.0f);

    // --- balance PAD / CROWD ---
    p.stopPad();
    runFor (p, 4.0);
    setP (p, "mix", 0.0f);
    const float crowdAtPad = runFor (p, 0.5);
    setP (p, "mix", 0.5f);
    const float crowdMid = runFor (p, 0.5);
    CHECK (crowdAtPad < 1e-4f && crowdMid > 1e-3f, "curseur tout PAD : crowds muets (" << crowdAtPad << " / " << crowdMid << ")");

    // --- volume Master ---
    {
        setP (p, "mix", 0.5f);
        p.playPad(); p.waitIdleForTests (3000);
        runFor (p, 3.0);
        setP (p, "master", 0.0f);
        const float m0 = runFor (p, 1.0);
        setP (p, "master", -6.0f);
        const float m6 = runFor (p, 1.0);
        setP (p, "master", -60.0f);
        const float mOff = runFor (p, 0.5);
        setP (p, "master", 0.0f);
        std::cout << "  Master : 0 dB " << m0 << " / -6 dB " << m6 << " (x" << juce::String (m6 / juce::jmax (1e-9f, m0), 2) << ") / muet " << mOff << "\n";
        CHECK (std::abs (m6 / juce::jmax (1e-9f, m0) - 0.5f) < 0.08f, "Master -6 dB : le volume est divise par deux");
        CHECK (mOff < 1e-5f, "Master au minimum : silence");
        p.stopPad();
        runFor (p, 4.0);
    }

    // --- fin du chant ---
    u = uiSettings(); u.fadeSeconds = 4; setUiSettings (u);
    p.playPad(); p.waitIdleForTests (3000);
    const float before = runFor (p, 2.0);
    p.fadeOutAll();
    const float mid = runFor (p, 1.0);
    const float after = runFor (p, 4.5);
    CHECK (before > 0.005f && mid < before && mid > 1e-4f, "fin du chant : le son descend doucement (" << before << " -> " << mid << ")");
    CHECK (after < 1e-4f && ! p.padActive() && ! p.crowdSounding(), "fin du chant : silence apres 4 s (" << after << ")");
    setUiSettings (UiSettings());

    // --- sortie séparée « Crowds » (plugin) ---
    {
        SpacepadProcessor q;
        auto layout = q.getBusesLayout();
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        CHECK (q.setBusesLayout (layout), "le plugin accepte la sortie Crowds");
        q.setRateAndBufferSizeDetails (48000, 256);
        q.prepareToPlay (48000, 256);
        q.playCrowd (crowdId, room, 0);
        q.waitIdleForTests (3000);
        float mainR = 0, auxR = 0;
        runFor (q, 1.0, 4, &mainR, &auxR);
        CHECK (auxR > 1e-3f && mainR < 1e-5f, "crowds seuls sur la sortie Crowds (principale " << mainR << ", crowds " << auxR << ")");
    }

    // --- copie, export, import ---
    {
        auto libs = std::vector<Library> { *lib };
        auto dest = tmp.getChildFile ("copie");
        CHECK (copySoundsInto (libs, dest, [] (double) { return true; }), "copie des sons");
        bool inside = true; int n = 0;
        for (auto& c : libs[0].categories) for (auto& s : c.sounds) { inside &= juce::File (s.path).isAChildOf (dest) && juce::File (s.path).existsAsFile(); ++n; }
        CHECK (inside && n == 4, "les sons copies sont dans Spacepad (" << n << ")");
        auto all = p.libraries().snapshot();
        auto exp = tmp.getChildFile ("export");
        exp.createDirectory();
        CHECK (exportLibraries (all, exp, [] (double) { return true; }), "export des librairies");
        auto back = importLibraries (exp.getChildFile ("Spacepad - librairies"), [] (double) { return true; });
        CHECK (back.size() == all.size(), "import : " << (int) back.size() << " / " << (int) all.size() << " librairies");
        bool ok = ! back.empty();
        for (auto& l : back) for (auto& c : l.categories) for (auto& s : c.sounds) ok &= juce::File (s.path).existsAsFile();
        CHECK (ok, "import : chaque son est present sur l'ordinateur");
    }
    while (! p.setlist().songs().empty()) p.setlist().remove (0);
}

static float sideRatio (const juce::AudioBuffer<float>& b, int start)
{
    double m = 0, sd = 0;
    for (int i = start; i < b.getNumSamples(); ++i)
    {
        const float l = b.getSample (0, i), r = b.getSample (1, i);
        m += (l + r) * (l + r); sd += (l - r) * (l - r);
    }
    return (float) std::sqrt (sd / juce::jmax (1e-12, m));
}

static void testCrowdFx (const juce::File& root)
{
    section ("Chorus, delay au tempo et effets des crowds, mesures");
    const double sr = 48000;
    // chorus : un son mono devient large
    {
        juce::AudioBuffer<float> a (2, 48000), b (2, 48000);
        for (int i = 0; i < 48000; ++i) { const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 330 * i / sr); a.setSample (0, i, v); a.setSample (1, i, v); }
        b.makeCopyOf (a);
        StereoChorus c0, c1; c0.prepare (sr); c1.prepare (sr);
        c0.process (a.getWritePointer (0), a.getWritePointer (1), 48000, 0.0f);
        c1.process (b.getWritePointer (0), b.getWritePointer (1), 48000, 0.8f);
        std::cout << "  Chorus : largeur " << sideRatio (a, 4800) << " (sans) / " << sideRatio (b, 4800) << " (avec)\n";
        CHECK (sideRatio (a, 4800) < 0.001f && sideRatio (b, 4800) > 0.15f, "chorus : le son s'elargit");
    }
    // delay : une impulsion revient au bon moment, à gauche puis à droite, de plus en plus douce
    {
        juce::AudioBuffer<float> b (2, 60000);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        TempoDelay d; d.prepare (sr);
        d.process (b.getWritePointer (0), b.getWritePointer (1), 60000, 0.25f, 0.5f, 1.0f);
        auto peakAt = [&] (int ch, int from, int to) { int best = from; float bv = 0; for (int i = from; i < to; ++i) if (std::abs (b.getSample (ch, i)) > bv) { bv = std::abs (b.getSample (ch, i)); best = i; } return std::pair<int, float> { best, bv }; };
        auto e1 = peakAt (0, 6000, 18000), e2 = peakAt (1, 18000, 30000), e3 = peakAt (0, 30000, 42000);
        std::cout << "  Delay 250 ms : echo 1 a " << e1.first << " (gauche " << e1.second << "), echo 2 a " << e2.first << " (droite " << e2.second << "), echo 3 " << e3.second << "\n";
        CHECK (std::abs (e1.first - 12000) < 40 && std::abs (e2.first - 24000) < 60, "delay : les repetitions tombent au bon moment");
        CHECK (e2.second < e1.second && e3.second < e2.second && e1.second > 0.3f, "delay : chaque repetition est plus douce");
    }
    // distance : la foule s'éloigne (moins d'aigus), mouvement : elle se promène
    {
        juce::Random rnd (5);
        juce::AudioBuffer<float> n0 (2, 96000), n1 (2, 96000);
        for (int i = 0; i < 96000; ++i) { const float v = rnd.nextFloat() * 0.4f - 0.2f; n0.setSample (0, i, v); n0.setSample (1, i, v); }
        n1.makeCopyOf (n0);
        CrowdFx f0, f1; f0.prepare (sr, 96000); f1.prepare (sr, 96000);
        CrowdFxParams p0; p0.reverb = 0; p0.lowCut = 0;
        CrowdFxParams p1 = p0; p1.distance = 1.0f;
        f0.process (n0, 96000, p0);
        f1.process (n1, 96000, p1);
        const double h0 = bandEnergy (n0, 24000, 48000, sr, 6000, 16000), h1 = bandEnergy (n1, 24000, 48000, sr, 6000, 16000);
        std::cout << "  Distance : aigus x" << juce::String (h1 / juce::jmax (1e-12, h0), 3) << "\n";
        CHECK (h1 < h0 * 0.2, "distance : la foule s'eloigne");

        juce::AudioBuffer<float> m (2, 192000);
        for (int i = 0; i < 192000; ++i) { const float v = rnd.nextFloat() * 0.4f - 0.2f; m.setSample (0, i, v); m.setSample (1, i, v); }
        CrowdFx fm; fm.prepare (sr, 192000);
        CrowdFxParams pm = p0; pm.movement = 1.0f;
        fm.process (m, 192000, pm);
        float lo = 1e9f, hi = 0;
        for (int blk = 0; blk < 192000 / 4800; ++blk)
        {
            const float l = m.getRMSLevel (0, blk * 4800, 4800), r = m.getRMSLevel (1, blk * 4800, 4800);
            const float ratio = l / juce::jmax (1e-6f, r);
            lo = juce::jmin (lo, ratio); hi = juce::jmax (hi, ratio);
        }
        std::cout << "  Mouvement : gauche/droite de " << lo << " a " << hi << "\n";
        CHECK (lo < 0.5f && hi > 2.0f, "mouvement : le son va de gauche a droite");
    }
    // tempo : divisions et TAP
    {
        SpacepadProcessor p;
        setP (p, "tempo", 120.0f);
        CHECK (std::abs (p.delaySeconds (1) - 0.5f) < 1e-4f && std::abs (p.delaySeconds (2) - 0.375f) < 1e-4f && std::abs (p.delaySeconds (3) - 0.25f) < 1e-4f,
               "tempo 120 : 1/4 = 0,5 s, 1/8 pointee = 0,375 s, 1/8 = 0,25 s");
        for (double t : { 0.0, 833.0, 1667.0, 2500.0 }) p.tapTempoAt (10000.0 + t);
        CHECK (std::abs (p.params.getRawParameterValue ("tempo")->load() - 72.0f) < 0.6f, "TAP : 4 frappes a 72 bpm -> 72 (" << p.params.getRawParameterValue ("tempo")->load() << ")");
        p.tapTempoAt (20000.0);
        CHECK (std::abs (p.params.getRawParameterValue ("tempo")->load() - 72.0f) < 0.6f, "TAP : une frappe isolee ne change rien");
    }
    // pad : chorus et delay dans le moteur (deux pads identiques, l'un avec l'effet)
    {
        auto renderPad = [] (const char* fxId, float amount, bool solo, float* soloLevel)
        {
            SpacepadProcessor p;
            p.setPlayConfigDetails (0, 2, 48000, 256);
            p.prepareToPlay (48000, 256);
            juce::String warm;
            for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") warm = l.id;
            p.selectPadLibrary (warm, 0); p.selectPadLibrary (warm, 1);
            setP (p, "reverb", 0.0f); setP (p, "shimmer", 0.0f); setP (p, "motion", 0.0f); setP (p, "attack", 0.05f);
            setP (p, fxId, amount);
            p.setMode (0); p.pressKey (0); p.waitIdleForTests (3000);
            juce::AudioBuffer<float> b (2, 256), all (2, 256 * 300); juce::MidiBuffer midi;
            for (int i = 0; i < 300; ++i) { b.clear(); p.processBlock (b, midi); all.copyFrom (0, i * 256, b, 0, 0, 256); all.copyFrom (1, i * 256, b, 1, 0, 256); }
            if (solo) { p.setSolo (true); *soloLevel = runFor (p, 0.5); p.setSolo (false); soloLevel[1] = runFor (p, 0.5); }
            p.stopAll();
            return all;
        };
        float lv[2] = { 0, 0 };
        auto a0 = renderPad ("chorus", 0.0f, true, lv);
        auto a1 = renderPad ("chorus", 0.9f, false, nullptr);
        auto a2 = renderPad ("delay", 0.8f, false, nullptr);
        auto diff = [&] (const juce::AudioBuffer<float>& x)
        {
            double d = 0, e = 0;
            for (int ch = 0; ch < 2; ++ch) for (int i = 48000; i < x.getNumSamples(); ++i) { const float u = a0.getSample (ch, i); d += (x.getSample (ch, i) - u) * (x.getSample (ch, i) - u); e += u * u; }
            return std::sqrt (d / juce::jmax (1e-12, e));
        };
        std::cout << "  Chorus du pad : difference " << diff (a1) << ", Delay du pad : difference " << diff (a2) << "\n";
        CHECK (diff (a1) > 0.15, "chorus du pad : il agit sur le son");
        CHECK (diff (a2) > 0.08, "delay du pad : il agit sur le son (sur un son tenu, les echos se fondent dans le pad)");
        CHECK (lv[0] < 1e-4f && lv[1] > 0.005f, "ecouter seul : le pad se tait, puis revient (" << lv[0] << " / " << lv[1] << ")");
    }
    // mémoire par son, favoris, chant
    {
        SpacepadProcessor p;
        p.setPlayConfigDetails (0, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        juce::String crowdId;
        for (auto& l : p.libraries().snapshot()) if (l.type == LibType::Crowd && l.name == "Mes Crowds") crowdId = l.id;
        p.playCrowd (crowdId, 0, 0);
        p.waitIdleForTests (2000);
        const auto keyA = p.crowdSoundKey (0);
        setP (p, SpacepadProcessor::crowdParam (0, "delay"), 0.6f);
        p.runControlForTests();
        CHECK (p.crowdPrefs().hasFx (keyA), "les effets regles pour un son sont memorises");
        p.playCrowd (crowdId, 1, 0);
        p.waitIdleForTests (2000);
        setP (p, SpacepadProcessor::crowdParam (0, "delay"), 0.1f);
        p.runControlForTests();
        p.playCrowd (crowdId, 0, 0);
        p.waitIdleForTests (2000);
        const float d = p.params.getRawParameterValue (SpacepadProcessor::crowdParam (0, "delay"))->load();
        CHECK (std::abs (d - 0.6f) < 0.01f, "le son retrouve son delay (" << d << ")");
        p.crowdPrefs().setFavourite (keyA, true);
        p.crowdPrefs().saveIfDirty();
        { CrowdPrefs other; other.load(); CHECK (other.isFavourite (keyA) && other.hasFx (keyA), "favoris et effets enregistres (partages app + plugin)"); }
        while (! p.setlist().songs().empty()) p.setlist().remove (0);
        setP (p, SpacepadProcessor::crowdParam (1, "chorus"), 0.7f);
        p.addSongFromCurrent ("Avec chorus");
        setP (p, SpacepadProcessor::crowdParam (1, "chorus"), 0.0f);
        p.gotoSong (0);
        CHECK (std::abs (p.params.getRawParameterValue (SpacepadProcessor::crowdParam (1, "chorus"))->load() - 0.7f) < 0.01f, "le chant garde les effets des crowds");
        while (! p.setlist().songs().empty()) p.setlist().remove (0);
        p.crowdPrefs().setFavourite (keyA, false);
        p.crowdPrefs().clearFx (keyA);
        p.playCrowd (crowdId, 1, 0); p.waitIdleForTests (2000);
        p.clearCrowdSoundFx (0);
        p.crowdPrefs().saveIfDirty();
        p.stopAll();
    }
    juce::ignoreUnused (root);
}

// ---------------------------------------------------------------------------------------------
// Vidéo de présentation : le vrai logiciel, piloté scène par scène, filmé image par image
// (SPACEPAD_VIDEO=fichier.mp4 ; nécessite ffmpeg)
#if ! JUCE_WINDOWS
#include <cstdio>
static void makeVideo (const juce::File& out)
{
    section ("Video de presentation");
    setUiSettings (UiSettings());
    setLang (Lang::FR);
    SpacepadProcessor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    juce::String warm, myst, crowdId;
    for (auto& l : p.libraries().snapshot())
    {
        if (l.name == "Warm Pad") warm = l.id;
        if (l.name == "Pad Mystere") myst = l.id;
        if (l.type == LibType::Crowd && l.name == "Mes Crowds") crowdId = l.id;
    }
    p.selectPadLibrary (warm, 0); p.selectPadLibrary (warm, 1);
    while (! p.setlist().songs().empty()) p.setlist().remove (0);
    const char* titles[] = { "Ouverture", "Louange", "Adoration", "Priere", "Final" };
    const int keys[] = { 4, 7, 2, 9, 0 }, modes[] = { 1, 0, 0, 1, 0 };
    for (int i = 0; i < 5; ++i) { p.setMode (modes[i]); p.pressKey (keys[i]); p.addSongFromCurrent (titles[i]); }
    p.stopAll();
    p.setMode (1); p.pressKey (4); p.stopPad();
    if (auto lib = p.libraries().find (crowdId)) if (auto* s0 = lib->sound (0)) p.crowdPrefs().setFavourite (CrowdPrefs::keyFor (crowdId, s0->path), true);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (MainView::W, MainView::H);
    auto& v = dynamic_cast<SpacepadEditor&> (*ed).view();
    auto find = [&] (auto* dummy) -> decltype (dummy) { for (auto* c : v.getChildren()) if (auto* x = dynamic_cast<decltype (dummy)> (c)) return x; return nullptr; };
    auto* panel = find ((LibraryPanel*) nullptr);
    auto* fx = find ((FxPanel*) nullptr);
    auto* stage = find ((CrowdStage*) nullptr);
    auto centre = [&] (int vw) { auto u = uiSettings(); u.centerView = vw; setUiSettings (u); v.applySettings(); };

    const float scale = 1920.0f / MainView::W;
    const int capH = 150, W = 1920, H = juce::roundToInt ((MainView::H + capH) * scale) & ~1;
    const juce::String cmd = "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (W) + "x" + juce::String (H)
                           + " -r 30 -i - -c:v libx264 -preset medium -crf 19 -pix_fmt yuv420p -movflags +faststart \"" + out.getFullPathName() + "\"";
    FILE* pipe = popen (cmd.toRawUTF8(), "w");
    if (pipe == nullptr) { CHECK (false, "ffmpeg introuvable"); return; }

    struct Scene { float start; const char* title; const char* sub; };
    const Scene scenes[] = {
        { 0.0f,  "", "" },
        { 4.0f,  "Touchez une tonalit\xc3\xa9, le pad joue", "La galaxie tourne et respire avec le son \xc2\xb7 le crochet passe du mineur au majeur" },
        { 13.0f, "Vos dossiers, analys\xc3\xa9s tout seuls", "Le nom du dossier est gard\xc3\xa9, la tonalit\xc3\xa9 de chaque son est trouv\xc3\xa9""e \xc2\xb7 deux pads A + B m\xc3\xa9lang\xc3\xa9s" },
        { 20.0f, "Des effets qui enrichissent le son", "Shimmer, Warm, Air, Reverb, Chorus, Delay au tempo, Sub, Freeze, Swell\xe2\x80\xa6" },
        { 27.0f, "Mode CROWD", "Deux crowds en m\xc3\xaame temps, li\xc3\xa9s au pad et jou\xc3\xa9s dans sa tonalit\xc3\xa9" },
        { 36.0f, "Les crowds ont leurs propres effets", "Chorus, Delay, Distance, Mouvement, Reverb\xe2\x80\xa6 m\xc3\xa9moris\xc3\xa9s pour chaque son \xc2\xb7 favoris et tri" },
        { 43.0f, "Setlist", "Chaque chant garde sa tonalit\xc3\xa9, ses pads, ses crowds et ses effets \xc2\xb7 \xc2\xab Chant suivant \xc2\xbb en fondu" },
        { 50.0f, "P\xc3\xa9""dales et MIDI", "Assignez une p\xc3\xa9""dale \xc3\xa0 chaque action \xc2\xb7 une zone du clavier choisit la tonalit\xc3\xa9" },
        { 55.0f, "Mode sc\xc3\xa8ne", "Plein \xc3\xa9""cran, tr\xc3\xa8s gros boutons, lisible de loin" },
        { 60.0f, "", "" },
    };
    const int nScenes = (int) (sizeof (scenes) / sizeof (scenes[0]));
    const float total = 65.0f;
    juce::Image frame (juce::Image::ARGB, W, H, true), prev;
    int lastScene = -1, fadeFrames = 0;
    juce::AudioBuffer<float> buf (2, 400); juce::MidiBuffer midi;
    bool done[16] {};
    auto once = [&] (int i, float t, float at) { if (! done[i] && t >= at) { done[i] = true; return true; } return false; };
    float mix = 0;

    for (int fi = 0; fi < (int) (total * 30); ++fi)
    {
        const float t = fi / 30.0f;
        int sc = 0;
        for (int i = 0; i < nScenes; ++i) if (t >= scenes[i].start) sc = i;
        // --- ce qui se passe dans le logiciel ---
        if (once (0, t, 1.0f)) { p.pressKey (4); }
        if (once (1, t, 7.5f)) p.pressKey (7);
        if (once (2, t, 10.5f)) p.setMode (0);
        if (once (3, t, 13.0f)) { panel->showTabIndex (0); }
        if (once (4, t, 16.0f)) p.selectPadLibraryB (myst, 0);
        if (sc == 3) { mix = juce::jlimit (0.0f, 1.0f, (t - 20.5f) / 5.0f); setP (p, "shimmer", 0.45f + 0.4f * mix); setP (p, "chorus", 0.35f * mix); setP (p, "delay", 0.3f * mix); setP (p, "reverb", 0.55f + 0.3f * mix); }
        if (once (5, t, 27.0f)) { p.selectPadLibraryB ({}, 0); centre (1); panel->showTabIndex (1); stage->refreshTiles(); }
        if (once (6, t, 28.5f)) { p.setCrowdTarget (0); p.playCrowd (crowdId, 2, 0); }
        if (once (7, t, 31.0f)) { p.setCrowdTarget (1); p.playCrowd (crowdId, 1, 1); stage->refreshTiles(); }
        if (once (8, t, 36.0f)) { fx->showPage (1); fx->showSlot (0); }
        if (sc == 5) { const float k = juce::jlimit (0.0f, 1.0f, (t - 37.0f) / 4.0f); setP (p, SpacepadProcessor::crowdParam (0, "chorus"), 0.5f * k); setP (p, SpacepadProcessor::crowdParam (0, "delay"), 0.35f * k); setP (p, SpacepadProcessor::crowdParam (0, "dist"), 0.3f * k); }
        if (once (9, t, 43.0f)) { centre (0); fx->showPage (0); panel->showTabIndex (2); p.stopCrowd(); p.gotoSong (0); }
        if (once (10, t, 46.5f)) p.nextSong();
        if (once (11, t, 50.0f)) { v.openSettings(); for (auto* c : v.getChildren()) if (auto* so = dynamic_cast<SettingsOverlay*> (c)) so->showPage (1); }
        if (once (12, t, 55.0f)) { for (auto* c : v.getChildren()) if (auto* so = dynamic_cast<SettingsOverlay*> (c)) so->setVisible (false); v.openStage(); }
        if (once (13, t, 58.0f)) p.nextSong();
        if (once (14, t, 60.0f)) { for (auto* c : v.getChildren()) if (auto* so = dynamic_cast<StageOverlay*> (c)) so->setVisible (false); centre (1); panel->showTabIndex (1); p.playCrowd (crowdId, 2, 0); }
        // --- le son (1/30 s) et l'animation ---
        for (int k = 0; k < 4; ++k) { buf.clear(); p.processBlock (buf, midi); }
        v.animateForTests (1.0f / 30.0f);
        panel->updateState();
        fx->tick();
        if (fi % 15 == 0) stage->refreshTiles();

        // --- l'image ---
        auto shot = v.createComponentSnapshot (v.getLocalBounds(), true, scale);
        {
            juce::Graphics g (frame);
            g.fillAll (juce::Colour (0xff04060c));
            g.drawImageAt (shot, 0, 0);
            auto cap = juce::Rectangle<float> (0, (float) shot.getHeight(), (float) W, (float) (H - shot.getHeight()));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b1020), 0, cap.getY(), juce::Colour (0xff04060c), 0, cap.getBottom(), false));
            g.fillRect (cap);
            const auto& S = scenes[sc];
            const float local = t - S.start;
            const float a = juce::jlimit (0.0f, 1.0f, local / 0.6f);
            if (juce::String (S.title).isNotEmpty())
            {
                g.setColour (theme::accent.withAlpha (a));
                g.fillRoundedRectangle (cap.getX() + 70, cap.getY() + 44, 6, 66, 3);
                g.setColour (juce::Colours::white.withAlpha (a));
                g.setFont (theme::ui (44.0f, 2));
                g.drawText (juce::String::fromUTF8 (S.title), cap.withTrimmedLeft (100).withHeight (70).translated (0, 30), juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xffaab5ca).withAlpha (a));
                g.setFont (theme::ui (26.0f));
                g.drawText (juce::String::fromUTF8 (S.sub), cap.withTrimmedLeft (100).withHeight (40).translated (0, 98), juce::Justification::centredLeft);
            }
            // titre d'ouverture et de fin
            if (sc == 0 || sc == nScenes - 1)
            {
                const float fade = sc == 0 ? juce::jlimit (0.0f, 1.0f, 1.0f - (t - 2.6f) / 1.4f) : juce::jlimit (0.0f, 1.0f, (t - 60.6f) / 1.0f);
                g.setColour (juce::Colour (0xff04060c).withAlpha (0.86f * fade));
                g.fillRect (frame.getBounds());
                auto f = theme::title (120.0f); f.setExtraKerningFactor (0.24f);
                g.setFont (f);
                g.setColour (juce::Colours::white.withAlpha (fade));
                g.drawText ("SPACEPAD", juce::Rectangle<float> (0, H * 0.30f, (float) W, 170), juce::Justification::centred);
                auto by = theme::ui (32.0f, 1); by.setExtraKerningFactor (0.5f);
                g.setFont (by);
                g.setColour (theme::accent.withAlpha (fade));
                g.drawText ("BY JB PRAISE", juce::Rectangle<float> (0, H * 0.30f + 170, (float) W, 50), juce::Justification::centred);
                g.setFont (theme::ui (34.0f));
                g.setColour (juce::Colour (0xffcfd8ea).withAlpha (fade));
                g.drawText (sc == 0 ? juce::String::fromUTF8 ("Pads et crowds pour la musique live et worship")
                                    : juce::String::fromUTF8 ("Application Windows \xc2\xb7 Plugin VST3"),
                            juce::Rectangle<float> (0, H * 0.30f + 250, (float) W, 60), juce::Justification::centred);
            }
            // fondu entre les scènes
            if (sc != lastScene) { if (lastScene >= 0 && prev.isValid()) fadeFrames = 12; lastScene = sc; }
            if (fadeFrames > 0 && prev.isValid()) { g.setOpacity (fadeFrames / 12.0f); g.drawImageAt (prev, 0, 0); g.setOpacity (1.0f); --fadeFrames; }
        }
        if (fadeFrames == 0) prev = frame.createCopy();
        juce::Image::BitmapData bd (frame, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < H; ++y) fwrite (bd.getLinePointer (y), 4, (size_t) W, pipe);
    }
    const int rc = pclose (pipe);
    std::cout << "  video : " << out.getFullPathName() << " (" << out.getSize() / 1024 << " Ko)\n";
    CHECK (rc == 0 && out.getSize() > 100000, "video creee");
    p.stopAll();
    while (! p.setlist().songs().empty()) p.setlist().remove (0);
    setUiSettings (UiSettings());
}
#endif

// ---------------------------------------------------------------------------------------------
// WAV « difficiles » écrits à la main : ils doivent tous être acceptés
static void putU16 (juce::MemoryOutputStream& o, int v) { o.writeShort ((short) v); }
static void putU32 (juce::MemoryOutputStream& o, juce::uint32 v) { o.writeInt ((int) v); }

static void writeRawWav (const juce::File& f, int tag, int chans, int rate, int bits, const juce::MemoryBlock& data, bool zeroSize, bool extraOddChunk, int blockAlign = 0, int extensible = 0)
{
    juce::MemoryOutputStream o;
    o.write ("RIFF", 4); putU32 (o, 0); o.write ("WAVE", 4);
    if (extraOddChunk) { o.write ("LIST", 4); putU32 (o, 5); o.write ("abcde", 5); o.writeByte (0); }   // bloc de taille impaire, avec octet de bourrage
    o.write ("fmt ", 4);
    const int ba = blockAlign > 0 ? blockAlign : chans * ((bits + 7) / 8);
    putU32 (o, extensible ? 40 : (tag == 0x11 ? 20 : 16));
    putU16 (o, extensible ? 0xFFFE : tag); putU16 (o, chans); putU32 (o, (juce::uint32) rate); putU32 (o, (juce::uint32) (rate * ba)); putU16 (o, ba); putU16 (o, bits);
    if (extensible) { putU16 (o, 22); putU16 (o, bits); putU32 (o, 3); putU16 (o, extensible); for (int i = 0; i < 14; ++i) o.writeByte (0); }
    else if (tag == 0x11) { putU16 (o, 2); putU16 (o, (ba - 4 * chans) * 8 / (4 * chans) + 1); }
    o.write ("data", 4); putU32 (o, zeroSize ? 0 : (juce::uint32) data.getSize());
    o.write (data.getData(), data.getSize());
    f.getParentDirectory().createDirectory();
    f.replaceWithData (o.getData(), o.getDataSize());
}

static void testWavFormats (const juce::File& tmp)
{
    section ("Tous les WAV acceptes (en-tetes inhabituels, formats speciaux)");
    const int rate = 44100, n = rate * 2;
    auto sine = [&] (int i) { return 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 220 * i / rate); };
    auto dir = tmp.getChildFile ("wav_difficiles");
    dir.deleteRecursively();
    struct Case { const char* file; int tag, bits, ext; bool zero, odd; };
    const Case cases[] = { { "pcm16 taille zero.wav", 1, 16, 0, true, false }, { "pcm24 bloc impair.wav", 1, 24, 0, false, true },
                           { "flottant32.wav", 3, 32, 0, false, false }, { "flottant64 taille zero.wav", 3, 64, 0, true, true },
                           { "extensible24.wav", 1, 24, 1, false, false }, { "PCM8 MAJUSCULES.WAV", 1, 8, 0, false, false },
                           { "pcm32 entier.wav", 1, 32, 0, true, false }, { "alaw.wav", 6, 8, 0, false, false }, { "ulaw.wav", 7, 8, 0, false, false } };
    for (auto& c : cases)
    {
        juce::MemoryOutputStream d;
        for (int i = 0; i < n; ++i)
        {
            const double v = sine (i);
            if (c.tag == 3 && c.bits == 32) d.writeFloat ((float) v);
            else if (c.tag == 3) d.writeDouble (v);
            else if (c.tag == 6 || c.tag == 7)
            {
                // codage approché : on garde l'octet qui décode le plus près (vérifie le décodeur sans dépendre d'un encodeur)
                static std::vector<float> table[2];
                auto& tb = table[c.tag == 6 ? 0 : 1];
                if (tb.empty())
                    for (int b = 0; b < 256; ++b)
                    {
                        juce::MemoryOutputStream one; writeRawWav (tmp.getChildFile ("one.wav"), c.tag, 1, rate, 8, juce::MemoryBlock (&b, 1), false, false);
                        juce::AudioFormatManager fm; auto rd = openAudioReader (fm, tmp.getChildFile ("one.wav"));
                        float x = 0; float* px = &x; if (rd) rd->read (&px, 1, 0, 1); tb.push_back (x);
                    }
                int best = 0; for (int b = 1; b < 256; ++b) if (std::abs (tb[(size_t) b] - v) < std::abs (tb[(size_t) best] - v)) best = b;
                d.writeByte ((char) best);
            }
            else if (c.bits == 8) d.writeByte ((char) (juce::uint8) juce::jlimit (0, 255, (int) (128 + v * 127)));
            else if (c.bits == 16) d.writeShort ((short) (v * 32767));
            else if (c.bits == 24) { const int x = (int) (v * 8388607); d.writeByte ((char) (x & 255)); d.writeByte ((char) ((x >> 8) & 255)); d.writeByte ((char) ((x >> 16) & 255)); }
            else d.writeInt ((int) (v * 2147483000.0));
        }
        writeRawWav (dir.getChildFile (c.file), c.tag, 1, rate, c.bits, d.getMemoryBlock(), c.zero, c.odd, 0, c.ext);
    }
    // IMA ADPCM (compressé) : petit encodeur pour le test
    {
        static const int idxT[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
        static const int stepT[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
                                       253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
                                       3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
        const int ba = 256, perBlock = (ba - 4) * 2 + 1;
        juce::MemoryOutputStream d;
        int pred = 0, index = 0, i = 0;
        while (i + perBlock <= n)
        {
            pred = (int) (sine (i) * 32767); d.writeShort ((short) pred); d.writeByte ((char) index); d.writeByte (0); ++i;
            for (int b = 0; b < ba - 4; ++b)
            {
                int nib[2];
                for (int h = 0; h < 2; ++h)
                {
                    const int target = (int) (sine (i++) * 32767), step = stepT[index];
                    int diff = target - pred, code = 0;
                    if (diff < 0) { code = 8; diff = -diff; }
                    int delta = step >> 3;
                    if (diff >= step) { code |= 4; diff -= step; delta += step; }
                    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; delta += step >> 1; }
                    if (diff >= step >> 2) { code |= 1; delta += step >> 2; }
                    pred = juce::jlimit (-32768, 32767, pred + ((code & 8) ? -delta : delta));
                    index = juce::jlimit (0, 88, index + idxT[code]);
                    nib[h] = code;
                }
                d.writeByte ((char) (nib[0] | (nib[1] << 4)));
            }
        }
        writeRawWav (dir.getChildFile ("ima adpcm.wav"), 0x11, 1, rate, 4, d.getMemoryBlock(), false, false, ba);
    }
    dir.getChildFile ("faux.wav").replaceWithText ("ceci n'est pas un son");
    tmp.getChildFile ("one.wav").deleteFile();

    juce::AudioFormatManager fm;
    int ok = 0, total = 0;
    for (auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles))
    {
        const auto f = e.getFile();
        if (f.getFileName() == "faux.wav") continue;
        ++total;
        juce::String why;
        auto rd = openAudioReader (fm, f, &why);
        bool good = rd != nullptr && std::abs ((double) rd->lengthInSamples - n) < n * 0.02 && rd->sampleRate == rate;
        if (good)
        {
            juce::AudioBuffer<float> b (1, 4410);
            rd->read (&b, 0, 4410, 22050, true, false);
            float err = 0;
            for (int i = 0; i < 4410; ++i) err = juce::jmax (err, std::abs (b.getSample (0, i) - (float) sine (22050 + i)));
            good = err < (f.getFileName().containsIgnoreCase ("adpcm") || f.getFileName().containsIgnoreCase ("law") || f.getFileName().containsIgnoreCase ("pcm8") ? 0.06f : 0.002f);
            if (! good) std::cout << "  " << f.getFileName() << " : ecart " << err << "\n";
        }
        else std::cout << "  " << f.getFileName() << " refuse : " << why << "\n";
        CHECK (good, "WAV lu correctement : " << f.getFileName());
        ok += good ? 1 : 0;
    }
    std::cout << "  " << ok << " / " << total << " WAV difficiles lus\n";
    // l'analyse les accepte tous, et explique le fichier refusé
    FolderAnalyzer a;
    auto libs = a.analyzeItems ({ dir }, LibType::Pad);
    int sounds = 0; for (auto& l : libs) sounds += l.numSounds();
    CHECK (sounds == total, "analyse : tous les WAV difficiles sont acceptes (" << sounds << " / " << total << ")");
    CHECK (a.skipped.size() == 1 && a.skipped[0].startsWith ("faux.wav"), "le faux WAV est signale avec la raison (" << a.skipped.joinIntoString (" | ") << ")");
    // le moteur les joue
    SampleLoader sl;
    auto data = sl.loadNow (dir.getChildFile ("pcm16 taille zero.wav").getFullPathName());
    CHECK (data != nullptr && data->buffer.getNumSamples() > n / 2, "le moteur charge un WAV a taille zero");

    // fichiers choisis un par un, de deux dossiers, plus un dossier entier
    auto d2 = tmp.getChildFile ("autres sons");
    d2.deleteRecursively();
    writeRawWav (d2.getChildFile ("Pad A.wav"), 1, 1, rate, 16, juce::MemoryBlock (8000, true), false, false);
    writeRawWav (d2.getChildFile ("Pad B.wav"), 1, 1, rate, 16, juce::MemoryBlock (8000, true), false, false);
    FolderAnalyzer b;
    auto mixed = b.analyzeItems ({ dir.getChildFile ("flottant32.wav"), dir.getChildFile ("alaw.wav"), d2.getChildFile ("Pad A.wav") }, LibType::Pad);
    CHECK (mixed.size() == 2 && mixed[0].partial && mixed[1].partial, "fichiers de deux dossiers : deux librairies au nom de leur dossier (" << (int) mixed.size() << ")");
    LibraryStore store;
    store.addOrReplace (mixed);
    auto more = b.analyzeItems ({ d2.getChildFile ("Pad B.wav") }, LibType::Pad);
    store.addOrReplace (more);
    auto lib = store.find (makeLibraryId (d2.getFullPathName()));
    CHECK (lib != nullptr && lib->numSounds() == 2, "un son ajoute ensuite s'ajoute a la librairie au lieu de la remplacer (" << (lib ? lib->numSounds() : 0) << ")");
    // supprimer depuis les Paramètres
    store.remove (makeLibraryId (d2.getFullPathName()));
    store.remove (makeLibraryId (dir.getFullPathName()));
    CHECK (store.find (makeLibraryId (d2.getFullPathName())) == nullptr, "librairie supprimee");
    LibraryStore again;
    CHECK (again.find (makeLibraryId (d2.getFullPathName())) == nullptr, "suppression enregistree (application et plugin)");
}

static void testAutomation (const juce::File& root)
{
    section ("Enregistrement dans le logiciel de musique (automation)");
    juce::ignoreUnused (root);
    SpacepadProcessor p;
    p.setPlayConfigDetails (0, 2, 48000, 256);
    p.prepareToPlay (48000, 256);
    juce::String warm, crowdId;
    for (auto& l : p.libraries().snapshot()) { if (l.name == "Warm Pad") warm = l.id; if (l.type == LibType::Crowd && l.name == "Mes Crowds") crowdId = l.id; }
    p.selectPadLibrary (warm, 0); p.selectPadLibrary (warm, 1);
    auto val = [&] (const char* id) { return p.params.getRawParameterValue (id)->load(); };
    // un clic à l'écran devient une valeur de paramètre (Ableton l'enregistre)
    p.setMode (0);
    p.pressKey (7);
    CHECK (juce::roundToInt (val ("a_key")) == 7 && val ("a_play") > 0.5f && juce::roundToInt (val ("a_mode")) == 0, "un clic sur G majeur est enregistrable (tonalite, mode, jouer)");
    p.playCrowd (crowdId, 1, 0);
    CHECK (val ("a_c1") > 0.5f && juce::roundToInt (val ("a_c1snd")) == 2, "jouer un crowd est enregistrable (son 2 dans le Crowd 1)");
    // à la lecture : le logiciel de musique rejoue les valeurs
    auto host = [&] (const char* id, float real) { auto* prm = p.params.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (real)); p.runControlForTests(); };
    host ("a_key", 2);
    CHECK (p.currentKey() == 2 && p.padActive(), "lecture : la tonalite passe en D, le pad continue");
    host ("a_mode", 1);
    CHECK (p.currentMode() == 1, "lecture : passage en mineur");
    host ("a_play", 0);
    CHECK (! p.padActive(), "lecture : le pad s'arrete");
    host ("a_play", 1);
    CHECK (p.padActive() && p.currentKey() == 2, "lecture : le pad repart en Dm");
    host ("a_c1", 0);
    CHECK (p.crowdSoundIndex (0) < 0, "lecture : le crowd 1 s'arrete");
    host ("a_c1snd", 3); host ("a_c1", 1);
    CHECK (p.crowdSoundIndex (0) == 2 && p.crowdLibraryId (0) == crowdId, "lecture : le crowd 1 rejoue le son 3");
    while (! p.setlist().songs().empty()) p.setlist().remove (0);
    p.setMode (0); p.pressKey (9); p.addSongFromCurrent ("A");
    p.setMode (1); p.pressKey (4); p.addSongFromCurrent ("B");
    host ("a_song", 1);
    CHECK (p.setlist().current() == 0 && p.currentKey() == 9 && p.currentMode() == 0, "lecture : le chant 1 de la setlist revient (chant " << p.setlist().current() << ", " << p.currentKey() << "/" << p.currentMode() << ")");
    host ("a_song", 2);
    CHECK (p.setlist().current() == 1 && p.currentKey() == 4 && p.currentMode() == 1, "lecture : le chant 2 de la setlist revient (chant " << p.setlist().current() << ", " << p.currentKey() << "/" << p.currentMode() << ", a_song " << val ("a_song") << ", a_key " << val ("a_key") << ")");
    // projet rouvert : le pad ne démarre pas tout seul
    juce::MemoryBlock st;
    p.getStateInformation (st);
    SpacepadProcessor q;
    q.setStateInformation (st.getData(), (int) st.getSize());
    q.runControlForTests();
    CHECK (! q.padActive() && q.currentKey() == 4, "projet rouvert : meme tonalite, le pad attend (pas de depart tout seul) (joue " << (int) q.padActive() << ", ton " << q.currentKey() << ")");
    while (! p.setlist().songs().empty()) p.setlist().remove (0);
    p.stopAll();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("spacepad_tests");
    tmp.deleteRecursively();
    auto data = tmp.getChildFile ("donnees");
    data.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s ("SPACEPAD_DATA", data.getFullPathName().toRawUTF8());
   #else
    setenv ("SPACEPAD_DATA", data.getFullPathName().toRawUTF8(), 1);
   #endif

    testNames();
    testAudioKeys();
    auto root = makeTestLibrary (tmp.getChildFile ("Sons"));
    testFolders (root);
    testStore (root);
    testLoopSeam();
    testProcessor();
    testSeparation (root);
    testEffects (tmp);
    testNewFeatures (root, tmp);
    testCrowdFx (root);
    testWavFormats (tmp);
    testAutomation (root);
    if (argc > 1) snapshot (juce::File (juce::String (argv[1])), root);
    if (juce::SystemStats::getEnvironmentVariable ("SPACEPAD_BENCH", {}).isNotEmpty()) benchPaint (root);
   #if ! JUCE_WINDOWS
    if (juce::SystemStats::getEnvironmentVariable ("SPACEPAD_VIDEO", {}).isNotEmpty()) makeVideo (juce::File (juce::SystemStats::getEnvironmentVariable ("SPACEPAD_VIDEO", {})));
   #endif

    std::cout << "\n" << (checks - failures) << " / " << checks << " verifications reussies\n";
    std::cout << (failures == 0 ? "TOUT EST BON\n" : "ECHECS\n");
    return failures == 0 ? 0 : 1;
}
