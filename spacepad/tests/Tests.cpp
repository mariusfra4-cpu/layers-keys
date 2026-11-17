// Spacepad by JB Praise — tests automatiques.
// Lancer : SpacepadTests [dossier_de_sortie]   (capture de l'interface en PNG dans ce dossier)
#include <juce_audio_utils/juce_audio_utils.h>
#include "KeyDetect.h"
#include "Library.h"
#include "PadEngine.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Lang.h"

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
    p.selectPadLibrary (warmId);
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

    // MIDI : la touche Ré (n'importe quelle octave)
    midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
    buf.clear(); p.processBlock (buf, midi); midi.clear();
    juce::Thread::sleep (60);
    p.waitIdleForTests (3000);
    CHECK (p.currentKey() == 2, "touche MIDI Re -> tonalite D");
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
    CHECK (q.padLibraryId() == warmId, "le projet rouvre la meme librairie");
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
    SpacepadProcessor p;
    p.prepareToPlay (48000, 256);
    for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") p.selectPadLibrary (l.id);
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
        v.openScan (root);
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
        struct LC { Lang l; const char* file; const char* word; };
        for (auto lc : { LC { Lang::EN, "spacepad_en.png", "Choose a folder" }, LC { Lang::ES, "spacepad_es.png", "Elegir una carpeta" } })
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
    for (auto& l : p.libraries().snapshot()) if (l.name == "Warm Pad") p.selectPadLibrary (l.id);
    p.pressKey (4);
    p.waitIdleForTests (3000);
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
    for (int i = 0; i < 200; ++i) { buf.clear(); p.processBlock (buf, midi); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (MainView::W, MainView::H);
    auto& v = dynamic_cast<SpacepadEditor&> (*ed).view();
    juce::Image img (juce::Image::ARGB, MainView::W, MainView::H, true);
    auto timeIt = [&] (const char* name, juce::Component& c)
    {
        const int N = 15;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < N; ++i) { juce::Graphics g (img); c.paintEntireComponent (g, false); }
        std::cout << "  " << name << " : " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / N, 2) << " ms\n";
    };
    timeIt ("fenetre entiere", v);
    for (auto* c : v.getChildren())
    {
        juce::String n = dynamic_cast<KeyRing*> (c) ? "anneau" : dynamic_cast<Header*> (c) ? "entete" : dynamic_cast<Waveform*> (c) ? "forme d'onde"
                       : dynamic_cast<LibraryPanel*> (c) ? "bibliotheque" : dynamic_cast<Knob*> (c) ? "" : "";
        if (n.isNotEmpty()) timeIt (n.toRawUTF8(), *c);
    }
    juce::ignoreUnused (root);
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
    if (argc > 1) snapshot (juce::File (juce::String (argv[1])), root);
    if (juce::SystemStats::getEnvironmentVariable ("SPACEPAD_BENCH", {}).isNotEmpty()) benchPaint (root);

    std::cout << "\n" << (checks - failures) << " / " << checks << " verifications reussies\n";
    std::cout << (failures == 0 ? "TOUT EST BON\n" : "ECHECS\n");
    return failures == 0 ? 0 : 1;
}
