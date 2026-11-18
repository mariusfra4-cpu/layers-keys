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
    writeFile(dir / "notes.txt", {'x'});
    Library lib;
    std::string err;
    int calls = 0;
    CHECK(Library::scan(dir.string(), lib, err, [&](int, int, const std::string&) { ++calls; }), "scan : %s", err.c_str());
    CHECK(lib.entries.size() == 4 && calls == 4, "%zu loops, %d analyses", lib.entries.size(), calls);
    std::map<std::string, std::string> want = {
        {"Worship/Praise Drums.wav", "Worship"}, {"I Love/Soft 01.wav", "I Love"},
        {"love_groove_90bpm.wav", "Love"}, {"Djembe solo.wav", kUncategorized}};
    for (auto& e : lib.entries) {
        CHECK(want[e.path] == e.category, "%s classé dans « %s »", e.path.c_str(), e.category.c_str());
        if (e.path == "love_groove_90bpm.wav") CHECK(e.info.tempo.bpm == 90, "BPM du nom ignoré : %.2f", e.info.tempo.bpm);
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
    fs::remove_all(dir);

    current = "styles";
    CHECK(categoryOf("loops_coupé-décalé_01.wav") == "Coupé-Décalé", "%s", categoryOf("loops_coupé-décalé_01.wav").c_str());
    CHECK(categoryOf("Adoration_Talking.wav") == "Worship", "adoration");
    CHECK(categoryOf("I Love\\x.wav") == "I Love", "chemin Windows");
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
    if (failures) {
        std::printf("%d test(s) en échec\n", failures);
        return 1;
    }
    std::printf("Tous les tests passent.\n");
    return 0;
}
