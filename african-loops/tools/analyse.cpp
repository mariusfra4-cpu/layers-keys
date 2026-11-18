// africanloops-analyse : outil du moteur African Loops.
//
//   africanloops-analyse <dossier>
//       analyse et classe toutes les loops du dossier
//   africanloops-analyse --separer <loop.wav> [--sortie <dossier>] [--tempo 120]
//       écrit une piste WAV par percussion (et la loop rejouée au tempo demandé)
#include "AfricanLoops/AfricanLoops.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace africanloops;
namespace fs = std::filesystem;

static bool write(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return (bool)f;
}

static int separateCmd(const std::string& path, const std::string& outDir, double tempo) {
    Audio a;
    std::string err;
    if (!loadWAV(path, a, err)) {
        std::fprintf(stderr, "Erreur : %s\n", err.c_str());
        return 1;
    }
    std::vector<Stem> stems;
    LoopInfo info = separate(a, path, stems);
    fs::create_directories(outDir);
    std::string base = fs::path(path).stem().string();
    std::printf("%s : %.2f BPM (%s), %d temps, %.2f s\n", base.c_str(), info.tempo.bpm, info.tempo.source.c_str(),
                info.tempo.beats, info.seconds);
    for (const auto& s : stems) {
        std::string name = s.kind;
        for (auto& c : name)
            if (c == '/' || c == ' ') c = '-';
        fs::path p = fs::path(outDir) / (base + "_" + name + ".wav");
        if (!write(p, encodeWAV(*s.audio, info.tempo.bpm, info.tempo.beats))) return 1;
        std::printf("  piste : %s\n", p.string().c_str());
    }
    if (tempo > 0 && info.tempo.bpm > 0) {
        Looper l(a.rate);
        l.load(info, stems);
        l.setGlobalBPM(tempo);
        l.play();
        int n = (int)(a.frames() * info.tempo.bpm / tempo * 2);  // deux tours
        Audio r;
        r.rate = a.rate;
        r.L.resize(n);
        r.R.resize(n);
        l.render(r.L.data(), r.R.data(), n);
        fs::path p = fs::path(outDir) / (base + "_a_" + std::to_string((int)tempo) + "BPM.wav");
        if (!write(p, encodeWAV(r, tempo, 0))) return 1;
        std::printf("  au nouveau tempo : %s\n", p.string().c_str());
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string sep, out = "pistes", dir;
    double tempo = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--separer" && i + 1 < argc) sep = argv[++i];
        else if (a == "--sortie" && i + 1 < argc) out = argv[++i];
        else if (a == "--tempo" && i + 1 < argc) tempo = std::atof(argv[++i]);
        else dir = a;
    }
    if (!sep.empty()) return separateCmd(sep, out, tempo);
    if (dir.empty()) {
        std::fprintf(stderr, "Utilisation : africanloops-analyse <dossier de loops>\n"
                             "          ou africanloops-analyse --separer <loop.wav> [--sortie <dossier>] [--tempo 120]\n");
        return 2;
    }
    Library lib;
    std::string err;
    bool ok = Library::scan(dir, lib, err, [](int done, int total, const std::string& cur) {
        std::fprintf(stderr, "\rAnalyse %d / %d  %-60.60s", done, total, cur.c_str());
    });
    std::fprintf(stderr, "\n");
    if (!ok) {
        std::fprintf(stderr, "Erreur : %s\n", err.c_str());
        return 1;
    }
    for (const auto& c : lib.categories()) {
        std::printf("\n== %s (%d) ==\n", c.name.c_str(), c.count);
        for (const Entry* e : lib.search(c.name, "")) {
            std::string name = fs::path(e->path).filename().string();
            if (!e->error.empty()) {
                std::printf("  %-44.44s  ERREUR : %s\n", name.c_str(), e->error.c_str());
                continue;
            }
            const auto& t = e->info.tempo;
            std::string kinds;
            for (const auto& p : e->info.percussions) kinds += (kinds.empty() ? "" : ", ") + p.kind;
            std::string bars = t.bars > 0 ? std::to_string(t.bars) + " mes." : "";
            std::printf("  %-44.44s %7.2f BPM (%-7s %3.0f%%) %-7s %s\n", name.c_str(), t.bpm, (t.source + ")").c_str(),
                        100 * t.confidence, bars.c_str(), kinds.c_str());
        }
    }
    std::printf("\nRésultats gardés dans %s\n", (fs::path(lib.root) / kIndexFile).string().c_str());
    return 0;
}
