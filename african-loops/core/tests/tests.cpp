// Tests du moteur African Loops : loops de batterie fabriquées (kick, snare,
// hi-hat, conga, talking drum) à des tempos connus.
#include "AfricanLoops/AfricanLoops.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>

using namespace africanloops;
namespace fs = std::filesystem;

static int failures = 0;
static const char* current = "";
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::printf("ÉCHEC [%s] ligne %d : ", current, __LINE__);      \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

static std::map<std::string, std::vector<int>> pattern = {
    {"kick", {0, 8, 10}}, {"snare", {4, 12}}, {"hat", {0, 2, 4, 6, 8, 10, 12, 14}},
    {"conga", {3, 7, 11}}, {"talk", {13}},
};

// Fabrique une loop : tempo, nombre de mesures, silence ajouté à la fin.
static Audio synthLoop(double bpm, int bars, double tail) {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> noise(-1, 1);
    const double six = 60 / bpm / 4;
    const int total = (int)std::lround(bars * 16 * six * kRate) + (int)(tail * kRate);
    Audio a;
    a.rate = kRate;
    a.L.assign(total, 0.f);
    a.R.assign(total, 0.f);
    auto add = [&](int start, double dur, auto f) {
        int n = (int)(dur * kRate);
        for (int i = 0; i < n && start + i < total; ++i) {
            float v = (float)f((double)i / kRate);
            a.L[start + i] += v;
            a.R[start + i] += v;
        }
    };
    for (int bar = 0; bar < bars; ++bar)
        for (auto& [name, steps] : pattern)
            for (int s : steps) {
                int start = (int)std::lround((bar * 16 + s) * six * kRate);
                if (name == "kick")
                    add(start, 0.5, [](double t) {
                        return 0.8 * std::sin(2 * kPi * (50 * t + 70 * 0.03 * (1 - std::exp(-t / 0.03)))) * std::exp(-t / 0.12);
                    });
                else if (name == "snare")
                    add(start, 0.3, [&](double t) {
                        return 0.45 * noise(rng) * std::exp(-t / 0.06) + 0.35 * std::sin(2 * kPi * 190 * t) * std::exp(-t / 0.05);
                    });
                else if (name == "hat") {
                    double p1 = 0, p2 = 0;
                    add(start, 0.12, [&](double t) {
                        double x = noise(rng), h = x - 2 * p1 + p2;  // passe-haut
                        p2 = p1; p1 = x;
                        return 0.12 * h * std::exp(-t / 0.025);
                    });
                } else if (name == "conga")
                    add(start, 0.5, [](double t) {
                        return 0.5 * (std::sin(2 * kPi * 330 * t) + 0.3 * std::sin(2 * kPi * 660 * t)) * std::exp(-t / 0.09);
                    });
                else if (name == "talk")
                    add(start, 0.8, [](double t) {  // note qui monte de 160 à 260 Hz
                        double ph = t <= 0.25 ? 2 * kPi * (160 * t + 100 * t * t / 0.5)
                                              : 2 * kPi * (160 * 0.25 + 100 * 0.25 / 2) + 2 * kPi * 260 * (t - 0.25);
                        return 0.55 * std::sin(ph) * std::exp(-t / 0.2);
                    });
            }
    return a;
}

static double energy(const std::vector<float>& x, int from, int to) {
    double s = 0;
    for (int i = std::max(0, from); i < to && i < (int)x.size(); ++i) s += (double)x[i] * x[i];
    return s;
}

static void testTempoFromLength() {
    current = "tempo par la durée";
    auto info = analyse(synthLoop(96, 4, 0), "Worship_Groove.wav");
    CHECK(std::abs(info.tempo.bpm - 96) < 0.01 && info.tempo.bars == 4 && info.tempo.source == tempo_source::Length,
          "%.2f BPM, %d mesures, source %s", info.tempo.bpm, info.tempo.bars, info.tempo.source.c_str());
}

static void testTempoVarious() {
    current = "tempos variés";
    for (double bpm : {72.0, 88.0, 100.0, 118.0, 124.0, 140.0}) {
        auto info = analyse(synthLoop(bpm, 2, 0), "loop.wav");
        CHECK(std::abs(info.tempo.bpm - bpm) < 0.01, "%.0f BPM : trouvé %.2f (%s)", bpm, info.tempo.bpm, info.tempo.source.c_str());
    }
}

static void testTempoAnalysisOnly() {
    current = "loop mal coupée";
    auto info = analyse(synthLoop(110, 4, 0.37), "loop.wav");
    CHECK(std::abs(info.tempo.bpm - 110) <= 1, "%.2f BPM (%s), attendu ≈ 110", info.tempo.bpm, info.tempo.source.c_str());
}

static void testFilename() {
    current = "tempo dans le nom";
    struct C { const char* name; double bpm; bool strong; };
    for (C c : {C{"Worship_96bpm.wav", 96, true}, C{"Love Groove 87 BPM Am.wav", 87, true}, C{"BPM120_afro.wav", 120, true},
                C{"Coupe_Decale_118.5bpm.mp3", 118.5, true}, C{"Worship_96_Am.wav", 96, false},
                C{"Kit 2024 v3.wav", 0, false}, C{"Djembe_Solo.wav", 0, false}}) {
        bool strong;
        double b = tempoFromFilename(c.name, strong);
        CHECK(b == c.bpm && strong == c.strong, "%s : %.2f %d", c.name, b, (int)strong);
    }
}

static void testWAVAndFileTempo() {
    current = "WAV + tempo du fichier";
    Audio a = synthLoop(100, 1, 0), b;
    std::string err;
    CHECK(parseWAV(encodeWAV(a, 87.5, 4), b, err), "relecture : %s", err.c_str());
    CHECK(b.metaBPM == 87.5 && b.metaBeats == 4 && b.frames() == a.frames() && b.rate == kRate, "bpm %.2f temps %d", b.metaBPM, b.metaBeats);
    bool same = true;
    for (int i = 0; i < a.frames(); i += 997) same &= std::abs(a.L[i] - b.L[i]) < 1e-6 || std::abs(a.L[i]) >= 1;
    CHECK(same, "échantillons différents après relecture");
    auto info = analyse(b, "x.wav");
    CHECK(info.tempo.bpm == 87.5 && info.tempo.source == tempo_source::File, "tempo du fichier ignoré : %.2f %s", info.tempo.bpm, info.tempo.source.c_str());
}

static std::set<std::string> kindsOf(const LoopInfo& info) {
    std::set<std::string> s;
    for (auto& p : info.percussions) s.insert(p.kind);
    return s;
}

static void testPercussions() {
    current = "percussions reconnues";
    auto got = kindsOf(analyse(synthLoop(96, 4, 0), "x.wav"));
    for (const auto& k : percussionOrder()) CHECK(got.count(k), "%s non reconnu", k.c_str());

    current = "pas de percussion inventée";
    auto saved = pattern;
    pattern.erase("talk");
    pattern.erase("conga");
    got = kindsOf(analyse(synthLoop(100, 4, 0), "x.wav"));
    CHECK(!got.count(perc::Talking) && !got.count(perc::Hand), "talking drum ou conga inventé");
    pattern = saved;
}

static void testSeparation() {
    current = "séparation";
    Audio a = synthLoop(96, 2, 0);
    std::vector<Stem> stems;
    auto info = separate(a, "x.wav", stems);
    CHECK(stems.size() >= 4, "seulement %zu pistes", stems.size());
    double maxErr = 0;
    for (int i = 0; i < a.frames(); ++i) {
        float s = 0;
        for (auto& st : stems) s += st.audio->L[i];
        maxErr = std::max(maxErr, (double)std::abs(s - a.L[i]));
    }
    CHECK(maxErr < 1e-4, "la somme des pistes s'écarte de l'original : %g", maxErr);
    const double six = 60.0 / 96 / 4 * kRate;
    std::map<std::string, const Audio*> by;
    for (auto& st : stems) by[st.kind] = st.audio.get();
    auto check = [&](const char* kind, int own, int other) {
        auto it = by.find(kind);
        if (it == by.end()) { CHECK(false, "piste %s absente", kind); return; }
        int o = (int)(own * six), p = (int)(other * six), w = (int)(0.05 * kRate);
        double eo = energy(it->second->L, o, o + w), ep = energy(it->second->L, p, p + w);
        CHECK(eo >= 4 * ep, "piste %s : %.3g sur son coup, %.3g ailleurs", kind, eo, ep);
    };
    check(perc::Kick, 8, 4);
    check(perc::Snare, 4, 3);
    check(perc::Hand, 3, 4);
    check(perc::Talking, 13, 4);
}

static void writeFile(const fs::path& p, const std::vector<uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

static void testLibrary() {
    current = "bibliothèque";
    fs::path dir = fs::temp_directory_path() / ("al-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    writeFile(dir / "Worship" / "Praise Drums.wav", encodeWAV(synthLoop(96, 1, 0), 0, 0));
    writeFile(dir / "I Love" / "Soft 01.wav", encodeWAV(synthLoop(80, 1, 0), 0, 0));
    writeFile(dir / "love_groove_90bpm.wav", encodeWAV(synthLoop(90, 1, 0), 0, 0));
    writeFile(dir / "Djembe solo.wav", encodeWAV(synthLoop(100, 1, 0), 0, 0));
    writeFile(dir / fs::u8path(u8"Coupé-Décalé") / fs::u8path(u8"Abidjan été.wav"), encodeWAV(synthLoop(120, 1, 0), 118, 4));
    writeFile(dir / "notes.txt", {'x'});
    Library lib;
    std::string err;
    int calls = 0;
    CHECK(Library::scan(dir.string(), lib, err, [&](int, int, const std::string&) { ++calls; }), "scan : %s", err.c_str());
    CHECK(lib.entries.size() == 5 && calls == 5, "%zu loops, %d analyses", lib.entries.size(), calls);
    std::map<std::string, std::string> want = {
        {"Worship/Praise Drums.wav", "Worship"}, {"I Love/Soft 01.wav", "I Love"},
        {"love_groove_90bpm.wav", kUncategorized}, {"Djembe solo.wav", kUncategorized},
        {"Coupé-Décalé/Abidjan été.wav", "Coupé-Décalé"}};
    for (auto& e : lib.entries) {
        CHECK(want[e.path] == e.category, "%s classé dans « %s »", e.path.c_str(), e.category.c_str());
        if (e.path == "love_groove_90bpm.wav") CHECK(e.info.tempo.bpm == 90, "BPM du nom ignoré : %.2f", e.info.tempo.bpm);
        if (e.path == "Coupé-Décalé/Abidjan été.wav")
            CHECK(e.info.tempo.bpm == 118 && e.info.tempo.source == tempo_source::File, "fichier avec accents : %.2f %s",
                  e.info.tempo.bpm, e.info.tempo.source.c_str());
        if (e.path == "Worship/Praise Drums.wav") CHECK(std::abs(e.info.tempo.bpm - 96) < 0.01, "BPM %.2f", e.info.tempo.bpm);
    }
    // correction à la main puis nouvelle analyse : rien n'est refait, la correction reste
    lib.setCategory("Djembe solo.wav", "Afrobeats");
    CHECK(lib.save(), "sauvegarde");
    Library lib2;
    calls = 0;
    CHECK(Library::scan(dir.string(), lib2, err, [&](int, int, const std::string&) { ++calls; }), "scan 2 : %s", err.c_str());
    CHECK(calls == 0, "%d loops réanalysées alors que rien n'a changé", calls);
    auto af = lib2.search("Afrobeats", "");
    CHECK(af.size() == 1 && af[0]->path == "Djembe solo.wav", "correction perdue");
    CHECK(lib2.search("", "PRAISE").size() == 1, "recherche");
    CHECK(lib2.categories().back().name == kUncategorized || lib2.search(kUncategorized, "").empty(), "« Non classés » pas à la fin");
    for (auto& e : lib2.entries)
        if (e.path == "Worship/Praise Drums.wav")
            CHECK(std::abs(e.info.tempo.bpm - 96) < 0.01 && !e.info.slices.empty() && !e.info.percussions.empty(), "index relu incomplet");

    current = "chargement instantané depuis la bibliothèque";
    {
        const Entry* e = lib2.find((dir / "Worship" / "Praise Drums.wav").string());
        CHECK(e && lib2.upToDate(*e) && !e->info.hits.empty(), "loop introuvable ou analyse perdue dans l'index");
        if (e) {
            Audio a;
            std::string er;
            loadWAV((dir / "Worship" / "Praise Drums.wav").string(), a, er);
            std::vector<Stem> ref;
            auto full = separate(a, "x.wav", ref);
            auto fast = separateWith(a, e->info);
            CHECK(fast.size() == ref.size(), "pas les mêmes pistes : %zu au lieu de %zu", fast.size(), ref.size());
            double diff = 0;
            for (size_t i = 0; i < std::min(fast.size(), ref.size()); ++i)
                for (int j = 0; j < a.frames(); ++j)
                    diff = std::max(diff, (double)std::abs(fast[i].audio->L[(size_t)j] - ref[i].audio->L[(size_t)j]));
            CHECK(diff < 1e-3, "les pistes rechargées diffèrent (%g)", diff);
            CHECK(full.tempo.bpm == e->info.tempo.bpm, "tempo différent");
        }
        CHECK(lib2.find((dir / ".." / "ailleurs.wav").string()) == nullptr, "fichier hors bibliothèque trouvé");
    }

    current = "favoris et styles créés à la main";
    CHECK(lib2.setFavorite("Worship/Praise Drums.wav", true) && lib2.favoriteCount() == 1, "étoile");
    CHECK(lib2.addStyle("Mes Préférées") && !lib2.addStyle("Mes Préférées") && !lib2.addStyle("Worship"), "nouveau style");
    CHECK(lib2.save(), "sauvegarde");
    {
        Library again;
        CHECK(Library::scan(dir.string(), again, err, nullptr), "scan");
        bool vide = false;
        for (auto& c : again.categories()) vide |= c.name == "Mes Préférées" && c.count == 0;
        CHECK(vide, "le style vide créé à la main a disparu");
        CHECK(again.favoriteCount() == 1, "l'étoile a disparu après une nouvelle analyse");
        CHECK(again.renameCategory("Mes Préférées", "Top") == 1, "renommer un style vide");
        CHECK(again.removeCategory("Top") == 1 && again.styles.empty(), "supprimer un style vide");
    }

    current = "supprimer / renommer un style";
    CHECK(lib2.removeCategory("I Love") == 1, "suppression du style");
    CHECK(lib2.renameCategory("Worship", "Louange") == 1, "renommage du style");
    CHECK(lib2.removeCategory(kUncategorized) == 0, "« Non classés » ne doit pas pouvoir être supprimé");
    CHECK(lib2.save(), "sauvegarde");
    Library lib3;
    calls = 0;
    CHECK(Library::scan(dir.string(), lib3, err, [&](int, int, const std::string&) { ++calls; }), "scan 3");
    bool gone = true, renamed = false;
    for (auto& c : lib3.categories()) {
        if (c.name == "I Love" || c.name == "Worship") gone = false;
        if (c.name == "Louange") renamed = true;
    }
    CHECK(gone && renamed && calls == 0, "style supprimé ou renommé revenu après une nouvelle analyse");
    CHECK(lib3.search(kUncategorized, "soft").size() == 1, "la loop du style supprimé n'est pas dans Non classés");
    CHECK(fs::exists(dir / "I Love" / "Soft 01.wav"), "un fichier a été effacé !");
    fs::remove_all(dir);

    current = "styles";
    CHECK(categoryOf("loops_coupé-décalé_01.wav") == "Coupé-Décalé", "%s", categoryOf("loops_coupé-décalé_01.wav").c_str());
    CHECK(categoryOf("Adoration_Talking.wav") == "Worship", "adoration");
    CHECK(categoryOf("Mes Rythmes\\x.wav") == "Mes Rythmes", "chemin Windows");
    CHECK(categoryOf("love_groove.wav") == kUncategorized, "« Love » n'est pas un style");
}

static void testLooper() {
    current = "lecteur au tempo d'origine";
    Audio a = synthLoop(96, 1, 0);
    std::vector<Stem> stems;
    auto info = separate(a, "x.wav", stems);
    Looper l(kRate);
    l.load(info, stems);
    l.setGlobalBPM(96);
    l.play();
    const int n = a.frames();
    std::vector<float> L(n), R(n);
    l.render(L.data(), R.data(), n);
    int bad = -1;
    for (int i = 0; i < n && bad < 0; ++i)
        if (std::abs(L[i] - a.L[i]) > 1e-3) bad = i;
    CHECK(bad < 0, "échantillon %d différent de l'original", bad);

    current = "lecteur à 120 BPM";
    l.setGlobalBPM(120);
    l.stop();
    l.play();
    double want = (double)n * 96 / 120;
    CHECK(std::abs(l.loopLength() - want) < 1, "longueur %.0f au lieu de %.0f", l.loopLength(), want);
    int m = (int)want;
    std::vector<float> L2(m), R2(m);
    l.render(L2.data(), R2.data(), m);
    int s = (int)(60.0 / 120 * kRate);  // 2e temps : la caisse claire
    double before = energy(L2, s - (int)(0.03 * kRate), s - 220), after = energy(L2, s, s + 1100);
    CHECK(after >= 5 * before, "coup absent au 2e temps : avant %.3g après %.3g", before, after);
    l.setPresetBPM(true);
    CHECK(l.playingBPM() == 96 && std::abs(l.loopLength() - n) < 1, "Preset BPM : %.2f", l.playingBPM());

    current = "mute / solo";
    l.setPresetBPM(false);
    l.setGlobalBPM(96);
    for (int i = 0; i < kNumTracks; ++i) l.withTrack(i, [](Track& t) { t.mute = true; });
    std::vector<float> o(4096), o2(4096);
    l.stop(); l.play();
    l.render(o.data(), o2.data(), 4096);
    CHECK(energy(o, 0, 4096) == 0, "son entendu alors que tout est coupé");
    l.stop(); l.play();
    l.withTrack(0, [](Track& t) { t.mute = false; });  // pas en solo : ne doit pas s'entendre
    l.withTrack(1, [](Track& t) { t.mute = false; t.solo = true; });
    l.render(o.data(), o2.data(), 4096);
    bad = -1;
    for (int i = 0; i < 4096 && bad < 0; ++i)
        if (std::abs(o[i] - stems[1].audio->L[i]) > 1e-3) bad = i;
    CHECK(bad < 0, "solo : échantillon %d", bad);
}

static void testBadFiles() {
    current = "fichiers abîmés";
    Audio a;
    std::string err;
    std::string s1 = std::string("RIFF\0\0\0\0WAVE", 12), s2 = "hello";
    CHECK(!parseWAV(std::vector<uint8_t>(s1.begin(), s1.end()), a, err), "WAV vide accepté");
    CHECK(!parseWAV(std::vector<uint8_t>(s2.begin(), s2.end()), a, err), "faux WAV accepté");
    Audio silence;
    silence.L.assign(kRate, 0.f);
    silence.R.assign(kRate, 0.f);
    CHECK(analyse(silence, "silence.wav").percussions.empty(), "percussions trouvées dans le silence");
}

static void testContinuousLoop() {
    current = "boucle continue (fin replacée au début)";
    // 2 mesures à 96 BPM + 0,4 s de résonance en trop à la fin
    Audio a = synthLoop(96, 2, 0.4);
    const int musical = (int)std::lround(8 * 60.0 / 96 * kRate);
    for (int i = musical; i < a.frames(); ++i) a.L[i] = a.R[i] = 0.3f * std::sin(i * 0.05f);  // la « queue »
    std::vector<Stem> stems;
    auto info = separate(a, "groove_96bpm.wav", stems);
    CHECK(info.tempo.bpm == 96 && info.tempo.beats == 8, "%.2f BPM, %d temps", info.tempo.bpm, info.tempo.beats);
    Looper l(kRate);
    l.load(info, stems);
    l.setGlobalBPM(96);
    CHECK(std::abs(l.loopLength() - musical) < 1, "boucle de %.0f au lieu de %d", l.loopLength(), musical);
    l.play();
    std::vector<float> L(musical * 2), R(musical * 2);
    l.render(L.data(), R.data(), musical * 2);
    // au 2e tour, le début contient la queue : il diffère de l'original sans queue
    double tailE = energy(L, musical, musical + 2000);
    CHECK(tailE > energy(a.L, 0, 2000) * 1.05, "la queue n'est pas replacée au début");
    // aucune coupure : le niveau juste avant et juste après la jonction est continu
    CHECK(std::abs(L[musical - 1]) < 1.5 && std::isfinite(L[musical]), "jonction anormale");

    current = "son très court, tempo inconnu";
    Audio shot;
    shot.L.assign(kRate / 5, 0.f);  // 0,2 s : un seul coup
    for (int i = 0; i < (int)shot.L.size(); ++i) shot.L[i] = 0.5f * std::sin(i * 0.02f) * std::exp(-i / 3000.f);
    shot.R = shot.L;
    auto si = separate(shot, "coup.wav", stems);
    Looper l2(kRate);
    l2.load(si, stems);
    l2.setGlobalBPM(120);  // un temps = 0,5 s : le coup revient à chaque temps
    CHECK(std::abs(l2.loopLength() - kRate / 2.0) < 1, "boucle de %.0f au lieu de %d", l2.loopLength(), kRate / 2);
    l2.play();
    std::vector<float> o(kRate * 2), o2(kRate * 2);
    l2.render(o.data(), o2.data(), kRate * 2);
    for (int beat = 0; beat < 4; ++beat) {
        int s = beat * kRate / 2;
        CHECK(energy(o, s, s + 2000) > 1, "coup absent au temps %d", beat + 1);
        CHECK(energy(o, s + kRate / 4, s + kRate / 2 - 10) < 1e-6, "pas de silence après le coup %d", beat + 1);
    }

    current = "Double / Half";
    l.setFeel(2);
    CHECK(std::abs(l.loopLength() - musical / 2.0) < 1 && l.playingBPM() == 192, "Double : %.0f", l.loopLength());
    l.setFeel(0.5);
    CHECK(std::abs(l.loopLength() - musical * 2.0) < 1 && l.playingBPM() == 48, "Half : %.0f", l.loopLength());
    l.setFeel(1);
}

static void testBounceAndMetronome() {
    current = "glisser vers le logiciel (bounce)";
    Audio a = synthLoop(96, 1, 0);
    std::vector<Stem> stems;
    auto info = separate(a, "x.wav", stems);
    Looper l(kRate);
    l.load(info, stems);
    l.setGlobalBPM(110);
    l.withTrack(2, [](Track& t) { t.mute = true; });
    double bpm = 0;
    auto b = l.bounce(-1, 1, bpm);
    CHECK(bpm == 110 && std::abs(b->frames() - l.loopLength()) <= 1, "bounce : %.2f BPM, %d échantillons", bpm, b->frames());
    l.play();
    std::vector<float> L(b->frames()), R(b->frames());
    l.render(L.data(), R.data(), b->frames());
    int bad = -1;
    for (int i = 0; i < b->frames() && bad < 0; ++i)
        if (std::abs(L[i] - b->L[i]) > 1e-5) bad = i;
    CHECK(bad < 0, "le fichier glissé diffère de ce qu'on entend (échantillon %d)", bad);
    auto one = l.bounce(0, 2, bpm);
    CHECK(std::abs(one->frames() - 2 * l.loopLength()) <= 2, "piste seule sur 2 tours : %d", one->frames());
    double e0 = energy(one->L, 0, one->frames());
    auto two = l.bounce(1, 2, bpm);
    CHECK(e0 > 0 && std::abs(e0 - energy(two->L, 0, two->frames())) > 1e-6, "pistes seules identiques");
    Track t2 = l.track(2);
    CHECK(t2.mute, "le bounce a modifié les réglages du lecteur");

    current = "métronome";
    Audio silence;
    silence.L.assign(kRate * 2, 0.f);
    silence.R = silence.L;
    LoopInfo si;
    si.tempo.bpm = 120;
    si.tempo.beats = 4;
    Looper m(kRate);
    m.load(si, {{"LOOP", std::make_shared<Audio>(silence)}});
    m.setGlobalBPM(120);
    m.setMetronome(true);
    m.play();
    std::vector<float> o(kRate * 2), o2(kRate * 2);
    m.render(o.data(), o2.data(), kRate * 2);
    for (int beat = 0; beat < 4; ++beat) {
        int s = beat * kRate / 2;
        CHECK(energy(o, s, s + 600) > 0.1, "pas de clic au temps %d", beat + 1);
        CHECK(energy(o, s + 3000, s + kRate / 2 - 100) == 0, "bruit entre les clics");
    }
    CHECK(energy(o, 0, 600) > energy(o, kRate / 2, kRate / 2 + 600), "le 1er temps n'est pas accentué");
}

static void testSwitchOnNextBar() {
    current = "loop suivante : entre à la prochaine mesure";
    Audio a = synthLoop(96, 2, 0), b = synthLoop(96, 1, 0);
    for (auto& v : b.L) v *= 0.5f;  // b plus douce : on reconnaît laquelle joue
    for (auto& v : b.R) v *= 0.5f;
    std::vector<Stem> sa, sb;
    auto ia = separate(a, "a.wav", sa), ib = separate(b, "b.wav", sb);
    Looper l(kRate);
    l.load(ia, sa);
    l.setGlobalBPM(96);
    l.play();
    const double beat = 60.0 / 96 * kRate;
    std::vector<float> L((size_t)(beat * 5.25)), R(L.size());
    l.render(L.data(), R.data(), (int)L.size());  // 5 temps et quart : 2e mesure entamée
    l.load(ib, sb);
    CHECK(l.hasPendingLoop(), "la nouvelle loop doit attendre la mesure suivante");
    std::vector<float> L2((size_t)(beat * 2.7)), R2(L2.size());  // jusqu'à 7,95 temps
    l.render(L2.data(), R2.data(), (int)L2.size());
    CHECK(l.hasPendingLoop(), "la loop est entrée avant la barre de mesure");
    // de 7,95 à 8,5 temps : on doit entendre la nouvelle loop pile au temps 8 (son début)
    const int n = (int)(beat * 0.55);
    std::vector<float> L3((size_t)n), R3((size_t)n);
    l.render(L3.data(), R3.data(), n);
    CHECK(!l.hasPendingLoop(), "la loop n'est pas entrée à la mesure");
    const int at8 = (int)std::lround(beat * 8) - (int)L.size() - (int)L2.size();
    double diff = 0;
    for (int i = 0; i < 2000; ++i) diff = std::max(diff, (double)std::abs(L3[(size_t)(at8 + i)] - b.L[(size_t)i]));
    CHECK(diff < 2e-3, "la nouvelle loop ne démarre pas sur le temps 1 de la mesure (écart %g)", diff);
    // à l'arrêt : tout de suite, depuis le début
    l.stop();
    l.load(ia, sa);
    CHECK(!l.hasPendingLoop() && l.position() == 0, "à l'arrêt, une nouvelle loop doit être prête tout de suite");
}

static void testMeters() {
    current = "mesure 3/4 : la loop devient une valse";
    Audio a = synthLoop(98, 2, 0);
    std::vector<Stem> st;
    auto info = separate(a, "x.wav", st);
    Looper l(kRate);
    l.load(info, st);
    l.setGlobalBPM(98);  // 1 temps = 27000 échantillons pile
    l.setMeter(Meter::M34);
    const double beat = 60.0 / 98 * kRate;
    CHECK(std::abs(l.loopLength() - 6 * beat) < 2 && l.beatsPerBar() == 3, "3/4 : boucle de %.2f temps", l.loopLength() / beat);
    l.play();
    const int n = (int)(6 * beat);
    std::vector<float> L((size_t)n), R((size_t)n);
    l.render(L.data(), R.data(), n);
    // temps 1-2-3 de la mesure 1, puis temps 1-2-3 de la mesure 2 (le temps 4 est sauté)
    auto same = [&](int outBeat, int srcBeat) {
        double d = 0;
        const int o = (int)std::lround(outBeat * beat) + 400, sIdx = (int)std::lround(srcBeat * beat) + 400;
        for (int i = 0; i < 3000; ++i) d = std::max(d, (double)std::abs(L[(size_t)(o + i)] - a.L[(size_t)(sIdx + i)]));
        return d;
    };
    CHECK(same(1, 1) < 1e-3 && same(2, 2) < 1e-3, "3/4 : temps 2 et 3 faux");
    CHECK(same(3, 4) < 1e-3 && same(5, 6) < 1e-3, "3/4 : la 2e mesure ne reprend pas au temps 1 de la mesure suivante (%g %g)", same(3, 4), same(5, 6));

    current = "mesure 6/8 : balancement ternaire";
    l.stop();
    l.setMeter(Meter::M68);
    CHECK(std::abs(l.loopLength() - 6 * beat) < 2, "6/8 : boucle de %.2f temps", l.loopLength() / beat);
    l.play();
    l.render(L.data(), R.data(), n);
    auto sameEighth = [&](int outEighth, int srcEighth) {
        double d = 0;
        const int o = (int)std::lround(outEighth * beat / 2) + 300, sIdx = (int)std::lround(srcEighth * beat / 2) + 300;
        for (int i = 0; i < 2000; ++i) d = std::max(d, (double)std::abs(L[(size_t)(o + i)] - a.L[(size_t)(sIdx + i)]));
        return d;
    };
    CHECK(sameEighth(1, 1) < 1e-3 && sameEighth(3, 4) < 1e-3 && sameEighth(5, 6) < 1e-3, "6/8 : croches mal placées");
    CHECK(sameEighth(6, 8) < 1e-3, "6/8 : la 2e mesure ne reprend pas au début de la mesure suivante");
    double bpm = 0;
    auto b = l.bounce(-1, 1, bpm);
    CHECK(std::abs(b->frames() - 6 * beat) < 2, "fichier glissé en 6/8 : %d", b->frames());

    current = "mesure : loop qui n'a pas des mesures entières";
    const double beat96 = 60.0 / 96 * kRate;
    Audio odd = synthLoop(96, 1, 0);
    odd.L.resize((size_t)(6 * beat96));  // 6 temps : pas de transformation possible
    odd.R.resize((size_t)(6 * beat96));
    std::vector<Stem> so;
    auto io = separate(odd, "y_96bpm.wav", so);
    Looper l2(kRate);
    l2.load(io, so);
    l2.setPresetBPM(true);
    l2.setMeter(Meter::M34);
    CHECK(std::abs(l2.loopLength() - 6 * beat96) < 2, "loop de 6 temps : %.2f temps", l2.loopLength() / beat96);

    current = "métronome en 3/4";
    Audio silence;
    silence.L.assign((size_t)kRate * 3, 0.f);
    silence.R = silence.L;
    LoopInfo si;
    si.tempo.bpm = 120;
    si.tempo.beats = 6;
    Looper m(kRate);
    m.load(si, {{"LOOP", std::make_shared<Audio>(silence)}});
    m.setGlobalBPM(120);
    m.setMeter(Meter::M34);
    m.setMetronome(true);
    m.play();
    std::vector<float> o((size_t)kRate * 2), o2(o.size());
    m.render(o.data(), o2.data(), (int)o.size());
    const int b0 = 0, b1 = kRate / 2, b3 = 3 * kRate / 2;
    CHECK(energy(o, b3, b3 + 600) > energy(o, b1, b1 + 600) * 1.5 && energy(o, b0, b0 + 600) > energy(o, b1, b1 + 600) * 1.5,
          "en 3/4, le temps 4 doit être le 1er de la mesure suivante (accentué)");
}

static void testHostClock() {
    current = "horloge externe (Link) : décompte puis départ sur la mesure";
    Audio a = synthLoop(96, 1, 0);
    std::vector<Stem> st;
    auto info = separate(a, "x.wav", st);
    Looper l(kRate);
    l.load(info, st);
    MixSettings s;
    s.globalBPM = 96;
    s.playing = true;
    s.hostClock = true;
    s.hostBeats = -1.0;  // un temps avant le départ
    std::vector<float> o(4096), o2(4096);
    l.process(s, o.data(), o2.data(), 4096);
    CHECK(energy(o, 0, 4096) == 0, "du son pendant le décompte");
    s.hostBeats = 0;
    l.process(s, o.data(), o2.data(), 4096);
    double d = 0;
    for (int i = 0; i < 4096; ++i) d = std::max(d, (double)std::abs(o[(size_t)i] - a.L[(size_t)i]));
    CHECK(d < 1e-3, "la loop ne part pas pile au temps 0 de Link (écart %g)", d);
    // Double : nos temps vont deux fois plus vite que ceux de Link, toujours calés
    s.feel = 2;
    s.hostBeats = 1.0;  // temps 1 de Link = notre temps 2
    l.process(s, o.data(), o2.data(), 1000);
    const int at = (int)std::lround(2 * 60.0 / 96 * kRate);
    double e1 = 0, e2 = 0;
    for (int i = 0; i < 300; ++i) { e1 += o[(size_t)i] * o[(size_t)i]; e2 += a.L[(size_t)(at + i)] * a.L[(size_t)(at + i)]; }
    CHECK(e1 > 0.3 * e2 && e2 > 0, "en Double, pas calé sur Link");
}

static std::vector<uint8_t> withSmpl(std::vector<uint8_t> wav, int start, int end) {
    // ajoute un bloc « smpl » avec une boucle [start, end]
    std::vector<uint8_t> c(60, 0);
    auto put = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) c[at + i] = (uint8_t)(v >> (8 * i)); };
    put(28, 1);
    put(44, (uint32_t)start);
    put(48, (uint32_t)(end - 1));
    std::vector<uint8_t> chunk = {'s', 'm', 'p', 'l', 60, 0, 0, 0};
    chunk.insert(chunk.end(), c.begin(), c.end());
    wav.insert(wav.begin() + 36, chunk.begin(), chunk.end());  // juste après « fmt »
    uint32_t riff = (uint32_t)wav.size() - 8;
    for (int i = 0; i < 4; ++i) wav[4 + i] = (uint8_t)(riff >> (8 * i));
    return wav;
}

static void testSeamlessFiles() {
    current = "points de boucle du WAV";
    {
        Audio a = synthLoop(96, 2, 0.6);  // 2 mesures + 0,6 s en trop, avec une boucle écrite dans le fichier
        const int musical = (int)std::lround(8 * 60.0 / 96 * kRate);
        auto bytes = withSmpl(encodeWAV(a, 0, 0), 0, musical);
        fs::path p = fs::temp_directory_path() / "al_smpl.wav";
        std::ofstream(p, std::ios::binary).write((const char*)bytes.data(), (std::streamsize)bytes.size());
        Audio b;
        std::string err;
        CHECK(loadWAV(p.string(), b, err) && b.frames() == musical, "zone de boucle ignorée : %d au lieu de %d", b.frames(), musical);
        CHECK(std::abs(analyse(b, "x.wav").tempo.bpm - 96) < 0.01, "tempo de la zone de boucle");
        fs::remove(p);
    }
    current = "silence de l'encodeur MP3";
    {
        Audio a = synthLoop(100, 1, 0);
        const int n = a.frames();
        Audio m = a;
        m.L.insert(m.L.begin(), 1105, 0.f);  // retard typique d'un encodeur MP3
        m.R.insert(m.R.begin(), 1105, 0.f);
        m.L.insert(m.L.end(), 700, 0.f);
        m.R.insert(m.R.end(), 700, 0.f);
        prepareLoop(m, true);
        CHECK(m.frames() <= n && m.frames() > n - 50, "silence MP3 non retiré : %d au lieu de %d", m.frames(), n);
        // un vrai soupir au début (plus de 60 ms) est gardé
        Audio r = a;
        r.L.insert(r.L.begin(), kRate / 5, 0.f);
        r.R.insert(r.R.begin(), kRate / 5, 0.f);
        const int before = r.frames();
        prepareLoop(r, true);
        CHECK(r.frames() == before, "un silence musical a été coupé");
    }
    current = "tempo écrit dans le MP3 (ID3)";
    {
        auto tag = [](int ver, const char* id, const std::string& text) {
            std::vector<uint8_t> f;
            f.insert(f.end(), id, id + 4);
            const uint32_t fs = (uint32_t)text.size() + 1;
            if (ver >= 4) for (int k = 3; k >= 0; --k) f.push_back((uint8_t)((fs >> (7 * k)) & 0x7f));
            else for (int k = 3; k >= 0; --k) f.push_back((uint8_t)(fs >> (8 * k)));
            f.push_back(0); f.push_back(0); f.push_back(0);
            f.insert(f.end(), text.begin(), text.end());
            std::vector<uint8_t> h = {'I', 'D', '3', (uint8_t)ver, 0, 0};
            const uint32_t sz = (uint32_t)f.size();
            for (int k = 3; k >= 0; --k) h.push_back((uint8_t)((sz >> (7 * k)) & 0x7f));
            h.insert(h.end(), f.begin(), f.end());
            return h;
        };
        CHECK(readMp3Bpm(tag(3, "TBPM", "96")) == 96, "ID3v2.3");
        CHECK(readMp3Bpm(tag(4, "TBPM", "118.5")) == 118.5, "ID3v2.4");
        CHECK(readMp3Bpm(tag(3, "TIT2", "Love song")) == 0, "titre pris pour un tempo");
        CHECK(readMp3Bpm({'f', 'o', 'o'}) == 0, "fichier sans étiquette");
    }
    current = "longueur de boucle toujours calée sur la mesure";
    for (double extra : {0.9, 1.6, -0.4}) {  // en temps : fin qui dépasse, ou fichier un peu court
        Audio a = synthLoop(90, 4, 0);
        const int add = (int)(extra * 60.0 / 90 * kRate);
        if (add > 0) { a.L.insert(a.L.end(), (size_t)add, 0.f); a.R.insert(a.R.end(), (size_t)add, 0.f); }
        else { a.L.resize(a.L.size() + add); a.R.resize(a.R.size() + add); }
        auto info = analyse(a, "groove_90bpm.wav");
        CHECK(info.tempo.beats == 16, "%+.1f temps : %d temps au lieu de 16", extra, info.tempo.beats);
        std::vector<Stem> st;
        separate(a, "groove_90bpm.wav", st);
        Looper l(kRate);
        l.load(info, st);
        l.setPresetBPM(true);
        CHECK(std::abs(l.loopLength() - 16 * 60.0 / 90 * kRate) < 2, "%+.1f temps : boucle de %.0f", extra, l.loopLength());
    }
}

static void testPrecision() {
    current = "précision : loop à un tempo non entier (95,6 BPM)";
    for (double bpm : {95.6, 87.3, 121.5}) {
        Audio a = synthLoop(bpm, 4, 0);  // coupée pile : 16 temps exactement
        auto info = analyse(a, "groove.wav");
        const double exactLen = 16 * 60.0 / bpm * kRate;
        CHECK(info.tempo.beats == 16 && std::abs(info.tempo.bpm - bpm) < 0.02, "%.1f BPM : trouvé %.3f, %d temps", bpm,
              info.tempo.bpm, info.tempo.beats);
        std::vector<Stem> st;
        separate(a, "groove.wav", st);
        Looper l(kRate);
        l.load(info, st);
        l.setPresetBPM(true);
        CHECK(std::abs(l.loopLength() - a.frames()) < 1.5, "%.1f BPM : boucle de %.1f au lieu de %d échantillons", bpm,
              l.loopLength(), a.frames());
        (void)exactLen;
    }
    current = "précision : le nom dit 96 mais la loop est coupée à 95,6";
    {
        Audio a = synthLoop(95.6, 4, 0);
        auto info = analyse(a, "groove_96bpm.wav");
        CHECK(std::abs(info.tempo.bpm - 95.6) < 0.02 && info.tempo.beats == 16, "tempo %.3f (%d temps)", info.tempo.bpm, info.tempo.beats);
    }
    current = "précision : silence d'encodeur en trop → tempo entier, silence retiré";
    {
        Audio a = synthLoop(100, 2, 0);
        a.L.insert(a.L.end(), 900, 0.f);
        a.R.insert(a.R.end(), 900, 0.f);
        auto info = analyse(a, "loop.wav");
        CHECK(info.tempo.bpm == 100 && info.tempo.beats == 8, "tempo %.3f (%d temps)", info.tempo.bpm, info.tempo.beats);
    }
}

static void testTracksAndCrossfade() {
    current = "pistes séparées pour les effets (somme = mélange)";
    Audio a = synthLoop(98, 1, 0);
    std::vector<Stem> st;
    auto info = separate(a, "x.wav", st);
    Looper l1(kRate), l2(kRate);
    l1.load(info, st);
    l2.load(info, st);
    MixSettings s;
    s.globalBPM = 98;
    s.playing = true;
    s.metronome = true;
    s.tracks[1].gain = 0.5f;
    s.tracks[2].pan = -0.7f;
    const int n = 20000;
    std::vector<float> L(n), R(n);
    l1.process(s, L.data(), R.data(), n);
    std::vector<std::vector<float>> tl(kNumTracks, std::vector<float>(n)), tr(kNumTracks, std::vector<float>(n));
    std::vector<float> cl(n), cr(n);
    std::array<Bus, kNumTracks> buses;
    for (int i = 0; i < kNumTracks; ++i) buses[(size_t)i] = {tl[(size_t)i].data(), tr[(size_t)i].data()};
    l2.processTracks(s, buses, {cl.data(), cr.data()}, n);
    double d = 0;
    for (int j = 0; j < n; ++j) {
        float sl = cl[(size_t)j];
        for (int i = 0; i < kNumTracks; ++i) sl += tl[(size_t)i][(size_t)j];
        d = std::max(d, (double)std::abs(sl - L[(size_t)j]));
    }
    CHECK(d < 1e-5, "la somme des pistes séparées diffère du mélange (%g)", d);
    CHECK(energy(cl, 0, n) > 0.01, "le métronome n'est pas dans son tampon à part");

    current = "fondu enchaîné au changement de loop";
    Audio b = synthLoop(98, 1, 0);
    for (auto& v : b.L) v = 0;  // b silencieuse : pendant le fondu, a s'éteint progressivement
    for (auto& v : b.R) v = 0;
    std::vector<Stem> sb;
    LoopInfo ib = info;
    sb.push_back({"LOOP", std::make_shared<Audio>(b)});
    Looper l(kRate);
    l.load(info, st);
    s.metronome = false;
    s.crossfade = 0.2;  // 200 ms
    const int beat = 27000;
    std::vector<float> o((size_t)beat * 2), o2(o.size());
    l.process(s, o.data(), o2.data(), (int)o.size());  // 2 temps
    l.load(ib, sb);                                     // entre au temps 4 (mesure suivante)
    std::vector<float> p((size_t)beat * 3), p2(p.size());
    l.process(s, p.data(), p2.data(), (int)p.size());  // temps 2 à 5
    const int at = 2 * beat;  // temps 4 dans ce bloc
    const int half = (int)(0.1 * kRate);
    double ref = energy(a.L, 0, 2000), mid = 0, after = energy(p, at + (int)(0.25 * kRate), at + (int)(0.25 * kRate) + 2000);
    for (int j = at + half; j < at + half + 2000; ++j) mid += (double)a.L[(size_t)(j - at)] * a.L[(size_t)(j - at)];
    CHECK(energy(p, at + half, at + half + 2000) > 0.2 * mid && energy(p, at + half, at + half + 2000) < 0.9 * mid + 1e-9,
          "au milieu du fondu, l'ancienne loop doit être à moitié");
    CHECK(after < 1e-6, "après le fondu, l'ancienne loop s'entend encore");
    (void)ref;
}

static void testProcess() {
    current = "réglages par bloc (plugin)";
    Audio a = synthLoop(98, 1, 0);
    std::vector<Stem> stems;
    auto info = separate(a, "x.wav", stems);
    Looper l(kRate);
    l.load(info, stems);
    MixSettings s;
    s.globalBPM = 98;  // 1 temps = 27000 échantillons pile
    s.playing = true;
    const int n = a.frames();
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; i += 512) l.process(s, L.data() + i, R.data() + i, std::min(512, n - i));
    int bad = -1;
    for (int i = 0; i < n && bad < 0; ++i)
        if (std::abs(L[i] - a.L[i]) > 1e-3) bad = i;
    CHECK(bad < 0, "lecture par blocs différente (échantillon %d)", bad);
    // calage sur l'hôte : au temps 2 de l'hôte, on doit entendre le temps 2 de la loop
    s.hostClock = true;
    s.hostBeats = 1.0;
    std::vector<float> o(1100), o2(1100);
    l.process(s, o.data(), o2.data(), 1100);
    int s2 = 27000;
    double diff = 0;
    for (int i = 0; i < 1100; ++i) diff = std::max(diff, (double)std::abs(o[i] - a.L[s2 + i]));
    CHECK(diff < 1e-3, "pas calé sur la position de l'hôte (écart %g)", diff);
    s.playing = false;
    l.process(s, o.data(), o2.data(), 1100);
    CHECK(energy(o, 0, 1100) == 0 && !l.isPlaying(), "ne s'arrête pas");
}

int main() {
    testTempoFromLength();
    testTempoVarious();
    testTempoAnalysisOnly();
    testFilename();
    testWAVAndFileTempo();
    testPercussions();
    testSeparation();
    testLibrary();
    testLooper();
    testBadFiles();
    testContinuousLoop();
    testBounceAndMetronome();
    testProcess();
    testTracksAndCrossfade();
    testPrecision();
    testSeamlessFiles();
    testSwitchOnNextBar();
    testMeters();
    testHostClock();
    if (failures) {
        std::printf("%d test(s) en échec\n", failures);
        return 1;
    }
    std::printf("Tous les tests passent.\n");
    return 0;
}
