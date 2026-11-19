// Outils internes du moteur (analyse en fréquences, coups).
#pragma once

#include "AfricanLoops/AfricanLoops.h"

#include <complex>
#include <filesystem>

namespace africanloops::detail {

constexpr int kFFT = 2048;
constexpr int kHop = 512;                  // 4 tranches se chevauchent : reconstruction exacte
constexpr int kFrameStart = -(kFFT - kHop);  // chaque échantillon est couvert par 4 tranches
constexpr int kBins = kFFT / 2 + 1;

enum Band { Sub, Body, Mid, Pres, Air, NumBands };  // <120, 120–500, 500–2500, 2500–7000, >7000 Hz
using BandArray = std::array<double, NumBands>;

inline int frameCenter(int t) { return t * kHop + kFrameStart + kFFT / 2; }
int frameAt(double sample);
inline double binHz(int k, int rate) { return (double)k * rate / kFFT; }
int bandOf(double hz);


struct Analysis {
    int rate = 0, n = 0;
    std::vector<std::vector<float>> mag;   // amplitude du mélange (mono), par tranche
    std::vector<BandArray> bands;          // énergie par bande
    std::vector<BandArray> bflux;          // montée d'énergie par bande
    std::vector<double> flux;              // courbe d'attaques
    std::vector<int> onsets;               // tranches où un coup commence
    std::vector<int> starts;               // échantillon exact de chaque coup
    std::vector<float> mono;
    std::vector<int> bandOfBin;
};

Analysis analyseSpectrum(const Audio& a);
std::vector<Hit> detectHits(const Analysis& an);
std::vector<Percussion> summarize(const std::vector<Hit>& hits);
// Séparation rapide, directement sur le son (sans analyse en fréquences) :
// découpage en 5 bandes dont la somme redonne exactement la loop, puis chaque
// bande est partagée entre les percussions qui jouent à cet instant.
std::vector<Stem> separateStems(const Audio& a, const std::vector<Percussion>& percs, const std::vector<Hit>& hits);
Tempo detectTempo(const Audio& a, const Analysis& an, const std::vector<Hit>& hits,
                  const std::string& filename);

inline double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Chemin de fichier écrit en UTF-8 (accents) → chemin du système. Indispensable
// sous Windows, où un simple std::string serait lu dans l'ancien jeu de caractères.
inline std::filesystem::path pathFromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}
inline std::string pathToUtf8(const std::filesystem::path& p) {
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace africanloops::detail
