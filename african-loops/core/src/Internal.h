// Outils internes du moteur (analyse en fréquences, coups).
#pragma once

#include "AfricanLoops/AfricanLoops.h"

#include <complex>

namespace africanloops::detail {

constexpr int kFFT = 2048;
constexpr int kHop = 512;                  // 4 tranches se chevauchent : reconstruction exacte
constexpr int kFrameStart = -(kFFT - kHop);  // chaque échantillon est couvert par 4 tranches
constexpr int kBins = kFFT / 2 + 1;

enum Band { Sub, Body, Mid, Pres, Air, NumBands };  // <120, 120–500, 500–2500, 2500–7000, >7000 Hz
using BandArray = std::array<double, NumBands>;
using Spectrum = std::vector<std::complex<double>>;

inline int frameCenter(int t) { return t * kHop + kFrameStart + kFFT / 2; }
int frameAt(double sample);
inline double binHz(int k, int rate) { return (double)k * rate / kFFT; }
int bandOf(double hz);

std::vector<Spectrum> stft(const std::vector<float>& x);
std::vector<float> istft(const std::vector<Spectrum>& spec, int n);

struct Analysis {
    int rate = 0, n = 0;
    std::vector<Spectrum> specL, specR;
    std::vector<std::vector<double>> mag;  // amplitude du mélange
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
std::vector<Stem> separateStems(const Analysis& an, const std::vector<Percussion>& percs,
                                const std::vector<Hit>& hits);
Tempo detectTempo(const Audio& a, const Analysis& an, const std::vector<Hit>& hits,
                  const std::string& filename);

inline double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

}  // namespace africanloops::detail
